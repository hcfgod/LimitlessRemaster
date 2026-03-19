#include "PrecompiledHeader.h"
#include "EditorLayer.h"

#include "Audio/AudioEngine.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetImportPipeline.h"
#include "Assets/AssetPaths.h"
#include "Core/Debug/Log.h"
#include "EditorInspectorPanel.h"
#include "Project/BuildSettings.h"
#include "Project/ProjectManager.h"
#include "Project/ProjectSettings.h"
#include "Scene/Components/AudioComponents.h"
#include "Scene/Components/RenderingComponents.h"
#include "Scene/Components/ScriptingComponents.h"
#include "Scene/Scene.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <unordered_set>
#include <vector>

namespace Limitless
{
    namespace
    {
        constexpr const char* kAssetTexturePayload = "ASSET_TEXTURE";
        constexpr const char* kAssetAudioPayload = "ASSET_AUDIO";
        constexpr const char* kAssetMovePayload = "ASSET_MOVE";
        constexpr const char* kAssetScenePayload = "ASSET_SCENE";
        constexpr const char* kAssetMaterialPayload = "ASSET_MATERIAL";
        constexpr const char* kAssetPrefabPayload = "ASSET_PREFAB";
        constexpr const char* kAssetShaderPayload = "ASSET_SHADER";
        constexpr const char* kAssetFontPayload = "ASSET_FONT";
    }

    void EditorLayer::SynchronizeProjectPanelGridScale(float gridScale)
    {
        const float clampedGridScale = std::clamp(gridScale, 0.0f, 1.80f);
        m_ProjectPanelState.GridScale = clampedGridScale;
        m_ProjectPanelState.GridScaleChanged = false;

        for (auto& additionalProjectPanel : m_AdditionalProjectPanels)
        {
            additionalProjectPanel.State.GridScale = clampedGridScale;
            additionalProjectPanel.State.GridScaleChanged = false;
        }
    }

    void EditorLayer::DrawProjectPanel()
    {
        if (!m_ShowProjectPanel)
            return;

        const std::string prevClipKey = m_SelectedAnimationClipAssetKey;
        const std::string prevControllerKey = m_SelectedAnimatorControllerAssetKey;
        const std::string prevInputActionsKey = m_SelectedInputActionsAssetKey;

        EditorProjectPanel::Draw(
            "Project",
            m_ShowProjectPanel,
            m_ProjectPanelState,
            m_MaterialPreviewCache,
            m_SelectedEntity,
            m_SelectedTextureAssetKey,
            m_CachedTextureAsset,
            m_SelectedMaterialAssetKey,
            m_CachedMaterialAsset,
            m_SelectedNativeScriptAssetKey,
            m_SelectedPrefabAssetKey,
            m_SelectedTilesetAssetKey,
            m_SelectedAudioMixerAssetKey,
            m_SelectedInputActionsAssetKey,
            m_SelectedAnimationClipAssetKey,
            m_SelectedAnimatorControllerAssetKey,
            kAssetTexturePayload,
            kAssetAudioPayload,
            kAssetMovePayload,
            kAssetScenePayload,
            kAssetMaterialPayload,
            kAssetPrefabPayload,
            kAssetShaderPayload,
            kAssetFontPayload,
            [this](const std::string& assetKey) { LoadSceneFromAssetKey(assetKey); },
            [this](const std::string& assetKey) {
                m_SelectedInputActionsAssetKey = assetKey;
                m_ProjectPanelState.RequestFocusInputActionsEditor = true;
                m_ShowInputActionsPanel = true;
            },
            [this](const std::filesystem::path& relativeFolderPath) {
                const std::string createdSceneAssetKey = CreateSceneAssetInFolder(relativeFolderPath);
                if (!createdSceneAssetKey.empty())
                    LoadSceneFromAssetKey(createdSceneAssetKey);
            },
            [this](const std::filesystem::path& relativeFolderPath, const std::string& preferredName) {
                (void)CreateMaterialAssetInFolder(relativeFolderPath, preferredName);
            },
            [this](const std::filesystem::path& relativeFolderPath, const std::string& preferredName) {
                (void)CreateTilesetAssetInFolder(relativeFolderPath, preferredName);
            },
            [this](const std::filesystem::path& relativeFolderPath, const std::string& preferredName) {
                (void)CreateAudioMixerAssetInFolder(relativeFolderPath, preferredName);
            },
            [this](const std::filesystem::path& relativeFolderPath, const std::string& preferredName) {
                (void)CreateInputActionsAssetInFolder(relativeFolderPath, preferredName);
            },
            [this](const std::filesystem::path& relativeFolderPath, const std::string& preferredName) {
                (void)CreateAnimationClipAssetInFolder(relativeFolderPath, preferredName);
            },
            [this](const std::filesystem::path& relativeFolderPath, const std::string& preferredName) {
                (void)CreateAnimatorControllerAssetInFolder(relativeFolderPath, preferredName);
            },
            [this](entt::entity entity, const std::filesystem::path& relativeFolderPath) {
                (void)CreatePrefabFromEntityInFolder(entity, relativeFolderPath);
            },
            [this](const std::string& prefabAssetKey) {
                (void)OpenPrefabAssetForEditing(prefabAssetKey);
            },
            [this](const std::string& prefabAssetKey) {
                (void)InstantiatePrefabAtParent(prefabAssetKey, entt::null);
            },
            [this](const std::string& sceneAssetKey) { SetProjectDefaultSceneAssetKey(sceneAssetKey); },
            [this](const std::string& oldAssetKey, const std::string& newAssetKey) {
                if (oldAssetKey.empty() || newAssetKey.empty() || oldAssetKey == newAssetKey)
                    return;

                EditorInspectorPanel::OnNativeScriptAssetRenamed(oldAssetKey, newAssetKey);

                if (m_SelectedMaterialAssetKey == oldAssetKey)
                {
                    m_SelectedMaterialAssetKey = newAssetKey;
                    m_CachedMaterialAsset.reset();
                }
                if (m_SelectedNativeScriptAssetKey == oldAssetKey)
                    m_SelectedNativeScriptAssetKey = newAssetKey;
                if (m_SelectedPrefabAssetKey == oldAssetKey)
                    m_SelectedPrefabAssetKey = newAssetKey;
                if (m_SelectedTilesetAssetKey == oldAssetKey)
                    m_SelectedTilesetAssetKey = newAssetKey;
                if (m_SelectedAudioMixerAssetKey == oldAssetKey)
                    m_SelectedAudioMixerAssetKey = newAssetKey;
                if (m_SelectedInputActionsAssetKey == oldAssetKey)
                    m_SelectedInputActionsAssetKey = newAssetKey;
                if (m_SelectedAnimationClipAssetKey == oldAssetKey)
                    m_SelectedAnimationClipAssetKey = newAssetKey;
                if (m_SelectedAnimatorControllerAssetKey == oldAssetKey)
                    m_SelectedAnimatorControllerAssetKey = newAssetKey;
                if (m_CurrentSceneAssetKey == oldAssetKey)
                    m_CurrentSceneAssetKey = newAssetKey;
                if (m_EditSceneStoredAssetKey == oldAssetKey)
                    m_EditSceneStoredAssetKey = newAssetKey;
                if (m_PrefabModeReturnSceneAssetKey == oldAssetKey)
                    m_PrefabModeReturnSceneAssetKey = newAssetKey;

                if (auto& projectManager = Project::ProjectManager::GetInstance(); projectManager.HasOpenProject())
                {
                    std::string renamedAssetGuid;
                    if (const auto renamedRecord = Assets::AssetDatabase::GetInstance().FindByKey(newAssetKey);
                        renamedRecord.IsSuccess())
                    {
                        renamedAssetGuid = renamedRecord.GetValue().Guid;
                    }

                    const auto updateBuildSceneEntriesForRename =
                        [&oldAssetKey, &newAssetKey, &renamedAssetGuid](Project::BuildSettings& settings) -> bool
                    {
                        bool updatedAny = false;
                        for (auto& sceneEntry : settings.BuildScenes)
                        {
                            const bool matchedByKey = (sceneEntry.Key == oldAssetKey);
                            const bool matchedByGuid =
                                !renamedAssetGuid.empty() &&
                                !sceneEntry.Guid.empty() &&
                                sceneEntry.Guid == renamedAssetGuid;
                            if (!matchedByKey && !matchedByGuid)
                                continue;

                            if (sceneEntry.Key != newAssetKey)
                            {
                                sceneEntry.Key = newAssetKey;
                                updatedAny = true;
                            }

                            if (!renamedAssetGuid.empty() && sceneEntry.Guid != renamedAssetGuid)
                            {
                                sceneEntry.Guid = renamedAssetGuid;
                                updatedAny = true;
                            }
                        }

                        return updatedAny;
                    };

                    if (m_BuildSettingsPanelState.SettingsLoaded)
                    {
                        if (updateBuildSceneEntriesForRename(m_BuildSettingsPanelState.Settings))
                        {
                            const auto saveBuildSettingsResult = Project::SaveBuildSettings(projectManager.GetProjectRoot(), m_BuildSettingsPanelState.Settings);
                            if (saveBuildSettingsResult.IsFailure())
                            {
                                LT_WARN("Failed to update build settings scenes after rename '{}' -> '{}': {}",
                                    oldAssetKey,
                                    newAssetKey,
                                    saveBuildSettingsResult.GetError().GetErrorMessage());
                            }
                        }
                    }
                    else
                    {
                        const auto loadBuildSettingsResult = Project::LoadBuildSettings(projectManager.GetProjectRoot());
                        if (loadBuildSettingsResult.IsFailure())
                        {
                            LT_WARN("Failed to load build settings while processing rename '{}' -> '{}': {}",
                                oldAssetKey,
                                newAssetKey,
                                loadBuildSettingsResult.GetError().GetErrorMessage());
                        }
                        else
                        {
                            Project::BuildSettings settings = loadBuildSettingsResult.GetValue();
                            if (updateBuildSceneEntriesForRename(settings))
                            {
                                const auto saveBuildSettingsResult = Project::SaveBuildSettings(projectManager.GetProjectRoot(), settings);
                                if (saveBuildSettingsResult.IsFailure())
                                {
                                    LT_WARN("Failed to persist build settings scenes after rename '{}' -> '{}': {}",
                                        oldAssetKey,
                                        newAssetKey,
                                        saveBuildSettingsResult.GetError().GetErrorMessage());
                                }
                            }
                        }
                    }

                    if (const auto definition = projectManager.GetProjectDefinition();
                        definition.has_value() && definition->DefaultScene.Key == oldAssetKey)
                    {
                        const auto saveResult = projectManager.SetDefaultSceneAssetKey(newAssetKey);
                        if (saveResult.IsFailure())
                        {
                            LT_WARN("Failed to update default scene after rename '{}' -> '{}': {}",
                                oldAssetKey,
                                newAssetKey,
                                saveResult.GetError().GetErrorMessage());
                        }
                    }
                    PersistProjectSessionState();
                }
                if (m_ProjectAudioSettings.MixerAssetKey == oldAssetKey)
                {
                    m_ProjectAudioSettings.MixerAssetKey = newAssetKey;
                    m_ProjectSettingsPanelState.Audio.MixerAssetKey = newAssetKey;
                    m_ProjectAppliedAudioMixerAssetKey.clear();
                    m_ProjectAppliedAudioMixerLastWriteTimeTicks = 0;
                    ApplyProjectAudioSettings();
                    if (const auto& projectManager = Project::ProjectManager::GetInstance(); projectManager.HasOpenProject())
                    {
                        const auto saveAudioResult = Project::SaveAudioSettings(projectManager.GetProjectRoot(), m_ProjectAudioSettings);
                        if (saveAudioResult.IsFailure())
                            LT_WARN("Failed to persist audio settings after audio mixer rename: {}", saveAudioResult.GetError().GetErrorMessage());
                    }
                }

                if (!m_Scene)
                {
                    return;
                }

                bool updatedAnyMaterialReference = false;
                bool updatedAnyAudioReference = false;
                bool updatedAnyAnimationReference = false;
                const auto updateMaterialReferencesInScene = [&oldAssetKey, &newAssetKey, &updatedAnyMaterialReference](Scene* scene) {
                    if (!scene)
                        return;

                    auto& registry = scene->GetRegistry();
                    auto materialView = registry.view<MaterialComponent>();
                    for (entt::entity entity : materialView)
                    {
                        auto& material = materialView.get<MaterialComponent>(entity);
                        if (material.MaterialKey == oldAssetKey)
                        {
                            material.MaterialKey = newAssetKey;
                            material.CachedMaterial.reset();
                            material.MaterialLoadAttempted = false;
                            updatedAnyMaterialReference = true;
                        }
                    }
                };

                const auto updateAudioReferencesInScene = [&oldAssetKey, &newAssetKey, &updatedAnyAudioReference](Scene* scene) {
                    if (!scene)
                        return;

                    auto& registry = scene->GetRegistry();
                    auto audioView = registry.view<AudioSourceComponent>();
                    for (entt::entity entity : audioView)
                    {
                        auto& audioSource = audioView.get<AudioSourceComponent>(entity);
                        if (audioSource.AudioClipKey == oldAssetKey)
                        {
                            if (audioSource.RuntimeVoiceId != 0)
                                Audio::AudioEngine::GetInstance().Stop(audioSource.RuntimeVoiceId);
                            audioSource.AudioClipKey = newAssetKey;
                            audioSource.RuntimeVoiceId = 0;
                            audioSource.RuntimePlaybackStarted = false;
                            audioSource.RuntimePlayOnStartConsumed = false;
                            audioSource.RuntimeHasPreviousWorldPosition = false;
                            audioSource.RuntimePreviousWorldPosition = glm::vec3(0.0f);
                            updatedAnyAudioReference = true;
                        }
                    }
                };

                const auto updateAnimationReferencesInScene = [&oldAssetKey, &newAssetKey, &updatedAnyAnimationReference](Scene* scene) {
                    if (!scene)
                        return;

                    auto& registry = scene->GetRegistry();
                    auto animatorView = registry.view<AnimatorComponent>();
                    for (entt::entity entity : animatorView)
                    {
                        auto& animator = animatorView.get<AnimatorComponent>(entity);
                        if (animator.ControllerKey == oldAssetKey)
                        {
                            animator.ControllerKey = newAssetKey;
                            animator.CachedController.reset();
                            animator.ControllerLoadAttempted = false;
                            animator.RuntimeInitialized = false;
                            updatedAnyAnimationReference = true;
                        }
                        if (animator.DefaultClipKey == oldAssetKey)
                        {
                            animator.DefaultClipKey = newAssetKey;
                            animator.CachedDefaultClip.reset();
                            animator.DefaultClipLoadAttempted = false;
                            animator.RuntimeInitialized = false;
                            updatedAnyAnimationReference = true;
                        }
                        if (animator.RuntimeSpriteTextureOverrideKey == oldAssetKey)
                        {
                            animator.RuntimeSpriteTextureOverrideKey = newAssetKey;
                            animator.RuntimeCachedSpriteTextureOverride.reset();
                            animator.RuntimeSpriteTextureOverrideLoadAttempted = false;
                            updatedAnyAnimationReference = true;
                        }
                    }
                };

                bool updatedAnyNativeScriptPath = false;
                const auto toScriptAssetRelativePathWithoutExtension = [](const std::string& key, std::string& outRelativePath) -> bool {
                    if (key.rfind("Assets/", 0) != 0)
                        return false;
                    std::filesystem::path path = key.substr(std::strlen("Assets/"));
                    std::string extension = path.extension().string();
                    for (char& character : extension)
                        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
                    if (extension != ".h" && extension != ".cpp")
                        return false;
                    path.replace_extension("");
                    outRelativePath = path.generic_string();
                    return !outRelativePath.empty();
                };
                std::string oldScriptRelativePath;
                std::string newScriptRelativePath;
                const std::string oldScriptClassName = std::filesystem::path(oldAssetKey).stem().string();
                const std::string newScriptClassName = std::filesystem::path(newAssetKey).stem().string();
                const bool isScriptRename =
                    toScriptAssetRelativePathWithoutExtension(oldAssetKey, oldScriptRelativePath) &&
                    toScriptAssetRelativePathWithoutExtension(newAssetKey, newScriptRelativePath);

                const auto updateScriptReferencesInScene =
                    [&oldScriptRelativePath, &newScriptRelativePath, &oldScriptClassName, &newScriptClassName, &updatedAnyNativeScriptPath](Scene* scene) {
                    if (!scene)
                        return;
                    auto& registry = scene->GetRegistry();
                    auto scriptView = registry.view<ScriptComponent>();
                    for (entt::entity scriptEntity : scriptView)
                    {
                        auto& scriptComponent = scriptView.get<ScriptComponent>(scriptEntity);
                        NativeScriptEntry* scriptEntry = scriptComponent.TryGetNativeEntry();
                        if (!scriptEntry)
                            continue;
                        const bool matchedByStoredPath = (scriptEntry->ScriptAssetRelativePath == oldScriptRelativePath);
                        const bool matchedByLegacyClassOnly =
                            scriptEntry->ScriptAssetRelativePath.empty() &&
                            !oldScriptClassName.empty() &&
                            (scriptEntry->ScriptClassName == oldScriptClassName);
                        if (matchedByStoredPath || matchedByLegacyClassOnly)
                        {
                            scriptEntry->ScriptAssetRelativePath = newScriptRelativePath;
                            if (!newScriptClassName.empty())
                                scriptEntry->ScriptClassName = newScriptClassName;
                            scriptEntry->RuntimeInitialized = false;
                            scriptEntry->RuntimeInstance.reset();
                            updatedAnyNativeScriptPath = true;
                        }
                    }
                };

                updateMaterialReferencesInScene(m_Scene.get());
                updateMaterialReferencesInScene(m_EditSceneStored.get());
                updateAudioReferencesInScene(m_Scene.get());
                updateAudioReferencesInScene(m_EditSceneStored.get());
                updateAnimationReferencesInScene(m_Scene.get());
                updateAnimationReferencesInScene(m_EditSceneStored.get());
                if (isScriptRename)
                {
                    updateScriptReferencesInScene(m_Scene.get());
                    updateScriptReferencesInScene(m_EditSceneStored.get());
                }

                if ((updatedAnyMaterialReference || updatedAnyAudioReference || updatedAnyAnimationReference || updatedAnyNativeScriptPath) &&
                    m_PlayModeState == EditorPlayModeState::Edit &&
                    !m_CurrentSceneAssetKey.empty())
                {
                    if (!SaveSceneToAssetKey(m_CurrentSceneAssetKey))
                    {
                        LT_WARN("Asset rename updated scene references but failed to auto-save '{}'.", m_CurrentSceneAssetKey);
                    }
                }
            },
            [this](const std::vector<std::string>& sceneAssetKeys) -> bool {
                if (sceneAssetKeys.empty())
                    return false;

                auto& projectManager = Project::ProjectManager::GetInstance();
                if (!projectManager.HasOpenProject())
                    return false;

                const std::filesystem::path projectRoot = projectManager.GetProjectRoot();
                if (projectRoot.empty())
                    return false;

                struct DeletedSceneAssetSnapshot
                {
                    std::string AssetKey;
                    std::string Guid;
                    std::filesystem::path AssetPath;
                    std::filesystem::path MetaPath;
                    std::vector<uint8_t> AssetBytes;
                    std::vector<uint8_t> MetaBytes;
                    bool HadMeta = false;
                };

                struct SceneDeleteUndoState
                {
                    std::filesystem::path ProjectRoot;
                    bool BuildSettingsWereLoaded = false;
                    bool BuildSettingsChanged = false;
                    std::vector<DeletedSceneAssetSnapshot> Snapshots;
                    Project::BuildSettings BuildSettingsBefore;
                    Project::BuildSettings BuildSettingsAfter;
                };

                const auto readBinaryFile = [](const std::filesystem::path& filePath, std::vector<uint8_t>& outBytes) -> bool {
                    std::ifstream inputStream(filePath, std::ios::in | std::ios::binary);
                    if (!inputStream.is_open())
                        return false;

                    inputStream.seekg(0, std::ios::end);
                    const std::streamoff size = inputStream.tellg();
                    if (size < 0)
                        return false;

                    outBytes.resize(static_cast<size_t>(size));
                    inputStream.seekg(0, std::ios::beg);
                    if (size > 0 && !inputStream.read(reinterpret_cast<char*>(outBytes.data()), size))
                        return false;
                    return true;
                };

                const auto writeBinaryFile = [](const std::filesystem::path& filePath, const std::vector<uint8_t>& bytes) -> bool {
                    std::error_code directoryError;
                    if (filePath.has_parent_path())
                        std::filesystem::create_directories(filePath.parent_path(), directoryError);
                    if (directoryError)
                        return false;

                    std::ofstream outputStream(filePath, std::ios::out | std::ios::binary | std::ios::trunc);
                    if (!outputStream.is_open())
                        return false;
                    if (!bytes.empty())
                        outputStream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                    return outputStream.good();
                };

                auto isSceneAssetKey = [](const std::string& assetKey) -> bool {
                    if (assetKey.rfind("Assets/", 0) != 0)
                        return false;
                    std::string lowerKey = assetKey;
                    std::transform(lowerKey.begin(), lowerKey.end(), lowerKey.begin(), [](unsigned char character) {
                        return static_cast<char>(std::tolower(character));
                    });
                    return lowerKey.ends_with(".scene.json");
                };

                std::vector<DeletedSceneAssetSnapshot> snapshots;
                snapshots.reserve(sceneAssetKeys.size());
                std::unordered_set<std::string> deduplicatedSceneKeys;
                deduplicatedSceneKeys.reserve(sceneAssetKeys.size());
                for (const std::string& assetKey : sceneAssetKeys)
                {
                    if (!isSceneAssetKey(assetKey) || !deduplicatedSceneKeys.insert(assetKey).second)
                        continue;

                    const auto resolveResult = Assets::ResolveAssetKeyToPath(assetKey);
                    if (resolveResult.IsFailure())
                        continue;

                    const std::filesystem::path scenePath = resolveResult.GetValue();
                    std::error_code existsError;
                    if (!std::filesystem::exists(scenePath, existsError) ||
                        !std::filesystem::is_regular_file(scenePath, existsError))
                    {
                        continue;
                    }

                    DeletedSceneAssetSnapshot snapshot;
                    snapshot.AssetKey = assetKey;
                    snapshot.AssetPath = scenePath;
                    snapshot.MetaPath = scenePath.parent_path() / (scenePath.filename().string() + ".meta");

                    if (!readBinaryFile(scenePath, snapshot.AssetBytes))
                    {
                        LT_WARN("Failed to capture scene file '{}' for undoable delete.", scenePath.string());
                        return false;
                    }

                    if (const auto sceneRecord = Assets::AssetDatabase::GetInstance().FindByKey(assetKey);
                        sceneRecord.IsSuccess())
                    {
                        snapshot.Guid = sceneRecord.GetValue().Guid;
                    }

                    std::error_code metaExistsError;
                    snapshot.HadMeta = std::filesystem::exists(snapshot.MetaPath, metaExistsError);
                    if (snapshot.HadMeta)
                    {
                        if (!readBinaryFile(snapshot.MetaPath, snapshot.MetaBytes))
                        {
                            LT_WARN("Failed to capture scene meta file '{}' for undoable delete.", snapshot.MetaPath.string());
                            return false;
                        }
                    }

                    snapshots.push_back(std::move(snapshot));
                }

                if (snapshots.empty())
                    return false;

                const bool buildSettingsWereLoaded = m_BuildSettingsPanelState.SettingsLoaded;
                Project::BuildSettings buildSettingsBefore;
                if (buildSettingsWereLoaded)
                {
                    buildSettingsBefore = m_BuildSettingsPanelState.Settings;
                }
                else
                {
                    const auto loadBuildSettingsResult = Project::LoadBuildSettings(projectRoot);
                    if (loadBuildSettingsResult.IsFailure())
                    {
                        LT_WARN("Failed to load build settings before scene delete: {}",
                            loadBuildSettingsResult.GetError().GetErrorMessage());
                        return false;
                    }
                    buildSettingsBefore = loadBuildSettingsResult.GetValue();
                }

                std::unordered_set<std::string> deletedSceneKeySet;
                std::unordered_set<std::string> deletedSceneGuidSet;
                deletedSceneKeySet.reserve(snapshots.size());
                deletedSceneGuidSet.reserve(snapshots.size());
                for (const auto& snapshot : snapshots)
                {
                    deletedSceneKeySet.insert(snapshot.AssetKey);
                    if (!snapshot.Guid.empty())
                        deletedSceneGuidSet.insert(snapshot.Guid);
                }

                Project::BuildSettings buildSettingsAfter = buildSettingsBefore;
                bool buildSettingsChanged = false;
                auto& buildSceneEntries = buildSettingsAfter.BuildScenes;
                const auto removeIt = std::remove_if(buildSceneEntries.begin(), buildSceneEntries.end(),
                    [&deletedSceneKeySet, &deletedSceneGuidSet, &buildSettingsChanged](const Project::BuildSceneEntry& sceneEntry) {
                        const bool matchedByKey = deletedSceneKeySet.find(sceneEntry.Key) != deletedSceneKeySet.end();
                        const bool matchedByGuid =
                            !sceneEntry.Guid.empty() &&
                            deletedSceneGuidSet.find(sceneEntry.Guid) != deletedSceneGuidSet.end();
                        if (matchedByKey || matchedByGuid)
                            buildSettingsChanged = true;
                        return matchedByKey || matchedByGuid;
                    });
                buildSceneEntries.erase(removeIt, buildSceneEntries.end());

                auto undoState = std::make_shared<SceneDeleteUndoState>();
                undoState->ProjectRoot = projectRoot;
                undoState->BuildSettingsWereLoaded = buildSettingsWereLoaded;
                undoState->BuildSettingsChanged = buildSettingsChanged;
                undoState->Snapshots = snapshots;
                undoState->BuildSettingsBefore = buildSettingsBefore;
                undoState->BuildSettingsAfter = buildSettingsAfter;

                const auto applyBuildSettings =
                    [this, undoState](const Project::BuildSettings& settings) -> bool {
                    if (undoState->BuildSettingsWereLoaded)
                        m_BuildSettingsPanelState.Settings = settings;

                    const auto saveBuildSettingsResult = Project::SaveBuildSettings(undoState->ProjectRoot, settings);
                    if (saveBuildSettingsResult.IsFailure())
                    {
                        LT_WARN("Failed to save build settings after scene delete/restore: {}",
                            saveBuildSettingsResult.GetError().GetErrorMessage());
                        return false;
                    }
                    return true;
                };

                const auto performDelete =
                    [undoState, applyBuildSettings]() -> bool {
                    for (const auto& snapshot : undoState->Snapshots)
                    {
                        std::error_code removeError;
                        if (std::filesystem::exists(snapshot.AssetPath, removeError))
                        {
                            if (!std::filesystem::remove(snapshot.AssetPath, removeError) || removeError)
                                return false;
                        }

                        removeError.clear();
                        if (snapshot.HadMeta && std::filesystem::exists(snapshot.MetaPath, removeError))
                            std::filesystem::remove(snapshot.MetaPath, removeError);
                        if (removeError)
                            return false;
                    }

                    (void)Assets::AssetImportPipeline::ReimportChanged(true);
                    if (undoState->BuildSettingsChanged && !applyBuildSettings(undoState->BuildSettingsAfter))
                        return false;

                    return true;
                };

                const auto performRestore =
                    [undoState, applyBuildSettings, writeBinaryFile]() -> bool {
                    for (const auto& snapshot : undoState->Snapshots)
                    {
                        if (!writeBinaryFile(snapshot.AssetPath, snapshot.AssetBytes))
                            return false;
                        if (snapshot.HadMeta && !writeBinaryFile(snapshot.MetaPath, snapshot.MetaBytes))
                            return false;
                    }

                    (void)Assets::AssetImportPipeline::ReimportChanged(true);
                    if (undoState->BuildSettingsChanged && !applyBuildSettings(undoState->BuildSettingsBefore))
                        return false;

                    return true;
                };

                if (!performDelete())
                    return false;

                const std::string commandLabel = (undoState->Snapshots.size() == 1)
                    ? "Delete Scene Asset"
                    : "Delete Scene Assets";
                if (!m_EditorUndoService.ExecuteLambdaCommand(commandLabel,
                        [performRestore]() mutable { return performRestore(); },
                        [performDelete]() mutable { return performDelete(); }))
                {
                    LT_WARN("Scene delete completed but failed to register undo command.");
                }

                return true;
            },
            [this](const std::string& scriptAssetKey) {
                (void)EditorInspectorPanel::OpenNativeScriptEditorForAssetKey(scriptAssetKey);
            });

        const bool shouldPersistProjectSessionState =
            m_ProjectPanelState.TreeExpansionStateChanged ||
            m_ProjectPanelState.BrowseLocationChanged ||
            m_ProjectPanelState.GridScaleChanged;
        if (m_ProjectPanelState.GridScaleChanged)
            SynchronizeProjectPanelGridScale(m_ProjectPanelState.GridScale);

        if (shouldPersistProjectSessionState)
            PersistProjectSessionState();

        if (!m_SelectedAnimationClipAssetKey.empty() && m_SelectedAnimationClipAssetKey != prevClipKey)
            m_ShowAnimationTimelinePanel = true;
        if (!m_SelectedAnimatorControllerAssetKey.empty() && m_SelectedAnimatorControllerAssetKey != prevControllerKey)
            m_ShowAnimatorGraphPanel = true;
        if (!m_SelectedInputActionsAssetKey.empty() && m_SelectedInputActionsAssetKey != prevInputActionsKey)
            m_ShowInputActionsPanel = true;
        if (m_ProjectPanelState.RequestFocusAnimationClipEditor && !m_SelectedAnimationClipAssetKey.empty())
            m_ShowAnimationTimelinePanel = true;
        if (m_ProjectPanelState.RequestFocusAnimatorControllerEditor && !m_SelectedAnimatorControllerAssetKey.empty())
            m_ShowAnimatorGraphPanel = true;
        if (m_ProjectPanelState.RequestFocusInputActionsEditor && !m_SelectedInputActionsAssetKey.empty())
            m_ShowInputActionsPanel = true;
    }

    void EditorLayer::DrawAdditionalProjectPanels()
    {
        bool shouldPersistProjectSessionState = false;
        for (auto& additional : m_AdditionalProjectPanels)
        {
            if (!additional.IsOpen)
                continue;

            EditorProjectPanel::Draw(
                additional.WindowName.c_str(),
                additional.IsOpen,
                additional.State,
                m_MaterialPreviewCache,
                m_SelectedEntity,
                m_SelectedTextureAssetKey,
                m_CachedTextureAsset,
                m_SelectedMaterialAssetKey,
                m_CachedMaterialAsset,
                m_SelectedNativeScriptAssetKey,
                m_SelectedPrefabAssetKey,
                m_SelectedTilesetAssetKey,
                m_SelectedAudioMixerAssetKey,
                m_SelectedInputActionsAssetKey,
                m_SelectedAnimationClipAssetKey,
                m_SelectedAnimatorControllerAssetKey,
                kAssetTexturePayload,
                kAssetAudioPayload,
                kAssetMovePayload,
                kAssetScenePayload,
                kAssetMaterialPayload,
                kAssetPrefabPayload,
                kAssetShaderPayload,
                kAssetFontPayload,
                [this](const std::string& assetKey) { LoadSceneFromAssetKey(assetKey); },
                [this, &additional](const std::string& assetKey) {
                    m_SelectedInputActionsAssetKey = assetKey;
                    additional.State.RequestFocusInputActionsEditor = true;
                    m_ProjectPanelState.RequestFocusInputActionsEditor = true;
                    m_ShowInputActionsPanel = true;
                },
                [this](const std::filesystem::path& relativeFolderPath) {
                    const std::string createdSceneAssetKey = CreateSceneAssetInFolder(relativeFolderPath);
                    if (!createdSceneAssetKey.empty())
                        LoadSceneFromAssetKey(createdSceneAssetKey);
                },
                [this](const std::filesystem::path& relativeFolderPath, const std::string& preferredName) {
                    (void)CreateMaterialAssetInFolder(relativeFolderPath, preferredName);
                },
                [this](const std::filesystem::path& relativeFolderPath, const std::string& preferredName) {
                    (void)CreateTilesetAssetInFolder(relativeFolderPath, preferredName);
                },
                [this](const std::filesystem::path& relativeFolderPath, const std::string& preferredName) {
                    (void)CreateAudioMixerAssetInFolder(relativeFolderPath, preferredName);
                },
                [this](const std::filesystem::path& relativeFolderPath, const std::string& preferredName) {
                    (void)CreateInputActionsAssetInFolder(relativeFolderPath, preferredName);
                },
                [this](const std::filesystem::path& relativeFolderPath, const std::string& preferredName) {
                    (void)CreateAnimationClipAssetInFolder(relativeFolderPath, preferredName);
                },
                [this](const std::filesystem::path& relativeFolderPath, const std::string& preferredName) {
                    (void)CreateAnimatorControllerAssetInFolder(relativeFolderPath, preferredName);
                },
                [this](entt::entity entity, const std::filesystem::path& relativeFolderPath) {
                    (void)CreatePrefabFromEntityInFolder(entity, relativeFolderPath);
                },
                [this](const std::string& prefabAssetKey) {
                    (void)OpenPrefabAssetForEditing(prefabAssetKey);
                },
                [this](const std::string& prefabAssetKey) {
                    (void)InstantiatePrefabAtParent(prefabAssetKey, entt::null);
                },
                [this](const std::string& sceneAssetKey) { SetProjectDefaultSceneAssetKey(sceneAssetKey); },
                [this](const std::string& oldAssetKey, const std::string& newAssetKey) {
                    // Additional project panels share rename handling with the main panel;
                    // asset rename side effects are already handled by the primary panel.
                    (void)oldAssetKey;
                    (void)newAssetKey;
                },
                [this](const std::vector<std::string>&) -> bool { return false; },
                [this](const std::string& scriptAssetKey) {
                    (void)EditorInspectorPanel::OpenNativeScriptEditorForAssetKey(scriptAssetKey);
                });

            if (additional.State.GridScaleChanged)
            {
                SynchronizeProjectPanelGridScale(additional.State.GridScale);
                shouldPersistProjectSessionState = true;
            }
            else if (additional.State.TreeExpansionStateChanged || additional.State.BrowseLocationChanged)
            {
                shouldPersistProjectSessionState = true;
            }
        }

        if (shouldPersistProjectSessionState)
            PersistProjectSessionState();

        // Remove closed instances.
        m_AdditionalProjectPanels.erase(
            std::remove_if(m_AdditionalProjectPanels.begin(), m_AdditionalProjectPanels.end(),
                [](const AdditionalProjectPanelInstance& instance) { return !instance.IsOpen; }),
            m_AdditionalProjectPanels.end());
    }
}
