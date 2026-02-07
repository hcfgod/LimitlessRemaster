#pragma once

#include "Limitless.h"

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
        void PollAsyncTexture();

        std::shared_ptr<Shader> m_Shader;
        std::shared_ptr<VertexArray> m_VAO;
        std::shared_ptr<VertexBuffer> m_VBO;
        std::shared_ptr<IndexBuffer> m_IBO;

        std::shared_ptr<Texture2D> m_CheckerboardTexture;
        std::future<std::shared_ptr<Texture2D>> m_CheckerboardTextureFuture;

        float m_ClearColor[4] = { 0.2f, 0.3f, 0.8f, 1.0f };
        float m_ColorChangeSpeed = 0.5f;
        float m_TimeSeconds = 0.0f;
    };
}

