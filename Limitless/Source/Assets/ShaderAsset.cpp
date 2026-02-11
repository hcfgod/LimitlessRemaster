#include "Assets/ShaderAsset.h"

#include "Assets/AssetBundle.h"
#include "Assets/AssetLoadProgress.h"
#include "Assets/AssetPaths.h"
#include "Assets/AssetUtils.h"
#include "Assets/AssetManager.h"
#include "Assets/AssetLoadCoordinator.h"
#include "Assets/ShaderStageParsing.h"
#include "Assets/Cooking/CookedShaderStagesFormat.h"

#include "Core/Debug/Log.h"
#include "Graphics/GraphicsAPIDetector.h"
#include "Graphics/Renderer.h"
#include "Graphics/ShaderCompilation/ShaderCompiler.h"

#include <filesystem>
#include <atomic>
#include <future>
#include <fstream>
#include <sstream>
#include <vector>

namespace Limitless::Assets
{
#if 0
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

    static Result<ParsedShaderStages> PrepareShaderStagesForActiveGraphicsAPI(ParsedShaderStages parsed, const std::string& debugPath)
    {
#if defined(LT_ENABLE_SHADERC)
        // Today we only have an OpenGL runtime backend. We still run the source through
        // shaderc to compile GLSL -> SPIR-V on the CPU, then use SPIRV-Cross to reflect.
        //
        // IMPORTANT:
        // We intentionally do NOT feed SPIRV-Cross-generated GLSL back into the OpenGL runtime
        // compiler yet. The material/shader system relies on stable uniform names (e.g.
        // "u_ViewProjection", "u_Model") and SPIRV-Cross output can restructure/rename uniforms
        // (UBOs, flattened structs, etc.), causing silent "uniform not found" behavior.
        //
        // The runtime backend still consumes the *authored* GLSL, while SPIR-V is used for:
        // - validation (catching mistakes earlier)
        // - reflection/tooling (resource summaries today; pipeline layout later)
        const GraphicsAPI api = GraphicsAPIDetector::GetBestAPI().value_or(GraphicsAPI::OpenGL);
        if (api == GraphicsAPI::OpenGL)
        {
            const std::string vertexName = parsed.Name + " (vertex) " + debugPath;
            const std::string fragmentName = parsed.Name + " (fragment) " + debugPath;

            const auto vertexSpirvResult = ShaderCompilation::CompileGlslToSpirv(
                ShaderCompilation::ShaderStage::Vertex,
                parsed.Vertex,
                vertexName);
            if (vertexSpirvResult.IsFailure())
            {
                return Result<ParsedShaderStages>(vertexSpirvResult.GetError());
            }

            const auto fragmentSpirvResult = ShaderCompilation::CompileGlslToSpirv(
                ShaderCompilation::ShaderStage::Fragment,
                parsed.Fragment,
                fragmentName);
            if (fragmentSpirvResult.IsFailure())
            {
                return Result<ParsedShaderStages>(fragmentSpirvResult.GetError());
            }

            ShaderCompilation::DebugLogSpirvResourceSummary(vertexSpirvResult.GetValue(), vertexName);
            ShaderCompilation::DebugLogSpirvResourceSummary(fragmentSpirvResult.GetValue(), fragmentName);
        }
#else
        (void)debugPath;
#endif

        return parsed;
    }
#endif

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
                AssetLoadProgress::SetProgress(key, 0.05f, "Resolving...");

                if (!AssetLoadCoordinator::IsGenerationCurrent(generation))
                {
                    AssetLoadProgress::ClearProgress(key);
                    promise.set_value(nullptr);
                    return;
                }

                bool fromBundle = false;
                AssetBundlePayloadFormat bundlePayloadFormat = AssetBundlePayloadFormat::Raw;
                std::vector<uint8_t> bundleBytes;
                std::string resolvedPath;
                std::string guid;
                std::string fileText;
                ParsedShaderStages parsed{};

                auto& bundle = AssetBundle::GetInstance();
                if (bundle.IsEnabled() && bundle.IsLoaded())
                {
                    const auto entry = bundle.FindEntryByKey(key);
                    if (entry.has_value())
                    {
                        bundlePayloadFormat = entry->PayloadFormat;
                        if (bundlePayloadFormat == AssetBundlePayloadFormat::CookedShaderStages)
                        {
                            const auto bytesResult = bundle.ReadAllBytesByKey(key);
                            if (bytesResult.IsSuccess())
                            {
                                fromBundle = true;
                                guid = entry->Guid;
                                resolvedPath = "<AssetBundle>";
                                bundleBytes = bytesResult.GetValue();
                                AssetLoadProgress::SetProgress(key, 0.20f, "Reading from bundle...");
                            }
                        }
                        else
                        {
                            const auto textResult = bundle.ReadAllTextByKey(key);
                            if (textResult.IsSuccess())
                            {
                                fromBundle = true;
                                guid = entry->Guid;
                                resolvedPath = "<AssetBundle>";
                                fileText = textResult.GetValue();
                                AssetLoadProgress::SetProgress(key, 0.20f, "Reading from bundle...");
                            }
                        }
                    }
                }

                if (!fromBundle)
                {
                    const auto resolvedResult = ResolveAssetKeyToPath(key);
                    if (resolvedResult.IsFailure())
                    {
                        AssetLoadProgress::ClearProgress(key);
                        LT_CORE_ERROR("ShaderAsset::LoadAsync: failed to resolve key '{}': {}",
                                      key, resolvedResult.GetError().GetErrorMessage());
                        promise.set_value(nullptr);
                        return;
                    }

                    resolvedPath = resolvedResult.GetValue().string();
                    AssetLoadProgress::SetProgress(key, 0.12f, "Reading source...");

                    // Ensure GUID `.meta` next to real file.
                    const auto guidResult = LoadOrCreateGuid(resolvedPath, {{"key", key}, {"type", "Shader"}});
                    if (guidResult.IsFailure())
                    {
                        AssetLoadProgress::ClearProgress(key);
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
                        AssetLoadProgress::ClearProgress(key);
                        LT_CORE_ERROR("ShaderAsset::LoadAsync: failed to open '{}'", resolvedPath);
                        promise.set_value(nullptr);
                        return;
                    }

                    std::ostringstream ss;
                    ss << in.rdbuf();
                    fileText = ss.str();
                }

                AssetLoadProgress::SetProgress(key, 0.30f, "Parsing...");

                if (fromBundle && bundlePayloadFormat == AssetBundlePayloadFormat::CookedShaderStages)
                {
                    static std::atomic<bool> s_LoggedCookedShaderOnce{ false };
                    if (!s_LoggedCookedShaderOnce.exchange(true))
                    {
                        LT_CORE_INFO("ShaderAsset: using cooked shader stage payloads from AssetBundle (pre-split vertex/fragment)");
                    }

                    const auto cookedResult = ::Limitless::Assets::Cooking::ParseCookedShaderStages(bundleBytes.data(), bundleBytes.size());
                    if (cookedResult.IsFailure())
                    {
                        AssetLoadProgress::ClearProgress(key);
                        LT_CORE_ERROR("ShaderAsset::LoadAsync: cooked shader parse failed for '{}': {}",
                                      key, cookedResult.GetError().GetErrorMessage());
                        promise.set_value(nullptr);
                        return;
                    }

                    const auto cooked = cookedResult.GetValue();
                    parsed.Name = cooked.Name.empty() ? settings.Name : cooked.Name;
                    parsed.Vertex = cooked.Vertex;
                    parsed.Fragment = cooked.Fragment;
                }
                else
                {
                    const auto parsedResult = ParseCombinedGlsl(key, resolvedPath, fileText, settings.Name);
                    if (parsedResult.IsFailure())
                    {
                        AssetLoadProgress::ClearProgress(key);
                        LT_CORE_ERROR("ShaderAsset::LoadAsync: parse failed for '{}': {}",
                                      resolvedPath, parsedResult.GetError().GetErrorMessage());
                        promise.set_value(nullptr);
                        return;
                    }

                    parsed = parsedResult.GetValue();
                }

                AssetLoadProgress::SetProgress(key, 0.50f, "Compiling...");

                const auto preparedResult = PrepareShaderStagesForActiveGraphicsAPI(std::move(parsed), resolvedPath);
                if (preparedResult.IsFailure())
                {
                    AssetLoadProgress::ClearProgress(key);
                    LT_CORE_ERROR("ShaderAsset::LoadAsync: shaderc/SPIRV-Cross preparation failed for '{}': {}",
                                  resolvedPath, preparedResult.GetError().GetErrorMessage());
                    promise.set_value(nullptr);
                    return;
                }

                parsed = preparedResult.GetValue();

                auto& renderer = Renderer::GetInstance();
                if (!renderer.IsRenderThreadEnabled())
                {
                    AssetLoadProgress::ClearProgress(key);
                    LT_CORE_ERROR("ShaderAsset::LoadAsync: render thread must be enabled for GPU shader compilation");
                    promise.set_value(nullptr);
                    return;
                }

                AssetLoadProgress::SetProgress(key, 0.75f, "Uploading to GPU...");

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

                    const char* GetDebugName() const override
                    {
                        return "ShaderAsset/CreateShader";
                    }

                    void Execute(GraphicsContext*) override
                    {
                        try
                        {
                            if (!AssetLoadCoordinator::IsGenerationCurrent(m_State->generation))
                            {
                                AssetLoadProgress::ClearProgress(m_State->key);
                                m_State->promise.set_value(nullptr);
                                return;
                            }

                            auto shader = Shader::CreateFromSource(m_State->parsed.Name, m_State->parsed.Vertex, m_State->parsed.Fragment);
                            if (!shader)
                            {
                                AssetLoadProgress::ClearProgress(m_State->key);
                                m_State->promise.set_value(nullptr);
                                return;
                            }

                            // Ensure it is visible to the global cache for hot reload (key -> asset).
                            auto asset = AssetManager::GetOrLoad<ShaderAsset>(m_State->key, [&]() -> Ptr {
                                // Keep constructor private.
                                return Ptr(new ShaderAsset(m_State->key, m_State->guid, std::move(shader), m_State->settings));
                            });

                            AssetLoadProgress::ClearProgress(m_State->key);
                            m_State->promise.set_value(std::move(asset));
                        }
                        catch (...)
                        {
                            AssetLoadProgress::ClearProgress(m_State->key);
                            // Never throw across the task boundary.
                            try { m_State->promise.set_value(nullptr); } catch (...) {}
                        }
                    }

                private:
                    std::shared_ptr<SharedState> m_State;
                };

                if (!renderer.SubmitResource(std::make_unique<Command>(state)))
                {
                    AssetLoadProgress::ClearProgress(key);
                    LT_CORE_ERROR("ShaderAsset::LoadAsync: RenderResourceCommandQueue full while compiling '{}'", resolvedPath);
                    state->promise.set_value(nullptr);
                    return;
                }
            }
            catch (const std::exception& e)
            {
                AssetLoadProgress::ClearProgress(key);
                LT_CORE_ERROR("ShaderAsset::LoadAsync: exception while loading '{}': {}", key, e.what());
                try { promise.set_value(nullptr); } catch (...) {}
            }
            catch (...)
            {
                AssetLoadProgress::ClearProgress(key);
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
        AssetBundlePayloadFormat bundlePayloadFormat = AssetBundlePayloadFormat::Raw;
        std::string resolvedPath;
        std::string fileText;
        std::vector<uint8_t> bundleBytes;

        auto& bundle = AssetBundle::GetInstance();
        if (bundle.IsEnabled() && bundle.IsLoaded())
        {
            const auto entry = bundle.FindEntryByKey(key);
            if (entry.has_value())
            {
                bundlePayloadFormat = entry->PayloadFormat;
                if (bundlePayloadFormat == AssetBundlePayloadFormat::CookedShaderStages)
                {
                    const auto bytesResult = bundle.ReadAllBytesByKey(key);
                    if (bytesResult.IsSuccess())
                    {
                        fromBundle = true;
                        resolvedPath = "<AssetBundle>";
                        bundleBytes = bytesResult.GetValue();
                    }
                }
                else
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

        ParsedShaderStages parsed{};
        if (fromBundle && bundlePayloadFormat == AssetBundlePayloadFormat::CookedShaderStages)
        {
            const auto cookedResult = ::Limitless::Assets::Cooking::ParseCookedShaderStages(bundleBytes.data(), bundleBytes.size());
            if (cookedResult.IsFailure())
            {
                LT_CORE_ERROR("ShaderAsset::Reload: cooked shader parse failed for '{}': {}", key, cookedResult.GetError().GetErrorMessage());
                return false;
            }
            const auto cooked = cookedResult.GetValue();
            parsed.Name = cooked.Name.empty() ? m_Settings.Name : cooked.Name;
            parsed.Vertex = cooked.Vertex;
            parsed.Fragment = cooked.Fragment;
        }
        else
        {
            const auto parsedResult = ParseCombinedGlsl(key, resolvedPath, fileText, m_Settings.Name);
            if (parsedResult.IsFailure())
            {
                LT_CORE_ERROR("ShaderAsset::Reload: parse failed for '{}': {}", resolvedPath, parsedResult.GetError().GetErrorMessage());
                return false;
            }
            parsed = parsedResult.GetValue();
        }

        const auto preparedResult = PrepareShaderStagesForActiveGraphicsAPI(std::move(parsed), resolvedPath);
        if (preparedResult.IsFailure())
        {
            LT_CORE_ERROR("ShaderAsset::Reload: shaderc/SPIRV-Cross preparation failed for '{}': {}",
                          resolvedPath, preparedResult.GetError().GetErrorMessage());
            return false;
        }
        parsed = preparedResult.GetValue();

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
            compiled = renderer.SubmitResourceAndWait("ShaderAsset/Reload/CreateShader", [&](GraphicsContext*) -> std::shared_ptr<Shader> {
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

