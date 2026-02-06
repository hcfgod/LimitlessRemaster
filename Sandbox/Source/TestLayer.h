#pragma once
#include "Limitless.h"
#include <memory>

namespace Limitless
{
    class TestLayer : public Layer
    {
    public:
        TestLayer();
        virtual ~TestLayer() = default;

        // Layer lifecycle
        void OnAttach() override;
        void OnDetach() override;

        // Per-frame updates
        void OnUpdate(float deltaTime) override;
        void OnRender() override;

    private:
        float m_ClearColor[4]; // RGBA color for clearing
        float m_ColorChangeSpeed; // Speed at which to change colors
        float m_TimeSeconds = 0.0f; // Per-layer timer used for color transitions (instance-owned)

        // Triangle demo resources
        std::shared_ptr<Shader> m_TriangleShader;
        std::shared_ptr<VertexArray> m_TriangleVAO;
        std::shared_ptr<VertexBuffer> m_TriangleVBO;
        std::shared_ptr<IndexBuffer> m_TriangleIBO;

        std::shared_ptr<Texture2D> m_CheckerboardTexture;
    };
} 