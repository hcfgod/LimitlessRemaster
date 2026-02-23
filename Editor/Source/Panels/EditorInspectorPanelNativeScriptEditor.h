#pragma once

#include "Limitless.h"

#include <string>
#include <vector>

namespace Limitless::EditorInspectorPanel
{
    void RestorePendingNativeScriptEditorSession();
    void DrawNativeScriptEditorWindow();

    bool IsNativeScriptBuildInProgress();
    bool TriggerNativeScriptBuildFromInspector();
    bool HasAnyProjectNativeScriptSourcesForInspector();
    std::vector<std::string> DiscoverNativeScriptClassNamesFromProjectAssetsForInspector();
    std::string ResolveRegisteredScriptClassNameForInspector(const std::string& requestedClassName);
    bool SynchronizeExposedPropertiesFromScriptForInspector(NativeScriptEntry& nativeScript,
                                                            std::vector<std::string>& outFieldOrder,
                                                            std::string& outError);

    bool GetNativeScriptDebugInfoEnabled();
    void SetNativeScriptDebugInfoEnabled(bool enabled);
}
