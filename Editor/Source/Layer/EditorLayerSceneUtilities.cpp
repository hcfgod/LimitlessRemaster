#include "PrecompiledHeader.h"
#include "EditorLayer.h"

#include "Assets/AssetDatabase.h"
#include "Assets/AssetPaths.h"
#include "Core/Application.h"
#include "Core/Debug/Log.h"
#include "EditorInspectorPanel.h"
#include "ImGui/ImGuiLayer.h"
#include "Project/ProjectManager.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
#include <string_view>
#include <vector>

namespace Limitless
{
    namespace
    {
        constexpr const char* kSceneFileSuffix = ".scene.json";
        constexpr const char* kEditorSessionStateRelativePath = "Project/Settings/EditorSessionState.json";
        constexpr uint32_t kEditorSessionStateVersion = 11;
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

        std::filesystem::path GetEditorSessionStatePathForProjectRoot(const std::filesystem::path& projectRoot)
        {
            if (projectRoot.empty())
                return {};
            return projectRoot / kEditorSessionStateRelativePath;
        }

        EditorSessionStateData ReadProjectSessionState(const std::filesystem::path& projectRoot)
        {
            const std::filesystem::path statePath = GetEditorSessionStatePathForProjectRoot(projectRoot);
            if (statePath.empty())
                return {};

            try
            {
                std::ifstream in(statePath, std::ios::in | std::ios::binary);
                if (!in.is_open())
                    return {};

                nlohmann::json root;
                in >> root;
                if (!root.is_object())
                    return {};

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

                if (version != 3 && version != 4 && version != 5 && version != 6 && version != 7 && version != 8 &&
                    version != 9 && version != 10 && version != kEditorSessionStateVersion)
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
                    state.LayoutWindowState.ShowProjectSettingsWindow =
                        root.value("showProjectSettingsWindow", state.LayoutWindowState.ShowProjectSettingsWindow);
                    state.LayoutWindowState.ShowBuildSettingsWindow =
                        root.value("showBuildSettingsWindow", state.LayoutWindowState.ShowBuildSettingsWindow);
                    state.LayoutWindowState.ShowAssetDiagnosticsWindow =
                        root.value("showAssetDiagnosticsWindow", state.LayoutWindowState.ShowAssetDiagnosticsWindow);
                    state.LayoutWindowState.ShowPhysicsDiagnosticsWindow =
                        root.value("showPhysicsDiagnosticsWindow", state.LayoutWindowState.ShowPhysicsDiagnosticsWindow);
                    state.LayoutWindowState.ShowConsoleWindow =
                        root.value("showConsoleWindow", state.LayoutWindowState.ShowConsoleWindow);
                    state.LayoutWindowState.ShowEditorFpsOverlay =
                        root.value("showEditorFpsOverlay", state.LayoutWindowState.ShowEditorFpsOverlay);
                    state.LayoutWindowState.ShowGizmoToolbar =
                        root.value("showGizmoToolbar", state.LayoutWindowState.ShowGizmoToolbar);
                    state.LayoutWindowState.ShowPerformancePanel =
                        root.value("showPerformancePanel", state.LayoutWindowState.ShowPerformancePanel);
                    state.LayoutWindowState.ShowAnimationTimelinePanel =
                        root.value("showAnimationTimelinePanel", state.LayoutWindowState.ShowAnimationTimelinePanel);
                    state.LayoutWindowState.ShowAnimatorGraphPanel =
                        root.value("showAnimatorGraphPanel", state.LayoutWindowState.ShowAnimatorGraphPanel);
                    state.LayoutWindowState.ShowInputActionsPanel =
                        root.value("showInputActionsPanel", state.LayoutWindowState.ShowInputActionsPanel);
                    state.LayoutWindowState.ShowTilePalettePanel =
                        root.value("showTilePalettePanel", state.LayoutWindowState.ShowTilePalettePanel);
                    state.LayoutWindowState.ShowSpriteEditorWindow =
                        root.value("showSpriteEditorWindow", state.LayoutWindowState.ShowSpriteEditorWindow);
                    state.LayoutWindowState.ShowDemoWindow = root.value("showDemoWindow", state.LayoutWindowState.ShowDemoWindow);
                }

                state.NativeScriptEditorState.IsOpen = root.value("nativeScriptEditorIsOpen", false);
                state.NativeScriptEditorState.LastEditedScriptClassName =
                    root.value("nativeScriptEditorLastClassName", std::string{});
                state.NativeScriptEditorState.LastEditedScriptAssetRelativePath =
                    root.value("nativeScriptEditorLastAssetRelativePath", std::string{});
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
                return;

            const std::filesystem::path statePath = GetEditorSessionStatePathForProjectRoot(projectRoot);
            if (statePath.empty())
                return;

            try
            {
                std::error_code ec;
                std::filesystem::create_directories(statePath.parent_path(), ec);
                if (ec)
                    return;

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
                root["showInputActionsPanel"] = state.LayoutWindowState.ShowInputActionsPanel;
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
                        return;
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
    }

    Editor::EditorLayoutWindowState EditorLayer::CaptureLayoutWindowState() const
    {
        Editor::EditorLayoutWindowState state{};
        state.ShowScenePanel = m_ShowScenePanel;
        state.ShowInspectorPanel = m_ShowInspectorPanel;
        state.ShowProjectPanel = m_ShowProjectPanel;
        state.ShowSceneView = m_ShowSceneView;
        state.ShowGameView = m_ShowGameView;
        state.ShowEditorPreferencesWindow = m_ShowEditorPreferencesWindow;
        state.ShowProjectSettingsWindow = m_ShowProjectSettingsWindow;
        state.ShowBuildSettingsWindow = m_ShowBuildSettingsWindow;
        state.ShowAssetDiagnosticsWindow = m_ShowAssetDiagnosticsWindow;
        state.ShowPhysicsDiagnosticsWindow = m_ShowPhysicsDiagnosticsWindow;
        state.ShowConsoleWindow = m_ShowConsoleWindow;
        state.ShowEditorFpsOverlay = m_ShowEditorFpsOverlay;
        state.ShowGizmoToolbar = m_ShowGizmoToolbar;
        state.ShowPerformancePanel = m_ShowPerformancePanel;
        state.ShowAnimationTimelinePanel = m_ShowAnimationTimelinePanel;
        state.ShowAnimatorGraphPanel = m_ShowAnimatorGraphPanel;
        state.ShowInputActionsPanel = m_ShowInputActionsPanel;
        state.ShowTilePalettePanel = m_TilePaletteState.PanelOpen;
        state.ShowSpriteEditorWindow = m_SpriteEditorState.Open;
        state.ShowDemoWindow = m_ShowDemoWindow;
        state.ProjectGridScale = std::clamp(m_ProjectPanelState.GridScale, 0.0f, 1.80f);
        return state;
    }

    void EditorLayer::ApplyLayoutWindowState(const Editor::EditorLayoutWindowState& state)
    {
        m_ShowScenePanel = state.ShowScenePanel;
        m_ShowInspectorPanel = state.ShowInspectorPanel;
        m_ShowProjectPanel = state.ShowProjectPanel;
        m_ShowSceneView = state.ShowSceneView;
        m_ShowGameView = state.ShowGameView;
        m_ShowEditorPreferencesWindow = state.ShowEditorPreferencesWindow;
        m_ShowProjectSettingsWindow = state.ShowProjectSettingsWindow;
        m_ShowBuildSettingsWindow = state.ShowBuildSettingsWindow;
        m_ShowAssetDiagnosticsWindow = state.ShowAssetDiagnosticsWindow;
        m_ShowPhysicsDiagnosticsWindow = state.ShowPhysicsDiagnosticsWindow;
        m_ShowConsoleWindow = state.ShowConsoleWindow;
        m_ShowEditorFpsOverlay = state.ShowEditorFpsOverlay;
        m_ShowGizmoToolbar = state.ShowGizmoToolbar;
        m_ShowPerformancePanel = state.ShowPerformancePanel;
        m_ShowAnimationTimelinePanel = state.ShowAnimationTimelinePanel;
        m_ShowAnimatorGraphPanel = state.ShowAnimatorGraphPanel;
        m_ShowInputActionsPanel = state.ShowInputActionsPanel;
        m_TilePaletteState.PanelOpen = state.ShowTilePalettePanel;
        m_SpriteEditorState.Open = state.ShowSpriteEditorWindow;
        m_ShowDemoWindow = state.ShowDemoWindow;
        m_ProjectPanelState.GridScale = std::clamp(state.ProjectGridScale, 0.0f, 1.80f);
        for (auto& additionalProjectPanel : m_AdditionalProjectPanels)
            additionalProjectPanel.State.GridScale = m_ProjectPanelState.GridScale;
    }

    ImGuiLayer* EditorLayer::GetImGuiLayer() const
    {
        LayerRef imguiLayerRef = Application::GetInstance().GetLayerStack().GetLayer("ImGuiLayer");
        if (!imguiLayerRef)
            return nullptr;
        return dynamic_cast<ImGuiLayer*>(imguiLayerRef.get());
    }

    bool EditorLayer::SaveWorkingLayoutToDisk()
    {
        const auto& projectManager = Project::ProjectManager::GetInstance();
        if (!projectManager.HasOpenProject())
            return false;

        ImGuiLayer* imguiLayer = GetImGuiLayer();
        if (!imguiLayer)
            return false;

        if (imguiLayer->HasPendingLayoutLoad())
            return true;

        const std::filesystem::path workingLayoutPath = Editor::EditorLayoutManager::GetProjectWorkingLayoutPath(projectManager.GetProjectRoot());
        if (workingLayoutPath.empty())
            return false;

        if (imguiLayer->GetLayoutIniPath() != workingLayoutPath.string() && !imguiLayer->SetLayoutIniPath(workingLayoutPath))
            return false;

        return imguiLayer->SaveCurrentLayoutToDisk(workingLayoutPath);
    }

    bool EditorLayer::LoadLayoutByName(const std::string& layoutName)
    {
        const auto& projectManager = Project::ProjectManager::GetInstance();
        if (!projectManager.HasOpenProject())
            return false;

        ImGuiLayer* imguiLayer = GetImGuiLayer();
        if (!imguiLayer)
            return false;

        Editor::EditorLayoutManager layoutManager;
        std::string normalizedLayoutName = Editor::EditorLayoutManager::NormalizeLayoutName(layoutName);
        if (normalizedLayoutName.empty())
            normalizedLayoutName = Editor::EditorLayoutManager::GetDefaultLayoutName();

        const std::filesystem::path projectRoot = projectManager.GetProjectRoot();
        const std::filesystem::path workingLayoutPath = Editor::EditorLayoutManager::GetProjectWorkingLayoutPath(projectRoot);
        const std::filesystem::path defaultLayoutPath = imguiLayer->GetDefaultLayoutIniPath();
        Editor::EditorLayoutWindowState windowState = Editor::EditorLayoutManager::CreateDefaultWindowState();

        std::string resolvedLayoutName = normalizedLayoutName;
        if (!layoutManager.LoadLayoutToPath(resolvedLayoutName, defaultLayoutPath, workingLayoutPath, windowState))
        {
            resolvedLayoutName = Editor::EditorLayoutManager::GetDefaultLayoutName();
            if (!layoutManager.LoadLayoutToPath(resolvedLayoutName, defaultLayoutPath, workingLayoutPath, windowState))
                return false;
        }

        if (!imguiLayer->SetLayoutIniPath(workingLayoutPath))
            return false;
        if (!imguiLayer->LoadLayoutFromDisk(workingLayoutPath))
            return false;

        ApplyLayoutWindowState(windowState);
        m_ActiveLayoutName = resolvedLayoutName;
        PersistProjectSessionState();
        return true;
    }

    bool EditorLayer::SaveCurrentLayoutAs(const std::string& layoutName)
    {
        const auto& projectManager = Project::ProjectManager::GetInstance();
        if (!projectManager.HasOpenProject())
            return false;

        const std::string normalizedLayoutName = Editor::EditorLayoutManager::NormalizeLayoutName(layoutName);
        if (normalizedLayoutName.empty() || normalizedLayoutName == Editor::EditorLayoutManager::GetDefaultLayoutName())
            return false;

        if (!SaveWorkingLayoutToDisk())
            return false;

        Editor::EditorLayoutManager layoutManager;
        const std::filesystem::path workingLayoutPath = Editor::EditorLayoutManager::GetProjectWorkingLayoutPath(projectManager.GetProjectRoot());
        if (!layoutManager.SaveCustomLayout(normalizedLayoutName, workingLayoutPath, CaptureLayoutWindowState()))
            return false;

        m_ActiveLayoutName = normalizedLayoutName;
        PersistProjectSessionState();
        return true;
    }

    bool EditorLayer::DeleteSavedLayout(const std::string& layoutName)
    {
        const std::string normalizedLayoutName = Editor::EditorLayoutManager::NormalizeLayoutName(layoutName);
        if (normalizedLayoutName.empty() || normalizedLayoutName == Editor::EditorLayoutManager::GetDefaultLayoutName())
            return false;

        Editor::EditorLayoutManager layoutManager;
        if (!layoutManager.DeleteCustomLayout(normalizedLayoutName))
            return false;

        if (m_ActiveLayoutName == normalizedLayoutName)
            return LoadLayoutByName(Editor::EditorLayoutManager::GetDefaultLayoutName());

        PersistProjectSessionState();
        return true;
    }

    void EditorLayer::RequestOpenSaveLayoutPopup(const std::string& initialLayoutName)
    {
        std::fill(m_SaveLayoutNameBuffer.begin(), m_SaveLayoutNameBuffer.end(), '\0');
        std::string popupName = initialLayoutName;
        if (popupName.empty() && m_ActiveLayoutName != Editor::EditorLayoutManager::GetDefaultLayoutName())
            popupName = m_ActiveLayoutName;
        const size_t copyCount = std::min(popupName.size(), m_SaveLayoutNameBuffer.size() - 1);
        std::copy_n(popupName.c_str(), copyCount, m_SaveLayoutNameBuffer.begin());
        m_SaveLayoutNameBuffer[copyCount] = '\0';
        m_RequestOpenSaveLayoutPopup = true;
    }

    void EditorLayer::RequestOpenDeleteLayoutPopup(const std::string& layoutName)
    {
        const std::string normalizedLayoutName = Editor::EditorLayoutManager::NormalizeLayoutName(layoutName);
        if (normalizedLayoutName.empty() || normalizedLayoutName == Editor::EditorLayoutManager::GetDefaultLayoutName())
            return;
        m_PendingDeleteLayoutName = normalizedLayoutName;
        m_RequestOpenDeleteLayoutPopup = true;
    }

    void EditorLayer::DrawLayoutSavePopup()
    {
        if (m_RequestOpenSaveLayoutPopup)
        {
            ImGui::OpenPopup("Save Layout As");
            m_RequestOpenSaveLayoutPopup = false;
            m_SaveLayoutPopupOpen = true;
        }

        if (!m_SaveLayoutPopupOpen)
            return;

        if (!ImGui::BeginPopupModal("Save Layout As", &m_SaveLayoutPopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
            return;

        ImGui::Text("Save the current editor layout as a named preset.");
        ImGui::Separator();
        ImGui::InputText("Layout Name", m_SaveLayoutNameBuffer.data(), m_SaveLayoutNameBuffer.size());

        bool closePopup = false;
        if (ImGui::Button("Save", ImVec2(120.0f, 0.0f)))
        {
            if (SaveCurrentLayoutAs(m_SaveLayoutNameBuffer.data()))
                closePopup = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
            closePopup = true;

        if (closePopup)
        {
            m_SaveLayoutPopupOpen = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    void EditorLayer::DrawLayoutDeletePopup()
    {
        if (m_RequestOpenDeleteLayoutPopup)
        {
            ImGui::OpenPopup("Delete Layout");
            m_RequestOpenDeleteLayoutPopup = false;
            m_DeleteLayoutPopupOpen = true;
        }

        if (!m_DeleteLayoutPopupOpen)
            return;

        if (!ImGui::BeginPopupModal("Delete Layout", &m_DeleteLayoutPopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
            return;

        ImGui::Text("Delete layout '%s'?", m_PendingDeleteLayoutName.c_str());
        ImGui::Separator();

        bool closePopup = false;
        if (ImGui::Button("Delete", ImVec2(120.0f, 0.0f)))
        {
            (void)DeleteSavedLayout(m_PendingDeleteLayoutName);
            closePopup = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
            closePopup = true;

        if (closePopup)
        {
            m_DeleteLayoutPopupOpen = false;
            m_PendingDeleteLayoutName.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
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
        EditorInspectorPanel::GetPersistentFoldoutState(state.InspectorFoldoutState);
        EditorInspectorPanel::GetPersistentSectionOrderState(state.InspectorSectionOrderState);
        state.ActiveLayoutName = m_ActiveLayoutName;
        state.LayoutWindowState = CaptureLayoutWindowState();
        state.ShowProjectSettingsWindow = m_ShowProjectSettingsWindow;
        state.ShowAssetDiagnosticsWindow = m_ShowAssetDiagnosticsWindow;
        state.ShowPerformancePanel = m_ShowPerformancePanel;
        state.ShowConsoleWindow = m_ShowConsoleWindow;
        state.ProjectAssetsRootExpanded = m_ProjectPanelState.AssetsRootExpanded;
        state.ProjectActiveFolderRelativePath = m_ProjectPanelState.ActiveFolderRelativePath.generic_string();
        state.ProjectGridScale = m_ProjectPanelState.GridScale;
        state.ProjectFolderExpansionState = m_ProjectPanelState.ExpandedFolderState;
        state.AdditionalInspectorCount = static_cast<int>(m_AdditionalInspectors.size());
        state.AdditionalProjectPanelCount = static_cast<int>(m_AdditionalProjectPanels.size());
        WriteProjectSessionState(projectManager.GetProjectRoot(), state);
        (void)SaveWorkingLayoutToDisk();
    }

    void EditorLayer::ProcessPendingSceneTransitions()
    {
        while (true)
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

                    if (m_PlayModeState != EditorPlayModeState::Edit &&
                        pendingTransition->LoadMode == LoadSceneMode::Single &&
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
                        (void)LoadSceneFromAssetKeyInPlayMode(*resolvedSceneAssetKey, pendingTransition->LoadMode);
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
                        (void)LoadSceneFromAssetKeyInPlayMode(m_CurrentSceneAssetKey, LoadSceneMode::Single);
                    break;
                }
                case SceneTransitionType::SetActiveSceneByAssetKey:
                {
                    const auto resolvedSceneAssetKey = ResolveSceneIdentifierToAssetKey(pendingTransition->SceneIdentifier);
                    if (!resolvedSceneAssetKey.has_value())
                    {
                        LT_WARN("Scene activation request '{}' could not be resolved to a scene asset key.",
                            pendingTransition->SceneIdentifier);
                        break;
                    }

                    if (m_PlayModeState == EditorPlayModeState::Edit)
                    {
                        LT_WARN("SetActiveScene('{}') is ignored in Edit Mode.", *resolvedSceneAssetKey);
                        break;
                    }

                    if (!ActivateLoadedSceneInPlayModeByAssetKey(*resolvedSceneAssetKey))
                    {
                        LT_WARN("SetActiveScene('{}') failed because the scene is not loaded.", *resolvedSceneAssetKey);
                    }
                    break;
                }
                case SceneTransitionType::UnloadByAssetKey:
                {
                    const auto resolvedSceneAssetKey = ResolveSceneIdentifierToAssetKey(pendingTransition->SceneIdentifier);
                    if (!resolvedSceneAssetKey.has_value())
                    {
                        LT_WARN("Scene unload request '{}' could not be resolved to a scene asset key.",
                            pendingTransition->SceneIdentifier);
                        break;
                    }

                    if (m_PlayModeState == EditorPlayModeState::Edit)
                    {
                        LT_WARN("UnloadScene('{}') is ignored in Edit Mode.", *resolvedSceneAssetKey);
                        break;
                    }

                    if (!UnloadLoadedSceneInPlayModeByAssetKey(*resolvedSceneAssetKey))
                    {
                        LT_WARN("UnloadScene('{}') failed because the scene is not loaded.", *resolvedSceneAssetKey);
                    }
                    break;
                }
            }
        }
    }
}  // namespace Limitless
