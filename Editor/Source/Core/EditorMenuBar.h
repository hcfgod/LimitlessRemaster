#pragma once

#include "EditorPlayMode.h"

#include <functional>
#include <string>

namespace Limitless::EditorMenuBar
{
    void Draw(EditorPlayModeState playModeState,
              bool& showScenePanel,
              bool& showInspectorPanel,
              bool& showSceneView,
              bool& showGameView,
              bool& showProjectPanel,
              bool& showDemoWindow,
              bool& showAssetDiagnosticsWindow,
              bool& showPhysicsDiagnosticsWindow,
              bool& showConsoleWindow,
              bool& showEditorFpsOverlay,
              bool& showGizmoToolbar,
              bool& showPerformancePanel,
              bool& showAnimationTimelinePanel,
              bool& showAnimatorGraphPanel,
              bool& showTilePalettePanel,
              const std::function<void()>& onOpenProject,
              const std::function<void()>& onCreateProject,
              const std::function<void()>& onProjectSettings,
              const std::function<void()>& onBuildSettings,
              const std::function<void()>& onBuildScripts,
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
              const std::function<void()>& onSimulate,
              const std::function<void()>& onStop,
              const std::function<void()>& onTogglePause,
              bool isEditingPrefabAsset,
              const std::string& prefabAssetDisplayName,
              bool canReturnFromPrefabMode,
              const std::function<void()>& onReturnFromPrefabMode,
              bool canApplyPrefabToInstances,
              const std::function<void()>& onApplyPrefabToInstances,
              const std::function<void()>& onDrawLayoutsMenu,
              const std::function<void()>& onResetLayoutToDefault);
}
