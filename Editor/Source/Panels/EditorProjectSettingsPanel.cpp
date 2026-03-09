#include "EditorProjectSettingsPanel.h"

#include "EditorAssetNaming.h"
#include "EditorPanelStyle.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetTypes.h"
#include "Core/Concurrency/JobSystem.h"
#include "Core/ConfigManager.h"
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
        constexpr const char* kAudioMixerSuffix = ".audiomixer.json";

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

        std::vector<std::string> DiscoverAudioMixerAssetKeys(const std::filesystem::path& projectRoot)
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
                if (!EditorAssetNaming::EndsWithCaseInsensitive(fileName, kAudioMixerSuffix))
                    continue;

                std::filesystem::path relativePath = std::filesystem::relative(currentPath, assetsDirectory, errorCode);
                if (errorCode)
                {
                    errorCode.clear();
                    continue;
                }

                const std::string assetKey = "Assets/" + relativePath.generic_string();
                keys.push_back(assetKey);
                (void)assetDatabase.ImportOrUpdate(assetKey, Assets::AssetType::AudioMixer);
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

        void RefreshAudioMixerAssetKeys(EditorProjectSettingsPanelState& state, const std::filesystem::path& projectRoot)
        {
            state.AvailableAudioMixerAssetKeys = DiscoverAudioMixerAssetKeys(projectRoot);
            std::set<std::string> uniqueKeys(
                state.AvailableAudioMixerAssetKeys.begin(),
                state.AvailableAudioMixerAssetKeys.end());
            if (!state.Audio.MixerAssetKey.empty())
                uniqueKeys.insert(state.Audio.MixerAssetKey);
            state.AvailableAudioMixerAssetKeys.assign(uniqueKeys.begin(), uniqueKeys.end());
            state.SelectedAudioMixerAssetIndex = std::clamp(
                state.SelectedAudioMixerAssetIndex,
                -1,
                static_cast<int>(state.AvailableAudioMixerAssetKeys.size()) - 1);
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

        void DrawEcsMultithreadingRuntimeEditor(EditorProjectSettingsPanelState& state)
        {
            auto& config = ConfigManager::GetInstance();
            auto& jobSystem = Concurrency::GetJobSystem();

            auto drawToggle = [&](const char* label, const char* widgetId, const char* configKey, bool defaultValue, const char* tooltip = nullptr) {
                bool enabled = config.GetValue<bool>(configKey, defaultValue);
                ImGui::TextUnformatted(label);
                if (ImGui::Checkbox(widgetId, &enabled))
                    config.SetValue<bool>(configKey, enabled);
                if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip("%s", tooltip);
            };

            drawToggle("Enable System Scheduler",
                       "##EcsMtEnableSystemScheduler",
                       "ecs.mt.enable_system_scheduler",
                       true,
                       "Compatibility-based simulation barrier scheduling.");
            drawToggle("Enable Parallel Scripts",
                       "##EcsMtEnableParallelScripts",
                       "ecs.mt.enable_parallel_scripts",
                       true,
                       "Allows ParallelSafe scripts to run on workers.");
            drawToggle("Require Parallel Script Access Declarations",
                       "##EcsMtRequireParallelScriptAccessDeclarations",
                       "ecs.mt.require_parallel_script_access_declarations",
                       true,
                       "If enabled, undeclared ParallelSafe scripts stay on the main thread.");
            drawToggle("Warn Implicit Parallel Script Access",
                       "##EcsMtWarnImplicitParallelScriptAccess",
                       "ecs.mt.warn_implicit_parallel_script_access",
                       true);
            drawToggle("Validate Parallel Script Access Masks",
                       "##EcsMtValidateParallelScriptAccessMasks",
                       "ecs.mt.validate_parallel_script_access_masks",
                       true);
            drawToggle("Warn Parallel Script Access Mismatch",
                       "##EcsMtWarnParallelScriptAccessMismatch",
                       "ecs.mt.warn_parallel_script_access_mismatch",
                       true);
            drawToggle("Enable Parallel Physics World Step",
                       "##EcsMtEnableParallelPhysicsWorldStep",
                       "ecs.mt.enable_parallel_physics_world_step",
                       true);
            drawToggle("Enable Parallel Transforms",
                       "##EcsMtEnableParallelTransforms",
                       "ecs.mt.enable_parallel_transforms",
                       true);

            ImGui::Separator();
            ImGui::TextDisabled("Parallel script job heuristics");

            int minSlots = static_cast<int>(config.GetValue<size_t>("ecs.mt.parallel_script_min_slots", 0));
            ImGui::TextUnformatted("Parallel Script Min Slots (0 = auto)");
            if (ImGui::DragInt("##EcsMtParallelScriptMinSlots", &minSlots, 1.0f, 0, 4096))
                config.SetValue<size_t>("ecs.mt.parallel_script_min_slots", static_cast<size_t>(std::max(0, minSlots)));

            int minSlotsPerWorker = static_cast<int>(config.GetValue<size_t>("ecs.mt.parallel_script_min_slots_per_worker", 2));
            ImGui::TextUnformatted("Parallel Script Min Slots Per Worker");
            if (ImGui::DragInt("##EcsMtParallelScriptMinSlotsPerWorker", &minSlotsPerWorker, 1.0f, 1, 64))
                config.SetValue<size_t>("ecs.mt.parallel_script_min_slots_per_worker", static_cast<size_t>(std::max(1, minSlotsPerWorker)));

            int minBatchSize = static_cast<int>(config.GetValue<size_t>("ecs.mt.parallel_script_min_batch_size", 2));
            ImGui::TextUnformatted("Parallel Script Min Batch Size");
            if (ImGui::DragInt("##EcsMtParallelScriptMinBatchSize", &minBatchSize, 1.0f, 2, 256))
                config.SetValue<size_t>("ecs.mt.parallel_script_min_batch_size", static_cast<size_t>(std::max(2, minBatchSize)));

            const size_t autoSlotsPerWorker =
                std::max<size_t>(1, config.GetValue<size_t>("ecs.mt.parallel_script_min_slots_per_worker", 2));
            const size_t workerCount = std::max<size_t>(1, jobSystem.GetWorkerCount());
            const size_t autoThreshold = std::max<size_t>(2, workerCount * autoSlotsPerWorker);
            if (minSlots <= 0)
                ImGui::TextDisabled("Auto threshold: %zu slots (%zu workers x %zu per worker)", autoThreshold, workerCount, autoSlotsPerWorker);

            if (ImGui::Button("Reset ECS MT Defaults", ImVec2(200, 0)))
            {
                config.SetValue<bool>("ecs.mt.enable_system_scheduler", true);
                config.SetValue<bool>("ecs.mt.enable_parallel_scripts", true);
                config.SetValue<bool>("ecs.mt.require_parallel_script_access_declarations", true);
                config.SetValue<bool>("ecs.mt.warn_implicit_parallel_script_access", true);
                config.SetValue<bool>("ecs.mt.validate_parallel_script_access_masks", true);
                config.SetValue<bool>("ecs.mt.warn_parallel_script_access_mismatch", true);
                config.SetValue<bool>("ecs.mt.enable_parallel_physics_world_step", true);
                config.SetValue<bool>("ecs.mt.enable_parallel_transforms", true);
                config.SetValue<size_t>("ecs.mt.parallel_script_min_slots", 0);
                config.SetValue<size_t>("ecs.mt.parallel_script_min_slots_per_worker", 2);
                config.SetValue<size_t>("ecs.mt.parallel_script_min_batch_size", 2);
                SetStatus(state, false, "Reset ECS MT runtime settings to defaults.");
            }
            ImGui::SameLine();
            if (ImGui::Button("Save Runtime Config", ImVec2(170, 0)))
            {
                if (config.SaveToFile())
                    SetStatus(state, false, "Saved runtime config to config.json.");
                else
                    SetStatus(state, true, "Failed to save runtime config.");
            }
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
            const auto p = Project::LoadPhysics2DSettings(projectRoot);
            const auto lighting = Project::LoadLighting2DSettings(projectRoot);

            if (r.IsFailure()) { SetStatus(state, true, r.GetError().GetErrorMessage()); return false; }
            if (a.IsFailure()) { SetStatus(state, true, a.GetError().GetErrorMessage()); return false; }
            if (i.IsFailure()) { SetStatus(state, true, i.GetError().GetErrorMessage()); return false; }
            if (l.IsFailure()) { SetStatus(state, true, l.GetError().GetErrorMessage()); return false; }
            if (p.IsFailure()) { SetStatus(state, true, p.GetError().GetErrorMessage()); return false; }
            if (lighting.IsFailure()) { SetStatus(state, true, lighting.GetError().GetErrorMessage()); return false; }

            state.Render = r.GetValue();
            state.Audio = a.GetValue();
            state.Input = i.GetValue();
            state.Layers = l.GetValue();
            state.Physics2D = p.GetValue();
            state.Lighting2D = lighting.GetValue();
            RefreshAudioMixerAssetKeys(state, projectRoot);
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

                ImGui::TextUnformatted("Name");
                ImGui::InputText("##LayerName", nameBuffer.data(), nameBuffer.size());
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

        EditorPanelStyle::PushPanelVisualStyle();
        if (!ImGui::Begin("Project Settings", &open))
        {
            ImGui::End();
            EditorPanelStyle::PopPanelVisualStyle();
            return;
        }

        const auto& pm = Project::ProjectManager::GetInstance();
        if (!pm.HasOpenProject())
        {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "No project is open.");
            ImGui::End();
            EditorPanelStyle::PopPanelVisualStyle();
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
                ImGui::TextUnformatted("VSync");
                ImGui::Checkbox("##RenderVSync", &state.Render.VSync);
                ImGui::TextUnformatted("MSAA Samples");
                ImGui::SliderInt("##RenderMsaaSamples", &state.Render.MsaaSamples, 1, 8);
                ImGui::TextUnformatted("Render Scale");
                ImGui::SliderFloat("##RenderScale", &state.Render.RenderScale, 0.5f, 2.0f, "%.2f");
                ImGui::TextUnformatted("Clear Color");
                ImGui::ColorEdit4("##RenderClearColor", state.Render.ClearColor, ImGuiColorEditFlags_Float);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Audio"))
            {
                ImGui::TextUnformatted("Muted");
                ImGui::Checkbox("##AudioMuted", &state.Audio.Muted);
                ImGui::TextUnformatted("Master Volume");
                ImGui::SliderFloat("##AudioMasterVolume", &state.Audio.MasterVolume, 0.0f, 1.0f, "%.2f");

                const bool hasAudioMixer = !state.Audio.MixerAssetKey.empty();
                const std::string audioMixerDisplayName = hasAudioMixer
                    ? InputActionsDisplayNameFromKey(state.Audio.MixerAssetKey)
                    : std::string("None");
                ImGui::TextUnformatted("Audio Mixer Asset");
                if (ImGui::BeginCombo("##AudioMixerAsset", audioMixerDisplayName.c_str()))
                {
                    const bool selectedNone = state.Audio.MixerAssetKey.empty();
                    if (ImGui::Selectable("None", selectedNone))
                        state.Audio.MixerAssetKey.clear();

                    for (const auto& key : state.AvailableAudioMixerAssetKeys)
                    {
                        const bool selected = (key == state.Audio.MixerAssetKey);
                        const std::string label = InputActionsDisplayNameFromKey(key) + "##AudioMixerAsset:" + key;
                        if (ImGui::Selectable(label.c_str(), selected))
                            state.Audio.MixerAssetKey = key;
                    }
                    ImGui::EndCombo();
                }

                if (ImGui::Button("Refresh Audio Mixer Assets", ImVec2(220, 0)))
                {
                    RefreshAudioMixerAssetKeys(state, projectRoot);
                }
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
                ImGui::TextUnformatted("Default InputActions");
                if (ImGui::BeginCombo("##DefaultInputActions", defaultDisplay.c_str()))
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

                ImGui::TextUnformatted("Available InputActions");
                if (ImGui::BeginCombo("##AvailableInputActions", availablePreview))
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

                    ImGui::TextUnformatted("Alias");
                    ImGui::InputText("##AdditionalInputAlias", state.SelectedAdditionalInputAliasBuffer.data(), state.SelectedAdditionalInputAliasBuffer.size());
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

            if (ImGui::BeginTabItem("Physics 2D"))
            {
                ImGui::TextUnformatted("Gravity");
                EditorPanelStyle::DragFloatNWithAxisLabels("##PhysicsGravity", &state.Physics2D.GravityX, 2, 0.05f, -100.0f, 100.0f);
                ImGui::TextUnformatted("Velocity Sub Steps");
                ImGui::SliderInt("##PhysicsVelocitySubSteps", &state.Physics2D.VelocitySubSteps, 1, 24);
                ImGui::TextUnformatted("Enable Sleep");
                ImGui::Checkbox("##PhysicsEnableSleep", &state.Physics2D.EnableSleep);
                ImGui::TextUnformatted("Enable Continuous Collision");
                ImGui::Checkbox("##PhysicsEnableContinuousCollision", &state.Physics2D.EnableContinuousCollision);
                ImGui::TextUnformatted("High Contact Quality Mode");
                ImGui::Checkbox("##PhysicsHighContactQualityMode", &state.Physics2D.HighContactQualityMode);
                ImGui::TextUnformatted("High Quality Extra Sub Steps");
                ImGui::DragInt("##PhysicsHighQualityExtraSubSteps", &state.Physics2D.HighContactQualityExtraSubSteps, 1.0f, 0, 24);
                state.Physics2D.HighContactQualityExtraSubSteps = std::max(0, state.Physics2D.HighContactQualityExtraSubSteps);
                ImGui::TextUnformatted("Contact Hertz");
                ImGui::DragFloat("##PhysicsContactHertz", &state.Physics2D.ContactHertz, 0.5f, 1.0f, 240.0f);
                ImGui::TextUnformatted("Contact Damping Ratio");
                ImGui::DragFloat("##PhysicsContactDampingRatio", &state.Physics2D.ContactDampingRatio, 0.01f, 0.0f, 2.0f);
                ImGui::TextUnformatted("Contact Push Speed");
                ImGui::DragFloat("##PhysicsContactPushSpeed", &state.Physics2D.ContactPushSpeed, 0.1f, 0.1f, 64.0f);
                ImGui::TextDisabled("Higher sub-steps/contact tuning reduces clipping on fast/rotating collisions.");
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Lighting 2D"))
            {
                ImGui::TextUnformatted("Enabled");
                ImGui::Checkbox("##LightingEnabled", &state.Lighting2D.Enabled);
                ImGui::TextUnformatted("Enable Normal Maps");
                ImGui::Checkbox("##LightingEnableNormalMaps", &state.Lighting2D.EnableNormalMaps);
                ImGui::TextUnformatted("Enable Shadows");
                ImGui::Checkbox("##LightingEnableShadows", &state.Lighting2D.EnableShadows);
                ImGui::TextUnformatted("Ambient Color");
                ImGui::ColorEdit3("##LightingAmbientColor", state.Lighting2D.AmbientColor);
                ImGui::TextUnformatted("Ambient Intensity");
                ImGui::SliderFloat("##LightingAmbientIntensity", &state.Lighting2D.AmbientIntensity, 0.0f, 4.0f, "%.2f");
                ImGui::TextUnformatted("Shadow Quality");
                ImGui::SliderInt("##LightingShadowQuality", &state.Lighting2D.ShadowQualityLevel, 0, 2);
                ImGui::TextUnformatted("Max Directional Lights");
                ImGui::DragInt("##LightingMaxDirectionalLights", &state.Lighting2D.MaxDirectionalLights, 1.0f, 0, 32);
                ImGui::TextUnformatted("Max Point Lights");
                ImGui::DragInt("##LightingMaxPointLights", &state.Lighting2D.MaxPointLights, 1.0f, 0, 256);
                ImGui::TextUnformatted("Max Shadow Segments");
                ImGui::DragInt("##LightingMaxShadowSegments", &state.Lighting2D.MaxShadowSegments, 1.0f, 8, 512);
                ImGui::TextUnformatted("Shadow Softness Scale");
                ImGui::DragFloat("##LightingShadowSoftnessScale", &state.Lighting2D.ShadowSoftnessScale, 0.01f, 0.0f, 16.0f, "%.2f");
                ImGui::TextUnformatted("Directional Shadow Bias Scale");
                ImGui::DragFloat("##LightingDirectionalShadowBiasScale", &state.Lighting2D.DirectionalShadowBiasScale, 0.01f, 0.0f, 8.0f, "%.2f");
                ImGui::TextUnformatted("Shadow Alpha Cutoff");
                ImGui::SliderFloat("##LightingShadowAlphaCutoff", &state.Lighting2D.ShadowAlphaCutoff, 0.0f, 1.0f, "%.2f");
                ImGui::TextUnformatted("Shadow Segment Snap Pixels");
                ImGui::DragFloat("##LightingShadowSegmentSnapPixels", &state.Lighting2D.ShadowSegmentSnapPixels, 0.05f, 0.0f, 4.0f, "%.2f");
                ImGui::TextUnformatted("Enable High Angular Velocity Shadow Freeze");
                ImGui::Checkbox("##LightingEnableShadowFreeze", &state.Lighting2D.EnableHighAngularVelocityShadowFreeze);
                ImGui::TextUnformatted("Shadow Freeze Angular Velocity (Deg/Sec)");
                ImGui::DragFloat("##LightingShadowFreezeAngularVelocity", &state.Lighting2D.ShadowFreezeAngularVelocityDegreesPerSecond, 1.0f, 1.0f, 1440.0f, "%.1f");
                ImGui::TextUnformatted("Shadow Freeze Frame Count");
                ImGui::DragInt("##LightingShadowFreezeFrameCount", &state.Lighting2D.ShadowFreezeFrameCount, 1.0f, 1, 16);
                ImGui::TextUnformatted("Max Shadow Samples Per Light");
                ImGui::DragInt("##LightingMaxShadowSamplesPerLight", &state.Lighting2D.MaxShadowSamplesPerLight, 1.0f, 1, 32);

                state.Lighting2D.ShadowQualityLevel = std::clamp(state.Lighting2D.ShadowQualityLevel, 0, 2);
                state.Lighting2D.MaxDirectionalLights = std::max(0, state.Lighting2D.MaxDirectionalLights);
                state.Lighting2D.MaxPointLights = std::max(0, state.Lighting2D.MaxPointLights);
                state.Lighting2D.MaxShadowSegments = std::max(1, state.Lighting2D.MaxShadowSegments);
                state.Lighting2D.ShadowSoftnessScale = std::max(0.0f, state.Lighting2D.ShadowSoftnessScale);
                state.Lighting2D.DirectionalShadowBiasScale = std::max(0.0f, state.Lighting2D.DirectionalShadowBiasScale);
                state.Lighting2D.ShadowAlphaCutoff = std::clamp(state.Lighting2D.ShadowAlphaCutoff, 0.0f, 1.0f);
                state.Lighting2D.ShadowSegmentSnapPixels = std::max(0.0f, state.Lighting2D.ShadowSegmentSnapPixels);
                state.Lighting2D.ShadowFreezeAngularVelocityDegreesPerSecond = std::max(1.0f, state.Lighting2D.ShadowFreezeAngularVelocityDegreesPerSecond);
                state.Lighting2D.ShadowFreezeFrameCount = std::max(1, state.Lighting2D.ShadowFreezeFrameCount);
                state.Lighting2D.MaxShadowSamplesPerLight = std::max(1, state.Lighting2D.MaxShadowSamplesPerLight);
                state.Lighting2D.AmbientIntensity = std::max(0.0f, state.Lighting2D.AmbientIntensity);

                ImGui::TextDisabled("Quality 0=Low, 1=Medium, 2=High.");
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("ECS MT (Runtime)"))
            {
                DrawEcsMultithreadingRuntimeEditor(state);
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::Separator();

        if (ImGui::Button("Reload From Disk", ImVec2(160, 0)))
        {
            state.Loaded = false;
            state.SelectedAudioMixerAssetIndex = -1;
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
            const auto sp = Project::SavePhysics2DSettings(projectRoot, state.Physics2D);
            const auto lighting = Project::SaveLighting2DSettings(projectRoot, state.Lighting2D);

            if (sr.IsFailure()) { SetStatus(state, true, sr.GetError().GetErrorMessage()); }
            else if (sa.IsFailure()) { SetStatus(state, true, sa.GetError().GetErrorMessage()); }
            else if (si.IsFailure()) { SetStatus(state, true, si.GetError().GetErrorMessage()); }
            else if (sl.IsFailure()) { SetStatus(state, true, sl.GetError().GetErrorMessage()); }
            else if (sp.IsFailure()) { SetStatus(state, true, sp.GetError().GetErrorMessage()); }
            else if (lighting.IsFailure()) { SetStatus(state, true, lighting.GetError().GetErrorMessage()); }
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
        EditorPanelStyle::PopPanelVisualStyle();
    }
}

