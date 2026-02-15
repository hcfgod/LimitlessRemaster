#pragma once

#include "Project/ProjectSettings.h"

#include <array>
#include <vector>
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
        Project::Physics2DSettings Physics2D;
        Project::Lighting2DSettings Lighting2D;

        std::vector<std::string> AvailableInputActionsAssetKeys;
        int SelectedAdditionalInputActionsIndex = -1;
        int SelectedAvailableInputActionsIndex = -1;
        std::array<char, 128> SelectedAdditionalInputAliasBuffer{};
        int SelectedAdditionalInputAliasBufferSourceIndex = -1;

        std::string StatusMessage;
        bool StatusIsError = false;
    };

    void Draw(bool& open, EditorProjectSettingsPanelState& state);
}

