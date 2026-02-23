#pragma once

#include "Core/EditorPlayMode.h"

namespace Limitless::ScriptCoreModuleRuntime
{
    void SetGameplayInputRoutingState(bool gameViewFocused,
                                      bool gameViewHovered,
                                      bool uiWantsMouseCapture,
                                      bool uiWantsKeyboardCapture);
    void Initialize();
    void Shutdown();
    void Update(EditorPlayModeState playModeState);
}
