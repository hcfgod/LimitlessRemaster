#pragma once
#include "Limitless.h"
#include <memory>
#include <future>

namespace Limitless
{
    class EditorCameraController;
    class TexturedTriangleDemo;

    class TestLayer : public Layer
    {
    public:
        TestLayer();
        ~TestLayer() override;

        // Layer lifecycle
        void OnAttach() override;
        void OnDetach() override;

        // Per-frame updates
        void OnUpdate(float deltaTime) override;
        void OnRender() override;

    protected:
        void OnWindowResize(Events::WindowResizeEvent& event) override;

    private:
        CameraManager m_CameraManager;
        CameraId m_CameraId{};

        std::unique_ptr<TexturedTriangleDemo> m_TriangleDemo;
        std::unique_ptr<EditorCameraController> m_EditorCameraController;

        bool m_UsingAssetBundle = false;
    };
} 