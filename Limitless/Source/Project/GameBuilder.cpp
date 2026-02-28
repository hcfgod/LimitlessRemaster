#include "Project/GameBuilder.h"

#include "Assets/AssetBundleBuilder.h"
#include "Core/Debug/Log.h"
#include "Project/ProjectDefinition.h"
#include "Project/RemoteBuildProvider.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
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
    // Helpers
    // -------------------------------------------------------------------------

    namespace
    {
        std::string GetScriptCoreLibraryName(const std::string& targetOS);

        /// Converts a user-facing configuration name (e.g. "Release") to the
        /// premake cfg.shortname token (e.g. "release_x64").
        /// Premake shortname format: lowercase(config)_lowercase(platform).
        std::string ToPremakeShortname(const std::string& configuration, const std::string& targetArchitecture)
        {
            std::string lower = configuration;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            std::string architectureLower = targetArchitecture;
            std::transform(architectureLower.begin(), architectureLower.end(), architectureLower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return lower + "_" + architectureLower;
        }

        std::string ResolveTargetOS(const GameBuildRequest& request)
        {
            if (request.Settings.TargetOS.empty())
                return GetHostBuildTargetOS();
            return request.Settings.TargetOS;
        }

        std::string ResolveTargetArchitecture(const GameBuildRequest& request)
        {
            if (request.Settings.TargetArchitecture.empty())
                return GetHostBuildTargetArchitecture();
            return request.Settings.TargetArchitecture;
        }

        std::string GetTargetPlatformToken(const std::string& targetOS)
        {
            if (targetOS == BuildTargetOS::Windows)
                return "windows";
            if (targetOS == BuildTargetOS::MacOS)
                return "macosx";
            return "linux";
        }

        /// Returns the Runtime build output directory for a given config.
        /// Example: <EngineRoot>/Build/release_x64-windows-x64/Runtime/
        std::filesystem::path GetConfigPlatformBuildFolder(const std::filesystem::path& engineRoot,
                                                           const std::string& configuration,
                                                           const std::string& targetOS,
                                                           const std::string& targetArchitecture)
        {
            const std::string platformToken = GetTargetPlatformToken(targetOS);
            const std::string architectureToken = targetArchitecture;
            const std::string shortname = ToPremakeShortname(configuration, architectureToken);
            const std::string folderName = shortname + "-" + platformToken + "-" + architectureToken;
            return engineRoot / "Build" / folderName;
        }

        std::filesystem::path GetRuntimeBuildDirectory(const std::filesystem::path& engineRoot,
                                                       const std::string& configuration,
                                                       const std::string& targetOS,
                                                       const std::string& targetArchitecture)
        {
            return GetConfigPlatformBuildFolder(engineRoot, configuration, targetOS, targetArchitecture) / "Runtime";
        }

        /// Returns the ScriptCore build output directory for a given config.
        std::filesystem::path GetScriptCoreBuildDirectory(const std::filesystem::path& engineRoot,
                                                          const std::string& configuration,
                                                          const std::string& targetOS,
                                                          const std::string& targetArchitecture)
        {
            return GetConfigPlatformBuildFolder(engineRoot, configuration, targetOS, targetArchitecture) / "Editor";
        }

        std::filesystem::path GetInternalRuntimeTemplateDirectory(const std::filesystem::path& toolchainRoot,
                                                                  const std::string& configuration,
                                                                  const std::string& targetOS,
                                                                  const std::string& targetArchitecture)
        {
            const std::string folderName =
                GetConfigPlatformBuildFolder(std::filesystem::path(), configuration, targetOS, targetArchitecture)
                    .filename()
                    .string();
            return toolchainRoot / "RuntimeTemplates" / folderName;
        }

        std::filesystem::path GetProjectScriptCoreOutputDirectory(const GameBuildRequest& request)
        {
            const std::string folderName =
                GetConfigPlatformBuildFolder(std::filesystem::path(),
                                             request.Settings.BuildConfiguration,
                                             ResolveTargetOS(request),
                                             ResolveTargetArchitecture(request))
                    .filename()
                    .string();
            return request.ProjectRoot / "Build" / "ScriptCore" / folderName;
        }

        std::filesystem::path GetProjectScriptCoreLibraryPath(const GameBuildRequest& request)
        {
            return GetProjectScriptCoreOutputDirectory(request) / GetScriptCoreLibraryName(ResolveTargetOS(request));
        }

        std::string GetRuntimeExecutableName(const std::string& targetOS)
        {
            if (targetOS == BuildTargetOS::Windows)
                return "Runtime.exe";
            return "Runtime";
        }

        std::string GetGameExecutableName(const std::string& projectName, const std::string& targetOS)
        {
            if (targetOS == BuildTargetOS::Windows)
                return projectName + ".exe";
            return projectName;
        }

        std::string GetScriptCoreLibraryName(const std::string& targetOS)
        {
            if (targetOS == BuildTargetOS::Windows)
                return "ScriptCore.dll";
            if (targetOS == BuildTargetOS::MacOS)
                return "libScriptCore.dylib";
            return "libScriptCore.so";
        }

        std::string GetBuildScriptName(bool internalBackend)
        {
#if defined(LT_PLATFORM_WINDOWS)
            if (internalBackend)
                return "build-project-scriptcore-windows.bat";
            return "build-scriptcore-windows.bat";
#else
            if (internalBackend)
                return "build-project-scriptcore-unix.sh";
            return "build-scriptcore-unix.sh";
#endif
        }

        std::string GetBuildPlatformArg(const GameBuildRequest& request)
        {
            return ResolveTargetArchitecture(request);
        }

        bool IsHostTargetPair(const GameBuildRequest& request)
        {
            return ResolveTargetOS(request) == GetHostBuildTargetOS();
        }

        bool IsWindowsHostLinuxTarget(const GameBuildRequest& request)
        {
#if defined(LT_PLATFORM_WINDOWS)
            return ResolveTargetOS(request) == BuildTargetOS::Linux;
#else
            (void)request;
            return false;
#endif
        }

        bool IsWslAvailable()
        {
#if defined(LT_PLATFORM_WINDOWS)
            // Local Linux cross-build requires at least one installed WSL distro.
            return std::system("wsl.exe -l -q | findstr /R . >nul 2>&1") == 0;
#else
            return false;
#endif
        }

        std::string ResolveExecutionMode(const GameBuildRequest& request)
        {
            if (request.Settings.ExecutionMode == BuildExecutionMode::Local ||
                request.Settings.ExecutionMode == BuildExecutionMode::Remote)
            {
                return request.Settings.ExecutionMode;
            }

            if (IsHostTargetPair(request))
                return BuildExecutionMode::Local;

#if defined(LT_PLATFORM_WINDOWS)
            if (IsWindowsHostLinuxTarget(request) && IsWslAvailable())
                return BuildExecutionMode::Local;
#endif

            return BuildExecutionMode::Remote;
        }

#if defined(LT_PLATFORM_WINDOWS)
        std::string ConvertWindowsPathToWslPath(const std::filesystem::path& path)
        {
            std::string value = path.string();
            if (value.size() >= 2 && value[1] == ':')
            {
                const char driveLetter = static_cast<char>(std::tolower(static_cast<unsigned char>(value[0])));
                std::string converted = "/mnt/";
                converted.push_back(driveLetter);
                converted.push_back('/');
                converted += value.substr(2);
                std::replace(converted.begin(), converted.end(), '\\', '/');
                return converted;
            }

            std::replace(value.begin(), value.end(), '\\', '/');
            return value;
        }

        std::string EscapeBashSingleQuoted(std::string value)
        {
            size_t index = 0;
            while ((index = value.find('\'', index)) != std::string::npos)
            {
                value.replace(index, 1, "'\\''");
                index += 4;
            }
            return value;
        }

        std::string BuildWslBashScriptCommand(const std::filesystem::path& engineRoot,
                                              const std::filesystem::path& scriptPath,
                                              const std::vector<std::string>& args)
        {
            const std::string engineRootWsl = EscapeBashSingleQuoted(ConvertWindowsPathToWslPath(engineRoot));
            const std::string scriptWsl = EscapeBashSingleQuoted(ConvertWindowsPathToWslPath(scriptPath));

            std::string bashScript = "cd '" + engineRootWsl + "' && bash '" + scriptWsl + "'";
            for (const std::string& arg : args)
                bashScript += " '" + EscapeBashSingleQuoted(arg) + "'";

            return "wsl.exe bash -lc \"" + bashScript + "\"";
        }

        std::string TrimAsciiWhitespace(std::string value)
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

        std::string NormalizeWslCapturedText(std::string text)
        {
            text.erase(std::remove(text.begin(), text.end(), '\0'), text.end());
            if (text.size() >= 3 &&
                static_cast<unsigned char>(text[0]) == 0xEF &&
                static_cast<unsigned char>(text[1]) == 0xBB &&
                static_cast<unsigned char>(text[2]) == 0xBF)
            {
                text.erase(0, 3);
            }
            return text;
        }

        std::string SanitizeWslDistributionName(std::string value)
        {
            value = NormalizeWslCapturedText(std::move(value));
            std::string sanitized;
            sanitized.reserve(value.size());
            for (const unsigned char character : value)
            {
                if (character < 32 || character == 127)
                    continue;
                if (character == '"' || character == '\'')
                    continue;
                sanitized.push_back(static_cast<char>(character));
            }
            return TrimAsciiWhitespace(std::move(sanitized));
        }

        std::vector<std::string> SplitNonEmptyLines(const std::string& text)
        {
            // Some Windows console processes (including wsl.exe in hidden pipe mode)
            // can emit UTF-16LE-like byte streams with interleaved NUL bytes.
            // Strip NULs so downstream line parsing sees intact ASCII/UTF-8 text.
            const std::string normalizedText = NormalizeWslCapturedText(text);

            std::vector<std::string> lines;
            std::stringstream stream(normalizedText);
            std::string line;
            while (std::getline(stream, line))
            {
                line = TrimAsciiWhitespace(line);
                if (!line.empty())
                    lines.push_back(line);
            }
            return lines;
        }

        bool RunHiddenCommandCapture(const std::string& commandLine,
                                     DWORD timeoutMilliseconds,
                                     DWORD& exitCode,
                                     std::string& stdoutOutput,
                                     const std::function<void(const std::string&)>& outputCallback = {})
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

            const ULONGLONG startTick = GetTickCount64();
            bool timedOut = false;
            std::array<char, 4096> buffer{};
            std::string pendingOutputLine;
            auto flushChunk = [&](const char* chunk, const size_t size)
            {
                if (size == 0)
                    return;

                stdoutOutput.append(chunk, size);
                if (!outputCallback)
                    return;

                pendingOutputLine.append(chunk, size);
                size_t newlineIndex = std::string::npos;
                while ((newlineIndex = pendingOutputLine.find('\n')) != std::string::npos)
                {
                    std::string line = pendingOutputLine.substr(0, newlineIndex);
                    if (!line.empty() && line.back() == '\r')
                        line.pop_back();
                    line.erase(std::remove(line.begin(), line.end(), '\0'), line.end());
                    line = TrimAsciiWhitespace(line);
                    if (!line.empty())
                        outputCallback(line);
                    pendingOutputLine.erase(0, newlineIndex + 1);
                }
            };

            while (true)
            {
                DWORD bytesAvailable = 0;
                if (PeekNamedPipe(readPipe, nullptr, 0, nullptr, &bytesAvailable, nullptr) && bytesAvailable > 0)
                {
                    const DWORD bytesToRead = std::min<DWORD>(bytesAvailable, static_cast<DWORD>(buffer.size()));
                    DWORD bytesRead = 0;
                    if (ReadFile(readPipe, buffer.data(), bytesToRead, &bytesRead, nullptr) && bytesRead > 0)
                        flushChunk(buffer.data(), bytesRead);
                }

                const DWORD waitResult = WaitForSingleObject(processInformation.hProcess, 25);
                if (waitResult == WAIT_OBJECT_0)
                    break;

                if (waitResult == WAIT_FAILED)
                    break;

                const ULONGLONG elapsed = GetTickCount64() - startTick;
                if (elapsed >= timeoutMilliseconds)
                {
                    timedOut = true;
                    TerminateProcess(processInformation.hProcess, WAIT_TIMEOUT);
                    break;
                }
            }

            DWORD bytesRead = 0;
            while (ReadFile(readPipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr) && bytesRead > 0)
                flushChunk(buffer.data(), bytesRead);
            CloseHandle(readPipe);

            if (!pendingOutputLine.empty() && outputCallback)
            {
                pendingOutputLine.erase(std::remove(pendingOutputLine.begin(), pendingOutputLine.end(), '\0'),
                                        pendingOutputLine.end());
                std::string line = TrimAsciiWhitespace(pendingOutputLine);
                if (!line.empty())
                    outputCallback(line);
            }

            (void)GetExitCodeProcess(processInformation.hProcess, &exitCode);
            CloseHandle(processInformation.hThread);
            CloseHandle(processInformation.hProcess);

            if (timedOut)
                exitCode = WAIT_TIMEOUT;

            return true;
        }

        bool EnsureWslLinuxBuildTools(GameBuildResult& result,
                                      const std::function<void(const std::string&)>& progressCallback)
        {
            auto outputIndicatesDistroNotFound = [](const std::string& output) -> bool
            {
                std::string normalized = NormalizeWslCapturedText(output);
                std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                return normalized.find("wsl_e_distro_not_found") != std::string::npos ||
                       normalized.find("there is no distribution with the supplied name.") != std::string::npos;
            };

            DWORD distroExitCode = 1;
            std::string distroListOutput;
            if (!RunHiddenCommandCapture("wsl.exe -l -q", 10000, distroExitCode, distroListOutput) ||
                distroExitCode != 0)
            {
                result.ErrorMessage = "Failed to query WSL distributions. Ensure WSL is installed and try again.";
                return false;
            }

            const auto distributions = SplitNonEmptyLines(distroListOutput);
            if (distributions.empty())
            {
                result.ErrorMessage = "No WSL Linux distribution is installed. Install one (for example Ubuntu) and retry.";
                return false;
            }

            std::string selectedDistribution;
            for (const std::string& candidate : distributions)
            {
                selectedDistribution = SanitizeWslDistributionName(candidate);
                if (!selectedDistribution.empty())
                    break;
            }
            if (selectedDistribution.empty())
            {
                result.ErrorMessage = "Failed to parse an installed WSL distribution name.";
                return false;
            }
            if (progressCallback)
                progressCallback("Using WSL distribution: " + selectedDistribution);

            bool useExplicitDistribution = true;
            auto buildCheckCommand = [&](const bool explicitDistribution)
            {
                const std::string dependencyProbe =
                    "export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:/usr/local/share/pkgconfig:${PKG_CONFIG_PATH:-} && "
                    "command -v make >/dev/null 2>&1 && "
                    "command -v gcc >/dev/null 2>&1 && "
                    "command -v g++ >/dev/null 2>&1 && "
                    "command -v pkg-config >/dev/null 2>&1 && "
                    "pkg-config --exists sdl3 x11 xext xcursor xinerama xi xrandr xss xxf86vm alsa dbus-1 ibus-1.0 libudev gl glx libpulse libavcodec libavformat libavutil libswresample && "
                    "( [ ! -f /usr/local/lib/libSDL3.so ] || ldd /usr/local/lib/libSDL3.so 2>/dev/null | grep -qE 'libGL|libGLX|libEGL|libpulse' )";
                if (explicitDistribution)
                {
                    return "wsl.exe -d \"" + selectedDistribution
                        + "\" bash -lc \"" + dependencyProbe + "\"";
                }
                return std::string("wsl.exe bash -lc \"" + dependencyProbe + "\"");
            };

            DWORD checkExitCode = 1;
            std::string checkOutput;
            std::string checkCommand = buildCheckCommand(useExplicitDistribution);
            if (RunHiddenCommandCapture(checkCommand, 20000, checkExitCode, checkOutput) && checkExitCode == 0)
                return true;
            if (useExplicitDistribution && outputIndicatesDistroNotFound(checkOutput))
            {
                useExplicitDistribution = false;
                if (progressCallback)
                    progressCallback("WSL reported selected distro was not found; retrying with default WSL distro.");

                checkExitCode = 1;
                checkOutput.clear();
                checkCommand = buildCheckCommand(useExplicitDistribution);
                if (RunHiddenCommandCapture(checkCommand, 20000, checkExitCode, checkOutput) && checkExitCode == 0)
                    return true;
            }

            result.StepLog.push_back("WSL dependencies missing. Installing Linux build/runtime dependencies automatically...");
            LT_CORE_INFO("GameBuilder: WSL dependencies missing, attempting automatic install as root.");
            if (progressCallback)
                progressCallback("WSL dependencies missing. Installing Linux build/runtime dependencies automatically...");

            auto buildInstallCommand = [&](const bool explicitDistribution)
            {
                const std::string prefix = explicitDistribution
                    ? "wsl.exe -d \"" + selectedDistribution + "\" -u root bash -lc \""
                    : "wsl.exe -u root bash -lc \"";
                return prefix +
                    "if command -v apt-get >/dev/null 2>&1; then "
                    "export DEBIAN_FRONTEND=noninteractive; "
                    "export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:/usr/local/share/pkgconfig:${PKG_CONFIG_PATH:-}; "
                    "apt-get -o Dpkg::Use-Pty=0 update && "
                    "apt-get -o Dpkg::Use-Pty=0 install -y "
                    "build-essential pkg-config zlib1g-dev curl wget ca-certificates git cmake ninja-build "
                    "libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxinerama-dev libxxf86vm-dev libxss-dev "
                    "libasound2-dev libdbus-1-dev libudev-dev libibus-1.0-dev libpulse-dev "
                    "libavcodec-dev libavformat-dev libavutil-dev libswresample-dev "
                    "libgl1-mesa-dev libglx-dev libegl1-mesa-dev libopengl-dev libglu1-mesa-dev; "
                    "if ! pkg-config --exists sdl3 || { [ -f /usr/local/lib/libSDL3.so ] && ! ldd /usr/local/lib/libSDL3.so 2>/dev/null | grep -qE 'libGL|libGLX|libEGL|libpulse'; }; then "
                    "rm -rf /tmp/limitless_sdl3_build; "
                    "mkdir -p /tmp/limitless_sdl3_build; "
                    "git clone --depth 1 --branch release-3.2.18 https://github.com/libsdl-org/SDL.git /tmp/limitless_sdl3_build/SDL && "
                    "cmake -S /tmp/limitless_sdl3_build/SDL -B /tmp/limitless_sdl3_build/SDL/build -DCMAKE_BUILD_TYPE=Release -DSDL_STATIC=OFF -DSDL_SHARED=ON -DSDL_TEST=OFF -DSDL_INSTALL=ON -DSDL_OPENGL=ON -DSDL_OPENGL_GLX=ON -DSDL_OPENGLES=ON -DSDL_PULSEAUDIO=ON && "
                    "cmake --build /tmp/limitless_sdl3_build/SDL/build --parallel $(nproc) && "
                    "cmake --install /tmp/limitless_sdl3_build/SDL/build && "
                    "if command -v ldconfig >/dev/null 2>&1; then ldconfig; fi; "
                    "rm -rf /tmp/limitless_sdl3_build || true; "
                    "fi; "
                    "elif command -v pacman >/dev/null 2>&1; then "
                    "pacman -Syu --noconfirm --needed "
                    "base-devel pkgconf zlib sdl3 curl wget ca-certificates "
                    "libx11 libxext libxrandr libxcursor libxi libxinerama libxxf86vm libxss mesa "
                    "alsa-lib dbus ibus systemd libpulse ffmpeg; "
                    "else "
                    "echo 'Unsupported Linux package manager in WSL distro.'; "
                    "exit 1; "
                    "fi\"";
            };

            DWORD installExitCode = 1;
            std::string installOutput;
            std::string installCommand = buildInstallCommand(useExplicitDistribution);
            if (!RunHiddenCommandCapture(installCommand, 900000, installExitCode, installOutput, progressCallback))
            {
                result.ErrorMessage = "Failed to start WSL dependency install command.";
                return false;
            }
            if (installExitCode != 0 && useExplicitDistribution && outputIndicatesDistroNotFound(installOutput))
            {
                useExplicitDistribution = false;
                if (progressCallback)
                    progressCallback("WSL reported selected distro was not found during install; retrying with default WSL distro.");

                installExitCode = 1;
                installOutput.clear();
                installCommand = buildInstallCommand(useExplicitDistribution);
                if (!RunHiddenCommandCapture(installCommand, 900000, installExitCode, installOutput, progressCallback))
                {
                    result.ErrorMessage = "Failed to start WSL dependency install command.";
                    return false;
                }
            }
            if (installExitCode != 0)
            {
                result.ErrorMessage =
                    "WSL auto-install failed while installing Linux build/runtime dependencies. "
                    "Open your WSL distro and run: sudo apt-get update && sudo apt-get install -y build-essential pkg-config libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxinerama-dev libxxf86vm-dev libxss-dev libasound2-dev libdbus-1-dev libudev-dev libibus-1.0-dev libpulse-dev libavcodec-dev libavformat-dev libavutil-dev libswresample-dev libgl1-mesa-dev libglx-dev libegl1-mesa-dev libopengl-dev";
                if (installExitCode == WAIT_TIMEOUT)
                    result.ErrorMessage += " (operation timed out)";
                else
                    result.ErrorMessage += " (exit code " + std::to_string(installExitCode) + ")";
                return false;
            }

            checkExitCode = 1;
            checkOutput.clear();
            checkCommand = buildCheckCommand(useExplicitDistribution);
            if (!RunHiddenCommandCapture(checkCommand, 20000, checkExitCode, checkOutput, progressCallback) || checkExitCode != 0)
            {
                const std::string warning =
                    "WSL dependency probe did not validate after auto-install; continuing and letting Runtime build report concrete missing libs if any.";
                result.StepLog.push_back("Warning: " + warning);
                if (progressCallback)
                {
                    progressCallback(warning);
                    progressCallback("WSL post-install probe output:");
                    const auto probeLines = SplitNonEmptyLines(checkOutput);
                    for (const std::string& line : probeLines)
                        progressCallback(line);
                }
            }

            result.StepLog.push_back("WSL build/runtime dependencies installed successfully.");
            if (progressCallback)
                progressCallback("WSL build/runtime dependencies installed successfully.");
            return true;
        }
#endif

        bool IsRemoteExecutionEnabled(const GameBuildRequest& request)
        {
            return ResolveExecutionMode(request) == BuildExecutionMode::Remote;
        }

        bool ResolveConfiguredWindowIconPath(const GameBuildRequest& request,
                                             std::filesystem::path& resolvedPath,
                                             std::string& errorMessage)
        {
            resolvedPath.clear();
            errorMessage.clear();

            if (request.Settings.GameWindowIconPath.empty())
                return true;

            const std::filesystem::path configuredPath(request.Settings.GameWindowIconPath);
            std::vector<std::filesystem::path> candidates;

            if (configuredPath.is_absolute())
            {
                candidates.push_back(configuredPath);
            }
            else
            {
                candidates.push_back(request.ProjectRoot / configuredPath);
                candidates.push_back(request.ProjectRoot / "Assets" / configuredPath);
            }

            std::error_code errorCode;
            for (const std::filesystem::path& candidate : candidates)
            {
                errorCode.clear();
                if (std::filesystem::is_regular_file(candidate, errorCode))
                {
                    resolvedPath = candidate;
                    return true;
                }
            }

            std::ostringstream message;
            message << "Configured game window icon not found: '" << request.Settings.GameWindowIconPath << "'.";
            message << " Checked: ";
            for (size_t index = 0; index < candidates.size(); ++index)
            {
                if (index > 0)
                    message << "; ";
                message << "'" << candidates[index].string() << "'";
            }
            errorMessage = message.str();
            return false;
        }

#if defined(LT_PLATFORM_WINDOWS)
        std::string FormatWindowsErrorMessage(DWORD errorCode)
        {
            LPSTR messageBuffer = nullptr;
            const DWORD size = FormatMessageA(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr,
                errorCode,
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                reinterpret_cast<LPSTR>(&messageBuffer),
                0,
                nullptr);

            std::string message = (size > 0 && messageBuffer) ? std::string(messageBuffer, size) : "Unknown Win32 error";
            if (messageBuffer)
                LocalFree(messageBuffer);

            while (!message.empty() && (message.back() == '\r' || message.back() == '\n'))
                message.pop_back();
            return message + " (code " + std::to_string(errorCode) + ")";
        }

        bool HasIcoExtension(const std::filesystem::path& iconPath)
        {
            std::string extension = iconPath.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
            return extension == ".ico";
        }

        void NotifyWindowsShellFileChanged(const std::filesystem::path& filePath)
        {
            const std::wstring filePathWide = filePath.wstring();
            SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW, filePathWide.c_str(), nullptr);

            if (filePath.has_parent_path())
            {
                const std::wstring parentPathWide = filePath.parent_path().wstring();
                SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_PATHW, parentPathWide.c_str(), nullptr);
            }

            // Force icon association refresh so Explorer picks the updated PE icon
            // for existing filenames instead of stale cached entries.
            SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        }

#pragma pack(push, 1)
        struct IcoDirectoryHeader final
        {
            std::uint16_t Reserved = 0;
            std::uint16_t Type = 0;
            std::uint16_t Count = 0;
        };

        struct IcoDirectoryEntry final
        {
            std::uint8_t Width = 0;
            std::uint8_t Height = 0;
            std::uint8_t ColorCount = 0;
            std::uint8_t Reserved = 0;
            std::uint16_t Planes = 0;
            std::uint16_t BitCount = 0;
            std::uint32_t BytesInRes = 0;
            std::uint32_t ImageOffset = 0;
        };

        struct GroupIconDirectoryHeader final
        {
            std::uint16_t Reserved = 0;
            std::uint16_t Type = 1;
            std::uint16_t Count = 0;
        };

        struct GroupIconDirectoryEntry final
        {
            std::uint8_t Width = 0;
            std::uint8_t Height = 0;
            std::uint8_t ColorCount = 0;
            std::uint8_t Reserved = 0;
            std::uint16_t Planes = 0;
            std::uint16_t BitCount = 0;
            std::uint32_t BytesInRes = 0;
            std::uint16_t ResourceId = 0;
        };
#pragma pack(pop)

        bool EmbedIconIntoWindowsExecutable(const std::filesystem::path& executablePath,
                                            const std::filesystem::path& iconPath,
                                            std::string& errorMessage)
        {
            errorMessage.clear();

            std::ifstream iconStream(iconPath, std::ios::binary | std::ios::ate);
            if (!iconStream.is_open())
            {
                errorMessage = "Could not open icon file: " + iconPath.string();
                return false;
            }

            const std::streamoff streamSize = iconStream.tellg();
            if (streamSize <= 0)
            {
                errorMessage = "Icon file is empty: " + iconPath.string();
                return false;
            }

            std::vector<std::uint8_t> iconBytes(static_cast<size_t>(streamSize));
            iconStream.seekg(0, std::ios::beg);
            iconStream.read(reinterpret_cast<char*>(iconBytes.data()), static_cast<std::streamsize>(iconBytes.size()));
            if (!iconStream)
            {
                errorMessage = "Failed reading icon file bytes: " + iconPath.string();
                return false;
            }

            if (iconBytes.size() < sizeof(IcoDirectoryHeader))
            {
                errorMessage = "Icon file is too small to be a valid .ico: " + iconPath.string();
                return false;
            }

            const auto* directoryHeader = reinterpret_cast<const IcoDirectoryHeader*>(iconBytes.data());
            if (directoryHeader->Reserved != 0 || directoryHeader->Type != 1 || directoryHeader->Count == 0)
            {
                errorMessage = "Icon file has invalid ICO header: " + iconPath.string();
                return false;
            }

            const size_t iconCount = static_cast<size_t>(directoryHeader->Count);
            const size_t tableSize = sizeof(IcoDirectoryHeader) + iconCount * sizeof(IcoDirectoryEntry);
            if (iconBytes.size() < tableSize)
            {
                errorMessage = "Icon file is truncated (directory table out of bounds): " + iconPath.string();
                return false;
            }

            const auto* iconEntries = reinterpret_cast<const IcoDirectoryEntry*>(iconBytes.data() + sizeof(IcoDirectoryHeader));
            for (size_t index = 0; index < iconCount; ++index)
            {
                const size_t imageOffset = static_cast<size_t>(iconEntries[index].ImageOffset);
                const size_t imageSize = static_cast<size_t>(iconEntries[index].BytesInRes);
                if (imageSize == 0 || imageOffset > iconBytes.size() || imageSize > (iconBytes.size() - imageOffset))
                {
                    errorMessage = "Icon file contains invalid image payload bounds: " + iconPath.string();
                    return false;
                }
            }

            auto applyIconResources = [&](const std::filesystem::path& targetExecutablePath) -> bool
            {
                HANDLE updateHandle = BeginUpdateResourceW(targetExecutablePath.wstring().c_str(), FALSE);
                if (!updateHandle)
                {
                    const DWORD winError = GetLastError();
                    errorMessage = "BeginUpdateResource failed for '" + targetExecutablePath.string()
                        + "': " + FormatWindowsErrorMessage(winError);
                    return false;
                }

                auto failAndDiscard = [&](const std::string& message) -> bool
                {
                    EndUpdateResourceW(updateHandle, TRUE);
                    errorMessage = message;
                    return false;
                };

                constexpr std::uint16_t languageId = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);
                constexpr std::uint16_t firstIconResourceId = 1;

                for (size_t index = 0; index < iconCount; ++index)
                {
                    const auto& sourceEntry = iconEntries[index];
                    const std::uint8_t* imageData = iconBytes.data() + static_cast<size_t>(sourceEntry.ImageOffset);
                    const DWORD imageSize = static_cast<DWORD>(sourceEntry.BytesInRes);
                    const WORD iconResourceId = static_cast<WORD>(firstIconResourceId + index);
                    if (!UpdateResourceW(updateHandle,
                                         RT_ICON,
                                         MAKEINTRESOURCEW(iconResourceId),
                                         languageId,
                                         const_cast<std::uint8_t*>(imageData),
                                         imageSize))
                    {
                        const DWORD winError = GetLastError();
                        return failAndDiscard("UpdateResource(RT_ICON) failed for '" + targetExecutablePath.string()
                                              + "': " + FormatWindowsErrorMessage(winError));
                    }
                }

                std::vector<std::uint8_t> groupData(
                    sizeof(GroupIconDirectoryHeader) + iconCount * sizeof(GroupIconDirectoryEntry),
                    0);
                auto* groupHeader = reinterpret_cast<GroupIconDirectoryHeader*>(groupData.data());
                groupHeader->Reserved = 0;
                groupHeader->Type = 1;
                groupHeader->Count = static_cast<std::uint16_t>(iconCount);

                auto* groupEntries = reinterpret_cast<GroupIconDirectoryEntry*>(groupData.data() + sizeof(GroupIconDirectoryHeader));
                for (size_t index = 0; index < iconCount; ++index)
                {
                    const auto& sourceEntry = iconEntries[index];
                    auto& destinationEntry = groupEntries[index];
                    destinationEntry.Width = sourceEntry.Width;
                    destinationEntry.Height = sourceEntry.Height;
                    destinationEntry.ColorCount = sourceEntry.ColorCount;
                    destinationEntry.Reserved = sourceEntry.Reserved;
                    destinationEntry.Planes = sourceEntry.Planes;
                    destinationEntry.BitCount = sourceEntry.BitCount;
                    destinationEntry.BytesInRes = sourceEntry.BytesInRes;
                    destinationEntry.ResourceId = static_cast<std::uint16_t>(firstIconResourceId + index);
                }

                if (!UpdateResourceW(updateHandle,
                                     RT_GROUP_ICON,
                                     L"IDI_MAIN_ICON",
                                     languageId,
                                     groupData.data(),
                                     static_cast<DWORD>(groupData.size())))
                {
                    const DWORD winError = GetLastError();
                    return failAndDiscard("UpdateResource(RT_GROUP_ICON) failed for '" + targetExecutablePath.string()
                                          + "': " + FormatWindowsErrorMessage(winError));
                }

                if (!EndUpdateResourceW(updateHandle, FALSE))
                {
                    const DWORD winError = GetLastError();
                    errorMessage = "EndUpdateResource failed for '" + targetExecutablePath.string()
                        + "': " + FormatWindowsErrorMessage(winError);
                    return false;
                }

                return true;
            };

            std::error_code fileError;
            const std::filesystem::path tempDirectory = std::filesystem::temp_directory_path(fileError) / "LimitlessGameBuilder";
            if (fileError)
            {
                errorMessage = "Failed to resolve temporary directory for icon embedding: " + fileError.message();
                return false;
            }
            std::filesystem::create_directories(tempDirectory, fileError);
            if (fileError)
            {
                errorMessage = "Failed to create temporary directory for icon embedding '" + tempDirectory.string()
                    + "': " + fileError.message();
                return false;
            }

            const auto processId = static_cast<unsigned long>(GetCurrentProcessId());
            const std::filesystem::path tempExecutablePath = tempDirectory
                / (executablePath.stem().string() + ".icon-embed-tmp-" + std::to_string(processId)
                   + executablePath.extension().string());

            std::filesystem::copy_file(executablePath, tempExecutablePath, std::filesystem::copy_options::overwrite_existing, fileError);
            if (fileError)
            {
                errorMessage = "Failed to stage executable for icon embedding from '" + executablePath.string()
                    + "' to '" + tempExecutablePath.string() + "': " + fileError.message();
                return false;
            }

            if (!applyIconResources(tempExecutablePath))
            {
                std::error_code ignoreError;
                std::filesystem::remove(tempExecutablePath, ignoreError);
                return false;
            }

            std::filesystem::copy_file(tempExecutablePath, executablePath, std::filesystem::copy_options::overwrite_existing, fileError);
            std::error_code ignoreError;
            std::filesystem::remove(tempExecutablePath, ignoreError);
            if (fileError)
            {
                errorMessage = "Failed to copy embedded icon executable back to output path '" + executablePath.string()
                    + "': " + fileError.message();
                return false;
            }

            NotifyWindowsShellFileChanged(executablePath);
            return true;
        }
#endif

        /// Copy a single file with overwrite semantics. Logs and returns false on failure.
        bool CopySingleFile(const std::filesystem::path& source,
                            const std::filesystem::path& destination,
                            GameBuildResult& result)
        {
            std::error_code errorCode;
            std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, errorCode);
            if (errorCode)
            {
                const std::string message = "Failed to copy " + source.filename().string() + ": " + errorCode.message();
                result.StepLog.push_back(message);
                LT_CORE_WARN("GameBuilder: {}", message);
                return false;
            }
            return true;
        }

        /// Copy all files matching a pattern from a directory.
        size_t CopyDllsFromDirectory(const std::filesystem::path& sourceDirectory,
                                     const std::filesystem::path& destinationDirectory,
                                     const std::string& extension,
                                     GameBuildResult& result)
        {
            if (!std::filesystem::is_directory(sourceDirectory))
                return 0;

            auto normalizeLower = [](std::string value) -> std::string
            {
                std::transform(value.begin(), value.end(), value.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                return value;
            };

            const std::string expectedExtension = normalizeLower(extension);
            size_t copiedCount = 0;
            std::error_code iterateError;
            for (const auto& entry : std::filesystem::directory_iterator(sourceDirectory, iterateError))
            {
                if (entry.is_regular_file())
                {
                    const std::string fileExtension = normalizeLower(entry.path().extension().string());
                    if (fileExtension != expectedExtension)
                        continue;

                    const auto destPath = destinationDirectory / entry.path().filename();
                    if (CopySingleFile(entry.path(), destPath, result))
                        ++copiedCount;
                }
            }

            if (iterateError)
            {
                result.StepLog.push_back("Warning: failed while scanning dynamic library directory '"
                    + sourceDirectory.string() + "': " + iterateError.message());
            }

            return copiedCount;
        }

        int RunCommand(const std::string& command,
                       const std::function<void(const std::string&)>& outputCallback = {})
        {
            LT_CORE_INFO("GameBuilder: executing: {}", command);
            if (!outputCallback)
                return std::system(command.c_str());

            const std::string redirectedCommand = command + " 2>&1";
#if defined(LT_PLATFORM_WINDOWS)
            FILE* pipe = _popen(redirectedCommand.c_str(), "r");
#else
            FILE* pipe = popen(redirectedCommand.c_str(), "r");
#endif
            if (!pipe)
                return std::system(command.c_str());

            std::array<char, 2048> buffer{};
            while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
            {
                std::string line(buffer.data());
                while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
                    line.pop_back();
                if (!line.empty())
                    outputCallback(line);
            }

#if defined(LT_PLATFORM_WINDOWS)
            return _pclose(pipe);
#else
            const int status = pclose(pipe);
            if (status == -1)
                return status;
            if (WIFEXITED(status))
                return WEXITSTATUS(status);
            return status;
#endif
        }

        bool MirrorProjectNativeScriptsToGeneratedDirectory(const GameBuildRequest& request, GameBuildResult& result)
        {
            const std::filesystem::path assetsRoot = request.ProjectRoot / "Assets";
            if (!std::filesystem::is_directory(assetsRoot))
            {
                result.ErrorMessage = "Cannot mirror scripts: project Assets directory not found at " + assetsRoot.string();
                return false;
            }

            // Mirror into both project-local and engine-local generated roots.
            // - Internal backend script compile reads project-local generated sources.
            // - External backend ScriptCore.vcxproj reads engine-local generated sources.
            std::vector<std::filesystem::path> generatedDirectories;
            generatedDirectories.push_back(request.ProjectRoot / "Build" / "Generated" / "ScriptCore");
            if (!request.EngineRoot.empty())
            {
                const std::filesystem::path engineGeneratedDirectory = request.EngineRoot / "Build" / "Generated" / "ScriptCore";
                if (std::find(generatedDirectories.begin(), generatedDirectories.end(), engineGeneratedDirectory) == generatedDirectories.end())
                    generatedDirectories.push_back(engineGeneratedDirectory);
            }

            std::error_code errorCode;
            for (const std::filesystem::path& generatedDirectory : generatedDirectories)
            {
                std::filesystem::remove_all(generatedDirectory, errorCode);
                if (errorCode)
                {
                    result.ErrorMessage = "Cannot clear generated ScriptCore mirror directory '" + generatedDirectory.string() + "': " + errorCode.message();
                    return false;
                }

                std::filesystem::create_directories(generatedDirectory, errorCode);
                if (errorCode)
                {
                    result.ErrorMessage = "Cannot create generated ScriptCore mirror directory '" + generatedDirectory.string() + "': " + errorCode.message();
                    return false;
                }
            }

            size_t mirroredScriptPairCount = 0;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(assetsRoot, std::filesystem::directory_options::skip_permission_denied))
            {
                if (!entry.is_regular_file() || entry.path().extension() != ".cpp")
                    continue;

                const std::filesystem::path sourceCppPath = entry.path();
                const std::filesystem::path sourceHeaderPath = sourceCppPath.parent_path() / (sourceCppPath.stem().string() + ".h");
                if (!std::filesystem::exists(sourceHeaderPath))
                    continue;

                std::error_code relativePathError;
                const std::filesystem::path relativeCppPath = std::filesystem::relative(sourceCppPath, assetsRoot, relativePathError);
                if (relativePathError || relativeCppPath.empty())
                    continue;

                const std::filesystem::path relativeHeaderPath = std::filesystem::relative(sourceHeaderPath, assetsRoot, relativePathError);
                if (relativePathError || relativeHeaderPath.empty())
                    continue;

                for (const std::filesystem::path& generatedDirectory : generatedDirectories)
                {
                    const std::filesystem::path destinationCppPath = generatedDirectory / relativeCppPath;
                    const std::filesystem::path destinationHeaderPath = generatedDirectory / relativeHeaderPath;

                    std::filesystem::create_directories(destinationCppPath.parent_path(), errorCode);
                    if (errorCode)
                    {
                        result.ErrorMessage = "Failed to create generated script directory '" + destinationCppPath.parent_path().string() + "': " + errorCode.message();
                        return false;
                    }

                    std::filesystem::copy_file(sourceCppPath, destinationCppPath, std::filesystem::copy_options::overwrite_existing, errorCode);
                    if (errorCode)
                    {
                        result.ErrorMessage = "Failed to mirror script source '" + sourceCppPath.string() + "' to '" + destinationCppPath.string() + "': " + errorCode.message();
                        return false;
                    }

                    std::filesystem::copy_file(sourceHeaderPath, destinationHeaderPath, std::filesystem::copy_options::overwrite_existing, errorCode);
                    if (errorCode)
                    {
                        result.ErrorMessage = "Failed to mirror script header '" + sourceHeaderPath.string() + "' to '" + destinationHeaderPath.string() + "': " + errorCode.message();
                        return false;
                    }
                }

                ++mirroredScriptPairCount;
            }

            std::string mirroredDirectoriesText;
            for (size_t index = 0; index < generatedDirectories.size(); ++index)
            {
                if (index != 0)
                    mirroredDirectoriesText += "; ";
                mirroredDirectoriesText += generatedDirectories[index].string();
            }

            result.StepLog.push_back("Mirrored " + std::to_string(mirroredScriptPairCount)
                + " native script source pair(s) into: " + mirroredDirectoriesText + ".");
            return true;
        }
    }

    // -------------------------------------------------------------------------
    // Build Pipeline
    // -------------------------------------------------------------------------

    bool GameBuilder::IsInternalBackend(const GameBuildRequest& request)
    {
        return request.Settings.BuildBackend == BuildBackend::InternalToolchain;
    }

    bool GameBuilder::ValidateRequest(const GameBuildRequest& request, GameBuildResult& result)
    {
        const std::string targetOS = ResolveTargetOS(request);
        const std::string targetArchitecture = ResolveTargetArchitecture(request);
        const std::string executionMode = ResolveExecutionMode(request);
        const bool remoteExecution = (executionMode == BuildExecutionMode::Remote);
        const bool localLinuxCrossViaWsl = IsWindowsHostLinuxTarget(request);

        if (targetOS != BuildTargetOS::Windows &&
            targetOS != BuildTargetOS::MacOS &&
            targetOS != BuildTargetOS::Linux)
        {
            result.ErrorMessage = "Unsupported target OS: " + targetOS;
            return false;
        }

        if (targetArchitecture != BuildTargetArchitecture::X64 &&
            targetArchitecture != BuildTargetArchitecture::ARM64)
        {
            result.ErrorMessage = "Unsupported target architecture: " + targetArchitecture;
            return false;
        }

        if (!remoteExecution && !IsHostTargetPair(request) && !localLinuxCrossViaWsl)
        {
            result.ErrorMessage = "Local execution only supports host platform builds. Switch to Remote mode for cross-platform builds.";
            return false;
        }

        if (!remoteExecution && localLinuxCrossViaWsl && !IsWslAvailable())
        {
            result.ErrorMessage = "WSL is required for local Windows->Linux cross-builds. Install WSL or use Remote mode.";
            return false;
        }

        // Check project root exists.
        if (request.ProjectRoot.empty() || !std::filesystem::is_directory(request.ProjectRoot))
        {
            result.ErrorMessage = "Invalid project root: " + request.ProjectRoot.string();
            return false;
        }

        // Check selected backend root exists.
        if (request.EngineRoot.empty() || !std::filesystem::is_directory(request.EngineRoot))
        {
            if (IsInternalBackend(request))
                result.ErrorMessage = "Invalid internal toolchain root: " + request.EngineRoot.string();
            else
                result.ErrorMessage = "Invalid engine root: " + request.EngineRoot.string();
            return false;
        }

        if (IsInternalBackend(request) && !remoteExecution && !localLinuxCrossViaWsl)
        {
#if defined(LT_PLATFORM_WINDOWS)
            const std::filesystem::path scriptCoreBuildScript = request.EngineRoot / "Scripts" / "build-project-scriptcore-windows.bat";
#else
            const std::filesystem::path scriptCoreBuildScript = request.EngineRoot / "Scripts" / "build-project-scriptcore-unix.sh";
#endif
            const std::filesystem::path runtimeTemplateRoot = request.EngineRoot / "RuntimeTemplates";
            if (!std::filesystem::exists(scriptCoreBuildScript))
            {
                result.ErrorMessage = "Internal toolchain script compile entrypoint missing: " + scriptCoreBuildScript.string();
                return false;
            }
            if (!std::filesystem::is_directory(runtimeTemplateRoot))
            {
                result.ErrorMessage = "Internal runtime template directory missing: " + runtimeTemplateRoot.string();
                return false;
            }
        }

        if (remoteExecution)
        {
            const std::filesystem::path remoteClientScript = request.EngineRoot / "Scripts" / "remote_build_client.py";
            if (!std::filesystem::exists(remoteClientScript))
            {
                result.ErrorMessage = "Remote build client script missing: " + remoteClientScript.string();
                return false;
            }
            const std::string remoteEndpoint = ResolveRemoteBuildEndpoint(request.Settings, targetOS);
            if (remoteEndpoint.empty())
            {
                result.ErrorMessage = "Remote build endpoint is not configured for target OS '" + targetOS + "'.";
                return false;
            }
        }

        // Check at least one enabled build scene.
        const auto enabledScenes = GetEnabledBuildSceneKeys(request.Settings);
        if (enabledScenes.empty())
        {
            result.ErrorMessage = "No enabled scenes in the build settings. Add at least one scene.";
            return false;
        }

        // Check output directory is writable.
        if (request.OutputDirectory.empty())
        {
            result.ErrorMessage = "Output directory is empty. Please choose an output folder.";
            return false;
        }

        std::error_code createDirError;
        std::filesystem::create_directories(request.OutputDirectory, createDirError);
        if (createDirError)
        {
            result.ErrorMessage = "Cannot create output directory: " + createDirError.message();
            return false;
        }

        std::filesystem::path configuredWindowIconPath;
        std::string configuredWindowIconError;
        if (!ResolveConfiguredWindowIconPath(request, configuredWindowIconPath, configuredWindowIconError))
        {
            result.ErrorMessage = configuredWindowIconError;
            return false;
        }

        result.StepLog.push_back("Validation passed: " + std::to_string(enabledScenes.size()) + " scene(s) enabled.");
        result.StepLog.push_back("Target: " + targetOS + " " + targetArchitecture
                                 + " via " + executionMode + " execution.");
        return true;
    }

    bool GameBuilder::BuildAssetBundle(const GameBuildRequest& request, GameBuildResult& result)
    {
        result.StepLog.push_back("Building asset bundle...");

        Assets::AssetBundleBuilder::Settings bundleSettings;
        if (request.Settings.CompressionMode == "None")
            bundleSettings.Compression = Assets::AssetBundleCompression::None;
        else
            bundleSettings.Compression = Assets::AssetBundleCompression::Zstd;
        bundleSettings.ZstdCompressionLevel = request.Settings.ZstdCompressionLevel;

        const auto assetBundleOutputDirectory = request.OutputDirectory / "AssetBundle";
        const auto buildResult = Assets::AssetBundleBuilder::BuildAssetBundleToDirectory(assetBundleOutputDirectory, bundleSettings);
        if (!buildResult.IsSuccess())
        {
            result.ErrorMessage = "Asset bundle build failed: " + buildResult.GetError().GetErrorMessage();
            return false;
        }

        result.StepLog.push_back("Asset bundle built successfully.");
        return true;
    }

    bool GameBuilder::BuildScriptCore(const GameBuildRequest& request, GameBuildResult& result)
    {
        result.StepLog.push_back("Building ScriptCore (" + request.Settings.BuildConfiguration + ")...");

        // Keep game-build ScriptCore in sync with current project scripts.
        if (!MirrorProjectNativeScriptsToGeneratedDirectory(request, result))
            return false;

        const bool internalBackend = IsInternalBackend(request);
        std::filesystem::path scriptPath;
#if defined(LT_PLATFORM_WINDOWS)
        const bool windowsLinuxCross = IsWindowsHostLinuxTarget(request);
        if (windowsLinuxCross)
            scriptPath = request.EngineRoot / "Scripts" / (internalBackend ? "build-project-scriptcore-unix.sh" : "build-scriptcore-unix.sh");
        else
            scriptPath = request.EngineRoot / "Scripts" / GetBuildScriptName(internalBackend);
#else
        scriptPath = request.EngineRoot / "Scripts" / GetBuildScriptName(internalBackend);
#endif
        if (!std::filesystem::exists(scriptPath))
        {
            result.ErrorMessage = "Build script not found: " + scriptPath.string();
            return false;
        }

        const std::string configArg = request.Settings.BuildConfiguration;
        const std::string platformArg = GetBuildPlatformArg(request);

#if defined(LT_PLATFORM_WINDOWS)
        std::string command;
        if (windowsLinuxCross)
        {
            if (!EnsureWslLinuxBuildTools(result, request.ProgressCallback))
                return false;

            std::vector<std::string> args = {
                "--config", configArg,
                "--platform", platformArg
            };
            if (internalBackend)
            {
                args.push_back("--project-root");
                args.push_back(ConvertWindowsPathToWslPath(request.ProjectRoot));
            }
            command = BuildWslBashScriptCommand(request.EngineRoot, scriptPath, args);
        }
        else
        {
            command = "cd /d \"" + request.EngineRoot.string() + "\" && call \""
                + scriptPath.string() + "\" " + configArg + " " + platformArg;
            if (internalBackend)
                command += " \"" + request.ProjectRoot.string() + "\"";
        }
#else
        std::string command = "cd \"" + request.EngineRoot.string() + "\" && bash \""
            + scriptPath.string() + "\" --config " + configArg + " --platform " + platformArg;
        if (internalBackend)
            command += " --project-root \"" + request.ProjectRoot.string() + "\"";
#endif

        const int exitCode = RunCommand(command, request.ProgressCallback);
        if (exitCode != 0)
        {
            result.ErrorMessage = "ScriptCore build failed (exit code " + std::to_string(exitCode) + ").";
            return false;
        }

        if (IsInternalBackend(request))
        {
            const std::filesystem::path builtScriptCorePath =
                GetScriptCoreBuildDirectory(request.EngineRoot,
                                            request.Settings.BuildConfiguration,
                                            ResolveTargetOS(request),
                                            ResolveTargetArchitecture(request))
                / GetScriptCoreLibraryName(ResolveTargetOS(request));
            if (!std::filesystem::exists(builtScriptCorePath))
            {
                result.ErrorMessage = "Built ScriptCore library not found at " + builtScriptCorePath.string();
                return false;
            }

            const std::filesystem::path projectOutputPath = GetProjectScriptCoreLibraryPath(request);
            std::error_code createDirError;
            std::filesystem::create_directories(projectOutputPath.parent_path(), createDirError);
            if (createDirError)
            {
                result.ErrorMessage = "Failed to create project ScriptCore output directory: " + createDirError.message();
                return false;
            }

            if (!CopySingleFile(builtScriptCorePath, projectOutputPath, result))
            {
                result.ErrorMessage = "Failed to stage ScriptCore library into project build output.";
                return false;
            }
            result.StepLog.push_back("Staged ScriptCore to project-local output: " + projectOutputPath.string());
        }

        result.StepLog.push_back("ScriptCore built successfully.");
        return true;
    }

    bool GameBuilder::PrepareLocalArtifacts(const GameBuildRequest& request, BuildArtifactLayout& layout, GameBuildResult& result)
    {
        if (!BuildScriptCore(request, result))
            return false;

        const std::string config = request.Settings.BuildConfiguration;
        const std::string targetOS = ResolveTargetOS(request);
        const bool useInternalBackend = IsInternalBackend(request);

        if (useInternalBackend)
        {
            layout.RuntimeDirectory = GetInternalRuntimeTemplateDirectory(request.EngineRoot,
                                                                          config,
                                                                          targetOS,
                                                                          ResolveTargetArchitecture(request));
            if (!std::filesystem::is_directory(layout.RuntimeDirectory))
            {
                result.ErrorMessage = "Internal runtime template directory not found: " + layout.RuntimeDirectory.string();
                return false;
            }
            result.StepLog.push_back("Using internal runtime templates: " + layout.RuntimeDirectory.string());
        }
        else
        {
#if defined(LT_PLATFORM_WINDOWS)
            const bool windowsLinuxCross = IsWindowsHostLinuxTarget(request);
            if (windowsLinuxCross)
            {
                const auto runtimeBuildScript = request.EngineRoot / "Scripts" / "build-runtime-unix.sh";
                const auto fallbackBuildScript = request.EngineRoot / "Scripts" / "build-unix.sh";
                const auto mainBuildScript = std::filesystem::exists(runtimeBuildScript)
                    ? runtimeBuildScript
                    : fallbackBuildScript;
                if (std::filesystem::exists(mainBuildScript))
                {
                    result.StepLog.push_back("Building Runtime (" + config + ") via WSL...");
                    std::vector<std::string> args = { "--config", config };
                    if (mainBuildScript.filename() == "build-runtime-unix.sh")
                    {
                        args.push_back("--platform");
                        args.push_back(GetBuildPlatformArg(request));
                    }

                    const std::string buildCommand = BuildWslBashScriptCommand(request.EngineRoot, mainBuildScript, args);
                    const int buildExitCode = RunCommand(buildCommand, request.ProgressCallback);
                    if (buildExitCode != 0)
                    {
                        result.ErrorMessage = "Failed to build Runtime via WSL (exit code " + std::to_string(buildExitCode) + ").";
                        return false;
                    }
                }
            }
            else
            {
                const auto runtimeBuildScript = request.EngineRoot / "Scripts" / "build-runtime-windows.bat";
                const auto fallbackBuildScript = request.EngineRoot / "Scripts" / "build-windows.bat";
                const auto mainBuildScript = std::filesystem::exists(runtimeBuildScript)
                    ? runtimeBuildScript
                    : fallbackBuildScript;
                if (std::filesystem::exists(mainBuildScript))
                {
                    result.StepLog.push_back("Building Runtime (" + config + ")...");
                    const std::string buildCommand = "cd /d \"" + request.EngineRoot.string()
                        + "\" && call \"" + mainBuildScript.string() + "\" " + config + " " + GetBuildPlatformArg(request);
                    const int buildExitCode = RunCommand(buildCommand, request.ProgressCallback);
                    if (buildExitCode != 0)
                    {
                        result.ErrorMessage = "Failed to build Runtime (exit code " + std::to_string(buildExitCode) + ").";
                        return false;
                    }
                }
            }
#else
            const auto runtimeBuildScript = request.EngineRoot / "Scripts" / "build-runtime-unix.sh";
            const auto fallbackBuildScript = request.EngineRoot / "Scripts" / "build-unix.sh";
            const auto mainBuildScript = std::filesystem::exists(runtimeBuildScript)
                ? runtimeBuildScript
                : fallbackBuildScript;
            if (std::filesystem::exists(mainBuildScript))
            {
                result.StepLog.push_back("Building Runtime (" + config + ")...");
                std::string buildCommand = "cd \"" + request.EngineRoot.string()
                    + "\" && bash \"" + mainBuildScript.string() + "\" --config " + config;
                if (mainBuildScript.filename() == "build-runtime-unix.sh")
                    buildCommand += " --platform " + GetBuildPlatformArg(request);
                const int buildExitCode = RunCommand(buildCommand, request.ProgressCallback);
                if (buildExitCode != 0)
                {
                    result.ErrorMessage = "Failed to build Runtime (exit code " + std::to_string(buildExitCode) + ").";
                    return false;
                }
            }
#endif
            layout.RuntimeDirectory = GetRuntimeBuildDirectory(request.EngineRoot,
                                                               config,
                                                               targetOS,
                                                               ResolveTargetArchitecture(request));
        }

        layout.ScriptCoreLibraryPath = GetScriptCoreBuildDirectory(request.EngineRoot,
                                                                   config,
                                                                   targetOS,
                                                                   ResolveTargetArchitecture(request))
            / GetScriptCoreLibraryName(targetOS);
        if (useInternalBackend)
        {
            const std::filesystem::path projectLocalScriptCore = GetProjectScriptCoreLibraryPath(request);
            if (std::filesystem::exists(projectLocalScriptCore))
                layout.ScriptCoreLibraryPath = projectLocalScriptCore;
        }

        layout.DynamicLibrarySourceDirectories.clear();
        layout.DynamicLibrarySourceDirectories.push_back(layout.RuntimeDirectory);
        if (targetOS == BuildTargetOS::Windows)
        {
            if (useInternalBackend)
            {
                layout.DynamicLibrarySourceDirectories.push_back(request.EngineRoot / "SDK" / "vendor" / "shaderc" / "dlls");
                layout.DynamicLibrarySourceDirectories.push_back(request.EngineRoot / "SDK" / "vendor" / "ffmpeg" / "dlls");
            }
            else
            {
                layout.DynamicLibrarySourceDirectories.push_back(request.EngineRoot / "Limitless" / "Vendor" / "shaderc" / "dlls");
                layout.DynamicLibrarySourceDirectories.push_back(request.EngineRoot / "Limitless" / "Vendor" / "ffmpeg" / "dlls");
            }
        }

        return true;
    }

    bool GameBuilder::PrepareRemoteArtifacts(const GameBuildRequest& request, BuildArtifactLayout& layout, GameBuildResult& result)
    {
        if (!MirrorProjectNativeScriptsToGeneratedDirectory(request, result))
            return false;

        const std::filesystem::path generatedScriptsDirectory = request.ProjectRoot / "Build" / "Generated" / "ScriptCore";
        RemoteBuildArtifactManifest remoteManifest;
        if (!FetchRemoteBuildArtifacts(request, generatedScriptsDirectory, remoteManifest, result))
            return false;

        layout.RuntimeDirectory = remoteManifest.RuntimeDirectory;
        layout.ScriptCoreLibraryPath = remoteManifest.ScriptCoreLibraryPath;
        layout.DynamicLibrarySourceDirectories = remoteManifest.DynamicLibraryDirectories;
        if (layout.DynamicLibrarySourceDirectories.empty())
            layout.DynamicLibrarySourceDirectories.push_back(layout.RuntimeDirectory);

        return true;
    }

    bool GameBuilder::PrepareBuildArtifacts(const GameBuildRequest& request, BuildArtifactLayout& layout, GameBuildResult& result)
    {
        if (IsRemoteExecutionEnabled(request))
        {
            if (PrepareRemoteArtifacts(request, layout, result))
                return true;

            if (request.Settings.AllowLocalBuildFallback &&
                (IsHostTargetPair(request) ||
                 (IsWindowsHostLinuxTarget(request) && IsWslAvailable())))
            {
                result.StepLog.push_back("Remote build failed, attempting local fallback.");
                result.StepLog.push_back("Remote failure reason: " + result.ErrorMessage);
                result.ErrorMessage.clear();
                return PrepareLocalArtifacts(request, layout, result);
            }

            return false;
        }
        return PrepareLocalArtifacts(request, layout, result);
    }

    bool GameBuilder::CopyRuntimeFiles(const GameBuildRequest& request, const BuildArtifactLayout& layout, GameBuildResult& result)
    {
        result.StepLog.push_back("Copying runtime files...");

        const std::string projectName = request.ProjectName.empty() ? "Game" : request.ProjectName;
        const std::string targetOS = ResolveTargetOS(request);
        const std::filesystem::path runtimeDir = layout.RuntimeDirectory;
        if (!std::filesystem::is_directory(runtimeDir))
        {
            result.ErrorMessage = "Runtime artifact directory is missing: " + runtimeDir.string();
            return false;
        }

        // 1. Copy Runtime executable (renamed to project name).
        const auto runtimeExePath = runtimeDir / GetRuntimeExecutableName(targetOS);
        const auto gameExePath = request.OutputDirectory / GetGameExecutableName(projectName, targetOS);
        if (!std::filesystem::exists(runtimeExePath))
        {
            result.ErrorMessage = "Runtime executable not found after build: " + runtimeExePath.string();
            return false;
        }

        if (!CopySingleFile(runtimeExePath, gameExePath, result))
        {
            result.ErrorMessage = "Failed to copy Runtime executable to output.";
            return false;
        }
        result.OutputExecutablePath = gameExePath;

        std::filesystem::path configuredWindowIconPath;
        std::string configuredWindowIconError;
        if (!ResolveConfiguredWindowIconPath(request, configuredWindowIconPath, configuredWindowIconError))
        {
            result.ErrorMessage = configuredWindowIconError;
            return false;
        }

        const bool hasConfiguredWindowIcon = !configuredWindowIconPath.empty();
        const std::string shippedWindowIconName = hasConfiguredWindowIcon
            ? configuredWindowIconPath.filename().string()
            : "LimitlessLogo.ico";
        std::filesystem::path shippedWindowIconPath = request.OutputDirectory / shippedWindowIconName;

        // 2. Copy config.json.
        const auto sourceConfig = runtimeDir / "config.json";
        if (std::filesystem::exists(sourceConfig))
        {
            CopySingleFile(sourceConfig, request.OutputDirectory / "config.json", result);
            try
            {
                // Stamp shipped game config with project-specific title/icon.
                const auto outputConfigPath = request.OutputDirectory / "config.json";
                std::ifstream in(outputConfigPath, std::ios::in | std::ios::binary);
                if (in.is_open())
                {
                    json configRoot;
                    in >> configRoot;
                    in.close();

                    if (!configRoot.contains("window") || !configRoot["window"].is_object())
                        configRoot["window"] = json::object();
                    configRoot["window"]["title"] = projectName;
                    configRoot["window"]["icon"] = shippedWindowIconName;

                    std::ofstream out(outputConfigPath, std::ios::out | std::ios::binary | std::ios::trunc);
                    if (out.is_open())
                    {
                        out << configRoot.dump(4);
                        result.StepLog.push_back("Updated config.json window settings (title='" + projectName
                            + "', icon='" + shippedWindowIconName + "').");
                    }
                }
            }
            catch (const std::exception& e)
            {
                result.StepLog.push_back(std::string("Warning: failed to update window settings in config.json: ") + e.what());
            }
        }

        // 2b. Copy game window icon so shipped config can resolve `window.icon`.
        if (hasConfiguredWindowIcon)
        {
            if (!CopySingleFile(configuredWindowIconPath, shippedWindowIconPath, result))
            {
                result.ErrorMessage = "Failed to copy configured game window icon: " + configuredWindowIconPath.string();
                return false;
            }
            result.StepLog.push_back("Copied configured game window icon: " + configuredWindowIconPath.string());
        }
        else
        {
            const auto sourceWindowIcon = runtimeDir / "LimitlessLogo.ico";
            if (std::filesystem::exists(sourceWindowIcon))
            {
                shippedWindowIconPath = request.OutputDirectory / "LimitlessLogo.ico";
                if (!CopySingleFile(sourceWindowIcon, shippedWindowIconPath, result))
                {
                    result.ErrorMessage = "Failed to copy runtime window icon.";
                    return false;
                }
            }
        }

#if defined(LT_PLATFORM_WINDOWS)
        if (hasConfiguredWindowIcon && targetOS == BuildTargetOS::Windows)
        {
            std::filesystem::path executableIconPath = shippedWindowIconPath;
            if (!HasIcoExtension(executableIconPath))
            {
                // Explorer executable icons require .ico resource data.
                // If the runtime icon is non-.ico (png/jpg/etc), allow a companion
                // same-stem .ico to drive executable metadata.
                std::filesystem::path companionIcoPath = configuredWindowIconPath;
                companionIcoPath.replace_extension(".ico");
                if (!std::filesystem::exists(companionIcoPath))
                {
                    result.ErrorMessage =
                        "Configured game window icon updates runtime window icon, but Windows executable icon embedding "
                        "requires a .ico file. Provide a .ico icon path (or add companion icon: "
                        + companionIcoPath.string() + ").";
                    return false;
                }

                executableIconPath = request.OutputDirectory / companionIcoPath.filename();
                if (!CopySingleFile(companionIcoPath, executableIconPath, result))
                {
                    result.ErrorMessage = "Failed to copy companion .ico for executable embedding: " + companionIcoPath.string();
                    return false;
                }
                result.StepLog.push_back("Copied companion .ico for executable icon embedding: " + companionIcoPath.string());
            }

            std::string embedError;
            if (!EmbedIconIntoWindowsExecutable(gameExePath, executableIconPath, embedError))
            {
                result.ErrorMessage = "Failed to embed executable icon: " + embedError;
                return false;
            }
            result.StepLog.push_back("Embedded Windows executable icon from: " + executableIconPath.string());
        }
#endif

        // 3. Copy ScriptCore DLL (prefer project-local staging in internal mode).
        const std::filesystem::path scriptCorePath = layout.ScriptCoreLibraryPath;
        if (std::filesystem::exists(scriptCorePath))
        {
            CopySingleFile(scriptCorePath,
                           request.OutputDirectory / GetScriptCoreLibraryName(targetOS),
                           result);
            result.StepLog.push_back("Copied ScriptCore library.");
        }
        else
        {
            result.StepLog.push_back("Warning: ScriptCore library not found at " + scriptCorePath.string());
        }

        // 4. Copy runtime dynamic libraries (shaderc, ffmpeg, etc.).
        std::string dynamicLibraryExtension;
        if (targetOS == BuildTargetOS::Windows)
            dynamicLibraryExtension = ".dll";
        else if (targetOS == BuildTargetOS::Linux)
            dynamicLibraryExtension = ".so";
        else
            dynamicLibraryExtension = ".dylib";

        size_t totalDynamicLibrariesCopied = 0;
        for (const auto& sourceDirectory : layout.DynamicLibrarySourceDirectories)
            totalDynamicLibrariesCopied += CopyDllsFromDirectory(sourceDirectory, request.OutputDirectory, dynamicLibraryExtension, result);
        result.StepLog.push_back("Copied " + std::to_string(totalDynamicLibrariesCopied)
                                 + " runtime '" + dynamicLibraryExtension + "' file(s).");

        result.StepLog.push_back("Runtime files copied.");
        return true;
    }

    bool GameBuilder::WriteGameBootstrap(const GameBuildRequest& request, GameBuildResult& result)
    {
        result.StepLog.push_back("Writing GameBootstrap.json...");

        const auto enabledScenes = GetEnabledBuildSceneKeys(request.Settings);
        const std::string startupScene = enabledScenes.empty() ? "" : enabledScenes.front();

        json root;
        root["projectName"] = request.ProjectName.empty() ? "Game" : request.ProjectName;
        root["startupSceneKey"] = startupScene;

        json scenesArray = json::array();
        for (const auto& sceneKey : enabledScenes)
            scenesArray.push_back(sceneKey);
        root["buildScenes"] = std::move(scenesArray);

        const auto bootstrapPath = request.OutputDirectory / "GameBootstrap.json";
        try
        {
            std::ofstream outputStream(bootstrapPath, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!outputStream.is_open())
            {
                result.ErrorMessage = "Failed to write GameBootstrap.json.";
                return false;
            }
            outputStream << root.dump(2);
        }
        catch (const std::exception& e)
        {
            result.ErrorMessage = std::string("Failed to write GameBootstrap.json: ") + e.what();
            return false;
        }

        result.StepLog.push_back("GameBootstrap.json written (startup: " + startupScene + ").");
        return true;
    }

    bool GameBuilder::FinalizePlatformArtifacts(const GameBuildRequest& request, GameBuildResult& result)
    {
        const std::string projectName = request.ProjectName.empty() ? "Game" : request.ProjectName;
        const std::string targetOS = ResolveTargetOS(request);

        if (targetOS == BuildTargetOS::Windows)
        {
            // Windows layout already finalized during copy stage.
            return true;
        }

        if (result.OutputExecutablePath.empty() || !std::filesystem::exists(result.OutputExecutablePath))
        {
            result.ErrorMessage = "Cannot finalize platform artifacts: output executable not found.";
            return false;
        }

        if (targetOS == BuildTargetOS::MacOS)
        {
            const std::filesystem::path appBundlePath = request.OutputDirectory / (projectName + ".app");
            const std::filesystem::path contentsPath = appBundlePath / "Contents";
            const std::filesystem::path macosPath = contentsPath / "MacOS";
            const std::filesystem::path resourcesPath = contentsPath / "Resources";

            std::error_code ec;
            std::filesystem::remove_all(appBundlePath, ec);
            ec.clear();
            std::filesystem::create_directories(macosPath, ec);
            if (ec)
            {
                result.ErrorMessage = "Failed to create macOS bundle directory '" + macosPath.string() + "': " + ec.message();
                return false;
            }
            ec.clear();
            std::filesystem::create_directories(resourcesPath, ec);
            if (ec)
            {
                result.ErrorMessage = "Failed to create macOS bundle resources directory '" + resourcesPath.string() + "': " + ec.message();
                return false;
            }

            for (const auto& entry : std::filesystem::directory_iterator(request.OutputDirectory))
            {
                const std::filesystem::path sourcePath = entry.path();
                if (sourcePath == appBundlePath)
                    continue;

                const std::filesystem::path destinationPath = macosPath / sourcePath.filename();
                ec.clear();
                if (entry.is_directory())
                {
                    std::filesystem::copy(sourcePath,
                                          destinationPath,
                                          std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
                                          ec);
                }
                else if (entry.is_regular_file())
                {
                    std::filesystem::copy_file(sourcePath, destinationPath, std::filesystem::copy_options::overwrite_existing, ec);
                }

                if (ec)
                {
                    result.ErrorMessage = "Failed to stage file into macOS app bundle ('" + sourcePath.string() + "'): " + ec.message();
                    return false;
                }
            }

            const std::filesystem::path bundledExecutablePath = macosPath / result.OutputExecutablePath.filename();
            ec.clear();
            std::filesystem::permissions(
                bundledExecutablePath,
                std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec | std::filesystem::perms::others_exec,
                std::filesystem::perm_options::add,
                ec);
            if (ec)
            {
                result.ErrorMessage = "Failed to mark bundled executable as executable: " + ec.message();
                return false;
            }

            const std::filesystem::path shippedIconPath = request.OutputDirectory /
                (request.Settings.GameWindowIconPath.empty()
                     ? std::string("LimitlessLogo.ico")
                     : std::filesystem::path(request.Settings.GameWindowIconPath).filename().string());

            std::string iconPlistValue;
            if (std::filesystem::exists(shippedIconPath))
            {
                std::string extension = shippedIconPath.extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (extension == ".icns")
                {
                    const std::filesystem::path iconDestination = resourcesPath / shippedIconPath.filename();
                    ec.clear();
                    std::filesystem::copy_file(shippedIconPath, iconDestination, std::filesystem::copy_options::overwrite_existing, ec);
                    if (ec)
                    {
                        result.ErrorMessage = "Failed to copy .icns into app bundle resources: " + ec.message();
                        return false;
                    }
                    iconPlistValue = shippedIconPath.filename().string();
                }
                else if (!request.Settings.GameWindowIconPath.empty())
                {
                    result.StepLog.push_back("Warning: macOS app icon embedding requires a .icns file; using window icon only.");
                }
            }

            auto sanitizeIdentifierPart = [](std::string value)
            {
                for (char& c : value)
                {
                    const bool isAlnum = std::isalnum(static_cast<unsigned char>(c)) != 0;
                    if (!isAlnum && c != '-' && c != '.')
                        c = '-';
                }
                if (value.empty())
                    value = "game";
                return value;
            };
            const std::string bundleIdentifier = "com.limitless." + sanitizeIdentifierPart(projectName);

            const std::filesystem::path infoPlistPath = contentsPath / "Info.plist";
            std::ofstream infoPlist(infoPlistPath, std::ios::out | std::ios::trunc);
            if (!infoPlist.is_open())
            {
                result.ErrorMessage = "Failed to write Info.plist for macOS app bundle.";
                return false;
            }

            infoPlist
                << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                << "<plist version=\"1.0\">\n"
                << "<dict>\n"
                << "    <key>CFBundleName</key>\n"
                << "    <string>" << projectName << "</string>\n"
                << "    <key>CFBundleDisplayName</key>\n"
                << "    <string>" << projectName << "</string>\n"
                << "    <key>CFBundleExecutable</key>\n"
                << "    <string>" << result.OutputExecutablePath.filename().string() << "</string>\n"
                << "    <key>CFBundleIdentifier</key>\n"
                << "    <string>" << bundleIdentifier << "</string>\n"
                << "    <key>CFBundlePackageType</key>\n"
                << "    <string>APPL</string>\n"
                << "    <key>CFBundleVersion</key>\n"
                << "    <string>1.0</string>\n"
                << "    <key>CFBundleShortVersionString</key>\n"
                << "    <string>1.0</string>\n";
            if (!iconPlistValue.empty())
            {
                infoPlist
                    << "    <key>CFBundleIconFile</key>\n"
                    << "    <string>" << iconPlistValue << "</string>\n";
            }
            infoPlist
                << "</dict>\n"
                << "</plist>\n";
            infoPlist.close();

            if (!infoPlist.good())
            {
                result.ErrorMessage = "Failed writing Info.plist content for macOS app bundle.";
                return false;
            }

            result.OutputExecutablePath = appBundlePath;
            result.StepLog.push_back("Created macOS app bundle: " + appBundlePath.string());
            return true;
        }

        if (targetOS == BuildTargetOS::Linux)
        {
            const std::filesystem::path shippedIconPath = request.OutputDirectory /
                (request.Settings.GameWindowIconPath.empty()
                     ? std::string("LimitlessLogo.ico")
                     : std::filesystem::path(request.Settings.GameWindowIconPath).filename().string());
            const std::filesystem::path desktopPath = request.OutputDirectory / (projectName + ".desktop");

            std::error_code ec;
            std::filesystem::permissions(
                result.OutputExecutablePath,
                std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec | std::filesystem::perms::others_exec,
                std::filesystem::perm_options::add,
                ec);
            if (ec)
                result.StepLog.push_back("Warning: could not mark Linux executable as executable: " + ec.message());

            auto toLowerCopy = [](std::string value)
            {
                std::transform(value.begin(), value.end(), value.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                return value;
            };
            auto escapeShellDoubleQuoted = [](const std::string& value)
            {
                std::string escaped;
                escaped.reserve(value.size() + 8);
                for (char c : value)
                {
                    if (c == '\\' || c == '"' || c == '$' || c == '`')
                        escaped.push_back('\\');
                    escaped.push_back(c);
                }
                return escaped;
            };
            auto escapeDesktopValue = [](std::string value)
            {
                std::string escaped;
                escaped.reserve(value.size() + 8);
                for (char c : value)
                {
                    if (c == '\\' || c == ' ')
                        escaped.push_back('\\');
                    escaped.push_back(c);
                }
                return escaped;
            };
            auto sanitizeDesktopId = [](std::string value)
            {
                std::string sanitized;
                sanitized.reserve(value.size());
                for (char c : value)
                {
                    if (std::isalnum(static_cast<unsigned char>(c)) != 0)
                        sanitized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                    else if (c == '.' || c == '-' || c == '_')
                        sanitized.push_back(c == '_' ? '-' : c);
                    else
                        sanitized.push_back('-');
                }

                while (!sanitized.empty() && sanitized.front() == '-')
                    sanitized.erase(sanitized.begin());
                while (!sanitized.empty() && sanitized.back() == '-')
                    sanitized.pop_back();

                if (sanitized.empty())
                    sanitized = "game";
                return sanitized;
            };

            std::filesystem::path launcherIconPath = shippedIconPath;
            if (std::filesystem::exists(launcherIconPath))
            {
                const std::string extension = toLowerCopy(launcherIconPath.extension().string());
                if (extension == ".ico")
                {
                    std::filesystem::path companionPngPath = launcherIconPath;
                    companionPngPath.replace_extension(".png");
                    if (std::filesystem::exists(companionPngPath))
                    {
                        launcherIconPath = companionPngPath;
                        result.StepLog.push_back("Linux launcher icon switched to companion .png: " + companionPngPath.string());
                    }
                    else if (request.Settings.GameWindowIconPath.empty())
                    {
                        const std::filesystem::path defaultLogoPngSource = request.EngineRoot / "Resources" / "LimitlessLogo.png";
                        const std::filesystem::path defaultLogoPngDestination = request.OutputDirectory / "LimitlessLogo.png";
                        if (std::filesystem::exists(defaultLogoPngSource))
                        {
                            if (CopySingleFile(defaultLogoPngSource, defaultLogoPngDestination, result))
                            {
                                launcherIconPath = defaultLogoPngDestination;
                                result.StepLog.push_back("Copied Linux launcher icon fallback (.png): " + defaultLogoPngSource.string());
                            }
                        }
                    }
                }
            }
            const bool hasLauncherIcon = std::filesystem::exists(launcherIconPath);

            const std::string escapedExecutableName =
                escapeShellDoubleQuoted(result.OutputExecutablePath.filename().string());
            const std::string desktopExecLine =
                "sh -c \"cd \\\"$(dirname \\\"$1\\\")\\\" && if [ -f \\\"./install-linux-desktop-entry.sh\\\" ]; "
                "then sh ./install-linux-desktop-entry.sh >/dev/null 2>&1 || true; fi && exec \\\"./"
                + escapedExecutableName + "\\\"\" sh \"%k\"";

            std::ofstream desktopFile(desktopPath, std::ios::out | std::ios::trunc);
            if (!desktopFile.is_open())
            {
                result.ErrorMessage = "Failed to write Linux desktop entry: " + desktopPath.string();
                return false;
            }

            desktopFile
                << "[Desktop Entry]\n"
                << "Version=1.0\n"
                << "Type=Application\n"
                << "Name=" << projectName << "\n"
                << "Exec=" << desktopExecLine << "\n"
                << "Terminal=false\n"
                << "Categories=Game;\n";
            if (hasLauncherIcon)
            {
                const std::string iconValue = "./" + launcherIconPath.filename().string();
                desktopFile << "Icon=" << escapeDesktopValue(iconValue) << "\n";

                if (toLowerCopy(launcherIconPath.extension().string()) == ".ico")
                {
                    result.StepLog.push_back(
                        "Warning: Linux desktop launchers often ignore .ico icon files. Prefer a .png icon path in Build Settings.");
                }
            }
            desktopFile.close();

            if (!desktopFile.good())
            {
                result.ErrorMessage = "Failed writing Linux desktop entry content.";
                return false;
            }

            ec.clear();
            std::filesystem::permissions(
                desktopPath,
                std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec | std::filesystem::perms::others_exec,
                std::filesystem::perm_options::add,
                ec);
            if (ec)
                result.StepLog.push_back("Warning: could not mark desktop entry executable: " + ec.message());

            const std::filesystem::path installScriptPath = request.OutputDirectory / "install-linux-desktop-entry.sh";
            std::ofstream installScript(installScriptPath, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!installScript.is_open())
            {
                result.ErrorMessage = "Failed to write Linux desktop install script: " + installScriptPath.string();
                return false;
            }

            const std::string desktopAppId = sanitizeDesktopId(projectName);
            installScript
                << "#!/usr/bin/env bash\n"
                << "set -euo pipefail\n\n"
                << "SCRIPT_DIR=\"$(cd -- \"$(dirname -- \"${BASH_SOURCE[0]}\")\" && pwd)\"\n"
                << "EXECUTABLE_NAME=\"" << escapeShellDoubleQuoted(result.OutputExecutablePath.filename().string()) << "\"\n"
                << "APP_NAME=\"" << escapeShellDoubleQuoted(projectName) << "\"\n"
                << "APP_ID=\"" << escapeShellDoubleQuoted(desktopAppId) << "\"\n"
                << "ICON_NAME=\"" << (hasLauncherIcon ? escapeShellDoubleQuoted(launcherIconPath.filename().string()) : std::string()) << "\"\n\n"
                << "escape_exec_value() {\n"
                << "  local value=\"$1\"\n"
                << "  value=\"${value//\\\\/\\\\\\\\}\"\n"
                << "  value=\"${value// /\\\\ }\"\n"
                << "  printf '%s' \"$value\"\n"
                << "}\n\n"
                << "TARGET_DESKTOP_DIR=\"${XDG_DATA_HOME:-$HOME/.local/share}/applications\"\n"
                << "TARGET_DESKTOP_PATH=\"$TARGET_DESKTOP_DIR/${APP_ID}.desktop\"\n"
                << "mkdir -p \"$TARGET_DESKTOP_DIR\"\n\n"
                << "EXEC_PATH=\"$SCRIPT_DIR/$EXECUTABLE_NAME\"\n"
                << "if [[ ! -f \"$EXEC_PATH\" ]]; then\n"
                << "  echo \"Missing executable: $EXEC_PATH\" >&2\n"
                << "  exit 1\n"
                << "fi\n\n"
                << "ESCAPED_EXEC_PATH=\"$(escape_exec_value \"$EXEC_PATH\")\"\n\n"
                << "{\n"
                << "  echo \"[Desktop Entry]\"\n"
                << "  echo \"Version=1.0\"\n"
                << "  echo \"Type=Application\"\n"
                << "  echo \"Name=$APP_NAME\"\n"
                << "  echo \"Exec=$ESCAPED_EXEC_PATH\"\n"
                << "  echo \"Path=$SCRIPT_DIR\"\n"
                << "  echo \"Terminal=false\"\n"
                << "  echo \"Categories=Game;\"\n"
                << "  if [[ -n \"$ICON_NAME\" ]]; then\n"
                << "    ICON_PATH=\"$SCRIPT_DIR/$ICON_NAME\"\n"
                << "    if [[ -f \"$ICON_PATH\" ]]; then\n"
                << "      echo \"Icon=$ICON_PATH\"\n"
                << "    fi\n"
                << "  fi\n"
                << "} > \"$TARGET_DESKTOP_PATH\"\n\n"
                << "chmod +x \"$EXEC_PATH\" \"$TARGET_DESKTOP_PATH\"\n\n"
                << "DESKTOP_SHORTCUT_PATH=\"$HOME/Desktop/${APP_ID}.desktop\"\n"
                << "if [[ -d \"$HOME/Desktop\" ]]; then\n"
                << "  cp \"$TARGET_DESKTOP_PATH\" \"$DESKTOP_SHORTCUT_PATH\" 2>/dev/null || true\n"
                << "  chmod +x \"$DESKTOP_SHORTCUT_PATH\" 2>/dev/null || true\n"
                << "fi\n\n"
                << "if command -v update-desktop-database >/dev/null 2>&1; then\n"
                << "  update-desktop-database \"$TARGET_DESKTOP_DIR\" >/dev/null 2>&1 || true\n"
                << "fi\n\n"
                << "echo \"Installed launcher: $TARGET_DESKTOP_PATH\"\n"
                << "if [[ -f \"$DESKTOP_SHORTCUT_PATH\" ]]; then\n"
                << "  echo \"Desktop shortcut: $DESKTOP_SHORTCUT_PATH\"\n"
                << "fi\n"
                << "echo \"If icon cache is stale, log out/in or restart the desktop shell.\"\n";
            installScript.close();
            if (!installScript.good())
            {
                result.ErrorMessage = "Failed writing Linux desktop install script content.";
                return false;
            }

            ec.clear();
            std::filesystem::permissions(
                installScriptPath,
                std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec | std::filesystem::perms::others_exec,
                std::filesystem::perm_options::add,
                ec);
            if (ec)
                result.StepLog.push_back("Warning: could not mark desktop install script executable: " + ec.message());

            result.StepLog.push_back("Generated Linux desktop launcher: " + desktopPath.string());
            result.StepLog.push_back("Generated portable Linux launcher command (relative to .desktop location).");
            result.StepLog.push_back("Generated Linux launcher installer script: " + installScriptPath.string());
            result.StepLog.push_back("Linux launcher auto-runs 'install-linux-desktop-entry.sh' on launch (best-effort).");
            result.StepLog.push_back("Note: Linux executable files do not support embedded icon metadata; launcher icon is provided via .desktop file.");
            return true;
        }

        result.ErrorMessage = "Unsupported target OS for finalization: " + targetOS;
        return false;
    }

    void GameBuilder::LaunchExecutable(const std::filesystem::path& executablePath)
    {
        if (executablePath.empty() || !std::filesystem::exists(executablePath))
            return;

        const std::string command =
#if defined(LT_PLATFORM_WINDOWS)
            "start \"\" /D \"" + executablePath.parent_path().string() + "\" \"" + executablePath.string() + "\"";
#elif defined(LT_PLATFORM_MACOS)
            "open \"" + executablePath.string() + "\" &";
#else
            "\"" + executablePath.string() + "\" &";
#endif

        LT_CORE_INFO("GameBuilder: launching: {}", command);
        std::system(command.c_str());
    }

    // -------------------------------------------------------------------------
    // Public API
    // -------------------------------------------------------------------------

    GameBuildResult GameBuilder::BuildGame(const GameBuildRequest& request)
    {
        GameBuildResult result;
        const auto startTime = std::chrono::steady_clock::now();
        BuildArtifactLayout artifactLayout;

        LT_CORE_INFO("GameBuilder: Starting build -> {}", request.OutputDirectory.string());

        if (!ValidateRequest(request, result))
        {
            result.StepLog.push_back("Build failed: " + result.ErrorMessage);
            return result;
        }

        if (!BuildAssetBundle(request, result))
        {
            result.StepLog.push_back("Build failed: " + result.ErrorMessage);
            return result;
        }

        if (!PrepareBuildArtifacts(request, artifactLayout, result))
        {
            result.StepLog.push_back("Build failed: " + result.ErrorMessage);
            return result;
        }

        if (!CopyRuntimeFiles(request, artifactLayout, result))
        {
            result.StepLog.push_back("Build failed: " + result.ErrorMessage);
            return result;
        }

        if (!WriteGameBootstrap(request, result))
        {
            result.StepLog.push_back("Build failed: " + result.ErrorMessage);
            return result;
        }

        if (!FinalizePlatformArtifacts(request, result))
        {
            result.StepLog.push_back("Build failed: " + result.ErrorMessage);
            return result;
        }

        const auto endTime = std::chrono::steady_clock::now();
        result.ElapsedSeconds = std::chrono::duration<float>(endTime - startTime).count();
        result.Success = true;
        result.StepLog.push_back("Build succeeded in " + std::to_string(result.ElapsedSeconds) + "s.");
        LT_CORE_INFO("GameBuilder: Build completed successfully in {:.2f}s.", result.ElapsedSeconds);

        return result;
    }

    GameBuildResult GameBuilder::BuildAndRunGame(const GameBuildRequest& request)
    {
        auto result = BuildGame(request);
        if (result.Success && !result.OutputExecutablePath.empty())
        {
            if (ResolveTargetOS(request) != GetHostBuildTargetOS())
            {
                result.StepLog.push_back("Skipped launch: target platform differs from host platform.");
            }
            else
            {
                result.StepLog.push_back("Launching game...");
                LaunchExecutable(result.OutputExecutablePath);
            }
        }
        return result;
    }
}
