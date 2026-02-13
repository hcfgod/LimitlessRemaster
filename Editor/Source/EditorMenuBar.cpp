#include "EditorMenuBar.h"

#include "Core/Application.h"
#include "imgui/imgui.h"

namespace Limitless::EditorMenuBar
{
    void Draw(EditorPlayModeState playModeState,
              bool& showDemoWindow,
              bool& showAssetDiagnosticsWindow,
              bool& showBuildAndRunWindow,
              const std::function<void()>& onOpenProject,
              const std::function<void()>& onCreateProject,
              const std::function<void()>& onProjectSettings,
              const std::function<void()>& onReimportChangedAssets,
              const std::function<void()>& onReimportAllAssets,
              const std::function<void()>& onValidateAssetDatabase,
              const std::function<void()>& onNewScene,
              const std::function<void()>& onSaveScene,
              const std::function<void()>& onSaveSceneAs,
              const std::function<void()>& onPlay,
              const std::function<void()>& onStop,
              const std::function<void()>& onTogglePause)
    {
        if (!ImGui::BeginMainMenuBar())
            return;

        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Open Project..."))
                onOpenProject();
            if (ImGui::MenuItem("Create Project..."))
                onCreateProject();
            if (ImGui::MenuItem("Project Settings..."))
                onProjectSettings();
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
            if (ImGui::MenuItem("Undo")) {}
            if (ImGui::MenuItem("Redo")) {}
            ImGui::Separator();
            if (ImGui::MenuItem("Preferences")) {}
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

        if (ImGui::BeginMenu("View"))
        {
            ImGui::MenuItem("Scene", nullptr, nullptr);
            ImGui::MenuItem("Inspector", nullptr, nullptr);
            ImGui::MenuItem("Viewport", nullptr, nullptr);
            ImGui::MenuItem("Project", nullptr, nullptr);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Window"))
        {
            ImGui::MenuItem("Demo Window", nullptr, &showDemoWindow);
            ImGui::MenuItem("Asset Diagnostics", nullptr, &showAssetDiagnosticsWindow);
            ImGui::MenuItem("Build And Run", nullptr, &showBuildAndRunWindow);
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
            "Pause";
        ImGui::TextDisabled("Mode: %s", modeLabel);
        ImGui::SameLine();

        if (playModeState == EditorPlayModeState::Edit)
        {
            if (ImGui::Button("Play"))
                onPlay();
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

        ImGui::EndMainMenuBar();
    }
}
