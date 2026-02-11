#pragma once

#include "Limitless.h"

#include <memory>

namespace Limitless
{
    class EditorCameraController;
    class Framebuffer;

    /**
     * @brief Editor layer with viewport, scene hierarchy, inspector, and project panels.
     *
     * Renders a 3D scene to a framebuffer displayed in the Viewport panel.
     * Editor camera input is only active when the viewport is focused.
     */
    class EditorLayer : public Layer
    {
    public:
        EditorLayer();
        ~EditorLayer() override;

        void OnAttach() override;
        void OnDetach() override;
        void OnUpdate(float deltaTime) override;
        void OnRender() override;

    protected:
        void OnWindowResize(Events::WindowResizeEvent& event) override;

    private:
        void DrawMenuBar();
        void DrawViewportPanel();
        void DrawScenePanel();
        void DrawInspectorPanel();
        void DrawProjectPanel();

        void EnsureViewportFramebuffer(uint32_t width, uint32_t height);

        uint32_t m_ViewportWidthPixels = 1280;
        uint32_t m_ViewportHeightPixels = 720;

        std::shared_ptr<Framebuffer> m_ViewportFramebuffer;
        bool m_ViewportFocused = false;
        bool m_ViewportHovered = false;

        CameraManager m_CameraManager;
        CameraId m_CameraId{};
        std::unique_ptr<EditorCameraController> m_EditorCameraController;

        bool m_ShowDemoWindow = false;
    };

}  // namespace Limitless
