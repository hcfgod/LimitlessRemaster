#pragma once

#include "../EditorPlayMode.h"

namespace Limitless::ScriptCoreModuleRuntime
{
    void Initialize();
    void Shutdown();
    void Update(EditorPlayModeState playModeState);
}
