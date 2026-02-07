#pragma once
#include "Limitless.h"
#include <memory>
#include <future>

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

    protected:
        void OnWindowResize(Events::WindowResizeEvent& event) override;

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
        std::future<std::shared_ptr<Texture2D>> m_CheckerboardTextureFuture;

        // Input Actions (Unity-style)
        std::shared_ptr<InputActionAsset> m_InputAsset;
        InputAction* m_ActionMove = nullptr;  // Axis2D (WASD)
        InputAction* m_ActionLook = nullptr;  // Axis2D (Mouse delta)
        InputAction* m_ActionBoost = nullptr; // Button (Shift)
        InputAction* m_ActionLookEnable = nullptr; // Button (RMB)

        // Camera
        CameraManager m_CameraManager;
        CameraId m_CameraId{};
        float m_CameraMoveSpeed = 3.0f;
        float m_CameraBoostMultiplier = 4.0f;
        float m_CameraLookSensitivity = 0.0025f;
    };
} 