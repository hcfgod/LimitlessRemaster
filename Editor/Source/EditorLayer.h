#pragma once

#include "Limitless.h"

#include <filesystem>
#include <memory>

namespace Limitless
{
    class EditorCameraController;
    class Framebuffer;
    class Scene;

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
        void DrawAssetTree(const std::filesystem::path& assetsDir, const std::filesystem::path& relPath);
        void DrawProjectFolderPopups(const std::filesystem::path& assetsDir);

        void EnsureViewportFramebuffer(uint32_t width, uint32_t height);

        uint32_t m_ViewportWidthPixels = 1280;
        uint32_t m_ViewportHeightPixels = 720;

        std::shared_ptr<Framebuffer> m_ViewportFramebuffer;
        bool m_ViewportFocused = false;
        bool m_ViewportHovered = false;

        CameraManager m_CameraManager;
        CameraId m_CameraId{};
        std::unique_ptr<EditorCameraController> m_EditorCameraController;

        std::unique_ptr<Scene> m_Scene;
        entt::entity m_SelectedEntity = entt::null;

        bool m_ShowDemoWindow = false;

        // Project panel folder popups (Create/Rename).
        // OpenPopup must be called outside a closing popup, so we defer via Pending.
        enum class ProjectFolderPopup { None, Create, Rename };
        ProjectFolderPopup m_ProjectFolderPopupPending = ProjectFolderPopup::None;
        std::filesystem::path m_ProjectFolderPopupParent;
        char m_ProjectFolderPopupBuffer[256] = {};
        bool m_CreateFolderPopupOpen = false;
        bool m_RenameFolderPopupOpen = false;
    };

}  // namespace Limitless
