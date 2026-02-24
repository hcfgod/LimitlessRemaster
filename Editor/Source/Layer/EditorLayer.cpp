#include "EditorLayer.h"
#include "EditorAssetNaming.h"
#include "Audio/AudioEngine.h"
#include "Audio/AudioMixerAsset.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetManager.h"
#include "Assets/AudioClipAsset.h"
#include "Assets/AssetPaths.h"
#include "Assets/AssetImportPipeline.h"
#include "Assets/AssetTypes.h"
#include "Assets/SpriteImportSettings.h"
#include "Assets/TextureAsset.h"
#include "Assets/TileAsset.h"
#include "Core/Application.h"
#include "Core/Debug/Log.h"
#include "Editor/EditorCameraController.h"
#include "EditorInspectorPanel.h"
#include "EditorInspectorPanelAssetInspectors.h"
#include "EditorMenuBar.h"
#include "EditorPrefabSystem.h"
#include "EditorPlayMode.h"
#include "EditorProjectDialog.h"
#include "EditorProjectPanel.h"
#include "EditorRuntimeOperations.h"
#include "EditorScenePanel.h"
#include "EditorViewportPanel.h"
#include "Scene/ParticleEmitterSystem.h"
#include "Scripting/ScriptCoreModuleRuntime.h"
#include "Core/Input/InputSystem.h"
#include "Core/PerformanceMonitor.h"
#include "Graphics/Camera/Camera.h"
#include "Graphics/Camera/OrthographicCamera2D.h"
#include "Graphics/Camera/PerspectiveCamera3D.h"
#include "Graphics/Lighting2DRenderer.h"
#include "Graphics/Renderer2D.h"
#include "ImGui/ImGuiLayer.h"
#include "Project/ProjectManager.h"
#include "Project/ProjectSettings.h"
#include "Scene/Components.h"
#include "Scene/Scene.h"
#include "imgui/imgui.h"

#include <fstream>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <nlohmann/json.hpp>
#include <sstream>
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
        constexpr const char* kDefaultSceneFileName = "SampleScene.scene.json";
        constexpr const char* kSceneFileSuffix = ".scene.json";
        constexpr const char* kEditorSessionStateRelativePath = "Project/Settings/EditorSessionState.json";
        constexpr uint32_t kEditorSessionStateVersion = 5;
        constexpr std::string_view kSceneAssetSuffix = ".scene.json";

        struct EditorSessionStateData final
        {
            std::string LastOpenedSceneAssetKey;
            EditorInspectorPanel::NativeScriptEditorSessionState NativeScriptEditorState;
            bool ShowProjectSettingsWindow = false;
            bool ShowAssetDiagnosticsWindow = false;
            bool ShowPerformancePanel = false;
            bool ShowConsoleWindow = true;
            bool ProjectAssetsRootExpanded = true;
            std::unordered_map<std::string, bool> ProjectFolderExpansionState;
        };

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

        std::string ToLowerAscii(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return text;
        }

        std::string ExtractSceneNameFromAssetKey(const std::string& assetKey)
        {
            const std::string normalizedKey = NormalizeSlashes(assetKey);
            const std::filesystem::path keyPath(normalizedKey);
            const std::string filename = keyPath.filename().string();
            if (filename.empty())
                return {};

            if (filename.ends_with(kSceneAssetSuffix))
                return filename.substr(0, filename.size() - kSceneAssetSuffix.size());

            return keyPath.stem().string();
        }

        std::optional<std::string> ResolveSceneNameToAssetKey(const std::string& sceneName)
        {
            const std::string requestedName = sceneName.ends_with(kSceneAssetSuffix)
                ? sceneName.substr(0, sceneName.size() - kSceneAssetSuffix.size())
                : sceneName;
            if (requestedName.empty())
                return std::nullopt;

            std::vector<std::string> caseSensitiveMatches;
            std::vector<std::string> caseInsensitiveMatches;
            const std::string requestedNameLower = ToLowerAscii(requestedName);

            const auto records = Assets::AssetDatabase::GetInstance().GetAllRecords();
            for (const auto& record : records)
            {
                if (record.Type != Assets::AssetType::Scene || record.Key.empty())
                    continue;

                const std::string candidateName = ExtractSceneNameFromAssetKey(record.Key);
                if (candidateName.empty())
                    continue;

                if (candidateName == requestedName)
                    caseSensitiveMatches.push_back(record.Key);

                if (ToLowerAscii(candidateName) == requestedNameLower)
                    caseInsensitiveMatches.push_back(record.Key);
            }

            if (caseSensitiveMatches.size() == 1)
                return caseSensitiveMatches.front();

            if (caseSensitiveMatches.size() > 1)
            {
                LT_WARN("SceneManager::LoadScene('{}') is ambiguous. {} scenes share this name.",
                    sceneName, caseSensitiveMatches.size());
                return std::nullopt;
            }

            if (caseInsensitiveMatches.size() == 1)
                return caseInsensitiveMatches.front();

            if (caseInsensitiveMatches.size() > 1)
            {
                LT_WARN("SceneManager::LoadScene('{}') is ambiguous after case-insensitive lookup. {} scenes share this name.",
                    sceneName, caseInsensitiveMatches.size());
                return std::nullopt;
            }

            return std::nullopt;
        }

        std::optional<std::string> ResolveSceneIdentifierToAssetKey(const std::string& sceneIdentifier)
        {
            if (sceneIdentifier.empty())
                return std::nullopt;

            const std::string normalizedIdentifier = NormalizeSlashes(sceneIdentifier);
            const bool looksLikePath = normalizedIdentifier.find('/') != std::string::npos;
            if (looksLikePath)
            {
                std::string sceneAssetKey = normalizedIdentifier;
                if (sceneAssetKey.rfind("Assets/", 0) != 0)
                    sceneAssetKey = "Assets/" + sceneAssetKey;

                if (!sceneAssetKey.ends_with(kSceneAssetSuffix))
                    sceneAssetKey += std::string(kSceneAssetSuffix);

                return sceneAssetKey;
            }

            return ResolveSceneNameToAssetKey(normalizedIdentifier);
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

        std::filesystem::path GetEditorSessionStatePathForProjectRoot(const std::filesystem::path& projectRoot)
        {
            if (projectRoot.empty())
            {
                return {};
            }
            return projectRoot / kEditorSessionStateRelativePath;
        }

        EditorSessionStateData ReadProjectSessionState(const std::filesystem::path& projectRoot)
        {
            const std::filesystem::path statePath = GetEditorSessionStatePathForProjectRoot(projectRoot);
            if (statePath.empty())
            {
                return {};
            }

            std::error_code ec;
            if (!std::filesystem::exists(statePath, ec))
            {
                return {};
            }

            try
            {
                std::ifstream in(statePath, std::ios::in | std::ios::binary);
                if (!in.is_open())
                {
                    return {};
                }

                nlohmann::json root;
                in >> root;
                if (!root.is_object())
                {
                    return {};
                }

                EditorSessionStateData state{};
                const uint32_t version = root.value("version", 0u);
                if (version == 1)
                {
                    state.LastOpenedSceneAssetKey = root.value("lastOpenedSceneAssetKey", std::string{});
                    return state;
                }

                if (version != 3 && version != 4 && version != kEditorSessionStateVersion)
                {
                    return {};
                }

                state.LastOpenedSceneAssetKey = root.value("lastOpenedSceneAssetKey", std::string{});
                state.NativeScriptEditorState.IsOpen = root.value("nativeScriptEditorIsOpen", false);
                state.NativeScriptEditorState.LastEditedScriptClassName = root.value("nativeScriptEditorLastClassName", std::string{});
                state.NativeScriptEditorState.LastEditedScriptAssetRelativePath = root.value("nativeScriptEditorLastAssetRelativePath", std::string{});
                state.NativeScriptEditorState.ShowDebugInfo = root.value("nativeScriptEditorShowDebugInfo", false);
                state.ShowProjectSettingsWindow = root.value("showProjectSettingsWindow", false);
                state.ShowAssetDiagnosticsWindow = root.value("showAssetDiagnosticsWindow", false);
                if (version >= 5)
                {
                    state.ShowPerformancePanel = root.value("showPerformancePanel", false);
                    state.ShowConsoleWindow = root.value("showConsoleWindow", true);
                }
                if (version >= 4)
                {
                    state.ProjectAssetsRootExpanded = root.value("projectAssetsRootExpanded", true);
                    if (const auto expansionStateIt = root.find("projectFolderExpansionState");
                        expansionStateIt != root.end() && expansionStateIt->is_object())
                    {
                        for (auto stateIt = expansionStateIt->begin(); stateIt != expansionStateIt->end(); ++stateIt)
                        {
                            if (!stateIt.value().is_boolean())
                                continue;
                            state.ProjectFolderExpansionState[stateIt.key()] = stateIt.value().get<bool>();
                        }
                    }
                }
                return state;
            }
            catch (...)
            {
                return {};
            }
        }

        void WriteProjectSessionState(const std::filesystem::path& projectRoot, const EditorSessionStateData& state)
        {
            if (projectRoot.empty())
            {
                return;
            }

            const std::filesystem::path statePath = GetEditorSessionStatePathForProjectRoot(projectRoot);
            if (statePath.empty())
            {
                return;
            }

            try
            {
                std::error_code ec;
                std::filesystem::create_directories(statePath.parent_path(), ec);
                if (ec)
                {
                    return;
                }

                nlohmann::json root;
                root["version"] = kEditorSessionStateVersion;
                root["lastOpenedSceneAssetKey"] = state.LastOpenedSceneAssetKey;
                root["nativeScriptEditorIsOpen"] = state.NativeScriptEditorState.IsOpen;
                root["nativeScriptEditorLastClassName"] = state.NativeScriptEditorState.LastEditedScriptClassName;
                root["nativeScriptEditorLastAssetRelativePath"] = state.NativeScriptEditorState.LastEditedScriptAssetRelativePath;
                root["nativeScriptEditorShowDebugInfo"] = state.NativeScriptEditorState.ShowDebugInfo;
                root["showProjectSettingsWindow"] = state.ShowProjectSettingsWindow;
                root["showAssetDiagnosticsWindow"] = state.ShowAssetDiagnosticsWindow;
                root["showPerformancePanel"] = state.ShowPerformancePanel;
                root["showConsoleWindow"] = state.ShowConsoleWindow;
                root["projectAssetsRootExpanded"] = state.ProjectAssetsRootExpanded;

                nlohmann::json folderExpansionRoot = nlohmann::json::object();
                for (const auto& [folderPath, expanded] : state.ProjectFolderExpansionState)
                    folderExpansionRoot[folderPath] = expanded;
                root["projectFolderExpansionState"] = std::move(folderExpansionRoot);

                const std::filesystem::path tmpPath = statePath.string() + ".tmp";
                {
                    std::ofstream out(tmpPath, std::ios::out | std::ios::binary | std::ios::trunc);
                    if (!out.is_open())
                    {
                        return;
                    }
                    out << root.dump(2);
                    out.flush();
                }

                std::filesystem::rename(tmpPath, statePath, ec);
                if (ec)
                {
                    ec.clear();
                    std::filesystem::remove(statePath, ec);
                    ec.clear();
                    std::filesystem::rename(tmpPath, statePath, ec);
                }
            }
            catch (...)
            {
                // Session state persistence is best-effort.
            }
        }

        std::string SceneDisplayNameFromFileName(const std::string& fileName)
        {
            return EditorAssetNaming::GetAssetDisplayNameFromFileName(fileName);
        }

        std::string NormalizeSceneFileName(const char* rawName)
        {
            std::string fileName = rawName ? rawName : "";
            while (!fileName.empty() && std::isspace(static_cast<unsigned char>(fileName.front())))
                fileName.erase(fileName.begin());
            while (!fileName.empty() && std::isspace(static_cast<unsigned char>(fileName.back())))
                fileName.pop_back();

            if (fileName.empty())
                fileName = "New Scene";

            for (char& character : fileName)
            {
                if (character == '/' || character == '\\' || character == ':' || character == '*' ||
                    character == '?' || character == '"' || character == '<' || character == '>' || character == '|')
                    character = '_';
            }

            std::string lowerFileName = fileName;
            std::transform(lowerFileName.begin(), lowerFileName.end(), lowerFileName.begin(), [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });

            std::string lowerSuffix = kSceneFileSuffix;
            if (lowerFileName.size() < lowerSuffix.size() ||
                lowerFileName.rfind(lowerSuffix) != (lowerFileName.size() - lowerSuffix.size()))
            {
                fileName += kSceneFileSuffix;
            }
            return fileName;
        }

        void StopAudioSourcesInScene(Scene* scene)
        {
            if (!scene)
                return;

            auto& registry = scene->GetRegistry();
            auto audioView = registry.view<AudioSourceComponent>();
            for (entt::entity entity : audioView)
            {
                auto& audioSource = audioView.get<AudioSourceComponent>(entity);
                if (audioSource.RuntimeVoiceId != 0)
                    Audio::AudioEngine::GetInstance().Stop(audioSource.RuntimeVoiceId);
                audioSource.RuntimeVoiceId = 0;
                audioSource.RuntimePlaybackStarted = false;
            }
        }

        struct AudioListener2DRuntimeState
        {
            bool HasListener = false;
            glm::vec2 Position = glm::vec2(0.0f);
        };

        using AudioListenerPositions2D = std::vector<glm::vec2>;

        struct AudioSpatialMix2D
        {
            float Gain = 1.0f;
            float Pan = 0.0f;
        };

        glm::vec2 ComputeEntityWorldPosition2D(const Scene& scene, entt::entity entity)
        {
            const glm::mat4 worldTransform = scene.GetWorldTransformMatrix(entity);
            return glm::vec2(worldTransform[3][0], worldTransform[3][1]);
        }

        bool TryFindPrimaryCameraEntity(const Scene& scene, entt::entity& outEntity)
        {
            const auto& registry = scene.GetRegistry();
            auto cameraView = registry.view<CameraComponent>();
            entt::entity fallbackEntity = entt::null;
            for (entt::entity entity : cameraView)
            {
                if (fallbackEntity == entt::null)
                    fallbackEntity = entity;
                const auto& camera = cameraView.get<CameraComponent>(entity);
                if (camera.IsPrimary)
                {
                    outEntity = entity;
                    return true;
                }
            }

            if (fallbackEntity != entt::null)
            {
                outEntity = fallbackEntity;
                return true;
            }

            return false;
        }

        AudioListenerPositions2D CollectAudioListenerPositions2D(const Scene& scene)
        {
            AudioListenerPositions2D listenerPositions;
            const auto& registry = scene.GetRegistry();

            auto listenerView = registry.view<AudioListener2DComponent>();
            for (entt::entity entity : listenerView)
            {
                if (!scene.IsEntityEnabledInHierarchy(entity))
                    continue;
                const auto& listener = listenerView.get<AudioListener2DComponent>(entity);
                if (!listener.Enabled)
                    continue;

                if (listener.UsePrimaryCameraPosition)
                {
                    entt::entity cameraEntity = entt::null;
                    if (TryFindPrimaryCameraEntity(scene, cameraEntity))
                    {
                        listenerPositions.push_back(ComputeEntityWorldPosition2D(scene, cameraEntity));
                        continue;
                    }
                }

                listenerPositions.push_back(ComputeEntityWorldPosition2D(scene, entity));
            }

            // Fallback behavior keeps authored scenes audible even before listeners are added.
            if (listenerPositions.empty())
            {
                entt::entity fallbackCameraEntity = entt::null;
                if (TryFindPrimaryCameraEntity(scene, fallbackCameraEntity))
                    listenerPositions.push_back(ComputeEntityWorldPosition2D(scene, fallbackCameraEntity));
            }

            return listenerPositions;
        }

        AudioListener2DRuntimeState ResolveNearestAudioListener2DRuntimeState(const AudioListenerPositions2D& listeners,
                                                                              const glm::vec2& sourcePosition)
        {
            AudioListener2DRuntimeState listenerState{};
            if (listeners.empty())
                return listenerState;

            listenerState.HasListener = true;
            listenerState.Position = listeners.front();
            float bestDistanceSq = glm::dot(sourcePosition - listenerState.Position, sourcePosition - listenerState.Position);
            for (size_t i = 1; i < listeners.size(); ++i)
            {
                const glm::vec2& listenerPosition = listeners[i];
                const float distanceSq = glm::dot(sourcePosition - listenerPosition, sourcePosition - listenerPosition);
                if (distanceSq < bestDistanceSq)
                {
                    bestDistanceSq = distanceSq;
                    listenerState.Position = listenerPosition;
                }
            }
            return listenerState;
        }

        AudioSpatialMix2D ComputeAudioSpatialMix2D(const AudioSourceComponent& audioSource,
                                                   const glm::vec2& sourcePosition,
                                                   const AudioListenerPositions2D& listeners)
        {
            AudioSpatialMix2D result{};
            if (audioSource.Space != AudioSourceComponent::PlaybackSpace::Spatial2D || listeners.empty())
                return result;

            const AudioListener2DRuntimeState listenerState =
                ResolveNearestAudioListener2DRuntimeState(listeners, sourcePosition);

            const float minDistance = std::max(0.001f, audioSource.SpatialMinDistance);
            const float maxDistance = std::max(minDistance, audioSource.SpatialMaxDistance);
            const float rolloffExponent = std::max(0.01f, audioSource.SpatialRolloffExponent);
            const float panStrength = std::clamp(audioSource.StereoPanStrength, 0.0f, 1.0f);

            const float distanceToListener = glm::length(sourcePosition - listenerState.Position);
            if (distanceToListener <= minDistance)
            {
                result.Gain = 1.0f;
            }
            else if (distanceToListener >= maxDistance)
            {
                result.Gain = 0.0f;
            }
            else
            {
                const float normalized = (distanceToListener - minDistance) / (maxDistance - minDistance);
                result.Gain = std::pow(std::max(0.0f, 1.0f - normalized), rolloffExponent);
            }

            const float panNormalizationDistance = std::max(maxDistance, 0.001f);
            const float signedPan = std::clamp((sourcePosition.x - listenerState.Position.x) / panNormalizationDistance, -1.0f, 1.0f);
            result.Pan = signedPan * panStrength;
            return result;
        }

        void UpdateSceneAudioSources(Scene* scene, EditorPlayModeState playModeState)
        {
            if (!scene)
                return;

            const bool runtimePlaybackAllowed =
                (playModeState == EditorPlayModeState::Play ||
                 playModeState == EditorPlayModeState::Simulate ||
                 playModeState == EditorPlayModeState::Pause);

            auto& registry = scene->GetRegistry();
            const AudioListenerPositions2D listenerPositions = CollectAudioListenerPositions2D(*scene);
            auto audioView = registry.view<AudioSourceComponent>();
            for (entt::entity entity : audioView)
            {
                auto& audioSource = audioView.get<AudioSourceComponent>(entity);
                const bool entityEnabled = scene->IsEntityEnabledInHierarchy(entity);
                if (audioSource.RuntimeVoiceId != 0 &&
                    !Audio::AudioEngine::GetInstance().IsVoiceActive(audioSource.RuntimeVoiceId))
                {
                    audioSource.RuntimeVoiceId = 0;
                }
                if (!entityEnabled)
                {
                    if (audioSource.RuntimeVoiceId != 0)
                        Audio::AudioEngine::GetInstance().Stop(audioSource.RuntimeVoiceId);
                    audioSource.RuntimeVoiceId = 0;
                    audioSource.RuntimePlaybackStarted = false;
                    continue;
                }
                if (!runtimePlaybackAllowed)
                {
                    // In Edit mode, leave manual preview playback alone.
                    continue;
                }

                const glm::vec2 sourcePosition = ComputeEntityWorldPosition2D(*scene, entity);
                const AudioSpatialMix2D spatialMix = ComputeAudioSpatialMix2D(audioSource, sourcePosition, listenerPositions);
                const float authoredVolume = audioSource.Muted ? 0.0f : std::max(0.0f, audioSource.Volume);
                const float runtimeVolume = authoredVolume * spatialMix.Gain;
                const float runtimePan = spatialMix.Pan;
                const float runtimePitch = std::max(0.01f, audioSource.Pitch);

                const bool shouldPlayOnStart =
                    audioSource.PlayOnStart &&
                    !audioSource.AudioClipKey.empty();

                if (shouldPlayOnStart && !audioSource.RuntimePlaybackStarted)
                {
                    auto clipAsset = Assets::AudioClipAsset::LoadBlocking(audioSource.AudioClipKey);
                    if (clipAsset && clipAsset->GetClip())
                    {
                        audioSource.RuntimeVoiceId = Audio::AudioEngine::GetInstance().PlayClip(
                            clipAsset->GetClip(),
                            runtimeVolume,
                            audioSource.Loop,
                            audioSource.MixerGroup,
                            runtimePan,
                            runtimePitch);
                        audioSource.RuntimePlaybackStarted = (audioSource.RuntimeVoiceId != 0);
                    }
                }
                else if (shouldPlayOnStart && audioSource.RuntimeVoiceId != 0)
                {
                    (void)Audio::AudioEngine::GetInstance().SetVoiceMixParameters(
                        audioSource.RuntimeVoiceId,
                        runtimeVolume,
                        runtimePan,
                        audioSource.MixerGroup,
                        runtimePitch);
                }
                else if (!shouldPlayOnStart && audioSource.RuntimeVoiceId != 0)
                {
                    Audio::AudioEngine::GetInstance().Stop(audioSource.RuntimeVoiceId);
                    audioSource.RuntimeVoiceId = 0;
                    audioSource.RuntimePlaybackStarted = false;
                }
            }
        }

    }  // anonymous namespace

    EditorLayer::EditorLayer()
        : Layer("EditorLayer")
    {
        const std::string defaultName = SceneDisplayNameFromFileName(kDefaultSceneFileName);
        const size_t copyCount = std::min(defaultName.size(), m_SaveSceneFileNameBuffer.size() - 1);
        std::copy_n(defaultName.c_str(), copyCount, m_SaveSceneFileNameBuffer.begin());
        m_SaveSceneFileNameBuffer[copyCount] = '\0';
    }

    EditorLayer::~EditorLayer() = default;

    void EditorLayer::OnAttach()
    {
        SceneManager::ClearPendingSceneTransition();
        m_EditorUndoService.Initialize(
            [this]() { return m_Scene.get(); },
            [this](std::unique_ptr<Scene> scene) { m_Scene = std::move(scene); },
            [this](std::unique_ptr<Scene>& snapshot) {
                if (!snapshot)
                    return false;
                m_Scene.swap(snapshot);
                return (m_Scene != nullptr);
            });

        // Unity-style startup:
        // Always begin in the Project Browser and require explicit Open/Create.
        // Never auto-open from the editor working directory.
        Project::ProjectManager::GetInstance().CloseProject();
        EditorProjectDialog::RequestOpen(m_ProjectDialogState, EditorProjectDialog::ProjectDialogMode::Open);

        EditorRuntimeOperations::Attach(
            m_SceneViewWidthPixels,
            m_SceneViewHeightPixels,
            m_Scene,
            m_CameraManager,
            m_EditorCameraId,
            m_EditorCameraController,
            m_SceneViewFramebuffer);

        Application::GetInstance().GetWindow().SetFileDropCallback([this](const std::vector<std::filesystem::path>& droppedPaths) {
            if (droppedPaths.empty())
            {
                return;
            }

            m_ProjectPanelState.PendingExternalDropPaths.insert(
                m_ProjectPanelState.PendingExternalDropPaths.end(),
                droppedPaths.begin(),
                droppedPaths.end());
        });

        ScriptCoreModuleRuntime::Initialize();
    }

    void EditorLayer::OnDetach()
    {
        SceneManager::ClearPendingSceneTransition();
        if (m_PlayModeState != EditorPlayModeState::Edit)
            ExitPlayMode();

        // Explicitly finalize any in-flight Build Settings build job before shutdown.
        EditorBuildSettingsPanel::Shutdown(m_BuildSettingsPanelState);

        // Persist pending animation asset editor changes before shutdown.
        if (!EditorAnimationTimelinePanel::ApplyPendingChanges(&m_EditorUndoService))
            LT_WARN("Editor exit: failed to auto-save pending Animation Clip edits.");
        if (!EditorAnimatorGraphPanel::ApplyPendingChanges(&m_EditorUndoService))
            LT_WARN("Editor exit: failed to auto-save pending Animator Controller edits.");

        // Save current scene on editor exit when possible.
        if (m_Scene && m_PlayModeState == EditorPlayModeState::Edit)
        {
            if (!m_CurrentSceneAssetKey.empty())
            {
                if (!SaveSceneToAssetKey(m_CurrentSceneAssetKey))
                    LT_WARN("Editor exit: failed to save current scene '{}'.", m_CurrentSceneAssetKey);
            }
            else if (m_EditorUndoService.IsDirty() || m_EditorUndoService.CanUndo())
            {
                LT_WARN("Editor exit: current scene has unsaved changes but has no asset key. Use Save Scene As before exit to persist it.");
            }
        }

        // Persist project settings values currently loaded in the Project Settings panel.
        {
            const auto& projectManager = Project::ProjectManager::GetInstance();
            if (projectManager.HasOpenProject() && m_ProjectSettingsPanelState.Loaded)
            {
                const std::filesystem::path projectRoot = projectManager.GetProjectRoot();
                const auto sr = Project::SaveRenderSettings(projectRoot, m_ProjectSettingsPanelState.Render);
                const auto sa = Project::SaveAudioSettings(projectRoot, m_ProjectSettingsPanelState.Audio);
                const auto si = Project::SaveInputSettings(projectRoot, m_ProjectSettingsPanelState.Input);
                const auto sl = Project::SaveLayersSettings(projectRoot, m_ProjectSettingsPanelState.Layers);
                const auto sp = Project::SavePhysics2DSettings(projectRoot, m_ProjectSettingsPanelState.Physics2D);
                const auto lighting = Project::SaveLighting2DSettings(projectRoot, m_ProjectSettingsPanelState.Lighting2D);

                if (sr.IsFailure()) LT_WARN("Editor exit: failed to save render settings: {}", sr.GetError().GetErrorMessage());
                if (sa.IsFailure()) LT_WARN("Editor exit: failed to save audio settings: {}", sa.GetError().GetErrorMessage());
                if (si.IsFailure()) LT_WARN("Editor exit: failed to save input settings: {}", si.GetError().GetErrorMessage());
                if (sl.IsFailure()) LT_WARN("Editor exit: failed to save layers settings: {}", sl.GetError().GetErrorMessage());
                if (sp.IsFailure()) LT_WARN("Editor exit: failed to save physics settings: {}", sp.GetError().GetErrorMessage());
                if (lighting.IsFailure()) LT_WARN("Editor exit: failed to save lighting settings: {}", lighting.GetError().GetErrorMessage());
            }
        }

        m_EditorUndoService.Clear();
        m_PendingTexturePrewarmTasks.clear();
        m_PendingMaterialPrewarmTasks.clear();
        m_PrewarmedTextureAssets.clear();
        m_PrewarmedMaterialAssets.clear();
        PersistProjectSessionState();
        Application::GetInstance().GetWindow().SetFileDropCallback({});

        EditorRuntimeOperations::Detach(
            m_Scene,
            m_EditSceneStored,
            m_EditorCameraController,
            m_SceneViewFramebuffer);
        DestroyGameViewPreviewCamera();
        m_GameViewFramebuffer.reset();
        ScriptCoreModuleRuntime::Shutdown();
    }

    void EditorLayer::OnUpdate(float deltaTime)
    {
        const ImGuiIO& io = ImGui::GetIO();
        ScriptCoreModuleRuntime::SetGameplayInputRoutingState(
            m_GameViewFocused,
            m_GameViewHovered,
            io.WantCaptureMouse,
            io.WantCaptureKeyboard || io.WantTextInput);
        ScriptCoreModuleRuntime::Update(m_PlayModeState);
        ApplyProjectRenderSettings();
        UpdateSceneAudioSources(m_Scene.get(), m_PlayModeState);
        ApplyProjectAudioSettings();

        if (m_ProjectSettingsPanelState.Loaded)
        {
            m_ProjectRenderSettings = m_ProjectSettingsPanelState.Render;
            m_ProjectRenderSettingsLoaded = true;
            ApplyProjectRenderSettings();

            m_ProjectAudioSettings = m_ProjectSettingsPanelState.Audio;
            m_ProjectAudioSettingsLoaded = true;
            ApplyProjectAudioSettings();

            m_ProjectPhysics2DSettings = m_ProjectSettingsPanelState.Physics2D;
            m_ProjectPhysics2DSettingsLoaded = true;
            ApplyProjectPhysics2DSettingsToScenes();

            m_ProjectLighting2DSettings = m_ProjectSettingsPanelState.Lighting2D;
            m_ProjectLighting2DSettingsLoaded = true;
            ApplyProjectLighting2DSettings();
        }

        if (m_PlayModeState == EditorPlayModeState::Play && m_Scene && m_Scene->IsReady())
            m_Scene->Update(deltaTime);

        // Tick particle emitters in edit mode so the inspector preview works.
        // Pass editModePreview=true to prevent PlayOnStart from auto-triggering.
        if (m_PlayModeState != EditorPlayModeState::Play && m_Scene && m_Scene->IsReady())
            UpdateParticleEmitterSystem(m_Scene->GetRegistry(), deltaTime, true);

        ProcessPendingSceneTransitions();
        if (m_StartupAssetImportPending && (!m_Scene || m_Scene->IsReady()))
        {
            LaunchStartupAssetImport();
            m_StartupAssetImportPending = false;
        }
        PumpStartupAssetImport();
        PumpSceneAssetPrewarm();
        UpdateSceneLoadingState();

        EditorPlayMode::SyncSceneCamera(
            m_PlayModeState,
            m_Scene.get(),
            m_CameraManager,
            m_CachedGameplayCameraId,
            m_CreatedGameplayCameraFromScene);

        EditorRuntimeOperations::Update(
            m_PlayModeState,
            m_SceneViewHovered,
            m_SceneViewRectValid,
            m_SceneViewRectMinPixels,
            m_SceneViewRectMaxPixels,
            m_GameViewRectValid,
            m_GameViewRectMinPixels,
            m_GameViewRectMaxPixels,
            io.WantTextInput,
            deltaTime,
            m_EditorCameraController.get());

        const bool saveModifierDown = io.KeyCtrl || io.KeySuper;
        if (saveModifierDown && !io.WantTextInput)
        {
            const bool undoPressed = ImGui::IsKeyPressed(ImGuiKey_Z, false);
            const bool redoPressed = ImGui::IsKeyPressed(ImGuiKey_Y, false) || (io.KeyShift && undoPressed);
            if (redoPressed)
            {
                (void)m_EditorUndoService.Redo();
            }
            else if (undoPressed && !io.KeyShift)
            {
                (void)m_EditorUndoService.Undo();
            }

            if (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false))
                SaveSceneAs();
            else if (!io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false))
                SaveScene();

            if (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_R, false))
                BuildProjectScripts();
        }
    }

    void EditorLayer::OnFixedUpdate(float fixedDeltaTime)
    {
        if (!m_Scene)
            return;

        if ((m_PlayModeState == EditorPlayModeState::Play || m_PlayModeState == EditorPlayModeState::Simulate) &&
            m_Scene->IsReady())
        {
            // Only collect expensive per-body diagnostics when the diagnostics
            // panel is actually visible, saving O(N*C) contact queries per step.
            const uint16_t worldCount = std::max<uint16_t>(1, m_Scene->GetPhysics2DWorldCount());
            for (uint16_t worldSlot = 0; worldSlot < worldCount; ++worldSlot)
            {
                Physics2DWorld* physicsWorld = m_Scene->GetPhysics2DWorld(worldSlot);
                if (physicsWorld)
                    physicsWorld->SetDiagnosticsEnabled(m_ShowPhysicsDiagnosticsWindow);
            }

            m_Scene->FixedUpdate(fixedDeltaTime);
            m_Scene->StepPhysics2D(fixedDeltaTime);
        }
    }

    void EditorLayer::OnRender()
    {
        const bool openedProjectThisFrame = EditorProjectDialog::Draw(m_ProjectDialogState);

        // Block the full editor until a project is explicitly selected.
        if (!Project::ProjectManager::GetInstance().HasOpenProject())
        {
            if (!m_ProjectDialogState.IsOpen)
            {
                EditorProjectDialog::RequestOpen(m_ProjectDialogState, EditorProjectDialog::ProjectDialogMode::Open);
            }
            return;
        }

        if (openedProjectThisFrame)
        {
            const auto& pm = Project::ProjectManager::GetInstance();
            const std::filesystem::path projectRoot = pm.GetProjectRoot();

            // Defer startup reimport to a background task once the initial scene is
            // visible, so project open remains responsive for tile-heavy projects.
            m_StartupAssetImportPending = true;
            m_StartupAssetImportInProgress = false;
            m_StartupAssetImportTask = Async::Task<std::string>();

            // Reset per-project panel cache so settings always match the newly opened project.
            m_ProjectSettingsPanelState.Loaded = false;
            m_ProjectSettingsPanelState.StatusMessage.clear();
            m_ProjectSettingsPanelState.StatusIsError = false;

            // Reset tile palette selection/caches so stale keys from the previous project never linger.
            m_TilePaletteState.ActivePaletteKey.clear();
            m_TilePaletteState.InvalidateCache();
            EditorTilePalettePanel::InvalidatePaletteKeyCache();

            // Apply project-level input settings at project open so runtime input defaults are active immediately.
            const auto inputSettingsResult = Project::LoadInputSettings(projectRoot);
            if (inputSettingsResult.IsSuccess())
            {
                const Project::InputSettings& inputSettings = inputSettingsResult.GetValue();
                if (inputSettings.ProjectInputActionsKey.empty())
                    InputSystem::GetInstance().SetProjectActionAsset(nullptr);
                else
                    InputSystem::GetInstance().SetProjectActionAssetFromKey(inputSettings.ProjectInputActionsKey);
                InputSystem::GetInstance().SetProjectAdditionalActionAssetsFromKeys(
                    Project::CollectAdditionalInputActionsAssetKeys(inputSettings));
            }
            else
            {
                LT_WARN("Failed to load project input settings: {}", inputSettingsResult.GetError().GetErrorMessage());
                InputSystem::GetInstance().SetProjectActionAsset(nullptr);
                InputSystem::GetInstance().SetProjectAdditionalActionAssetsFromKeys({});
            }

            RefreshProjectPhysics2DSettings();
            RefreshProjectRenderSettings();
            RefreshProjectAudioSettings();
            RefreshProjectLighting2DSettings();

            const auto isMissingSceneAssetKey = [](const std::string& sceneAssetKey) -> bool {
                if (sceneAssetKey.empty())
                    return true;
                const auto resolvedPathResult = Assets::ResolveAssetKeyToPath(sceneAssetKey);
                if (resolvedPathResult.IsFailure())
                    return true;
                std::error_code errorCode;
                return !std::filesystem::exists(resolvedPathResult.GetValue(), errorCode);
            };

            bool loadedScene = false;
            const auto initialProjectDefinition = pm.GetProjectDefinition();
            const bool defaultSceneWasMissing = !initialProjectDefinition.has_value() ||
                isMissingSceneAssetKey(initialProjectDefinition->DefaultScene.Key);
            const EditorSessionStateData sessionState = ReadProjectSessionState(projectRoot);
            EditorInspectorPanel::ApplyNativeScriptEditorSessionState(sessionState.NativeScriptEditorState);
            m_ShowProjectSettingsWindow = sessionState.ShowProjectSettingsWindow;
            m_ShowAssetDiagnosticsWindow = sessionState.ShowAssetDiagnosticsWindow;
            m_ShowPerformancePanel = sessionState.ShowPerformancePanel;
            m_ShowConsoleWindow = sessionState.ShowConsoleWindow;
            m_ProjectPanelState.AssetsRootExpanded = sessionState.ProjectAssetsRootExpanded;
            m_ProjectPanelState.ExpandedFolderState = sessionState.ProjectFolderExpansionState;
            const std::string lastOpenedSceneAssetKey = sessionState.LastOpenedSceneAssetKey;
            if (!lastOpenedSceneAssetKey.empty())
            {
                loadedScene = LoadSceneFromAssetKey(lastOpenedSceneAssetKey);
                if (!loadedScene && (m_RequestOpenSceneSwitchConfirmationPopup || m_SceneSwitchConfirmationPopupOpen))
                    loadedScene = true;
            }

            if (!loadedScene)
            {
                if (initialProjectDefinition.has_value() && !initialProjectDefinition->DefaultScene.Key.empty())
                {
                    loadedScene = LoadSceneFromAssetKey(initialProjectDefinition->DefaultScene.Key);
                    if (!loadedScene && (m_RequestOpenSceneSwitchConfirmationPopup || m_SceneSwitchConfirmationPopupOpen))
                        loadedScene = true;
                }
            }

            if (!loadedScene)
            {
                // Ensure a deterministic default scene exists for new projects.
                const std::string defaultSceneAssetKey = CreateSceneAssetInFolder("Scenes", kDefaultSceneFileName);
                if (!defaultSceneAssetKey.empty())
                {
                    loadedScene = LoadSceneFromAssetKey(defaultSceneAssetKey);
                    if (loadedScene)
                        SetProjectDefaultSceneAssetKey(defaultSceneAssetKey);
                }
            }

            // Self-heal stale default scene keys (for example after scene rename/delete).
            // If startup succeeded by loading a valid scene, persist that as the new
            // default so Asset Diagnostics no longer reports a missing default scene.
            if (defaultSceneWasMissing && loadedScene && !m_CurrentSceneAssetKey.empty())
                SetProjectDefaultSceneAssetKey(m_CurrentSceneAssetKey);
        }

        DrawMenuBar();
        if (m_StartupAssetImportPending || m_StartupAssetImportInProgress)
        {
            const char* phaseText = m_StartupAssetImportPending ? "Queued" : "Running";
            const int dotCount = 1 + static_cast<int>(std::fmod(ImGui::GetTime() * 2.0, 3.0));
            const std::string animatedDots(static_cast<size_t>(dotCount), '.');
            const std::string statusText = std::string("Background Asset Sync: ") + phaseText + animatedDots;

            ImGuiViewport* viewport = ImGui::GetMainViewport();
            if (viewport)
            {
                const ImVec2 windowPos(
                    viewport->Pos.x + viewport->Size.x - 12.0f,
                    viewport->Pos.y + 28.0f);
                ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always, ImVec2(1.0f, 0.0f));
            }
            ImGui::SetNextWindowBgAlpha(0.80f);
            constexpr ImGuiWindowFlags statusFlags =
                ImGuiWindowFlags_NoDecoration |
                ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoFocusOnAppearing |
                ImGuiWindowFlags_NoNav;
            if (ImGui::Begin("##BackgroundAssetSyncStatus", nullptr, statusFlags))
            {
                ImGui::TextUnformatted(statusText.c_str());
            }
            ImGui::End();
        }

        if (m_ScriptSafeModeActive &&
            (m_PlayModeState == EditorPlayModeState::Play ||
             m_PlayModeState == EditorPlayModeState::Simulate ||
             m_PlayModeState == EditorPlayModeState::Pause))
        {
            const std::string statusText = m_ScriptSafeModeMessage.empty()
                ? std::string("Scripts are disabled because native script build failed.")
                : m_ScriptSafeModeMessage;

            ImGuiViewport* viewport = ImGui::GetMainViewport();
            if (viewport)
            {
                const ImVec2 windowPos(
                    viewport->Pos.x + viewport->Size.x - 12.0f,
                    viewport->Pos.y + 58.0f);
                ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always, ImVec2(1.0f, 0.0f));
            }
            ImGui::SetNextWindowBgAlpha(0.90f);
            constexpr ImGuiWindowFlags safeModeFlags =
                ImGuiWindowFlags_NoDecoration |
                ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoFocusOnAppearing |
                ImGuiWindowFlags_NoNav;
            if (ImGui::Begin("##ScriptSafeModeStatus", nullptr, safeModeFlags))
            {
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Script Safe Mode Active");
                ImGui::Separator();
                ImGui::TextWrapped("%s", statusText.c_str());
            }
            ImGui::End();
        }

        EditorProjectSettingsPanel::Draw(m_ShowProjectSettingsWindow, m_ProjectSettingsPanelState);
        EditorBuildSettingsPanel::Draw(m_ShowBuildSettingsWindow, m_BuildSettingsPanelState,
                                       m_CurrentSceneAssetKey, m_Scene.get(),
                                       [this]() -> bool
                                       {
                                           if (!m_Scene || m_CurrentSceneAssetKey.empty())
                                               return true;
                                           return SaveSceneToAssetKey(m_CurrentSceneAssetKey);
                                       });
        EditorAssetDiagnosticsPanel::Draw(m_ShowAssetDiagnosticsWindow);
        DrawViewportPanel();
        DrawScenePanel();
        DrawInspectorPanel();
        DrawTilePalettePanelFrame();
        DrawSpriteEditorPanel();
        DrawProjectPanel();
        DrawAnimationTimelinePanel();
        DrawAnimatorGraphPanel();
        DrawPhysicsDiagnosticsPanel();
        DrawPerformancePanel();
        DrawConsolePanel();
        DrawSaveScenePopup();
        DrawSceneSwitchConfirmationPopup();

        if (m_ShowDemoWindow)
            ImGui::ShowDemoWindow(&m_ShowDemoWindow);
    }

    void EditorLayer::OnWindowResize(Events::WindowResizeEvent& event)
    {
        EditorRuntimeOperations::HandleWindowResize(
            event.GetWidth(),
            event.GetHeight(),
            m_SceneViewWidthPixels,
            m_SceneViewHeightPixels,
            m_SceneViewFramebuffer,
            m_EditorCameraController.get());
    }

    void EditorLayer::BuildProjectScripts()
    {
        std::string scriptBuildStatus;
        const bool buildStarted = EditorInspectorPanel::BuildProjectNativeScripts(&scriptBuildStatus);
        if (buildStarted)
            LT_INFO("Native scripts: {}", scriptBuildStatus.empty() ? "building..." : scriptBuildStatus);
        else
            LT_WARN("Native scripts: {}", scriptBuildStatus.empty() ? "build did not start." : scriptBuildStatus);
    }

    void EditorLayer::DrawMenuBar()
    {
        const bool isEditingPrefabAsset = IsPrefabAssetKey(m_CurrentSceneAssetKey);
        const bool canReturnFromPrefabMode = isEditingPrefabAsset && !m_PrefabModeReturnSceneAssetKey.empty();
        const bool canApplyPrefabToInstances = canReturnFromPrefabMode;

        EditorMenuBar::Draw(
            m_PlayModeState,
            m_ShowDemoWindow,
            m_ShowAssetDiagnosticsWindow,
            m_ShowPhysicsDiagnosticsWindow,
            m_ShowConsoleWindow,
            m_ShowEditorFpsOverlay,
            m_ShowPerformancePanel,
            m_ShowAnimationTimelinePanel,
            m_ShowAnimatorGraphPanel,
            m_TilePaletteState.PanelOpen,
            [this]() { EditorProjectDialog::RequestOpen(m_ProjectDialogState, EditorProjectDialog::ProjectDialogMode::Open); },
            [this]() { EditorProjectDialog::RequestOpen(m_ProjectDialogState, EditorProjectDialog::ProjectDialogMode::Create); },
            [this]() { m_ShowProjectSettingsWindow = true; },
            [this]() { m_ShowBuildSettingsWindow = true; },
            [this]() { BuildProjectScripts(); },
            [this]() {
                const auto result = Assets::AssetImportPipeline::ReimportChanged(true);
                if (result.IsFailure())
                {
                    LT_ERROR("Reimport Changed failed: {}", result.GetError().GetErrorMessage());
                    return;
                }
                const auto& s = result.GetValue();
                LT_INFO("Reimport Changed: discovered={} imported={} skipped={} missing={} errors={}",
                        s.DiscoveredFiles, s.Imported, s.SkippedUpToDate, s.MissingOnDisk, s.Errors);
            },
            [this]() {
                const auto result = Assets::AssetImportPipeline::ReimportAll(true);
                if (result.IsFailure())
                {
                    LT_ERROR("Reimport All failed: {}", result.GetError().GetErrorMessage());
                    return;
                }
                const auto& s = result.GetValue();
                LT_INFO("Reimport All: discovered={} imported={} skipped={} missing={} errors={}",
                        s.DiscoveredFiles, s.Imported, s.SkippedUpToDate, s.MissingOnDisk, s.Errors);
            },
            [this]() {
                const auto result = Assets::AssetImportPipeline::ValidateAssetDatabase();
                if (result.IsFailure())
                {
                    LT_ERROR("Validate Asset Database failed: {}", result.GetError().GetErrorMessage());
                    return;
                }

                const auto& issues = result.GetValue();
                if (issues.empty())
                {
                    LT_INFO("Asset Database validation: no issues found.");
                }
                else
                {
                    LT_WARN("Asset Database validation: {} issue(s) found.", issues.size());
                    for (const auto& issue : issues)
                    {
                        LT_WARN(" - {} (key='{}' guid='{}' path='{}')",
                                issue.Message, issue.Key, issue.Guid, issue.ResolvedPath);
                    }
                }
            },
            [this]() { NewScene(); },
            [this]() { SaveScene(); },
            [this]() { SaveSceneAs(); },
            [this]() { (void)m_EditorUndoService.Undo(); },
            [this]() { (void)m_EditorUndoService.Redo(); },
            m_EditorUndoService.CanUndo(),
            m_EditorUndoService.CanRedo(),
            m_EditorUndoService.GetUndoLabel(),
            m_EditorUndoService.GetRedoLabel(),
            [this]() { EnterPlayMode(); },
            [this]() { EnterSimulateMode(); },
            [this]() { ExitPlayMode(); },
            [this]() { TogglePausePlayMode(); },
            isEditingPrefabAsset,
            isEditingPrefabAsset
                ? SceneDisplayNameFromFileName(std::filesystem::path(m_CurrentSceneAssetKey).filename().string())
                : std::string{},
            canReturnFromPrefabMode,
            [this]() { (void)ReturnFromPrefabMode(false); },
            canApplyPrefabToInstances,
            [this]() { (void)ApplyPrefabStageChangesToInstances(); },
            [this]() { ResetLayoutToDefault(); });
    }

    void EditorLayer::ResetLayoutToDefault()
    {
        std::filesystem::path activeLayoutPath = "imgui.ini";
        if (const ImGuiIO& io = ImGui::GetIO(); io.IniFilename && io.IniFilename[0] != '\0')
            activeLayoutPath = io.IniFilename;

        std::filesystem::path defaultLayoutPath = activeLayoutPath.parent_path() / "imgui-default.ini";
        std::error_code errorCode;
        if (!std::filesystem::exists(defaultLayoutPath, errorCode))
        {
            // Dev fallback when the default layout wasn't copied beside the output binary.
            std::filesystem::path probe = activeLayoutPath.parent_path();
            for (int depth = 0; depth < 8 && !probe.empty(); ++depth)
            {
                defaultLayoutPath = probe / "Editor" / "imgui-default.ini";
                errorCode.clear();
                if (std::filesystem::exists(defaultLayoutPath, errorCode))
                    break;

                const std::filesystem::path parent = probe.parent_path();
                if (parent == probe)
                    break;
                probe = parent;
            }
        }

        if (!std::filesystem::exists(defaultLayoutPath, errorCode))
        {
            LT_WARN("Reset Layout failed: '{}' was not found.", defaultLayoutPath.string());
            return;
        }

        std::filesystem::copy_file(
            defaultLayoutPath,
            activeLayoutPath,
            std::filesystem::copy_options::overwrite_existing,
            errorCode);
        if (errorCode)
        {
            LT_WARN("Reset Layout failed while writing '{}': {}", activeLayoutPath.string(), errorCode.message());
            return;
        }

        ImGui::LoadIniSettingsFromDisk(activeLayoutPath.string().c_str());
        LT_INFO("Editor layout reset to default.");
    }

    void EditorLayer::DrawPhysicsDiagnosticsPanel()
    {
        if (!m_ShowPhysicsDiagnosticsWindow)
            return;

        if (!ImGui::Begin("Physics 2D Diagnostics", &m_ShowPhysicsDiagnosticsWindow))
        {
            ImGui::End();
            return;
        }

        if (!m_Scene)
        {
            ImGui::TextDisabled("No active scene.");
            ImGui::End();
            return;
        }

        {
            Physics2DWorldSettings scenePhysicsSettings = m_Scene->GetPhysics2DSettings();
            int worldCount = std::clamp<int>(scenePhysicsSettings.WorldCount, 1, 16);
            ImGui::TextDisabled("Scene Physics");
            if (ImGui::SliderInt("World Count", &worldCount, 1, 16))
            {
                scenePhysicsSettings.WorldCount = static_cast<uint16_t>(worldCount);
                m_Scene->SetPhysics2DSettings(scenePhysicsSettings);
                if (m_EditSceneStored && m_EditSceneStored.get() != m_Scene.get())
                {
                    Physics2DWorldSettings editSceneSettings = m_EditSceneStored->GetPhysics2DSettings();
                    editSceneSettings.WorldCount = scenePhysicsSettings.WorldCount;
                    m_EditSceneStored->SetPhysics2DSettings(editSceneSettings);
                }
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Allocates independent Box2D worlds for this scene. Rigidbody2D Physics World Slot selects the world per body.");
            ImGui::Separator();
        }

        const uint16_t worldCount = std::max<uint16_t>(1, m_Scene->GetPhysics2DWorldCount());
        Physics2DDiagnostics diagnostics{};
        bool hasAnyWorld = false;
        for (uint16_t worldSlot = 0; worldSlot < worldCount; ++worldSlot)
        {
            const Physics2DWorld* physicsWorld = m_Scene->GetPhysics2DWorld(worldSlot);
            if (!physicsWorld)
                continue;
            hasAnyWorld = true;
            const Physics2DDiagnostics& worldDiagnostics = physicsWorld->GetDiagnostics();
            diagnostics.BodyCount += worldDiagnostics.BodyCount;
            diagnostics.AwakeBodyCount += worldDiagnostics.AwakeBodyCount;
            diagnostics.SleepingBodyCount += worldDiagnostics.SleepingBodyCount;
            diagnostics.ContactPairCount += worldDiagnostics.ContactPairCount;
            diagnostics.PenetratingContactPointCount += worldDiagnostics.PenetratingContactPointCount;
            diagnostics.MaxPenetrationDepth = std::max(diagnostics.MaxPenetrationDepth, worldDiagnostics.MaxPenetrationDepth);
        }
        if (hasAnyWorld)
        {
            constexpr float kRecentPeakHoldDurationSeconds = 0.35f;
            const float frameDeltaSeconds = std::max(0.0f, ImGui::GetIO().DeltaTime);

            if (diagnostics.ContactPairCount > 0 || diagnostics.PenetratingContactPointCount > 0 || diagnostics.MaxPenetrationDepth > 0.0f)
            {
                m_PhysicsDiagnosticsRecentPeakContactPairs =
                    std::max(m_PhysicsDiagnosticsRecentPeakContactPairs, diagnostics.ContactPairCount);
                m_PhysicsDiagnosticsRecentPeakPenetratingPoints =
                    std::max(m_PhysicsDiagnosticsRecentPeakPenetratingPoints, diagnostics.PenetratingContactPointCount);
                m_PhysicsDiagnosticsRecentPeakMaxPenetrationDepth =
                    std::max(m_PhysicsDiagnosticsRecentPeakMaxPenetrationDepth, diagnostics.MaxPenetrationDepth);
                m_PhysicsDiagnosticsRecentPeakHoldSeconds = kRecentPeakHoldDurationSeconds;
            }
            else if (m_PhysicsDiagnosticsRecentPeakHoldSeconds > 0.0f)
            {
                m_PhysicsDiagnosticsRecentPeakHoldSeconds =
                    std::max(0.0f, m_PhysicsDiagnosticsRecentPeakHoldSeconds - frameDeltaSeconds);
                if (m_PhysicsDiagnosticsRecentPeakHoldSeconds <= 0.0f)
                {
                    m_PhysicsDiagnosticsRecentPeakContactPairs = diagnostics.ContactPairCount;
                    m_PhysicsDiagnosticsRecentPeakPenetratingPoints = diagnostics.PenetratingContactPointCount;
                    m_PhysicsDiagnosticsRecentPeakMaxPenetrationDepth = diagnostics.MaxPenetrationDepth;
                }
            }

            ImGui::Text("Bodies: %d (Awake: %d, Sleeping: %d)", diagnostics.BodyCount, diagnostics.AwakeBodyCount, diagnostics.SleepingBodyCount);
            ImGui::Text("Contact Pairs: %d", diagnostics.ContactPairCount);
            ImGui::Text("Penetrating Points: %d", diagnostics.PenetratingContactPointCount);
            ImGui::Text("Max Penetration Depth: %.5f", diagnostics.MaxPenetrationDepth);
            ImGui::TextDisabled("Recent Peak (%.2fs): contacts=%d, penetrating=%d, maxDepth=%.5f",
                                kRecentPeakHoldDurationSeconds,
                                m_PhysicsDiagnosticsRecentPeakContactPairs,
                                m_PhysicsDiagnosticsRecentPeakPenetratingPoints,
                                m_PhysicsDiagnosticsRecentPeakMaxPenetrationDepth);
            ImGui::Separator();

            if (m_SelectedEntity != entt::null)
            {
                Physics2DBodyDiagnostics bodyDiagnostics{};
                if (m_Scene->TryGetPhysics2DBodyDiagnostics(m_SelectedEntity, bodyDiagnostics))
                {
                    ImGui::TextDisabled("Selected Body");
                    ImGui::Text("Awake: %s", bodyDiagnostics.IsAwake ? "Yes" : "No");
                    ImGui::Text("Contact Pairs: %d", bodyDiagnostics.ContactPairCount);
                    ImGui::Text("Penetrating Points: %d", bodyDiagnostics.PenetratingContactPointCount);
                    ImGui::Text("Max Penetration Depth: %.5f", bodyDiagnostics.MaxPenetrationDepth);
                }
                else
                {
                    ImGui::TextDisabled("Selected entity has no active Rigidbody2D diagnostics.");
                }
            }
            else
            {
                ImGui::TextDisabled("Select an entity to inspect per-body sleep/contact state.");
            }

            ImGui::TextWrapped("Tip: if contacts and penetration are stable but motion appears jittery, it is usually render sampling rather than solver instability.");
            ImGui::Separator();
        }
        else
        {
            ImGui::TextDisabled("Physics world is not initialized yet.");
            ImGui::Separator();
        }

        const Lighting2DDiagnostics& lightingDiagnostics = Lighting2DRenderer::GetDiagnostics();
        ImGui::TextDisabled("Lighting 2D");
        ImGui::Text("Path Active: %s", lightingDiagnostics.UsingLightingPath ? "Yes" : "No");
        ImGui::Text("Directional Lights: %u", lightingDiagnostics.DirectionalLightsRendered);
        ImGui::Text("Point Lights: %u", lightingDiagnostics.PointLightsRendered);
        ImGui::Text("Shadow Occluders: %u", lightingDiagnostics.ShadowOccluderCount);
        ImGui::Text("Shadow Segments: %u", lightingDiagnostics.ShadowSegmentCount);
        ImGui::Text("CPU Build: %.3f ms | Submit: %.3f ms",
                    lightingDiagnostics.CpuBuildTimeMs,
                    lightingDiagnostics.CpuSubmitTimeMs);
        ImGui::End();
    }

    void EditorLayer::DrawPerformancePanel()
    {
        if (!m_ShowPerformancePanel)
            return;

        if (!ImGui::Begin("Performance", &m_ShowPerformancePanel))
        {
            ImGui::End();
            return;
        }

        auto& monitor = PerformanceMonitor::GetInstance();
        if (!monitor.IsInitialized())
        {
            ImGui::TextDisabled("Performance monitor not initialized.");
            ImGui::End();
            return;
        }

        const PerformanceMetrics metrics = monitor.CollectMetrics();

        ImGui::TextDisabled("Frame");
        ImGui::Text("Frame time: %.2f ms (avg %.2f ms)", metrics.frameTime, metrics.frameTimeAvg);
        ImGui::Text("FPS: %.1f (avg %.1f)", metrics.fps, metrics.fpsAvg);
        ImGui::Text("Frame count: %u", metrics.frameCount);
        ImGui::Separator();

        ImGui::TextDisabled("CPU");
        ImGui::Text("Usage: %.1f%% (avg %.1f%%)", metrics.cpuUsage, metrics.cpuUsageAvg);
        ImGui::Text("Cores: %u", metrics.cpuCoreCount);
        ImGui::Separator();

        ImGui::TextDisabled("GPU");
        ImGui::Text("Memory: %.1f%%", metrics.gpuMemoryUsage);
        if (metrics.gpuMemoryTotalBytes > 0)
        {
            const double usedMB = static_cast<double>(metrics.gpuMemoryUsedBytes) / (1024.0 * 1024.0);
            const double totalMB = static_cast<double>(metrics.gpuMemoryTotalBytes) / (1024.0 * 1024.0);
            ImGui::Text("VRAM: %.1f MB / %.1f MB", usedMB, totalMB);
        }
        else
        {
            ImGui::TextDisabled("VRAM: (OpenGL driver did not report)");
        }
        if (metrics.gpuUsage > 0.0)
            ImGui::Text("Usage: %.1f%%", metrics.gpuUsage);
        if (metrics.gpuTemperature > 0.0)
            ImGui::Text("Temperature: %.0f C", metrics.gpuTemperature);
        ImGui::Separator();

        ImGui::TextDisabled("Process memory");
        const double currentMB = static_cast<double>(metrics.currentMemory) / (1024.0 * 1024.0);
        const double peakMB = static_cast<double>(metrics.peakMemory) / (1024.0 * 1024.0);
        ImGui::Text("Current: %.1f MB", currentMB);
        ImGui::Text("Peak: %.1f MB", peakMB);
        ImGui::Text("Allocations: %u", metrics.allocationCount);

        ImGui::End();
    }

    void EditorLayer::DrawConsolePanel()
    {
        if (!m_ShowConsoleWindow)
            return;

        if (!ImGui::Begin("Console", &m_ShowConsoleWindow))
        {
            ImGui::End();
            return;
        }

        const std::vector<LogMessageEntry> messages = Log::GetRecentMessages();
        int scriptInfoCount = 0;
        int scriptWarningCount = 0;
        int scriptErrorCount = 0;
        for (const LogMessageEntry& entry : messages)
        {
            if (!entry.Message.starts_with("[Script]"))
                continue;

            if (entry.Level >= spdlog::level::err)
                ++scriptErrorCount;
            else if (entry.Level == spdlog::level::warn)
                ++scriptWarningCount;
            else
                ++scriptInfoCount;
        }

        if (ImGui::Button("Clear"))
        {
            Log::ClearRecentMessages();
            m_ConsoleSelectedEntryText.clear();
            m_ConsoleSelectedMessageText.clear();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &m_ConsoleAutoScroll);
        ImGui::SameLine();
        ImGui::Checkbox("Scripts", &m_ConsoleShowScriptLogs);
        ImGui::SameLine();
        ImGui::Checkbox("Engine", &m_ConsoleShowEngineLogs);
        ImGui::SameLine();
        ImGui::Checkbox("Info", &m_ConsoleShowInfo);
        ImGui::SameLine();
        ImGui::Checkbox("Warnings", &m_ConsoleShowWarnings);
        ImGui::SameLine();
        ImGui::Checkbox("Errors", &m_ConsoleShowErrors);
        ImGui::SameLine();
        ImGui::TextDisabled("Script Severity I:%d W:%d E:%d", scriptInfoCount, scriptWarningCount, scriptErrorCount);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(260.0f);
        ImGui::InputTextWithHint("##ConsoleSearch", "Search logs...", m_ConsoleSearchBuffer.data(), m_ConsoleSearchBuffer.size());
        ImGui::SameLine();
        bool copyVisibleRequested = ImGui::Button("Copy Visible");
        {
            static constexpr int kLogLevelCount = 7;
            static constexpr const char* kLogLevelLabels[] = { "Trace", "Debug", "Info", "Warn", "Error", "Critical", "Off" };
            static constexpr spdlog::level::level_enum kLogLevelValues[] = {
                spdlog::level::trace,
                spdlog::level::debug,
                spdlog::level::info,
                spdlog::level::warn,
                spdlog::level::err,
                spdlog::level::critical,
                spdlog::level::off
            };

            auto levelToIndex = [](spdlog::level::level_enum level) -> int
            {
                for (int index = 0; index < kLogLevelCount; ++index)
                {
                    if (kLogLevelValues[index] == level)
                        return index;
                }
                return 2; // Info
            };

            int coreLevelIndex = levelToIndex(Log::GetCoreLogLevel());
            int appLevelIndex = levelToIndex(Log::GetClientLogLevel());
            const bool hasCoreLogger = static_cast<bool>(Log::GetCoreLogger());

            ImGui::SetNextItemWidth(120.0f);
            ImGui::BeginDisabled(!hasCoreLogger);
            if (ImGui::Combo("Core Level", &coreLevelIndex, kLogLevelLabels, kLogLevelCount))
                Log::SetCoreLogLevel(kLogLevelValues[coreLevelIndex], true);
            ImGui::EndDisabled();
            if (!hasCoreLogger && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Core logger is disabled in this build configuration.");

            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::Combo("App Level", &appLevelIndex, kLogLevelLabels, kLogLevelCount))
                Log::SetClientLogLevel(kLogLevelValues[appLevelIndex], true);
        }

        const std::string searchText = m_ConsoleSearchBuffer.data();
        const auto shouldDisplayEntry = [&](const LogMessageEntry& entry) -> bool
        {
            const bool isScriptLog = entry.Message.starts_with("[Script]");
            const bool isWarning = entry.Level == spdlog::level::warn;
            const bool isError = entry.Level >= spdlog::level::err;
            const bool isInfo = !isWarning && !isError;

            if ((isScriptLog && !m_ConsoleShowScriptLogs) || (!isScriptLog && !m_ConsoleShowEngineLogs))
                return false;
            if ((isInfo && !m_ConsoleShowInfo) || (isWarning && !m_ConsoleShowWarnings) || (isError && !m_ConsoleShowErrors))
                return false;

            if (!searchText.empty())
            {
                const bool loggerMatch = entry.LoggerName.find(searchText) != std::string::npos;
                const bool messageMatch = entry.Message.find(searchText) != std::string::npos;
                if (!loggerMatch && !messageMatch)
                    return false;
            }

            return true;
        };

        std::vector<const LogMessageEntry*> visibleEntries;
        visibleEntries.reserve(messages.size());
        for (const LogMessageEntry& entry : messages)
        {
            if (shouldDisplayEntry(entry))
                visibleEntries.push_back(&entry);
        }

        if (copyVisibleRequested)
        {
            auto levelToLabel = [](spdlog::level::level_enum level) -> const char*
            {
                if (level >= spdlog::level::critical) return "Critical";
                if (level >= spdlog::level::err) return "Error";
                if (level >= spdlog::level::warn) return "Warning";
                if (level >= spdlog::level::info) return "Info";
                if (level >= spdlog::level::debug) return "Debug";
                return "Trace";
            };

            std::string clipboardText;
            for (const LogMessageEntry* entry : visibleEntries)
            {
                if (!entry)
                    continue;
                clipboardText += "[" + entry->LoggerName + "] ";
                clipboardText += "[" + std::string(levelToLabel(entry->Level)) + "] ";
                clipboardText += entry->Message;
                clipboardText.push_back('\n');
            }
            ImGui::SetClipboardText(clipboardText.c_str());
        }
        ImGui::Separator();

        if (ImGui::BeginChild("ConsoleEntries"))
        {
            const bool shouldScrollToBottom = m_ConsoleAutoScroll && (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 8.0f);
            for (size_t entryIndex = 0; entryIndex < visibleEntries.size(); ++entryIndex)
            {
                const LogMessageEntry* entry = visibleEntries[entryIndex];
                if (!entry)
                    continue;

                const bool isWarning = entry->Level == spdlog::level::warn;
                const bool isError = entry->Level >= spdlog::level::err;
                const std::string lineText = "[" + entry->LoggerName + "] " + entry->Message;
                const bool isSelected = (lineText == m_ConsoleSelectedEntryText);

                const ImVec4 textColor = isError
                    ? ImVec4(1.0f, 0.38f, 0.38f, 1.0f)
                    : (isWarning ? ImVec4(1.0f, 0.85f, 0.35f, 1.0f) : ImGui::GetStyleColorVec4(ImGuiCol_Text));

                ImGui::PushStyleColor(ImGuiCol_Text, textColor);
                const std::string selectableLabel = lineText + "##ConsoleEntry_" + std::to_string(entryIndex);
                if (ImGui::Selectable(selectableLabel.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick))
                {
                    m_ConsoleSelectedEntryText = lineText;
                    m_ConsoleSelectedMessageText = entry->Message;
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        ImGui::SetClipboardText(entry->Message.c_str());
                }
                if (ImGui::BeginPopupContextItem())
                {
                    if (ImGui::MenuItem("Copy"))
                        ImGui::SetClipboardText(lineText.c_str());
                    if (ImGui::MenuItem("Copy Message"))
                        ImGui::SetClipboardText(entry->Message.c_str());
                    ImGui::EndPopup();
                }
                ImGui::PopStyleColor();
            }

            const ImGuiIO& io = ImGui::GetIO();
            if (!ImGui::IsAnyItemActive() &&
                !m_ConsoleSelectedMessageText.empty() &&
                (io.KeyCtrl || io.KeySuper) &&
                ImGui::IsKeyPressed(ImGuiKey_C, false))
            {
                ImGui::SetClipboardText(m_ConsoleSelectedMessageText.c_str());
            }

            if (shouldScrollToBottom)
                ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();

        ImGui::End();
    }

    void EditorLayer::DrawViewportPanel()
    {
        Camera* sceneViewCamera = m_CameraManager.GetCamera(m_EditorCameraId);
        bool missingGameplayCamera = false;
        Camera* gameViewCamera = ResolveGameViewCamera(m_GameViewWidthPixels, m_GameViewHeightPixels, missingGameplayCamera);

        EditorViewportPanel::Draw(
            m_SceneViewWidthPixels,
            m_SceneViewHeightPixels,
            m_SceneViewFramebuffer,
            m_SceneViewFocused,
            m_SceneViewHovered,
            m_SceneViewRectValid,
            m_SceneViewRectMinPixels,
            m_SceneViewRectMaxPixels,
            m_GameViewWidthPixels,
            m_GameViewHeightPixels,
            m_GameViewFramebuffer,
            m_GameViewFocused,
            m_GameViewHovered,
            m_GameViewRectValid,
            m_GameViewRectMinPixels,
            m_GameViewRectMaxPixels,
            m_FocusSceneViewOnPlayExit,
            m_FocusGameViewOnPlayEnter,
            m_EditorCameraController.get(),
            sceneViewCamera,
            gameViewCamera,
            m_Scene.get(),
            m_PlayModeState,
            [this](uint32_t width, uint32_t height) { EnsureSceneViewFramebuffer(width, height); },
            [this](uint32_t width, uint32_t height) { EnsureGameViewFramebuffer(width, height); },
            kAssetScenePayload,
            [this](const std::string& assetKey) { LoadSceneFromAssetKey(assetKey); },
            kAssetPrefabPayload,
            [this](const std::string& prefabAssetKey, const glm::vec3& worldPosition) {
                (void)InstantiatePrefabAtWorldPosition(prefabAssetKey, worldPosition);
            },
            m_SelectedEntity,
            &m_EditorUndoService,
            kAssetMaterialPayload,
            m_SelectedTextureAssetKey,
            m_CachedTextureAsset,
            m_SelectedMaterialAssetKey,
            m_CachedMaterialAsset,
            m_SelectedNativeScriptAssetKey,
            m_ShowEditorFpsOverlay,
            &m_TilemapEditorState,
            missingGameplayCamera);
    }

    void EditorLayer::DrawScenePanel()
    {
        std::string sceneRootDisplayName = SceneDisplayNameFromFileName(kDefaultSceneFileName);
        if (!m_CurrentSceneAssetKey.empty())
        {
            const std::filesystem::path sceneAssetPath(m_CurrentSceneAssetKey);
            sceneRootDisplayName = SceneDisplayNameFromFileName(sceneAssetPath.filename().string());
            if (IsPrefabAssetKey(m_CurrentSceneAssetKey))
                sceneRootDisplayName = "Prefab: " + sceneRootDisplayName;
        }

        EditorScenePanel::Draw(
            m_Scene.get(),
            m_ScenePanelState,
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
            kAssetMaterialPayload,
            kAssetPrefabPayload,
            sceneRootDisplayName,
            &m_EditorUndoService,
            [this](const std::string& prefabAssetKey, entt::entity parentEntity) { return InstantiatePrefabAtParent(prefabAssetKey, parentEntity); },
            [this](entt::entity entity) { return CreatePrefabFromEntity(entity); },
            [this](entt::entity entity) { return ApplyPrefabFromEntity(entity); },
            [this](entt::entity entity) { return RevertPrefabEntity(entity); },
            [this](entt::entity entity) { return UnpackPrefabEntity(entity); });
    }

    void EditorLayer::DrawInspectorPanel()
    {
        EditorInspectorPanel::Draw(
            m_Scene.get(),
            m_SelectedEntity,
            kAssetTexturePayload,
            m_SelectedTextureAssetKey,
            m_CachedTextureAsset,
            kAssetAudioPayload,
            kAssetMaterialPayload,
            kAssetShaderPayload,
            kAssetFontPayload,
            m_SelectedMaterialAssetKey,
            m_CachedMaterialAsset,
            m_SelectedNativeScriptAssetKey,
            m_SelectedPrefabAssetKey,
            m_SelectedTilesetAssetKey,
            m_SelectedAudioMixerAssetKey,
            m_SelectedInputActionsAssetKey,
            m_SelectedAnimationClipAssetKey,
            m_SelectedAnimatorControllerAssetKey,
            &m_EditorUndoService);
    }

    void EditorLayer::DrawProjectPanel()
    {
        EditorProjectPanel::Draw(
            m_ProjectPanelState,
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

                // Keep project/session references coherent when a scene or prefab
                // asset currently in use gets renamed from the Project panel.
                if (auto& projectManager = Project::ProjectManager::GetInstance(); projectManager.HasOpenProject())
                {
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
                    // Even without an active scene instance, keep selected material state consistent.
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
                    auto scriptView = registry.view<NativeScriptComponent>();
                    for (entt::entity entity : scriptView)
                    {
                        auto& nativeScript = scriptView.get<NativeScriptComponent>(entity);
                        for (auto& scriptEntry : nativeScript.Scripts)
                        {
                            const bool matchedByStoredPath = (scriptEntry.ScriptAssetRelativePath == oldScriptRelativePath);
                            const bool matchedByLegacyClassOnly =
                                scriptEntry.ScriptAssetRelativePath.empty() &&
                                !oldScriptClassName.empty() &&
                                (scriptEntry.ScriptClassName == oldScriptClassName);
                            if (matchedByStoredPath || matchedByLegacyClassOnly)
                            {
                                scriptEntry.ScriptAssetRelativePath = newScriptRelativePath;
                                if (!newScriptClassName.empty())
                                    scriptEntry.ScriptClassName = newScriptClassName;
                                scriptEntry.RuntimeInitialized = false;
                                scriptEntry.RuntimeInstance.reset();
                                updatedAnyNativeScriptPath = true;
                            }
                        }
                    }
                };

                // Update both scene instances so rename remains stable across Play/Edit transitions.
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

                // Persist immediately so reopening the editor cannot revive stale material keys.
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
            [this](const std::string& scriptAssetKey) {
                (void)EditorInspectorPanel::OpenNativeScriptEditorForAssetKey(scriptAssetKey);
            });

        if (m_ProjectPanelState.TreeExpansionStateChanged)
            PersistProjectSessionState();
    }

    void EditorLayer::DrawAnimationTimelinePanel()
    {
        EditorAnimationTimelinePanel::Draw(m_ShowAnimationTimelinePanel, m_SelectedAnimationClipAssetKey, &m_EditorUndoService);
    }

    void EditorLayer::DrawAnimatorGraphPanel()
    {
        EditorAnimatorGraphPanel::Draw(m_ShowAnimatorGraphPanel, m_SelectedAnimatorControllerAssetKey, &m_EditorUndoService);
    }

    void EditorLayer::DrawSpriteEditorPanel()
    {
        // Poll the inspector for a pending "Open Sprite Editor" request.
        const std::string& pendingKey = EditorInspectorPanel::GetPendingSpriteEditorRequest();
        if (!pendingKey.empty())
        {
            EditorSpriteEditor::Open(m_SpriteEditorState, pendingKey);
            EditorInspectorPanel::ClearPendingSpriteEditorRequest();
        }

        EditorSpriteEditor::Draw(m_SpriteEditorState);
    }

    void EditorLayer::DrawTilePalettePanelFrame()
    {
        EditorTilePalettePanel::DrawTilePalettePanel(
            m_TilePaletteState,
            m_Scene.get(),
            m_SelectedEntity,
            m_TilemapEditorState,
            &m_EditorUndoService);
    }

    void EditorLayer::EnsureSceneViewFramebuffer(uint32_t width, uint32_t height)
    {
        EditorRuntimeOperations::EnsureViewportFramebuffer(
            width,
            height,
            m_SceneViewFramebuffer,
            m_SceneViewWidthPixels,
            m_SceneViewHeightPixels,
            m_EditorCameraController.get());
    }

    void EditorLayer::EnsureGameViewFramebuffer(uint32_t width, uint32_t height)
    {
        EditorRuntimeOperations::EnsureViewportFramebuffer(
            width,
            height,
            m_GameViewFramebuffer,
            m_GameViewWidthPixels,
            m_GameViewHeightPixels,
            nullptr);
    }

    Camera* EditorLayer::ResolveGameViewCamera(uint32_t viewportWidthPixels,
                                               uint32_t viewportHeightPixels,
                                               bool& outMissingGameplayCamera)
    {
        outMissingGameplayCamera = false;

        const auto findFirstGameplayCamera = [this]() -> Camera* {
            for (CameraId id : m_CameraManager.GetAllCameraIds())
            {
                Camera* camera = m_CameraManager.GetCamera(id);
                if (camera && camera->GetUsage() == CameraUsage::Gameplay)
                    return camera;
            }
            return nullptr;
        };

        if (m_PlayModeState != EditorPlayModeState::Edit)
        {
            if (Camera* camera = m_CameraManager.GetCamera(m_CachedGameplayCameraId))
            {
                camera->SetViewportSize(viewportWidthPixels, viewportHeightPixels);
                return camera;
            }

            if (Camera* fallbackCamera = findFirstGameplayCamera())
            {
                fallbackCamera->SetViewportSize(viewportWidthPixels, viewportHeightPixels);
                return fallbackCamera;
            }

            outMissingGameplayCamera = true;
            return nullptr;
        }

        if (!m_Scene)
        {
            outMissingGameplayCamera = true;
            return nullptr;
        }

        struct GameplaySceneCameraSelection final
        {
            entt::entity Entity = entt::null;
            CameraComponent Component{};
        };

        const auto findGameplaySceneCamera = [this]() -> std::optional<GameplaySceneCameraSelection> {
            auto& registry = m_Scene->GetRegistry();
            auto view = registry.view<CameraComponent>();
            std::optional<GameplaySceneCameraSelection> fallback{};
            for (entt::entity entity : view)
            {
                const auto& cameraComponent = view.get<CameraComponent>(entity);
                if (!fallback.has_value())
                    fallback = GameplaySceneCameraSelection{ entity, cameraComponent };
                if (cameraComponent.IsPrimary)
                    return GameplaySceneCameraSelection{ entity, cameraComponent };
            }

            return fallback;
        };

        // Edit Mode Game View mirrors Unity/Godot behavior:
        // render from the scene's Primary camera even when runtime is not active.
        const std::optional<GameplaySceneCameraSelection> selection = findGameplaySceneCamera();
        if (!selection.has_value())
        {
            DestroyGameViewPreviewCamera();
            outMissingGameplayCamera = true;
            return nullptr;
        }

        const CameraType expectedType = (selection->Component.Projection == CameraComponent::ProjectionType::Perspective3D)
            ? CameraType::Perspective3D
            : CameraType::Orthographic2D;

        Camera* previewCamera = m_CameraManager.GetCamera(m_GameViewPreviewCameraId);
        if (!previewCamera || previewCamera->GetType() != expectedType || previewCamera->GetUsage() != CameraUsage::Gameplay)
        {
            DestroyGameViewPreviewCamera();

            if (expectedType == CameraType::Perspective3D)
            {
                CameraManager::Perspective3DCreateInfo createInfo{};
                createInfo.Name = "GameView Preview Camera";
                createInfo.Usage = CameraUsage::Gameplay;
                createInfo.ViewportWidthPixels = viewportWidthPixels;
                createInfo.ViewportHeightPixels = viewportHeightPixels;
                createInfo.FieldOfViewYDegrees = selection->Component.FieldOfViewYDegrees;
                if (selection->Component.NearPlane <= 0.0f && selection->Component.FarPlane <= 1.0f)
                {
                    createInfo.NearPlane = 0.1f;
                    createInfo.FarPlane = 1000.0f;
                }
                else
                {
                    createInfo.NearPlane = selection->Component.NearPlane > 0.0f ? selection->Component.NearPlane : 0.01f;
                    createInfo.FarPlane = selection->Component.FarPlane > createInfo.NearPlane
                        ? selection->Component.FarPlane
                        : (createInfo.NearPlane + 1000.0f);
                }
                m_GameViewPreviewCameraId = m_CameraManager.CreatePerspective3D(createInfo);
            }
            else
            {
                CameraManager::Orthographic2DCreateInfo createInfo{};
                createInfo.Name = "GameView Preview Camera";
                createInfo.Usage = CameraUsage::Gameplay;
                createInfo.ViewportWidthPixels = viewportWidthPixels;
                createInfo.ViewportHeightPixels = viewportHeightPixels;
                createInfo.Zoom = selection->Component.Zoom > 0.0f ? selection->Component.Zoom : 1.0f;
                createInfo.NearPlane = selection->Component.NearPlane;
                createInfo.FarPlane = selection->Component.FarPlane > selection->Component.NearPlane
                    ? selection->Component.FarPlane
                    : (selection->Component.NearPlane + 2.0f);
                m_GameViewPreviewCameraId = m_CameraManager.CreateOrthographic2D(createInfo);
            }

            previewCamera = m_CameraManager.GetCamera(m_GameViewPreviewCameraId);
        }

        if (!previewCamera)
        {
            outMissingGameplayCamera = true;
            return nullptr;
        }

        previewCamera->SetViewportSize(viewportWidthPixels, viewportHeightPixels);
        if (selection->Component.Projection == CameraComponent::ProjectionType::Orthographic2D)
        {
            auto* orthographicCamera = m_CameraManager.GetOrthographic2D(m_GameViewPreviewCameraId);
            if (!orthographicCamera)
            {
                outMissingGameplayCamera = true;
                return nullptr;
            }

            const float zoom = selection->Component.Zoom > 0.0f ? selection->Component.Zoom : 1.0f;
            const float nearPlane = selection->Component.NearPlane;
            const float farPlane = selection->Component.FarPlane > nearPlane
                ? selection->Component.FarPlane
                : (nearPlane + 2.0f);
            orthographicCamera->SetProjection(zoom, nearPlane, farPlane);
        }
        else
        {
            auto* perspectiveCamera = m_CameraManager.GetPerspective3D(m_GameViewPreviewCameraId);
            if (!perspectiveCamera)
            {
                outMissingGameplayCamera = true;
                return nullptr;
            }

            const float fieldOfViewY = selection->Component.FieldOfViewYDegrees > 1.0f ? selection->Component.FieldOfViewYDegrees : 60.0f;
            float nearPlane = selection->Component.NearPlane > 0.0f ? selection->Component.NearPlane : 0.01f;
            float farPlane = selection->Component.FarPlane > nearPlane ? selection->Component.FarPlane : nearPlane + 1000.0f;
            if (selection->Component.NearPlane <= 0.0f && selection->Component.FarPlane <= 1.0f)
            {
                nearPlane = 0.1f;
                farPlane = 1000.0f;
            }
            perspectiveCamera->SetPerspective(fieldOfViewY, nearPlane, farPlane);
        }

        const glm::mat4 worldTransform = m_Scene->GetWorldTransformMatrix(selection->Entity);
        const glm::vec3 position = glm::vec3(worldTransform[3]);
        if (auto* orthographicCamera = m_CameraManager.GetOrthographic2D(m_GameViewPreviewCameraId))
        {
            orthographicCamera->SetPosition(position);
            const float rotationRadians = std::atan2(worldTransform[1][0], worldTransform[0][0]);
            orthographicCamera->SetRotationRadians(rotationRadians);
        }
        else if (auto* perspectiveCamera = m_CameraManager.GetPerspective3D(m_GameViewPreviewCameraId))
        {
            perspectiveCamera->SetPosition(position);
            const glm::vec3 forward = glm::normalize(glm::vec3(worldTransform * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
            const float yawDegrees = glm::degrees(std::atan2(forward.z, forward.x));
            const float pitchDegrees = glm::degrees(std::asin(glm::clamp(forward.y, -1.0f, 1.0f)));
            perspectiveCamera->SetYawPitchDegrees(yawDegrees, pitchDegrees);
        }

        return previewCamera;
    }

    void EditorLayer::DestroyGameViewPreviewCamera()
    {
        if (m_GameViewPreviewCameraId)
        {
            (void)m_CameraManager.DestroyCamera(m_GameViewPreviewCameraId);
            m_GameViewPreviewCameraId = {};
        }
    }

}  // namespace Limitless
