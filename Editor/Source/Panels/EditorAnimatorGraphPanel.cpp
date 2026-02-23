#include "EditorAnimatorGraphPanel.h"

#include "EditorAssetNaming.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetImportPipeline.h"
#include "Assets/AssetManager.h"
#include "Assets/AssetPaths.h"
#include "Assets/AssetTypes.h"
#include "Assets/AnimatorControllerAsset.h"
#include "Core/Debug/Log.h"
#include "Undo/EditorTextAssetCommand.h"
#include "Undo/EditorUndoService.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

namespace Limitless::EditorAnimatorGraphPanel
{
    namespace
    {
        using json = nlohmann::json;

        constexpr const char* kParameterTypeNames[] = {
            "Bool",
            "Float",
            "Integer",
            "Trigger"
        };

        constexpr const char* kConditionModeNames[] = {
            "If",
            "IfNot",
            "Greater",
            "Less",
            "Equals",
            "NotEquals",
            "Triggered"
        };

        struct GraphEditorState
        {
            std::string LoadedAssetKey;
            std::filesystem::path ResolvedPath;
            json AppliedJson = json::object();
            json WorkingJson = json::object();
            bool Loaded = false;
            bool LoadFailed = false;
            std::string StatusMessage;
            bool StatusIsError = false;
        };

        GraphEditorState& GetGraphEditorState()
        {
            static GraphEditorState state;
            return state;
        }

        std::vector<std::string> BuildAnimationClipPickerKeys()
        {
            std::vector<std::string> keys;
            std::unordered_set<std::string> seen;

            const auto records = Assets::AssetDatabase::GetInstance().GetAllRecords();
            for (const auto& record : records)
            {
                if (record.Type != Assets::AssetType::AnimationClip || record.Key.empty())
                    continue;
                if (seen.insert(record.Key).second)
                    keys.push_back(record.Key);
            }

            std::sort(keys.begin(), keys.end());
            return keys;
        }

        bool IsAnimationClipAssetKey(const std::string& assetKey)
        {
            if (assetKey.empty())
                return false;

            const auto record = Assets::AssetDatabase::GetInstance().FindByKey(assetKey);
            if (record.IsSuccess())
                return record.GetValue().Type == Assets::AssetType::AnimationClip;

            std::string lowerAssetKey = assetKey;
            std::transform(lowerAssetKey.begin(), lowerAssetKey.end(), lowerAssetKey.begin(), [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
            return lowerAssetKey.ends_with(".animationclip.json") ||
                   lowerAssetKey.ends_with(".animation.json") ||
                   lowerAssetKey.ends_with(".anim.json");
        }

        bool ResolveControllerPath(const std::string& assetKey, std::filesystem::path& outPath)
        {
            const auto resolvedResult = Assets::ResolveAssetKeyToPath(assetKey);
            if (resolvedResult.IsFailure())
                return false;
            outPath = resolvedResult.GetValue();
            return true;
        }

        bool LoadJsonFromPath(const std::filesystem::path& path, json& outJson)
        {
            std::ifstream input(path, std::ios::in | std::ios::binary);
            if (!input.is_open())
                return false;
            try
            {
                input >> outJson;
            }
            catch (...)
            {
                return false;
            }
            if (!outJson.is_object())
                outJson = json::object();
            return true;
        }

        void EnsureControllerSchemaDefaults(json& root)
        {
            if (!root.is_object())
                root = json::object();

            if (!root.contains("Name") || !root["Name"].is_string())
                root["Name"] = std::string("Animator Controller");
            if (!root.contains("DefaultStateName") || !root["DefaultStateName"].is_string())
                root["DefaultStateName"] = std::string{};
            if (!root.contains("Parameters") || !root["Parameters"].is_array())
                root["Parameters"] = json::array();
            if (!root.contains("States") || !root["States"].is_array())
                root["States"] = json::array();
        }

        bool ApplyControllerTextToDisk(const std::filesystem::path& path,
                                       const std::string& assetKey,
                                       const std::string& jsonText)
        {
            std::ofstream output(path, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!output.is_open())
                return false;
            output << jsonText;
            output.flush();
            if (!output.good())
                return false;

            (void)Assets::AssetDatabase::GetInstance().ImportOrUpdate(assetKey, Assets::AssetType::AnimatorController);
            (void)Assets::AssetImportPipeline::ReimportChanged(true);
            if (const auto cached = Assets::AssetManager::GetCachedByKey(assetKey))
                (void)cached->Reload();
            return true;
        }

        bool CommitControllerChanges(EditorUndoService* undoService,
                                     const std::string& label,
                                     const std::string& assetKey,
                                     const std::filesystem::path& path,
                                     const json& beforeJson,
                                     const json& afterJson)
        {
            const std::string beforeText = beforeJson.dump(2);
            const std::string afterText = afterJson.dump(2);
            if (beforeText == afterText)
                return true;

            auto applyCallback = [path, assetKey](const std::string& text) {
                return ApplyControllerTextToDisk(path, assetKey, text);
            };

            if (!undoService)
                return applyCallback(afterText);

            // Apply first so persisted asset state changes immediately; command stores undo/redo texts.
            if (!applyCallback(afterText))
                return false;

            auto command = std::make_unique<EditorTextAssetCommand>(
                label,
                beforeText,
                afterText,
                std::move(applyCallback));
            return undoService->ExecuteCommand(std::move(command));
        }

        bool ApplyPendingControllerChanges(EditorUndoService* undoService, GraphEditorState& state, const char* label)
        {
            if (!state.Loaded || state.LoadFailed)
                return true;

            if (state.LoadedAssetKey.empty() || state.ResolvedPath.empty())
                return false;

            if (state.WorkingJson.dump() == state.AppliedJson.dump())
                return true;

            if (!CommitControllerChanges(
                    undoService,
                    label ? std::string(label) : std::string("Edit Animator Controller"),
                    state.LoadedAssetKey,
                    state.ResolvedPath,
                    state.AppliedJson,
                    state.WorkingJson))
            {
                return false;
            }

            state.AppliedJson = state.WorkingJson;
            state.StatusMessage = "Animator controller changes applied.";
            state.StatusIsError = false;
            return true;
        }

        int ParameterTypeIndexFromName(const std::string& typeName)
        {
            for (int index = 0; index < 4; ++index)
            {
                if (typeName == kParameterTypeNames[index])
                    return index;
            }
            return 0;
        }

        int ConditionModeIndexFromName(const std::string& modeName)
        {
            for (int index = 0; index < 7; ++index)
            {
                if (modeName == kConditionModeNames[index])
                    return index;
            }
            return 0;
        }

        void DrawParametersSection(json& parameters)
        {
            ImGui::SeparatorText("Parameters");
            if (ImGui::Button("Add Parameter"))
            {
                parameters.push_back({
                    {"Name", "NewParameter"},
                    {"Type", "Bool"},
                    {"DefaultBool", false},
                    {"DefaultFloat", 0.0f},
                    {"DefaultInteger", 0}
                });
            }

            int32_t removeIndex = -1;
            for (size_t parameterIndex = 0; parameterIndex < parameters.size(); ++parameterIndex)
            {
                auto& parameter = parameters[parameterIndex];
                ImGui::PushID(static_cast<int>(parameterIndex));
                const std::string header = "Parameter " + std::to_string(parameterIndex);
                if (ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                {
                    std::array<char, 128> nameBuffer{};
                    std::snprintf(nameBuffer.data(), nameBuffer.size(), "%s", parameter.value("Name", std::string{}).c_str());
                    if (ImGui::InputText("Name", nameBuffer.data(), nameBuffer.size()))
                        parameter["Name"] = std::string(nameBuffer.data());

                    const std::string typeName = parameter.value("Type", std::string("Bool"));
                    int typeIndex = ParameterTypeIndexFromName(typeName);
                    if (ImGui::Combo("Type", &typeIndex, kParameterTypeNames, 4))
                        parameter["Type"] = std::string(kParameterTypeNames[typeIndex]);

                    const std::string selectedType = parameter.value("Type", std::string("Bool"));
                    if (selectedType == "Bool" || selectedType == "Trigger")
                    {
                        bool defaultBool = parameter.value("DefaultBool", false);
                        if (ImGui::Checkbox("Default Bool", &defaultBool))
                            parameter["DefaultBool"] = defaultBool;
                    }
                    else if (selectedType == "Float")
                    {
                        float defaultFloat = parameter.value("DefaultFloat", 0.0f);
                        if (ImGui::DragFloat("Default Float", &defaultFloat, 0.01f))
                            parameter["DefaultFloat"] = defaultFloat;
                    }
                    else if (selectedType == "Integer")
                    {
                        int defaultInteger = parameter.value("DefaultInteger", 0);
                        if (ImGui::DragInt("Default Integer", &defaultInteger))
                            parameter["DefaultInteger"] = defaultInteger;
                    }

                    if (ImGui::Button("Remove Parameter"))
                        removeIndex = static_cast<int32_t>(parameterIndex);
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }

            if (removeIndex >= 0)
                parameters.erase(parameters.begin() + removeIndex);
        }

        void DrawTransitionConditions(json& conditions)
        {
            if (!conditions.is_array())
                conditions = json::array();

            if (ImGui::Button("Add Condition"))
            {
                conditions.push_back({
                    {"ParameterName", ""},
                    {"Mode", "If"},
                    {"BoolValue", false},
                    {"FloatThreshold", 0.0f},
                    {"IntegerThreshold", 0}
                });
            }

            int32_t removeConditionIndex = -1;
            for (size_t conditionIndex = 0; conditionIndex < conditions.size(); ++conditionIndex)
            {
                auto& condition = conditions[conditionIndex];
                ImGui::PushID(static_cast<int>(conditionIndex));
                const std::string header = "Condition " + std::to_string(conditionIndex);
                if (ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                {
                    std::array<char, 128> parameterNameBuffer{};
                    std::snprintf(
                        parameterNameBuffer.data(),
                        parameterNameBuffer.size(),
                        "%s",
                        condition.value("ParameterName", std::string{}).c_str());
                    if (ImGui::InputText("Parameter Name", parameterNameBuffer.data(), parameterNameBuffer.size()))
                        condition["ParameterName"] = std::string(parameterNameBuffer.data());

                    int modeIndex = ConditionModeIndexFromName(condition.value("Mode", std::string("If")));
                    if (ImGui::Combo("Mode", &modeIndex, kConditionModeNames, 7))
                        condition["Mode"] = std::string(kConditionModeNames[modeIndex]);

                    const std::string modeName = condition.value("Mode", std::string("If"));
                    if (modeName == "If" || modeName == "IfNot")
                    {
                        bool boolValue = condition.value("BoolValue", false);
                        if (ImGui::Checkbox("Bool Value", &boolValue))
                            condition["BoolValue"] = boolValue;
                    }
                    else if (modeName == "Greater" || modeName == "Less")
                    {
                        float floatThreshold = condition.value("FloatThreshold", 0.0f);
                        if (ImGui::DragFloat("Float Threshold", &floatThreshold, 0.01f))
                            condition["FloatThreshold"] = floatThreshold;
                    }
                    else if (modeName == "Equals" || modeName == "NotEquals")
                    {
                        int integerThreshold = condition.value("IntegerThreshold", 0);
                        if (ImGui::DragInt("Integer Threshold", &integerThreshold))
                            condition["IntegerThreshold"] = integerThreshold;
                    }

                    if (ImGui::Button("Remove Condition"))
                        removeConditionIndex = static_cast<int32_t>(conditionIndex);
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }

            if (removeConditionIndex >= 0)
                conditions.erase(conditions.begin() + removeConditionIndex);
        }

        void DrawTransitionsSection(json& transitions)
        {
            if (!transitions.is_array())
                transitions = json::array();

            if (ImGui::Button("Add Transition"))
            {
                transitions.push_back({
                    {"ToState", ""},
                    {"HasExitTime", false},
                    {"ExitTimeNormalized", 1.0f},
                    {"DurationSeconds", 0.1f},
                    {"CanTransitionToSelf", false},
                    {"Conditions", json::array()}
                });
            }

            int32_t removeTransitionIndex = -1;
            for (size_t transitionIndex = 0; transitionIndex < transitions.size(); ++transitionIndex)
            {
                auto& transition = transitions[transitionIndex];
                ImGui::PushID(static_cast<int>(transitionIndex));
                const std::string header = "Transition " + std::to_string(transitionIndex);
                if (ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                {
                    std::array<char, 128> toStateBuffer{};
                    std::snprintf(toStateBuffer.data(), toStateBuffer.size(), "%s", transition.value("ToState", std::string{}).c_str());
                    if (ImGui::InputText("To State", toStateBuffer.data(), toStateBuffer.size()))
                        transition["ToState"] = std::string(toStateBuffer.data());

                    bool hasExitTime = transition.value("HasExitTime", false);
                    if (ImGui::Checkbox("Has Exit Time", &hasExitTime))
                        transition["HasExitTime"] = hasExitTime;

                    float exitTimeNormalized = transition.value("ExitTimeNormalized", 1.0f);
                    if (ImGui::DragFloat("Exit Time (Normalized)", &exitTimeNormalized, 0.01f, 0.0f, 1.0f))
                        transition["ExitTimeNormalized"] = std::clamp(exitTimeNormalized, 0.0f, 1.0f);

                    float durationSeconds = transition.value("DurationSeconds", 0.1f);
                    if (ImGui::DragFloat("Duration (Seconds)", &durationSeconds, 0.01f))
                        transition["DurationSeconds"] = std::max(0.0f, durationSeconds);

                    bool canTransitionToSelf = transition.value("CanTransitionToSelf", false);
                    if (ImGui::Checkbox("Can Transition To Self", &canTransitionToSelf))
                        transition["CanTransitionToSelf"] = canTransitionToSelf;

                    ImGui::SeparatorText("Conditions");
                    DrawTransitionConditions(transition["Conditions"]);

                    if (ImGui::Button("Remove Transition"))
                        removeTransitionIndex = static_cast<int32_t>(transitionIndex);
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }

            if (removeTransitionIndex >= 0)
                transitions.erase(transitions.begin() + removeTransitionIndex);
        }

        void DrawStatesSection(json& states)
        {
            ImGui::SeparatorText("States");
            if (ImGui::Button("Add State"))
            {
                states.push_back({
                    {"Name", "State"},
                    {"Clip", {{"key", ""}}},
                    {"SpeedMultiplier", 1.0f},
                    {"LoopOverrideEnabled", false},
                    {"LoopOverride", true},
                    {"Transitions", json::array()}
                });
            }

            int32_t removeStateIndex = -1;
            for (size_t stateIndex = 0; stateIndex < states.size(); ++stateIndex)
            {
                auto& state = states[stateIndex];
                ImGui::PushID(static_cast<int>(stateIndex));
                const std::string header = "State " + std::to_string(stateIndex);
                if (ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                {
                    std::array<char, 128> stateNameBuffer{};
                    std::snprintf(stateNameBuffer.data(), stateNameBuffer.size(), "%s", state.value("Name", std::string{}).c_str());
                    if (ImGui::InputText("Name", stateNameBuffer.data(), stateNameBuffer.size()))
                        state["Name"] = std::string(stateNameBuffer.data());

                    if (!state.contains("Clip") || !state["Clip"].is_object())
                    {
                        const std::string fallbackClipKey = state.value("ClipKey", std::string{});
                        state["Clip"] = json::object({{"key", fallbackClipKey}});
                    }
                    std::string clipKey = state["Clip"].value("key", std::string{});
                    const std::string clipLabel = clipKey.empty()
                        ? std::string("None")
                        : EditorAssetNaming::GetAssetDisplayNameFromAssetKey(clipKey);

                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("Clip");
                    ImGui::Button((clipLabel + "##AnimatorStateClip").c_str(),
                                  ImVec2(std::max(60.0f, ImGui::GetContentRegionAvail().x - 120.0f), 0.0f));
                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_MOVE"))
                        {
                            const char* key = static_cast<const char*>(payload->Data);
                            if (key && key[0])
                            {
                                const std::string keyValue = key;
                                if (IsAnimationClipAssetKey(keyValue) && clipKey != keyValue)
                                {
                                    clipKey = keyValue;
                                    state["Clip"]["key"] = clipKey;
                                }
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("...##AnimatorStateClipPicker"))
                        ImGui::OpenPopup("AnimatorStateClipPickerPopup");
                    if (ImGui::BeginPopup("AnimatorStateClipPickerPopup"))
                    {
                        if (ImGui::Selectable("None##AnimatorStateClipPickerNone"))
                        {
                            clipKey.clear();
                            state["Clip"]["key"] = "";
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::Separator();

                        const std::vector<std::string> clipKeys = BuildAnimationClipPickerKeys();
                        for (const auto& key : clipKeys)
                        {
                            const bool isSelected = (clipKey == key);
                            const std::string display = EditorAssetNaming::GetAssetDisplayNameFromAssetKey(key);
                            if (ImGui::Selectable((display + "##AnimatorStateClipPicker_" + key).c_str(), isSelected))
                            {
                                clipKey = key;
                                state["Clip"]["key"] = clipKey;
                                ImGui::CloseCurrentPopup();
                            }
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                                ImGui::SetTooltip("%s", key.c_str());
                        }
                        ImGui::EndPopup();
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("X##AnimatorStateClearClip"))
                    {
                        clipKey.clear();
                        state["Clip"]["key"] = "";
                    }

                    float speedMultiplier = state.value("SpeedMultiplier", 1.0f);
                    if (ImGui::DragFloat("Speed Multiplier", &speedMultiplier, 0.01f))
                        state["SpeedMultiplier"] = std::max(0.0f, speedMultiplier);

                    bool loopOverrideEnabled = state.value("LoopOverrideEnabled", false);
                    if (ImGui::Checkbox("Loop Override Enabled", &loopOverrideEnabled))
                        state["LoopOverrideEnabled"] = loopOverrideEnabled;

                    bool loopOverride = state.value("LoopOverride", true);
                    if (ImGui::Checkbox("Loop Override Value", &loopOverride))
                        state["LoopOverride"] = loopOverride;

                    DrawTransitionsSection(state["Transitions"]);

                    if (ImGui::Button("Remove State"))
                        removeStateIndex = static_cast<int32_t>(stateIndex);
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }

            if (removeStateIndex >= 0)
                states.erase(states.begin() + removeStateIndex);
        }
    }

    void Draw(bool& isOpen, const std::string& animatorControllerAssetKey, EditorUndoService* undoService)
    {
        if (!isOpen)
            return;

        if (!ImGui::Begin("Animator Graph", &isOpen))
        {
            ImGui::End();
            return;
        }

        auto& state = GetGraphEditorState();
        if (animatorControllerAssetKey.empty())
        {
            ImGui::TextDisabled("Select an Animator Controller asset to edit.");
            ImGui::End();
            return;
        }

        if (state.LoadedAssetKey != animatorControllerAssetKey)
        {
            state = GraphEditorState{};
            state.LoadedAssetKey = animatorControllerAssetKey;
            if (!ResolveControllerPath(animatorControllerAssetKey, state.ResolvedPath))
            {
                state.LoadFailed = true;
                state.StatusMessage = "Failed to resolve animator controller path.";
                state.StatusIsError = true;
            }
            else if (!LoadJsonFromPath(state.ResolvedPath, state.WorkingJson))
            {
                state.LoadFailed = true;
                state.StatusMessage = "Failed to load animator controller JSON.";
                state.StatusIsError = true;
            }
            else
            {
                EnsureControllerSchemaDefaults(state.WorkingJson);
                state.AppliedJson = state.WorkingJson;
                state.Loaded = true;
            }
        }

        ImGui::Text("Controller: %s", EditorAssetNaming::GetAssetDisplayNameFromAssetKey(animatorControllerAssetKey).c_str());
        ImGui::TextDisabled("Asset Key: %s", animatorControllerAssetKey.c_str());
        if (!state.ResolvedPath.empty())
            ImGui::TextDisabled("Path: %s", state.ResolvedPath.string().c_str());
        ImGui::Separator();

        if (state.LoadFailed || !state.Loaded)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", state.StatusMessage.c_str());
            ImGui::End();
            return;
        }

        EnsureControllerSchemaDefaults(state.WorkingJson);

        std::array<char, 256> controllerNameBuffer{};
        std::snprintf(controllerNameBuffer.data(),
                      controllerNameBuffer.size(),
                      "%s",
                      state.WorkingJson.value("Name", std::string{}).c_str());
        if (ImGui::InputText("Name", controllerNameBuffer.data(), controllerNameBuffer.size()))
            state.WorkingJson["Name"] = std::string(controllerNameBuffer.data());

        std::array<char, 128> defaultStateNameBuffer{};
        std::snprintf(defaultStateNameBuffer.data(),
                      defaultStateNameBuffer.size(),
                      "%s",
                      state.WorkingJson.value("DefaultStateName", std::string{}).c_str());
        if (ImGui::InputText("Default State Name", defaultStateNameBuffer.data(), defaultStateNameBuffer.size()))
            state.WorkingJson["DefaultStateName"] = std::string(defaultStateNameBuffer.data());

        DrawParametersSection(state.WorkingJson["Parameters"]);
        DrawStatesSection(state.WorkingJson["States"]);

        const bool hasUnsavedChanges = (state.WorkingJson.dump() != state.AppliedJson.dump());
        ImGui::Separator();
        ImGui::BeginDisabled(!hasUnsavedChanges);
        if (ImGui::Button("Apply Changes", ImVec2(180.0f, 0.0f)))
        {
            if (ApplyPendingControllerChanges(undoService, state, "Edit Animator Controller"))
            {
                // State updated by ApplyPendingControllerChanges.
            }
            else
            {
                state.StatusMessage = "Failed to apply animator controller changes.";
                state.StatusIsError = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Revert Changes", ImVec2(180.0f, 0.0f)))
        {
            state.WorkingJson = state.AppliedJson;
            state.StatusMessage = "Reverted local animator controller edits.";
            state.StatusIsError = false;
        }
        ImGui::EndDisabled();

        if (!hasUnsavedChanges)
            ImGui::TextDisabled("No pending edits.");
        else
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f), "Pending edits not yet applied.");

        if (!state.StatusMessage.empty())
        {
            const ImVec4 messageColor = state.StatusIsError
                ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f)
                : ImVec4(0.6f, 1.0f, 0.6f, 1.0f);
            ImGui::TextColored(messageColor, "%s", state.StatusMessage.c_str());
        }

        ImGui::End();
    }

    bool ApplyPendingChanges(EditorUndoService* undoService)
    {
        auto& state = GetGraphEditorState();
        return ApplyPendingControllerChanges(undoService, state, "Auto Save Animator Controller");
    }
}
