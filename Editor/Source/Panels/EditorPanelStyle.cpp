#include "EditorPanelStyle.h"

#include "imgui/imgui.h"

#include <algorithm>
#include <array>

namespace Limitless::EditorPanelStyle
{
    namespace
    {
        const char* GetAxisLabel(int axisIndex)
        {
            static constexpr std::array<const char*, 4> axisLabels = { "X", "Y", "Z", "W" };
            if (axisIndex < 0 || axisIndex >= static_cast<int>(axisLabels.size()))
                return "?";
            return axisLabels[static_cast<size_t>(axisIndex)];
        }

        ImVec4 GetAxisColor(int axisIndex)
        {
            static constexpr std::array<ImVec4, 4> axisColors = {
                ImVec4(0.90f, 0.36f, 0.36f, 1.0f),
                ImVec4(0.47f, 0.78f, 0.32f, 1.0f),
                ImVec4(0.33f, 0.63f, 0.95f, 1.0f),
                ImVec4(0.88f, 0.72f, 0.28f, 1.0f)
            };
            if (axisIndex < 0 || axisIndex >= static_cast<int>(axisColors.size()))
                return ImVec4(0.75f, 0.80f, 0.88f, 1.0f);
            return axisColors[static_cast<size_t>(axisIndex)];
        }
    }

    void PushPanelVisualStyle()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 12.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 8.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(8.0f, 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 7.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 10.0f);

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.03f, 0.05f, 0.09f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.05f, 0.08f, 0.13f, 0.98f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.22f, 0.34f, 0.50f, 0.45f));
        ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.18f, 0.29f, 0.43f, 0.65f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.10f, 0.15f, 0.24f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.14f, 0.21f, 0.33f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.18f, 0.27f, 0.40f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.23f, 0.35f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.31f, 0.46f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.27f, 0.37f, 0.54f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.12f, 0.18f, 0.29f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.18f, 0.26f, 0.40f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.22f, 0.32f, 0.48f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.60f, 0.79f, 1.0f, 1.0f));
    }

    void PopPanelVisualStyle()
    {
        ImGui::PopStyleColor(14);
        ImGui::PopStyleVar(7);
    }

    bool DragFloatNWithAxisLabels(const char* label,
                                  float* values,
                                  int componentCount,
                                  float speed,
                                  float minValue,
                                  float maxValue,
                                  const char* format,
                                  ImGuiSliderFlags flags,
                                  AxisVectorDragState* interactionState)
    {
        if (!label || !values || componentCount < 2 || componentCount > 4)
            return false;

        AxisVectorDragState localInteractionState{};
        AxisVectorDragState& state = interactionState ? *interactionState : localInteractionState;
        state.InteractionId = ImGui::GetID(label);
        state.Activated = false;
        state.DeactivatedAfterEdit = false;

        const ImGuiStyle& style = ImGui::GetStyle();
        const float totalWidth = ImGui::CalcItemWidth();
        const float columnSpacing = style.ItemInnerSpacing.x;
        const float labelSpacing = style.ItemInnerSpacing.x * 0.5f;

        float axisLabelWidth = 0.0f;
        for (int axisIndex = 0; axisIndex < componentCount; ++axisIndex)
            axisLabelWidth = std::max(axisLabelWidth, ImGui::CalcTextSize(GetAxisLabel(axisIndex)).x);

        const float componentWidth = std::max(1.0f, (totalWidth - columnSpacing * static_cast<float>(componentCount - 1)) / static_cast<float>(componentCount));
        const float inputWidth = std::max(1.0f, componentWidth - axisLabelWidth - labelSpacing);

        bool changed = false;
        ImGui::PushID(label);
        for (int axisIndex = 0; axisIndex < componentCount; ++axisIndex)
        {
            if (axisIndex > 0)
                ImGui::SameLine(0.0f, columnSpacing);

            ImGui::BeginGroup();
            ImGui::PushStyleColor(ImGuiCol_Text, GetAxisColor(axisIndex));
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(GetAxisLabel(axisIndex));
            ImGui::PopStyleColor();
            ImGui::SameLine(0.0f, labelSpacing);

            ImGui::PushID(axisIndex);
            ImGui::SetNextItemWidth(inputWidth);
            changed = ImGui::DragFloat("##Value", &values[axisIndex], speed, minValue, maxValue, format, flags) || changed;
            state.Activated = state.Activated || ImGui::IsItemActivated();
            state.DeactivatedAfterEdit = state.DeactivatedAfterEdit || ImGui::IsItemDeactivatedAfterEdit();
            ImGui::PopID();
            ImGui::EndGroup();
        }
        ImGui::PopID();

        return changed;
    }
}
