#pragma once

#include "EditorPlayMode.h"

#include <functional>
#include <string>

namespace Limitless::EditorMenuBar
{
    void Draw(EditorPlayModeState playModeState,
              bool& showDemoWindow,
              bool& showAssetDiagnosticsWindow,
              bool& showBuildAndRunWindow,
              const std::function<void()>& onOpenProject,
              const std::function<void()>& onCreateProject,
              const std::function<void()>& onProjectSettings,
              const std::function<void()>& onReimportChangedAssets,
              const std::function<void()>& onReimportAllAssets,
              const std::function<void()>& onValidateAssetDatabase,
              const std::function<void()>& onNewScene,
              const std::function<void()>& onSaveScene,
              const std::function<void()>& onSaveSceneAs,
              const std::function<void()>& onUndo,
              const std::function<void()>& onRedo,
              bool canUndo,
              bool canRedo,
              const std::string& undoLabel,
              const std::string& redoLabel,
              const std::function<void()>& onPlay,
              const std::function<void()>& onStop,
              const std::function<void()>& onTogglePause);
}
