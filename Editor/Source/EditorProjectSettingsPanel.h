#pragma once

#include "Project/ProjectSettings.h"

#include <string>

namespace Limitless::EditorProjectSettingsPanel
{
    struct EditorProjectSettingsPanelState final
    {
        bool Loaded = false;

        Project::RenderSettings Render;
        Project::AudioSettings Audio;
        Project::InputSettings Input;
        Project::LayersSettings Layers;

        std::string StatusMessage;
        bool StatusIsError = false;
    };

    void Draw(bool& open, EditorProjectSettingsPanelState& state);
}

