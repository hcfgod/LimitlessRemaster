#pragma once

#include "EditorPlayMode.h"

#include <functional>

namespace Limitless::EditorMenuBar
{
    void Draw(EditorPlayModeState playModeState,
              bool& showDemoWindow,
              const std::function<void()>& onNewScene,
              const std::function<void()>& onSaveScene,
              const std::function<void()>& onSaveSceneAs,
              const std::function<void()>& onPlay,
              const std::function<void()>& onStop,
              const std::function<void()>& onTogglePause);
}
