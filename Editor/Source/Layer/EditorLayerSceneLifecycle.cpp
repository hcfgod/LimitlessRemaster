#include "PrecompiledHeader.h"
#include "EditorLayer.h"

#include "Audio/SceneAudioSystem.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetPaths.h"
#include "Core/Debug/Log.h"
#include "EditorAnimationTimelinePanel.h"
#include "EditorAnimatorGraphPanel.h"
#include "EditorAssetNaming.h"
#include "EditorPlayMode.h"
#include "Graphics/Camera/PerspectiveCamera3D.h"
#include "Panels/EditorInspectorPanel.h"
#include "Project/BuildSettings.h"
#include "Project/ProjectManager.h"
#include "Scene/Components/CoreComponents.h"
#include "Scene/Scene.h"
#include "Scripting/NativeScriptRegistry.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace Limitless
{
    namespace
    {
        constexpr const char* kDefaultSceneFileName = "SampleScene.scene.json";

        std::string NormalizeSlashes(std::string pathText)
        {
            std::replace(pathText.begin(), pathText.end(), '\\', '/');
            return pathText;
        }

        bool IsPrefabAssetKey(const std::string& assetKey)
        {
            if (assetKey.empty())
                return false;
            std::string lowerKey = NormalizeSlashes(assetKey);
            std::transform(lowerKey.begin(), lowerKey.end(), lowerKey.begin(), [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
            return lowerKey.ends_with(".prefab.json");
        }

        std::string SceneDisplayNameFromFileName(const std::string& fileName)
        {
            return EditorAssetNaming::GetAssetDisplayNameFromFileName(fileName);
        }

        std::string NormalizeScriptCompileFailurePolicy(std::string policy)
        {
            if (policy == Project::ScriptCompileFailurePolicy::SafeMode ||
                policy == Project::ScriptCompileFailurePolicy::BlockPlay)
            {
                return policy;
            }
            return Project::ScriptCompileFailurePolicy::SafeMode;
        }

        std::string ResolveScriptCompileFailurePolicy()
        {
            const auto& projectManager = Project::ProjectManager::GetInstance();
            if (!projectManager.HasOpenProject())
                return Project::ScriptCompileFailurePolicy::SafeMode;

            const auto loadResult = Project::LoadBuildSettings(projectManager.GetProjectRoot());
            if (!loadResult.IsSuccess())
                return Project::ScriptCompileFailurePolicy::SafeMode;
            return NormalizeScriptCompileFailurePolicy(loadResult.GetValue().ScriptCompileFailurePolicy);
        }

        void PopulateDefaultSceneTemplate(Scene& scene)
        {
            const entt::entity cameraEntity = scene.CreateEntity("Main Camera");
            auto& cameraTransform = scene.GetRegistry().get<TransformComponent>(cameraEntity);
            cameraTransform.Position = glm::vec3(0.0f, 0.0f, 5.0f);
            cameraTransform.Rotation = glm::vec3(0.0f);

            auto& camera = scene.GetRegistry().emplace<CameraComponent>(cameraEntity);
            camera.IsPrimary = true;
            camera.Projection = CameraComponent::ProjectionType::Perspective3D;
            camera.FieldOfViewYDegrees = 60.0f;
            camera.NearPlane = 0.1f;
            camera.FarPlane = 1000.0f;
        }

    }

    void EditorLayer::EnterPlayMode()
    {
        if (m_PlayModeState != EditorPlayModeState::Edit)
            return;
        m_ScriptSafeModeActive = false;
        m_ScriptSafeModeMessage.clear();
        NativeScriptRegistry::SetExecutionBlocked(false);
        DestroyGameViewPreviewCamera();

        // Ensure Play Mode uses the latest animation authoring edits even if user did not click Apply.
        if (!EditorAnimationTimelinePanel::ApplyPendingChanges(&m_EditorUndoService))
        {
            LT_ERROR("Cannot enter Play Mode: failed to auto-apply pending Animation Clip edits.");
            return;
        }
        if (!EditorAnimatorGraphPanel::ApplyPendingChanges(&m_EditorUndoService))
        {
            LT_ERROR("Cannot enter Play Mode: failed to auto-apply pending Animator Controller edits.");
            return;
        }

        std::string scriptBuildFailureMessage;
        if (EditorInspectorPanel::GetLastNativeScriptBuildFailure(&scriptBuildFailureMessage))
        {
            const std::string failurePolicy = ResolveScriptCompileFailurePolicy();
            if (failurePolicy == Project::ScriptCompileFailurePolicy::BlockPlay)
            {
                LT_ERROR("Cannot enter Play Mode: {} Build scripts to continue.",
                         scriptBuildFailureMessage.empty() ? "native script build previously failed." : scriptBuildFailureMessage);
                return;
            }

            m_ScriptSafeModeActive = true;
            m_ScriptSafeModeMessage = scriptBuildFailureMessage.empty()
                ? std::string("Last native script build failed.")
                : scriptBuildFailureMessage;
            NativeScriptRegistry::SetExecutionBlocked(true);
            LT_WARN("Play Mode Safe Mode enabled: scripts are disabled. {}", m_ScriptSafeModeMessage);
        }

        // Unity-style: Playing from Prefab Mode first returns to the previous scene.
        if (IsPrefabAssetKey(m_CurrentSceneAssetKey) && !m_PrefabModeReturnSceneAssetKey.empty())
        {
            const std::string returnSceneKey = m_PrefabModeReturnSceneAssetKey;
            if (!EnsureSceneSwitchAllowed([this, returnSceneKey]() {
                    m_PrefabModeReturnSceneAssetKey.clear();
                    (void)LoadSceneFromAssetKey(returnSceneKey, true);
                    EnterPlayMode();
                }))
            {
                return;
            }

            m_PrefabModeReturnSceneAssetKey.clear();
            if (!LoadSceneFromAssetKey(returnSceneKey, true))
                return;
        }

        Audio::StopAudioSourcesInScene(m_Scene.get());
        Audio::StopAudioSourcesInScene(m_EditSceneStored.get());
        m_EditSceneStoredAssetKey = m_CurrentSceneAssetKey;

        EditorPlayMode::Enter(
            m_PlayModeState,
            m_Scene,
            m_EditSceneStored,
            m_CameraManager,
            m_EditorCameraId,
            m_GameViewWidthPixels,
            m_GameViewHeightPixels,
            m_CachedGameplayCameraId,
            m_CreatedGameplayCameraFromScene,
            m_PlayModeMissingGameplayCamera,
            m_SelectedEntity,
            m_SelectedTextureAssetKey,
            m_CachedTextureAsset);
        m_FocusSceneViewOnPlayExit = false;
        m_FocusGameViewOnPlayEnter = true;
    }

    void EditorLayer::EnterSimulateMode()
    {
        if (m_PlayModeState != EditorPlayModeState::Edit)
            return;
        m_ScriptSafeModeActive = false;
        m_ScriptSafeModeMessage.clear();
        NativeScriptRegistry::SetExecutionBlocked(false);
        DestroyGameViewPreviewCamera();

        // Keep Simulate Mode consistent with authored animation panel changes.
        if (!EditorAnimationTimelinePanel::ApplyPendingChanges(&m_EditorUndoService))
        {
            LT_ERROR("Cannot enter Simulate Mode: failed to auto-apply pending Animation Clip edits.");
            return;
        }
        if (!EditorAnimatorGraphPanel::ApplyPendingChanges(&m_EditorUndoService))
        {
            LT_ERROR("Cannot enter Simulate Mode: failed to auto-apply pending Animator Controller edits.");
            return;
        }

        std::string scriptBuildFailureMessage;
        if (EditorInspectorPanel::GetLastNativeScriptBuildFailure(&scriptBuildFailureMessage))
        {
            const std::string failurePolicy = ResolveScriptCompileFailurePolicy();
            if (failurePolicy == Project::ScriptCompileFailurePolicy::BlockPlay)
            {
                LT_ERROR("Cannot enter Simulate Mode: {} Build scripts to continue.",
                         scriptBuildFailureMessage.empty() ? "native script build previously failed." : scriptBuildFailureMessage);
                return;
            }

            m_ScriptSafeModeActive = true;
            m_ScriptSafeModeMessage = scriptBuildFailureMessage.empty()
                ? std::string("Last native script build failed.")
                : scriptBuildFailureMessage;
            NativeScriptRegistry::SetExecutionBlocked(true);
            LT_WARN("Simulate Mode Safe Mode enabled: scripts are disabled. {}", m_ScriptSafeModeMessage);
        }

        Audio::StopAudioSourcesInScene(m_Scene.get());
        Audio::StopAudioSourcesInScene(m_EditSceneStored.get());
        m_EditSceneStoredAssetKey = m_CurrentSceneAssetKey;

        EditorPlayMode::EnterSimulate(
            m_PlayModeState,
            m_Scene,
            m_EditSceneStored,
            m_CameraManager,
            m_EditorCameraId,
            m_GameViewWidthPixels,
            m_GameViewHeightPixels,
            m_CachedGameplayCameraId,
            m_CreatedGameplayCameraFromScene,
            m_PlayModeMissingGameplayCamera,
            m_SelectedEntity,
            m_SelectedTextureAssetKey,
            m_CachedTextureAsset);
        m_FocusSceneViewOnPlayExit = false;
    }

    void EditorLayer::ExitPlayMode()
    {
        NativeScriptRegistry::SetExecutionBlocked(false);
        m_ScriptSafeModeActive = false;
        m_ScriptSafeModeMessage.clear();
        Audio::StopAudioSourcesInScene(m_Scene.get());
        Audio::StopAudioSourcesInScene(m_EditSceneStored.get());

        EditorPlayMode::Exit(
            m_PlayModeState,
            m_Scene,
            m_EditSceneStored,
            m_CameraManager,
            m_EditorCameraId,
            m_CachedGameplayCameraId,
            m_CreatedGameplayCameraFromScene,
            m_PlayModeMissingGameplayCamera,
            m_SelectedEntity,
            m_SelectedTextureAssetKey,
            m_CachedTextureAsset);

        // Restore the edit scene identity after Play Mode runtime scene changes.
        m_CurrentSceneAssetKey = m_EditSceneStoredAssetKey;
        m_EditSceneStoredAssetKey.clear();
        m_FocusGameViewOnPlayEnter = false;
        m_FocusSceneViewOnPlayExit = true;
        DestroyGameViewPreviewCamera();
    }

    void EditorLayer::TogglePausePlayMode()
    {
        EditorPlayMode::TogglePause(m_PlayModeState);
    }

    void EditorLayer::NewScene()
    {
        NewScene(false);
    }

    void EditorLayer::NewScene(bool forceWithoutConfirmation)
    {
        if (!forceWithoutConfirmation)
        {
            if (!EnsureSceneSwitchAllowed([this]() { NewScene(true); }))
                return;
        }

        if (m_PlayModeState != EditorPlayModeState::Edit)
            ExitPlayMode();

        BeginSceneSwitch();
        Audio::StopAudioSourcesInScene(m_Scene.get());

        m_Scene = std::make_unique<Scene>();
        PopulateDefaultSceneTemplate(*m_Scene);
        m_SelectedEntity = entt::null;
        m_SelectedTextureAssetKey.clear();
        m_SelectedNativeScriptAssetKey.clear();
        m_SelectedPrefabAssetKey.clear();
        m_SelectedTilesetAssetKey.clear();
        m_SelectedAudioMixerAssetKey.clear();
        m_SelectedInputActionsAssetKey.clear();
        m_SelectedAnimationClipAssetKey.clear();
        m_SelectedAnimatorControllerAssetKey.clear();
        m_CachedTextureAsset.reset();
        m_CurrentSceneAssetKey.clear();
        m_EditorUndoService.MarkSaved();
    }

    void EditorLayer::SaveScene()
    {
        if (m_PlayModeState != EditorPlayModeState::Edit || !m_Scene)
            return;

        if (!m_CurrentSceneAssetKey.empty())
        {
            (void)SaveSceneToAssetKey(m_CurrentSceneAssetKey);
            return;
        }

        SaveSceneAs();
    }

    void EditorLayer::SaveSceneAs()
    {
        if (m_PlayModeState != EditorPlayModeState::Edit || !m_Scene)
            return;

        if (!m_CurrentSceneAssetKey.empty())
        {
            std::filesystem::path currentPath(m_CurrentSceneAssetKey);
            if (currentPath.has_filename())
            {
                const std::string fileName = SceneDisplayNameFromFileName(currentPath.filename().string());
                const size_t copyCount = std::min(fileName.size(), m_SaveSceneFileNameBuffer.size() - 1);
                std::copy_n(fileName.c_str(), copyCount, m_SaveSceneFileNameBuffer.begin());
                m_SaveSceneFileNameBuffer[copyCount] = '\0';
            }
            if (currentPath.has_parent_path())
                m_SaveSceneFolderPath = currentPath.parent_path().lexically_relative("Assets");
        }
        else
        {
            const std::string defaultName = SceneDisplayNameFromFileName(kDefaultSceneFileName);
            const size_t copyCount = std::min(defaultName.size(), m_SaveSceneFileNameBuffer.size() - 1);
            std::copy_n(defaultName.c_str(), copyCount, m_SaveSceneFileNameBuffer.begin());
            m_SaveSceneFileNameBuffer[copyCount] = '\0';
            m_SaveSceneFolderPath = "Scenes";
        }

        m_RequestOpenSaveScenePopup = true;
    }

    bool EditorLayer::EnsureSceneSwitchAllowed(const std::function<void()>& deferredSwitchAction)
    {
        if (m_PlayModeState != EditorPlayModeState::Edit)
            return true;
        if (!m_EditorUndoService.IsDirty() && !m_EditorUndoService.CanUndo())
            return true;

        m_PendingSceneSwitchAction = deferredSwitchAction;
        m_RequestOpenSceneSwitchConfirmationPopup = true;
        return false;
    }

    void EditorLayer::BeginSceneSwitch()
    {
        DestroyGameViewPreviewCamera();
        m_EditorUndoService.Clear();
        m_EditorUndoService.MarkSaved();
        m_ActiveSceneTexturePrewarmKeys.clear();
        m_ActiveSceneMaterialPrewarmKeys.clear();
    }

    bool EditorLayer::LoadSceneFromAssetKey(const std::string& assetKey)
    {
        return LoadSceneFromAssetKey(assetKey, false);
    }

    bool EditorLayer::LoadSceneFromAssetKey(const std::string& assetKey, bool forceWithoutConfirmation)
    {
        if (assetKey.empty())
            return false;

        if (!forceWithoutConfirmation)
        {
            if (!EnsureSceneSwitchAllowed([this, assetKey]() { (void)LoadSceneFromAssetKey(assetKey, true); }))
                return false;
        }

        if (m_PlayModeState != EditorPlayModeState::Edit)
            ExitPlayMode();

        BeginSceneSwitch();
        Audio::StopAudioSourcesInScene(m_Scene.get());
        Audio::StopAudioSourcesInScene(m_EditSceneStored.get());

        const auto resolvedPathResult = Assets::ResolveAssetKeyToPath(assetKey);
        if (resolvedPathResult.IsFailure())
        {
            LT_ERROR("Failed to resolve scene asset key {}: {}", assetKey, resolvedPathResult.GetError().GetErrorMessage());
            return false;
        }

        auto sceneResult = Scene::LoadFromFile(resolvedPathResult.GetValue());
        if (sceneResult.IsFailure())
        {
            LT_ERROR("Failed to load scene {}: {}", assetKey, sceneResult.GetError().GetErrorMessage());
            return false;
        }

        m_Scene = std::move(sceneResult.GetValue());
        m_Scene->BeginLoadingState();
        m_Scene->MarkSceneObjectsInitialized();
        ApplyProjectPhysics2DSettingsToScenes();
        QueueSceneAssetPrewarm();
        UpdateSceneLoadingState();
        m_SelectedEntity = entt::null;
        m_SelectedTextureAssetKey.clear();
        m_SelectedNativeScriptAssetKey.clear();
        m_SelectedPrefabAssetKey.clear();
        m_SelectedTilesetAssetKey.clear();
        m_SelectedAudioMixerAssetKey.clear();
        m_SelectedInputActionsAssetKey.clear();
        m_SelectedAnimationClipAssetKey.clear();
        m_SelectedAnimatorControllerAssetKey.clear();
        m_CachedTextureAsset.reset();
        m_CurrentSceneAssetKey = assetKey;
        if (!IsPrefabAssetKey(assetKey))
            m_PrefabModeReturnSceneAssetKey.clear();
        if (const auto& pm = Project::ProjectManager::GetInstance(); pm.HasOpenProject())
        {
            PersistProjectSessionState();
        }

        if (auto* editorCamera = m_CameraManager.GetPerspective3D(m_EditorCameraId))
        {
            const auto& bookmark = m_Scene->GetEditorCameraBookmark();
            if (bookmark.has_value())
            {
                editorCamera->SetPosition(bookmark->Position);
                editorCamera->SetYawPitchDegrees(bookmark->YawDegrees, bookmark->PitchDegrees);
            }
        }

        LT_INFO("Loaded scene {}", assetKey);
        m_EditorUndoService.MarkSaved();
        return true;
    }

    bool EditorLayer::LoadSceneFromAssetKeyInPlayMode(const std::string& assetKey)
    {
        if (assetKey.empty())
            return false;

        Audio::StopAudioSourcesInScene(m_Scene.get());

        const auto resolvedPathResult = Assets::ResolveAssetKeyToPath(assetKey);
        if (resolvedPathResult.IsFailure())
        {
            LT_ERROR("Failed to resolve scene asset key {}: {}", assetKey, resolvedPathResult.GetError().GetErrorMessage());
            return false;
        }

        auto sceneResult = Scene::LoadFromFile(resolvedPathResult.GetValue());
        if (sceneResult.IsFailure())
        {
            LT_ERROR("Failed to load scene {}: {}", assetKey, sceneResult.GetError().GetErrorMessage());
            return false;
        }

        m_Scene = std::move(sceneResult.GetValue());
        m_Scene->BeginLoadingState();
        m_Scene->MarkSceneObjectsInitialized();
        ApplyProjectPhysics2DSettingsToScenes();
        QueueSceneAssetPrewarm();
        UpdateSceneLoadingState();
        m_SelectedEntity = entt::null;
        m_SelectedTextureAssetKey.clear();
        m_SelectedNativeScriptAssetKey.clear();
        m_SelectedPrefabAssetKey.clear();
        m_SelectedTilesetAssetKey.clear();
        m_SelectedAudioMixerAssetKey.clear();
        m_SelectedInputActionsAssetKey.clear();
        m_SelectedAnimationClipAssetKey.clear();
        m_SelectedAnimatorControllerAssetKey.clear();
        m_CachedTextureAsset.reset();
        m_CurrentSceneAssetKey = assetKey;

        LT_INFO("Loaded scene during Play Mode {}", assetKey);
        m_EditorUndoService.Clear();
        return true;
    }

    bool EditorLayer::SaveSceneToAssetKey(const std::string& assetKey)
    {
        if (assetKey.empty() || !m_Scene)
            return false;

        // Save scene should also flush pending animation authoring asset edits.
        if (!EditorAnimationTimelinePanel::ApplyPendingChanges(&m_EditorUndoService))
        {
            LT_ERROR("Failed to save scene because pending Animation Clip edits could not be persisted.");
            return false;
        }
        if (!EditorAnimatorGraphPanel::ApplyPendingChanges(&m_EditorUndoService))
        {
            LT_ERROR("Failed to save scene because pending Animator Controller edits could not be persisted.");
            return false;
        }

        if (auto* editorCamera = m_CameraManager.GetPerspective3D(m_EditorCameraId))
        {
            Scene::EditorCameraBookmark bookmark{};
            bookmark.Position = editorCamera->GetPosition();
            bookmark.YawDegrees = editorCamera->GetYawDegrees();
            bookmark.PitchDegrees = editorCamera->GetPitchDegrees();
            m_Scene->SetEditorCameraBookmark(bookmark);
        }

        const auto resolvedPathResult = Assets::ResolveAssetKeyToPath(assetKey);
        if (resolvedPathResult.IsFailure())
        {
            LT_ERROR("Failed to resolve scene asset key {}: {}", assetKey, resolvedPathResult.GetError().GetErrorMessage());
            return false;
        }

        const auto saveResult = m_Scene->SaveToFile(resolvedPathResult.GetValue());
        if (saveResult.IsFailure())
        {
            LT_ERROR("Failed to save scene {}: {}", assetKey, saveResult.GetError().GetErrorMessage());
            return false;
        }

        const Assets::AssetType assetTypeForSave = IsPrefabAssetKey(assetKey) ? Assets::AssetType::Prefab : Assets::AssetType::Scene;
        const auto importResult = Assets::AssetDatabase::GetInstance().ImportOrUpdate(assetKey, assetTypeForSave);
        if (importResult.IsFailure())
        {
            LT_WARN("Asset saved but failed to import into AssetDatabase ({}): {}", assetKey, importResult.GetError().GetErrorMessage());
        }

        if (const auto& pm = Project::ProjectManager::GetInstance(); pm.HasOpenProject())
        {
            PersistProjectSessionState();
        }

        if (IsPrefabAssetKey(assetKey))
            LT_INFO("Saved prefab {}", assetKey);
        else
            LT_INFO("Saved scene {}", assetKey);
        m_EditorUndoService.MarkSaved();
        return true;
    }
}  // namespace Limitless
