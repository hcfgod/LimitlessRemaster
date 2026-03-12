#include "EditorMenuBar.h"

#include "EditorPanelStyle.h"
#include "Core/Application.h"
#include "imgui/imgui.h"

namespace Limitless::EditorMenuBar
{
    void Draw(EditorPlayModeState playModeState,
              bool& showScenePanel,
              bool& showInspectorPanel,
              bool& showSceneView,
              bool& showGameView,
              bool& showProjectPanel,
              bool& showDemoWindow,
              bool& showEditorPreferencesWindow,
              bool& showAssetDiagnosticsWindow,
              bool& showPhysicsDiagnosticsWindow,
              bool& showConsoleWindow,
              bool& showEditorFpsOverlay,
              bool& showGizmoToolbar,
              bool& showPerformancePanel,
              bool& showAnimationTimelinePanel,
              bool& showAnimatorGraphPanel,
              bool& showTilePalettePanel,
              const std::function<void()>& onOpenProject,
              const std::function<void()>& onCreateProject,
              const std::function<void()>& onProjectSettings,
              const std::function<void()>& onBuildSettings,
              const std::function<void()>& onBuildScripts,
              const std::function<void()>& onReimportChangedAssets,
              const std::function<void()>& onReimportAllAssets,
              const std::function<void()>& onValidateAssetDatabase,
              const std::function<void()>& onNewScene,
              const std::function<void()>& onSaveScene,
              const std::function<void()>& onSaveSceneAs,
              const std::function<void()>& onUndo,
              const std::function<void()>& onRedo,
              bool canUndo,
              bool canRedo,
              const std::string& undoLabel,
              const std::string& redoLabel,
              const std::function<void()>& onPlay,
              const std::function<void()>& onSimulate,
              const std::function<void()>& onStop,
              const std::function<void()>& onTogglePause,
              bool isEditingPrefabAsset,
              const std::string& prefabAssetDisplayName,
              bool canReturnFromPrefabMode,
              const std::function<void()>& onReturnFromPrefabMode,
              bool canApplyPrefabToInstances,
              const std::function<void()>& onApplyPrefabToInstances,
              const std::function<void()>& onDrawLayoutsMenu,
              const std::function<void()>& onResetLayoutToDefault)
    {
        EditorPanelStyle::PushPanelVisualStyle();
        if (!ImGui::BeginMainMenuBar())
        {
            EditorPanelStyle::PopPanelVisualStyle();
            return;
        }

        auto trimLabel = [](const std::string& label) {
            constexpr size_t kMaxLabelLength = 40;
            if (label.size() <= kMaxLabelLength)
                return label;
            return label.substr(0, kMaxLabelLength - 3) + "...";
        };

        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Open Project..."))
                onOpenProject();
            if (ImGui::MenuItem("Create Project..."))
                onCreateProject();
            if (ImGui::MenuItem("Project Settings..."))
                onProjectSettings();
            if (ImGui::MenuItem("Build Settings..."))
                onBuildSettings();
            ImGui::Separator();
            if (ImGui::MenuItem("New Scene"))
                onNewScene();
            if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
                onSaveScene();
            if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S"))
                onSaveSceneAs();
            ImGui::Separator();
            if (ImGui::MenuItem("Exit"))
                Application::GetInstance().SetRunning(false);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit"))
        {
            const std::string undoMenuText = undoLabel.empty() ? "Undo" : ("Undo " + undoLabel);
            const std::string redoMenuText = redoLabel.empty() ? "Redo" : ("Redo " + redoLabel);

            if (ImGui::MenuItem(undoMenuText.c_str(), "Ctrl+Z", false, canUndo))
                onUndo();
            if (ImGui::MenuItem(redoMenuText.c_str(), "Ctrl+Y", false, canRedo))
                onRedo();
            ImGui::Separator();
            if (ImGui::MenuItem("Preferences..."))
                showEditorPreferencesWindow = true;
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Assets"))
        {
            if (ImGui::MenuItem("Reimport Changed"))
                onReimportChangedAssets();
            if (ImGui::MenuItem("Reimport All"))
                onReimportAllAssets();
            ImGui::Separator();
            if (ImGui::MenuItem("Validate Asset Database"))
                onValidateAssetDatabase();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Tools"))
        {
            if (ImGui::MenuItem("Build Scripts", "Ctrl+Shift+R"))
                onBuildScripts();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View"))
        {
            ImGui::MenuItem("Scene", nullptr, &showScenePanel);
            ImGui::MenuItem("Inspector", nullptr, &showInspectorPanel);
            ImGui::MenuItem("Scene View", nullptr, &showSceneView);
            ImGui::MenuItem("Game View", nullptr, &showGameView);
            ImGui::MenuItem("Project", nullptr, &showProjectPanel);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Window"))
        {
            ImGui::MenuItem("Preferences", nullptr, &showEditorPreferencesWindow);
            ImGui::MenuItem("Demo Window", nullptr, &showDemoWindow);
            ImGui::MenuItem("Asset Diagnostics", nullptr, &showAssetDiagnosticsWindow);
            ImGui::MenuItem("Physics 2D Diagnostics", nullptr, &showPhysicsDiagnosticsWindow);
            ImGui::MenuItem("Console", nullptr, &showConsoleWindow);
            ImGui::MenuItem("FPS Overlay", nullptr, &showEditorFpsOverlay);
            ImGui::MenuItem("Gizmo Toolbar", nullptr, &showGizmoToolbar);
            ImGui::MenuItem("Performance", nullptr, &showPerformancePanel);
            ImGui::MenuItem("Animation Timeline", nullptr, &showAnimationTimelinePanel);
            ImGui::MenuItem("Animator Graph", nullptr, &showAnimatorGraphPanel);
            ImGui::MenuItem("Tile Palette", nullptr, &showTilePalettePanel);
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Layout to Default") && onResetLayoutToDefault)
                onResetLayoutToDefault();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Layouts"))
        {
            if (onDrawLayoutsMenu)
                onDrawLayoutsMenu();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help"))
        {
            if (ImGui::MenuItem("About")) {}
            ImGui::EndMenu();
        }

        ImGui::Separator();

        const char* modeLabel =
            (playModeState == EditorPlayModeState::Edit) ? "Edit" :
            (playModeState == EditorPlayModeState::Play) ? "Play" :
            (playModeState == EditorPlayModeState::Simulate) ? "Simulate" :
            "Pause";
        ImGui::TextDisabled("Mode: %s", modeLabel);
        ImGui::SameLine();

        if (playModeState == EditorPlayModeState::Edit)
        {
            if (ImGui::Button("Play"))
                onPlay();
            ImGui::SameLine();
            if (ImGui::Button("Simulate"))
                onSimulate();
        }
        else
        {
            if (ImGui::Button("Stop"))
                onStop();
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(playModeState == EditorPlayModeState::Edit);
        if (playModeState == EditorPlayModeState::Pause)
        {
            if (ImGui::Button("Resume"))
                onTogglePause();
        }
        else
        {
            if (ImGui::Button("Pause"))
                onTogglePause();
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        const std::string undoStatus = canUndo
            ? ("Undo: " + trimLabel(undoLabel))
            : std::string("Undo: <none>");
        const std::string redoStatus = canRedo
            ? ("Redo: " + trimLabel(redoLabel))
            : std::string("Redo: <none>");
        ImGui::TextDisabled("%s    %s", undoStatus.c_str(), redoStatus.c_str());

        if (isEditingPrefabAsset)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.40f, 0.73f, 1.0f, 1.0f), "Prefab Mode: %s", prefabAssetDisplayName.c_str());

            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();

            ImGui::BeginDisabled(!canReturnFromPrefabMode);
            if (ImGui::Button("Back") && onReturnFromPrefabMode)
                onReturnFromPrefabMode();
            ImGui::EndDisabled();

            ImGui::SameLine();
            ImGui::BeginDisabled(!canApplyPrefabToInstances);
            if (ImGui::Button("Apply To Instances") && onApplyPrefabToInstances)
                onApplyPrefabToInstances();
            ImGui::EndDisabled();
        }

        ImGui::EndMainMenuBar();
        EditorPanelStyle::PopPanelVisualStyle();
    }
}
