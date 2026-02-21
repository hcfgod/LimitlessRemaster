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
        constexpr uint32_t kEditorSessionStateVersion = 3;
        constexpr std::string_view kSceneAssetSuffix = ".scene.json";

        struct EditorSessionStateData final
        {
            std::string LastOpenedSceneAssetKey;
            EditorInspectorPanel::NativeScriptEditorSessionState NativeScriptEditorState;
            bool ShowProjectSettingsWindow = false;
            bool ShowAssetDiagnosticsWindow = false;
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

        int64_t GetLastWriteTimeTicksOrZero(const std::filesystem::path& path)
        {
            std::error_code errorCode;
            const auto lastWriteTime = std::filesystem::last_write_time(path, errorCode);
            if (errorCode)
                return 0;
            return static_cast<int64_t>(lastWriteTime.time_since_epoch().count());
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

                if (version != kEditorSessionStateVersion)
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

        AudioListener2DRuntimeState ResolveAudioListener2DRuntimeState(const Scene& scene)
        {
            AudioListener2DRuntimeState listenerState{};
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
                        listenerState.HasListener = true;
                        listenerState.Position = ComputeEntityWorldPosition2D(scene, cameraEntity);
                        return listenerState;
                    }
                }

                listenerState.HasListener = true;
                listenerState.Position = ComputeEntityWorldPosition2D(scene, entity);
                return listenerState;
            }

            // Fallback behavior keeps authored scenes audible even before a listener is added.
            entt::entity fallbackCameraEntity = entt::null;
            if (TryFindPrimaryCameraEntity(scene, fallbackCameraEntity))
            {
                listenerState.HasListener = true;
                listenerState.Position = ComputeEntityWorldPosition2D(scene, fallbackCameraEntity);
            }

            return listenerState;
        }

        AudioSpatialMix2D ComputeAudioSpatialMix2D(const AudioSourceComponent& audioSource,
                                                   const glm::vec2& sourcePosition,
                                                   const AudioListener2DRuntimeState& listenerState)
        {
            AudioSpatialMix2D result{};
            if (audioSource.Space != AudioSourceComponent::PlaybackSpace::Spatial2D || !listenerState.HasListener)
                return result;

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
            const AudioListener2DRuntimeState listenerState = ResolveAudioListener2DRuntimeState(*scene);
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
                const AudioSpatialMix2D spatialMix = ComputeAudioSpatialMix2D(audioSource, sourcePosition, listenerState);
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
            Physics2DWorld* physicsWorld = m_Scene->GetPhysics2DWorld();
            if (physicsWorld)
                physicsWorld->SetDiagnosticsEnabled(m_ShowPhysicsDiagnosticsWindow);

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
            m_ShowAnimationTimelinePanel,
            m_ShowAnimatorGraphPanel,
            m_TilePaletteState.PanelOpen,
            [this]() { EditorProjectDialog::RequestOpen(m_ProjectDialogState, EditorProjectDialog::ProjectDialogMode::Open); },
            [this]() { EditorProjectDialog::RequestOpen(m_ProjectDialogState, EditorProjectDialog::ProjectDialogMode::Create); },
            [this]() { m_ShowProjectSettingsWindow = true; },
            [this]() { m_ShowBuildSettingsWindow = true; },
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
        const std::filesystem::path defaultLayoutPath = "imgui-default.ini";
        const std::filesystem::path activeLayoutPath = "imgui.ini";

        std::error_code errorCode;
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

        const Physics2DWorld* physicsWorld = m_Scene->GetPhysics2DWorld();
        if (physicsWorld)
        {
            const Physics2DDiagnostics& diagnostics = physicsWorld->GetDiagnostics();
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
                if (physicsWorld->TryGetBodyDiagnostics(m_SelectedEntity, bodyDiagnostics))
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

    void EditorLayer::EnterPlayMode()
    {
        if (m_PlayModeState != EditorPlayModeState::Edit)
            return;
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

        StopAudioSourcesInScene(m_Scene.get());
        StopAudioSourcesInScene(m_EditSceneStored.get());
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

        StopAudioSourcesInScene(m_Scene.get());
        StopAudioSourcesInScene(m_EditSceneStored.get());
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
        StopAudioSourcesInScene(m_Scene.get());
        StopAudioSourcesInScene(m_EditSceneStored.get());

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
        StopAudioSourcesInScene(m_Scene.get());

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

    void EditorLayer::DrawSaveScenePopup()
    {
        if (m_RequestOpenSaveScenePopup)
        {
            ImGui::OpenPopup("Save Scene As");
            m_RequestOpenSaveScenePopup = false;
            m_SaveScenePopupOpen = true;
        }

        if (!m_SaveScenePopupOpen)
            return;

        const auto rootResult = Assets::FindProjectRootFromWorkingDirectory();
        if (rootResult.IsFailure())
        {
            LT_ERROR("Could not open Save Scene popup: {}", rootResult.GetError().GetErrorMessage());
            m_SaveScenePopupOpen = false;
            return;
        }

        const std::filesystem::path assetsDirectory = rootResult.GetValue() / "Assets";
        std::error_code errorCode;
        std::filesystem::create_directories(assetsDirectory / m_SaveSceneFolderPath, errorCode);

        if (!ImGui::BeginPopupModal("Save Scene As", &m_SaveScenePopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
            return;

        ImGui::Text("Choose folder and file name");
        ImGui::Separator();
        ImGui::Text("Folder:");
        ImGui::SameLine();
        ImGui::TextUnformatted(("Assets/" + m_SaveSceneFolderPath.generic_string()).c_str());

        ImGui::BeginChild("SceneFolderTree", ImVec2(420.0f, 220.0f), true);
        std::function<void(const std::filesystem::path&)> drawFolderTree;
        drawFolderTree = [&](const std::filesystem::path& relativePath) {
            const std::filesystem::path absolutePath = assetsDirectory / relativePath;
            std::vector<std::filesystem::path> subFolders;
            std::error_code iterateError;
            for (const auto& entry : std::filesystem::directory_iterator(absolutePath, iterateError))
            {
                if (iterateError)
                    continue;
                if (!entry.is_directory())
                    continue;
                const std::string folderName = entry.path().filename().string();
                if (!folderName.empty() && folderName[0] == '.')
                    continue;
                if (folderName == "Cache")
                    continue;
                subFolders.push_back(entry.path().filename());
            }

            std::sort(subFolders.begin(), subFolders.end(), [](const auto& left, const auto& right) {
                return left.string() < right.string();
            });

            const std::string label = relativePath.empty() ? "Assets" : relativePath.filename().string();
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
            if (subFolders.empty())
                flags |= ImGuiTreeNodeFlags_Leaf;
            if (relativePath == m_SaveSceneFolderPath)
                flags |= ImGuiTreeNodeFlags_Selected;

            const bool opened = ImGui::TreeNodeEx(label.c_str(), flags);
            if (ImGui::IsItemClicked())
                m_SaveSceneFolderPath = relativePath;

            if (opened)
            {
                for (const auto& childFolder : subFolders)
                    drawFolderTree(relativePath / childFolder);
                ImGui::TreePop();
            }
        };
        drawFolderTree("");
        ImGui::EndChild();

        ImGui::InputText("File Name", m_SaveSceneFileNameBuffer.data(), m_SaveSceneFileNameBuffer.size());

        bool closePopup = false;
        if (ImGui::Button("Save", ImVec2(130.0f, 0.0f)))
        {
            const std::string normalizedFileName = NormalizeSceneFileName(m_SaveSceneFileNameBuffer.data());
            const std::string assetKey = CreateSceneAssetInFolder(m_SaveSceneFolderPath, normalizedFileName);
            if (!assetKey.empty() && SaveSceneToAssetKey(assetKey))
            {
                m_CurrentSceneAssetKey = assetKey;
                closePopup = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(130.0f, 0.0f)))
        {
            closePopup = true;
        }

        if (closePopup)
        {
            m_SaveScenePopupOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void EditorLayer::DrawSceneSwitchConfirmationPopup()
    {
        if (m_RequestOpenSceneSwitchConfirmationPopup)
        {
            ImGui::OpenPopup("Unsaved Changes");
            m_RequestOpenSceneSwitchConfirmationPopup = false;
            m_SceneSwitchConfirmationPopupOpen = true;
        }

        if (!m_SceneSwitchConfirmationPopupOpen)
            return;

        if (!ImGui::BeginPopupModal("Unsaved Changes", &m_SceneSwitchConfirmationPopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
            return;

        if (IsPrefabAssetKey(m_CurrentSceneAssetKey))
            ImGui::TextWrapped("You have undo history for the current prefab asset. Leaving Prefab Mode will clear undo/redo history.");
        else
            ImGui::TextWrapped("You have undo history for the current scene. Switching scenes will clear undo/redo history.");
        ImGui::Spacing();
        if (IsPrefabAssetKey(m_CurrentSceneAssetKey))
            ImGui::TextWrapped("Do you want to save your prefab changes before leaving?");
        else
            ImGui::TextWrapped("Do you want to save before switching?");

        if (ImGui::Button("Save", ImVec2(120.0f, 0.0f)))
        {
            bool saved = false;
            if (!m_CurrentSceneAssetKey.empty())
                saved = SaveSceneToAssetKey(m_CurrentSceneAssetKey);
            else
                SaveSceneAs();

            if (saved && m_PendingSceneSwitchAction)
            {
                auto action = std::move(m_PendingSceneSwitchAction);
                m_PendingSceneSwitchAction = nullptr;
                action();
            }
            m_SceneSwitchConfirmationPopupOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard", ImVec2(120.0f, 0.0f)))
        {
            if (m_PendingSceneSwitchAction)
            {
                auto action = std::move(m_PendingSceneSwitchAction);
                m_PendingSceneSwitchAction = nullptr;
                action();
            }
            m_SceneSwitchConfirmationPopupOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
        {
            m_PendingSceneSwitchAction = nullptr;
            m_SceneSwitchConfirmationPopupOpen = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
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


    void EditorLayer::PersistProjectSessionState()
    {
        const auto& projectManager = Project::ProjectManager::GetInstance();
        if (!projectManager.HasOpenProject())
            return;

        EditorSessionStateData state{};
        if (m_PlayModeState != EditorPlayModeState::Edit)
            state.LastOpenedSceneAssetKey = m_EditSceneStoredAssetKey;
        else if (IsPrefabAssetKey(m_CurrentSceneAssetKey) && !m_PrefabModeReturnSceneAssetKey.empty())
            state.LastOpenedSceneAssetKey = m_PrefabModeReturnSceneAssetKey;
        else
            state.LastOpenedSceneAssetKey = m_CurrentSceneAssetKey;
        EditorInspectorPanel::GetNativeScriptEditorSessionState(state.NativeScriptEditorState);
        state.ShowProjectSettingsWindow = m_ShowProjectSettingsWindow;
        state.ShowAssetDiagnosticsWindow = m_ShowAssetDiagnosticsWindow;
        WriteProjectSessionState(projectManager.GetProjectRoot(), state);
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
        StopAudioSourcesInScene(m_Scene.get());
        StopAudioSourcesInScene(m_EditSceneStored.get());

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

        StopAudioSourcesInScene(m_Scene.get());

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

    void EditorLayer::ProcessPendingSceneTransitions()
    {
        const std::optional<SceneTransitionRequest> pendingTransition = SceneManager::ConsumePendingSceneTransition();
        if (!pendingTransition.has_value())
            return;

        switch (pendingTransition->Type)
        {
            case SceneTransitionType::LoadByAssetKey:
            {
                const auto resolvedSceneAssetKey = ResolveSceneIdentifierToAssetKey(pendingTransition->SceneIdentifier);
                if (!resolvedSceneAssetKey.has_value())
                {
                    LT_WARN("Scene transition request '{}' could not be resolved to a scene asset key.",
                        pendingTransition->SceneIdentifier);
                    break;
                }

                // Guard against accidental self-load loops in Play Mode.
                // Example: script OnCreate calls LoadScene(currentScene) each time scene starts.
                if (m_PlayModeState != EditorPlayModeState::Edit &&
                    !m_CurrentSceneAssetKey.empty() &&
                    *resolvedSceneAssetKey == m_CurrentSceneAssetKey)
                {
                    LT_WARN("Ignored SceneManager::LoadScene('{}') in Play Mode because it matches the active scene. Use ReloadCurrentScene() for intentional restart.",
                        *resolvedSceneAssetKey);
                    break;
                }

                if (m_PlayModeState == EditorPlayModeState::Edit)
                    (void)LoadSceneFromAssetKey(*resolvedSceneAssetKey);
                else
                    (void)LoadSceneFromAssetKeyInPlayMode(*resolvedSceneAssetKey);
                break;
            }
            case SceneTransitionType::ReloadCurrentScene:
            {
                if (m_CurrentSceneAssetKey.empty())
                {
                    LT_WARN("ReloadCurrentScene requested but no active scene asset key is available.");
                    break;
                }

                if (m_PlayModeState == EditorPlayModeState::Edit)
                    (void)LoadSceneFromAssetKey(m_CurrentSceneAssetKey);
                else
                    (void)LoadSceneFromAssetKeyInPlayMode(m_CurrentSceneAssetKey);
                break;
            }
        }
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

    void EditorLayer::RefreshProjectAudioSettings()
    {
        const auto& projectManager = Project::ProjectManager::GetInstance();
        if (!projectManager.HasOpenProject())
            return;

        const auto audioSettingsResult = Project::LoadAudioSettings(projectManager.GetProjectRoot());
        if (audioSettingsResult.IsSuccess())
        {
            m_ProjectAudioSettings = audioSettingsResult.GetValue();
            m_ProjectAudioSettingsLoaded = true;
            m_ProjectSettingsPanelState.Audio = m_ProjectAudioSettings;
            m_ProjectAppliedAudioMixerAssetKey.clear();
            m_ProjectAppliedAudioMixerLastWriteTimeTicks = 0;
            ApplyProjectAudioSettings();
        }
        else
        {
            LT_WARN("Failed to load project audio settings: {}", audioSettingsResult.GetError().GetErrorMessage());
            m_ProjectAudioSettingsLoaded = false;
        }
    }

    void EditorLayer::RefreshProjectRenderSettings()
    {
        const auto& projectManager = Project::ProjectManager::GetInstance();
        if (!projectManager.HasOpenProject())
            return;

        const auto renderSettingsResult = Project::LoadRenderSettings(projectManager.GetProjectRoot());
        if (renderSettingsResult.IsSuccess())
        {
            m_ProjectRenderSettings = renderSettingsResult.GetValue();
            m_ProjectRenderSettingsLoaded = true;
            m_ProjectSettingsPanelState.Render = m_ProjectRenderSettings;
            ApplyProjectRenderSettings();
        }
        else
        {
            LT_WARN("Failed to load project render settings: {}", renderSettingsResult.GetError().GetErrorMessage());
            m_ProjectRenderSettingsLoaded = false;
        }
    }

    void EditorLayer::ApplyProjectRenderSettings()
    {
        if (!m_ProjectRenderSettingsLoaded)
            return;

        const glm::vec4 clearColor(
            std::clamp(m_ProjectRenderSettings.ClearColor[0], 0.0f, 1.0f),
            std::clamp(m_ProjectRenderSettings.ClearColor[1], 0.0f, 1.0f),
            std::clamp(m_ProjectRenderSettings.ClearColor[2], 0.0f, 1.0f),
            std::clamp(m_ProjectRenderSettings.ClearColor[3], 0.0f, 1.0f));

        if (!m_ProjectRenderVSyncApplied || m_ProjectRenderAppliedVSyncValue != m_ProjectRenderSettings.VSync)
        {
            auto& window = Application::GetInstance().GetWindow();
            window.SetVSync(m_ProjectRenderSettings.VSync);
            m_ProjectRenderAppliedVSyncValue = m_ProjectRenderSettings.VSync;
            m_ProjectRenderVSyncApplied = true;
        }

        const auto clearColorChanged = [](const glm::vec4& a, const glm::vec4& b) {
            constexpr float epsilon = 0.0001f;
            return std::abs(a.r - b.r) > epsilon ||
                   std::abs(a.g - b.g) > epsilon ||
                   std::abs(a.b - b.b) > epsilon ||
                   std::abs(a.a - b.a) > epsilon;
        };

        if (clearColorChanged(clearColor, m_ProjectRenderAppliedClearColor))
        {
            SceneRenderer::SetViewportClearColor(clearColor);
            m_ProjectRenderAppliedClearColor = clearColor;
        }
    }

    void EditorLayer::ApplyProjectAudioSettings()
    {
        if (!m_ProjectAudioSettingsLoaded)
            return;

        Audio::AudioEngine& audioEngine = Audio::AudioEngine::GetInstance();
        const float masterVolume = m_ProjectAudioSettings.Muted
            ? 0.0f
            : std::max(0.0f, m_ProjectAudioSettings.MasterVolume);
        audioEngine.SetMasterVolume(masterVolume);

        if (m_ProjectAudioSettings.MixerAssetKey.empty())
            return;

        std::filesystem::path mixerResolvedPath;
        Audio::AudioMixerDefinition mixerDefinition{};
        if (!Audio::LoadAudioMixerDefinitionFromAssetKey(
                m_ProjectAudioSettings.MixerAssetKey,
                mixerDefinition,
                &mixerResolvedPath))
        {
            return;
        }

        const int64_t lastWriteTicks = GetLastWriteTimeTicksOrZero(mixerResolvedPath);
        const bool shouldApplyMixer =
            m_ProjectAppliedAudioMixerAssetKey != m_ProjectAudioSettings.MixerAssetKey ||
            m_ProjectAppliedAudioMixerLastWriteTimeTicks != lastWriteTicks;
        if (!shouldApplyMixer)
            return;

        for (const auto& group : mixerDefinition.Groups)
            audioEngine.SetMixerGroupVolume(group.Name, group.Volume);

        m_ProjectAppliedAudioMixerAssetKey = m_ProjectAudioSettings.MixerAssetKey;
        m_ProjectAppliedAudioMixerLastWriteTimeTicks = lastWriteTicks;
    }

    void EditorLayer::RefreshProjectPhysics2DSettings()
    {
        const auto& pm = Project::ProjectManager::GetInstance();
        if (!pm.HasOpenProject())
            return;

        const auto physicsSettingsResult = Project::LoadPhysics2DSettings(pm.GetProjectRoot());
        if (physicsSettingsResult.IsSuccess())
        {
            m_ProjectPhysics2DSettings = physicsSettingsResult.GetValue();
            m_ProjectPhysics2DSettingsLoaded = true;
            m_ProjectSettingsPanelState.Physics2D = m_ProjectPhysics2DSettings;
        }
        else
        {
            LT_WARN("Failed to load project physics settings: {}", physicsSettingsResult.GetError().GetErrorMessage());
            m_ProjectPhysics2DSettingsLoaded = false;
        }
    }

    void EditorLayer::RefreshProjectLighting2DSettings()
    {
        const auto& pm = Project::ProjectManager::GetInstance();
        if (!pm.HasOpenProject())
            return;

        const auto lightingSettingsResult = Project::LoadLighting2DSettings(pm.GetProjectRoot());
        if (lightingSettingsResult.IsSuccess())
        {
            m_ProjectLighting2DSettings = lightingSettingsResult.GetValue();
            m_ProjectLighting2DSettingsLoaded = true;
            m_ProjectSettingsPanelState.Lighting2D = m_ProjectLighting2DSettings;
            ApplyProjectLighting2DSettings();
        }
        else
        {
            LT_WARN("Failed to load project lighting settings: {}", lightingSettingsResult.GetError().GetErrorMessage());
            m_ProjectLighting2DSettingsLoaded = false;
        }
    }

    void EditorLayer::ApplyProjectPhysics2DSettingsToScenes()
    {
        if (!m_ProjectPhysics2DSettingsLoaded)
            return;

        Physics2DWorldSettings runtimeSettings{};
        runtimeSettings.Gravity = glm::vec2(m_ProjectPhysics2DSettings.GravityX, m_ProjectPhysics2DSettings.GravityY);
        runtimeSettings.VelocitySubSteps = std::max(1, m_ProjectPhysics2DSettings.VelocitySubSteps);
        runtimeSettings.EnableSleep = m_ProjectPhysics2DSettings.EnableSleep;
        runtimeSettings.EnableContinuousCollision = m_ProjectPhysics2DSettings.EnableContinuousCollision;
        runtimeSettings.HighContactQualityMode = m_ProjectPhysics2DSettings.HighContactQualityMode;
        runtimeSettings.HighContactQualityExtraSubSteps = std::max(0, m_ProjectPhysics2DSettings.HighContactQualityExtraSubSteps);
        runtimeSettings.ContactHertz = m_ProjectPhysics2DSettings.ContactHertz;
        runtimeSettings.ContactDampingRatio = m_ProjectPhysics2DSettings.ContactDampingRatio;
        runtimeSettings.ContactPushSpeed = m_ProjectPhysics2DSettings.ContactPushSpeed;

        if (m_Scene)
            m_Scene->SetPhysics2DSettings(runtimeSettings);
        if (m_EditSceneStored)
            m_EditSceneStored->SetPhysics2DSettings(runtimeSettings);
    }

    void EditorLayer::ApplyProjectLighting2DSettings()
    {
        if (!m_ProjectLighting2DSettingsLoaded)
            return;

        Lighting2DSettings runtimeSettings{};
        runtimeSettings.Enabled = m_ProjectLighting2DSettings.Enabled;
        runtimeSettings.EnableNormalMaps = m_ProjectLighting2DSettings.EnableNormalMaps;
        runtimeSettings.EnableShadows = m_ProjectLighting2DSettings.EnableShadows;
        runtimeSettings.AmbientColor = glm::vec3(
            m_ProjectLighting2DSettings.AmbientColor[0],
            m_ProjectLighting2DSettings.AmbientColor[1],
            m_ProjectLighting2DSettings.AmbientColor[2]);
        runtimeSettings.AmbientIntensity = m_ProjectLighting2DSettings.AmbientIntensity;
        runtimeSettings.ShadowQualityLevel = std::clamp(m_ProjectLighting2DSettings.ShadowQualityLevel, 0, 2);
        runtimeSettings.MaxDirectionalLights = std::max(0, m_ProjectLighting2DSettings.MaxDirectionalLights);
        runtimeSettings.MaxPointLights = std::max(0, m_ProjectLighting2DSettings.MaxPointLights);
        runtimeSettings.MaxShadowSegments = std::max(1, m_ProjectLighting2DSettings.MaxShadowSegments);
        runtimeSettings.ShadowSoftnessScale = std::max(0.0f, m_ProjectLighting2DSettings.ShadowSoftnessScale);
        runtimeSettings.DirectionalShadowBiasScale = std::max(0.0f, m_ProjectLighting2DSettings.DirectionalShadowBiasScale);
        runtimeSettings.ShadowAlphaCutoff = std::clamp(m_ProjectLighting2DSettings.ShadowAlphaCutoff, 0.0f, 1.0f);
        runtimeSettings.ShadowSegmentSnapPixels = std::max(0.0f, m_ProjectLighting2DSettings.ShadowSegmentSnapPixels);
        runtimeSettings.EnableHighAngularVelocityShadowFreeze = m_ProjectLighting2DSettings.EnableHighAngularVelocityShadowFreeze;
        runtimeSettings.ShadowFreezeAngularVelocityDegreesPerSecond = std::max(1.0f, m_ProjectLighting2DSettings.ShadowFreezeAngularVelocityDegreesPerSecond);
        runtimeSettings.ShadowFreezeFrameCount = std::max(1, m_ProjectLighting2DSettings.ShadowFreezeFrameCount);
        runtimeSettings.MaxShadowSamplesPerLight = std::max(1, m_ProjectLighting2DSettings.MaxShadowSamplesPerLight);
        Lighting2DRenderer::SetSettings(runtimeSettings);
    }

    void EditorLayer::LaunchStartupAssetImport()
    {
        if (m_StartupAssetImportInProgress)
            return;

        const auto existingRecords = Assets::AssetDatabase::GetInstance().GetAllRecords();
        size_t tileRecordCount = 0;
        for (const auto& record : existingRecords)
        {
            if (record.Type == Assets::AssetType::Tile)
                ++tileRecordCount;
        }

        // Use a lighter changed-only pass for established projects to avoid an
        // expensive dependent cascade while the editor is becoming interactive.
        const bool includeDependents = existingRecords.empty();
        m_StartupAssetImportInProgress = true;
        m_StartupAssetImportTask = Async::GetAsyncIO().RunAsync([includeDependents]() -> std::string {
            const auto result = Assets::AssetImportPipeline::ReimportChanged(includeDependents);
            if (result.IsFailure())
                return std::string("error: ") + result.GetError().GetErrorMessage();

            const auto& stats = result.GetValue();
            std::ostringstream stream;
            stream << "discovered=" << stats.DiscoveredFiles
                   << " imported=" << stats.Imported
                   << " skipped=" << stats.SkippedUpToDate
                   << " missing=" << stats.MissingOnDisk
                   << " errors=" << stats.Errors;
            return stream.str();
        });

        LT_INFO(
            "Scheduled background startup reimport (records={}, tileRecords={}, includeDependents={}).",
            existingRecords.size(),
            tileRecordCount,
            includeDependents ? "true" : "false");
    }

    void EditorLayer::PumpStartupAssetImport()
    {
        if (!m_StartupAssetImportInProgress || !m_StartupAssetImportTask.IsValid() || !m_StartupAssetImportTask.IsDone())
            return;

        try
        {
            const std::string result = m_StartupAssetImportTask.Get();
            if (result.rfind("error:", 0) == 0)
                LT_WARN("Background startup reimport failed: {}", result);
            else
                LT_INFO("Background startup reimport completed: {}", result);
        }
        catch (const std::exception& exception)
        {
            LT_WARN("Background startup reimport failed with exception: {}", exception.what());
        }
        catch (...)
        {
            LT_WARN("Background startup reimport failed with unknown exception.");
        }

        m_StartupAssetImportTask = Async::Task<std::string>();
        m_StartupAssetImportInProgress = false;
    }

    void EditorLayer::QueueSceneAssetPrewarm()
    {
        m_ActiveSceneTexturePrewarmKeys.clear();
        m_ActiveSceneMaterialPrewarmKeys.clear();

        if (!m_Scene)
            return;

        auto& registry = m_Scene->GetRegistry();

        auto spriteView = registry.view<SpriteComponent>();
        for (entt::entity entity : spriteView)
        {
            const auto& sprite = spriteView.get<SpriteComponent>(entity);
            if (sprite.TextureKey.empty())
                continue;

            m_ActiveSceneTexturePrewarmKeys.insert(sprite.TextureKey);
            if (m_PrewarmedTextureAssets.contains(sprite.TextureKey) || m_PendingTexturePrewarmTasks.contains(sprite.TextureKey))
                continue;

            m_PendingTexturePrewarmTasks.emplace(sprite.TextureKey, Assets::TextureAsset::LoadAsync(sprite.TextureKey));
        }

        auto materialView = registry.view<MaterialComponent>();
        for (entt::entity entity : materialView)
        {
            const auto& material = materialView.get<MaterialComponent>(entity);
            if (material.MaterialKey.empty())
                continue;

            m_ActiveSceneMaterialPrewarmKeys.insert(material.MaterialKey);
            if (m_PrewarmedMaterialAssets.contains(material.MaterialKey) || m_PendingMaterialPrewarmTasks.contains(material.MaterialKey))
                continue;

            m_PendingMaterialPrewarmTasks.emplace(material.MaterialKey, Assets::MaterialAsset::LoadAsync(material.MaterialKey));
        }

        // Prewarm tile textures so the render cache doesn't stall on first frame.
        auto tilemapView = registry.view<TilemapLayerComponent>();
        for (entt::entity entity : tilemapView)
        {
            const auto& layer = tilemapView.get<TilemapLayerComponent>(entity);
            std::vector<bool> usedTileTableEntries(layer.TileTable.size(), false);
            for (uint32_t tileId : layer.Tiles)
            {
                if (tileId == 0u)
                    continue;
                if (static_cast<size_t>(tileId) < usedTileTableEntries.size())
                    usedTileTableEntries[tileId] = true;
            }

            for (size_t tableIndex = 0; tableIndex < layer.TileTable.size(); ++tableIndex)
            {
                if (tableIndex >= usedTileTableEntries.size() || !usedTileTableEntries[tableIndex])
                    continue;

                const std::string& tileKey = layer.TileTable[tableIndex];
                if (tileKey.empty())
                    continue;

                auto tileResult = Assets::LoadTileAssetData(tileKey);
                if (tileResult.IsFailure())
                    continue;

                const std::string& textureKey = tileResult.GetValue().SpriteTextureKey;
                if (textureKey.empty())
                    continue;

                m_ActiveSceneTexturePrewarmKeys.insert(textureKey);
                if (m_PrewarmedTextureAssets.contains(textureKey) || m_PendingTexturePrewarmTasks.contains(textureKey))
                    continue;

                m_PendingTexturePrewarmTasks.emplace(textureKey, Assets::TextureAsset::LoadAsync(textureKey));
            }
        }
    }

    bool EditorLayer::IsSceneAssetPrewarmComplete() const
    {
        for (const std::string& textureKey : m_ActiveSceneTexturePrewarmKeys)
        {
            if (m_PendingTexturePrewarmTasks.contains(textureKey))
                return false;
        }

        for (const std::string& materialKey : m_ActiveSceneMaterialPrewarmKeys)
        {
            if (m_PendingMaterialPrewarmTasks.contains(materialKey))
                return false;
        }

        return true;
    }

    void EditorLayer::UpdateSceneLoadingState()
    {
        if (!m_Scene || m_Scene->GetLoadState() != Scene::LoadState::Loading)
            return;

        // Explicitly initialize physics runtime while the scene is loading so
        // rendering only starts after colliders/bodies are fully prepared.
        (void)m_Scene->InitializePhysicsWorldForLoading();

        const bool shaderReady = Renderer2D::IsShaderReady();
        const bool assetsReady = IsSceneAssetPrewarmComplete();
        const bool objectsReady = m_Scene->IsSceneObjectsInitialized();
        const bool physicsReady = m_Scene->IsPhysicsWorldInitializedForLoading();

        if (shaderReady && assetsReady && objectsReady && physicsReady)
        {
            m_Scene->SetLoadStateReady();
            LT_INFO("Scene load completed and is now ready for rendering.");
        }
    }

    void EditorLayer::PumpSceneAssetPrewarm()
    {
        for (auto it = m_PendingTexturePrewarmTasks.begin(); it != m_PendingTexturePrewarmTasks.end();)
        {
            if (!it->second.IsDone())
            {
                ++it;
                continue;
            }

            try
            {
                if (auto loaded = it->second.Get())
                    m_PrewarmedTextureAssets[it->first] = loaded;
            }
            catch (...) {}

            it = m_PendingTexturePrewarmTasks.erase(it);
        }

        for (auto it = m_PendingMaterialPrewarmTasks.begin(); it != m_PendingMaterialPrewarmTasks.end();)
        {
            if (!it->second.IsDone())
            {
                ++it;
                continue;
            }

            try
            {
                if (auto loaded = it->second.Get())
                    m_PrewarmedMaterialAssets[it->first] = loaded;
            }
            catch (...) {}

            it = m_PendingMaterialPrewarmTasks.erase(it);
        }
    }

    bool EditorLayer::OpenPrefabAssetForEditing(const std::string& prefabAssetKey)
    {
        if (prefabAssetKey.empty())
            return false;

        if (!EnsureSceneSwitchAllowed([this, prefabAssetKey]() {
                if (!IsPrefabAssetKey(m_CurrentSceneAssetKey))
                    m_PrefabModeReturnSceneAssetKey = m_CurrentSceneAssetKey;
                (void)LoadSceneFromAssetKey(prefabAssetKey, true);
            }))
        {
            return false;
        }

        if (!IsPrefabAssetKey(m_CurrentSceneAssetKey))
            m_PrefabModeReturnSceneAssetKey = m_CurrentSceneAssetKey;

        return LoadSceneFromAssetKey(prefabAssetKey, true);
    }

    bool EditorLayer::ReturnFromPrefabMode(bool forceWithoutConfirmation)
    {
        if (!IsPrefabAssetKey(m_CurrentSceneAssetKey))
            return false;
        if (m_PrefabModeReturnSceneAssetKey.empty())
            return false;

        const std::string returnSceneKey = m_PrefabModeReturnSceneAssetKey;

        if (!forceWithoutConfirmation)
        {
            if (!EnsureSceneSwitchAllowed([this, returnSceneKey]() {
                    m_PrefabModeReturnSceneAssetKey.clear();
                    (void)LoadSceneFromAssetKey(returnSceneKey, true);
                }))
            {
                return false;
            }
        }

        m_PrefabModeReturnSceneAssetKey.clear();
        return LoadSceneFromAssetKey(returnSceneKey, true);
    }

    bool EditorLayer::ApplyPrefabStageChangesToInstances()
    {
        if (!IsPrefabAssetKey(m_CurrentSceneAssetKey))
            return false;
        if (m_PrefabModeReturnSceneAssetKey.empty())
            return false;
        if (!m_Scene)
            return false;

        const std::string prefabAssetKey = m_CurrentSceneAssetKey;
        const std::string returnSceneKey = m_PrefabModeReturnSceneAssetKey;

        // Save the prefab asset before pushing changes to instances.
        if (!SaveSceneToAssetKey(prefabAssetKey))
            return false;

        // Switch back to the scene we came from, then apply to all instances there.
        m_PrefabModeReturnSceneAssetKey.clear();
        if (!LoadSceneFromAssetKey(returnSceneKey, true))
            return false;

        return m_EditorUndoService.ExecuteSceneMutation("Apply Prefab To Instances", [&](Scene& mutableScene) {
            return EditorPrefabSystem::ApplyPrefabAssetToInstancesInScene(mutableScene, prefabAssetKey);
        });
    }

    std::string EditorLayer::CreateSceneAssetInFolder(const std::filesystem::path& relativeFolderPath, const std::string& preferredFileName)
    {
        const auto rootResult = Assets::FindProjectRootFromWorkingDirectory();
        if (rootResult.IsFailure())
        {
            LT_ERROR("Could not create scene asset: {}", rootResult.GetError().GetErrorMessage());
            return {};
        }

        const std::filesystem::path assetsDirectory = rootResult.GetValue() / "Assets";
        const std::filesystem::path targetDirectory = assetsDirectory / relativeFolderPath;
        std::error_code errorCode;
        std::filesystem::create_directories(targetDirectory, errorCode);
        if (errorCode)
        {
            LT_ERROR("Could not create scene folder {}: {}", targetDirectory.string(), errorCode.message());
            return {};
        }

        const std::string normalizedFileName = preferredFileName.empty()
            ? std::string(kDefaultSceneFileName)
            : NormalizeSceneFileName(preferredFileName.c_str());
        std::filesystem::path scenePath = targetDirectory / normalizedFileName;
        if (preferredFileName.empty() && std::filesystem::exists(scenePath, errorCode))
        {
            for (int32_t index = 1; index < 1024; ++index)
            {
                const std::filesystem::path candidate = targetDirectory / ("SampleScene " + std::to_string(index) + ".scene.json");
                if (!std::filesystem::exists(candidate, errorCode))
                {
                    scenePath = candidate;
                    break;
                }
            }
        }

        if (!std::filesystem::exists(scenePath, errorCode))
        {
            Scene scene;
            PopulateDefaultSceneTemplate(scene);
            const auto saveResult = scene.SaveToFile(scenePath);
            if (saveResult.IsFailure())
            {
                LT_ERROR("Could not create scene asset {}: {}", scenePath.string(), saveResult.GetError().GetErrorMessage());
                return {};
            }
        }

        std::filesystem::path relativeAssetPath = std::filesystem::relative(scenePath, assetsDirectory, errorCode);
        if (errorCode)
        {
            LT_ERROR("Could not compute scene asset key for {}", scenePath.string());
            return {};
        }

        const std::string assetKey = "Assets/" + relativeAssetPath.generic_string();
        const auto importResult = Assets::AssetDatabase::GetInstance().ImportOrUpdate(assetKey, Assets::AssetType::Scene);
        if (importResult.IsFailure())
        {
            LT_WARN("Created scene asset but failed to import into AssetDatabase ({}): {}", assetKey, importResult.GetError().GetErrorMessage());
        }

        LT_INFO("Created scene asset {}", assetKey);
        return assetKey;
    }

    std::string EditorLayer::CreateMaterialAssetInFolder(const std::filesystem::path& relativeFolderPath, const std::string& preferredFileName)
    {
        const auto rootResult = Assets::FindProjectRootFromWorkingDirectory();
        if (rootResult.IsFailure())
        {
            LT_ERROR("Could not create material asset: {}", rootResult.GetError().GetErrorMessage());
            return {};
        }

        const std::filesystem::path assetsDirectory = rootResult.GetValue() / "Assets";
        const std::filesystem::path targetDirectory = assetsDirectory / relativeFolderPath;
        std::error_code errorCode;
        std::filesystem::create_directories(targetDirectory, errorCode);
        if (errorCode)
        {
            LT_ERROR("Could not create material folder {}: {}", targetDirectory.string(), errorCode.message());
            return {};
        }

        const std::string baseName = preferredFileName.empty() ? std::string("New Material") : preferredFileName;
        std::string finalFileName = baseName;
        if (!finalFileName.ends_with(".material.json"))
            finalFileName += ".material.json";

        std::filesystem::path materialPath = targetDirectory / finalFileName;
        if (std::filesystem::exists(materialPath, errorCode))
        {
            for (int32_t index = 1; index < 1024; ++index)
            {
                const std::filesystem::path candidate = targetDirectory / ("New Material " + std::to_string(index) + ".material.json");
                if (!std::filesystem::exists(candidate, errorCode))
                {
                    materialPath = candidate;
                    break;
                }
            }
        }

        nlohmann::json materialJson = {
            { "shader", { { "key", "Assets/Shaders/Renderer2D_TexturedQuad.glsl" } } }
        };

        {
            std::ofstream output(materialPath, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!output.is_open())
            {
                LT_ERROR("Could not create material asset {}", materialPath.string());
                return {};
            }
            output << materialJson.dump(2);
        }

        std::filesystem::path relativeAssetPath = std::filesystem::relative(materialPath, assetsDirectory, errorCode);
        if (errorCode)
        {
            LT_ERROR("Could not compute material asset key for {}", materialPath.string());
            return {};
        }

        const std::string assetKey = "Assets/" + relativeAssetPath.generic_string();
        const auto importResult = Assets::AssetDatabase::GetInstance().ImportOrUpdate(assetKey, Assets::AssetType::Material);
        if (importResult.IsFailure())
            LT_WARN("Created material asset but failed to import into AssetDatabase ({}): {}", assetKey, importResult.GetError().GetErrorMessage());

        LT_INFO("Created material asset {}", assetKey);
        return assetKey;
    }

    std::string EditorLayer::CreateTilesetAssetInFolder(const std::filesystem::path& relativeFolderPath, const std::string& preferredFileName)
    {
        const auto rootResult = Assets::FindProjectRootFromWorkingDirectory();
        if (rootResult.IsFailure())
        {
            LT_ERROR("Could not create tileset asset: {}", rootResult.GetError().GetErrorMessage());
            return {};
        }

        const std::filesystem::path assetsDirectory = rootResult.GetValue() / "Assets";
        const std::filesystem::path targetDirectory = assetsDirectory / relativeFolderPath;
        std::error_code errorCode;
        std::filesystem::create_directories(targetDirectory, errorCode);
        if (errorCode)
        {
            LT_ERROR("Could not create tileset folder {}: {}", targetDirectory.string(), errorCode.message());
            return {};
        }

        const std::string baseName = preferredFileName.empty() ? std::string("New Tileset") : preferredFileName;
        std::string finalFileName = baseName;
        if (!finalFileName.ends_with(".tileset.json"))
            finalFileName += ".tileset.json";

        std::filesystem::path tilesetPath = targetDirectory / finalFileName;
        if (std::filesystem::exists(tilesetPath, errorCode))
        {
            for (int32_t index = 1; index < 1024; ++index)
            {
                const std::filesystem::path candidate = targetDirectory / ("New Tileset " + std::to_string(index) + ".tileset.json");
                if (!std::filesystem::exists(candidate, errorCode))
                {
                    tilesetPath = candidate;
                    break;
                }
            }
        }

        nlohmann::json tilesetJson = {
            { "TextureKey", "" },
            { "TileSizePixels", { 16, 16 } }
        };

        {
            std::ofstream output(tilesetPath, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!output.is_open())
            {
                LT_ERROR("Could not create tileset asset {}", tilesetPath.string());
                return {};
            }
            output << tilesetJson.dump(2);
        }

        std::filesystem::path relativeAssetPath = std::filesystem::relative(tilesetPath, assetsDirectory, errorCode);
        if (errorCode)
        {
            LT_ERROR("Could not compute tileset asset key for {}", tilesetPath.string());
            return {};
        }

        const std::string assetKey = "Assets/" + relativeAssetPath.generic_string();
        const auto importResult = Assets::AssetDatabase::GetInstance().ImportOrUpdate(assetKey, Assets::AssetType::Tileset);
        if (importResult.IsFailure())
            LT_WARN("Created tileset asset but failed to import into AssetDatabase ({}): {}", assetKey, importResult.GetError().GetErrorMessage());

        LT_INFO("Created tileset asset {}", assetKey);
        m_SelectedTilesetAssetKey = assetKey;
        m_SelectedTextureAssetKey.clear();
        m_CachedTextureAsset.reset();
        m_SelectedMaterialAssetKey.clear();
        m_CachedMaterialAsset.reset();
        m_SelectedNativeScriptAssetKey.clear();
        m_SelectedPrefabAssetKey.clear();
        m_SelectedTilesetAssetKey.clear();
        m_SelectedAudioMixerAssetKey.clear();
        m_SelectedInputActionsAssetKey.clear();
        m_SelectedAnimationClipAssetKey.clear();
        m_SelectedAnimatorControllerAssetKey.clear();
        m_SelectedEntity = entt::null;
        return assetKey;
    }

    std::string EditorLayer::CreateAudioMixerAssetInFolder(const std::filesystem::path& relativeFolderPath, const std::string& preferredFileName)
    {
        const auto rootResult = Assets::FindProjectRootFromWorkingDirectory();
        if (rootResult.IsFailure())
        {
            LT_ERROR("Could not create audio mixer asset: {}", rootResult.GetError().GetErrorMessage());
            return {};
        }

        const std::filesystem::path assetsDirectory = rootResult.GetValue() / "Assets";
        const std::filesystem::path targetDirectory = assetsDirectory / relativeFolderPath;
        std::error_code errorCode;
        std::filesystem::create_directories(targetDirectory, errorCode);
        if (errorCode)
        {
            LT_ERROR("Could not create audio mixer folder {}: {}", targetDirectory.string(), errorCode.message());
            return {};
        }

        const std::string baseName = preferredFileName.empty() ? std::string("New Audio Mixer") : preferredFileName;
        std::string finalFileName = baseName;
        if (!finalFileName.ends_with(".audiomixer.json"))
            finalFileName += ".audiomixer.json";

        std::filesystem::path mixerPath = targetDirectory / finalFileName;
        if (std::filesystem::exists(mixerPath, errorCode))
        {
            for (int32_t index = 1; index < 1024; ++index)
            {
                const std::filesystem::path candidate = targetDirectory / ("New Audio Mixer " + std::to_string(index) + ".audiomixer.json");
                if (!std::filesystem::exists(candidate, errorCode))
                {
                    mixerPath = candidate;
                    break;
                }
            }
        }

        Audio::AudioMixerDefinition definition{};
        Audio::NormalizeAudioMixerDefinition(definition);
        if (!Audio::SaveAudioMixerDefinitionToPath(mixerPath, definition))
        {
            LT_ERROR("Could not create audio mixer asset {}", mixerPath.string());
            return {};
        }

        std::filesystem::path relativeAssetPath = std::filesystem::relative(mixerPath, assetsDirectory, errorCode);
        if (errorCode)
        {
            LT_ERROR("Could not compute audio mixer asset key for {}", mixerPath.string());
            return {};
        }

        const std::string assetKey = "Assets/" + relativeAssetPath.generic_string();
        const auto importResult = Assets::AssetDatabase::GetInstance().ImportOrUpdate(assetKey, Assets::AssetType::AudioMixer);
        if (importResult.IsFailure())
            LT_WARN("Created audio mixer asset but failed to import into AssetDatabase ({}): {}", assetKey, importResult.GetError().GetErrorMessage());

        LT_INFO("Created audio mixer asset {}", assetKey);
        m_SelectedAudioMixerAssetKey = assetKey;
        m_SelectedTextureAssetKey.clear();
        m_CachedTextureAsset.reset();
        m_SelectedMaterialAssetKey.clear();
        m_CachedMaterialAsset.reset();
        m_SelectedNativeScriptAssetKey.clear();
        m_SelectedPrefabAssetKey.clear();
        m_SelectedTilesetAssetKey.clear();
        m_SelectedInputActionsAssetKey.clear();
        m_SelectedAnimationClipAssetKey.clear();
        m_SelectedAnimatorControllerAssetKey.clear();
        m_SelectedEntity = entt::null;
        return assetKey;
    }

    std::string EditorLayer::CreateAnimationClipAssetInFolder(const std::filesystem::path& relativeFolderPath, const std::string& preferredFileName)
    {
        const auto rootResult = Assets::FindProjectRootFromWorkingDirectory();
        if (rootResult.IsFailure())
        {
            LT_ERROR("Could not create animation clip asset: {}", rootResult.GetError().GetErrorMessage());
            return {};
        }

        const std::filesystem::path assetsDirectory = rootResult.GetValue() / "Assets";
        const std::filesystem::path targetDirectory = assetsDirectory / relativeFolderPath;
        std::error_code errorCode;
        std::filesystem::create_directories(targetDirectory, errorCode);
        if (errorCode)
        {
            LT_ERROR("Could not create animation clip folder {}: {}", targetDirectory.string(), errorCode.message());
            return {};
        }

        const std::string baseName = preferredFileName.empty() ? std::string("New Animation Clip") : preferredFileName;
        std::string finalFileName = baseName;
        if (!finalFileName.ends_with(".animationclip.json"))
            finalFileName += ".animationclip.json";

        std::filesystem::path clipPath = targetDirectory / finalFileName;
        if (std::filesystem::exists(clipPath, errorCode))
        {
            for (int32_t index = 1; index < 1024; ++index)
            {
                const std::filesystem::path candidate = targetDirectory / ("New Animation Clip " + std::to_string(index) + ".animationclip.json");
                if (!std::filesystem::exists(candidate, errorCode))
                {
                    clipPath = candidate;
                    break;
                }
            }
        }

        nlohmann::json clipJson = {
            {"Version", 1},
            {"Name", clipPath.stem().string()},
            {"Loop", true},
            {"DurationSeconds", 1.0f},
            {"SamplesPerSecond", 30.0f},
            {"SpriteSubRectTrack", nlohmann::json::array()},
            {"SpriteTextureTrack", nlohmann::json::array()},
            {"PositionTrack", nlohmann::json::array()},
            {"ScaleTrack", nlohmann::json::array()},
            {"RotationZTrack", nlohmann::json::array()},
            {"EventTrack", nlohmann::json::array()}
        };

        {
            std::ofstream output(clipPath, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!output.is_open())
            {
                LT_ERROR("Could not create animation clip asset {}", clipPath.string());
                return {};
            }
            output << clipJson.dump(2);
        }

        std::filesystem::path relativeAssetPath = std::filesystem::relative(clipPath, assetsDirectory, errorCode);
        if (errorCode)
        {
            LT_ERROR("Could not compute animation clip asset key for {}", clipPath.string());
            return {};
        }

        const std::string assetKey = "Assets/" + relativeAssetPath.generic_string();
        const auto importResult = Assets::AssetDatabase::GetInstance().ImportOrUpdate(assetKey, Assets::AssetType::AnimationClip);
        if (importResult.IsFailure())
            LT_WARN("Created animation clip asset but failed to import into AssetDatabase ({}): {}", assetKey, importResult.GetError().GetErrorMessage());

        LT_INFO("Created animation clip asset {}", assetKey);
        m_SelectedAnimationClipAssetKey = assetKey;
        m_SelectedAnimatorControllerAssetKey.clear();
        m_SelectedTextureAssetKey.clear();
        m_CachedTextureAsset.reset();
        m_SelectedMaterialAssetKey.clear();
        m_CachedMaterialAsset.reset();
        m_SelectedNativeScriptAssetKey.clear();
        m_SelectedPrefabAssetKey.clear();
        m_SelectedTilesetAssetKey.clear();
        m_SelectedAudioMixerAssetKey.clear();
        m_SelectedInputActionsAssetKey.clear();
        m_SelectedEntity = entt::null;
        return assetKey;
    }

    std::string EditorLayer::CreateAnimatorControllerAssetInFolder(const std::filesystem::path& relativeFolderPath, const std::string& preferredFileName)
    {
        const auto rootResult = Assets::FindProjectRootFromWorkingDirectory();
        if (rootResult.IsFailure())
        {
            LT_ERROR("Could not create animator controller asset: {}", rootResult.GetError().GetErrorMessage());
            return {};
        }

        const std::filesystem::path assetsDirectory = rootResult.GetValue() / "Assets";
        const std::filesystem::path targetDirectory = assetsDirectory / relativeFolderPath;
        std::error_code errorCode;
        std::filesystem::create_directories(targetDirectory, errorCode);
        if (errorCode)
        {
            LT_ERROR("Could not create animator controller folder {}: {}", targetDirectory.string(), errorCode.message());
            return {};
        }

        const std::string baseName = preferredFileName.empty() ? std::string("New Animator Controller") : preferredFileName;
        std::string finalFileName = baseName;
        if (!finalFileName.ends_with(".animcontroller.json"))
            finalFileName += ".animcontroller.json";

        std::filesystem::path controllerPath = targetDirectory / finalFileName;
        if (std::filesystem::exists(controllerPath, errorCode))
        {
            for (int32_t index = 1; index < 1024; ++index)
            {
                const std::filesystem::path candidate = targetDirectory / ("New Animator Controller " + std::to_string(index) + ".animcontroller.json");
                if (!std::filesystem::exists(candidate, errorCode))
                {
                    controllerPath = candidate;
                    break;
                }
            }
        }

        nlohmann::json controllerJson = {
            {"Name", controllerPath.stem().string()},
            {"DefaultStateName", ""},
            {"Parameters", nlohmann::json::array()},
            {"States", nlohmann::json::array()}
        };

        {
            std::ofstream output(controllerPath, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!output.is_open())
            {
                LT_ERROR("Could not create animator controller asset {}", controllerPath.string());
                return {};
            }
            output << controllerJson.dump(2);
        }

        std::filesystem::path relativeAssetPath = std::filesystem::relative(controllerPath, assetsDirectory, errorCode);
        if (errorCode)
        {
            LT_ERROR("Could not compute animator controller asset key for {}", controllerPath.string());
            return {};
        }

        const std::string assetKey = "Assets/" + relativeAssetPath.generic_string();
        const auto importResult = Assets::AssetDatabase::GetInstance().ImportOrUpdate(assetKey, Assets::AssetType::AnimatorController);
        if (importResult.IsFailure())
            LT_WARN("Created animator controller asset but failed to import into AssetDatabase ({}): {}", assetKey, importResult.GetError().GetErrorMessage());

        LT_INFO("Created animator controller asset {}", assetKey);
        m_SelectedAnimatorControllerAssetKey = assetKey;
        m_SelectedAnimationClipAssetKey.clear();
        m_SelectedTextureAssetKey.clear();
        m_CachedTextureAsset.reset();
        m_SelectedMaterialAssetKey.clear();
        m_CachedMaterialAsset.reset();
        m_SelectedNativeScriptAssetKey.clear();
        m_SelectedPrefabAssetKey.clear();
        m_SelectedTilesetAssetKey.clear();
        m_SelectedAudioMixerAssetKey.clear();
        m_SelectedInputActionsAssetKey.clear();
        m_SelectedEntity = entt::null;
        return assetKey;
    }

    std::string EditorLayer::CreatePrefabAssetPathForEntity(entt::entity entity, const std::filesystem::path& relativeFolderPath) const
    {
        if (!m_Scene || !m_Scene->IsValid(entity))
            return {};

        std::string baseName = "New Prefab";
        if (const auto* tag = m_Scene->GetRegistry().try_get<TagComponent>(entity))
        {
            if (!tag->Tag.empty())
                baseName = tag->Tag;
        }
        for (char& character : baseName)
        {
            if (character == '/' || character == '\\' || character == ':' || character == '*'
                || character == '?' || character == '"' || character == '<' || character == '>'
                || character == '|')
            {
                character = '_';
            }
        }
        if (baseName.empty())
            baseName = "New Prefab";

        const auto rootResult = Assets::FindProjectRootFromWorkingDirectory();
        if (rootResult.IsFailure())
            return {};

        const std::filesystem::path targetFolder = relativeFolderPath.empty()
            ? std::filesystem::path("Prefabs")
            : relativeFolderPath;
        const std::filesystem::path prefabsFolder = rootResult.GetValue() / "Assets" / targetFolder;
        std::error_code errorCode;
        std::filesystem::create_directories(prefabsFolder, errorCode);
        if (errorCode)
            return {};

        std::filesystem::path candidatePath = prefabsFolder / (baseName + ".prefab.json");
        uint32_t suffix = 1;
        while (std::filesystem::exists(candidatePath, errorCode))
        {
            candidatePath = prefabsFolder / (baseName + " " + std::to_string(suffix) + ".prefab.json");
            ++suffix;
            errorCode.clear();
        }

        std::filesystem::path relativePath = std::filesystem::relative(candidatePath, rootResult.GetValue(), errorCode);
        if (errorCode || relativePath.empty())
            return {};
        return relativePath.generic_string();
    }

    bool EditorLayer::CreatePrefabFromEntity(entt::entity entity)
    {
        return CreatePrefabFromEntityInFolder(entity, std::filesystem::path("Prefabs"));
    }

    bool EditorLayer::CreatePrefabFromEntityInFolder(entt::entity entity, const std::filesystem::path& relativeFolderPath)
    {
        if (!m_Scene || !m_Scene->IsValid(entity))
            return false;

        const std::string prefabAssetKey = CreatePrefabAssetPathForEntity(entity, relativeFolderPath);
        if (prefabAssetKey.empty())
            return false;

        const bool success = m_EditorUndoService.ExecuteSceneMutation("Create Prefab", [&](Scene& mutableScene) {
            return EditorPrefabSystem::CreateOrUpdatePrefabFromEntity(mutableScene, entity, prefabAssetKey);
        });

        if (success)
            (void)Assets::AssetDatabase::GetInstance().ImportOrUpdate(prefabAssetKey, Assets::AssetType::Prefab);
        return success;
    }

    entt::entity EditorLayer::InstantiatePrefabAtParent(const std::string& prefabAssetKey, entt::entity parentEntity)
    {
        if (!m_Scene || prefabAssetKey.empty())
            return entt::null;

        entt::entity createdEntity = entt::null;
        const bool success = m_EditorUndoService.ExecuteSceneMutation("Instantiate Prefab", [&](Scene& mutableScene) {
            createdEntity = EditorPrefabSystem::InstantiatePrefab(mutableScene, prefabAssetKey, parentEntity);
            return createdEntity != entt::null;
        });
        if (!success)
            return entt::null;

        if (createdEntity != entt::null)
        {
            m_SelectedEntity = createdEntity;
            m_SelectedTextureAssetKey.clear();
            m_CachedTextureAsset.reset();
            m_SelectedMaterialAssetKey.clear();
            m_CachedMaterialAsset.reset();
            m_SelectedNativeScriptAssetKey.clear();
            m_SelectedPrefabAssetKey.clear();
            m_SelectedTilesetAssetKey.clear();
            m_SelectedAudioMixerAssetKey.clear();
            m_SelectedInputActionsAssetKey.clear();
            m_SelectedAnimationClipAssetKey.clear();
            m_SelectedAnimatorControllerAssetKey.clear();
        }
        return createdEntity;
    }

    entt::entity EditorLayer::InstantiatePrefabAtWorldPosition(const std::string& prefabAssetKey, const glm::vec3& worldPosition)
    {
        if (!m_Scene || prefabAssetKey.empty())
            return entt::null;

        entt::entity createdEntity = entt::null;
        const bool success = m_EditorUndoService.ExecuteSceneMutation("Instantiate Prefab", [&](Scene& mutableScene) {
            createdEntity = EditorPrefabSystem::InstantiatePrefab(mutableScene, prefabAssetKey, entt::null);
            if (createdEntity == entt::null || !mutableScene.IsValid(createdEntity))
                return false;

            if (auto* transform = mutableScene.GetRegistry().try_get<TransformComponent>(createdEntity))
                transform->Position = worldPosition;
            return true;
        });
        if (!success)
            return entt::null;

        m_SelectedEntity = createdEntity;
        m_SelectedTextureAssetKey.clear();
        m_CachedTextureAsset.reset();
        m_SelectedMaterialAssetKey.clear();
        m_CachedMaterialAsset.reset();
        m_SelectedNativeScriptAssetKey.clear();
        m_SelectedPrefabAssetKey.clear();
        m_SelectedTilesetAssetKey.clear();
        m_SelectedAudioMixerAssetKey.clear();
        m_SelectedInputActionsAssetKey.clear();
        m_SelectedAnimationClipAssetKey.clear();
        m_SelectedAnimatorControllerAssetKey.clear();
        return createdEntity;
    }

    bool EditorLayer::ApplyPrefabFromEntity(entt::entity entity)
    {
        if (!m_Scene || !m_Scene->IsValid(entity))
            return false;

        std::string prefabAssetKey;
        if (const auto* prefabInstance = m_Scene->GetRegistry().try_get<PrefabInstanceComponent>(entity))
            prefabAssetKey = prefabInstance->PrefabAssetKey;

        const bool success = EditorPrefabSystem::ApplyPrefabFromInstance(*m_Scene, entity);
        if (success && !prefabAssetKey.empty())
            (void)Assets::AssetDatabase::GetInstance().ImportOrUpdate(prefabAssetKey, Assets::AssetType::Prefab);
        return success;
    }

    entt::entity EditorLayer::RevertPrefabEntity(entt::entity entity)
    {
        if (!m_Scene || !m_Scene->IsValid(entity))
            return entt::null;

        entt::entity revertedEntity = entt::null;
        const bool success = m_EditorUndoService.ExecuteSceneMutation("Revert Prefab", [&](Scene& mutableScene) {
            revertedEntity = EditorPrefabSystem::RevertPrefabInstance(mutableScene, entity);
            return revertedEntity != entt::null;
        });
        if (!success)
            return entt::null;

        if (revertedEntity != entt::null)
            m_SelectedEntity = revertedEntity;
        return revertedEntity;
    }

    bool EditorLayer::UnpackPrefabEntity(entt::entity entity)
    {
        if (!m_Scene || !m_Scene->IsValid(entity))
            return false;

        return m_EditorUndoService.ExecuteSceneMutation("Unpack Prefab", [&](Scene& mutableScene) {
            return EditorPrefabSystem::UnpackPrefabInstance(mutableScene, entity);
        });
    }

    void EditorLayer::SetProjectDefaultSceneAssetKey(const std::string& sceneAssetKey)
    {
        if (sceneAssetKey.empty())
        {
            return;
        }

        auto& projectManager = Project::ProjectManager::GetInstance();
        if (!projectManager.HasOpenProject())
        {
            LT_WARN("Cannot set default scene: no project is currently open.");
            return;
        }

        const auto saveResult = projectManager.SetDefaultSceneAssetKey(sceneAssetKey);
        if (saveResult.IsFailure())
        {
            LT_ERROR("Failed to set default scene '{}': {}", sceneAssetKey, saveResult.GetError().GetErrorMessage());
            return;
        }

        LT_INFO("Default scene updated to '{}'", sceneAssetKey);
    }

}  // namespace Limitless
