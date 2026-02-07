#include "Assets/ShaderAsset.h"

#include "Assets/AssetBundle.h"
#include "Assets/AssetPaths.h"
#include "Assets/AssetUtils.h"
#include "Assets/AssetManager.h"
#include "Assets/AssetLoadCoordinator.h"

#include "Core/Debug/Log.h"
#include "Graphics/Renderer.h"

#include <filesystem>
#include <future>
#include <fstream>
#include <sstream>

namespace Limitless::Assets
{
    struct ParsedShaderStages
    {
        std::string Name;
        std::string Vertex;
        std::string Fragment;
    };

    static Result<ParsedShaderStages> ParseCombinedGlsl(const std::string& key, const std::string& resolvedPath, const std::string& fileText, const ShaderAsset::Settings& settings)
    {
        // Format:
        //   #type vertex
        //   ...
        //   #type fragment
        //   ...
        auto findStage = [&](const std::string& stage) -> size_t {
            const std::string tag = "#type " + stage;
            return fileText.find(tag);
        };

        const size_t vPos = findStage("vertex");
        const size_t fPos = findStage("fragment");
        if (vPos == std::string::npos || fPos == std::string::npos)
        {
            return Result<ParsedShaderStages>(ErrorCode::ResourceFormatNotSupported,
                                             "Shader file must contain '#type vertex' and '#type fragment': " + resolvedPath);
        }

        auto sliceStage = [&](size_t stagePos) -> std::pair<std::string, size_t> {
            const size_t lineEnd = fileText.find('\n', stagePos);
            const size_t bodyStart = (lineEnd == std::string::npos) ? fileText.size() : (lineEnd + 1);
            return {fileText.substr(bodyStart), bodyStart};
        };

        // Determine ordering so we can cut stage bodies cleanly.
        ParsedShaderStages out;
        if (!settings.Name.empty())
        {
            out.Name = settings.Name;
        }
        else
        {
            out.Name = std::filesystem::path(key).stem().string();
            if (out.Name.empty())
            {
                out.Name = std::filesystem::path(resolvedPath).stem().string();
            }
        }

        if (vPos < fPos)
        {
            const size_t vLineEnd = fileText.find('\n', vPos);
            const size_t vBodyStart = (vLineEnd == std::string::npos) ? fileText.size() : (vLineEnd + 1);
            out.Vertex = fileText.substr(vBodyStart, fPos - vBodyStart);

            const size_t fLineEnd = fileText.find('\n', fPos);
            const size_t fBodyStart = (fLineEnd == std::string::npos) ? fileText.size() : (fLineEnd + 1);
            out.Fragment = fileText.substr(fBodyStart);
        }
        else
        {
            const size_t fLineEnd = fileText.find('\n', fPos);
            const size_t fBodyStart = (fLineEnd == std::string::npos) ? fileText.size() : (fLineEnd + 1);
            out.Fragment = fileText.substr(fBodyStart, vPos - fBodyStart);

            const size_t vLineEnd = fileText.find('\n', vPos);
            const size_t vBodyStart = (vLineEnd == std::string::npos) ? fileText.size() : (vLineEnd + 1);
            out.Vertex = fileText.substr(vBodyStart);
        }

        if (out.Vertex.empty() || out.Fragment.empty())
        {
            return Result<ParsedShaderStages>(ErrorCode::ResourceCorrupted, "Shader stage source was empty: " + resolvedPath);
        }

        return out;
    }

    Async::Task<ShaderAsset::Ptr> ShaderAsset::LoadAsync(const std::string& key, Settings settings)
    {
        return LoadAsync(key, std::move(settings), AssetLoadCoordinator::GetGeneration());
    }

    Async::Task<ShaderAsset::Ptr> ShaderAsset::LoadAsync(const std::string& key, Settings settings, uint64_t generation)
    {
        // Two-stage async:
        // - CPU stage (AsyncIO): resolve path + read file + parse stage sources
        // - GPU stage (render thread): compile/link shader + fulfill promise
        std::promise<Ptr> promise;
        std::shared_future<Ptr> shared = promise.get_future().share();

        Async::GetAsyncIO().RunAsync([key, settings, generation, promise = std::move(promise)]() mutable -> void {
            try
            {
                if (!AssetLoadCoordinator::IsGenerationCurrent(generation))
                {
                    promise.set_value(nullptr);
                    return;
                }

                bool fromBundle = false;
                std::string resolvedPath;
                std::string guid;
                std::string fileText;

                auto& bundle = AssetBundle::GetInstance();
                if (bundle.IsEnabled() && bundle.IsLoaded())
                {
                    const auto entry = bundle.FindEntryByKey(key);
                    if (entry.has_value())
                    {
                        const auto textResult = bundle.ReadAllTextByKey(key);
                        if (textResult.IsSuccess())
                        {
                            fromBundle = true;
                            guid = entry->Guid;
                            resolvedPath = "<AssetBundle>";
                            fileText = textResult.GetValue();
                        }
                    }
                }

                if (!fromBundle)
                {
                    const auto resolvedResult = ResolveAssetKeyToPath(key);
                    if (resolvedResult.IsFailure())
                    {
                        LT_CORE_ERROR("ShaderAsset::LoadAsync: failed to resolve key '{}': {}",
                                      key, resolvedResult.GetError().GetErrorMessage());
                        promise.set_value(nullptr);
                        return;
                    }

                    resolvedPath = resolvedResult.GetValue().string();

                    // Ensure GUID `.meta` next to real file.
                    const auto guidResult = LoadOrCreateGuid(resolvedPath, {{"key", key}, {"type", "Shader"}});
                    if (guidResult.IsFailure())
                    {
                        LT_CORE_ERROR("ShaderAsset::LoadAsync: meta GUID failed for '{}': {}",
                                      resolvedPath, guidResult.GetError().GetErrorMessage());
                        promise.set_value(nullptr);
                        return;
                    }
                    guid = guidResult.GetValue();

                    // Read file on this worker.
                    std::ifstream in(resolvedPath, std::ios::in | std::ios::binary);
                    if (!in.is_open())
                    {
                        LT_CORE_ERROR("ShaderAsset::LoadAsync: failed to open '{}'", resolvedPath);
                        promise.set_value(nullptr);
                        return;
                    }

                    std::ostringstream ss;
                    ss << in.rdbuf();
                    fileText = ss.str();
                }

                const auto parsedResult = ParseCombinedGlsl(key, resolvedPath, fileText, settings);
                if (parsedResult.IsFailure())
                {
                    LT_CORE_ERROR("ShaderAsset::LoadAsync: parse failed for '{}': {}",
                                  resolvedPath, parsedResult.GetError().GetErrorMessage());
                    promise.set_value(nullptr);
                    return;
                }

                ParsedShaderStages parsed = parsedResult.GetValue();

                auto& renderer = Renderer::GetInstance();
                if (!renderer.IsRenderThreadEnabled())
                {
                    LT_CORE_ERROR("ShaderAsset::LoadAsync: render thread must be enabled for GPU shader compilation");
                    promise.set_value(nullptr);
                    return;
                }

                struct SharedState
                {
                    std::promise<Ptr> promise;
                    std::string key;
                    std::string guid;
                    ParsedShaderStages parsed;
                    Settings settings;
                    uint64_t generation = 0;
                };

                auto state = std::make_shared<SharedState>();
                state->promise = std::move(promise);
                state->key = key;
                state->guid = guid;
                state->parsed = std::move(parsed);
                state->settings = settings;
                state->generation = generation;

                class Command final : public RenderResourceCommandQueue::Command
                {
                public:
                    explicit Command(std::shared_ptr<SharedState> s)
                        : m_State(std::move(s))
                    {
                    }

                    void Execute(GraphicsContext*) override
                    {
                        try
                        {
                            if (!AssetLoadCoordinator::IsGenerationCurrent(m_State->generation))
                            {
                                m_State->promise.set_value(nullptr);
                                return;
                            }

                            auto shader = Shader::CreateFromSource(m_State->parsed.Name, m_State->parsed.Vertex, m_State->parsed.Fragment);
                            if (!shader)
                            {
                                m_State->promise.set_value(nullptr);
                                return;
                            }

                            // Ensure it is visible to the global cache for hot reload (key -> asset).
                            auto asset = AssetManager::GetOrLoad<ShaderAsset>(m_State->key, [&]() -> Ptr {
                                // Keep constructor private.
                                return Ptr(new ShaderAsset(m_State->key, m_State->guid, std::move(shader), m_State->settings));
                            });

                            m_State->promise.set_value(std::move(asset));
                        }
                        catch (...)
                        {
                            // Never throw across the task boundary.
                            try { m_State->promise.set_value(nullptr); } catch (...) {}
                        }
                    }

                private:
                    std::shared_ptr<SharedState> m_State;
                };

                if (!renderer.SubmitResource(std::make_unique<Command>(state)))
                {
                    LT_CORE_ERROR("ShaderAsset::LoadAsync: RenderResourceCommandQueue full while compiling '{}'", resolvedPath);
                    state->promise.set_value(nullptr);
                    return;
                }
            }
            catch (const std::exception& e)
            {
                LT_CORE_ERROR("ShaderAsset::LoadAsync: exception while loading '{}': {}", key, e.what());
                try { promise.set_value(nullptr); } catch (...) {}
            }
            catch (...)
            {
                LT_CORE_ERROR("ShaderAsset::LoadAsync: unknown exception while loading '{}'", key);
                try { promise.set_value(nullptr); } catch (...) {}
            }
        });

        return Async::Task<Ptr>(std::move(shared));
    }

    ShaderAsset::Ptr ShaderAsset::LoadBlocking(const std::string& key, Settings settings)
    {
        auto task = LoadAsync(key, std::move(settings));
        task.Wait();
        return task.Get();
    }

    bool ShaderAsset::Reload()
    {
        // IMPORTANT:
        // Reload must recompile in-place. Calling LoadBlocking() would go through the cache and
        // typically return this same instance without rebuilding GPU resources.
        const std::string key = GetKey();

        bool fromBundle = false;
        std::string resolvedPath;
        std::string fileText;

        auto& bundle = AssetBundle::GetInstance();
        if (bundle.IsEnabled() && bundle.IsLoaded())
        {
            const auto entry = bundle.FindEntryByKey(key);
            if (entry.has_value())
            {
                const auto textResult = bundle.ReadAllTextByKey(key);
                if (textResult.IsSuccess())
                {
                    fromBundle = true;
                    resolvedPath = "<AssetBundle>";
                    fileText = textResult.GetValue();
                }
            }
        }

        if (!fromBundle)
        {
            const auto resolvedResult = ResolveAssetKeyToPath(key);
            if (resolvedResult.IsFailure())
            {
                LT_CORE_ERROR("ShaderAsset::Reload: failed to resolve key '{}': {}", key, resolvedResult.GetError().GetErrorMessage());
                return false;
            }
            resolvedPath = resolvedResult.GetValue().string();

            // Read file (synchronous on the calling thread - ok for hot reload).
            std::ifstream in(resolvedPath, std::ios::in | std::ios::binary);
            if (!in.is_open())
            {
                LT_CORE_ERROR("ShaderAsset::Reload: failed to open '{}'", resolvedPath);
                return false;
            }
            std::ostringstream ss;
            ss << in.rdbuf();
            fileText = ss.str();
        }

        const auto parsedResult = ParseCombinedGlsl(key, resolvedPath, fileText, m_Settings);
        if (parsedResult.IsFailure())
        {
            LT_CORE_ERROR("ShaderAsset::Reload: parse failed for '{}': {}", resolvedPath, parsedResult.GetError().GetErrorMessage());
            return false;
        }

        const ParsedShaderStages parsed = parsedResult.GetValue();

        auto& renderer = Renderer::GetInstance();
        if (!renderer.IsRenderThreadEnabled())
        {
            LT_CORE_ERROR("ShaderAsset::Reload: render thread must be enabled for GPU shader compilation");
            return false;
        }

        // Compile on render thread and swap.
        std::shared_ptr<Shader> compiled;
        try
        {
            compiled = renderer.SubmitResourceAndWait([&](GraphicsContext*) -> std::shared_ptr<Shader> {
                return Shader::CreateFromSource(parsed.Name, parsed.Vertex, parsed.Fragment);
            });
        }
        catch (const std::exception& e)
        {
            LT_CORE_ERROR("ShaderAsset::Reload: compile threw for '{}': {}", resolvedPath, e.what());
            return false;
        }
        catch (...)
        {
            LT_CORE_ERROR("ShaderAsset::Reload: compile threw (unknown) for '{}'", resolvedPath);
            return false;
        }

        if (!compiled)
        {
            LT_CORE_ERROR("ShaderAsset::Reload: compile failed for '{}'", resolvedPath);
            return false;
        }

        m_Shader = std::move(compiled);
        return true;
    }
}

