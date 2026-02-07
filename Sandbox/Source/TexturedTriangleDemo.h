#pragma once

#include "Limitless.h"

#include "Assets/MaterialAssetImporter.h"

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

        bool IsMaterialReady() const { return m_Material && m_Material->GetShader() && m_Material->GetMainTexture(); }

    private:
        void CreateResources();
        void PollMaterialReadiness();

        std::shared_ptr<VertexArray> m_VAO;
        std::shared_ptr<VertexBuffer> m_VBO;
        std::shared_ptr<IndexBuffer> m_IBO;

        Assets::MaterialAsset::Ptr m_Material;
        bool m_LoggedMaterialReady = false;

        float m_ClearColor[4] = { 0.2f, 0.3f, 0.8f, 1.0f };
        float m_ColorChangeSpeed = 0.5f;
        float m_TimeSeconds = 0.0f;
    };
}

