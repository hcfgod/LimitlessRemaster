#pragma once

#include "Assets/Asset.h"
#include "Assets/AssetHandle.h"

#include "Assets/ShaderAsset.h"
#include "Assets/TextureAsset.h"

#include "Core/Concurrency/AsyncIO.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>

namespace Limitless
{
    class Renderer;
}

namespace Limitless::Assets
{
    // -----------------------------------------------------------------------------
    // MaterialAsset
    // Minimal Unity-style material:
    // - references shader + main texture via GUID handles
    // - supports hot reload via dependency tracking (AssetDatabase deps)
    //
    // Stored as JSON (text) so it's editor-friendly.
    // -----------------------------------------------------------------------------
    class MaterialAsset final : public Limitless::Asset
    {
    public:
        using Ptr = std::shared_ptr<MaterialAsset>;

        struct Settings
        {
            // reserved for importer settings (render state, etc.)
        };

        static Async::Task<Ptr> LoadAsync(const std::string& key, Settings settings = {});
        static Ptr LoadBlocking(const std::string& key, Settings settings = {});

        bool Reload() override;

        // Resolved runtime resources (may be null until loaded).
        std::shared_ptr<Shader> GetShader() const;
        std::shared_ptr<Texture2D> GetMainTexture() const;
        std::shared_ptr<Texture2D> GetNormalTexture() const;

        const AssetHandle<ShaderAsset>& GetShaderHandle() const { return m_Shader; }
        const AssetHandle<TextureAsset>& GetMainTextureHandle() const { return m_MainTexture; }
        const AssetHandle<TextureAsset>& GetNormalTextureHandle() const { return m_NormalTexture; }
        float GetNormalStrength() const { return m_NormalStrength; }
        float GetRoughness() const { return m_Roughness; }
        float GetSpecularIntensity() const { return m_SpecularIntensity; }
        bool HasMainTextureSubRect() const { return m_HasMainTextureSubRect; }
        const glm::vec2& GetMainTextureUvMin() const { return m_MainTextureUvMin; }
        const glm::vec2& GetMainTextureUvMax() const { return m_MainTextureUvMax; }

        // Submit material binds to render command stream.
        void SubmitBind(Limitless::Renderer& renderer, const glm::mat4& viewProjection, const glm::mat4& model) const;

    private:
        MaterialAsset(std::string key, std::string guid, Settings settings)
            : Asset(std::move(key), std::move(guid))
            , m_Settings(std::move(settings))
        {
        }

        bool LoadFromJsonFile();

    private:
        Settings m_Settings{};

        AssetHandle<ShaderAsset> m_Shader;
        AssetHandle<TextureAsset> m_MainTexture;
        AssetHandle<TextureAsset> m_NormalTexture;

        bool m_HasMainTextureSpecOverride = false;
        TextureSpecification m_MainTextureSpecOverride{};
        bool m_HasMainTextureSubRect = false;
        glm::vec2 m_MainTextureUvMin = glm::vec2(0.0f, 0.0f);
        glm::vec2 m_MainTextureUvMax = glm::vec2(1.0f, 1.0f);

        float m_NormalStrength = 1.0f;
        float m_Roughness = 0.5f;
        float m_SpecularIntensity = 0.5f;

        // Strong refs keep dependencies alive while the material is alive.
        // (AssetManager cache is weak by design.)
        mutable std::shared_ptr<ShaderAsset> m_ShaderResolved;
        mutable std::shared_ptr<TextureAsset> m_MainTextureResolved;
        mutable std::shared_ptr<TextureAsset> m_NormalTextureResolved;

        std::string m_ResolvedPath;
    };
}

