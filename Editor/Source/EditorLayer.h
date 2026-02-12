#pragma once

#include "Limitless.h"
#include "Assets/MaterialAsset.h"
#include "Assets/TextureAsset.h"
#include "EditorPlayMode.h"
#include "EditorProjectPanel.h"
#include "EditorScenePanel.h"
#include "Graphics/Texture.h"

#include <array>
#include <filesystem>
#include <memory>
#include <string>

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
     * Most panel/runtime logic is implemented in dedicated Editor* modules and
     * this layer coordinates high-level state and call order.
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
        void EnterPlayMode();
        void ExitPlayMode();
        void TogglePausePlayMode();
        void NewScene();
        void SaveScene();
        void SaveSceneAs();
        void DrawSaveScenePopup();
        bool LoadSceneFromAssetKey(const std::string& assetKey);
        bool SaveSceneToAssetKey(const std::string& assetKey);
        std::string CreateSceneAssetInFolder(const std::filesystem::path& relativeFolderPath, const std::string& preferredFileName = {});

        uint32_t m_ViewportWidthPixels = 1280;
        uint32_t m_ViewportHeightPixels = 720;

        std::shared_ptr<Framebuffer> m_ViewportFramebuffer;
        bool m_ViewportFocused = false;
        bool m_ViewportHovered = false;

        CameraManager m_CameraManager;
        CameraId m_EditorCameraId{};
        CameraId m_CachedGameplayCameraId{};
        bool m_CreatedGameplayCameraFromScene = false;
        std::unique_ptr<EditorCameraController> m_EditorCameraController;

        std::unique_ptr<Scene> m_Scene;
        /// Stored edit-scene while in Play/Pause. On Stop, we restore this instance.
        std::unique_ptr<Scene> m_EditSceneStored;
        EditorPlayModeState m_PlayModeState = EditorPlayModeState::Edit;
        bool m_PlayModeMissingGameplayCamera = false;
        entt::entity m_SelectedEntity = entt::null;

        /// Selected texture asset key when user double-clicks a texture in the Project panel (e.g. "Assets/Textures/X.png").
        /// When non-empty, the Inspector shows the texture and its spec; entity selection is ignored.
        std::string m_SelectedTextureAssetKey;

        /// Cached texture asset for the selected key; avoids LoadBlocking every frame and reduces lag.
        Assets::TextureAsset::Ptr m_CachedTextureAsset;

        /// Selected material asset key when user double-clicks a material in the Project panel (e.g. "Assets/Materials/X.material.json").
        /// When non-empty, the Inspector shows the material editor; entity selection is ignored.
        std::string m_SelectedMaterialAssetKey;

        /// Cached material asset for the selected key; avoids LoadBlocking every frame and reduces lag.
        Assets::MaterialAsset::Ptr m_CachedMaterialAsset;

        std::string m_CurrentSceneAssetKey;
        bool m_SaveScenePopupOpen = false;
        bool m_RequestOpenSaveScenePopup = false;
        std::filesystem::path m_SaveSceneFolderPath = "Scenes";
        std::array<char, 256> m_SaveSceneFileNameBuffer{};

        bool m_ShowDemoWindow = false;
        EditorScenePanelState m_ScenePanelState;
        EditorProjectPanelState m_ProjectPanelState;
    };

}  // namespace Limitless
