#include "EditorLayer.h"
#include "EditorAssetNaming.h"
#include "Audio/AudioEngine.h"
#include "Assets/AssetDatabase.h"
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
        constexpr const char* kDefaultSceneFileName = "New Scene.scene.json";
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
                (playModeState == EditorPlayModeState::Play || playModeState == EditorPlayModeState::Pause);

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
        m_EditorUndoService.Clear();
        PersistProjectSessionState();
        ScriptCoreModuleRuntime::Shutdown();
        Application::GetInstance().GetWindow().SetFileDropCallback({});
        EditorBuildAndRunPanel::Shutdown(m_BuildAndRunPanelState);

        EditorRuntimeOperations::Detach(
            m_Scene,
            m_EditSceneStored,
            m_EditorCameraController,
            m_ViewportFramebuffer);
    }

    void EditorLayer::OnUpdate(float deltaTime)
    {
        ScriptCoreModuleRuntime::Update(m_PlayModeState);
        UpdateSceneAudioSources(m_Scene.get(), m_PlayModeState);

        if (m_PlayModeState == EditorPlayModeState::Play && m_Scene)
            m_Scene->Update(deltaTime);

        ProcessPendingSceneTransitions();

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
        EditorBuildAndRunPanel::Draw(m_ShowBuildAndRunWindow, m_BuildAndRunPanelState);
        DrawViewportPanel();
        DrawScenePanel();
        DrawInspectorPanel();
        DrawProjectPanel();
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
        EditorMenuBar::Draw(
            m_PlayModeState,
            m_ShowDemoWindow,
            m_ShowAssetDiagnosticsWindow,
            m_ShowBuildAndRunWindow,
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
            [this]() { ExitPlayMode(); },
            [this]() { TogglePausePlayMode(); });
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
            kAssetMaterialPayload,
            m_SelectedTextureAssetKey,
            m_CachedTextureAsset,
            m_SelectedMaterialAssetKey,
            m_CachedMaterialAsset,
            m_SelectedNativeScriptAssetKey);
    }

    void EditorLayer::DrawScenePanel()
    {
        std::string sceneRootDisplayName = SceneDisplayNameFromFileName(kDefaultSceneFileName);
        if (!m_CurrentSceneAssetKey.empty())
        {
            const std::filesystem::path sceneAssetPath(m_CurrentSceneAssetKey);
            sceneRootDisplayName = SceneDisplayNameFromFileName(sceneAssetPath.filename().string());
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
            [this](entt::entity entity, const std::filesystem::path& relativeFolderPath) {
                (void)CreatePrefabFromEntityInFolder(entity, relativeFolderPath);
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

                if (!m_Scene)
                {
                    // Even without an active scene instance, keep selected material state consistent.
                    return;
                }

                bool updatedAnyMaterialReference = false;
                bool updatedAnyAudioReference = false;
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
                if (isScriptRename)
                {
                    updateScriptReferencesInScene(m_Scene.get());
                    updateScriptReferencesInScene(m_EditSceneStored.get());
                }

                // Persist immediately so reopening the editor cannot revive stale material keys.
                if ((updatedAnyMaterialReference || updatedAnyAudioReference || updatedAnyNativeScriptPath) &&
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

        ImGui::TextWrapped("You have undo history for the current scene. Switching scenes will clear undo/redo history.");
        ImGui::Spacing();
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
        m_SelectedEntity = entt::null;
        m_SelectedTextureAssetKey.clear();
        m_SelectedNativeScriptAssetKey.clear();
        m_CachedTextureAsset.reset();
        m_CurrentSceneAssetKey = assetKey;
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
        m_SelectedEntity = entt::null;
        m_SelectedTextureAssetKey.clear();
        m_SelectedNativeScriptAssetKey.clear();
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

        const auto importResult = Assets::AssetDatabase::GetInstance().ImportOrUpdate(assetKey, Assets::AssetType::Scene);
        if (importResult.IsFailure())
        {
            LT_WARN("Scene saved but failed to import into AssetDatabase ({}): {}", assetKey, importResult.GetError().GetErrorMessage());
        }

        if (const auto& pm = Project::ProjectManager::GetInstance(); pm.HasOpenProject())
        {
            PersistProjectSessionState();
        }

        LT_INFO("Saved scene {}", assetKey);
        m_EditorUndoService.MarkSaved();
        return true;
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
                const std::filesystem::path candidate = targetDirectory / ("New Scene " + std::to_string(index) + ".scene.json");
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
            (void)Assets::AssetImportPipeline::ReimportChanged(true);
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
        return createdEntity;
    }

    bool EditorLayer::ApplyPrefabFromEntity(entt::entity entity)
    {
        if (!m_Scene || !m_Scene->IsValid(entity))
            return false;

        const bool success = EditorPrefabSystem::ApplyPrefabFromInstance(*m_Scene, entity);
        if (success)
            (void)Assets::AssetImportPipeline::ReimportChanged(true);
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
