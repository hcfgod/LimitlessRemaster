#include "PrecompiledHeader.h"
#include "EditorInspectorPanelNativeScriptEditor.h"
#include "EditorInspectorPanelNativeScriptEditorShared.h"

namespace Limitless::EditorInspectorPanel
{
    void RestorePendingNativeScriptEditorSession()
    {
        Internal::RestorePendingNativeScriptEditorSession();
    }

    void DrawNativeScriptEditorWindow()
    {
        Internal::DrawNativeScriptEditorWindow();
    }

    bool IsNativeScriptBuildInProgress()
    {
        return Internal::IsNativeScriptBuildInProgress();
    }

    bool TriggerNativeScriptBuildFromInspector()
    {
        return Internal::TriggerNativeScriptBuildFromInspector();
    }

    bool HasAnyProjectNativeScriptSourcesForInspector()
    {
        return Internal::HasAnyProjectNativeScriptSourcesForInspector();
    }

    std::vector<std::string> DiscoverNativeScriptClassNamesFromProjectAssetsForInspector()
    {
        return Internal::DiscoverNativeScriptClassNamesFromProjectAssetsForInspector();
    }

    std::vector<ProjectNativeScriptInfo> GetAvailableProjectScriptsForInspector()
    {
        return Internal::GetAvailableProjectScriptsForInspector();
    }

    std::vector<std::string> GetAvailableProjectScriptClassNamesForInspector()
    {
        return Internal::GetAvailableProjectScriptClassNamesForInspector();
    }

    std::string ResolveRegisteredScriptClassNameForInspector(const std::string& requestedClassName)
    {
        return Internal::ResolveRegisteredScriptClassNameForInspector(requestedClassName);
    }

    bool SynchronizeExposedPropertiesFromScriptForInspector(NativeScriptEntry& nativeScript,
                                                            std::vector<std::string>& outFieldOrder,
                                                            std::string& outError)
    {
        return Internal::SynchronizeExposedPropertiesFromScriptForInspector(nativeScript, outFieldOrder, outError);
    }

    bool GetNativeScriptDebugInfoEnabled()
    {
        return Internal::GetNativeScriptDebugInfoEnabled();
    }

    void SetNativeScriptDebugInfoEnabled(bool enabled)
    {
        Internal::SetNativeScriptDebugInfoEnabled(enabled);
    }

    void GetNativeScriptEditorSessionState(NativeScriptEditorSessionState& outState)
    {
        Internal::GetNativeScriptEditorSessionState(outState);
    }

    void ApplyNativeScriptEditorSessionState(const NativeScriptEditorSessionState& state)
    {
        Internal::ApplyNativeScriptEditorSessionState(state);
    }

    bool OpenNativeScriptEditorForAssetKey(const std::string& assetKey)
    {
        return Internal::OpenNativeScriptEditorForAssetKey(assetKey);
    }

    bool BuildProjectNativeScripts(std::string* outStatusMessage)
    {
        return Internal::BuildProjectNativeScripts(outStatusMessage);
    }

    bool GetLastNativeScriptBuildFailure(std::string* outStatusMessage)
    {
        return Internal::GetLastNativeScriptBuildFailure(outStatusMessage);
    }

    void OnNativeScriptAssetRenamed(const std::string& oldAssetKey, const std::string& newAssetKey)
    {
        Internal::OnNativeScriptAssetRenamed(oldAssetKey, newAssetKey);
    }
}

