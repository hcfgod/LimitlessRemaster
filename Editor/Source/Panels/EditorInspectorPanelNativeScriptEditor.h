#pragma once

#include "Limitless.h"

#include <string>
#include <vector>

namespace Limitless::EditorInspectorPanel
{
    struct ProjectNativeScriptInfo final
    {
        std::string ScriptClassName;
        std::string ScriptAssetRelativePath;
        std::string FolderRelativePath;
        std::string DisplayName;
        bool LikelyDerivesFromScriptableEntity = true;
    };

    void RestorePendingNativeScriptEditorSession();
    void DrawNativeScriptEditorWindow();

    bool IsNativeScriptBuildInProgress();
    bool TriggerNativeScriptBuildFromInspector();
    bool HasAnyProjectNativeScriptSourcesForInspector();
    std::vector<std::string> DiscoverNativeScriptClassNamesFromProjectAssetsForInspector();
    std::vector<ProjectNativeScriptInfo> GetAvailableProjectScriptsForInspector();
    std::vector<std::string> GetAvailableProjectScriptClassNamesForInspector();
    std::string ResolveRegisteredScriptClassNameForInspector(const std::string& requestedClassName);
    bool SynchronizeExposedPropertiesFromScriptForInspector(NativeScriptEntry& nativeScript,
                                                            std::vector<std::string>& outFieldOrder,
                                                            std::string& outError);

    bool GetNativeScriptDebugInfoEnabled();
    void SetNativeScriptDebugInfoEnabled(bool enabled);
}
