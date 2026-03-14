#pragma once

// Internal header shared across the GameBuilder split translation units.
// NOT part of the public engine API.

#include "Project/GameBuilder.h"
#include "Project/BuildSettings.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#if defined(LT_PLATFORM_WINDOWS)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj_core.h>
#else
#include <sys/wait.h>
#endif

namespace Limitless::Project
{
    using json = nlohmann::json;

    // -------------------------------------------------------------------------
    // Path / platform resolution helpers
    // -------------------------------------------------------------------------

    std::string ToPremakeShortname(const std::string& configuration, const std::string& targetArchitecture);
    std::string ResolveTargetOS(const GameBuildRequest& request);
    std::string ResolveTargetArchitecture(const GameBuildRequest& request);
    std::string GetTargetPlatformToken(const std::string& targetOS);
    std::filesystem::path GetConfigPlatformBuildFolder(const std::filesystem::path& engineRoot,
                                                       const std::string& configuration,
                                                       const std::string& targetOS,
                                                       const std::string& targetArchitecture);
    std::filesystem::path GetRuntimeBuildDirectory(const std::filesystem::path& engineRoot,
                                                   const std::string& configuration,
                                                   const std::string& targetOS,
                                                   const std::string& targetArchitecture);
    std::filesystem::path GetScriptCoreBuildDirectory(const std::filesystem::path& engineRoot,
                                                      const std::string& configuration,
                                                      const std::string& targetOS,
                                                      const std::string& targetArchitecture);
    std::filesystem::path GetInternalRuntimeTemplateDirectory(const std::filesystem::path& toolchainRoot,
                                                              const std::string& configuration,
                                                              const std::string& targetOS,
                                                              const std::string& targetArchitecture);
    std::filesystem::path GetProjectScriptCoreOutputDirectory(const GameBuildRequest& request);
    std::filesystem::path GetProjectScriptCoreLibraryPath(const GameBuildRequest& request);
    std::string GetRuntimeExecutableName(const std::string& targetOS);
    std::string GetGameExecutableName(const std::string& projectName, const std::string& targetOS);
    std::string GetScriptCoreLibraryName(const std::string& targetOS);
    std::string GetBuildScriptName(bool internalBackend);
    std::string GetBuildPlatformArg(const GameBuildRequest& request);
    bool IsHostTargetPair(const GameBuildRequest& request);
    bool IsWindowsHostLinuxTarget(const GameBuildRequest& request);
    bool IsWslAvailable();
    std::string ResolveExecutionMode(const GameBuildRequest& request);
    bool IsRemoteExecutionEnabled(const GameBuildRequest& request);
    bool ResolveConfiguredWindowIconPath(const GameBuildRequest& request,
                                         std::filesystem::path& resolvedPath,
                                         std::string& errorMessage);

    // -------------------------------------------------------------------------
    // WSL / process helpers (Windows only)
    // -------------------------------------------------------------------------

#if defined(LT_PLATFORM_WINDOWS)
    std::string ConvertWindowsPathToWslPath(const std::filesystem::path& path);
    std::string EscapeBashSingleQuoted(std::string value);
    std::string BuildWslBashScriptCommand(const std::filesystem::path& engineRoot,
                                          const std::filesystem::path& scriptPath,
                                          const std::vector<std::string>& args);
    std::string TrimAsciiWhitespace(std::string value);
    std::string NormalizeWslCapturedText(std::string text);
    std::string SanitizeWslDistributionName(std::string value);
    std::vector<std::string> SplitNonEmptyLines(const std::string& text);
    bool RunHiddenCommandCapture(const std::string& commandLine,
                                 DWORD timeoutMilliseconds,
                                 DWORD& exitCode,
                                 std::string& stdoutOutput,
                                 const std::function<void(const std::string&)>& outputCallback = {});
    bool EnsureWslLinuxBuildTools(GameBuildResult& result,
                                  const std::function<void(const std::string&)>& progressCallback);
    std::string FormatWindowsErrorMessage(DWORD errorCode);
    bool HasIcoExtension(const std::filesystem::path& iconPath);
    void NotifyWindowsShellFileChanged(const std::filesystem::path& filePath);
    bool EmbedIconIntoWindowsExecutable(const std::filesystem::path& executablePath,
                                        const std::filesystem::path& iconPath,
                                        std::string& errorMessage);
#endif

    // -------------------------------------------------------------------------
    // File copy helpers
    // -------------------------------------------------------------------------

    bool CopySingleFile(const std::filesystem::path& source,
                        const std::filesystem::path& destination,
                        GameBuildResult& result);
    bool CopyDirectoryRecursive(const std::filesystem::path& sourceDirectory,
                                const std::filesystem::path& destinationDirectory,
                                GameBuildResult& result);
    size_t CopyDllsFromDirectory(const std::filesystem::path& sourceDirectory,
                                 const std::filesystem::path& destinationDirectory,
                                 const std::string& extension,
                                 GameBuildResult& result);

    // -------------------------------------------------------------------------
    // Command execution
    // -------------------------------------------------------------------------

    int RunCommand(const std::string& command,
                   const std::function<void(const std::string&)>& outputCallback = {});

    // -------------------------------------------------------------------------
    // Script mirroring
    // -------------------------------------------------------------------------

    bool MirrorProjectNativeScriptsToGeneratedDirectory(const GameBuildRequest& request, GameBuildResult& result);
}
