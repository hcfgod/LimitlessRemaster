#pragma once
#include "Limitless.h"
#include <memory>
#include <future>

namespace Limitless
{
    class EditorCameraController;
    class Renderer2DDemo;
    class AudioDemo;

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
        uint32_t m_ViewportWidthPixels = 1280;
        uint32_t m_ViewportHeightPixels = 720;

        CameraManager m_CameraManager;
        CameraId m_CameraId{};

        std::unique_ptr<Renderer2DDemo> m_Renderer2DDemo;
        std::unique_ptr<EditorCameraController> m_EditorCameraController;
        std::unique_ptr<AudioDemo> m_AudioDemo;

        bool m_UsingAssetBundle = false;
    };
} 