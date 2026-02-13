#include "EditorProjectSettingsPanel.h"

#include "EditorAssetNaming.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetTypes.h"
#include "Core/Input/InputSystem.h"
#include "Project/ProjectManager.h"

#include "imgui/imgui.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <set>
#include <string>

namespace Limitless::EditorProjectSettingsPanel
{
    namespace
    {
        constexpr const char* kInputActionsSuffix = ".inputactions.json";

        std::string InputActionsDisplayNameFromKey(const std::string& assetKey)
        {
            return EditorAssetNaming::GetAssetDisplayNameFromAssetKey(assetKey);
        }

        std::string MakeUniqueInputActionsAlias(const Project::InputSettings& settings, const std::string& desiredAlias)
        {
            std::string baseAlias = desiredAlias.empty() ? "InputActions" : desiredAlias;
            std::string candidateAlias = baseAlias;
            int32_t suffix = 2;
            auto canonicalizeAlias = [](const std::string& alias) {
                std::string lowered = alias;
                std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char character) {
                    return static_cast<char>(std::tolower(character));
                });
                return lowered;
            };
            auto aliasExists = [&](const std::string& alias) {
                const std::string canonical = canonicalizeAlias(alias);
                return std::any_of(settings.AdditionalInputActionsAssets.begin(), settings.AdditionalInputActionsAssets.end(),
                                   [&](const Project::InputActionsAssetAliasEntry& entry) {
                                       return canonicalizeAlias(entry.Alias) == canonical;
                                   });
            };

            while (aliasExists(candidateAlias))
            {
                candidateAlias = baseAlias + std::to_string(suffix);
                ++suffix;
            }

            return candidateAlias;
        }

        std::vector<std::string> DiscoverInputActionsAssetKeys(const std::filesystem::path& projectRoot)
        {
            std::vector<std::string> keys;
            const std::filesystem::path assetsDirectory = projectRoot / "Assets";
            std::error_code errorCode;
            if (!std::filesystem::exists(assetsDirectory, errorCode) || !std::filesystem::is_directory(assetsDirectory, errorCode))
                return keys;

            auto& assetDatabase = Assets::AssetDatabase::GetInstance();
            for (std::filesystem::recursive_directory_iterator iterator(assetsDirectory, std::filesystem::directory_options::skip_permission_denied, errorCode), end;
                 iterator != end;
                 iterator.increment(errorCode))
            {
                if (errorCode)
                {
                    errorCode.clear();
                    continue;
                }

                const std::filesystem::path currentPath = iterator->path();
                const std::string fileName = currentPath.filename().string();
                if (iterator->is_directory(errorCode))
                {
                    if (fileName == "Cache")
                        iterator.disable_recursion_pending();
                    continue;
                }

                if (!iterator->is_regular_file(errorCode))
                    continue;
                if (!EditorAssetNaming::EndsWithCaseInsensitive(fileName, kInputActionsSuffix))
                    continue;

                std::filesystem::path relativePath = std::filesystem::relative(currentPath, assetsDirectory, errorCode);
                if (errorCode)
                {
                    errorCode.clear();
                    continue;
                }

                const std::string assetKey = "Assets/" + relativePath.generic_string();
                keys.push_back(assetKey);
                (void)assetDatabase.ImportOrUpdate(assetKey, Assets::AssetType::InputActions);
            }

            std::sort(keys.begin(), keys.end());
            keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
            return keys;
        }

        void RefreshInputActionsAssetKeys(EditorProjectSettingsPanelState& state, const std::filesystem::path& projectRoot)
        {
            state.AvailableInputActionsAssetKeys = DiscoverInputActionsAssetKeys(projectRoot);

            // Keep currently referenced keys visible even when the file is missing,
            // so users can diagnose and replace missing links without losing context.
            std::set<std::string> uniqueKeys(state.AvailableInputActionsAssetKeys.begin(), state.AvailableInputActionsAssetKeys.end());
            if (!state.Input.ProjectInputActionsKey.empty())
                uniqueKeys.insert(state.Input.ProjectInputActionsKey);
            for (const auto& key : Project::CollectAdditionalInputActionsAssetKeys(state.Input))
            {
                if (!key.empty())
                    uniqueKeys.insert(key);
            }

            state.AvailableInputActionsAssetKeys.assign(uniqueKeys.begin(), uniqueKeys.end());
            state.SelectedAvailableInputActionsIndex = std::clamp(
                state.SelectedAvailableInputActionsIndex,
                -1,
                static_cast<int>(state.AvailableInputActionsAssetKeys.size()) - 1);
            state.SelectedAdditionalInputActionsIndex = std::clamp(
                state.SelectedAdditionalInputActionsIndex,
                -1,
                static_cast<int>(state.Input.AdditionalInputActionsAssets.size()) - 1);
            if (state.SelectedAdditionalInputActionsIndex != state.SelectedAdditionalInputAliasBufferSourceIndex)
            {
                if (state.SelectedAdditionalInputActionsIndex >= 0 &&
                    state.SelectedAdditionalInputActionsIndex < static_cast<int>(state.Input.AdditionalInputActionsAssets.size()))
                {
                    const std::string& alias = state.Input.AdditionalInputActionsAssets[static_cast<size_t>(state.SelectedAdditionalInputActionsIndex)].Alias;
                    std::snprintf(state.SelectedAdditionalInputAliasBuffer.data(),
                                  state.SelectedAdditionalInputAliasBuffer.size(),
                                  "%s",
                                  alias.c_str());
                }
                else
                {
                    state.SelectedAdditionalInputAliasBuffer[0] = '\0';
                }
                state.SelectedAdditionalInputAliasBufferSourceIndex = state.SelectedAdditionalInputActionsIndex;
            }
        }

        void ApplyInputSettingsToRuntime(const Project::InputSettings& settings)
        {
            auto& inputSystem = InputSystem::GetInstance();
            if (settings.ProjectInputActionsKey.empty())
                inputSystem.SetProjectActionAsset(nullptr);
            else
                inputSystem.SetProjectActionAssetFromKey(settings.ProjectInputActionsKey);
            inputSystem.SetProjectAdditionalActionAssetsFromKeys(Project::CollectAdditionalInputActionsAssetKeys(settings));
        }

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
            RefreshInputActionsAssetKeys(state, projectRoot);
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
                ImGui::TextDisabled("Project-Wide Input Actions");
                ImGui::Separator();

                const bool hasDefault = !state.Input.ProjectInputActionsKey.empty();
                const std::string defaultDisplay = hasDefault
                    ? InputActionsDisplayNameFromKey(state.Input.ProjectInputActionsKey)
                    : std::string("None");
                if (ImGui::BeginCombo("Default InputActions", defaultDisplay.c_str()))
                {
                    const bool selectedNone = state.Input.ProjectInputActionsKey.empty();
                    if (ImGui::Selectable("None", selectedNone))
                        state.Input.ProjectInputActionsKey.clear();

                    for (const auto& key : state.AvailableInputActionsAssetKeys)
                    {
                        const bool selected = (key == state.Input.ProjectInputActionsKey);
                        const std::string label = InputActionsDisplayNameFromKey(key) + "##DefaultInputActions:" + key;
                        if (ImGui::Selectable(label.c_str(), selected))
                            state.Input.ProjectInputActionsKey = key;
                    }

                    ImGui::EndCombo();
                }

                if (ImGui::Button("Apply To Runtime", ImVec2(150, 0)))
                {
                    ApplyInputSettingsToRuntime(state.Input);
                    SetStatus(state, false, "Input actions loaded into runtime InputSystem.");
                }

                ImGui::SameLine();
                if (ImGui::Button("Refresh Asset List", ImVec2(150, 0)))
                {
                    RefreshInputActionsAssetKeys(state, projectRoot);
                }

                ImGui::Spacing();
                ImGui::TextDisabled("Additional InputActions Assets");
                ImGui::Separator();

                if (ImGui::BeginListBox("##AdditionalInputActionsList", ImVec2(-1.0f, 140.0f)))
                {
                    for (int32_t index = 0; index < static_cast<int32_t>(state.Input.AdditionalInputActionsAssets.size()); ++index)
                    {
                        const Project::InputActionsAssetAliasEntry& entry = state.Input.AdditionalInputActionsAssets[static_cast<size_t>(index)];
                        const bool selected = (state.SelectedAdditionalInputActionsIndex == index);
                        const std::string label = entry.Alias + " -> " + InputActionsDisplayNameFromKey(entry.AssetKey) +
                                                  "##AdditionalInputActions:" + entry.AssetKey + ":" + std::to_string(index);
                        if (ImGui::Selectable(label.c_str(), selected))
                            state.SelectedAdditionalInputActionsIndex = index;
                    }
                    ImGui::EndListBox();
                }

                const char* availablePreview = "Select asset";
                if (state.SelectedAvailableInputActionsIndex >= 0 &&
                    state.SelectedAvailableInputActionsIndex < static_cast<int>(state.AvailableInputActionsAssetKeys.size()))
                {
                    availablePreview = state.AvailableInputActionsAssetKeys[static_cast<size_t>(state.SelectedAvailableInputActionsIndex)].c_str();
                }

                if (ImGui::BeginCombo("Available InputActions", availablePreview))
                {
                    for (int32_t index = 0; index < static_cast<int32_t>(state.AvailableInputActionsAssetKeys.size()); ++index)
                    {
                        const std::string& key = state.AvailableInputActionsAssetKeys[static_cast<size_t>(index)];
                        const bool selected = (state.SelectedAvailableInputActionsIndex == index);
                        const std::string label = InputActionsDisplayNameFromKey(key) + "##AvailableInputActions:" + key;
                        if (ImGui::Selectable(label.c_str(), selected))
                            state.SelectedAvailableInputActionsIndex = index;
                    }
                    ImGui::EndCombo();
                }

                if (ImGui::Button("Add Selected Asset", ImVec2(150, 0)))
                {
                    if (state.SelectedAvailableInputActionsIndex >= 0 &&
                        state.SelectedAvailableInputActionsIndex < static_cast<int>(state.AvailableInputActionsAssetKeys.size()))
                    {
                        const std::string& key = state.AvailableInputActionsAssetKeys[static_cast<size_t>(state.SelectedAvailableInputActionsIndex)];
                        bool alreadyAssigned = false;
                        for (const auto& entry : state.Input.AdditionalInputActionsAssets)
                        {
                            if (entry.AssetKey == key)
                            {
                                alreadyAssigned = true;
                                break;
                            }
                        }

                        if (!alreadyAssigned)
                        {
                            Project::InputActionsAssetAliasEntry entry{};
                            entry.AssetKey = key;
                            entry.Alias = MakeUniqueInputActionsAlias(state.Input, InputActionsDisplayNameFromKey(key));
                            state.Input.AdditionalInputActionsAssets.push_back(std::move(entry));
                            state.SelectedAdditionalInputActionsIndex = static_cast<int>(state.Input.AdditionalInputActionsAssets.size()) - 1;
                            state.SelectedAdditionalInputAliasBufferSourceIndex = -1;
                        }
                    }
                }

                ImGui::SameLine();
                const bool canRemoveSelected = state.SelectedAdditionalInputActionsIndex >= 0 &&
                                               state.SelectedAdditionalInputActionsIndex < static_cast<int>(state.Input.AdditionalInputActionsAssets.size());
                ImGui::BeginDisabled(!canRemoveSelected);
                if (ImGui::Button("Remove Selected", ImVec2(150, 0)))
                {
                    state.Input.AdditionalInputActionsAssets.erase(
                        state.Input.AdditionalInputActionsAssets.begin() + state.SelectedAdditionalInputActionsIndex);
                    if (state.Input.AdditionalInputActionsAssets.empty())
                        state.SelectedAdditionalInputActionsIndex = -1;
                    else
                        state.SelectedAdditionalInputActionsIndex = std::clamp(
                            state.SelectedAdditionalInputActionsIndex,
                            0,
                            static_cast<int>(state.Input.AdditionalInputActionsAssets.size()) - 1);
                    state.SelectedAdditionalInputAliasBufferSourceIndex = -1;
                }
                ImGui::EndDisabled();

                if (canRemoveSelected)
                {
                    Project::InputActionsAssetAliasEntry& selectedEntry =
                        state.Input.AdditionalInputActionsAssets[static_cast<size_t>(state.SelectedAdditionalInputActionsIndex)];

                    ImGui::InputText("Alias", state.SelectedAdditionalInputAliasBuffer.data(), state.SelectedAdditionalInputAliasBuffer.size());
                    if (ImGui::Button("Apply Alias", ImVec2(150, 0)))
                    {
                        const std::string desiredAlias = state.SelectedAdditionalInputAliasBuffer.data();
                        if (!desiredAlias.empty())
                        {
                            std::string uniqueAlias = desiredAlias;
                            auto canonicalizeAlias = [](const std::string& alias) {
                                std::string lowered = alias;
                                std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char character) {
                                    return static_cast<char>(std::tolower(character));
                                });
                                return lowered;
                            };
                            for (size_t index = 0; index < state.Input.AdditionalInputActionsAssets.size(); ++index)
                            {
                                if (static_cast<int>(index) == state.SelectedAdditionalInputActionsIndex)
                                    continue;
                                if (canonicalizeAlias(state.Input.AdditionalInputActionsAssets[index].Alias) == canonicalizeAlias(uniqueAlias))
                                {
                                    uniqueAlias = MakeUniqueInputActionsAlias(state.Input, desiredAlias);
                                    break;
                                }
                            }
                            selectedEntry.Alias = uniqueAlias;
                            std::snprintf(state.SelectedAdditionalInputAliasBuffer.data(),
                                          state.SelectedAdditionalInputAliasBuffer.size(),
                                          "%s",
                                          selectedEntry.Alias.c_str());
                        }
                    }
                }

                ImGui::TextDisabled("Code can resolve these assets by alias via Project::ResolveInputActionsAssetKeyByAlias(...).");
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
            state.SelectedAvailableInputActionsIndex = -1;
            state.SelectedAdditionalInputActionsIndex = -1;
            state.SelectedAdditionalInputAliasBufferSourceIndex = -1;
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

            // Best-effort apply so project defaults are live in the editor session.
            ApplyInputSettingsToRuntime(state.Input);
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

