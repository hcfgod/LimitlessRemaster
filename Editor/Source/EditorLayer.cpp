#include "EditorLayer.h"
#include "EditorAssetNaming.h"
#include "Audio/AudioEngine.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetManager.h"
#include "Assets/AudioClipAsset.h"
#include "Assets/AssetPaths.h"
#include "Assets/AssetImportPipeline.h"
#include "Assets/AssetTypes.h"
#include "Assets/TextureAsset.h"
#include "Core/Debug/Log.h"
#include "Editor/EditorCameraController.h"
#include "EditorInspectorPanel.h"
#include "EditorMenuBar.h"
#include "EditorPrefabSystem.h"
#include "EditorPlayMode.h"
#include "EditorProjectDialog.h"
#include "EditorProjectPanel.h"
#include "EditorRuntimeOperations.h"
#include "EditorScenePanel.h"
#include "EditorViewportPanel.h"
#include "Scripting/ScriptCoreModuleRuntime.h"
#include "Core/Input/InputSystem.h"
#include "Graphics/Camera/PerspectiveCamera3D.h"
#include "Graphics/Lighting2DRenderer.h"
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
#include <cstring>
#include <functional>
#include <limits>
#include <nlohmann/json.hpp>
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
        constexpr uint32_t kEditorSessionStateVersion = 2;
        constexpr std::string_view kSceneAssetSuffix = ".scene.json";

        struct EditorSessionStateData final
        {
            std::string LastOpenedSceneAssetKey;
            EditorInspectorPanel::NativeScriptEditorSessionState NativeScriptEditorState;
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

                if (version != kEditorSessionStateVersion)
                {
                    return {};
                }

                state.LastOpenedSceneAssetKey = root.value("lastOpenedSceneAssetKey", std::string{});
                state.NativeScriptEditorState.IsOpen = root.value("nativeScriptEditorIsOpen", false);
                state.NativeScriptEditorState.LastEditedScriptClassName = root.value("nativeScriptEditorLastClassName", std::string{});
                state.NativeScriptEditorState.LastEditedScriptAssetRelativePath = root.value("nativeScriptEditorLastAssetRelativePath", std::string{});
                state.NativeScriptEditorState.ShowDebugInfo = root.value("nativeScriptEditorShowDebugInfo", false);
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

        void UpdateSceneAudioSources(Scene* scene, EditorPlayModeState playModeState)
        {
            if (!scene)
                return;

            const bool runtimePlaybackAllowed =
                (playModeState == EditorPlayModeState::Play ||
                 playModeState == EditorPlayModeState::Simulate ||
                 playModeState == EditorPlayModeState::Pause);

            auto& registry = scene->GetRegistry();
            auto audioView = registry.view<AudioSourceComponent>();
            for (entt::entity entity : audioView)
            {
                auto& audioSource = audioView.get<AudioSourceComponent>(entity);
                if (audioSource.RuntimeVoiceId != 0 &&
                    !Audio::AudioEngine::GetInstance().IsVoiceActive(audioSource.RuntimeVoiceId))
                {
                    audioSource.RuntimeVoiceId = 0;
                }
                if (!runtimePlaybackAllowed)
                {
                    // In Edit mode, leave manual preview playback alone.
                    continue;
                }

                const bool shouldPlayOnStart =
                    audioSource.PlayOnStart &&
                    !audioSource.AudioClipKey.empty() &&
                    !audioSource.Muted;

                if (shouldPlayOnStart && !audioSource.RuntimePlaybackStarted)
                {
                    auto clipAsset = Assets::AudioClipAsset::LoadBlocking(audioSource.AudioClipKey);
                    if (clipAsset && clipAsset->GetClip())
                    {
                        audioSource.RuntimeVoiceId = Audio::AudioEngine::GetInstance().PlayClip(
                            clipAsset->GetClip(),
                            audioSource.Volume,
                            audioSource.Loop);
                        audioSource.RuntimePlaybackStarted = (audioSource.RuntimeVoiceId != 0);
                    }
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
            m_ViewportWidthPixels,
            m_ViewportHeightPixels,
            m_Scene,
            m_CameraManager,
            m_EditorCameraId,
            m_EditorCameraController,
            m_ViewportFramebuffer);

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
            m_ViewportFramebuffer);
        ScriptCoreModuleRuntime::Shutdown();
    }

    void EditorLayer::OnUpdate(float deltaTime)
    {
        ScriptCoreModuleRuntime::Update(m_PlayModeState);
        UpdateSceneAudioSources(m_Scene.get(), m_PlayModeState);

        if (m_ProjectSettingsPanelState.Loaded)
        {
            m_ProjectPhysics2DSettings = m_ProjectSettingsPanelState.Physics2D;
            m_ProjectPhysics2DSettingsLoaded = true;
            ApplyProjectPhysics2DSettingsToScenes();

            m_ProjectLighting2DSettings = m_ProjectSettingsPanelState.Lighting2D;
            m_ProjectLighting2DSettingsLoaded = true;
            ApplyProjectLighting2DSettings();
        }

        if (m_PlayModeState == EditorPlayModeState::Play && m_Scene)
            m_Scene->Update(deltaTime);

        ProcessPendingSceneTransitions();
        PumpSceneAssetPrewarm();

        EditorPlayMode::SyncSceneCamera(
            m_PlayModeState,
            m_Scene.get(),
            m_CameraManager,
            m_CachedGameplayCameraId,
            m_CreatedGameplayCameraFromScene);

        const ImGuiIO& io = ImGui::GetIO();
        EditorRuntimeOperations::Update(
            m_PlayModeState,
            m_ViewportHovered,
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

        if (m_PlayModeState == EditorPlayModeState::Play || m_PlayModeState == EditorPlayModeState::Simulate)
        {
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

            // Keep AssetDatabase key mappings canonical before any scene is loaded.
            // This repairs stale key mappings after file rename/move operations.
            const auto reimportResult = Assets::AssetImportPipeline::ReimportAll(true);
            if (reimportResult.IsFailure())
            {
                LT_WARN("Asset reimport on project open failed: {}", reimportResult.GetError().GetErrorMessage());
            }

            // Reset per-project panel cache so settings always match the newly opened project.
            m_ProjectSettingsPanelState.Loaded = false;
            m_ProjectSettingsPanelState.StatusMessage.clear();
            m_ProjectSettingsPanelState.StatusIsError = false;

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
            RefreshProjectLighting2DSettings();

            bool loadedScene = false;
            const EditorSessionStateData sessionState = ReadProjectSessionState(projectRoot);
            EditorInspectorPanel::ApplyNativeScriptEditorSessionState(sessionState.NativeScriptEditorState);
            const std::string lastOpenedSceneAssetKey = sessionState.LastOpenedSceneAssetKey;
            if (!lastOpenedSceneAssetKey.empty())
            {
                loadedScene = LoadSceneFromAssetKey(lastOpenedSceneAssetKey);
                if (!loadedScene && (m_RequestOpenSceneSwitchConfirmationPopup || m_SceneSwitchConfirmationPopupOpen))
                    loadedScene = true;
            }

            if (!loadedScene)
            {
                if (const auto definition = pm.GetProjectDefinition(); definition.has_value() && !definition->DefaultScene.Key.empty())
                {
                    loadedScene = LoadSceneFromAssetKey(definition->DefaultScene.Key);
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
                    (void)LoadSceneFromAssetKey(defaultSceneAssetKey);
                }
            }
        }

        DrawMenuBar();
        EditorProjectSettingsPanel::Draw(m_ShowProjectSettingsWindow, m_ProjectSettingsPanelState);
        EditorAssetDiagnosticsPanel::Draw(m_ShowAssetDiagnosticsWindow);
        DrawViewportPanel();
        DrawScenePanel();
        DrawInspectorPanel();
        DrawTilemapPanel();
        DrawProjectPanel();
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
            m_ViewportWidthPixels,
            m_ViewportHeightPixels,
            m_ViewportFramebuffer,
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
            [this]() { EditorProjectDialog::RequestOpen(m_ProjectDialogState, EditorProjectDialog::ProjectDialogMode::Open); },
            [this]() { EditorProjectDialog::RequestOpen(m_ProjectDialogState, EditorProjectDialog::ProjectDialogMode::Create); },
            [this]() { m_ShowProjectSettingsWindow = true; },
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
            [this]() { (void)ApplyPrefabStageChangesToInstances(); });
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
            Log::ClearRecentMessages();
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
            for (const LogMessageEntry* entry : visibleEntries)
            {
                if (!entry)
                    continue;

                const bool isWarning = entry->Level == spdlog::level::warn;
                const bool isError = entry->Level >= spdlog::level::err;

                const ImVec4 textColor = isError
                    ? ImVec4(1.0f, 0.38f, 0.38f, 1.0f)
                    : (isWarning ? ImVec4(1.0f, 0.85f, 0.35f, 1.0f) : ImGui::GetStyleColorVec4(ImGuiCol_Text));

                ImGui::PushStyleColor(ImGuiCol_Text, textColor);
                ImGui::TextWrapped("[%s] %s", entry->LoggerName.c_str(), entry->Message.c_str());
                ImGui::PopStyleColor();
            }

            if (shouldScrollToBottom)
                ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();

        ImGui::End();
    }

    void EditorLayer::DrawViewportPanel()
    {
        EditorViewportPanel::Draw(
            m_ViewportWidthPixels,
            m_ViewportHeightPixels,
            m_ViewportFramebuffer,
            m_ViewportFocused,
            m_ViewportHovered,
            m_EditorCameraController.get(),
            m_CameraManager,
            m_Scene.get(),
            m_PlayModeState,
            m_PlayModeMissingGameplayCamera,
            [this](uint32_t width, uint32_t height) { EnsureViewportFramebuffer(width, height); },
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
            &m_TilemapEditorState);
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

                if (!m_Scene)
                {
                    // Even without an active scene instance, keep selected material state consistent.
                    return;
                }

                bool updatedAnyMaterialReference = false;
                bool updatedAnyAudioReference = false;
                bool updatedAnyTilesetReference = false;
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

                const auto updateTilesetReferencesInScene = [&oldAssetKey, &newAssetKey, &updatedAnyTilesetReference](Scene* scene) {
                    if (!scene)
                        return;

                    auto& registry = scene->GetRegistry();
                    auto tilemapView = registry.view<TilemapComponent>();
                    for (entt::entity entity : tilemapView)
                    {
                        auto& tilemap = tilemapView.get<TilemapComponent>(entity);
                        if (tilemap.TilesetAssetKey == oldAssetKey)
                        {
                            tilemap.TilesetAssetKey = newAssetKey;
                            tilemap.TilesetAssetLoadAttempted = false;
                            tilemap.TilesetTextureLoadAttempted = false;
                            tilemap.CachedTilesetTexture.reset();
                            updatedAnyTilesetReference = true;
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
                updateTilesetReferencesInScene(m_Scene.get());
                updateTilesetReferencesInScene(m_EditSceneStored.get());
                if (isScriptRename)
                {
                    updateScriptReferencesInScene(m_Scene.get());
                    updateScriptReferencesInScene(m_EditSceneStored.get());
                }

                // Persist immediately so reopening the editor cannot revive stale material keys.
                if ((updatedAnyMaterialReference || updatedAnyAudioReference || updatedAnyTilesetReference || updatedAnyNativeScriptPath) &&
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

    void EditorLayer::DrawTilemapPanel()
    {
        if (!m_ShowTilemapPanel)
            return;

        if (!ImGui::Begin("Tilemap", &m_ShowTilemapPanel))
        {
            ImGui::End();
            return;
        }

        if (!m_Scene || m_SelectedEntity == entt::null || !m_Scene->IsValid(m_SelectedEntity))
        {
            ImGui::TextDisabled("Select a tilemap entity to edit.");
            ImGui::End();
            return;
        }

        auto& registry = m_Scene->GetRegistry();
        auto* tilemap = registry.try_get<TilemapComponent>(m_SelectedEntity);
        if (!tilemap)
        {
            ImGui::TextDisabled("Selected entity has no Tilemap component.");
            ImGui::End();
            return;
        }
        tilemap->EnsureLayerStorage();

        m_TilemapEditorState.ActiveLayerIndex = std::clamp(
            m_TilemapEditorState.ActiveLayerIndex,
            0,
            static_cast<int32_t>(tilemap->Layers.empty() ? 0 : tilemap->Layers.size() - 1));
        m_TilemapEditorState.BrushSize = std::max(1, m_TilemapEditorState.BrushSize);

        ImGui::Checkbox("Enable Painting", &m_TilemapEditorState.Enabled);
        ImGui::Checkbox("Show Grid Overlay", &m_TilemapEditorState.ShowGridOverlay);
        ImGui::Checkbox("Snap To Grid", &m_TilemapEditorState.SnapToGrid);

        int paintModeIndex = static_cast<int>(m_TilemapEditorState.PaintMode);
        const char* paintModeNames[] = { "Single", "Rectangle", "Fill", "Erase" };
        if (ImGui::Combo("Paint Mode", &paintModeIndex, paintModeNames, 4))
            m_TilemapEditorState.PaintMode = static_cast<EditorViewportPanel::TilemapPaintMode>(paintModeIndex);

        ImGui::DragInt("Brush Size", &m_TilemapEditorState.BrushSize, 1.0f, 1, 128);
        m_TilemapEditorState.BrushSize = std::max(1, m_TilemapEditorState.BrushSize);

        int activeTileId = static_cast<int>(m_TilemapEditorState.ActiveTileId);
        ImGui::DragInt("Active Tile ID", &activeTileId, 1.0f, 1, std::numeric_limits<int>::max());
        if (activeTileId < 1)
            activeTileId = 1;
        m_TilemapEditorState.ActiveTileId = static_cast<uint32_t>(activeTileId);

        ImGui::Checkbox("Paint Custom Data", &m_TilemapEditorState.PaintCustomData);
        if (m_TilemapEditorState.PaintCustomData)
            ImGui::InputScalar("Custom Data Value", ImGuiDataType_U32, &m_TilemapEditorState.ActiveCustomData);

        if (!tilemap->Layers.empty())
        {
            const char* activeLayerName = tilemap->Layers[static_cast<size_t>(m_TilemapEditorState.ActiveLayerIndex)].Name.c_str();
            if (ImGui::BeginCombo("Active Layer", activeLayerName))
            {
                for (size_t layerIndex = 0; layerIndex < tilemap->Layers.size(); ++layerIndex)
                {
                    const bool selected = static_cast<int32_t>(layerIndex) == m_TilemapEditorState.ActiveLayerIndex;
                    if (ImGui::Selectable(tilemap->Layers[layerIndex].Name.c_str(), selected))
                        m_TilemapEditorState.ActiveLayerIndex = static_cast<int32_t>(layerIndex);
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }

        if (m_TilemapEditorState.HasHoveredCell)
        {
            ImGui::Text("Hovered Cell: (%d, %d)", m_TilemapEditorState.HoveredCell.x, m_TilemapEditorState.HoveredCell.y);
        }
        else
        {
            ImGui::TextDisabled("Hovered Cell: (none)");
        }

        if (!tilemap->TilesetTextureKey.empty())
        {
            if (!tilemap->CachedTilesetTexture && !tilemap->TilesetTextureLoadAttempted)
            {
                tilemap->CachedTilesetTexture = std::dynamic_pointer_cast<Assets::TextureAsset>(
                    Assets::AssetManager::GetCachedByKey(tilemap->TilesetTextureKey));
                if (!tilemap->CachedTilesetTexture)
                    (void)Assets::TextureAsset::LoadAsync(tilemap->TilesetTextureKey);
                tilemap->TilesetTextureLoadAttempted = true;
            }
            else if (!tilemap->CachedTilesetTexture && tilemap->TilesetTextureLoadAttempted)
            {
                tilemap->CachedTilesetTexture = std::dynamic_pointer_cast<Assets::TextureAsset>(
                    Assets::AssetManager::GetCachedByKey(tilemap->TilesetTextureKey));
            }
        }

        if (!tilemap->CachedTilesetTexture || !tilemap->CachedTilesetTexture->GetTexture())
        {
            ImGui::TextDisabled("Assign a tileset texture in the Inspector to use palette painting.");
            ImGui::End();
            return;
        }

        const uint32_t textureWidth = tilemap->CachedTilesetTexture->GetTexture()->GetWidth();
        const uint32_t textureHeight = tilemap->CachedTilesetTexture->GetTexture()->GetHeight();
        const int32_t tileWidth = std::max(1, tilemap->TilesetTileSizePixels.x);
        const int32_t tileHeight = std::max(1, tilemap->TilesetTileSizePixels.y);
        const int32_t columns = static_cast<int32_t>(textureWidth) / tileWidth;
        const int32_t rows = static_cast<int32_t>(textureHeight) / tileHeight;
        if (columns <= 0 || rows <= 0)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Tileset tile size is larger than texture dimensions.");
            ImGui::End();
            return;
        }

        const int32_t maxTileCount = columns * rows;
        const int32_t paletteColumnCount = std::max(1, std::min(columns, 12));
        const ImTextureID textureId = (ImTextureID)(void*)(uintptr_t)tilemap->CachedTilesetTexture->GetTexture()->GetRendererID();
        constexpr float tileButtonSize = 30.0f;

        ImGui::Separator();
        ImGui::TextDisabled("Tile Palette");
        for (int32_t tileIndex = 0; tileIndex < maxTileCount; ++tileIndex)
        {
            const int32_t tileId = tileIndex + 1;
            const bool isTileCurrentlySelected = (tileId == static_cast<int32_t>(m_TilemapEditorState.ActiveTileId));
            const int32_t tileX = tileIndex % columns;
            const int32_t tileY = tileIndex / columns;
            const float u0 = static_cast<float>(tileX * tileWidth) / static_cast<float>(textureWidth);
            const float v0 = static_cast<float>(tileY * tileHeight) / static_cast<float>(textureHeight);
            const float u1 = static_cast<float>((tileX + 1) * tileWidth) / static_cast<float>(textureWidth);
            const float v1 = static_cast<float>((tileY + 1) * tileHeight) / static_cast<float>(textureHeight);

            ImGui::PushID(tileId);
            if (isTileCurrentlySelected)
                ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(85, 200, 255, 255));
            if (ImGui::ImageButton("##TilePaletteButton", textureId, ImVec2(tileButtonSize, tileButtonSize), ImVec2(u0, v0), ImVec2(u1, v1)))
                m_TilemapEditorState.ActiveTileId = static_cast<uint32_t>(tileId);
            if (isTileCurrentlySelected)
                ImGui::PopStyleColor();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Tile %d", tileId);
            ImGui::PopID();

            if ((tileIndex + 1) % paletteColumnCount != 0)
                ImGui::SameLine();
        }

        ImGui::End();
    }

    void EditorLayer::EnsureViewportFramebuffer(uint32_t width, uint32_t height)
    {
        EditorRuntimeOperations::EnsureViewportFramebuffer(
            width,
            height,
            m_ViewportFramebuffer,
            m_ViewportWidthPixels,
            m_ViewportHeightPixels,
            m_EditorCameraController.get());
    }

    void EditorLayer::EnterPlayMode()
    {
        if (m_PlayModeState != EditorPlayModeState::Edit)
            return;

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
            m_ViewportWidthPixels,
            m_ViewportHeightPixels,
            m_CachedGameplayCameraId,
            m_CreatedGameplayCameraFromScene,
            m_PlayModeMissingGameplayCamera,
            m_SelectedEntity,
            m_SelectedTextureAssetKey,
            m_CachedTextureAsset);
    }

    void EditorLayer::EnterSimulateMode()
    {
        if (m_PlayModeState != EditorPlayModeState::Edit)
            return;

        StopAudioSourcesInScene(m_Scene.get());
        StopAudioSourcesInScene(m_EditSceneStored.get());
        m_EditSceneStoredAssetKey = m_CurrentSceneAssetKey;

        EditorPlayMode::EnterSimulate(
            m_PlayModeState,
            m_Scene,
            m_EditSceneStored,
            m_CameraManager,
            m_EditorCameraId,
            m_ViewportWidthPixels,
            m_ViewportHeightPixels,
            m_CachedGameplayCameraId,
            m_CreatedGameplayCameraFromScene,
            m_PlayModeMissingGameplayCamera,
            m_SelectedEntity,
            m_SelectedTextureAssetKey,
            m_CachedTextureAsset);
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
        m_EditorUndoService.Clear();
        m_EditorUndoService.MarkSaved();
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
        ApplyProjectPhysics2DSettingsToScenes();
        QueueSceneAssetPrewarm();
        m_SelectedEntity = entt::null;
        m_SelectedTextureAssetKey.clear();
        m_SelectedNativeScriptAssetKey.clear();
        m_SelectedPrefabAssetKey.clear();
        m_SelectedTilesetAssetKey.clear();
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
        ApplyProjectPhysics2DSettingsToScenes();
        QueueSceneAssetPrewarm();
        m_SelectedEntity = entt::null;
        m_SelectedTextureAssetKey.clear();
        m_SelectedNativeScriptAssetKey.clear();
        m_SelectedPrefabAssetKey.clear();
        m_SelectedTilesetAssetKey.clear();
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
        runtimeSettings.MaxShadowSamplesPerLight = std::max(1, m_ProjectLighting2DSettings.MaxShadowSamplesPerLight);
        Lighting2DRenderer::SetSettings(runtimeSettings);
    }

    void EditorLayer::QueueSceneAssetPrewarm()
    {
        if (!m_Scene)
            return;

        auto& registry = m_Scene->GetRegistry();

        auto spriteView = registry.view<SpriteComponent>();
        for (entt::entity entity : spriteView)
        {
            const auto& sprite = spriteView.get<SpriteComponent>(entity);
            if (sprite.TextureKey.empty())
                continue;
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
            if (m_PrewarmedMaterialAssets.contains(material.MaterialKey) || m_PendingMaterialPrewarmTasks.contains(material.MaterialKey))
                continue;

            m_PendingMaterialPrewarmTasks.emplace(material.MaterialKey, Assets::MaterialAsset::LoadAsync(material.MaterialKey));
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
