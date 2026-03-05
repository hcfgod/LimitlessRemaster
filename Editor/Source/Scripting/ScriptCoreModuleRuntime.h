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

    // Incremental script compiler integration.
    void InitializeIncrementalCompiler();
    void ShutdownIncrementalCompiler();
    void SetAutoRecompileOnSave(bool enabled);
    bool IsAutoRecompileOnSave();

    // Returns true (once) when the file watcher has detected script source
    // changes and the debounce window has elapsed.  Call from the main thread.
    bool HasPendingScriptFileChanges();
}
