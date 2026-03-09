#include "EditorAnimatorGraphPanel.h"

#include "EditorPanelStyle.h"
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
#include <cmath>
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

        constexpr float kNodeWidth = 160.0f;
        constexpr float kNodeHeight = 40.0f;
        constexpr float kNodeRounding = 6.0f;
        constexpr float kEntryNodeWidth = 80.0f;
        constexpr float kEntryNodeHeight = 30.0f;
        constexpr float kSidebarWidth = 250.0f;
        constexpr float kGridStep = 50.0f;
        constexpr float kArrowSize = 10.0f;
        constexpr float kMinZoom = 0.3f;
        constexpr float kMaxZoom = 2.5f;

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

            ImVec2 CanvasOffset = ImVec2(50.0f, 50.0f);
            float CanvasZoom = 1.0f;
            std::vector<ImVec2> NodePositions;
            bool PositionsInitialized = false;

            int SelectedStateIndex = -1;
            int DraggedNodeIndex = -1;
            ImVec2 DragOffset = ImVec2(0.0f, 0.0f);
            int ContextNodeIndex = -1;
            int TransitionFromIndex = -1;
        };

        GraphEditorState& GetGraphEditorState()
        {
            static GraphEditorState state;
            return state;
        }

        // ---- Asset utility functions ----

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

        // ---- Canvas coordinate helpers ----

        ImVec2 CanvasToScreen(const ImVec2& p, const ImVec2& origin, const ImVec2& offset, float zoom)
        {
            return ImVec2(origin.x + (p.x + offset.x) * zoom,
                          origin.y + (p.y + offset.y) * zoom);
        }

        ImVec2 ScreenToCanvas(const ImVec2& p, const ImVec2& origin, const ImVec2& offset, float zoom)
        {
            return ImVec2((p.x - origin.x) / zoom - offset.x,
                          (p.y - origin.y) / zoom - offset.y);
        }

        ImVec2 NodeCenterPos(const ImVec2& pos, float w = kNodeWidth, float h = kNodeHeight)
        {
            return ImVec2(pos.x + w * 0.5f, pos.y + h * 0.5f);
        }

        int HitTestNodes(const std::vector<ImVec2>& positions, const ImVec2& cp)
        {
            for (int i = static_cast<int>(positions.size()) - 1; i >= 0; --i)
            {
                if (cp.x >= positions[i].x && cp.x <= positions[i].x + kNodeWidth &&
                    cp.y >= positions[i].y && cp.y <= positions[i].y + kNodeHeight)
                    return i;
            }
            return -1;
        }

        ImVec2 RectEdgePoint(const ImVec2& center, const ImVec2& target, float halfW, float halfH)
        {
            float dx = target.x - center.x;
            float dy = target.y - center.y;
            if (std::abs(dx) < 0.001f && std::abs(dy) < 0.001f)
                return ImVec2(center.x + halfW, center.y);
            float sx = (dx != 0.0f) ? halfW / std::abs(dx) : 1e6f;
            float sy = (dy != 0.0f) ? halfH / std::abs(dy) : 1e6f;
            float s = std::min(sx, sy);
            return ImVec2(center.x + dx * s, center.y + dy * s);
        }

        void SyncNodePositions(GraphEditorState& state)
        {
            const auto& arr = state.WorkingJson["States"];
            const size_t count = arr.is_array() ? arr.size() : 0;

            if (!state.PositionsInitialized && count > 0)
            {
                state.NodePositions.resize(count);
                for (size_t i = 0; i < count; ++i)
                {
                    state.NodePositions[i] = ImVec2(
                        200.0f + static_cast<float>(i % 3) * 200.0f,
                        50.0f + static_cast<float>(i / 3) * 80.0f);
                }
                state.PositionsInitialized = true;
                return;
            }

            while (state.NodePositions.size() < count)
            {
                state.NodePositions.emplace_back(
                    200.0f + static_cast<float>(state.NodePositions.size() % 3) * 200.0f,
                    50.0f + static_cast<float>(state.NodePositions.size() / 3) * 80.0f);
            }
            while (state.NodePositions.size() > count)
                state.NodePositions.pop_back();
        }

        int FindStateIndexByName(const json& statesArray, const std::string& name)
        {
            if (!statesArray.is_array() || name.empty()) return -1;
            for (size_t i = 0; i < statesArray.size(); ++i)
                if (statesArray[i].value("Name", std::string{}) == name)
                    return static_cast<int>(i);
            return -1;
        }

        void DrawArrowhead(ImDrawList* dl, const ImVec2& tip, const ImVec2& dir, float size, ImU32 col)
        {
            float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
            if (len < 0.001f) return;
            float nx = dir.x / len, ny = dir.y / len;
            float px = -ny, py = nx;
            dl->AddTriangleFilled(
                tip,
                ImVec2(tip.x - nx * size + px * size * 0.4f, tip.y - ny * size + py * size * 0.4f),
                ImVec2(tip.x - nx * size - px * size * 0.4f, tip.y - ny * size - py * size * 0.4f),
                col);
        }

        // ---- Sidebar drawing helpers ----

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
    }

    void Draw(bool& isOpen, const std::string& animatorControllerAssetKey, EditorUndoService* undoService, bool requestFocus)
    {
        if (!isOpen)
            return;

        EditorPanelStyle::PushPanelVisualStyle();
        if (requestFocus)
            ImGui::SetNextWindowFocus();
        if (!ImGui::Begin("Animator Graph", &isOpen))
        {
            ImGui::End();
            EditorPanelStyle::PopPanelVisualStyle();
            return;
        }

        auto& state = GetGraphEditorState();
        if (animatorControllerAssetKey.empty())
        {
            ImGui::TextDisabled("Select an Animator Controller asset to edit.");
            ImGui::End();
            EditorPanelStyle::PopPanelVisualStyle();
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

        if (state.LoadFailed || !state.Loaded)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", state.StatusMessage.c_str());
            ImGui::End();
            EditorPanelStyle::PopPanelVisualStyle();
            return;
        }

        EnsureControllerSchemaDefaults(state.WorkingJson);
        SyncNodePositions(state);

        if (state.SelectedStateIndex >= 0)
        {
            const auto& statesArray = state.WorkingJson["States"];
            if (!statesArray.is_array() || state.SelectedStateIndex >= static_cast<int>(statesArray.size()))
                state.SelectedStateIndex = -1;
        }

        const float footerHeight = 60.0f;
        const float availHeight = std::max(100.0f, ImGui::GetContentRegionAvail().y - footerHeight);

        // ---- Sidebar ----
        ImGui::BeginChild("##GraphSidebar", ImVec2(kSidebarWidth, availHeight), ImGuiChildFlags_Border);
        {
            ImGui::TextDisabled("Controller: %s",
                EditorAssetNaming::GetAssetDisplayNameFromAssetKey(animatorControllerAssetKey).c_str());

            std::array<char, 256> nameBuffer{};
            std::snprintf(nameBuffer.data(), nameBuffer.size(), "%s",
                state.WorkingJson.value("Name", std::string{}).c_str());
            if (ImGui::InputText("Name", nameBuffer.data(), nameBuffer.size()))
                state.WorkingJson["Name"] = std::string(nameBuffer.data());

            std::array<char, 128> defaultBuffer{};
            std::snprintf(defaultBuffer.data(), defaultBuffer.size(), "%s",
                state.WorkingJson.value("DefaultStateName", std::string{}).c_str());
            if (ImGui::InputText("Default State", defaultBuffer.data(), defaultBuffer.size()))
                state.WorkingJson["DefaultStateName"] = std::string(defaultBuffer.data());

            DrawParametersSection(state.WorkingJson["Parameters"]);

            ImGui::Separator();

            if (state.SelectedStateIndex >= 0 &&
                state.SelectedStateIndex < static_cast<int>(state.WorkingJson["States"].size()))
            {
                auto& selectedState = state.WorkingJson["States"][state.SelectedStateIndex];
                ImGui::PushID(state.SelectedStateIndex);
                ImGui::SeparatorText("Selected State");

                std::array<char, 128> stateNameBuffer{};
                std::snprintf(stateNameBuffer.data(), stateNameBuffer.size(), "%s",
                    selectedState.value("Name", std::string{}).c_str());
                if (ImGui::InputText("State Name", stateNameBuffer.data(), stateNameBuffer.size()))
                    selectedState["Name"] = std::string(stateNameBuffer.data());

                if (!selectedState.contains("Clip") || !selectedState["Clip"].is_object())
                {
                    const std::string fallbackClipKey = selectedState.value("ClipKey", std::string{});
                    selectedState["Clip"] = json::object({{"key", fallbackClipKey}});
                }
                std::string clipKey = selectedState["Clip"].value("key", std::string{});
                const std::string clipLabel = clipKey.empty()
                    ? std::string("None")
                    : EditorAssetNaming::GetAssetDisplayNameFromAssetKey(clipKey);

                ImGui::AlignTextToFramePadding();
                ImGui::Text("Clip");
                ImGui::Button((clipLabel + "##StateClip").c_str(),
                    ImVec2(std::max(60.0f, ImGui::GetContentRegionAvail().x - 60.0f), 0.0f));
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
                                selectedState["Clip"]["key"] = clipKey;
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                ImGui::SameLine();
                if (ImGui::Button("...##StateClipPicker"))
                    ImGui::OpenPopup("StateClipPickerPopup");
                if (ImGui::BeginPopup("StateClipPickerPopup"))
                {
                    if (ImGui::Selectable("None##ClipPickerNone"))
                    {
                        clipKey.clear();
                        selectedState["Clip"]["key"] = "";
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::Separator();

                    const std::vector<std::string> clipKeys = BuildAnimationClipPickerKeys();
                    for (const auto& key : clipKeys)
                    {
                        const bool isSelected = (clipKey == key);
                        const std::string display = EditorAssetNaming::GetAssetDisplayNameFromAssetKey(key);
                        if (ImGui::Selectable((display + "##ClipPicker_" + key).c_str(), isSelected))
                        {
                            clipKey = key;
                            selectedState["Clip"]["key"] = clipKey;
                            ImGui::CloseCurrentPopup();
                        }
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                            ImGui::SetTooltip("%s", key.c_str());
                    }
                    ImGui::EndPopup();
                }

                float speedMultiplier = selectedState.value("SpeedMultiplier", 1.0f);
                if (ImGui::DragFloat("Speed", &speedMultiplier, 0.01f))
                    selectedState["SpeedMultiplier"] = std::max(0.0f, speedMultiplier);

                bool loopOverrideEnabled = selectedState.value("LoopOverrideEnabled", false);
                if (ImGui::Checkbox("Loop Override", &loopOverrideEnabled))
                    selectedState["LoopOverrideEnabled"] = loopOverrideEnabled;
                if (loopOverrideEnabled)
                {
                    ImGui::SameLine();
                    bool loopOverride = selectedState.value("LoopOverride", true);
                    if (ImGui::Checkbox("##LoopVal", &loopOverride))
                        selectedState["LoopOverride"] = loopOverride;
                }

                ImGui::SeparatorText("Transitions");
                DrawTransitionsSection(selectedState["Transitions"]);

                ImGui::PopID();
            }
            else
            {
                ImGui::TextDisabled("Select a state in the graph.");
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // ---- Canvas ----
        ImGui::BeginChild("##GraphCanvas", ImVec2(0.0f, availHeight), ImGuiChildFlags_Border,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        {
            const ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
            const ImVec2 canvasSize = ImGui::GetContentRegionAvail();

            if (canvasSize.x > 1.0f && canvasSize.y > 1.0f)
            {
                ImGui::InvisibleButton("##canvas", canvasSize);
                const bool canvasHovered = ImGui::IsItemHovered();
                const ImVec2 mouseScreen = ImGui::GetIO().MousePos;
                const ImVec2 mouseCanvas = ScreenToCanvas(mouseScreen, canvasOrigin, state.CanvasOffset, state.CanvasZoom);

                // Zoom toward mouse
                if (canvasHovered && std::abs(ImGui::GetIO().MouseWheel) > 0.0f)
                {
                    const ImVec2 before = ScreenToCanvas(mouseScreen, canvasOrigin, state.CanvasOffset, state.CanvasZoom);
                    state.CanvasZoom = std::clamp(
                        state.CanvasZoom * (1.0f + ImGui::GetIO().MouseWheel * 0.1f), kMinZoom, kMaxZoom);
                    const ImVec2 after = ScreenToCanvas(mouseScreen, canvasOrigin, state.CanvasOffset, state.CanvasZoom);
                    state.CanvasOffset.x += after.x - before.x;
                    state.CanvasOffset.y += after.y - before.y;
                }

                // Pan with middle mouse
                if (canvasHovered && ImGui::IsMouseDown(ImGuiMouseButton_Middle))
                {
                    const ImVec2 delta = ImGui::GetIO().MouseDelta;
                    state.CanvasOffset.x += delta.x / state.CanvasZoom;
                    state.CanvasOffset.y += delta.y / state.CanvasZoom;
                }

                // Left click: select/drag nodes or complete transition
                if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    const int hit = HitTestNodes(state.NodePositions, mouseCanvas);
                    if (hit >= 0)
                    {
                        if (state.TransitionFromIndex >= 0)
                        {
                            const int fromIdx = state.TransitionFromIndex;
                            const std::string targetName =
                                state.WorkingJson["States"][hit].value("Name", std::string{});
                            auto& transitions = state.WorkingJson["States"][fromIdx]["Transitions"];
                            if (!transitions.is_array())
                                transitions = json::array();
                            transitions.push_back({
                                {"ToState", targetName},
                                {"HasExitTime", false},
                                {"ExitTimeNormalized", 1.0f},
                                {"DurationSeconds", 0.1f},
                                {"CanTransitionToSelf", hit == fromIdx},
                                {"Conditions", json::array()}
                            });
                            state.SelectedStateIndex = fromIdx;
                            state.TransitionFromIndex = -1;
                        }
                        else
                        {
                            state.SelectedStateIndex = hit;
                            state.DraggedNodeIndex = hit;
                            state.DragOffset = ImVec2(
                                mouseCanvas.x - state.NodePositions[hit].x,
                                mouseCanvas.y - state.NodePositions[hit].y);
                        }
                    }
                    else
                    {
                        state.SelectedStateIndex = -1;
                        state.TransitionFromIndex = -1;
                    }
                }

                // Node dragging
                if (state.DraggedNodeIndex >= 0 &&
                    state.DraggedNodeIndex < static_cast<int>(state.NodePositions.size()))
                {
                    if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
                    {
                        const ImVec2 mc = ScreenToCanvas(mouseScreen, canvasOrigin,
                            state.CanvasOffset, state.CanvasZoom);
                        state.NodePositions[state.DraggedNodeIndex] = ImVec2(
                            mc.x - state.DragOffset.x,
                            mc.y - state.DragOffset.y);
                    }
                    else
                    {
                        state.DraggedNodeIndex = -1;
                    }
                }

                // Right click context menus
                if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                {
                    const int hit = HitTestNodes(state.NodePositions, mouseCanvas);
                    state.ContextNodeIndex = hit;
                    if (hit >= 0)
                        ImGui::OpenPopup("##NodeCtx");
                    else
                        ImGui::OpenPopup("##CanvasCtx");
                }

                if (state.TransitionFromIndex >= 0 && ImGui::IsKeyPressed(ImGuiKey_Escape))
                    state.TransitionFromIndex = -1;

                if (ImGui::BeginPopup("##CanvasCtx"))
                {
                    if (ImGui::MenuItem("Add State"))
                    {
                        state.WorkingJson["States"].push_back({
                            {"Name", "New State"},
                            {"Clip", {{"key", ""}}},
                            {"SpeedMultiplier", 1.0f},
                            {"LoopOverrideEnabled", false},
                            {"LoopOverride", true},
                            {"Transitions", json::array()}
                        });
                        state.NodePositions.push_back(mouseCanvas);
                        state.SelectedStateIndex = static_cast<int>(state.WorkingJson["States"].size()) - 1;
                    }
                    ImGui::EndPopup();
                }

                if (ImGui::BeginPopup("##NodeCtx"))
                {
                    const int idx = state.ContextNodeIndex;
                    if (idx >= 0 && idx < static_cast<int>(state.WorkingJson["States"].size()))
                    {
                        if (ImGui::MenuItem("Set as Default"))
                            state.WorkingJson["DefaultStateName"] =
                                state.WorkingJson["States"][idx].value("Name", std::string{});

                        if (ImGui::MenuItem("Add Transition From Here"))
                        {
                            state.TransitionFromIndex = idx;
                            state.SelectedStateIndex = idx;
                        }

                        ImGui::Separator();
                        if (ImGui::MenuItem("Delete State"))
                        {
                            state.WorkingJson["States"].erase(state.WorkingJson["States"].begin() + idx);
                            state.NodePositions.erase(state.NodePositions.begin() + idx);
                            if (state.SelectedStateIndex == idx) state.SelectedStateIndex = -1;
                            else if (state.SelectedStateIndex > idx) --state.SelectedStateIndex;
                            if (state.TransitionFromIndex == idx) state.TransitionFromIndex = -1;
                            else if (state.TransitionFromIndex > idx) --state.TransitionFromIndex;
                        }
                    }
                    ImGui::EndPopup();
                }

                // ---- Render canvas ----
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const ImVec2 canvasMax(canvasOrigin.x + canvasSize.x, canvasOrigin.y + canvasSize.y);
                dl->PushClipRect(canvasOrigin, canvasMax, true);

                // Background
                dl->AddRectFilled(canvasOrigin, canvasMax, IM_COL32(35, 35, 35, 255));

                // Grid
                {
                    const float step = kGridStep * state.CanvasZoom;
                    if (step > 5.0f)
                    {
                        float ox = std::fmod(state.CanvasOffset.x * state.CanvasZoom, step);
                        float oy = std::fmod(state.CanvasOffset.y * state.CanvasZoom, step);
                        if (ox < 0.0f) ox += step;
                        if (oy < 0.0f) oy += step;
                        const ImU32 gridCol = IM_COL32(50, 50, 50, 255);
                        const int verticalLineCount = static_cast<int>(std::ceil((canvasSize.x - ox) / step));
                        for (int lineIndex = 0; lineIndex <= verticalLineCount; ++lineIndex)
                        {
                            const float x = ox + static_cast<float>(lineIndex) * step;
                            if (x < 0.0f || x > canvasSize.x)
                                continue;
                            dl->AddLine(ImVec2(canvasOrigin.x + x, canvasOrigin.y),
                                        ImVec2(canvasOrigin.x + x, canvasMax.y), gridCol);
                        }
                        const int horizontalLineCount = static_cast<int>(std::ceil((canvasSize.y - oy) / step));
                        for (int lineIndex = 0; lineIndex <= horizontalLineCount; ++lineIndex)
                        {
                            const float y = oy + static_cast<float>(lineIndex) * step;
                            if (y < 0.0f || y > canvasSize.y)
                                continue;
                            dl->AddLine(ImVec2(canvasOrigin.x, canvasOrigin.y + y),
                                        ImVec2(canvasMax.x, canvasOrigin.y + y), gridCol);
                        }
                    }
                }

                const json& statesArray = state.WorkingJson["States"];
                const std::string defaultStateName =
                    state.WorkingJson.value("DefaultStateName", std::string{});
                const float zoom = state.CanvasZoom;
                const ImU32 arrowCol = IM_COL32(200, 200, 200, 200);

                // Transition arrows
                for (size_t srcIdx = 0; srcIdx < statesArray.size() && srcIdx < state.NodePositions.size(); ++srcIdx)
                {
                    const auto& transitions = statesArray[srcIdx]["Transitions"];
                    if (!transitions.is_array()) continue;

                    for (const auto& transition : transitions)
                    {
                        const std::string toState = transition.value("ToState", std::string{});
                        if (toState.empty()) continue;

                        const int dstIdx = FindStateIndexByName(statesArray, toState);
                        if (dstIdx < 0 || dstIdx >= static_cast<int>(state.NodePositions.size())) continue;

                        if (static_cast<int>(srcIdx) == dstIdx)
                        {
                            // Self-transition loop
                            const ImVec2 top = CanvasToScreen(
                                ImVec2(state.NodePositions[srcIdx].x + kNodeWidth * 0.5f,
                                       state.NodePositions[srcIdx].y),
                                canvasOrigin, state.CanvasOffset, zoom);
                            const ImVec2 p1(top.x - 15.0f * zoom, top.y);
                            const ImVec2 p4(top.x + 15.0f * zoom, top.y);
                            const ImVec2 p2(top.x - 30.0f * zoom, top.y - 50.0f * zoom);
                            const ImVec2 p3(top.x + 30.0f * zoom, top.y - 50.0f * zoom);
                            dl->AddBezierCubic(p1, p2, p3, p4, arrowCol, 2.0f);
                            DrawArrowhead(dl, p4, ImVec2(15.0f, 40.0f),
                                kArrowSize * zoom * 0.7f, arrowCol);
                        }
                        else
                        {
                            const ImVec2 srcCenter = NodeCenterPos(state.NodePositions[srcIdx]);
                            const ImVec2 dstCenter = NodeCenterPos(state.NodePositions[dstIdx]);

                            const ImVec2 p1 = CanvasToScreen(
                                RectEdgePoint(srcCenter, dstCenter, kNodeWidth * 0.5f, kNodeHeight * 0.5f),
                                canvasOrigin, state.CanvasOffset, zoom);
                            const ImVec2 p4 = CanvasToScreen(
                                RectEdgePoint(dstCenter, srcCenter, kNodeWidth * 0.5f, kNodeHeight * 0.5f),
                                canvasOrigin, state.CanvasOffset, zoom);

                            const float midX = (p1.x + p4.x) * 0.5f;
                            const ImVec2 cp1(midX, p1.y);
                            const ImVec2 cp2(midX, p4.y);
                            dl->AddBezierCubic(p1, cp1, cp2, p4, arrowCol, 2.0f);

                            const ImVec2 arrowDir(p4.x - cp2.x, p4.y - cp2.y);
                            DrawArrowhead(dl, p4, arrowDir, kArrowSize * zoom, arrowCol);
                        }
                    }
                }

                // Entry node
                {
                    const int defaultIdx = FindStateIndexByName(statesArray, defaultStateName);
                    ImVec2 entryPos(0.0f, 150.0f);
                    if (defaultIdx >= 0 && defaultIdx < static_cast<int>(state.NodePositions.size()))
                        entryPos.y = state.NodePositions[defaultIdx].y +
                            (kNodeHeight - kEntryNodeHeight) * 0.5f;

                    const ImVec2 eMin = CanvasToScreen(entryPos, canvasOrigin, state.CanvasOffset, zoom);
                    const ImVec2 eMax(eMin.x + kEntryNodeWidth * zoom, eMin.y + kEntryNodeHeight * zoom);

                    dl->AddRectFilled(eMin, eMax, IM_COL32(60, 160, 60, 255), kNodeRounding * zoom);
                    dl->AddRect(eMin, eMax, IM_COL32(30, 30, 30, 255), kNodeRounding * zoom);
                    const ImVec2 ts = ImGui::CalcTextSize("Entry");
                    dl->PushClipRect(eMin, eMax, true);
                    dl->AddText(
                        ImVec2(eMin.x + (eMax.x - eMin.x - ts.x) * 0.5f,
                               eMin.y + (eMax.y - eMin.y - ts.y) * 0.5f),
                        IM_COL32(255, 255, 255, 255), "Entry");
                    dl->PopClipRect();

                    if (defaultIdx >= 0 && defaultIdx < static_cast<int>(state.NodePositions.size()))
                    {
                        const ImVec2 entryCenter = NodeCenterPos(entryPos, kEntryNodeWidth, kEntryNodeHeight);
                        const ImVec2 targetCenter = NodeCenterPos(state.NodePositions[defaultIdx]);
                        const ImVec2 ep1 = CanvasToScreen(
                            RectEdgePoint(entryCenter, targetCenter,
                                kEntryNodeWidth * 0.5f, kEntryNodeHeight * 0.5f),
                            canvasOrigin, state.CanvasOffset, zoom);
                        const ImVec2 ep4 = CanvasToScreen(
                            RectEdgePoint(targetCenter, entryCenter,
                                kNodeWidth * 0.5f, kNodeHeight * 0.5f),
                            canvasOrigin, state.CanvasOffset, zoom);
                        dl->AddLine(ep1, ep4, arrowCol, 2.0f);
                        DrawArrowhead(dl, ep4, ImVec2(ep4.x - ep1.x, ep4.y - ep1.y),
                            kArrowSize * zoom, arrowCol);
                    }
                }

                // State nodes
                for (size_t i = 0; i < state.NodePositions.size() && i < statesArray.size(); ++i)
                {
                    const ImVec2 nMin = CanvasToScreen(state.NodePositions[i],
                        canvasOrigin, state.CanvasOffset, zoom);
                    const ImVec2 nMax(nMin.x + kNodeWidth * zoom, nMin.y + kNodeHeight * zoom);

                    const std::string name = statesArray[i].value("Name", std::string("?"));
                    const bool isDefault = (name == defaultStateName && !defaultStateName.empty());
                    const bool isSelected = (static_cast<int>(i) == state.SelectedStateIndex);

                    const ImU32 fillCol = isDefault
                        ? IM_COL32(230, 150, 50, 255)
                        : IM_COL32(80, 80, 80, 255);
                    dl->AddRectFilled(nMin, nMax, fillCol, kNodeRounding * zoom);

                    const ImU32 borderCol = isSelected
                        ? IM_COL32(68, 138, 255, 255)
                        : IM_COL32(30, 30, 30, 255);
                    const float borderThick = isSelected ? 2.5f : 1.0f;
                    dl->AddRect(nMin, nMax, borderCol, kNodeRounding * zoom, 0, borderThick);

                    dl->PushClipRect(nMin, nMax, true);
                    const ImVec2 textSz = ImGui::CalcTextSize(name.c_str());
                    dl->AddText(
                        ImVec2(nMin.x + (nMax.x - nMin.x - textSz.x) * 0.5f,
                               nMin.y + (nMax.y - nMin.y - textSz.y) * 0.5f),
                        IM_COL32(255, 255, 255, 255), name.c_str());
                    dl->PopClipRect();
                }

                // Transition creation line
                if (state.TransitionFromIndex >= 0 &&
                    state.TransitionFromIndex < static_cast<int>(state.NodePositions.size()))
                {
                    const ImVec2 fromCenter = NodeCenterPos(state.NodePositions[state.TransitionFromIndex]);
                    const ImVec2 fromScreen = CanvasToScreen(fromCenter,
                        canvasOrigin, state.CanvasOffset, zoom);
                    dl->AddLine(fromScreen, mouseScreen, IM_COL32(255, 255, 100, 200), 2.0f);
                }

                dl->PopClipRect();
            }
        }
        ImGui::EndChild();

        // ---- Footer ----
        const bool hasUnsavedChanges = (state.WorkingJson.dump() != state.AppliedJson.dump());
        ImGui::Separator();
        ImGui::BeginDisabled(!hasUnsavedChanges);
        if (ImGui::Button("Apply Changes", ImVec2(180.0f, 0.0f)))
        {
            if (ApplyPendingControllerChanges(undoService, state, "Edit Animator Controller"))
            {
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
        EditorPanelStyle::PopPanelVisualStyle();
    }

    bool ApplyPendingChanges(EditorUndoService* undoService)
    {
        auto& state = GetGraphEditorState();
        return ApplyPendingControllerChanges(undoService, state, "Auto Save Animator Controller");
    }
}
