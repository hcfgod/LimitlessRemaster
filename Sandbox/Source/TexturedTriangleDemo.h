#pragma once

#include "Limitless.h"

#include "Assets/TextureAsset.h"
#include "Assets/TextureAssetImporter.h"
#include "Assets/ShaderAssetImporter.h"

#include <future>
#include <memory>

namespace Limitless
{
    class TexturedTriangleDemo final
    {
    public:
        TexturedTriangleDemo() = default;
        ~TexturedTriangleDemo() = default;

        void Initialize();
        void Shutdown();

        void Update(float deltaTime);
        void Render(const CameraManager& cameraManager) const;

        bool IsTextureReady() const { return m_CheckerboardTexture != nullptr; }

    private:
        void CreateResources();
        void PollAsyncTextureAsset();

        // Keep asset objects alive so hot reload can update them in-place.
        Assets::ShaderAsset::Ptr m_ShaderAsset;
        std::shared_ptr<Shader> m_Shader;

        Assets::TextureAsset::Ptr m_CheckerboardTextureAsset;
        std::shared_ptr<VertexArray> m_VAO;
        std::shared_ptr<VertexBuffer> m_VBO;
        std::shared_ptr<IndexBuffer> m_IBO;

        std::shared_ptr<Texture2D> m_CheckerboardTexture;
        Async::Task<Assets::TextureAsset::Ptr> m_CheckerboardTextureAssetTask;

        float m_ClearColor[4] = { 0.2f, 0.3f, 0.8f, 1.0f };
        float m_ColorChangeSpeed = 0.5f;
        float m_TimeSeconds = 0.0f;
    };
}

