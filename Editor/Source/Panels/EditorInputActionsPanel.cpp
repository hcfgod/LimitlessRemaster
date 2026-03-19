#include "EditorInputActionsPanel.h"

#include "EditorPanelStyle.h"
#include "EditorInspectorPanelAssetInspectorsShared.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetImportPipeline.h"
#include "Assets/AssetManager.h"
#include "Assets/AssetPaths.h"
#include "Assets/InputActionsAssetResource.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace Limitless::EditorInputActionsPanel
{
    namespace
    {
        using json = nlohmann::json;
        using namespace EditorInspectorPanel::Internal;

        // ---- Visual constants ----
        constexpr float kListPaneMinWidth   = 180.0f;
        constexpr float kDetailPaneMinWidth = 260.0f;
        constexpr float kToolbarHeight      = 32.0f;
        constexpr float kItemHeight         = 28.0f;
        constexpr float kSectionHeaderHeight = 26.0f;
        constexpr float kCardPadding        = 6.0f;
        constexpr float kCardRounding       = 5.0f;

        constexpr ImVec4 kAccentColor       = ImVec4(0.26f, 0.59f, 0.98f, 1.0f);
        constexpr ImVec4 kAccentHoverColor  = ImVec4(0.30f, 0.63f, 1.00f, 1.0f);
        constexpr ImVec4 kAccentActiveColor = ImVec4(0.22f, 0.52f, 0.90f, 1.0f);
        constexpr ImVec4 kDangerColor       = ImVec4(0.90f, 0.30f, 0.30f, 1.0f);
        constexpr ImVec4 kDangerHoverColor  = ImVec4(1.00f, 0.35f, 0.35f, 1.0f);
        constexpr ImVec4 kCardBgColor       = ImVec4(0.16f, 0.17f, 0.20f, 1.0f);
        constexpr ImVec4 kSelectedBgColor   = ImVec4(0.22f, 0.30f, 0.45f, 1.0f);
        constexpr ImVec4 kHoveredBgColor    = ImVec4(0.20f, 0.22f, 0.28f, 1.0f);
        constexpr ImVec4 kSeparatorColor    = ImVec4(0.30f, 0.32f, 0.36f, 0.60f);
        constexpr ImVec4 kHeaderTextColor   = ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
        constexpr ImVec4 kSubTextColor      = ImVec4(0.55f, 0.55f, 0.58f, 1.0f);
        constexpr ImVec4 kBadgeBgButton     = ImVec4(0.70f, 0.45f, 0.20f, 0.80f);
        constexpr ImVec4 kBadgeBgAxis1D     = ImVec4(0.20f, 0.55f, 0.70f, 0.80f);
        constexpr ImVec4 kBadgeBgAxis2D     = ImVec4(0.30f, 0.65f, 0.35f, 0.80f);

        struct PanelState
        {
            std::string LoadedAssetKey;
            std::filesystem::path ResolvedPath;
            json Json = json::object();
            bool Loaded = false;
            bool PendingSave = false;

            int SelectedMapIndex = 0;
            int SelectedActionIndex = 0;
            int SelectedBindingIndex = -1;

            // Inline rename
            bool RenamingMap = false;
            bool RenamingAction = false;
            std::array<char, 128> RenameBuffer{};

            bool ListeningForScancode = false;
            int ListeningMapIndex = -1;
            int ListeningActionIndex = -1;
            int ListeningBindingIndex = -1;
            std::string ListeningScancodeKey;
            std::string ListeningNameKey;
            std::array<bool, SDL_SCANCODE_COUNT> ListeningKeyboardState{};
        };

        PanelState& GetPanelState()
        {
            static PanelState s_State;
            return s_State;
        }

        // ---- Helpers ----

        ImVec4 GetActionTypeBadgeColor(const std::string& type)
        {
            if (type == "Axis1D") return kBadgeBgAxis1D;
            if (type == "Axis2D") return kBadgeBgAxis2D;
            return kBadgeBgButton;
        }

        const char* GetBindingTypeShortName(const std::string& bindingType)
        {
            if (bindingType == "KeyboardButton")  return "Keyboard";
            if (bindingType == "MouseButton")     return "Mouse";
            if (bindingType == "KeyboardAxis1D")  return "KB Axis 1D";
            if (bindingType == "KeyboardAxis2D")  return "KB Axis 2D";
            if (bindingType == "MouseDelta")      return "Mouse Delta";
            if (bindingType == "GamepadButton")   return "Gamepad Btn";
            if (bindingType == "GamepadAxis1D")   return "GP Axis 1D";
            if (bindingType == "GamepadAxis2D")   return "GP Axis 2D";
            return "Unknown";
        }

        std::string GetBindingSummary(const json& binding)
        {
            const std::string type = binding.value("binding", std::string("KeyboardButton"));
            if (type == "KeyboardButton")
            {
                int sc = ReadScancodeValue(binding, "scancode", "key");
                return GetScancodeDisplayName(sc);
            }
            if (type == "MouseButton")
            {
                int btn = binding.value("button", 1);
                const char* names[] = {"?", "Left", "Middle", "Right", "X1", "X2"};
                if (btn >= 1 && btn <= 5) return std::string(names[btn]) + " Click";
                return "Mouse";
            }
            if (type == "KeyboardAxis1D")
            {
                int neg = ReadScancodeValue(binding, "negative_scancode", "negative");
                int pos = ReadScancodeValue(binding, "positive_scancode", "positive");
                return GetScancodeDisplayName(neg) + " / " + GetScancodeDisplayName(pos);
            }
            if (type == "KeyboardAxis2D")
            {
                int up = ReadScancodeValue(binding, "up_scancode", "up");
                int down = ReadScancodeValue(binding, "down_scancode", "down");
                int left = ReadScancodeValue(binding, "left_scancode", "left");
                int right = ReadScancodeValue(binding, "right_scancode", "right");
                return GetScancodeDisplayName(up) + "/" + GetScancodeDisplayName(left) +
                       "/" + GetScancodeDisplayName(down) + "/" + GetScancodeDisplayName(right);
            }
            if (type == "MouseDelta") return "Mouse Delta";
            if (type == "GamepadButton")
            {
                int btn = ReadGamepadButtonValue(binding, "button_id", "button");
                return GetGamepadButtonDisplayName(btn);
            }
            if (type == "GamepadAxis1D")
            {
                int ax = ReadGamepadAxisValue(binding, "axis_id", "axis");
                return GetGamepadAxisDisplayName(ax);
            }
            if (type == "GamepadAxis2D")
            {
                int xAx = ReadGamepadAxisValue(binding, "x_axis_id", "x_axis");
                int yAx = ReadGamepadAxisValue(binding, "y_axis_id", "y_axis");
                return GetGamepadAxisDisplayName(xAx) + " / " + GetGamepadAxisDisplayName(yAx);
            }
            return type;
        }

        bool DrawPlusButton(const char* id, float diameter = 20.0f)
        {
            const float radius = diameter * 0.5f;
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const ImVec2 center(pos.x + radius, pos.y + radius);

            ImGui::InvisibleButton(id, ImVec2(diameter, diameter));
            const bool hovered = ImGui::IsItemHovered();
            const bool pressed = ImGui::IsItemClicked();

            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec4 bg = hovered ? kAccentHoverColor : kAccentColor;
            dl->AddCircleFilled(center, radius, ImGui::ColorConvertFloat4ToU32(bg));

            const float crossHalf = diameter * 0.22f;
            const ImU32 white = IM_COL32(255, 255, 255, 230);
            dl->AddLine(ImVec2(center.x - crossHalf, center.y), ImVec2(center.x + crossHalf, center.y), white, 1.6f);
            dl->AddLine(ImVec2(center.x, center.y - crossHalf), ImVec2(center.x, center.y + crossHalf), white, 1.6f);
            return pressed;
        }

        bool DangerButton(const char* label, const ImVec2& size = ImVec2(0, 0))
        {
            ImGui::PushStyleColor(ImGuiCol_Button, kDangerColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kDangerHoverColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.75f, 0.20f, 0.20f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            const bool pressed = ImGui::Button(label, size);
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(3);
            return pressed;
        }

        void StopScancodeListening(PanelState& state)
        {
            state.ListeningForScancode = false;
            state.ListeningMapIndex = -1;
            state.ListeningActionIndex = -1;
            state.ListeningBindingIndex = -1;
            state.ListeningScancodeKey.clear();
            state.ListeningNameKey.clear();
            state.ListeningKeyboardState.fill(false);
        }

        void SnapshotListeningKeyboardState(PanelState& state)
        {
            state.ListeningKeyboardState.fill(false);
            int keyCount = 0;
            const auto* keyboardState = SDL_GetKeyboardState(&keyCount);
            if (!keyboardState)
                return;

            const int maxCount = std::min(keyCount, static_cast<int>(SDL_SCANCODE_COUNT));
            for (int sc = 0; sc < maxCount; ++sc)
                state.ListeningKeyboardState[static_cast<size_t>(sc)] = keyboardState[sc] != 0;
        }

        bool IsListeningForScancodeField(const PanelState& state,
                                         const char* scancodeKey,
                                         const char* nameKey)
        {
            return state.ListeningForScancode &&
                   state.ListeningMapIndex == state.SelectedMapIndex &&
                   state.ListeningActionIndex == state.SelectedActionIndex &&
                   state.ListeningBindingIndex == state.SelectedBindingIndex &&
                   state.ListeningScancodeKey == scancodeKey &&
                   state.ListeningNameKey == nameKey;
        }

        void StartScancodeListening(PanelState& state,
                                    const char* scancodeKey,
                                    const char* nameKey)
        {
            state.ListeningForScancode = true;
            state.ListeningMapIndex = state.SelectedMapIndex;
            state.ListeningActionIndex = state.SelectedActionIndex;
            state.ListeningBindingIndex = state.SelectedBindingIndex;
            state.ListeningScancodeKey = scancodeKey;
            state.ListeningNameKey = nameKey;
            SnapshotListeningKeyboardState(state);
        }

        bool DrawListenButton(const char* id, bool listening)
        {
            constexpr float buttonSize = 28.0f;
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const ImVec2 size(buttonSize, buttonSize);
            const ImVec2 center(pos.x + buttonSize * 0.5f, pos.y + buttonSize * 0.5f);

            ImGui::InvisibleButton(id, size);
            const bool hovered = ImGui::IsItemHovered();
            const bool held = ImGui::IsItemActive();
            const bool pressed = ImGui::IsItemClicked();

            const ImVec4 bgColor = listening
                ? (held ? kAccentActiveColor : (hovered ? kAccentHoverColor : kAccentColor))
                : (held ? ImVec4(0.18f, 0.21f, 0.27f, 1.0f)
                        : (hovered ? ImVec4(0.26f, 0.29f, 0.36f, 1.0f) : ImVec4(0.22f, 0.25f, 0.31f, 1.0f)));
            const ImVec4 borderColor = listening ? kAccentHoverColor : ImVec4(0.34f, 0.38f, 0.45f, 1.0f);

            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImGui::ColorConvertFloat4ToU32(bgColor), 6.0f);
            dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImGui::ColorConvertFloat4ToU32(borderColor), 6.0f, 0, 1.0f);

            const ImU32 iconColor = IM_COL32(240, 244, 255, 235);
            const ImVec2 keyMin(center.x - 7.0f, center.y - 5.5f);
            const ImVec2 keyMax(center.x + 1.5f, center.y + 3.0f);
            dl->AddRect(keyMin, keyMax, iconColor, 2.5f, 0, 1.3f);
            dl->AddLine(ImVec2(keyMin.x + 2.0f, keyMin.y + 2.8f), ImVec2(keyMax.x - 2.0f, keyMin.y + 2.8f), iconColor, 1.1f);

            const ImVec2 arcCenter(center.x + 1.5f, center.y + 0.8f);
            dl->PathArcTo(arcCenter, 6.0f, -0.55f, 1.95f, 18);
            dl->PathStroke(iconColor, 0, 1.4f);

            const ImVec2 arrowTip(center.x + 1.5f, center.y + 6.8f);
            dl->AddTriangleFilled(
                arrowTip,
                ImVec2(arrowTip.x - 3.8f, arrowTip.y - 1.4f),
                ImVec2(arrowTip.x - 0.9f, arrowTip.y - 4.4f),
                iconColor);

            if (listening)
            {
                const ImU32 pulseColor = IM_COL32(255, 255, 255, 110);
                dl->AddCircle(center, 10.0f, pulseColor, 24, 1.2f);
            }

            if (hovered)
            {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(listening ? "Listening for input" : "Listen for input");
                ImGui::TextDisabled(listening ? "Press any keyboard key to assign it. Click again to cancel." : "Click to capture the next keyboard key.");
                ImGui::EndTooltip();
            }

            return pressed;
        }

        bool PollScancodeListening(PanelState& state,
                                   json& bindingJson,
                                   const char* scancodeKey,
                                   const char* nameKey)
        {
            if (!IsListeningForScancodeField(state, scancodeKey, nameKey))
                return false;

            int keyCount = 0;
            const auto* keyboardState = SDL_GetKeyboardState(&keyCount);
            if (!keyboardState)
                return false;

            const int maxCount = std::min(keyCount, static_cast<int>(SDL_SCANCODE_COUNT));
            for (int sc = 0; sc < maxCount; ++sc)
            {
                const bool isDown = keyboardState[sc] != 0;
                const bool wasDown = state.ListeningKeyboardState[static_cast<size_t>(sc)];
                if (isDown && !wasDown && sc != static_cast<int>(SDL_SCANCODE_UNKNOWN))
                {
                    WriteScancodeValue(bindingJson, scancodeKey, nameKey, sc);
                    StopScancodeListening(state);
                    return true;
                }
            }

            SnapshotListeningKeyboardState(state);
            return false;
        }

        bool ScancodeSelector(const char* label,
                              json& bindingJson,
                              const char* scancodeKey,
                              const char* nameKey,
                              PanelState& state)
        {
            int scancode = ReadScancodeValue(bindingJson, scancodeKey, nameKey);
            const bool listening = IsListeningForScancodeField(state, scancodeKey, nameKey);
            std::string currentName = listening ? std::string("Listening...") : GetScancodeDisplayName(scancode);
            bool changed = false;

            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            ImGui::SameLine(100.0f);

            const float buttonWidth = 28.0f;
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float comboWidth = std::max(80.0f, ImGui::GetContentRegionAvail().x - buttonWidth - spacing);
            ImGui::SetNextItemWidth(comboWidth);
            if (ImGui::BeginCombo((std::string("##") + scancodeKey).c_str(), currentName.c_str()))
            {
                for (int sc = 4; sc < 232; ++sc)
                {
                    const char* name = SDL_GetScancodeName(static_cast<SDL_Scancode>(sc));
                    if (!name || name[0] == '\0') continue;
                    const bool selected = (sc == scancode);
                    if (ImGui::Selectable(name, selected))
                    {
                        WriteScancodeValue(bindingJson, scancodeKey, nameKey, sc);
                        StopScancodeListening(state);
                        changed = true;
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            ImGui::SameLine(0.0f, spacing);
            if (DrawListenButton((std::string("##Listen") + scancodeKey).c_str(), listening))
            {
                if (listening)
                    StopScancodeListening(state);
                else
                    StartScancodeListening(state, scancodeKey, nameKey);
            }

            if (PollScancodeListening(state, bindingJson, scancodeKey, nameKey))
                changed = true;

            return changed;
        }

        bool DrawImprovedBindingEditor(json& bindingJson, PanelState& state)
        {
            if (!bindingJson.is_object())
            {
                bindingJson = CreateDefaultBindingJson("KeyboardButton");
                return true;
            }

            if (state.ListeningForScancode &&
                (state.ListeningMapIndex != state.SelectedMapIndex ||
                 state.ListeningActionIndex != state.SelectedActionIndex ||
                 state.ListeningBindingIndex != state.SelectedBindingIndex))
            {
                StopScancodeListening(state);
            }

            bool modified = false;
            std::string bindingType = bindingJson.value("binding", std::string("KeyboardButton"));
            int bindingTypeIndex = 0;
            for (size_t i = 0; i < kInputBindingTypes.size(); ++i)
            {
                if (bindingType == kInputBindingTypes[i]) { bindingTypeIndex = static_cast<int>(i); break; }
            }

            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Type");
            ImGui::SameLine(100.0f);
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            if (ImGui::Combo("##BindType", &bindingTypeIndex, kInputBindingTypes.data(), static_cast<int>(kInputBindingTypes.size())))
            {
                bindingType = kInputBindingTypes[static_cast<size_t>(bindingTypeIndex)];
                bindingJson = CreateDefaultBindingJson(bindingType);
                StopScancodeListening(state);
                modified = true;
            }
            ImGui::Spacing();

            if (bindingType == "KeyboardButton")
            {
                if (ScancodeSelector("Key", bindingJson, "scancode", "key", state)) modified = true;
            }
            else if (bindingType == "MouseButton")
            {
                int mouseButton = bindingJson.value("button", static_cast<int>(SDL_BUTTON_LEFT));
                const char* mouseNames[] = {"Left", "Middle", "Right", "X1", "X2"};
                const int mouseVals[] = { static_cast<int>(SDL_BUTTON_LEFT), static_cast<int>(SDL_BUTTON_MIDDLE), static_cast<int>(SDL_BUTTON_RIGHT), static_cast<int>(SDL_BUTTON_X1), static_cast<int>(SDL_BUTTON_X2) };
                int idx = 0;
                for (int i = 0; i < 5; ++i) { if (mouseButton == mouseVals[i]) { idx = i; break; } }
                ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Button"); ImGui::SameLine(100.0f);
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                if (ImGui::Combo("##MouseBtn", &idx, mouseNames, 5))
                { bindingJson["button"] = mouseVals[idx]; modified = true; }
            }
            else if (bindingType == "KeyboardAxis1D")
            {
                if (ScancodeSelector("Negative", bindingJson, "negative_scancode", "negative", state)) modified = true;
                if (ScancodeSelector("Positive", bindingJson, "positive_scancode", "positive", state)) modified = true;
                float negScale = bindingJson.value("negative_scale", -1.0f);
                float posScale = bindingJson.value("positive_scale", 1.0f);
                ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Neg Scale"); ImGui::SameLine(100.0f);
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                if (ImGui::DragFloat("##NegScale", &negScale, 0.05f)) { bindingJson["negative_scale"] = negScale; modified = true; }
                ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Pos Scale"); ImGui::SameLine(100.0f);
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                if (ImGui::DragFloat("##PosScale", &posScale, 0.05f)) { bindingJson["positive_scale"] = posScale; modified = true; }
            }
            else if (bindingType == "KeyboardAxis2D")
            {
                if (ScancodeSelector("Up", bindingJson, "up_scancode", "up", state)) modified = true;
                if (ScancodeSelector("Down", bindingJson, "down_scancode", "down", state)) modified = true;
                if (ScancodeSelector("Left", bindingJson, "left_scancode", "left", state)) modified = true;
                if (ScancodeSelector("Right", bindingJson, "right_scancode", "right", state)) modified = true;
                float scale = bindingJson.value("scale", 1.0f);
                ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Scale"); ImGui::SameLine(100.0f);
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                if (ImGui::DragFloat("##Scale2D", &scale, 0.05f)) { bindingJson["scale"] = scale; modified = true; }
            }
            else if (bindingType == "MouseDelta")
            {
                float sensitivity = bindingJson.value("sensitivity", 1.0f);
                bool invertY = bindingJson.value("invert_y", false);
                ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Sensitivity"); ImGui::SameLine(100.0f);
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                if (ImGui::DragFloat("##Sens", &sensitivity, 0.05f, 0.0f, 50.0f)) { bindingJson["sensitivity"] = sensitivity; modified = true; }
                if (ImGui::Checkbox("Invert Y", &invertY)) { bindingJson["invert_y"] = invertY; modified = true; }
            }
            else if (bindingType == "GamepadButton")
            {
                int btn = ReadGamepadButtonValue(bindingJson, "button_id", "button");
                std::string btnName = GetGamepadButtonDisplayName(btn);
                ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Button"); ImGui::SameLine(100.0f);
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                if (ImGui::DragInt("##GPBtn", &btn, 1.0f, 0, 20)) { WriteGamepadButtonValue(bindingJson, "button_id", "button", btn); modified = true; }
                ImGui::PushStyleColor(ImGuiCol_Text, kSubTextColor); ImGui::Text("  %s", btnName.c_str()); ImGui::PopStyleColor();
            }
            else if (bindingType == "GamepadAxis1D")
            {
                int axis = ReadGamepadAxisValue(bindingJson, "axis_id", "axis");
                float axScale = bindingJson.value("scale", 1.0f);
                float dz = bindingJson.value("deadzone", 0.15f);
                std::string axName = GetGamepadAxisDisplayName(axis);
                ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Axis"); ImGui::SameLine(100.0f);
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                if (ImGui::DragInt("##GPAx", &axis, 1.0f, 0, 10)) { WriteGamepadAxisValue(bindingJson, "axis_id", "axis", axis); modified = true; }
                ImGui::PushStyleColor(ImGuiCol_Text, kSubTextColor); ImGui::Text("  %s", axName.c_str()); ImGui::PopStyleColor();
                ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Scale"); ImGui::SameLine(100.0f);
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                if (ImGui::DragFloat("##GPAxScale", &axScale, 0.05f)) { bindingJson["scale"] = axScale; modified = true; }
                ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Deadzone"); ImGui::SameLine(100.0f);
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                if (ImGui::DragFloat("##GPAxDz", &dz, 0.01f, 0.0f, 1.0f)) { bindingJson["deadzone"] = dz; modified = true; }
            }
            else if (bindingType == "GamepadAxis2D")
            {
                int xAxis = ReadGamepadAxisValue(bindingJson, "x_axis_id", "x_axis");
                int yAxis = ReadGamepadAxisValue(bindingJson, "y_axis_id", "y_axis");
                float gScale = bindingJson.value("scale", 1.0f);
                float gDz = bindingJson.value("deadzone", 0.15f);
                bool gInvY = bindingJson.value("invert_y", false);
                ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("X Axis"); ImGui::SameLine(100.0f);
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                if (ImGui::DragInt("##GPXAx", &xAxis, 1.0f, 0, 10)) { WriteGamepadAxisValue(bindingJson, "x_axis_id", "x_axis", xAxis); modified = true; }
                ImGui::PushStyleColor(ImGuiCol_Text, kSubTextColor); ImGui::Text("  %s", GetGamepadAxisDisplayName(xAxis).c_str()); ImGui::PopStyleColor();
                ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Y Axis"); ImGui::SameLine(100.0f);
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                if (ImGui::DragInt("##GPYAx", &yAxis, 1.0f, 0, 10)) { WriteGamepadAxisValue(bindingJson, "y_axis_id", "y_axis", yAxis); modified = true; }
                ImGui::PushStyleColor(ImGuiCol_Text, kSubTextColor); ImGui::Text("  %s", GetGamepadAxisDisplayName(yAxis).c_str()); ImGui::PopStyleColor();
                ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Scale"); ImGui::SameLine(100.0f);
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                if (ImGui::DragFloat("##GP2Scale", &gScale, 0.05f)) { bindingJson["scale"] = gScale; modified = true; }
                ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Deadzone"); ImGui::SameLine(100.0f);
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                if (ImGui::DragFloat("##GP2Dz", &gDz, 0.01f, 0.0f, 1.0f)) { bindingJson["deadzone"] = gDz; modified = true; }
                if (ImGui::Checkbox("Invert Y", &gInvY)) { bindingJson["invert_y"] = gInvY; modified = true; }
            }
            return modified;
        }

        void SectionHeader(const char* label)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, kHeaderTextColor);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
            ImGui::TextUnformatted(label);
            ImGui::PopStyleColor();
        }

        void DrawBadge(const char* text, const ImVec4& bgColor)
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 textSize = ImGui::CalcTextSize(text);
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const float padX = 6.0f;
            const float padY = 2.0f;
            const ImVec2 badgeMin(pos.x, pos.y);
            const ImVec2 badgeMax(pos.x + textSize.x + padX * 2.0f, pos.y + textSize.y + padY * 2.0f);
            drawList->AddRectFilled(badgeMin, badgeMax, ImGui::ColorConvertFloat4ToU32(bgColor), 3.0f);
            drawList->AddText(ImVec2(badgeMin.x + padX, badgeMin.y + padY), IM_COL32(255, 255, 255, 220), text);
            ImGui::Dummy(ImVec2(textSize.x + padX * 2.0f, textSize.y + padY * 2.0f));
        }

        // ---- Column drawing functions ----

        void DrawActionMapsColumn(json& maps, PanelState& state, bool& modified)
        {
            SectionHeader("Action Maps");
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.0f);
            if (DrawPlusButton("##AddMap"))
            {
                json newMap = json::object();
                int suffix = static_cast<int>(maps.size()) + 1;
                std::string name = "NewMap";
                if (suffix > 1) name += std::to_string(suffix);
                newMap["name"] = name;
                newMap["enabled"] = true;
                newMap["actions"] = json::array();
                maps.push_back(std::move(newMap));
                state.SelectedMapIndex = static_cast<int>(maps.size()) - 1;
                state.SelectedActionIndex = 0;
                state.SelectedBindingIndex = -1;
                modified = true;
            }

            ImGui::Separator();

            if (maps.empty())
            {
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f);
                ImGui::TextDisabled("  No action maps.");
                ImGui::TextDisabled("  Click + to create one.");
                return;
            }

            int removeIndex = -1;
            for (size_t i = 0; i < maps.size(); ++i)
            {
                auto& mapJson = maps[i];
                const std::string mapName = mapJson.value("name", "Map " + std::to_string(i + 1));
                const bool mapEnabled = mapJson.value("enabled", true);
                const bool isSelected = (state.SelectedMapIndex == static_cast<int>(i));

                ImGui::PushID(static_cast<int>(i));

                // Card row
                const float cardWidth = ImGui::GetContentRegionAvail().x;
                ImVec2 cursorPos = ImGui::GetCursorPos();
                ImVec2 screenPos = ImGui::GetCursorScreenPos();
                ImVec2 cardMin = screenPos;
                ImVec2 cardMax(screenPos.x + cardWidth, screenPos.y + kItemHeight);

                // Draw rounded card background
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec4 bg = isSelected ? kSelectedBgColor : kCardBgColor;
                dl->AddRectFilled(cardMin, cardMax, ImGui::ColorConvertFloat4ToU32(bg), 4.0f);
                if (isSelected)
                    dl->AddRect(cardMin, cardMax, ImGui::ColorConvertFloat4ToU32(kAccentColor), 4.0f, 0, 1.0f);

                // Invisible selectable for interaction
                if (ImGui::InvisibleButton("##MapSelect", ImVec2(cardWidth, kItemHeight)))
                {
                    state.SelectedMapIndex = static_cast<int>(i);
                    state.SelectedActionIndex = 0;
                    state.SelectedBindingIndex = -1;
                    state.RenamingMap = false;
                    state.RenamingAction = false;
                }
                const bool mapItemHovered = ImGui::IsItemHovered();
                if (mapItemHovered && !isSelected)
                    dl->AddRectFilled(cardMin, cardMax, ImGui::ColorConvertFloat4ToU32(kHoveredBgColor), 4.0f);

                // Double-click to rename
                if (mapItemHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    state.RenamingMap = true;
                    state.RenamingAction = false;
                    state.SelectedMapIndex = static_cast<int>(i);
                    std::snprintf(state.RenameBuffer.data(), state.RenameBuffer.size(), "%s", mapName.c_str());
                }

                // Context menu
                if (ImGui::BeginPopupContextItem("##MapCtx"))
                {
                    if (ImGui::MenuItem("Rename"))
                    {
                        state.RenamingMap = true;
                        state.RenamingAction = false;
                        state.SelectedMapIndex = static_cast<int>(i);
                        std::snprintf(state.RenameBuffer.data(), state.RenameBuffer.size(), "%s", mapName.c_str());
                    }
                    bool enabled = mapEnabled;
                    if (ImGui::MenuItem("Enabled", nullptr, &enabled))
                    {
                        mapJson["enabled"] = enabled;
                        modified = true;
                    }
                    ImGui::Separator();
                    ImGui::PushStyleColor(ImGuiCol_Text, kDangerColor);
                    if (ImGui::MenuItem("Delete"))
                        removeIndex = static_cast<int>(i);
                    ImGui::PopStyleColor();
                    ImGui::EndPopup();
                }

                // Content overlay
                ImGui::SetCursorPos(ImVec2(cursorPos.x + 10.0f, cursorPos.y + (kItemHeight - ImGui::GetFontSize()) * 0.5f));

                if (isSelected && state.RenamingMap)
                {
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 8.0f);
                    ImGui::SetKeyboardFocusHere();
                    if (ImGui::InputText("##RenameMap", state.RenameBuffer.data(), state.RenameBuffer.size(),
                                         ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
                    {
                        mapJson["name"] = std::string(state.RenameBuffer.data());
                        state.RenamingMap = false;
                        modified = true;
                    }
                    if (!ImGui::IsItemActive() && !ImGui::IsItemFocused() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    {
                        mapJson["name"] = std::string(state.RenameBuffer.data());
                        state.RenamingMap = false;
                        modified = true;
                    }
                }
                else
                {
                    if (!mapEnabled)
                        ImGui::PushStyleColor(ImGuiCol_Text, kSubTextColor);
                    ImGui::TextUnformatted(mapName.c_str());
                    if (!mapEnabled)
                        ImGui::PopStyleColor();
                }

                ImGui::PopID();
            }

            if (removeIndex >= 0)
            {
                maps.erase(static_cast<size_t>(removeIndex));
                if (state.SelectedMapIndex >= static_cast<int>(maps.size()))
                    state.SelectedMapIndex = std::max(0, static_cast<int>(maps.size()) - 1);
                state.SelectedActionIndex = 0;
                state.SelectedBindingIndex = -1;
                modified = true;
            }
        }

        void DrawActionsColumn(json& mapJson, PanelState& state, bool& modified)
        {
            const std::string mapName = mapJson.value("name", std::string("Map"));
            if (!mapJson.contains("actions") || !mapJson["actions"].is_array())
            {
                mapJson["actions"] = json::array();
                modified = true;
            }
            auto& actions = mapJson["actions"];

            SectionHeader(("Actions  -  " + mapName).c_str());
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.0f);
            if (DrawPlusButton("##AddAction"))
            {
                json newAction = json::object();
                int suffix = static_cast<int>(actions.size()) + 1;
                newAction["name"] = "Action" + std::to_string(suffix);
                newAction["type"] = "Button";
                newAction["bindings"] = json::array();
                actions.push_back(std::move(newAction));
                state.SelectedActionIndex = static_cast<int>(actions.size()) - 1;
                state.SelectedBindingIndex = -1;
                modified = true;
            }

            ImGui::Separator();

            if (actions.empty())
            {
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f);
                ImGui::TextDisabled("  No actions in this map.");
                ImGui::TextDisabled("  Click + to add one.");
                return;
            }

            int removeIndex = -1;
            for (size_t i = 0; i < actions.size(); ++i)
            {
                auto& actionJson = actions[i];
                if (!actionJson.is_object())
                {
                    actionJson = json::object();
                    modified = true;
                }
                const std::string actionName = actionJson.value("name", "Action " + std::to_string(i + 1));
                const std::string actionType = actionJson.value("type", std::string("Button"));
                const bool isSelected = (state.SelectedActionIndex == static_cast<int>(i));

                ImGui::PushID(static_cast<int>(i));

                const float actionCardHeight = kItemHeight + 14.0f;
                const float actionCardWidth = ImGui::GetContentRegionAvail().x;
                ImVec2 cursorPos = ImGui::GetCursorPos();
                ImVec2 screenPos = ImGui::GetCursorScreenPos();
                ImVec2 acMin = screenPos;
                ImVec2 acMax(screenPos.x + actionCardWidth, screenPos.y + actionCardHeight);

                // Draw rounded card background
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec4 acBg = isSelected ? kSelectedBgColor : kCardBgColor;
                dl->AddRectFilled(acMin, acMax, ImGui::ColorConvertFloat4ToU32(acBg), 4.0f);
                if (isSelected)
                    dl->AddRect(acMin, acMax, ImGui::ColorConvertFloat4ToU32(kAccentColor), 4.0f, 0, 1.0f);

                // Invisible button for interaction
                if (ImGui::InvisibleButton("##ActionSelect", ImVec2(actionCardWidth, actionCardHeight)))
                {
                    state.SelectedActionIndex = static_cast<int>(i);
                    state.SelectedBindingIndex = -1;
                    state.RenamingAction = false;
                    state.RenamingMap = false;
                }
                const bool actionItemHovered = ImGui::IsItemHovered();
                if (actionItemHovered && !isSelected)
                    dl->AddRectFilled(acMin, acMax, ImGui::ColorConvertFloat4ToU32(kHoveredBgColor), 4.0f);

                // Double-click to rename
                if (actionItemHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    state.RenamingAction = true;
                    state.RenamingMap = false;
                    state.SelectedActionIndex = static_cast<int>(i);
                    std::snprintf(state.RenameBuffer.data(), state.RenameBuffer.size(), "%s", actionName.c_str());
                }

                // Context menu
                if (ImGui::BeginPopupContextItem("##ActionCtx"))
                {
                    if (ImGui::MenuItem("Rename"))
                    {
                        state.RenamingAction = true;
                        state.RenamingMap = false;
                        state.SelectedActionIndex = static_cast<int>(i);
                        std::snprintf(state.RenameBuffer.data(), state.RenameBuffer.size(), "%s", actionName.c_str());
                    }
                    ImGui::Separator();
                    ImGui::PushStyleColor(ImGuiCol_Text, kDangerColor);
                    if (ImGui::MenuItem("Delete"))
                        removeIndex = static_cast<int>(i);
                    ImGui::PopStyleColor();
                    ImGui::EndPopup();
                }

                // Content overlay
                ImGui::SetCursorPos(ImVec2(cursorPos.x + 10.0f, cursorPos.y + 5.0f));

                if (isSelected && state.RenamingAction)
                {
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 8.0f);
                    ImGui::SetKeyboardFocusHere();
                    if (ImGui::InputText("##RenameAction", state.RenameBuffer.data(), state.RenameBuffer.size(),
                                         ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
                    {
                        actionJson["name"] = std::string(state.RenameBuffer.data());
                        state.RenamingAction = false;
                        modified = true;
                    }
                    if (!ImGui::IsItemActive() && !ImGui::IsItemFocused() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    {
                        actionJson["name"] = std::string(state.RenameBuffer.data());
                        state.RenamingAction = false;
                        modified = true;
                    }
                }
                else
                {
                    // Action name
                    ImGui::TextUnformatted(actionName.c_str());

                    // Type badge on same line
                    ImGui::SameLine();
                    DrawBadge(actionType.c_str(), GetActionTypeBadgeColor(actionType));
                }

                // Binding count subtitle
                if (!(isSelected && state.RenamingAction))
                {
                    size_t bindingCount = 0;
                    if (actionJson.contains("bindings") && actionJson["bindings"].is_array())
                        bindingCount = actionJson["bindings"].size();

                    ImGui::SetCursorPos(ImVec2(cursorPos.x + 10.0f, cursorPos.y + kItemHeight - 2.0f));
                    ImGui::PushStyleColor(ImGuiCol_Text, kSubTextColor);
                    ImGui::Text("%zu binding%s", bindingCount, bindingCount == 1 ? "" : "s");
                    ImGui::PopStyleColor();
                }

                ImGui::PopID();
            }

            if (removeIndex >= 0)
            {
                actions.erase(static_cast<size_t>(removeIndex));
                if (state.SelectedActionIndex >= static_cast<int>(actions.size()))
                    state.SelectedActionIndex = std::max(0, static_cast<int>(actions.size()) - 1);
                state.SelectedBindingIndex = -1;
                modified = true;
            }
        }

        void DrawBindingCard(json& bindingJson, int bindingIndex, PanelState& state, bool& modified, bool& removeRequested)
        {
            const std::string bindingType = bindingJson.value("binding", std::string("KeyboardButton"));
            const std::string summary = GetBindingSummary(bindingJson);
            const bool isSelected = (state.SelectedBindingIndex == bindingIndex);

            ImGui::PushID(bindingIndex);

            // Card background
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImVec2 cardMin = ImGui::GetCursorScreenPos();
            float cardWidth = ImGui::GetContentRegionAvail().x;

            // Draw as expandable card
            ImVec4 cardBg = isSelected ? kSelectedBgColor : kCardBgColor;
            ImVec2 headerMin = cardMin;
            ImVec2 headerMax(cardMin.x + cardWidth, cardMin.y + kItemHeight + 4.0f);

            drawList->AddRectFilled(headerMin, headerMax, ImGui::ColorConvertFloat4ToU32(cardBg), kCardRounding);
            if (isSelected)
                drawList->AddRect(headerMin, headerMax, ImGui::ColorConvertFloat4ToU32(kAccentColor), kCardRounding, 0, 1.0f);

            // Clickable header area
            ImGui::SetCursorScreenPos(headerMin);
            if (ImGui::InvisibleButton("##BindingSelect", ImVec2(cardWidth, kItemHeight + 4.0f)))
            {
                state.SelectedBindingIndex = isSelected ? -1 : bindingIndex;
            }
            const bool bindingHeaderHovered = ImGui::IsItemHovered();
            if (bindingHeaderHovered && !isSelected)
                drawList->AddRectFilled(headerMin, headerMax, ImGui::ColorConvertFloat4ToU32(kHoveredBgColor), kCardRounding);

            // Context menu
            if (ImGui::BeginPopupContextItem("##BindingCtx"))
            {
                ImGui::PushStyleColor(ImGuiCol_Text, kDangerColor);
                if (ImGui::MenuItem("Delete Binding"))
                    removeRequested = true;
                ImGui::PopStyleColor();
                ImGui::EndPopup();
            }

            // Header content overlay - vertically centered
            const float headerTextY = headerMin.y + ((kItemHeight + 4.0f) - ImGui::GetFontSize()) * 0.5f;
            ImGui::SetCursorScreenPos(ImVec2(headerMin.x + 10.0f, headerTextY));
            ImGui::PushStyleColor(ImGuiCol_Text, kSubTextColor);
            ImGui::Text("%s", GetBindingTypeShortName(bindingType));
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextUnformatted(summary.c_str());

            // Expand/collapse triangle indicator
            {
                const float triSize = 4.5f;
                const ImVec2 triCenter(headerMax.x - 14.0f, (headerMin.y + headerMax.y) * 0.5f);
                const ImU32 triColor = ImGui::ColorConvertFloat4ToU32(kSubTextColor);
                if (isSelected)
                {
                    drawList->AddTriangleFilled(
                        ImVec2(triCenter.x - triSize, triCenter.y - triSize * 0.6f),
                        ImVec2(triCenter.x + triSize, triCenter.y - triSize * 0.6f),
                        ImVec2(triCenter.x, triCenter.y + triSize * 0.8f),
                        triColor);
                }
                else
                {
                    drawList->AddTriangleFilled(
                        ImVec2(triCenter.x - triSize * 0.6f, triCenter.y - triSize),
                        ImVec2(triCenter.x + triSize * 0.8f, triCenter.y),
                        ImVec2(triCenter.x - triSize * 0.6f, triCenter.y + triSize),
                        triColor);
                }
            }

            // Expanded detail area
            if (isSelected)
            {
                ImGui::SetCursorScreenPos(ImVec2(cardMin.x + 10.0f, headerMax.y + 8.0f));
                ImGui::PushItemWidth(cardWidth - 20.0f);

                if (DrawImprovedBindingEditor(bindingJson, state))
                    modified = true;

                ImGui::PopItemWidth();
                ImGui::Spacing();

                // Delete button
                if (DangerButton("Delete Binding##Del", ImVec2(ImGui::GetContentRegionAvail().x - 10.0f, 0.0f)))
                    removeRequested = true;

                ImGui::Spacing();
            }

            ImGui::PopID();
        }

        void DrawDetailColumn(json& actionJson, const std::string& actionType, PanelState& state, bool& modified)
        {
            const std::string actionName = actionJson.value("name", std::string("Action"));

            // Action properties section
            {
                SectionHeader("Action Properties");
                ImGui::Separator();
                ImGui::Spacing();

                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.65f);

                // Action name
                std::array<char, 128> nameBuffer{};
                std::snprintf(nameBuffer.data(), nameBuffer.size(), "%s", actionName.c_str());
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Name");
                ImGui::SameLine(90.0f);
                if (ImGui::InputText("##ActionName", nameBuffer.data(), nameBuffer.size()))
                {
                    actionJson["name"] = std::string(nameBuffer.data());
                    modified = true;
                }

                // Action type
                std::string currentType = actionJson.value("type", std::string("Button"));
                int typeIndex = 0;
                for (size_t i = 0; i < kInputActionValueTypes.size(); ++i)
                {
                    if (currentType == kInputActionValueTypes[i])
                    {
                        typeIndex = static_cast<int>(i);
                        break;
                    }
                }
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Type");
                ImGui::SameLine(90.0f);
                if (ImGui::Combo("##ActionType", &typeIndex, kInputActionValueTypes.data(), static_cast<int>(kInputActionValueTypes.size())))
                {
                    actionJson["type"] = std::string(kInputActionValueTypes[static_cast<size_t>(typeIndex)]);
                    modified = true;
                }

                ImGui::PopItemWidth();
            }

            ImGui::Spacing();
            ImGui::Spacing();

            // Bindings section
            {
                if (!actionJson.contains("bindings") || !actionJson["bindings"].is_array())
                {
                    actionJson["bindings"] = json::array();
                    modified = true;
                }
                auto& bindings = actionJson["bindings"];

                SectionHeader("Bindings");
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.0f);
                if (DrawPlusButton("##AddBinding"))
                {
                    const std::string defaultType = GetDefaultBindingTypeForActionType(actionType);
                    bindings.push_back(CreateDefaultBindingJson(defaultType));
                    state.SelectedBindingIndex = static_cast<int>(bindings.size()) - 1;
                    modified = true;
                }
                ImGui::Separator();

                if (bindings.empty())
                {
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f);
                    ImGui::TextDisabled("  No bindings. Click + to add one.");
                    return;
                }

                int removeBindingIndex = -1;
                for (size_t i = 0; i < bindings.size(); ++i)
                {
                    bool removeRequested = false;
                    DrawBindingCard(bindings[i], static_cast<int>(i), state, modified, removeRequested);
                    if (removeRequested)
                        removeBindingIndex = static_cast<int>(i);
                    ImGui::Spacing();
                }

                if (removeBindingIndex >= 0)
                {
                    bindings.erase(static_cast<size_t>(removeBindingIndex));
                    if (state.SelectedBindingIndex >= static_cast<int>(bindings.size()))
                        state.SelectedBindingIndex = static_cast<int>(bindings.size()) - 1;
                    modified = true;
                }
            }
        }

    } // anonymous namespace

    void Draw(bool& isOpen, const std::string& inputActionsAssetKey, bool requestFocus)
    {
        if (!isOpen)
            return;

        EditorPanelStyle::PushPanelVisualStyle();
        if (requestFocus)
            ImGui::SetNextWindowFocus();
        ImGui::SetNextWindowSize(ImVec2(800.0f, 420.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Input Actions", &isOpen))
        {
            ImGui::End();
            EditorPanelStyle::PopPanelVisualStyle();
            return;
        }

        auto& state = GetPanelState();

        if (inputActionsAssetKey.empty())
        {
            ImGui::TextDisabled("Select an Input Actions asset to edit.");
            ImGui::End();
            EditorPanelStyle::PopPanelVisualStyle();
            return;
        }

        // Load / reload when asset key changes
        if (!state.Loaded || state.LoadedAssetKey != inputActionsAssetKey)
        {
            state = PanelState{};
            state.LoadedAssetKey = inputActionsAssetKey;
            state.Loaded = LoadInputActionsJson(inputActionsAssetKey, state.Json, state.ResolvedPath);
            if (!state.Loaded)
            {
                ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Failed to load: %s", inputActionsAssetKey.c_str());
                ImGui::End();
                EditorPanelStyle::PopPanelVisualStyle();
                return;
            }
        }

        bool modified = false;

        // Toolbar
        {
            const std::string filename = std::filesystem::path(inputActionsAssetKey).filename().string();
            ImGui::PushStyleColor(ImGuiCol_Text, kHeaderTextColor);
            ImGui::Text("%s", filename.c_str());
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, kSubTextColor);
            ImGui::Text("  %s", inputActionsAssetKey.c_str());
            ImGui::PopStyleColor();
            if (state.PendingSave)
            {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), " (unsaved)");
            }
            ImGui::Separator();
        }

        // Ensure maps array
        if (!state.Json.contains("maps") || !state.Json["maps"].is_array())
        {
            state.Json["maps"] = json::array();
            modified = true;
        }
        auto& maps = state.Json["maps"];

        // Clamp selection indices
        if (state.SelectedMapIndex >= static_cast<int>(maps.size()))
            state.SelectedMapIndex = std::max(0, static_cast<int>(maps.size()) - 1);

        // Three-column layout using child regions
        const float availWidth = ImGui::GetContentRegionAvail().x;
        const float availHeight = ImGui::GetContentRegionAvail().y;
        const float col1Width = std::max(kListPaneMinWidth, availWidth * 0.22f);
        const float col2Width = std::max(kListPaneMinWidth, availWidth * 0.28f);
        const float col3Width = std::max(kDetailPaneMinWidth, availWidth - col1Width - col2Width - 8.0f);

        // Column 1: Action Maps
        ImGui::BeginChild("##MapsCol", ImVec2(col1Width, availHeight), ImGuiChildFlags_Borders);
        DrawActionMapsColumn(maps, state, modified);
        ImGui::EndChild();

        ImGui::SameLine();

        // Column 2: Actions
        ImGui::BeginChild("##ActionsCol", ImVec2(col2Width, availHeight), ImGuiChildFlags_Borders);
        if (!maps.empty() && state.SelectedMapIndex >= 0 && state.SelectedMapIndex < static_cast<int>(maps.size()))
        {
            auto& selectedMap = maps[static_cast<size_t>(state.SelectedMapIndex)];
            if (!selectedMap.contains("actions") || !selectedMap["actions"].is_array())
            {
                selectedMap["actions"] = json::array();
                modified = true;
            }
            auto& actions = selectedMap["actions"];
            if (state.SelectedActionIndex >= static_cast<int>(actions.size()))
                state.SelectedActionIndex = std::max(0, static_cast<int>(actions.size()) - 1);

            DrawActionsColumn(selectedMap, state, modified);
        }
        else
        {
            ImGui::TextDisabled("  Select an action map.");
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // Column 3: Detail / Bindings
        ImGui::BeginChild("##DetailCol", ImVec2(col3Width, availHeight), ImGuiChildFlags_Borders);
        if (!maps.empty() && state.SelectedMapIndex >= 0 && state.SelectedMapIndex < static_cast<int>(maps.size()))
        {
            auto& selectedMap = maps[static_cast<size_t>(state.SelectedMapIndex)];
            auto& actions = selectedMap["actions"];
            if (!actions.empty() && state.SelectedActionIndex >= 0 && state.SelectedActionIndex < static_cast<int>(actions.size()))
            {
                auto& selectedAction = actions[static_cast<size_t>(state.SelectedActionIndex)];
                const std::string actionType = selectedAction.value("type", std::string("Button"));
                DrawDetailColumn(selectedAction, actionType, state, modified);
            }
            else
            {
                ImGui::TextDisabled("  Select an action to edit.");
            }
        }
        else
        {
            ImGui::TextDisabled("  Select an action map first.");
        }
        ImGui::EndChild();

        // Deferred save
        if (modified)
            state.PendingSave = true;

        if (state.PendingSave &&
            !ImGui::IsAnyItemActive() &&
            !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) &&
            !state.ListeningForScancode)
        {
            if (SaveInputActionsJsonAndReload(inputActionsAssetKey, state.Json, state.ResolvedPath))
                state.PendingSave = false;
        }

        ImGui::End();
        EditorPanelStyle::PopPanelVisualStyle();
    }

    void ResetState()
    {
        GetPanelState() = PanelState{};
    }
}
