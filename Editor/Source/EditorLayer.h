#pragma once

#include "Limitless.h"
#include "Assets/TextureAsset.h"
#include "Graphics/Texture.h"

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
     * Editor camera input is active when the viewport is hovered (Unity-style).
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
        enum class PlayModeState
        {
            Edit = 0,
            Play = 1,
            Pause = 2
        };

        void DrawMenuBar();
        void DrawViewportPanel();
        void DrawScenePanel();
        void DrawInspectorPanel();
        void DrawTextureInspector();
        void ApplyTextureSpecAndPersist(Assets::TextureAsset::Ptr textureAsset, const TextureSpecification& spec);
        void PersistTextureSpecAndReload(Assets::TextureAsset::Ptr textureAsset, const TextureSpecification& spec);
        void InvalidateSpriteCachesForTexture(const std::string& textureKey);
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
        CameraId m_EditorCameraId{};
        CameraId m_CachedGameplayCameraId{};
        std::unique_ptr<EditorCameraController> m_EditorCameraController;

        std::unique_ptr<Scene> m_Scene;
        /// Stored edit-scene while in Play/Pause. On Stop, we restore this instance.
        std::unique_ptr<Scene> m_EditSceneStored;
        PlayModeState m_PlayModeState = PlayModeState::Edit;
        bool m_PlayModeMissingGameplayCamera = false;
        entt::entity m_SelectedEntity = entt::null;

        /// Selected texture asset key when user double-clicks a texture in the Project panel (e.g. "Assets/Textures/X.png").
        /// When non-empty, the Inspector shows the texture and its spec; entity selection is ignored.
        std::string m_SelectedTextureAssetKey;

        /// Cached texture asset for the selected key; avoids LoadBlocking every frame and reduces lag.
        Assets::TextureAsset::Ptr m_CachedTextureAsset;

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
