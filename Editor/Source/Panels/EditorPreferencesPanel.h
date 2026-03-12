#pragma once

#include "Utilities/EditorPreferences.h"

namespace Limitless::EditorPreferencesPanel
{
    struct EditorPreferencesPanelState final
    {
        bool Loaded = false;
        Editor::EditorPreferencesData Preferences{};
    };

    void Draw(bool& open, EditorPreferencesPanelState& state);
}
