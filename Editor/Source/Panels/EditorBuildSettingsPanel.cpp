#include "PrecompiledHeader.h"
#include "EditorBuildSettingsPanel.h"

#include "Assets/AssetDatabase.h"
#include "Core/Debug/Log.h"
#include "Project/BuildSettings.h"
#include "Project/GameBuilder.h"
#include "Project/ProjectManager.h"
#include "Project/ProjectDefinition.h"
#include "Platform/Platform.h"

#include "imgui/imgui.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#if defined(LT_PLATFORM_WINDOWS)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Limitless::EditorBuildSettingsPanel
{
    namespace
    {
        std::filesystem::path FindEngineRoot();
        std::filesystem::path FindInternalToolchainRoot();
        void AutoResolveBackendMode(EditorBuildSettingsPanelState& state);

        std::string NormalizeBuildBackend(std::string backend)
        {
            if (backend == Project::BuildBackend::LegacySdk || backend == Project::BuildBackend::InternalToolchain)
                return backend;
            return Project::BuildBackend::LegacySdk;
        }

        std::string NormalizeTargetOS(std::string targetOS)
        {
            if (targetOS == Project::BuildTargetOS::Windows ||
                targetOS == Project::BuildTargetOS::MacOS ||
                targetOS == Project::BuildTargetOS::Linux)
            {
                return targetOS;
            }
            return Project::GetHostBuildTargetOS();
        }

        std::string NormalizeTargetArchitecture(std::string targetArchitecture)
        {
            if (targetArchitecture == Project::BuildTargetArchitecture::X64 ||
                targetArchitecture == Project::BuildTargetArchitecture::ARM64)
            {
                return targetArchitecture;
            }
            return Project::GetHostBuildTargetArchitecture();
        }

        std::string NormalizeExecutionMode(std::string executionMode)
        {
            if (executionMode == Project::BuildExecutionMode::Auto ||
                executionMode == Project::BuildExecutionMode::Local ||
                executionMode == Project::BuildExecutionMode::Remote)
            {
                return executionMode;
            }
            return Project::BuildExecutionMode::Auto;
        }

#if defined(LT_PLATFORM_WINDOWS)
        struct WslHealthStatus final
        {
            bool WslCommandAvailable = false;
            bool StatusCommandSucceeded = false;
            bool HasInstalledDistribution = false;
            bool RebootRequired = false;
            DWORD StatusExitCode = 1;
            DWORD DistributionListExitCode = 1;
            std::string StatusOutput;
            std::string DistributionListOutput;

            bool IsReadyForLocalLinuxBuild() const
            {
                return WslCommandAvailable &&
                       StatusCommandSucceeded &&
                       HasInstalledDistribution &&
                       !RebootRequired;
            }
        };

        std::optional<WslHealthStatus> g_WslHealthCache;
        std::chrono::steady_clock::time_point g_WslHealthLastProbe{};
        bool g_HasWslHealthProbe = false;

        void InvalidateWslAvailabilityCache()
        {
            g_WslHealthCache.reset();
            g_HasWslHealthProbe = false;
        }

        std::string ToLowerCopy(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return value;
        }

        bool HasNonWhitespaceCharacter(const std::string& text)
        {
            return std::any_of(text.begin(), text.end(), [](unsigned char c) { return std::isspace(c) == 0; });
        }

        bool ContainsRebootRequiredHint(const std::string& text)
        {
            const std::string lower = ToLowerCopy(text);
            return lower.find("reboot") != std::string::npos ||
                   lower.find("restart required") != std::string::npos ||
                   lower.find("requires a restart") != std::string::npos;
        }

        bool RunHiddenCommandCapture(const std::string& commandLine,
                                     DWORD timeoutMilliseconds,
                                     DWORD& exitCode,
                                     std::string& stdoutOutput)
        {
            exitCode = 1;
            stdoutOutput.clear();

            SECURITY_ATTRIBUTES securityAttributes{};
            securityAttributes.nLength = sizeof(securityAttributes);
            securityAttributes.bInheritHandle = TRUE;

            HANDLE readPipe = nullptr;
            HANDLE writePipe = nullptr;
            if (!CreatePipe(&readPipe, &writePipe, &securityAttributes, 0))
                return false;

            if (!SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0))
            {
                CloseHandle(readPipe);
                CloseHandle(writePipe);
                return false;
            }

            STARTUPINFOA startupInfo{};
            startupInfo.cb = sizeof(startupInfo);
            startupInfo.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
            startupInfo.wShowWindow = SW_HIDE;
            startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
            startupInfo.hStdOutput = writePipe;
            startupInfo.hStdError = writePipe;

            PROCESS_INFORMATION processInformation{};
            std::string mutableCommandLine = commandLine;
            const BOOL created = CreateProcessA(
                nullptr,
                mutableCommandLine.data(),
                nullptr,
                nullptr,
                TRUE,
                CREATE_NO_WINDOW,
                nullptr,
                nullptr,
                &startupInfo,
                &processInformation);
            CloseHandle(writePipe);
            if (!created)
            {
                CloseHandle(readPipe);
                return false;
            }

            const DWORD waitResult = WaitForSingleObject(processInformation.hProcess, timeoutMilliseconds);
            if (waitResult == WAIT_TIMEOUT)
            {
                TerminateProcess(processInformation.hProcess, 1);
                CloseHandle(processInformation.hThread);
                CloseHandle(processInformation.hProcess);
                CloseHandle(readPipe);
                return true;
            }

            std::array<char, 1024> buffer{};
            DWORD bytesRead = 0;
            while (ReadFile(readPipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr) && bytesRead > 0)
                stdoutOutput.append(buffer.data(), bytesRead);
            CloseHandle(readPipe);

            (void)GetExitCodeProcess(processInformation.hProcess, &exitCode);
            CloseHandle(processInformation.hThread);
            CloseHandle(processInformation.hProcess);
            return true;
        }

        WslHealthStatus ProbeWslHealthNoWindow()
        {
            WslHealthStatus status;

            DWORD statusExitCode = 1;
            std::string statusOutput;
            const bool statusInvoked = RunHiddenCommandCapture("wsl.exe --status", 8000, statusExitCode, statusOutput);
            status.WslCommandAvailable = statusInvoked;
            status.StatusExitCode = statusExitCode;
            status.StatusOutput = statusOutput;
            status.StatusCommandSucceeded = statusInvoked && statusExitCode == 0;
            status.RebootRequired = ContainsRebootRequiredHint(statusOutput);

            DWORD distributionExitCode = 1;
            std::string distributionOutput;
            const bool distributionInvoked = RunHiddenCommandCapture("wsl.exe -l -q", 8000, distributionExitCode, distributionOutput);
            status.DistributionListExitCode = distributionExitCode;
            status.DistributionListOutput = distributionOutput;
            status.HasInstalledDistribution = distributionInvoked && distributionExitCode == 0 && HasNonWhitespaceCharacter(distributionOutput);

            return status;
        }

        const WslHealthStatus& GetWslHealthStatus(const bool forceRefresh = false)
        {
            const auto now = std::chrono::steady_clock::now();
            const bool shouldRefresh =
                forceRefresh ||
                !g_WslHealthCache.has_value() ||
                (!g_WslHealthCache->IsReadyForLocalLinuxBuild() &&
                 (!g_HasWslHealthProbe ||
                  std::chrono::duration_cast<std::chrono::seconds>(now - g_WslHealthLastProbe).count() >= 5));

            if (shouldRefresh)
            {
                g_WslHealthCache = ProbeWslHealthNoWindow();
                g_WslHealthLastProbe = now;
                g_HasWslHealthProbe = true;
            }

            return *g_WslHealthCache;
        }
#endif

        bool IsWslAvailable()
        {
#if defined(LT_PLATFORM_WINDOWS)
            return GetWslHealthStatus().IsReadyForLocalLinuxBuild();
#else
            return false;
#endif
        }

        bool IsWindowsHostLinuxTarget(const Project::BuildSettings& settings)
        {
#if defined(LT_PLATFORM_WINDOWS)
            return settings.TargetOS == Project::BuildTargetOS::Linux;
#else
            (void)settings;
            return false;
#endif
        }

        std::string ResolveEffectiveExecutionMode(const Project::BuildSettings& settings)
        {
            const std::string normalizedMode = NormalizeExecutionMode(settings.ExecutionMode);
            if (normalizedMode == Project::BuildExecutionMode::Local ||
                normalizedMode == Project::BuildExecutionMode::Remote)
            {
                return normalizedMode;
            }

            if (settings.TargetOS == Project::GetHostBuildTargetOS())
                return Project::BuildExecutionMode::Local;

            if (IsWindowsHostLinuxTarget(settings) && IsWslAvailable())
                return Project::BuildExecutionMode::Local;

            return Project::BuildExecutionMode::Remote;
        }

        std::string ResolveTargetRemoteEndpoint(const Project::BuildSettings& settings)
        {
            return Project::ResolveRemoteBuildEndpoint(settings, NormalizeTargetOS(settings.TargetOS));
        }

        std::string NormalizeScriptEditorMode(std::string mode)
        {
            if (mode == Project::ScriptEditorMode::Internal || mode == Project::ScriptEditorMode::External)
                return mode;
            return Project::ScriptEditorMode::Internal;
        }

        std::string NormalizeScriptCompileFailurePolicy(std::string policy)
        {
            if (policy == Project::ScriptCompileFailurePolicy::SafeMode ||
                policy == Project::ScriptCompileFailurePolicy::BlockPlay)
            {
                return policy;
            }
            return Project::ScriptCompileFailurePolicy::SafeMode;
        }

        std::string TrimCopy(std::string value)
        {
            auto isWhitespace = [](unsigned char character) {
                return std::isspace(character) != 0;
            };

            value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](char character) {
                            return !isWhitespace(static_cast<unsigned char>(character));
                        }));
            value.erase(std::find_if(value.rbegin(), value.rend(), [&](char character) {
                            return !isWhitespace(static_cast<unsigned char>(character));
                        }).base(),
                        value.end());
            return value;
        }

        void SetStatus(EditorBuildSettingsPanelState& state, std::string message, bool isError)
        {
            state.StatusMessage = std::move(message);
            state.StatusIsError = isError;
        }

        bool PromptAndStartWslInstall(EditorBuildSettingsPanelState& state)
        {
#if defined(LT_PLATFORM_WINDOWS)
            const char* promptTitle = "Install Windows Subsystem for Linux";
            const char* promptMessage =
                "Local Linux builds require WSL.\n\n"
                "Would you like to install WSL (Ubuntu distro) now?\n\n"
                "This may require admin approval and a restart.";

            const int choice = MessageBoxA(nullptr,
                                           promptMessage,
                                           promptTitle,
                                           MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON1 | MB_TASKMODAL);
            if (choice != IDYES)
            {
                SetStatus(state, "Build cancelled: WSL is required for local Linux builds.", true);
                return false;
            }

            const std::string installCommand =
                "powershell -NoProfile -Command \"Start-Process wsl.exe -ArgumentList '--install -d Ubuntu' -Verb RunAs\"";
            const int installExitCode = std::system(installCommand.c_str());
            if (installExitCode != 0)
            {
                SetStatus(state,
                          "Failed to start WSL installer (exit code " + std::to_string(installExitCode)
                              + "). Install WSL manually and retry.",
                          true);
                return false;
            }

            // Re-check next time build is attempted; installation may complete later.
            InvalidateWslAvailabilityCache();
            SetStatus(state, "WSL installation launched. Complete setup (reboot may be required), then click Re-check WSL and build again.", false);
            return true;
#else
            SetStatus(state, "WSL install prompt is only available on Windows hosts.", true);
            return false;
#endif
        }

        std::string BuildStepLogText(const std::vector<std::string>& stepLog)
        {
            std::string output;
            for (const std::string& line : stepLog)
            {
                if (!output.empty())
                    output.push_back('\n');
                output += line;
            }
            return output;
        }

        void PublishBuildResultToEditorConsole(const Project::GameBuildResult& result)
        {
            if (result.Success)
                LT_INFO("Game build succeeded in {:.2f}s.", result.ElapsedSeconds);
            else
                LT_ERROR("Game build failed: {}", result.ErrorMessage.empty() ? "unknown error" : result.ErrorMessage);

            for (const std::string& line : result.StepLog)
            {
                if (result.Success)
                    LT_INFO("[Game Build] {}", line);
                else
                    LT_WARN("[Game Build] {}", line);
            }
        }

        template <size_t BufferSize>
        void CopyStringToBuffer(const std::string& source, std::array<char, BufferSize>& destination)
        {
            std::memset(destination.data(), 0, destination.size());
            std::memcpy(destination.data(), source.c_str(), std::min(source.size(), destination.size() - 1));
        }

        std::vector<std::filesystem::path> BuildWindowIconCandidates(const std::filesystem::path& projectRoot, const std::string& configuredPath)
        {
            std::vector<std::filesystem::path> candidates;
            if (configuredPath.empty())
                return candidates;

            const std::filesystem::path iconPath(configuredPath);
            if (iconPath.is_absolute())
            {
                candidates.push_back(iconPath);
            }
            else
            {
                candidates.push_back(projectRoot / iconPath);
                candidates.push_back(projectRoot / "Assets" / iconPath);
            }
            return candidates;
        }

        std::optional<std::filesystem::path> ResolveExistingWindowIconPath(const std::filesystem::path& projectRoot, const std::string& configuredPath)
        {
            const auto candidates = BuildWindowIconCandidates(projectRoot, configuredPath);
            std::error_code errorCode;
            for (const auto& candidate : candidates)
            {
                errorCode.clear();
                if (std::filesystem::is_regular_file(candidate, errorCode))
                    return candidate;
            }
            return std::nullopt;
        }

        Result<void> ValidateWindowIconPath(const std::filesystem::path& projectRoot, const std::string& configuredPath)
        {
            if (configuredPath.empty())
                return Result<void>();

            const auto resolved = ResolveExistingWindowIconPath(projectRoot, configuredPath);
            if (resolved.has_value())
                return Result<void>();

            std::ostringstream message;
            message << "Game window icon path does not exist: '" << configuredPath << "'. Checked: ";
            const auto candidates = BuildWindowIconCandidates(projectRoot, configuredPath);
            for (size_t index = 0; index < candidates.size(); ++index)
            {
                if (index > 0)
                    message << "; ";
                message << "'" << candidates[index].string() << "'";
            }
            return Result<void>(ErrorCode::ResourceNotFound, message.str());
        }

        /// Ensure settings are loaded from the current project.
        void EnsureSettingsLoaded(EditorBuildSettingsPanelState& state)
        {
            if (state.SettingsLoaded)
                return;

            auto& projectManager = Project::ProjectManager::GetInstance();
            if (!projectManager.HasOpenProject())
                return;

            const auto projectRoot = projectManager.GetProjectRoot();
            const auto loadResult = Project::LoadBuildSettings(projectRoot);
            if (loadResult.IsSuccess())
                state.Settings = loadResult.GetValue();
            state.Settings.BuildConfiguration = "Dist";
            state.Settings.BuildBackend = NormalizeBuildBackend(state.Settings.BuildBackend);
            state.Settings.TargetOS = NormalizeTargetOS(state.Settings.TargetOS);
            state.Settings.TargetArchitecture = NormalizeTargetArchitecture(state.Settings.TargetArchitecture);
            state.Settings.ExecutionMode = NormalizeExecutionMode(state.Settings.ExecutionMode);
            state.Settings.ScriptEditorMode = NormalizeScriptEditorMode(state.Settings.ScriptEditorMode);
            state.Settings.ScriptCompileFailurePolicy =
                NormalizeScriptCompileFailurePolicy(state.Settings.ScriptCompileFailurePolicy);

            // Populate editable text buffers from saved settings.
            CopyStringToBuffer("", state.OutputDirectoryBuffer);
            CopyStringToBuffer("", state.WindowIconPathBuffer);
            CopyStringToBuffer("", state.RemoteBuildEndpointBuffer);
            CopyStringToBuffer("", state.RemoteBuildEndpointWindowsBuffer);
            CopyStringToBuffer("", state.RemoteBuildEndpointMacOSBuffer);
            CopyStringToBuffer("", state.RemoteBuildEndpointLinuxBuffer);
            CopyStringToBuffer("", state.RemoteBuildPoolBuffer);
            CopyStringToBuffer("", state.RemoteBuildAuthTokenBuffer);
            if (!state.Settings.LastOutputDirectory.empty())
                CopyStringToBuffer(state.Settings.LastOutputDirectory, state.OutputDirectoryBuffer);
            if (!state.Settings.GameWindowIconPath.empty())
                CopyStringToBuffer(state.Settings.GameWindowIconPath, state.WindowIconPathBuffer);
            if (!state.Settings.RemoteBuildEndpoint.empty())
                CopyStringToBuffer(state.Settings.RemoteBuildEndpoint, state.RemoteBuildEndpointBuffer);
            if (!state.Settings.RemoteBuildEndpointWindows.empty())
                CopyStringToBuffer(state.Settings.RemoteBuildEndpointWindows, state.RemoteBuildEndpointWindowsBuffer);
            if (!state.Settings.RemoteBuildEndpointMacOS.empty())
                CopyStringToBuffer(state.Settings.RemoteBuildEndpointMacOS, state.RemoteBuildEndpointMacOSBuffer);
            if (!state.Settings.RemoteBuildEndpointLinux.empty())
                CopyStringToBuffer(state.Settings.RemoteBuildEndpointLinux, state.RemoteBuildEndpointLinuxBuffer);
            if (!state.Settings.RemoteBuildPool.empty())
                CopyStringToBuffer(state.Settings.RemoteBuildPool, state.RemoteBuildPoolBuffer);
            if (!state.Settings.RemoteBuildAuthToken.empty())
                CopyStringToBuffer(state.Settings.RemoteBuildAuthToken, state.RemoteBuildAuthTokenBuffer);
            // Root is auto-detected at build-time; clear persisted manual overrides.
            state.Settings.EngineRootOverride.clear();

            AutoResolveBackendMode(state);

            state.SettingsLoaded = true;
        }

        /// Persist settings to disk.
        Result<void> SaveSettings(EditorBuildSettingsPanelState& state)
        {
            auto& projectManager = Project::ProjectManager::GetInstance();
            if (!projectManager.HasOpenProject())
                return Result<void>(ErrorCode::InvalidState, "No project is open.");

            // Update output directory from the buffer.
            state.Settings.LastOutputDirectory = TrimCopy(std::string(state.OutputDirectoryBuffer.data()));
            state.Settings.GameWindowIconPath = TrimCopy(std::string(state.WindowIconPathBuffer.data()));
            state.Settings.RemoteBuildEndpoint = TrimCopy(std::string(state.RemoteBuildEndpointBuffer.data()));
            state.Settings.RemoteBuildEndpointWindows =
                TrimCopy(std::string(state.RemoteBuildEndpointWindowsBuffer.data()));
            state.Settings.RemoteBuildEndpointMacOS =
                TrimCopy(std::string(state.RemoteBuildEndpointMacOSBuffer.data()));
            state.Settings.RemoteBuildEndpointLinux =
                TrimCopy(std::string(state.RemoteBuildEndpointLinuxBuffer.data()));
            state.Settings.RemoteBuildPool = TrimCopy(std::string(state.RemoteBuildPoolBuffer.data()));
            state.Settings.RemoteBuildAuthToken = TrimCopy(std::string(state.RemoteBuildAuthTokenBuffer.data()));
            state.Settings.EngineRootOverride.clear();
            state.Settings.BuildConfiguration = "Dist";
            state.Settings.BuildBackend = NormalizeBuildBackend(state.Settings.BuildBackend);
            state.Settings.TargetOS = NormalizeTargetOS(state.Settings.TargetOS);
            state.Settings.TargetArchitecture = NormalizeTargetArchitecture(state.Settings.TargetArchitecture);
            state.Settings.ExecutionMode = NormalizeExecutionMode(state.Settings.ExecutionMode);
            state.Settings.ScriptEditorMode = NormalizeScriptEditorMode(state.Settings.ScriptEditorMode);
            state.Settings.ScriptCompileFailurePolicy =
                NormalizeScriptCompileFailurePolicy(state.Settings.ScriptCompileFailurePolicy);
            state.Settings.RemoteBuildTimeoutSeconds = std::clamp(state.Settings.RemoteBuildTimeoutSeconds, 30, 7200);
            state.Settings.RemoteBuildPollIntervalSeconds = std::clamp(state.Settings.RemoteBuildPollIntervalSeconds, 1, 60);
            state.Settings.RemoteBuildMaxRetries = std::clamp(state.Settings.RemoteBuildMaxRetries, 0, 10);
            if (state.Settings.RemoteBuildPool.empty())
                state.Settings.RemoteBuildPool = "default";

            const auto projectRoot = projectManager.GetProjectRoot();
            const auto iconValidationResult = ValidateWindowIconPath(projectRoot, state.Settings.GameWindowIconPath);
            if (iconValidationResult.IsFailure())
                return iconValidationResult;
            return Project::SaveBuildSettings(projectRoot, state.Settings);
        }

        /// Returns true when `candidate` looks like the engine workspace root.
        /// We check for the Scripts/ directory which only exists at the top level.
        bool IsEngineWorkspaceRoot(const std::filesystem::path& candidate)
        {
            std::error_code errorCode;
            const bool hasScripts = std::filesystem::is_directory(candidate / "Scripts", errorCode);
            const bool hasSolution = std::filesystem::exists(candidate / "LimitlessRemaster.sln", errorCode);
            const bool hasPremake = std::filesystem::exists(candidate / "premake5.lua", errorCode);
            return hasScripts && (hasSolution || hasPremake);
        }

        bool IsInternalToolchainRoot(const std::filesystem::path& candidate)
        {
            std::error_code errorCode;
#if defined(LT_PLATFORM_WINDOWS)
            const std::filesystem::path scriptCoreScript = candidate / "Scripts" / "build-project-scriptcore-windows.bat";
#else
            const std::filesystem::path scriptCoreScript = candidate / "Scripts" / "build-project-scriptcore-unix.sh";
#endif
            const std::filesystem::path sdkIncludeRoot = candidate / "SDK" / "include";
            const std::filesystem::path sdkLibRoot = candidate / "SDK" / "lib";
            const std::filesystem::path runtimeTemplateRoot = candidate / "RuntimeTemplates";
            const std::filesystem::path generatedScriptMirrorRoot = candidate / "Build" / "Generated" / "ScriptCore";
            return std::filesystem::exists(scriptCoreScript, errorCode) &&
                   std::filesystem::is_directory(sdkIncludeRoot, errorCode) &&
                   std::filesystem::is_directory(sdkLibRoot, errorCode) &&
                   std::filesystem::is_directory(runtimeTemplateRoot, errorCode) &&
                   std::filesystem::is_directory(generatedScriptMirrorRoot, errorCode);
        }

        /// Try to locate the engine workspace root from the running editor.
        /// Walks up from both the executable directory and the current working
        /// directory; returns the first match.
        std::filesystem::path FindEngineRoot()
        {
            auto walkUp = [](std::filesystem::path probe) -> std::filesystem::path
            {
                for (int depth = 0; depth < 10 && !probe.empty(); ++depth)
                {
                    if (IsEngineWorkspaceRoot(probe))
                        return probe;
                    auto parent = probe.parent_path();
                    if (parent == probe)
                        break;
                    probe = parent;
                }
                return {};
            };

            // Environment override is useful for portable editor installs.
            if (const char* envRoot = std::getenv("LIMITLESS_ENGINE_ROOT"); envRoot && envRoot[0] != '\0')
            {
                std::filesystem::path candidate(envRoot);
                if (IsEngineWorkspaceRoot(candidate))
                    return candidate;
            }

            // Try from the executable location first (most reliable).
            const auto& platformInfo = PlatformDetection::GetPlatformInfo();
            if (!platformInfo.executablePath.empty())
            {
                auto result = walkUp(std::filesystem::path(platformInfo.executablePath).parent_path());
                if (!result.empty())
                    return result;
            }

            // Fallback: current working directory.
            std::error_code errorCode;
            auto result = walkUp(std::filesystem::current_path(errorCode));
            if (!result.empty())
                return result;

            return {};
        }

        std::filesystem::path FindInternalToolchainRoot()
        {
            // Shipped layout contract: <EditorExeDir>/Toolchain
            const auto& platformInfo = PlatformDetection::GetPlatformInfo();
            if (!platformInfo.executablePath.empty())
            {
                const std::filesystem::path executableDir = std::filesystem::path(platformInfo.executablePath).parent_path();
                const std::filesystem::path embeddedToolchain = executableDir / "Toolchain";
                if (IsInternalToolchainRoot(embeddedToolchain))
                    return embeddedToolchain;
            }

            if (const char* envRoot = std::getenv("LIMITLESS_TOOLCHAIN_ROOT"); envRoot && envRoot[0] != '\0')
            {
                std::filesystem::path candidate(envRoot);
                if (IsInternalToolchainRoot(candidate))
                    return candidate;
            }

            if (!platformInfo.executablePath.empty())
            {
                const std::filesystem::path executableDir = std::filesystem::path(platformInfo.executablePath).parent_path();
                if (IsInternalToolchainRoot(executableDir))
                    return executableDir;
            }

            std::error_code errorCode;
            const std::filesystem::path cwd = std::filesystem::current_path(errorCode);
            if (!errorCode)
            {
                if (IsInternalToolchainRoot(cwd))
                    return cwd;
                const std::filesystem::path cwdToolchain = cwd / "Toolchain";
                if (IsInternalToolchainRoot(cwdToolchain))
                    return cwdToolchain;
            }

            return {};
        }

        std::filesystem::path ResolveBuildBackendRoot(const EditorBuildSettingsPanelState& state)
        {
            if (state.Settings.BuildBackend == Project::BuildBackend::InternalToolchain)
                return FindInternalToolchainRoot();
            return FindEngineRoot();
        }

        void AutoResolveBackendMode(EditorBuildSettingsPanelState& state)
        {
            const std::filesystem::path detectedInternal = FindInternalToolchainRoot();
            const std::filesystem::path detectedLegacy = FindEngineRoot();
            if (!detectedInternal.empty() && detectedLegacy.empty())
                state.Settings.BuildBackend = Project::BuildBackend::InternalToolchain;
        }

        std::vector<std::string> GetBuildBackendHealthIssues(const EditorBuildSettingsPanelState& state, const std::filesystem::path& root)
        {
            std::vector<std::string> issues;
            if (root.empty())
            {
                if (state.Settings.BuildBackend == Project::BuildBackend::InternalToolchain)
                    issues.push_back("Toolchain root is not configured or auto-detected.");
                else
                    issues.push_back("Engine root is not configured or auto-detected.");
                return issues;
            }

            std::error_code errorCode;
            if (!std::filesystem::is_directory(root, errorCode))
            {
                issues.push_back("Configured root does not exist: " + root.string());
                return issues;
            }

            if (state.Settings.BuildBackend == Project::BuildBackend::InternalToolchain)
            {
                if (!IsInternalToolchainRoot(root))
                    issues.push_back("Missing internal toolchain markers (Scripts/build-project-scriptcore + SDK/include + SDK/lib + RuntimeTemplates).");

                const std::filesystem::path runtimeTemplates = root / "RuntimeTemplates";
                if (!std::filesystem::exists(runtimeTemplates, errorCode))
                    issues.push_back("Missing runtime templates folder: " + runtimeTemplates.string());

                const std::filesystem::path generatedScriptMirrorRoot = root / "Build" / "Generated" / "ScriptCore";
                if (!std::filesystem::exists(generatedScriptMirrorRoot, errorCode))
                    issues.push_back("Missing generated script mirror root: " + generatedScriptMirrorRoot.string());

                const std::filesystem::path sdkHeaderRoot = root / "SDK" / "include";
                if (!std::filesystem::exists(sdkHeaderRoot, errorCode))
                    issues.push_back("Missing script SDK include root: " + sdkHeaderRoot.string());

                const std::filesystem::path sdkLibraryRoot = root / "SDK" / "lib";
                if (!std::filesystem::exists(sdkLibraryRoot, errorCode))
                    issues.push_back("Missing script SDK library root: " + sdkLibraryRoot.string());
            }
            else
            {
                if (!IsEngineWorkspaceRoot(root))
                    issues.push_back("Missing legacy workspace markers (Scripts + premake/solution files).");
            }

            return issues;
        }

        void ConsumeCompletedBuildResult(EditorBuildSettingsPanelState& state)
        {
            const std::shared_ptr<EditorBuildSettingsPanelState::BuildJobState> buildJob = state.ActiveBuildJob;
            if (!buildJob)
                return;

            Project::GameBuildResult completedResult;
            bool hasCompletedResult = false;
            {
                std::lock_guard<std::mutex> lock(buildJob->Mutex);
                if (buildJob->Completed)
                {
                    completedResult = std::move(buildJob->Result);
                    buildJob->Completed = false;
                    hasCompletedResult = true;
                }
            }

            if (!hasCompletedResult)
                return;

            if (state.BuildThread.joinable())
                state.BuildThread.join();

            state.ActiveBuildJob.reset();
            state.BuildInProgress.store(false, std::memory_order_release);
            state.LastBuildResult = std::move(completedResult);
            if (state.LastBuildResult.Success)
                SetStatus(state, "Build succeeded (" + std::to_string(state.LastBuildResult.ElapsedSeconds) + "s).", false);
            else
                SetStatus(state, "Build failed: " + state.LastBuildResult.ErrorMessage, true);
            PublishBuildResultToEditorConsole(state.LastBuildResult);
        }

        /// Start a build in a background thread.
        void StartBuild(EditorBuildSettingsPanelState& state,
                        bool runAfterBuild,
                        const std::function<bool()>& saveActiveSceneBeforeBuild)
        {
            ConsumeCompletedBuildResult(state);

            if (state.BuildInProgress.load())
                return;

            if (saveActiveSceneBeforeBuild && !saveActiveSceneBeforeBuild())
            {
                SetStatus(state, "Build aborted: failed to save active scene. Save the scene and try again.", true);
                return;
            }

            auto& projectManager = Project::ProjectManager::GetInstance();
            if (!projectManager.HasOpenProject())
            {
                SetStatus(state, "No project is open.", true);
                return;
            }

            // Save settings before building.
            const auto saveSettingsResult = SaveSettings(state);
            if (saveSettingsResult.IsFailure())
            {
                SetStatus(state,
                          "Build aborted: failed to save build settings: " + saveSettingsResult.GetError().GetErrorMessage(),
                          true);
                return;
            }

            Project::GameBuildRequest request;
            request.OutputDirectory = std::string(state.OutputDirectoryBuffer.data());
            request.Settings = state.Settings;
            request.Settings.BuildConfiguration = "Dist";
            request.Settings.BuildBackend = NormalizeBuildBackend(request.Settings.BuildBackend);
            request.Settings.TargetOS = NormalizeTargetOS(request.Settings.TargetOS);
            request.Settings.TargetArchitecture = NormalizeTargetArchitecture(request.Settings.TargetArchitecture);
            request.Settings.ExecutionMode = NormalizeExecutionMode(request.Settings.ExecutionMode);
            request.ProjectRoot = projectManager.GetProjectRoot();
            request.EngineRoot = ResolveBuildBackendRoot(state);

            if (request.Settings.BuildBackend == Project::BuildBackend::LegacySdk &&
                IsInternalToolchainRoot(request.EngineRoot) &&
                !IsEngineWorkspaceRoot(request.EngineRoot))
            {
                request.Settings.BuildBackend = Project::BuildBackend::InternalToolchain;
                state.Settings.BuildBackend = Project::BuildBackend::InternalToolchain;
                const auto backendSaveResult = SaveSettings(state);
                if (backendSaveResult.IsFailure())
                {
                    SetStatus(state,
                              "Build aborted: failed to persist backend selection: " + backendSaveResult.GetError().GetErrorMessage(),
                              true);
                    return;
                }
            }

            // Use project name from ProjectDefinition.
            const auto definition = projectManager.GetProjectDefinition();
            if (definition.has_value() && !definition->ProjectName.empty())
                request.ProjectName = definition->ProjectName;
            else
                request.ProjectName = "Game";

            if (request.OutputDirectory.empty())
            {
                SetStatus(state, "Please set an output directory.", true);
                return;
            }

            if (request.EngineRoot.empty())
            {
                if (request.Settings.BuildBackend == Project::BuildBackend::InternalToolchain)
                    SetStatus(state, "Could not locate internal toolchain root.", true);
                else
                    SetStatus(state, "Could not locate engine workspace root.", true);
                return;
            }

            const std::string effectiveExecutionMode = ResolveEffectiveExecutionMode(request.Settings);
            if (effectiveExecutionMode == Project::BuildExecutionMode::Local &&
                request.Settings.TargetOS == Project::BuildTargetOS::Linux &&
                !IsWslAvailable())
            {
#if defined(LT_PLATFORM_WINDOWS)
                const WslHealthStatus& wslHealth = GetWslHealthStatus(true);
                if (wslHealth.RebootRequired)
                {
                    SetStatus(state, "WSL installation is pending restart. Reboot Windows, then click Re-check WSL.", true);
                    return;
                }

                if (!wslHealth.HasInstalledDistribution)
                {
                    (void)PromptAndStartWslInstall(state);
                    return;
                }

                SetStatus(state, "WSL is not ready for local Linux builds yet. Click Run WSL Setup Check for details.", true);
                return;
#else
                (void)PromptAndStartWslInstall(state);
                return;
#endif
            }

            if (effectiveExecutionMode == Project::BuildExecutionMode::Remote &&
                TrimCopy(ResolveTargetRemoteEndpoint(request.Settings)).empty())
            {
                SetStatus(state, "Remote build mode requires a remote build endpoint.", true);
                return;
            }

            std::vector<std::string> healthIssues;
            if (effectiveExecutionMode == Project::BuildExecutionMode::Remote)
            {
                const std::filesystem::path remoteClientScript = request.EngineRoot / "Scripts" / "remote_build_client.py";
                if (!std::filesystem::exists(remoteClientScript))
                    healthIssues.push_back("Missing remote build client script: " + remoteClientScript.string());
            }
            else
            {
                healthIssues = GetBuildBackendHealthIssues(state, request.EngineRoot);
            }
            if (!healthIssues.empty())
            {
                SetStatus(state, "Build backend is not ready: " + healthIssues.front(), true);
                return;
            }

            state.BuildInProgress.store(true, std::memory_order_release);
            SetStatus(state, "Building...", false);

            // Launch build on a managed background thread.
            if (state.BuildThread.joinable())
                state.BuildThread.join();
            state.ActiveBuildJob = std::make_shared<EditorBuildSettingsPanelState::BuildJobState>();
            const std::shared_ptr<EditorBuildSettingsPanelState::BuildJobState> buildJob = state.ActiveBuildJob;

            state.BuildThread = std::thread([buildJob, request, runAfterBuild]()
            {
                Project::GameBuildResult result;
                if (runAfterBuild)
                    result = Project::GameBuilder::BuildAndRunGame(request);
                else
                    result = Project::GameBuilder::BuildGame(request);

                {
                    std::lock_guard<std::mutex> lock(buildJob->Mutex);
                    buildJob->Result = std::move(result);
                    buildJob->Completed = true;
                }
            });
        }
    }

    // -------------------------------------------------------------------------
    // Draw
    // -------------------------------------------------------------------------

    void Draw(bool& showWindow,
              EditorBuildSettingsPanelState& state,
              const std::string& currentSceneAssetKey,
              Scene* currentScene,
              const std::function<bool()>& saveActiveSceneBeforeBuild)
    {
        ConsumeCompletedBuildResult(state);
        if (!showWindow)
            return;

        EnsureSettingsLoaded(state);

        ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Build Settings", &showWindow))
        {
            ImGui::End();
            return;
        }

        const bool buildInProgress = state.BuildInProgress.load(std::memory_order_acquire);

        // -----------------------------------------------------------------
        // Scenes In Build
        // -----------------------------------------------------------------
        ImGui::SeparatorText("Scenes In Build");

        auto& scenes = state.Settings.BuildScenes;
        int removeIndex = -1;
        int moveUpIndex = -1;
        int moveDownIndex = -1;

        for (int i = 0; i < static_cast<int>(scenes.size()); ++i)
        {
            ImGui::PushID(i);

            // Checkbox to enable/disable.
            ImGui::Checkbox("##Enabled", &scenes[i].Enabled);
            ImGui::SameLine();

            // Scene index label.
            if (scenes[i].Enabled)
            {
                bool isStartup = true;
                for (int j = 0; j < i; ++j)
                {
                    if (scenes[j].Enabled) { isStartup = false; break; }
                }
                if (isStartup)
                    ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "[%d] (Startup)", i);
                else
                    ImGui::Text("[%d]", i);
            }
            else
            {
                ImGui::TextDisabled("[%d]", i);
            }

            ImGui::SameLine();
            ImGui::TextUnformatted(scenes[i].Key.c_str());

            // Reorder / remove buttons.
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 80);
            if (i > 0)
            {
                if (ImGui::SmallButton("Up"))
                    moveUpIndex = i;
            }
            ImGui::SameLine();
            if (i < static_cast<int>(scenes.size()) - 1)
            {
                if (ImGui::SmallButton("Down"))
                    moveDownIndex = i;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("X"))
                removeIndex = i;

            ImGui::PopID();
        }

        // Apply reorder / remove.
        if (removeIndex >= 0 && removeIndex < static_cast<int>(scenes.size()))
            scenes.erase(scenes.begin() + removeIndex);
        if (moveUpIndex > 0 && moveUpIndex < static_cast<int>(scenes.size()))
            std::swap(scenes[moveUpIndex], scenes[moveUpIndex - 1]);
        if (moveDownIndex >= 0 && moveDownIndex < static_cast<int>(scenes.size()) - 1)
            std::swap(scenes[moveDownIndex], scenes[moveDownIndex + 1]);

        // Unity-style convenience action: add the currently open scene.
        const bool hasOpenScene = currentScene != nullptr;
        const bool canAddCurrentScene = hasOpenScene && !currentSceneAssetKey.empty();
        ImGui::BeginDisabled(!canAddCurrentScene);
        if (ImGui::Button("Add Current Scene"))
        {
            // Check for duplicates.
            const auto it = std::find_if(scenes.begin(), scenes.end(),
                [&currentSceneAssetKey](const Project::BuildSceneEntry& entry)
                {
                    return entry.Key == currentSceneAssetKey;
                });

            if (it == scenes.end())
            {
                Project::BuildSceneEntry newEntry;
                newEntry.Key = currentSceneAssetKey;
                newEntry.Enabled = true;

                // Capture GUID when available so the entry remains stable on renames.
                const auto recordResult = Assets::AssetDatabase::GetInstance().FindByKey(currentSceneAssetKey);
                if (recordResult.IsSuccess())
                    newEntry.Guid = recordResult.GetValue().Guid;

                scenes.push_back(std::move(newEntry));
                state.SceneListStatusMessage = "Added current scene to build list.";
            }
            else
            {
                state.SceneListStatusMessage = "Current scene is already in build list.";
            }
        }
        ImGui::EndDisabled();
        if (!canAddCurrentScene && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            if (!hasOpenScene)
                ImGui::SetTooltip("No scene is currently open.");
            else
                ImGui::SetTooltip("Save the current scene first, then add it to the build list.");
        }
        if (!state.SceneListStatusMessage.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", state.SceneListStatusMessage.c_str());
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        state.Settings.BuildConfiguration = "Dist";

        const char* backendOptions[] = { "Internal Toolchain", "Legacy SDK/Workspace" };
        int currentBackendIndex = (state.Settings.BuildBackend == Project::BuildBackend::InternalToolchain) ? 0 : 1;
        if (ImGui::Combo("Build Backend", &currentBackendIndex, backendOptions, 2))
        {
            state.Settings.BuildBackend = (currentBackendIndex == 0)
                ? Project::BuildBackend::InternalToolchain
                : Project::BuildBackend::LegacySdk;
        }

        const char* targetOSOptions[] = { "Windows", "macOS", "Linux" };
        const char* targetOSValues[] = {
            Project::BuildTargetOS::Windows,
            Project::BuildTargetOS::MacOS,
            Project::BuildTargetOS::Linux
        };
        int currentTargetOSIndex = 0;
        for (int index = 0; index < 3; ++index)
        {
            if (state.Settings.TargetOS == targetOSValues[index])
            {
                currentTargetOSIndex = index;
                break;
            }
        }
        if (ImGui::Combo("Target OS", &currentTargetOSIndex, targetOSOptions, 3))
            state.Settings.TargetOS = targetOSValues[currentTargetOSIndex];

        const char* targetArchitectureOptions[] = { "x64", "ARM64" };
        const char* targetArchitectureValues[] = {
            Project::BuildTargetArchitecture::X64,
            Project::BuildTargetArchitecture::ARM64
        };
        int currentTargetArchitectureIndex =
            (state.Settings.TargetArchitecture == Project::BuildTargetArchitecture::ARM64) ? 1 : 0;
        if (ImGui::Combo("Target Architecture", &currentTargetArchitectureIndex, targetArchitectureOptions, 2))
            state.Settings.TargetArchitecture = targetArchitectureValues[currentTargetArchitectureIndex];

        const char* executionModeOptions[] = { "Auto", "Local", "Remote" };
        int currentExecutionModeIndex = 0;
        if (state.Settings.ExecutionMode == Project::BuildExecutionMode::Local)
            currentExecutionModeIndex = 1;
        else if (state.Settings.ExecutionMode == Project::BuildExecutionMode::Remote)
            currentExecutionModeIndex = 2;
        if (ImGui::Combo("Execution Mode", &currentExecutionModeIndex, executionModeOptions, 3))
        {
            if (currentExecutionModeIndex == 0)
                state.Settings.ExecutionMode = Project::BuildExecutionMode::Auto;
            else if (currentExecutionModeIndex == 1)
                state.Settings.ExecutionMode = Project::BuildExecutionMode::Local;
            else
                state.Settings.ExecutionMode = Project::BuildExecutionMode::Remote;
        }

        const std::string effectiveExecutionMode = ResolveEffectiveExecutionMode(state.Settings);
        if (state.Settings.ExecutionMode == Project::BuildExecutionMode::Auto)
        {
            ImGui::TextDisabled("Auto resolves to: %s", effectiveExecutionMode.c_str());
        }

        if (effectiveExecutionMode == Project::BuildExecutionMode::Remote)
        {
            ImGui::InputText("Remote Endpoint", state.RemoteBuildEndpointBuffer.data(), state.RemoteBuildEndpointBuffer.size());
            ImGui::Checkbox("Route Endpoint By Target OS", &state.Settings.UseTargetEndpointRouting);
            if (state.Settings.UseTargetEndpointRouting)
            {
                ImGui::InputText("Remote Endpoint (Windows)", state.RemoteBuildEndpointWindowsBuffer.data(), state.RemoteBuildEndpointWindowsBuffer.size());
                ImGui::InputText("Remote Endpoint (macOS)", state.RemoteBuildEndpointMacOSBuffer.data(), state.RemoteBuildEndpointMacOSBuffer.size());
                ImGui::InputText("Remote Endpoint (Linux)", state.RemoteBuildEndpointLinuxBuffer.data(), state.RemoteBuildEndpointLinuxBuffer.size());
            }
            ImGui::InputText("Remote Pool", state.RemoteBuildPoolBuffer.data(), state.RemoteBuildPoolBuffer.size());
            ImGui::InputText("Remote Auth Token", state.RemoteBuildAuthTokenBuffer.data(), state.RemoteBuildAuthTokenBuffer.size());
            ImGui::SliderInt("Remote Timeout (s)", &state.Settings.RemoteBuildTimeoutSeconds, 30, 7200);
            ImGui::SliderInt("Remote Poll Interval (s)", &state.Settings.RemoteBuildPollIntervalSeconds, 1, 60);
            ImGui::SliderInt("Remote Retries", &state.Settings.RemoteBuildMaxRetries, 0, 10);
            ImGui::Checkbox("Allow Local Fallback", &state.Settings.AllowLocalBuildFallback);
        }
        else if (state.Settings.TargetOS != Project::GetHostBuildTargetOS() && !IsWindowsHostLinuxTarget(state.Settings))
        {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
                               "Cross-platform targets require Remote execution mode.");
        }
        else if (state.Settings.TargetOS != Project::GetHostBuildTargetOS() && IsWindowsHostLinuxTarget(state.Settings))
        {
#if defined(LT_PLATFORM_WINDOWS)
            const WslHealthStatus& wslHealth = GetWslHealthStatus();
            ImGui::SeparatorText("WSL Setup Assistant");

            if (wslHealth.IsReadyForLocalLinuxBuild())
            {
                ImGui::TextColored(ImVec4(0.3f, 0.85f, 0.3f, 1.0f), "WSL is ready: local Linux cross-build available.");
            }
            else
            {
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "WSL setup is incomplete.");
                if (wslHealth.RebootRequired)
                    ImGui::BulletText("Reboot is required before WSL can be used.");
                if (!wslHealth.WslCommandAvailable)
                    ImGui::BulletText("WSL command is not available on this system.");
                else if (!wslHealth.StatusCommandSucceeded)
                    ImGui::BulletText("`wsl --status` did not report ready state.");
                if (!wslHealth.HasInstalledDistribution)
                    ImGui::BulletText("No WSL Linux distribution is installed.");
            }

            if (ImGui::SmallButton("Run WSL Setup Check"))
            {
                InvalidateWslAvailabilityCache();
                const WslHealthStatus& refreshed = GetWslHealthStatus(true);
                if (refreshed.IsReadyForLocalLinuxBuild())
                    SetStatus(state, "WSL is ready for local Linux builds.", false);
                else if (refreshed.RebootRequired)
                    SetStatus(state, "WSL requires a Windows reboot before it can be used.", true);
                else if (!refreshed.HasInstalledDistribution)
                    SetStatus(state, "WSL is installed but no Linux distro exists. Install one (for example Ubuntu) and retry.", true);
                else
                    SetStatus(state, "WSL check completed, but local Linux build prerequisites are still not fully satisfied.", true);
            }

            if (!wslHealth.IsReadyForLocalLinuxBuild())
            {
                ImGui::SameLine();
                if (ImGui::SmallButton("Install WSL (Ubuntu)"))
                    (void)PromptAndStartWslInstall(state);
            }
#endif
        }

        const char* scriptEditorOptions[] = { "Internal (Built-in)", "External (Visual Studio)" };
        int currentScriptEditorIndex = (state.Settings.ScriptEditorMode == Project::ScriptEditorMode::External) ? 1 : 0;
        if (ImGui::Combo("Script Editor", &currentScriptEditorIndex, scriptEditorOptions, 2))
        {
            state.Settings.ScriptEditorMode = (currentScriptEditorIndex == 0)
                ? Project::ScriptEditorMode::Internal
                : Project::ScriptEditorMode::External;
        }

        const char* scriptFailurePolicyOptions[] = {
            "Safe Mode (allow play, disable scripts)",
            "Block Play Mode on failure"
        };
        int currentScriptFailurePolicyIndex =
            (NormalizeScriptCompileFailurePolicy(state.Settings.ScriptCompileFailurePolicy) ==
             Project::ScriptCompileFailurePolicy::BlockPlay)
                ? 1
                : 0;
        if (ImGui::Combo("Script Build Failure", &currentScriptFailurePolicyIndex, scriptFailurePolicyOptions, 2))
        {
            state.Settings.ScriptCompileFailurePolicy = (currentScriptFailurePolicyIndex == 0)
                ? Project::ScriptCompileFailurePolicy::SafeMode
                : Project::ScriptCompileFailurePolicy::BlockPlay;
        }

        // -----------------------------------------------------------------
        // Compression
        // -----------------------------------------------------------------
        const char* compressionOptions[] = { "None", "Zstd" };
        int currentCompressionIndex = (state.Settings.CompressionMode == "None") ? 0 : 1;
        if (ImGui::Combo("Compression", &currentCompressionIndex, compressionOptions, 2))
            state.Settings.CompressionMode = compressionOptions[currentCompressionIndex];

        if (currentCompressionIndex == 1)
        {
            ImGui::SliderInt("Zstd Level", &state.Settings.ZstdCompressionLevel, 1, 22);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // -----------------------------------------------------------------
        // Output Folder
        // -----------------------------------------------------------------
        ImGui::SeparatorText("Output");

        ImGui::InputText("Output Folder", state.OutputDirectoryBuffer.data(), state.OutputDirectoryBuffer.size());
        ImGui::InputText("Game Window Icon", state.WindowIconPathBuffer.data(), state.WindowIconPathBuffer.size());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Optional. Absolute or project-relative path (example: Assets/Icons/Game.ico).");

        const std::string configuredWindowIconPath = TrimCopy(std::string(state.WindowIconPathBuffer.data()));
        auto& projectManager = Project::ProjectManager::GetInstance();
        const std::filesystem::path currentProjectRoot = projectManager.HasOpenProject() ? projectManager.GetProjectRoot() : std::filesystem::path();
        if (!configuredWindowIconPath.empty() && !currentProjectRoot.empty())
        {
            const auto resolvedIcon = ResolveExistingWindowIconPath(currentProjectRoot, configuredWindowIconPath);
            if (resolvedIcon.has_value())
            {
                ImGui::TextColored(ImVec4(0.3f, 0.85f, 0.3f, 1.0f), "Icon found: %s", resolvedIcon->string().c_str());

                std::string extension = resolvedIcon->extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#if defined(LT_PLATFORM_WINDOWS)
                if (extension != ".ico")
                {
                    std::filesystem::path companionIco = resolvedIcon.value();
                    companionIco.replace_extension(".ico");
                    const bool hasCompanionIco = std::filesystem::exists(companionIco);
                    if (hasCompanionIco)
                        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.25f, 1.0f), "Explorer icon uses companion .ico: %s", companionIco.string().c_str());
                    else
                        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.25f, 1.0f), "Windows executable icon needs .ico (or companion .ico).");
                }
#elif defined(LT_PLATFORM_MACOS)
                if (extension != ".icns")
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.25f, 1.0f), "macOS app icon metadata uses .icns (window icon still applies).");
#endif
            }
            else
            {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.25f, 1.0f), "Icon not found. Save/build will fail until resolved.");
            }
        }

        const bool useInternalBackend = (state.Settings.BuildBackend == Project::BuildBackend::InternalToolchain);

        const std::filesystem::path healthRoot = ResolveBuildBackendRoot(state);
        const char* resolvedRootLabel = useInternalBackend ? "Toolchain Root (auto)" : "Engine Root (auto)";
        if (!healthRoot.empty())
            ImGui::Text("%s: %s", resolvedRootLabel, healthRoot.string().c_str());
        else
            ImGui::TextDisabled("%s: <not found>", resolvedRootLabel);

        std::vector<std::string> healthIssues;
        if (effectiveExecutionMode == Project::BuildExecutionMode::Remote)
        {
            const std::filesystem::path remoteClientScript = healthRoot / "Scripts" / "remote_build_client.py";
            if (healthRoot.empty())
                healthIssues.push_back("Build root is not configured or auto-detected.");
            else if (!std::filesystem::exists(remoteClientScript))
                healthIssues.push_back("Missing remote build client script: " + remoteClientScript.string());
            if (TrimCopy(ResolveTargetRemoteEndpoint(state.Settings)).empty())
                healthIssues.push_back("Remote endpoint is empty for selected target.");
        }
        else
        {
            healthIssues = GetBuildBackendHealthIssues(state, healthRoot);
        }
        if (healthIssues.empty())
        {
            ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "Backend ready: %s", healthRoot.string().c_str());
        }
        else
        {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "Backend check:");
            for (const std::string& issue : healthIssues)
                ImGui::BulletText("%s", issue.c_str());
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // -----------------------------------------------------------------
        // Build / Build & Run buttons
        // -----------------------------------------------------------------
        ImGui::BeginDisabled(buildInProgress);

        if (ImGui::Button("Build", ImVec2(120, 30)))
        {
            StartBuild(state, false, saveActiveSceneBeforeBuild);
        }

        ImGui::SameLine();

        if (ImGui::Button("Build And Run", ImVec2(140, 30)))
        {
            StartBuild(state, true, saveActiveSceneBeforeBuild);
        }

        ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Save Settings", ImVec2(120, 30)))
        {
            const auto saveResult = SaveSettings(state);
            if (saveResult.IsFailure())
                SetStatus(state, "Failed to save build settings: " + saveResult.GetError().GetErrorMessage(), true);
            else
                SetStatus(state, "Build settings saved.", false);
        }

        // -----------------------------------------------------------------
        // Build status / log
        // -----------------------------------------------------------------
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (buildInProgress)
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Building...");
        else if (!state.StatusMessage.empty())
        {
            if (!state.StatusIsError)
                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "%s", state.StatusMessage.c_str());
            else
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", state.StatusMessage.c_str());
        }

        // Show step log from last build.
        if (!state.LastBuildResult.StepLog.empty())
        {
            if (ImGui::CollapsingHeader("Build Log", ImGuiTreeNodeFlags_DefaultOpen))
            {
                const std::string buildLogText = BuildStepLogText(state.LastBuildResult.StepLog);
                if (ImGui::Button("Copy Build Log"))
                    ImGui::SetClipboardText(buildLogText.c_str());

                ImGui::BeginChild("BuildLog", ImVec2(0, 150), true);
                for (const auto& line : state.LastBuildResult.StepLog)
                    ImGui::TextWrapped("%s", line.c_str());
                ImGui::EndChild();
            }
        }

        ImGui::End();
    }

    void Shutdown(EditorBuildSettingsPanelState& state)
    {
        ConsumeCompletedBuildResult(state);
        if (!state.BuildThread.joinable())
            return;

        bool completed = false;
        if (state.ActiveBuildJob)
        {
            std::lock_guard<std::mutex> lock(state.ActiveBuildJob->Mutex);
            completed = state.ActiveBuildJob->Completed;
        }

        if (completed)
        {
            state.BuildThread.join();
            ConsumeCompletedBuildResult(state);
            return;
        }

        LT_WARN("Build Settings: build is still running during editor shutdown; detaching to avoid blocking close.");
        state.BuildThread.detach();
        state.ActiveBuildJob.reset();
        state.BuildInProgress.store(false, std::memory_order_release);
    }
}
