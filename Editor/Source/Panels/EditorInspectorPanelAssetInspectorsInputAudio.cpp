#include "EditorInspectorPanelAssetInspectorsShared.h"

namespace Limitless::EditorInspectorPanel::Internal
{
    void DrawInputActionsAssetInspectorInternal(std::string& selectedInputActionsAssetKey)
    {
        struct State
        {
            std::string LoadedKey;
            std::filesystem::path ResolvedPath;
            nlohmann::json Json = nlohmann::json::object();
            bool Loaded = false;
            bool PendingSave = false;
        };
        static State s_State;

        if (selectedInputActionsAssetKey.empty())
            return;

        if (!s_State.Loaded || s_State.LoadedKey != selectedInputActionsAssetKey)
        {
            s_State = {};
            s_State.LoadedKey = selectedInputActionsAssetKey;
            s_State.Loaded = LoadInputActionsJson(selectedInputActionsAssetKey, s_State.Json, s_State.ResolvedPath);
            if (!s_State.Loaded)
            {
                ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Failed to load input actions JSON: %s", selectedInputActionsAssetKey.c_str());
                return;
            }
        }

        bool modified = false;
        bool saveFailed = false;

        ImGui::Text("Input Actions: %s", std::filesystem::path(selectedInputActionsAssetKey).filename().string().c_str());
        ImGui::TextDisabled("Asset Key: %s", selectedInputActionsAssetKey.c_str());
        ImGui::Separator();

        if (!s_State.Json.contains("maps") || !s_State.Json["maps"].is_array())
        {
            s_State.Json["maps"] = nlohmann::json::array();
            modified = true;
        }

        auto& maps = s_State.Json["maps"];

        ImGui::TextUnformatted("Action Maps");
        if (ImGui::Button("Add Action Map", ImVec2(160.0f, 0.0f)))
        {
            nlohmann::json newMap = nlohmann::json::object();
            newMap["name"] = "NewMap" + std::to_string(maps.size() + 1);
            newMap["enabled"] = true;
            newMap["actions"] = nlohmann::json::array();
            maps.push_back(std::move(newMap));
            modified = true;
        }

        if (maps.empty())
            ImGui::TextDisabled("No action maps yet.");

        int removeMapIndex = -1;

        for (size_t mapIndex = 0; mapIndex < maps.size(); ++mapIndex)
        {
            auto& mapJson = maps[mapIndex];
            if (!mapJson.is_object())
            {
                mapJson = nlohmann::json::object();
                modified = true;
            }

            if (!mapJson.contains("name") || !mapJson["name"].is_string())
            {
                mapJson["name"] = "Map" + std::to_string(mapIndex + 1);
                modified = true;
            }
            if (!mapJson.contains("enabled") || !mapJson["enabled"].is_boolean())
            {
                mapJson["enabled"] = true;
                modified = true;
            }
            if (!mapJson.contains("actions") || !mapJson["actions"].is_array())
            {
                mapJson["actions"] = nlohmann::json::array();
                modified = true;
            }

            std::string mapName = mapJson.value("name", std::string("Map" + std::to_string(mapIndex + 1)));
            const std::string mapLabel = mapName.empty()
                ? ("Map " + std::to_string(mapIndex + 1) + "###InputMap_" + std::to_string(mapIndex))
                : ("Map: " + mapName + "###InputMap_" + std::to_string(mapIndex));

            ImGuiTreeNodeFlags mapFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
            const bool mapOpen = ImGui::TreeNodeEx(mapLabel.c_str(), mapFlags);

            ImGui::SameLine();
            const std::string removeMapButtonLabel = "Remove##RemoveMap_" + std::to_string(mapIndex);
            if (ImGui::Button(removeMapButtonLabel.c_str()))
                removeMapIndex = static_cast<int>(mapIndex);

            if (!mapOpen)
                continue;

            ImGui::PushID(static_cast<int>(mapIndex));

            std::array<char, 128> mapNameBuffer{};
            std::snprintf(mapNameBuffer.data(), mapNameBuffer.size(), "%s", mapName.c_str());
            if (ImGui::InputText("Map Name", mapNameBuffer.data(), mapNameBuffer.size()))
            {
                mapJson["name"] = std::string(mapNameBuffer.data());
                modified = true;
            }

            bool mapEnabled = mapJson.value("enabled", true);
            if (ImGui::Checkbox("Enabled", &mapEnabled))
            {
                mapJson["enabled"] = mapEnabled;
                modified = true;
            }

            auto& actions = mapJson["actions"];
            int removeActionIndex = -1;

            if (ImGui::Button("Add Action", ImVec2(120.0f, 0.0f)))
            {
                nlohmann::json newAction = nlohmann::json::object();
                newAction["name"] = "Action" + std::to_string(actions.size() + 1);
                newAction["type"] = "Button";
                newAction["bindings"] = nlohmann::json::array();
                actions.push_back(std::move(newAction));
                modified = true;
            }

            if (actions.empty())
                ImGui::TextDisabled("No actions in this map.");

            for (size_t actionIndex = 0; actionIndex < actions.size(); ++actionIndex)
            {
                auto& actionJson = actions[actionIndex];
                if (!actionJson.is_object())
                {
                    actionJson = nlohmann::json::object();
                    modified = true;
                }

                if (!actionJson.contains("name") || !actionJson["name"].is_string())
                {
                    actionJson["name"] = "Action" + std::to_string(actionIndex + 1);
                    modified = true;
                }
                if (!actionJson.contains("type") || !actionJson["type"].is_string())
                {
                    actionJson["type"] = "Button";
                    modified = true;
                }
                if (!actionJson.contains("bindings") || !actionJson["bindings"].is_array())
                {
                    actionJson["bindings"] = nlohmann::json::array();
                    modified = true;
                }

                const std::string actionName = actionJson.value("name", std::string("Action" + std::to_string(actionIndex + 1)));
                const std::string actionLabel = actionName.empty()
                    ? ("Action " + std::to_string(actionIndex + 1) + "###InputAction_" + std::to_string(actionIndex))
                    : ("Action: " + actionName + "###InputAction_" + std::to_string(actionIndex));
                const bool actionOpen = ImGui::TreeNodeEx(actionLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);

                ImGui::SameLine();
                const std::string removeActionButtonLabel = "Remove##RemoveAction_" + std::to_string(actionIndex);
                if (ImGui::Button(removeActionButtonLabel.c_str()))
                    removeActionIndex = static_cast<int>(actionIndex);

                if (!actionOpen)
                    continue;

                ImGui::PushID(static_cast<int>(actionIndex));

                std::array<char, 128> actionNameBuffer{};
                std::snprintf(actionNameBuffer.data(), actionNameBuffer.size(), "%s", actionName.c_str());
                if (ImGui::InputText("Action Name", actionNameBuffer.data(), actionNameBuffer.size()))
                {
                    actionJson["name"] = std::string(actionNameBuffer.data());
                    modified = true;
                }

                std::string actionType = actionJson.value("type", std::string("Button"));
                int actionTypeIndex = 0;
                for (size_t typeIndex = 0; typeIndex < kInputActionValueTypes.size(); ++typeIndex)
                {
                    if (actionType == kInputActionValueTypes[typeIndex])
                    {
                        actionTypeIndex = static_cast<int>(typeIndex);
                        break;
                    }
                }
                if (ImGui::Combo("Action Type", &actionTypeIndex, kInputActionValueTypes.data(), static_cast<int>(kInputActionValueTypes.size())))
                {
                    actionJson["type"] = std::string(kInputActionValueTypes[static_cast<size_t>(actionTypeIndex)]);
                    actionType = actionJson["type"].get<std::string>();
                    modified = true;
                }

                auto& bindings = actionJson["bindings"];
                int removeBindingIndex = -1;

                if (ImGui::Button("Add Binding", ImVec2(120.0f, 0.0f)))
                {
                    const std::string defaultBindingType = GetDefaultBindingTypeForActionType(actionType);
                    bindings.push_back(CreateDefaultBindingJson(defaultBindingType));
                    modified = true;
                }

                if (bindings.empty())
                    ImGui::TextDisabled("No bindings on this action.");

                for (size_t bindingIndex = 0; bindingIndex < bindings.size(); ++bindingIndex)
                {
                    auto& bindingJson = bindings[bindingIndex];
                    const std::string bindingLabel = "Binding " + std::to_string(bindingIndex + 1) + "##Binding_" + std::to_string(bindingIndex);
                    const bool bindingOpen = ImGui::TreeNodeEx(bindingLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);

                    ImGui::SameLine();
                    const std::string removeBindingButtonLabel = "Remove##RemoveBinding_" + std::to_string(bindingIndex);
                    if (ImGui::Button(removeBindingButtonLabel.c_str()))
                        removeBindingIndex = static_cast<int>(bindingIndex);

                    if (bindingOpen)
                    {
                        ImGui::PushID(static_cast<int>(bindingIndex));
                        if (DrawInputBindingEditor(bindingJson))
                            modified = true;
                        ImGui::PopID();
                        ImGui::TreePop();
                    }
                }

                if (removeBindingIndex >= 0)
                {
                    bindings.erase(static_cast<size_t>(removeBindingIndex));
                    modified = true;
                }

                ImGui::PopID();
                ImGui::TreePop();
            }

            if (removeActionIndex >= 0)
            {
                actions.erase(static_cast<size_t>(removeActionIndex));
                modified = true;
            }

            ImGui::PopID();
            ImGui::TreePop();
        }

        if (removeMapIndex >= 0)
        {
            maps.erase(static_cast<size_t>(removeMapIndex));
            modified = true;
        }

        if (modified)
            s_State.PendingSave = true;

        // Defer disk save/reimport while user is actively editing widgets (especially text fields)
        // to avoid interrupting ImGui active-item state and dropping keyboard focus.
        if (s_State.PendingSave && !ImGui::IsAnyItemActive())
        {
            if (!SaveInputActionsJsonAndReload(selectedInputActionsAssetKey, s_State.Json, s_State.ResolvedPath))
            {
                saveFailed = true;
            }
            else
            {
                s_State.PendingSave = false;
            }
        }

        if (saveFailed)
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Failed to save input actions asset.");
    }

    void DrawAudioMixerAssetInspectorInternal(std::string& selectedAudioMixerAssetKey)
    {
        struct State
        {
            std::string LoadedKey;
            std::filesystem::path ResolvedPath;
            Audio::AudioMixerDefinition Definition{};
            bool Loaded = false;
        };
        static State s_State;

        if (selectedAudioMixerAssetKey.empty())
            return;

        if (!s_State.Loaded || s_State.LoadedKey != selectedAudioMixerAssetKey)
        {
            s_State = {};
            s_State.LoadedKey = selectedAudioMixerAssetKey;
            s_State.Loaded = Audio::LoadAudioMixerDefinitionFromAssetKey(
                selectedAudioMixerAssetKey,
                s_State.Definition,
                &s_State.ResolvedPath);
            if (!s_State.Loaded)
            {
                ImGui::TextColored(
                    ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                    "Failed to load audio mixer asset: %s",
                    selectedAudioMixerAssetKey.c_str());
                return;
            }
        }

        bool modified = false;
        bool saveFailed = false;

        ImGui::Text("Audio Mixer: %s", std::filesystem::path(selectedAudioMixerAssetKey).filename().string().c_str());
        ImGui::TextDisabled("Asset Key: %s", selectedAudioMixerAssetKey.c_str());
        ImGui::Separator();

        if (ImGui::Button("Add Group", ImVec2(120.0f, 0.0f)))
        {
            std::string groupName = "Group";
            int32_t suffix = 1;
            auto nameExists = [&](const std::string& name) {
                return std::any_of(
                    s_State.Definition.Groups.begin(),
                    s_State.Definition.Groups.end(),
                    [&name](const Audio::AudioMixerGroupEntry& group) {
                        return group.Name == name;
                    });
            };
            while (nameExists(groupName))
            {
                ++suffix;
                groupName = "Group" + std::to_string(suffix);
            }

            s_State.Definition.Groups.push_back(Audio::AudioMixerGroupEntry{ groupName, 1.0f });
            modified = true;
        }

        if (s_State.Definition.Groups.empty())
            ImGui::TextDisabled("No groups authored.");

        int32_t removeGroupIndex = -1;
        for (int32_t groupIndex = 0; groupIndex < static_cast<int32_t>(s_State.Definition.Groups.size()); ++groupIndex)
        {
            auto& group = s_State.Definition.Groups[static_cast<size_t>(groupIndex)];
            ImGui::PushID(groupIndex);

            std::array<char, 128> groupNameBuffer{};
            std::snprintf(groupNameBuffer.data(), groupNameBuffer.size(), "%s", group.Name.c_str());
            if (ImGui::InputText("Group", groupNameBuffer.data(), groupNameBuffer.size()))
            {
                group.Name = groupNameBuffer.data();
                modified = true;
            }

            if (ImGui::SliderFloat("Volume", &group.Volume, 0.0f, 2.0f, "%.2f"))
            {
                group.Volume = std::max(0.0f, group.Volume);
                modified = true;
            }

            if (ImGui::SliderFloat("Reverb Send", &group.ReverbSend, 0.0f, 1.0f, "%.2f"))
            {
                group.ReverbSend = std::clamp(group.ReverbSend, 0.0f, 1.0f);
                modified = true;
            }

            if (ImGui::Button("Remove Group", ImVec2(120.0f, 0.0f)))
                removeGroupIndex = groupIndex;

            ImGui::Separator();
            ImGui::PopID();
        }

        if (removeGroupIndex >= 0 &&
            removeGroupIndex < static_cast<int32_t>(s_State.Definition.Groups.size()))
        {
            s_State.Definition.Groups.erase(
                s_State.Definition.Groups.begin() + removeGroupIndex);
            modified = true;
        }

        if (modified)
        {
            Audio::NormalizeAudioMixerDefinition(s_State.Definition);
            if (!Audio::SaveAudioMixerDefinitionToPath(s_State.ResolvedPath, s_State.Definition))
            {
                saveFailed = true;
            }
            else
            {
                (void)Assets::AssetDatabase::GetInstance().ImportOrUpdate(
                    selectedAudioMixerAssetKey,
                    Assets::AssetType::AudioMixer);
                (void)Assets::AssetImportPipeline::ReimportChanged(true);
            }
        }

        if (saveFailed)
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Failed to save audio mixer asset.");
    }
}
