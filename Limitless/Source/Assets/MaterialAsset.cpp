#include "Assets/MaterialAsset.h"

#include "Assets/AssetDatabase.h"
#include "Assets/AssetLoadCoordinator.h"
#include "Assets/AssetManager.h"
#include "Assets/AssetPaths.h"
#include "Assets/AssetUtils.h"
#include "Assets/ShaderAssetImporter.h"
#include "Assets/TextureAssetImporter.h"

#include "Core/Debug/Log.h"

#include "Graphics/RenderCommand.h"
#include "Graphics/Renderer.h"

#include <fstream>

namespace Limitless::Assets
{
    using json = nlohmann::json;

    static Result<std::shared_ptr<ShaderAsset>> ResolveShaderAsset(const json& root)
    {
        if (!root.contains("shader"))
        {
            return Result<std::shared_ptr<ShaderAsset>>(ErrorCode::InvalidArgument, "Material JSON missing 'shader'");
        }

        const json& ref = root["shader"];

        // Preferred: GUID (stable, Unity-style).
        if (ref.is_object() && ref.contains("guid") && ref["guid"].is_string())
        {
            const std::string guid = ref["guid"].get<std::string>();
            if (guid.empty())
            {
                return Result<std::shared_ptr<ShaderAsset>>(ErrorCode::InvalidArgument, "Material JSON 'shader' guid is empty");
            }

            // If already loaded, grab it.
            if (auto cached = AssetManager::GetByGuid<ShaderAsset>(guid))
            {
                return cached;
            }

            // Otherwise find key in DB and load.
            const auto rec = AssetDatabase::GetInstance().FindByGuid(guid);
            if (rec.IsFailure())
            {
                return Result<std::shared_ptr<ShaderAsset>>(rec.GetError());
            }

            return AssetManager::LoadBlocking<ShaderAsset>(rec.GetValue().Key);
        }

        // Convenience: key (path) during early iteration.
        if (ref.is_object() && ref.contains("key") && ref["key"].is_string())
        {
            const std::string key = ref["key"].get<std::string>();
            return AssetManager::LoadBlocking<ShaderAsset>(key);
        }

        return Result<std::shared_ptr<ShaderAsset>>(ErrorCode::InvalidArgument, "Material JSON 'shader' must contain {guid} or {key}");
    }

    static Result<std::shared_ptr<TextureAsset>> ResolveMainTextureAsset(const json& root)
    {
        if (!root.contains("mainTexture"))
        {
            // Optional.
            return std::shared_ptr<TextureAsset>();
        }

        const json& ref = root["mainTexture"];

        if (ref.is_object() && ref.contains("guid") && ref["guid"].is_string())
        {
            const std::string guid = ref["guid"].get<std::string>();
            if (guid.empty())
            {
                return Result<std::shared_ptr<TextureAsset>>(ErrorCode::InvalidArgument, "Material JSON 'mainTexture' guid is empty");
            }

            if (auto cached = AssetManager::GetByGuid<TextureAsset>(guid))
            {
                return cached;
            }

            const auto rec = AssetDatabase::GetInstance().FindByGuid(guid);
            if (rec.IsFailure())
            {
                return Result<std::shared_ptr<TextureAsset>>(rec.GetError());
            }

            return AssetManager::LoadBlocking<TextureAsset>(rec.GetValue().Key);
        }

        if (ref.is_object() && ref.contains("key") && ref["key"].is_string())
        {
            const std::string key = ref["key"].get<std::string>();
            return AssetManager::LoadBlocking<TextureAsset>(key);
        }

        return Result<std::shared_ptr<TextureAsset>>(ErrorCode::InvalidArgument, "Material JSON 'mainTexture' must contain {guid} or {key}");
    }

    static Result<TextureSpecification> ParseTextureSpecificationOverride(const json& root)
    {
        if (!root.contains("mainTextureSpec"))
        {
            return Result<TextureSpecification>(ErrorCode::ResourceNotFound, "No mainTextureSpec");
        }

        const json& s = root["mainTextureSpec"];
        if (!s.is_object())
        {
            return Result<TextureSpecification>(ErrorCode::InvalidArgument, "mainTextureSpec must be an object");
        }

        TextureSpecification spec{};

        auto parseFilter = [](const std::string& v) -> TextureFilter {
            if (v == "Nearest") return TextureFilter::Nearest;
            if (v == "Linear") return TextureFilter::Linear;
            return TextureFilter::Linear;
        };

        auto parseWrap = [](const std::string& v) -> TextureWrap {
            if (v == "Repeat") return TextureWrap::Repeat;
            if (v == "ClampToEdge") return TextureWrap::ClampToEdge;
            return TextureWrap::Repeat;
        };

        if (s.contains("minFilter") && s["minFilter"].is_string()) spec.MinFilter = parseFilter(s["minFilter"].get<std::string>());
        if (s.contains("magFilter") && s["magFilter"].is_string()) spec.MagFilter = parseFilter(s["magFilter"].get<std::string>());
        if (s.contains("wrapU") && s["wrapU"].is_string()) spec.WrapU = parseWrap(s["wrapU"].get<std::string>());
        if (s.contains("wrapV") && s["wrapV"].is_string()) spec.WrapV = parseWrap(s["wrapV"].get<std::string>());
        if (s.contains("generateMipmaps") && s["generateMipmaps"].is_boolean()) spec.GenerateMipmaps = s["generateMipmaps"].get<bool>();

        // FlipVerticallyOnLoad is a *load-time* thing; ignore in material overrides.
        spec.FlipVerticallyOnLoad = true;
        return spec;
    }

    Async::Task<MaterialAsset::Ptr> MaterialAsset::LoadAsync(const std::string& key, Settings settings)
    {
        // Materials are CPU-only to parse, then reference other assets.
        const uint64_t generation = AssetLoadCoordinator::GetGeneration();

        std::promise<Ptr> promise;
        std::shared_future<Ptr> shared = promise.get_future().share();

        Async::GetAsyncIO().RunAsync([key, settings, generation, promise = std::move(promise)]() mutable -> void {
            if (!AssetLoadCoordinator::IsGenerationCurrent(generation))
            {
                promise.set_value(nullptr);
                return;
            }

            try
            {
                const auto resolvedResult = ResolveAssetKeyToPath(key);
                if (resolvedResult.IsFailure())
                {
                    LT_CORE_ERROR("MaterialAsset::LoadAsync: failed to resolve key '{}': {}", key, resolvedResult.GetError().GetErrorMessage());
                    promise.set_value(nullptr);
                    return;
                }

                const std::string resolvedPath = resolvedResult.GetValue().string();

                const auto guidResult = LoadOrCreateGuid(resolvedPath, {{"key", key}, {"type", "Material"}});
                if (guidResult.IsFailure())
                {
                    LT_CORE_ERROR("MaterialAsset::LoadAsync: meta GUID failed for '{}': {}", resolvedPath, guidResult.GetError().GetErrorMessage());
                    promise.set_value(nullptr);
                    return;
                }

                const std::string guid = guidResult.GetValue();

                auto asset = AssetManager::GetOrLoad<MaterialAsset>(key, [&]() -> Ptr {
                    Ptr created(new MaterialAsset(key, guid, settings));
                    created->m_ResolvedPath = resolvedPath;
                    if (!created->LoadFromJsonFile())
                    {
                        return nullptr;
                    }
                    return created;
                });

                promise.set_value(std::move(asset));
            }
            catch (const std::exception& e)
            {
                LT_CORE_ERROR("MaterialAsset::LoadAsync: exception while loading '{}': {}", key, e.what());
                try { promise.set_value(nullptr); } catch (...) {}
            }
            catch (...)
            {
                LT_CORE_ERROR("MaterialAsset::LoadAsync: unknown exception while loading '{}'", key);
                try { promise.set_value(nullptr); } catch (...) {}
            }
        });

        return Async::Task<Ptr>(std::move(shared));
    }

    MaterialAsset::Ptr MaterialAsset::LoadBlocking(const std::string& key, Settings settings)
    {
        auto task = LoadAsync(key, std::move(settings));
        task.Wait();
        return task.Get();
    }

    bool MaterialAsset::LoadFromJsonFile()
    {
        if (m_ResolvedPath.empty())
        {
            const auto resolved = ResolveAssetKeyToPath(GetKey());
            if (resolved.IsFailure())
            {
                return false;
            }
            m_ResolvedPath = resolved.GetValue().string();
        }

        std::ifstream file(m_ResolvedPath, std::ios::in | std::ios::binary);
        if (!file.is_open())
        {
            LT_CORE_ERROR("MaterialAsset: failed to open '{}'", m_ResolvedPath);
            return false;
        }

        json root;
        try
        {
            file >> root;
        }
        catch (const std::exception& e)
        {
            LT_CORE_ERROR("MaterialAsset: JSON parse error '{}': {}", m_ResolvedPath, e.what());
            return false;
        }

        const auto shaderAssetResult = ResolveShaderAsset(root);
        if (shaderAssetResult.IsFailure())
        {
            LT_CORE_ERROR("MaterialAsset: {}", shaderAssetResult.GetError().GetErrorMessage());
            return false;
        }

        const auto texAssetResult = ResolveMainTextureAsset(root);
        if (texAssetResult.IsFailure())
        {
            LT_CORE_ERROR("MaterialAsset: {}", texAssetResult.GetError().GetErrorMessage());
            return false;
        }

        m_ShaderResolved = shaderAssetResult.GetValue();
        m_MainTextureResolved = texAssetResult.GetValue();

        m_Shader = m_ShaderResolved ? AssetHandle<ShaderAsset>(m_ShaderResolved) : AssetHandle<ShaderAsset>();
        m_MainTexture = m_MainTextureResolved ? AssetHandle<TextureAsset>(m_MainTextureResolved) : AssetHandle<TextureAsset>();

        const auto specOverride = ParseTextureSpecificationOverride(root);
        if (specOverride.IsSuccess())
        {
            m_HasMainTextureSpecOverride = true;
            m_MainTextureSpecOverride = specOverride.GetValue();
        }
        else
        {
            m_HasMainTextureSpecOverride = false;
        }
        return true;
    }

    bool MaterialAsset::Reload()
    {
        // Re-read JSON and update handles.
        m_ShaderResolved.reset();
        m_MainTextureResolved.reset();
        if (!LoadFromJsonFile())
        {
            return false;
        }

        // Dependency tracking:
        // - shader GUID is required
        // - texture GUID is optional
        std::vector<std::string> deps;
        deps.push_back(m_Shader.GetGuid());
        if (!m_MainTexture.GetGuid().empty())
        {
            deps.push_back(m_MainTexture.GetGuid());
        }

        (void)AssetDatabase::GetInstance().SetDependencies(GetGuid(), deps);
        return true;
    }

    std::shared_ptr<Shader> MaterialAsset::GetShader() const
    {
        auto shaderAsset = m_ShaderResolved ? m_ShaderResolved : m_Shader.Lock();
        if (shaderAsset && !m_ShaderResolved)
        {
            m_ShaderResolved = shaderAsset;
        }
        return shaderAsset ? shaderAsset->GetShader() : nullptr;
    }

    std::shared_ptr<Texture2D> MaterialAsset::GetMainTexture() const
    {
        auto texAsset = m_MainTextureResolved ? m_MainTextureResolved : m_MainTexture.Lock();
        if (texAsset && !m_MainTextureResolved)
        {
            m_MainTextureResolved = texAsset;
        }
        return texAsset ? texAsset->GetTexture() : nullptr;
    }

    void MaterialAsset::SubmitBind(Limitless::Renderer& renderer, const glm::mat4& viewProjection, const glm::mat4& model) const
    {
        auto shader = GetShader();
        if (!shader)
        {
            return;
        }

        renderer.SubmitCommand(std::make_unique<BindShaderCommand>(shader));
        renderer.SubmitCommand(std::make_unique<SetShaderMat4Command>(shader, "u_ViewProjection", viewProjection));
        renderer.SubmitCommand(std::make_unique<SetShaderMat4Command>(shader, "u_Model", model));

        if (auto tex = GetMainTexture())
        {
            if (m_HasMainTextureSpecOverride)
            {
                renderer.SubmitCommand(std::make_unique<SetTextureSpecificationCommand>(tex, m_MainTextureSpecOverride));
            }
            renderer.SubmitCommand(std::make_unique<BindTextureCommand>(tex, 0));
        }
    }
}

