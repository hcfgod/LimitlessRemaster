#include "EditorMenuBar.h"

#include "Core/Application.h"
#include "imgui/imgui.h"

namespace Limitless::EditorMenuBar
{
    void Draw(EditorPlayModeState playModeState,
              bool& showDemoWindow,
              const std::function<void()>& onPlay,
              const std::function<void()>& onStop,
              const std::function<void()>& onTogglePause)
    {
        if (!ImGui::BeginMainMenuBar())
            return;

        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("New")) {}
            if (ImGui::MenuItem("Open")) {}
            if (ImGui::MenuItem("Save")) {}
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
