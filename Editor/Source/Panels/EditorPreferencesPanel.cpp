#include "EditorPreferencesPanel.h"

#include "EditorPanelStyle.h"

#include "imgui/imgui.h"

namespace Limitless::EditorPreferencesPanel
{
    namespace
    {
        void EnsureLoaded(EditorPreferencesPanelState& state)
        {
            if (state.Loaded)
                return;
            Editor::EditorPreferences::GetInstance().EnsureLoaded();
            state.Preferences = Editor::EditorPreferences::GetInstance().GetData();
            state.Loaded = true;
        }

        void Persist(EditorPreferencesPanelState& state)
        {
            Editor::EditorPreferences::GetInstance().SetData(state.Preferences);
            state.Preferences = Editor::EditorPreferences::GetInstance().GetData();
        }
    }

    void Draw(bool& open, EditorPreferencesPanelState& state)
    {
        if (!open)
            return;

        EnsureLoaded(state);

        ImGui::SetNextWindowSize(ImVec2(460.0f, 240.0f), ImGuiCond_FirstUseEver);
        EditorPanelStyle::PushPanelVisualStyle();
        if (!ImGui::Begin("Preferences", &open))
        {
            ImGui::End();
            EditorPanelStyle::PopPanelVisualStyle();
            return;
        }

        if (ImGui::BeginTabBar("EditorPreferencesTabs"))
        {
            if (ImGui::BeginTabItem("Editor Camera"))
            {
                bool changed = false;

                ImGui::TextUnformatted("Frame Selected - Single Press Distance");
                changed |= ImGui::SliderFloat("##EntityFocusSinglePressDistanceMultiplier",
                                              &state.Preferences.EntityFocusSinglePressDistanceMultiplier,
                                              1.0f,
                                              12.0f,
                                              "%.2fx");

                ImGui::TextUnformatted("Frame Selected - Double Press Distance");
                changed |= ImGui::SliderFloat("##EntityFocusDoublePressDistanceMultiplier",
                                              &state.Preferences.EntityFocusDoublePressDistanceMultiplier,
                                              0.5f,
                                              8.0f,
                                              "%.2fx");

                if (changed)
                    Persist(state);

                if (ImGui::Button("Reset Defaults"))
                {
                    state.Preferences = Editor::EditorPreferencesData{};
                    Persist(state);
                }

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::End();
        EditorPanelStyle::PopPanelVisualStyle();
    }
}
