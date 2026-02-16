#pragma once

#include "Limitless.h"
#include "Assets/MaterialAsset.h"
#include "Assets/TextureAsset.h"
#include "EditorPlayMode.h"
#include "EditorProjectPanel.h"
#include "EditorProjectDialog.h"
#include "EditorProjectSettingsPanel.h"
#include "EditorAssetDiagnosticsPanel.h"
#include "EditorScenePanel.h"
#include "EditorViewportPanel.h"
#include "Core/Concurrency/AsyncIO.h"
#include "Undo/EditorUndoService.h"
#include "Graphics/Texture.h"

#include <array>
#include <filesystem>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>

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
        void OnFixedUpdate(float fixedDeltaTime) override;
        void OnRender() override;

    protected:
        void OnWindowResize(Events::WindowResizeEvent& event) override;

    private:
        void DrawMenuBar();
        void DrawViewportPanel();
        void DrawScenePanel();
        void DrawInspectorPanel();
        void DrawProjectPanel();
        void DrawPhysicsDiagnosticsPanel();
        void DrawConsolePanel();
        void DrawTilemapPanel();

        void EnsureViewportFramebuffer(uint32_t width, uint32_t height);
        void EnterPlayMode();
        void EnterSimulateMode();
        void ExitPlayMode();
        void TogglePausePlayMode();
        void NewScene();
        void NewScene(bool forceWithoutConfirmation);
        void SaveScene();
        void SaveSceneAs();
        void DrawSaveScenePopup();
        void DrawSceneSwitchConfirmationPopup();
        bool LoadSceneFromAssetKey(const std::string& assetKey);
        bool LoadSceneFromAssetKey(const std::string& assetKey, bool forceWithoutConfirmation);
        bool LoadSceneFromAssetKeyInPlayMode(const std::string& assetKey);
        bool SaveSceneToAssetKey(const std::string& assetKey);
        bool OpenPrefabAssetForEditing(const std::string& prefabAssetKey);
        bool ReturnFromPrefabMode(bool forceWithoutConfirmation);
        bool ApplyPrefabStageChangesToInstances();
        void QueueSceneAssetPrewarm();
        void PumpSceneAssetPrewarm();
        void ProcessPendingSceneTransitions();
        bool EnsureSceneSwitchAllowed(const std::function<void()>& deferredSwitchAction);
        void BeginSceneSwitch();
        void PersistProjectSessionState();
        void RefreshProjectPhysics2DSettings();
        void ApplyProjectPhysics2DSettingsToScenes();
        void RefreshProjectLighting2DSettings();
        void ApplyProjectLighting2DSettings();
        std::string CreateSceneAssetInFolder(const std::filesystem::path& relativeFolderPath, const std::string& preferredFileName = {});
        std::string CreateMaterialAssetInFolder(const std::filesystem::path& relativeFolderPath, const std::string& preferredFileName = {});
        std::string CreateTilesetAssetInFolder(const std::filesystem::path& relativeFolderPath, const std::string& preferredFileName = {});
        std::string CreatePrefabAssetPathForEntity(entt::entity entity, const std::filesystem::path& relativeFolderPath) const;
        bool CreatePrefabFromEntity(entt::entity entity);
        bool CreatePrefabFromEntityInFolder(entt::entity entity, const std::filesystem::path& relativeFolderPath);
        entt::entity InstantiatePrefabAtParent(const std::string& prefabAssetKey, entt::entity parentEntity);
        entt::entity InstantiatePrefabAtWorldPosition(const std::string& prefabAssetKey, const glm::vec3& worldPosition);
        bool ApplyPrefabFromEntity(entt::entity entity);
        entt::entity RevertPrefabEntity(entt::entity entity);
        bool UnpackPrefabEntity(entt::entity entity);
        void SetProjectDefaultSceneAssetKey(const std::string& sceneAssetKey);

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

        /// Selected script asset key when user single-clicks a script in the Project panel.
        /// When non-empty, the Inspector shows script asset metadata preview.
        std::string m_SelectedNativeScriptAssetKey;

        /// Selected prefab asset key when user single-clicks a prefab in the Project panel.
        /// When non-empty, the Inspector shows prefab asset metadata preview.
        std::string m_SelectedPrefabAssetKey;

        /// Selected tileset asset key when user clicks a tileset in the Project panel.
        std::string m_SelectedTilesetAssetKey;

        std::string m_CurrentSceneAssetKey;
        std::string m_EditSceneStoredAssetKey;
        std::string m_PrefabModeReturnSceneAssetKey;
        EditorUndoService m_EditorUndoService;
        bool m_RequestOpenSceneSwitchConfirmationPopup = false;
        bool m_SceneSwitchConfirmationPopupOpen = false;
        std::function<void()> m_PendingSceneSwitchAction;
        bool m_SaveScenePopupOpen = false;
        bool m_RequestOpenSaveScenePopup = false;
        std::filesystem::path m_SaveSceneFolderPath = "Scenes";
        std::array<char, 256> m_SaveSceneFileNameBuffer{};

        bool m_ShowDemoWindow = false;
        bool m_ShowProjectSettingsWindow = false;
        bool m_ShowAssetDiagnosticsWindow = false;
        bool m_ShowPhysicsDiagnosticsWindow = true;
        bool m_ShowConsoleWindow = true;
        bool m_ShowTilemapPanel = true;
        bool m_ConsoleAutoScroll = true;
        bool m_ConsoleShowScriptLogs = true;
        bool m_ConsoleShowEngineLogs = false;
        bool m_ConsoleShowInfo = true;
        bool m_ConsoleShowWarnings = true;
        bool m_ConsoleShowErrors = true;
        std::array<char, 256> m_ConsoleSearchBuffer{};
        int m_PhysicsDiagnosticsRecentPeakContactPairs = 0;
        int m_PhysicsDiagnosticsRecentPeakPenetratingPoints = 0;
        float m_PhysicsDiagnosticsRecentPeakMaxPenetrationDepth = 0.0f;
        float m_PhysicsDiagnosticsRecentPeakHoldSeconds = 0.0f;
        EditorScenePanelState m_ScenePanelState;
        EditorProjectPanelState m_ProjectPanelState;
        EditorProjectDialog::EditorProjectDialogState m_ProjectDialogState;
        EditorProjectSettingsPanel::EditorProjectSettingsPanelState m_ProjectSettingsPanelState;
        Project::Physics2DSettings m_ProjectPhysics2DSettings{};
        bool m_ProjectPhysics2DSettingsLoaded = false;
        Project::Lighting2DSettings m_ProjectLighting2DSettings{};
        bool m_ProjectLighting2DSettingsLoaded = false;

        std::unordered_map<std::string, Async::Task<Assets::TextureAsset::Ptr>> m_PendingTexturePrewarmTasks;
        std::unordered_map<std::string, Async::Task<Assets::MaterialAsset::Ptr>> m_PendingMaterialPrewarmTasks;
        std::unordered_map<std::string, Assets::TextureAsset::Ptr> m_PrewarmedTextureAssets;
        std::unordered_map<std::string, Assets::MaterialAsset::Ptr> m_PrewarmedMaterialAssets;
        EditorViewportPanel::TilemapEditorState m_TilemapEditorState{};
    };

}  // namespace Limitless
