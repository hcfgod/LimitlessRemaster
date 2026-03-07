#include "EditorPanelStyle.h"

#include "imgui/imgui.h"

namespace Limitless::EditorPanelStyle
{
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
}
