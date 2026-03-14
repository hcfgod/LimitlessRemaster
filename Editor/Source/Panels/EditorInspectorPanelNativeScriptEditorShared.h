#pragma once

#include "EditorInspectorPanel.h"
#include "EditorInspectorPanelNativeScriptEditor.h"

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace Limitless::EditorInspectorPanel::Internal
{
    inline constexpr size_t kNativeScriptEditorBufferSize = 256 * 1024;

    struct NativeScriptAuthoringState
    {
        bool EditorWindowOpen = false;
        bool FocusEditorWindowRequested = false;
        bool ShowDebugInfo = false;
        bool SelectHeaderTabRequested = false;
        bool SelectSourceTabRequested = false;
        bool HeaderDirty = false;
        bool SourceDirty = false;
        std::string ClassName;
        std::string AssetRelativePath;
        std::filesystem::path HeaderPath;
        std::filesystem::path SourcePath;
        std::array<char, kNativeScriptEditorBufferSize> HeaderBuffer{};
        std::array<char, kNativeScriptEditorBufferSize> SourceBuffer{};
        std::array<char, 128> NewScriptClassNameBuffer{};
        std::array<char, 256> NewScriptRelativeDirectoryBuffer{};
        std::string StatusMessage;
        bool StatusIsError = false;
        bool AutoBuildAfterSave = true;
        bool HasCompletedBuild = false;
        bool LastBuildSucceeded = true;
        int LastCompletedBuildExitCode = 0;
        std::string LastBuildSummary;
        std::atomic<bool> BuildInProgress{ false };
        std::atomic<int> LastBuildExitCode{ -1 };
        std::unique_ptr<std::thread> BuildThread;
        std::mutex LastBuildOutputMutex;
        std::string LastBuildOutput;
        bool BuildToastVisible = false;
        bool BuildToastIsError = false;
        std::string BuildToastMessage;
        std::chrono::steady_clock::time_point BuildToastShownAt{};

        ~NativeScriptAuthoringState()
        {
            if (BuildThread && BuildThread->joinable())
                BuildThread->join();
        }
    };

    NativeScriptAuthoringState& GetNativeScriptAuthoringState();
    std::optional<std::filesystem::path> GetOpenedProjectRoot();
    std::string GetScriptCoreLibraryFileName();
    std::string NormalizeBuildPlatformToken(const std::string& platform);
    std::string ToBuildConfigShortname(const std::string& configuration, const std::string& platform);
    std::string BuildConfigFolderName(const std::string& configuration, const std::string& platform);
    std::string NormalizeScriptEditorMode(std::string mode);
    bool OpenPathInExternalApplication(const std::filesystem::path& path, std::string& outError);
    bool IsInternalToolchainRootCandidate(const std::filesystem::path& candidate);
    std::optional<std::filesystem::path> FindEngineWorkspaceRoot();
    std::optional<std::filesystem::path> FindEngineSourceWorkspaceRoot();
    std::vector<std::filesystem::path> GetGeneratedScriptCoreMirrorDirectories();
    std::optional<std::filesystem::path> GetGeneratedScriptCoreMirrorDirectory();
    std::optional<std::filesystem::path> InferProjectRootFromScriptSourcePath(const std::filesystem::path& sourcePath);
    std::filesystem::path GetBuiltScriptCoreLibraryPath(const std::filesystem::path& buildRoot,
                                                        const std::string& configuration,
                                                        const std::string& platform);
    std::filesystem::path GetProjectLocalScriptCoreLibraryPath(const std::filesystem::path& projectRoot,
                                                               const std::string& configuration,
                                                               const std::string& platform);
    std::optional<std::filesystem::path> GetOpenedProjectAssetScriptsDirectory();
    std::optional<std::filesystem::path> GetOpenedProjectAssetsRoot();
    std::optional<std::filesystem::path> GetAuthoringNativeScriptsDirectory();
    std::pair<std::string, std::string> GetBuildConfigurationAndPlatform(const std::filesystem::path& settingsRoot);
    std::filesystem::path BuildNativeScriptBuildLogPath();
    std::string ReadTextFileOrEmpty(const std::filesystem::path& path);
    bool LooksLikePrefabAssetKey(const std::string& value);
    int RunBuildScriptBlocking(const std::filesystem::path& buildRoot,
                               const std::filesystem::path& openedProjectRoot,
                               const std::string& configuration,
                               const std::string& platform,
                               bool hasOpenedProject,
                               bool useInternalBackend,
                               std::string& outBuildOutput);
    bool MirrorAllProjectNativeScriptsToGeneratedDirectory(std::string& outError);
    bool TriggerNativeScriptsBuild(NativeScriptAuthoringState& state);
    void DrawNativeScriptBuildToast(NativeScriptAuthoringState& state);
    void ConsumeFinishedNativeScriptBuildResult(NativeScriptAuthoringState& state, bool updateStatusMessage);
    bool HasAnyProjectNativeScriptSources();
    std::vector<std::string> DiscoverNativeScriptClassNamesFromProjectAssets();
    std::vector<ProjectNativeScriptInfo> BuildAvailableProjectScripts();
    std::vector<std::string> BuildAvailableProjectScriptClassNames();
    std::string ResolveRegisteredScriptClassName(const std::string& requestedClassName);
    std::string SanitizeNativeScriptClassName(const char* rawName);
    std::string SanitizeRelativeAssetDirectory(const char* rawPath);
    bool LoadTextFileIntoBuffer(const std::filesystem::path& path,
                                std::array<char, kNativeScriptEditorBufferSize>& buffer,
                                std::string& outError);
    bool SaveBufferToTextFile(const std::filesystem::path& path,
                              const std::array<char, kNativeScriptEditorBufferSize>& buffer,
                              std::string& outError,
                              bool* outFileChanged = nullptr);
    bool ResolveNativeScriptFilePaths(const std::string& className,
                                      const std::string& preferredAssetRelativePath,
                                      std::filesystem::path& outHeaderPath,
                                      std::filesystem::path& outSourcePath);
    bool SynchronizeExposedPropertiesFromScript(NativeScriptEntry& nativeScript,
                                                std::vector<std::string>& outOrderedFieldNames,
                                                std::string& outError);
    bool OpenNativeScriptEditor(const std::string& className,
                                const std::string& preferredAssetRelativePath,
                                NativeScriptAuthoringState& state,
                                std::string& outError);
    bool MirrorScriptToGeneratedDirectory(const NativeScriptAuthoringState& state, std::string& outError, bool* outMirroredChange = nullptr);
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
    void GetNativeScriptEditorSessionState(NativeScriptEditorSessionState& outState);
    void ApplyNativeScriptEditorSessionState(const NativeScriptEditorSessionState& state);
    bool OpenNativeScriptEditorForAssetKey(const std::string& assetKey);
    bool BuildProjectNativeScripts(std::string* outStatusMessage = nullptr);
    bool GetLastNativeScriptBuildFailure(std::string* outStatusMessage = nullptr);
    void OnNativeScriptAssetRenamed(const std::string& oldAssetKey, const std::string& newAssetKey);
}

