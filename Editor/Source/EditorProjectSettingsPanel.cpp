#include "EditorProjectSettingsPanel.h"

#include "Core/Input/InputSystem.h"
#include "Project/ProjectManager.h"

#include "imgui/imgui.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <string>

namespace Limitless::EditorProjectSettingsPanel
{
    namespace
    {
        void SetStatus(EditorProjectSettingsPanelState& state, bool isError, const std::string& msg)
        {
            state.StatusIsError = isError;
            state.StatusMessage = msg;
        }

        bool EnsureLoaded(EditorProjectSettingsPanelState& state, const std::filesystem::path& projectRoot)
        {
            if (state.Loaded)
            {
                return true;
            }

            const auto r = Project::LoadRenderSettings(projectRoot);
            const auto a = Project::LoadAudioSettings(projectRoot);
            const auto i = Project::LoadInputSettings(projectRoot);
            const auto l = Project::LoadLayersSettings(projectRoot);

            if (r.IsFailure()) { SetStatus(state, true, r.GetError().GetErrorMessage()); return false; }
            if (a.IsFailure()) { SetStatus(state, true, a.GetError().GetErrorMessage()); return false; }
            if (i.IsFailure()) { SetStatus(state, true, i.GetError().GetErrorMessage()); return false; }
            if (l.IsFailure()) { SetStatus(state, true, l.GetError().GetErrorMessage()); return false; }

            state.Render = r.GetValue();
            state.Audio = a.GetValue();
            state.Input = i.GetValue();
            state.Layers = l.GetValue();
            state.Loaded = true;
            return true;
        }

        void DrawLayersEditor(Project::LayersSettings& layers)
        {
            if (layers.Layers.empty())
            {
                layers.Layers.push_back("Default");
            }

            ImGui::TextDisabled("Layers");
            ImGui::Separator();

            // Keep a stable selection index across frames.
            static int selectedIndex = 0;
            selectedIndex = std::clamp(selectedIndex, 0, static_cast<int>(layers.Layers.size()) - 1);

            ImGui::BeginChild("LayersList", ImVec2(280.0f, 220.0f), true);
            for (int i = 0; i < static_cast<int>(layers.Layers.size()); ++i)
            {
                const bool selected = (i == selectedIndex);
                if (ImGui::Selectable(layers.Layers[i].c_str(), selected))
                {
                    selectedIndex = i;
                }
            }
            ImGui::EndChild();

            ImGui::SameLine();

            ImGui::BeginGroup();
            ImGui::TextDisabled("Selected");

            if (selectedIndex >= 0 && selectedIndex < static_cast<int>(layers.Layers.size()))
            {
                static std::array<char, 128> nameBuffer{};
                std::snprintf(nameBuffer.data(), nameBuffer.size(), "%s", layers.Layers[selectedIndex].c_str());

                ImGui::InputText("Name", nameBuffer.data(), nameBuffer.size());
                if (ImGui::Button("Apply Rename", ImVec2(120, 0)))
                {
                    std::string newName = nameBuffer.data();
                    if (!newName.empty())
                    {
                        layers.Layers[selectedIndex] = std::move(newName);
                    }
                }
            }

            if (ImGui::Button("Add Layer", ImVec2(120, 0)))
            {
                layers.Layers.push_back("New Layer");
                selectedIndex = static_cast<int>(layers.Layers.size()) - 1;
            }

            ImGui::SameLine();
            ImGui::BeginDisabled(layers.Layers.size() <= 1);
            if (ImGui::Button("Remove", ImVec2(120, 0)))
            {
                if (selectedIndex >= 0 && selectedIndex < static_cast<int>(layers.Layers.size()))
                {
                    layers.Layers.erase(layers.Layers.begin() + selectedIndex);
                    selectedIndex = std::clamp(selectedIndex, 0, static_cast<int>(layers.Layers.size()) - 1);
                }
            }
            ImGui::EndDisabled();

            ImGui::EndGroup();
        }
    }

    void Draw(bool& open, EditorProjectSettingsPanelState& state)
    {
        if (!open)
        {
            return;
        }

        if (!ImGui::Begin("Project Settings", &open))
        {
            ImGui::End();
            return;
        }

        const auto& pm = Project::ProjectManager::GetInstance();
        if (!pm.HasOpenProject())
        {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "No project is open.");
            ImGui::End();
            return;
        }

        const std::filesystem::path projectRoot = pm.GetProjectRoot();

        if (!EnsureLoaded(state, projectRoot))
        {
            // Show status below.
        }

        if (ImGui::BeginTabBar("ProjectSettingsTabs"))
        {
            if (ImGui::BeginTabItem("Render"))
            {
                ImGui::Checkbox("VSync", &state.Render.VSync);
                ImGui::SliderInt("MSAA Samples", &state.Render.MsaaSamples, 1, 8);
                ImGui::SliderFloat("Render Scale", &state.Render.RenderScale, 0.5f, 2.0f, "%.2f");
                ImGui::ColorEdit4("Clear Color", state.Render.ClearColor, ImGuiColorEditFlags_Float);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Audio"))
            {
                ImGui::Checkbox("Muted", &state.Audio.Muted);
                ImGui::SliderFloat("Master Volume", &state.Audio.MasterVolume, 0.0f, 1.0f, "%.2f");
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Input"))
            {
                ImGui::TextDisabled("Project Input Actions");
                ImGui::Separator();

                static std::array<char, 512> keyBuffer{};
                if (ImGui::IsWindowAppearing())
                {
                    std::snprintf(keyBuffer.data(), keyBuffer.size(), "%s", state.Input.ProjectInputActionsKey.c_str());
                }

                ImGui::InputText("InputActions Asset Key", keyBuffer.data(), keyBuffer.size());
                if (ImGui::Button("Apply Key", ImVec2(120, 0)))
                {
                    state.Input.ProjectInputActionsKey = keyBuffer.data();
                }

                ImGui::SameLine();
                if (ImGui::Button("Load Now", ImVec2(120, 0)))
                {
                    if (!state.Input.ProjectInputActionsKey.empty())
                    {
                        InputSystem::GetInstance().SetProjectActionAssetFromKey(state.Input.ProjectInputActionsKey);
                        SetStatus(state, false, "InputActions loaded into InputSystem.");
                    }
                }

                ImGui::TextDisabled("Tip: store keys like 'Assets/InputActions/Sandbox.inputactions.json'.");
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Layers"))
            {
                DrawLayersEditor(state.Layers);
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::Separator();

        if (ImGui::Button("Reload From Disk", ImVec2(160, 0)))
        {
            state.Loaded = false;
            state.StatusMessage.clear();
            state.StatusIsError = false;
            (void)EnsureLoaded(state, projectRoot);
        }

        ImGui::SameLine();

        if (ImGui::Button("Save Settings", ImVec2(160, 0)))
        {
            const auto sr = Project::SaveRenderSettings(projectRoot, state.Render);
            const auto sa = Project::SaveAudioSettings(projectRoot, state.Audio);
            const auto si = Project::SaveInputSettings(projectRoot, state.Input);
            const auto sl = Project::SaveLayersSettings(projectRoot, state.Layers);

            if (sr.IsFailure()) { SetStatus(state, true, sr.GetError().GetErrorMessage()); }
            else if (sa.IsFailure()) { SetStatus(state, true, sa.GetError().GetErrorMessage()); }
            else if (si.IsFailure()) { SetStatus(state, true, si.GetError().GetErrorMessage()); }
            else if (sl.IsFailure()) { SetStatus(state, true, sl.GetError().GetErrorMessage()); }
            else { SetStatus(state, false, "Project settings saved."); }

            // Best-effort apply: if a key is set, load it so project defaults are live in the editor session.
            if (!state.Input.ProjectInputActionsKey.empty())
            {
                InputSystem::GetInstance().SetProjectActionAssetFromKey(state.Input.ProjectInputActionsKey);
            }
        }

        if (!state.StatusMessage.empty())
        {
            const ImVec4 color = state.StatusIsError ? ImVec4(1.0f, 0.35f, 0.35f, 1.0f) : ImVec4(0.35f, 1.0f, 0.35f, 1.0f);
            ImGui::SameLine();
            ImGui::TextColored(color, "%s", state.StatusMessage.c_str());
        }

        ImGui::End();
    }
}

