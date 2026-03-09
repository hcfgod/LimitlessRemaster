#pragma once

#include "imgui/imgui.h"

namespace Limitless::EditorPanelStyle
{
    struct AxisVectorDragState final
    {
        ImGuiID InteractionId = 0;
        bool Activated = false;
        bool DeactivatedAfterEdit = false;
    };

    void PushPanelVisualStyle();
    void PopPanelVisualStyle();
    bool DragFloatNWithAxisLabels(const char* label,
                                  float* values,
                                  int componentCount,
                                  float speed = 1.0f,
                                  float minValue = 0.0f,
                                  float maxValue = 0.0f,
                                  const char* format = "%.3f",
                                  ImGuiSliderFlags flags = 0,
                                  AxisVectorDragState* interactionState = nullptr);
}
