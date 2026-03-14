#include "EditorLayer.h"
#include "EditorAssetNaming.h"
#include "Audio/AudioEngine.h"
#include "Audio/AudioMixerAsset.h"
#include "Audio/SceneAudioSystem.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetManager.h"
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
#include "EditorPanelStyle.h"
#include "EditorPrefabSystem.h"
#include "EditorPlayMode.h"
#include "EditorProjectDialog.h"
#include "EditorProjectPanel.h"
#include "EditorRuntimeOperations.h"
#include "EditorScenePanel.h"
#include "EditorViewportPanel.h"
#include "Scene/ParticleEmitterSystem.h"
#include "Scene/SceneRenderer.h"
#include "Scripting/ScriptCoreModuleRuntime.h"
#include "Core/Input/InputSystem.h"
#include "Core/PerformanceMonitor.h"
#include "Graphics/Camera/Camera.h"
#include "Graphics/Camera/OrthographicCamera2D.h"
#include "Graphics/Camera/PerspectiveCamera3D.h"
#include "Graphics/Lighting2DRenderer.h"
#include "Graphics/Renderer2D.h"
#include "ImGui/ImGuiLayer.h"
#include "Project/BuildSettings.h"
#include "Project/ProjectManager.h"
#include "Physics/Physics2DQueries.h"
#include "Project/ProjectSettings.h"
#include "Scene/Components/CoreComponents.h"
#include "Scene/Components/RenderingComponents.h"
#include "Scene/Components/ScriptingComponents.h"
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
#include <memory>
#include <nlohmann/json.hpp>
#include <sstream>
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
        constexpr const char* kDefaultSceneFileName = "SampleScene.scene.json";
        constexpr const char* kSceneFileSuffix = ".scene.json";
        constexpr const char* kEditorSessionStateRelativePath = "Project/Settings/EditorSessionState.json";
        constexpr uint32_t kEditorSessionStateVersion = 11;
        constexpr double kEntityFocusShortcutDoublePressWindowSeconds = 0.35;
        constexpr std::string_view kSceneAssetSuffix = ".scene.json";

        struct EditorSessionStateData final
        {
            bool HasPersistedState = false;
            std::string LastOpenedSceneAssetKey;
            std::string ActiveLayoutName = Editor::EditorLayoutManager::GetDefaultLayoutName();
            EditorInspectorPanel::NativeScriptEditorSessionState NativeScriptEditorState;
            Editor::EditorLayoutWindowState LayoutWindowState = Editor::EditorLayoutManager::CreateDefaultWindowState();
            bool ShowProjectSettingsWindow = false;
            bool ShowAssetDiagnosticsWindow = false;
            bool ShowPerformancePanel = false;
            bool ShowConsoleWindow = true;
            bool ProjectAssetsRootExpanded = true;
            std::string ProjectActiveFolderRelativePath;
            float ProjectGridScale = 1.0f;
            std::unordered_map<std::string, bool> ProjectFolderExpansionState;
            std::unordered_map<std::string, bool> InspectorFoldoutState;
            std::unordered_map<std::string, std::vector<std::string>> InspectorSectionOrderState;
            int AdditionalInspectorCount = 0;
            int AdditionalProjectPanelCount = 0;
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

        float EstimateEditorCameraFocusDistance(const glm::mat4& worldTransform, float distanceMultiplier)
        {
            const float worldScaleX = glm::length(glm::vec3(worldTransform[0]));
            const float worldScaleY = glm::length(glm::vec3(worldTransform[1]));
            const float worldScaleZ = glm::length(glm::vec3(worldTransform[2]));
            const float estimatedRadius = std::max({ 0.5f, worldScaleX, worldScaleY, worldScaleZ });
            return std::clamp(estimatedRadius * distanceMultiplier, 0.75f, 150.0f);
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
                state.HasPersistedState = true;
                const uint32_t version = root.value("version", 0u);
                if (version == 1)
                {
                    state.LastOpenedSceneAssetKey = root.value("lastOpenedSceneAssetKey", std::string{});
                    state.LayoutWindowState.ShowProjectSettingsWindow = state.ShowProjectSettingsWindow;
                    state.LayoutWindowState.ShowAssetDiagnosticsWindow = state.ShowAssetDiagnosticsWindow;
                    state.LayoutWindowState.ShowPerformancePanel = state.ShowPerformancePanel;
                    state.LayoutWindowState.ShowConsoleWindow = state.ShowConsoleWindow;
                    return state;
                }

                if (version != 3 && version != 4 && version != 5 && version != 6 && version != 7 && version != 8 && version != 9 &&
                    version != 10 && version != kEditorSessionStateVersion)
                {
                    return {};
                }

                state.LastOpenedSceneAssetKey = root.value("lastOpenedSceneAssetKey", std::string{});
                if (version >= 8)
                {
                    state.ActiveLayoutName = Editor::EditorLayoutManager::NormalizeLayoutName(
                        root.value("activeLayoutName", Editor::EditorLayoutManager::GetDefaultLayoutName()));
                    state.LayoutWindowState.ShowScenePanel = root.value("showScenePanel", state.LayoutWindowState.ShowScenePanel);
                    state.LayoutWindowState.ShowInspectorPanel = root.value("showInspectorPanel", state.LayoutWindowState.ShowInspectorPanel);
                    state.LayoutWindowState.ShowProjectPanel = root.value("showProjectPanel", state.LayoutWindowState.ShowProjectPanel);
                    state.LayoutWindowState.ShowSceneView = root.value("showSceneView", state.LayoutWindowState.ShowSceneView);
                    state.LayoutWindowState.ShowGameView = root.value("showGameView", state.LayoutWindowState.ShowGameView);
                    state.LayoutWindowState.ShowEditorPreferencesWindow =
                        root.value("showEditorPreferencesWindow", state.LayoutWindowState.ShowEditorPreferencesWindow);
                    state.LayoutWindowState.ShowProjectSettingsWindow = root.value("showProjectSettingsWindow", state.LayoutWindowState.ShowProjectSettingsWindow);
                    state.LayoutWindowState.ShowBuildSettingsWindow = root.value("showBuildSettingsWindow", state.LayoutWindowState.ShowBuildSettingsWindow);
                    state.LayoutWindowState.ShowAssetDiagnosticsWindow = root.value("showAssetDiagnosticsWindow", state.LayoutWindowState.ShowAssetDiagnosticsWindow);
                    state.LayoutWindowState.ShowPhysicsDiagnosticsWindow = root.value("showPhysicsDiagnosticsWindow", state.LayoutWindowState.ShowPhysicsDiagnosticsWindow);
                    state.LayoutWindowState.ShowConsoleWindow = root.value("showConsoleWindow", state.LayoutWindowState.ShowConsoleWindow);
                    state.LayoutWindowState.ShowEditorFpsOverlay = root.value("showEditorFpsOverlay", state.LayoutWindowState.ShowEditorFpsOverlay);
                    state.LayoutWindowState.ShowGizmoToolbar = root.value("showGizmoToolbar", state.LayoutWindowState.ShowGizmoToolbar);
                    state.LayoutWindowState.ShowPerformancePanel = root.value("showPerformancePanel", state.LayoutWindowState.ShowPerformancePanel);
                    state.LayoutWindowState.ShowAnimationTimelinePanel = root.value("showAnimationTimelinePanel", state.LayoutWindowState.ShowAnimationTimelinePanel);
                    state.LayoutWindowState.ShowAnimatorGraphPanel = root.value("showAnimatorGraphPanel", state.LayoutWindowState.ShowAnimatorGraphPanel);
                    state.LayoutWindowState.ShowTilePalettePanel = root.value("showTilePalettePanel", state.LayoutWindowState.ShowTilePalettePanel);
                    state.LayoutWindowState.ShowSpriteEditorWindow = root.value("showSpriteEditorWindow", state.LayoutWindowState.ShowSpriteEditorWindow);
                    state.LayoutWindowState.ShowDemoWindow = root.value("showDemoWindow", state.LayoutWindowState.ShowDemoWindow);
                }
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
                if (version >= 6)
                    state.ProjectActiveFolderRelativePath = root.value("projectActiveFolderRelativePath", std::string{});
                if (version >= 7)
                    state.ProjectGridScale = root.value("projectGridScale", 1.0f);
                if (version >= 9)
                {
                    if (const auto inspectorFoldoutStateIt = root.find("inspectorFoldoutState");
                        inspectorFoldoutStateIt != root.end() && inspectorFoldoutStateIt->is_object())
                    {
                        for (auto stateIt = inspectorFoldoutStateIt->begin(); stateIt != inspectorFoldoutStateIt->end(); ++stateIt)
                        {
                            if (!stateIt.value().is_boolean())
                                continue;
                            state.InspectorFoldoutState[stateIt.key()] = stateIt.value().get<bool>();
                        }
                    }
                }
                if (version >= 10)
                {
                    if (const auto inspectorSectionOrderIt = root.find("inspectorSectionOrderState");
                        inspectorSectionOrderIt != root.end() && inspectorSectionOrderIt->is_object())
                    {
                        for (auto stateIt = inspectorSectionOrderIt->begin(); stateIt != inspectorSectionOrderIt->end(); ++stateIt)
                        {
                            if (!stateIt.value().is_array())
                                continue;

                            std::vector<std::string> orderedSections;
                            for (const auto& sectionValue : stateIt.value())
                            {
                                if (!sectionValue.is_string())
                                    continue;
                                orderedSections.push_back(sectionValue.get<std::string>());
                            }

                            if (!orderedSections.empty())
                                state.InspectorSectionOrderState[stateIt.key()] = std::move(orderedSections);
                        }
                    }
                }
                if (version >= 11)
                {
                    state.AdditionalInspectorCount = root.value("additionalInspectorCount", 0);
                    state.AdditionalProjectPanelCount = root.value("additionalProjectPanelCount", 0);
                }
                if (version < 8)
                {
                    state.LayoutWindowState.ShowProjectSettingsWindow = state.ShowProjectSettingsWindow;
                    state.LayoutWindowState.ShowAssetDiagnosticsWindow = state.ShowAssetDiagnosticsWindow;
                    state.LayoutWindowState.ShowPerformancePanel = state.ShowPerformancePanel;
                    state.LayoutWindowState.ShowConsoleWindow = state.ShowConsoleWindow;
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
                root["activeLayoutName"] = state.ActiveLayoutName;
                root["nativeScriptEditorIsOpen"] = state.NativeScriptEditorState.IsOpen;
                root["nativeScriptEditorLastClassName"] = state.NativeScriptEditorState.LastEditedScriptClassName;
                root["nativeScriptEditorLastAssetRelativePath"] = state.NativeScriptEditorState.LastEditedScriptAssetRelativePath;
                root["nativeScriptEditorShowDebugInfo"] = state.NativeScriptEditorState.ShowDebugInfo;
                root["showScenePanel"] = state.LayoutWindowState.ShowScenePanel;
                root["showInspectorPanel"] = state.LayoutWindowState.ShowInspectorPanel;
                root["showProjectPanel"] = state.LayoutWindowState.ShowProjectPanel;
                root["showSceneView"] = state.LayoutWindowState.ShowSceneView;
                root["showGameView"] = state.LayoutWindowState.ShowGameView;
                root["showEditorPreferencesWindow"] = state.LayoutWindowState.ShowEditorPreferencesWindow;
                root["showProjectSettingsWindow"] = state.ShowProjectSettingsWindow;
                root["showBuildSettingsWindow"] = state.LayoutWindowState.ShowBuildSettingsWindow;
                root["showAssetDiagnosticsWindow"] = state.ShowAssetDiagnosticsWindow;
                root["showPhysicsDiagnosticsWindow"] = state.LayoutWindowState.ShowPhysicsDiagnosticsWindow;
                root["showPerformancePanel"] = state.ShowPerformancePanel;
                root["showConsoleWindow"] = state.ShowConsoleWindow;
                root["showEditorFpsOverlay"] = state.LayoutWindowState.ShowEditorFpsOverlay;
                root["showGizmoToolbar"] = state.LayoutWindowState.ShowGizmoToolbar;
                root["showAnimationTimelinePanel"] = state.LayoutWindowState.ShowAnimationTimelinePanel;
                root["showAnimatorGraphPanel"] = state.LayoutWindowState.ShowAnimatorGraphPanel;
                root["showTilePalettePanel"] = state.LayoutWindowState.ShowTilePalettePanel;
                root["showSpriteEditorWindow"] = state.LayoutWindowState.ShowSpriteEditorWindow;
                root["showDemoWindow"] = state.LayoutWindowState.ShowDemoWindow;
                root["projectAssetsRootExpanded"] = state.ProjectAssetsRootExpanded;
                root["projectActiveFolderRelativePath"] = state.ProjectActiveFolderRelativePath;
                root["projectGridScale"] = state.ProjectGridScale;

                nlohmann::json folderExpansionRoot = nlohmann::json::object();
                for (const auto& [folderPath, expanded] : state.ProjectFolderExpansionState)
                    folderExpansionRoot[folderPath] = expanded;
                root["projectFolderExpansionState"] = std::move(folderExpansionRoot);

                nlohmann::json inspectorFoldoutRoot = nlohmann::json::object();
                for (const auto& [foldoutKey, expanded] : state.InspectorFoldoutState)
                    inspectorFoldoutRoot[foldoutKey] = expanded;
                root["inspectorFoldoutState"] = std::move(inspectorFoldoutRoot);

                nlohmann::json inspectorSectionOrderRoot = nlohmann::json::object();
                for (const auto& [contextKey, orderedSections] : state.InspectorSectionOrderState)
                    inspectorSectionOrderRoot[contextKey] = orderedSections;
                root["inspectorSectionOrderState"] = std::move(inspectorSectionOrderRoot);

                root["additionalInspectorCount"] = state.AdditionalInspectorCount;
                root["additionalProjectPanelCount"] = state.AdditionalProjectPanelCount;

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

    }  // anonymous namespace

    EditorLayer::EditorLayer()
        : Layer("EditorLayer")
        , m_Scene(m_SceneCollection, m_SceneHandle)
        , m_EditSceneStored(m_SceneCollection, m_EditSceneStoredHandle)
    {
        const std::string defaultName = SceneDisplayNameFromFileName(kDefaultSceneFileName);
        const size_t copyCount = std::min(defaultName.size(), m_SaveSceneFileNameBuffer.size() - 1);
        std::copy_n(defaultName.c_str(), copyCount, m_SaveSceneFileNameBuffer.begin());
        m_SaveSceneFileNameBuffer[copyCount] = '\0';
    }

    EditorLayer::~EditorLayer() = default;

    void EditorLayer::OnAttach()
    {
        Editor::EditorPreferences::GetInstance().EnsureLoaded();
        SceneManager::ClearPendingSceneTransition();
        Physics2DQueries::SetActiveSceneCollectionForScriptQueries(&m_SceneCollection, ToSceneRoleMask(SceneRole::ScriptQueryTarget));
        m_EditorUndoService.Initialize(
            [this]() { return m_Scene.get(); },
            [this](std::unique_ptr<Scene> scene) { m_Scene = std::move(scene); },
            [this](std::unique_ptr<Scene>& snapshot) {
                if (!snapshot)
                    return false;
                m_Scene.swap(snapshot);
                return static_cast<bool>(m_Scene);
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
        Physics2DQueries::SetActiveSceneForScriptQueries(nullptr);
        Physics2DQueries::SetActiveSceneCollectionForScriptQueries(nullptr);

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
                const auto scripting = Project::SaveScriptingSettings(projectRoot, m_ProjectSettingsPanelState.Scripting);

                if (sr.IsFailure()) LT_WARN("Editor exit: failed to save render settings: {}", sr.GetError().GetErrorMessage());
                if (sa.IsFailure()) LT_WARN("Editor exit: failed to save audio settings: {}", sa.GetError().GetErrorMessage());
                if (si.IsFailure()) LT_WARN("Editor exit: failed to save input settings: {}", si.GetError().GetErrorMessage());
                if (sl.IsFailure()) LT_WARN("Editor exit: failed to save layers settings: {}", sl.GetError().GetErrorMessage());
                if (sp.IsFailure()) LT_WARN("Editor exit: failed to save physics settings: {}", sp.GetError().GetErrorMessage());
                if (lighting.IsFailure()) LT_WARN("Editor exit: failed to save lighting settings: {}", lighting.GetError().GetErrorMessage());
                if (scripting.IsFailure()) LT_WARN("Editor exit: failed to save scripting settings: {}", scripting.GetError().GetErrorMessage());
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
        ScriptCoreModuleRuntime::ShutdownIncrementalCompiler();
        ScriptCoreModuleRuntime::Shutdown();
    }

    void EditorLayer::OnUpdate(float deltaTime)
    {
        ProcessPendingPlayModeTransition();

        const ImGuiIO& io = ImGui::GetIO();
        ScriptCoreModuleRuntime::SetGameplayInputRoutingState(
            m_GameViewFocused,
            m_GameViewHovered,
            io.WantCaptureMouse,
            io.WantCaptureKeyboard || io.WantTextInput);
        ScriptCoreModuleRuntime::Update(m_PlayModeState);

        // Auto-recompile: when the file watcher detects .cpp/.h saves in
        // Assets/, trigger the full mirror + incremental build + DLL copy
        // pipeline automatically so the user never has to click Build Scripts.
        if (m_PlayModeState == EditorPlayModeState::Edit &&
            ScriptCoreModuleRuntime::HasPendingScriptFileChanges())
        {
            BuildProjectScripts();
        }

        ApplyProjectRenderSettings();
        const bool audioPlaybackAllowed =
            (m_PlayModeState == EditorPlayModeState::Play ||
             m_PlayModeState == EditorPlayModeState::Simulate ||
             m_PlayModeState == EditorPlayModeState::Pause);

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
            m_ProjectLayersSettings = m_ProjectSettingsPanelState.Layers;
            m_ProjectLayersSettingsLoaded = true;
            ApplyProjectPhysics2DSettingsToScenes();

            m_ProjectLighting2DSettings = m_ProjectSettingsPanelState.Lighting2D;
            m_ProjectLighting2DSettingsLoaded = true;
            ApplyProjectLighting2DSettings();

            m_ProjectScriptingSettings = m_ProjectSettingsPanelState.Scripting;
            m_ProjectScriptingSettingsLoaded = true;
            ApplyProjectScriptingSettings();
        }

        if (m_PlayModeState == EditorPlayModeState::Play)
        {
            for (const SceneCollection::Handle handle : m_SceneCollection.CollectHandlesWithRoles(ToSceneRoleMask(SceneRole::RuntimeUpdate)))
            {
                if (handle == m_EditSceneStoredHandle)
                    continue;

                Scene* scene = m_SceneCollection.GetScene(handle);
                if (scene && scene->IsReady())
                    scene->Update(deltaTime);
            }
        }

        Physics2DQueries::SetActiveSceneForScriptQueries(nullptr);

        // Tick particle emitters in edit mode so the inspector preview works.
        // Pass editModePreview=true to prevent PlayOnStart from auto-triggering.
        if (m_PlayModeState != EditorPlayModeState::Play && m_Scene && m_Scene->IsReady())
            UpdateParticleEmitterSystem(m_Scene->GetRegistry(), deltaTime, true);

        if (audioPlaybackAllowed)
        {
            for (const SceneCollection::Handle handle : m_SceneCollection.CollectHandlesWithRoles(ToSceneRoleMask(SceneRole::AudioPlayback)))
            {
                if (handle == m_EditSceneStoredHandle)
                    continue;

                Scene* scene = m_SceneCollection.GetScene(handle);
                if (scene && scene->IsReady())
                    Audio::UpdateSceneAudioSources(scene, deltaTime, true);
            }
        }
        else
        {
            Audio::UpdateSceneAudioSources(m_Scene.get(), deltaTime, false);
        }
        ApplyProjectAudioSettings();

        ApplyAnimationTimelinePreviewToSelectedEntity();

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
        if (m_ShowSceneView &&
            !io.WantTextInput &&
            !io.KeyShift &&
            !io.KeyAlt &&
            !saveModifierDown &&
            ImGui::IsKeyPressed(ImGuiKey_F, false))
        {
            const double nowSeconds = ImGui::GetTime();
            const bool sameEntity = m_SelectedEntity != entt::null && m_SelectedEntity == m_LastEntityFocusShortcutEntity;
            const bool zoomedIn = sameEntity &&
                m_LastEntityFocusShortcutTimeSeconds >= 0.0 &&
                (nowSeconds - m_LastEntityFocusShortcutTimeSeconds) <= kEntityFocusShortcutDoublePressWindowSeconds;

            FocusEditorCameraOnSelectedEntity(zoomedIn);

            if (m_Scene && m_Scene->IsValid(m_SelectedEntity))
            {
                m_LastEntityFocusShortcutTimeSeconds = nowSeconds;
                m_LastEntityFocusShortcutEntity = m_SelectedEntity;
            }
            else
            {
                m_LastEntityFocusShortcutTimeSeconds = -1.0;
                m_LastEntityFocusShortcutEntity = entt::null;
            }
        }

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

    void EditorLayer::FocusEditorCameraOnSelectedEntity(bool zoomedIn)
    {
        if (!m_Scene || !m_EditorCameraController || !m_Scene->IsValid(m_SelectedEntity))
            return;

        const glm::mat4 worldTransform = m_Scene->GetWorldTransformMatrix(m_SelectedEntity);
        const glm::vec3 focusPoint = glm::vec3(worldTransform[3]);
        const Editor::EditorPreferencesData preferences = Editor::EditorPreferences::GetInstance().GetData();
        const float distanceMultiplier = zoomedIn
            ? preferences.EntityFocusDoublePressDistanceMultiplier
            : preferences.EntityFocusSinglePressDistanceMultiplier;
        const float desiredDistance = EstimateEditorCameraFocusDistance(worldTransform, distanceMultiplier);
        m_EditorCameraController->FocusOnPoint(focusPoint, desiredDistance);
    }

    void EditorLayer::OnFixedUpdate(float fixedDeltaTime)
    {
        if (!m_Scene)
            return;

        if ((m_PlayModeState == EditorPlayModeState::Play || m_PlayModeState == EditorPlayModeState::Simulate) &&
            m_Scene->IsReady())
        {
            for (const SceneCollection::Handle handle : m_SceneCollection.CollectHandlesWithRoles(ToSceneRoleMask(SceneRole::FixedUpdate)))
            {
                if (handle == m_EditSceneStoredHandle)
                    continue;

                Scene* scene = m_SceneCollection.GetScene(handle);
                if (!scene || !scene->IsReady())
                    continue;

                const uint16_t worldCount = std::max<uint16_t>(1, scene->GetPhysics2DWorldCount());
                for (uint16_t worldSlot = 0; worldSlot < worldCount; ++worldSlot)
                {
                    Physics2DWorld* physicsWorld = scene->GetPhysics2DWorld(worldSlot);
                    if (physicsWorld)
                        physicsWorld->SetDiagnosticsEnabled(m_ShowPhysicsDiagnosticsWindow);
                }

                scene->FixedUpdate(fixedDeltaTime);
                scene->StepPhysics2D(fixedDeltaTime);
            }
        }

        Physics2DQueries::SetActiveSceneForScriptQueries(nullptr);
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

            ScriptCoreModuleRuntime::ShutdownIncrementalCompiler();
            ScriptCoreModuleRuntime::Shutdown();
            ScriptCoreModuleRuntime::Initialize();

            // Defer startup reimport to a background task once the initial scene is
            // visible, so project open remains responsive for tile-heavy projects.
            m_StartupAssetImportPending = true;
            m_StartupAssetImportInProgress = false;
            m_StartupAssetImportTask = Async::Task<std::string>();

            // Reset per-project panel cache so settings always match the newly opened project.
            m_ProjectSettingsPanelState.Loaded = false;
            m_ProjectSettingsPanelState.StatusMessage.clear();
            m_ProjectSettingsPanelState.StatusIsError = false;
            m_ProjectScriptingSettingsLoaded = false;
            EditorProjectPanel::InvalidateProjectDirectoryCache(m_ProjectPanelState);
            m_MaterialPreviewCache.Entries.clear();

            // Reset tile palette selection/caches so stale keys from the previous project never linger.
            m_TilePaletteState.ActivePaletteKey.clear();
            m_TilePaletteState.InvalidateCache();
            EditorTilePalettePanel::InvalidatePaletteKeyCache(m_TilePaletteState);

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
            RefreshProjectScriptingSettings();

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
            m_ActiveLayoutName = Editor::EditorLayoutManager::NormalizeLayoutName(sessionState.ActiveLayoutName);
            if (m_ActiveLayoutName.empty())
                m_ActiveLayoutName = Editor::EditorLayoutManager::GetDefaultLayoutName();
            {
                Editor::EditorLayoutManager layoutManager;
                if (m_ActiveLayoutName != Editor::EditorLayoutManager::GetDefaultLayoutName() &&
                    !layoutManager.IsCustomLayoutName(m_ActiveLayoutName))
                {
                    m_ActiveLayoutName = Editor::EditorLayoutManager::GetDefaultLayoutName();
                }
            }
            ApplyLayoutWindowState(sessionState.LayoutWindowState);
            EditorInspectorPanel::ApplyNativeScriptEditorSessionState(sessionState.NativeScriptEditorState);
            EditorInspectorPanel::ApplyPersistentFoldoutState(sessionState.InspectorFoldoutState);
            EditorInspectorPanel::ApplyPersistentSectionOrderState(sessionState.InspectorSectionOrderState);
            m_ShowProjectSettingsWindow = sessionState.ShowProjectSettingsWindow;
            m_ShowAssetDiagnosticsWindow = sessionState.ShowAssetDiagnosticsWindow;
            m_ShowPerformancePanel = sessionState.ShowPerformancePanel;
            m_ShowConsoleWindow = sessionState.ShowConsoleWindow;
            m_ProjectPanelState.AssetsRootExpanded = sessionState.ProjectAssetsRootExpanded;
            m_ProjectPanelState.ActiveFolderRelativePath = std::filesystem::path(sessionState.ProjectActiveFolderRelativePath);
            m_ProjectPanelState.GridScale = std::clamp(sessionState.ProjectGridScale, 0.0f, 1.80f);
            m_ProjectPanelState.ExpandedFolderState = sessionState.ProjectFolderExpansionState;

            for (int i = 0; i < sessionState.AdditionalInspectorCount; ++i)
                SpawnAdditionalInspectorPanel();
            for (int i = 0; i < sessionState.AdditionalProjectPanelCount; ++i)
                SpawnAdditionalProjectPanel();

            if (ImGuiLayer* imguiLayer = GetImGuiLayer())
            {
                const std::filesystem::path workingLayoutPath = Editor::EditorLayoutManager::GetProjectWorkingLayoutPath(projectRoot);
                std::error_code layoutError;
                const bool hasWorkingLayout = !workingLayoutPath.empty() && std::filesystem::exists(workingLayoutPath, layoutError);
                if (hasWorkingLayout)
                {
                    if (!imguiLayer->SetLayoutIniPath(workingLayoutPath) || !imguiLayer->LoadLayoutFromDisk(workingLayoutPath))
                        LT_WARN("Failed to restore working editor layout for project '{}'.", projectRoot.string());
                }
                else if (!LoadLayoutByName(m_ActiveLayoutName))
                {
                    LT_WARN("Failed to load editor layout '{}'. Falling back to current ImGui state.", m_ActiveLayoutName);
                }
            }
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

            // Initialize incremental script compiler now that project root is available.
            ScriptCoreModuleRuntime::InitializeIncrementalCompiler();
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

        EditorPreferencesPanel::Draw(m_ShowEditorPreferencesWindow, m_EditorPreferencesPanelState);
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
        DrawAdditionalProjectPanels();
        DrawAnimationTimelinePanel();
        DrawAnimatorGraphPanel();
        DrawPhysicsDiagnosticsPanel();
        DrawPerformancePanel();
        DrawConsolePanel();
        DrawSaveScenePopup();
        DrawLayoutSavePopup();
        DrawLayoutDeletePopup();
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

    void EditorLayer::ResetLayoutToDefault()
    {
        if (!LoadLayoutByName(Editor::EditorLayoutManager::GetDefaultLayoutName()))
        {
            LT_WARN("Editor layout reset to default failed.");
            return;
        }

        LT_INFO("Editor layout reset to default.");
    }

}  // namespace Limitless

