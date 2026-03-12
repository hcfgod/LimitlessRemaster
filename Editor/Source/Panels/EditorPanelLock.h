#pragma once

#include "imgui/imgui.h"

namespace Limitless::EditorPanelLock
{
    /// Draws a padlock shape into the given draw list.
    inline void DrawPadlockIcon(ImDrawList* dl, ImVec2 center, float size, ImU32 color, bool locked)
    {
        const float bodyW = size * 0.60f;
        const float bodyH = size * 0.46f;
        const float bodyTop = center.y + size * 0.04f;
        const ImVec2 bodyMin(center.x - bodyW * 0.5f, bodyTop);
        const ImVec2 bodyMax(bodyMin.x + bodyW, bodyTop + bodyH);
        dl->AddRectFilled(bodyMin, bodyMax, color, 2.5f);

        const float shackleR = bodyW * 0.36f;
        const float strokeW = size * 0.14f;

        if (locked)
        {
            dl->PathArcTo(ImVec2(center.x, bodyTop), shackleR, 3.14159f, 6.28318f, 16);
            dl->PathStroke(color, 0, strokeW);
        }
        else
        {
            dl->PathArcTo(ImVec2(center.x + shackleR * 0.40f, bodyTop), shackleR, 3.14159f, 4.71239f, 12);
            dl->PathStroke(color, 0, strokeW);
        }

        const float dotR = size * 0.07f;
        dl->AddCircleFilled(ImVec2(center.x, bodyTop + bodyH * 0.45f), dotR, IM_COL32(0, 0, 0, 200));
    }

    /// Draws a lock toggle button at the top-right of the panel.
    /// Call immediately after ImGui::Begin() succeeds.
    /// Returns the new lock state after interaction.
    inline bool DrawLockToggle(bool isLocked)
    {
        const float btnSize = 22.0f;
        const float availW = ImGui::GetContentRegionAvail().x;

        // Right-align: invisible spacer pushes button to far right with margin.
        const float rightMargin = 6.0f;
        ImGui::Dummy(ImVec2(availW - btnSize - rightMargin, btnSize));
        ImGui::SameLine();

        ImGui::PushID("PanelLockToggle");
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_Button,
            isLocked ? ImVec4(0.40f, 0.24f, 0.10f, 1.0f) : ImVec4(0.14f, 0.18f, 0.24f, 0.60f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
            isLocked ? ImVec4(0.50f, 0.32f, 0.14f, 1.0f) : ImVec4(0.22f, 0.28f, 0.36f, 0.80f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
            isLocked ? ImVec4(0.58f, 0.38f, 0.18f, 1.0f) : ImVec4(0.28f, 0.34f, 0.44f, 1.0f));

        const bool clicked = ImGui::Button("##LockBtn", ImVec2(btnSize, btnSize));

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(2);

        // Draw padlock on top of the button rect.
        const ImVec2 rMin = ImGui::GetItemRectMin();
        const ImVec2 rMax = ImGui::GetItemRectMax();
        DrawPadlockIcon(
            ImGui::GetWindowDrawList(),
            ImVec2((rMin.x + rMax.x) * 0.5f, (rMin.y + rMax.y) * 0.5f),
            btnSize * 0.72f,
            isLocked ? IM_COL32(255, 200, 90, 255) : IM_COL32(150, 165, 185, 255),
            isLocked);

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip(isLocked ? "Unlock panel" : "Lock panel");

        ImGui::PopID();
        ImGui::Separator();

        return clicked ? !isLocked : isLocked;
    }
}
