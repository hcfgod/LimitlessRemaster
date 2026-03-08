#include "EditorInspectorPanel.h"
#include "EditorInspectorPanelNativeScriptEditor.h"
#include "EditorPanelStyle.h"
#include "Limitless.h"
#include "Platform/Platform.h"
#include "Project/BuildSettings.h"
#include "Project/BuildTargetsSettings.h"
#include "Project/ProjectManager.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetPaths.h"
#include "Scripting/NativeScriptRegistry.h"
#include "Scripting/NativeScriptExternalEditor.h"
#include "Core/Debug/Log.h"
#include "imgui/imgui.h"

#include <array>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <optional>
#include <atomic>
#include <thread>
#include <mutex>
#include <regex>
#include <string_view>
#include <unordered_set>
#include <vector>

#ifdef LT_PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <shellapi.h>
#else
    #include <sys/wait.h>
    #include <unistd.h>
#endif

namespace Limitless::EditorInspectorPanel
{
    namespace
    {
        constexpr size_t kNativeScriptEditorBufferSize = 256 * 1024;

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

        NativeScriptAuthoringState& GetNativeScriptAuthoringState()
        {
            static NativeScriptAuthoringState state;
            return state;
        }

        bool s_HasPendingNativeScriptEditorSessionRestore = false;
        NativeScriptEditorSessionState s_PendingNativeScriptEditorSessionState;

        std::optional<std::filesystem::path> GetOpenedProjectRoot()
        {
            const auto& projectManager = Project::ProjectManager::GetInstance();
            if (!projectManager.HasOpenProject())
                return std::nullopt;
            return projectManager.GetProjectRoot();
        }

        std::string GetScriptCoreLibraryFileName()
        {
#if defined(LT_PLATFORM_WINDOWS)
            return "ScriptCore.dll";
#elif defined(LT_PLATFORM_MACOS)
            return "libScriptCore.dylib";
#else
            return "libScriptCore.so";
#endif
        }

        std::string NormalizeBuildPlatformToken(const std::string& platform)
        {
            if (platform.empty())
                return "x64";

            std::string upper = platform;
            std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) {
                return static_cast<char>(std::toupper(c));
            });
            if (upper == "ARM64")
                return "ARM64";
            return "x64";
        }

        std::string ToBuildConfigShortname(const std::string& configuration, const std::string& platform)
        {
            std::string lower = configuration.empty() ? "debug" : configuration;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            const std::string normalizedPlatform = NormalizeBuildPlatformToken(platform);
            return lower + (normalizedPlatform == "ARM64" ? "_arm64" : "_x64");
        }

        std::string BuildConfigFolderName(const std::string& configuration, const std::string& platform)
        {
#if defined(LT_PLATFORM_WINDOWS)
            const std::string platformToken = "windows";
#elif defined(LT_PLATFORM_MACOS)
            const std::string platformToken = "macosx";
#else
            const std::string platformToken = "linux";
#endif

            const std::string architectureToken = NormalizeBuildPlatformToken(platform);
            return ToBuildConfigShortname(configuration, platform) + "-" + platformToken + "-" + architectureToken;
        }

        std::string NormalizeScriptEditorMode(std::string mode)
        {
            if (mode == Project::ScriptEditorMode::Internal || mode == Project::ScriptEditorMode::External)
                return mode;
            return Project::ScriptEditorMode::Internal;
        }

        bool OpenPathInExternalApplication(const std::filesystem::path& path, std::string& outError)
        {
            outError.clear();
            if (path.empty())
            {
                outError = "Target path is empty.";
                return false;
            }

            std::error_code errorCode;
            if (!std::filesystem::exists(path, errorCode))
            {
                outError = "Target file was not found: " + path.string();
                return false;
            }

#if defined(LT_PLATFORM_WINDOWS)
            const std::string pathString = path.string();
            const HINSTANCE result = ShellExecuteA(nullptr, "open", pathString.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            if (reinterpret_cast<intptr_t>(result) > 32)
                return true;
            outError = "Failed to open script in the external application.";
            return false;
#else
            pid_t pid = fork();
            if (pid < 0)
            {
                outError = "Failed to spawn external opener process.";
                return false;
            }

            if (pid == 0)
            {
#if defined(LT_PLATFORM_MACOS)
                execlp("open", "open", path.string().c_str(), static_cast<char*>(nullptr));
#else
                execlp("xdg-open", "xdg-open", path.string().c_str(), static_cast<char*>(nullptr));
#endif
                _exit(127);
            }

            return true;
#endif
        }

        bool IsInternalToolchainRootCandidate(const std::filesystem::path& candidate)
        {
            std::error_code errorCode;
#if defined(LT_PLATFORM_WINDOWS)
            const std::filesystem::path scriptCoreBuildScript = candidate / "Scripts" / "build-project-scriptcore-windows.bat";
#else
            const std::filesystem::path scriptCoreBuildScript = candidate / "Scripts" / "build-project-scriptcore-unix.sh";
#endif
            const std::filesystem::path sdkIncludeRoot = candidate / "SDK" / "include";
            const std::filesystem::path sdkLibRoot = candidate / "SDK" / "lib";
            return std::filesystem::exists(scriptCoreBuildScript, errorCode) &&
                   std::filesystem::is_directory(sdkIncludeRoot, errorCode) &&
                   std::filesystem::is_directory(sdkLibRoot, errorCode);
        }

        std::optional<std::filesystem::path> FindEngineWorkspaceRoot()
        {
            auto isEngineRootCandidate = [](const std::filesystem::path& candidate) -> bool
            {
                std::error_code errorCode;
#if defined(LT_PLATFORM_WINDOWS)
                const std::filesystem::path scriptCoreBuildScript = candidate / "Scripts" / "build-scriptcore-windows.bat";
#else
                const std::filesystem::path scriptCoreBuildScript = candidate / "Scripts" / "build-scriptcore-unix.sh";
#endif
                const std::filesystem::path scriptsFolder = candidate / "Scripts";
                const std::filesystem::path solutionPath = candidate / "LimitlessRemaster.sln";

                if (std::filesystem::exists(scriptCoreBuildScript, errorCode) &&
                    std::filesystem::is_regular_file(scriptCoreBuildScript, errorCode))
                    return true;
                if (std::filesystem::is_directory(scriptsFolder, errorCode) &&
                    std::filesystem::exists(solutionPath, errorCode))
                    return true;
                return false;
            };

            auto walkUp = [&](std::filesystem::path probe) -> std::optional<std::filesystem::path>
            {
                for (int depth = 0; depth < 32 && !probe.empty(); ++depth)
                {
                    if (isEngineRootCandidate(probe))
                        return probe;

                    if (!probe.has_parent_path())
                        break;
                    const std::filesystem::path parent = probe.parent_path();
                    if (parent == probe)
                        break;
                    probe = parent;
                }
                return std::nullopt;
            };

            if (const auto openedProjectRoot = GetOpenedProjectRoot(); openedProjectRoot.has_value())
            {
                const auto buildSettingsResult = Project::LoadBuildSettings(openedProjectRoot.value());
                if (buildSettingsResult.IsSuccess())
                {
                    const auto& buildSettings = buildSettingsResult.GetValue();
                    bool useInternalBackend = (buildSettings.BuildBackend == Project::BuildBackend::InternalToolchain);
                    const std::string& overrideRoot = buildSettings.EngineRootOverride;
                    if (!overrideRoot.empty())
                    {
                        std::filesystem::path candidate = std::filesystem::path(overrideRoot);
                        std::error_code ec;
                        candidate = std::filesystem::weakly_canonical(candidate, ec);
                        if (ec)
                            candidate = std::filesystem::path(overrideRoot);
                        if (!useInternalBackend && IsInternalToolchainRootCandidate(candidate))
                            useInternalBackend = true;
                        if (useInternalBackend ? IsInternalToolchainRootCandidate(candidate) : isEngineRootCandidate(candidate))
                            return candidate;
                    }

                    if (useInternalBackend)
                    {
                        if (const char* envRoot = std::getenv("LIMITLESS_TOOLCHAIN_ROOT"); envRoot && envRoot[0] != '\0')
                        {
                            std::filesystem::path candidate(envRoot);
                            std::error_code ec;
                            candidate = std::filesystem::weakly_canonical(candidate, ec);
                            if (ec)
                                candidate = std::filesystem::path(envRoot);
                            if (IsInternalToolchainRootCandidate(candidate))
                                return candidate;
                        }

                        const auto& platformInfo = PlatformDetection::GetPlatformInfo();
                        if (!platformInfo.executablePath.empty())
                        {
                            const std::filesystem::path executableDirectory = std::filesystem::path(platformInfo.executablePath).parent_path();
                            const std::filesystem::path toolchainSibling = executableDirectory / "Toolchain";
                            if (IsInternalToolchainRootCandidate(toolchainSibling))
                                return toolchainSibling;
                            if (IsInternalToolchainRootCandidate(executableDirectory))
                                return executableDirectory;
                        }

                        std::error_code errorCode;
                        const std::filesystem::path cwd = std::filesystem::current_path(errorCode);
                        if (!errorCode)
                        {
                            if (IsInternalToolchainRootCandidate(cwd))
                                return cwd;
                            const std::filesystem::path cwdToolchain = cwd / "Toolchain";
                            if (IsInternalToolchainRootCandidate(cwdToolchain))
                                return cwdToolchain;
                        }
                    }
                }
            }

            if (const char* envRoot = std::getenv("LIMITLESS_ENGINE_ROOT"); envRoot && envRoot[0] != '\0')
            {
                std::filesystem::path candidate(envRoot);
                std::error_code ec;
                candidate = std::filesystem::weakly_canonical(candidate, ec);
                if (ec)
                    candidate = std::filesystem::path(envRoot);
                if (isEngineRootCandidate(candidate))
                    return candidate;
            }

            const auto& platformInfo = PlatformDetection::GetPlatformInfo();
            if (!platformInfo.executablePath.empty())
            {
                if (const auto fromExe = walkUp(std::filesystem::path(platformInfo.executablePath).parent_path()); fromExe.has_value())
                    return fromExe;
            }

            std::error_code errorCode;
            if (const auto fromCwd = walkUp(std::filesystem::current_path(errorCode)); fromCwd.has_value())
                return fromCwd;

            if (const char* envRoot = std::getenv("LIMITLESS_TOOLCHAIN_ROOT"); envRoot && envRoot[0] != '\0')
            {
                std::filesystem::path candidate(envRoot);
                std::error_code ec;
                candidate = std::filesystem::weakly_canonical(candidate, ec);
                if (ec)
                    candidate = std::filesystem::path(envRoot);
                if (IsInternalToolchainRootCandidate(candidate))
                    return candidate;
            }

            if (!platformInfo.executablePath.empty())
            {
                const std::filesystem::path executableDirectory = std::filesystem::path(platformInfo.executablePath).parent_path();
                const std::filesystem::path toolchainSibling = executableDirectory / "Toolchain";
                if (IsInternalToolchainRootCandidate(toolchainSibling))
                    return toolchainSibling;
                if (IsInternalToolchainRootCandidate(executableDirectory))
                    return executableDirectory;
            }

            std::error_code fallbackErrorCode;
            const std::filesystem::path fallbackCwd = std::filesystem::current_path(fallbackErrorCode);
            if (!fallbackErrorCode)
            {
                if (IsInternalToolchainRootCandidate(fallbackCwd))
                    return fallbackCwd;
                const std::filesystem::path cwdToolchain = fallbackCwd / "Toolchain";
                if (IsInternalToolchainRootCandidate(cwdToolchain))
                    return cwdToolchain;
            }

            return std::nullopt;
        }

        std::optional<std::filesystem::path> FindEngineSourceWorkspaceRoot()
        {
            auto isEngineRootCandidate = [](const std::filesystem::path& candidate) -> bool
            {
                std::error_code errorCode;
#if defined(LT_PLATFORM_WINDOWS)
                const std::filesystem::path scriptCoreBuildScript = candidate / "Scripts" / "build-scriptcore-windows.bat";
#else
                const std::filesystem::path scriptCoreBuildScript = candidate / "Scripts" / "build-scriptcore-unix.sh";
#endif
                const std::filesystem::path scriptsFolder = candidate / "Scripts";
                const std::filesystem::path solutionPath = candidate / "LimitlessRemaster.sln";

                if (std::filesystem::exists(scriptCoreBuildScript, errorCode) &&
                    std::filesystem::is_regular_file(scriptCoreBuildScript, errorCode))
                    return true;
                if (std::filesystem::is_directory(scriptsFolder, errorCode) &&
                    std::filesystem::exists(solutionPath, errorCode))
                    return true;
                return false;
            };

            auto walkUp = [&](std::filesystem::path probe) -> std::optional<std::filesystem::path>
            {
                for (int depth = 0; depth < 32 && !probe.empty(); ++depth)
                {
                    if (isEngineRootCandidate(probe))
                        return probe;

                    if (!probe.has_parent_path())
                        break;
                    const std::filesystem::path parent = probe.parent_path();
                    if (parent == probe)
                        break;
                    probe = parent;
                }
                return std::nullopt;
            };

            if (const auto openedProjectRoot = GetOpenedProjectRoot(); openedProjectRoot.has_value())
            {
                const auto buildSettingsResult = Project::LoadBuildSettings(openedProjectRoot.value());
                if (buildSettingsResult.IsSuccess())
                {
                    const std::string& overrideRoot = buildSettingsResult.GetValue().EngineRootOverride;
                    if (!overrideRoot.empty())
                    {
                        std::filesystem::path candidate = std::filesystem::path(overrideRoot);
                        std::error_code ec;
                        candidate = std::filesystem::weakly_canonical(candidate, ec);
                        if (ec)
                            candidate = std::filesystem::path(overrideRoot);
                        if (isEngineRootCandidate(candidate))
                            return candidate;
                    }
                }
            }

            if (const char* envRoot = std::getenv("LIMITLESS_ENGINE_ROOT"); envRoot && envRoot[0] != '\0')
            {
                std::filesystem::path candidate(envRoot);
                std::error_code ec;
                candidate = std::filesystem::weakly_canonical(candidate, ec);
                if (ec)
                    candidate = std::filesystem::path(envRoot);
                if (isEngineRootCandidate(candidate))
                    return candidate;
            }

            const auto& platformInfo = PlatformDetection::GetPlatformInfo();
            if (!platformInfo.executablePath.empty())
            {
                if (const auto fromExe = walkUp(std::filesystem::path(platformInfo.executablePath).parent_path()); fromExe.has_value())
                    return fromExe;
            }

            std::error_code errorCode;
            if (const auto fromCwd = walkUp(std::filesystem::current_path(errorCode)); fromCwd.has_value())
                return fromCwd;

            return std::nullopt;
        }

        std::vector<std::filesystem::path> GetGeneratedScriptCoreMirrorDirectories()
        {
            std::vector<std::filesystem::path> directories;

            auto& projectManager = Project::ProjectManager::GetInstance();
            if (projectManager.HasOpenProject())
                directories.push_back(projectManager.GetProjectRoot() / "Build" / "Generated" / "ScriptCore");

            if (const auto engineRoot = FindEngineWorkspaceRoot(); engineRoot.has_value())
            {
                const std::filesystem::path engineMirror = engineRoot.value() / "Build" / "Generated" / "ScriptCore";
                if (std::find(directories.begin(), directories.end(), engineMirror) == directories.end())
                    directories.push_back(engineMirror);
            }

            return directories;
        }

        std::optional<std::filesystem::path> GetGeneratedScriptCoreMirrorDirectory()
        {
            const std::vector<std::filesystem::path> directories = GetGeneratedScriptCoreMirrorDirectories();
            if (directories.empty())
                return std::nullopt;
            return directories.front();
        }

        std::optional<std::filesystem::path> InferProjectRootFromScriptSourcePath(const std::filesystem::path& sourcePath)
        {
            if (sourcePath.empty())
                return std::nullopt;

            std::filesystem::path probe = sourcePath.parent_path();
            for (int depth = 0; depth < 48 && !probe.empty(); ++depth)
            {
                if (probe.filename() == "Assets")
                {
                    if (probe.has_parent_path())
                        return probe.parent_path();
                    return std::nullopt;
                }

                if (!probe.has_parent_path())
                    break;
                const std::filesystem::path parent = probe.parent_path();
                if (parent == probe)
                    break;
                probe = parent;
            }

            return std::nullopt;
        }

        std::filesystem::path GetBuiltScriptCoreLibraryPath(const std::filesystem::path& buildRoot,
                                                            const std::string& configuration,
                                                            const std::string& platform)
        {
            return buildRoot / "Build" / BuildConfigFolderName(configuration, platform) / "Editor" / GetScriptCoreLibraryFileName();
        }

        std::filesystem::path GetProjectLocalScriptCoreLibraryPath(const std::filesystem::path& projectRoot,
                                                                   const std::string& configuration,
                                                                   const std::string& platform)
        {
            return projectRoot / "Build" / "ScriptCore" / BuildConfigFolderName(configuration, platform) / GetScriptCoreLibraryFileName();
        }

        std::optional<std::filesystem::path> GetOpenedProjectAssetScriptsDirectory()
        {
            const auto& projectManager = Project::ProjectManager::GetInstance();
            if (!projectManager.HasOpenProject())
                return std::nullopt;
            return projectManager.GetProjectRoot() / "Assets" / "Scripts";
        }

        std::optional<std::filesystem::path> GetOpenedProjectAssetsRoot()
        {
            const auto projectRoot = GetOpenedProjectRoot();
            if (!projectRoot.has_value())
                return std::nullopt;
            return projectRoot.value() / "Assets";
        }

        std::optional<std::filesystem::path> GetAuthoringNativeScriptsDirectory()
        {
            const auto openedProjectDirectory = GetOpenedProjectAssetScriptsDirectory();
            if (openedProjectDirectory.has_value())
                return openedProjectDirectory;
            return std::nullopt;
        }

        std::pair<std::string, std::string> GetBuildConfigurationAndPlatform(const std::filesystem::path& settingsRoot)
        {
            std::string configuration = "Debug";
#if defined(LT_CONFIG_RELEASE)
            configuration = "Release";
#elif defined(LT_CONFIG_DIST)
            configuration = "Dist";
#endif

            const auto buildTargetsResult = Project::LoadBuildTargetsSettings(settingsRoot);
            if (buildTargetsResult.IsSuccess())
            {
                const auto& settings = buildTargetsResult.GetValue();
                const std::string platform = settings.Platform.empty() ? "x64" : settings.Platform;
                return { configuration, platform };
            }

            return { configuration, "x64" };
        }

        std::filesystem::path BuildNativeScriptBuildLogPath()
        {
            std::error_code errorCode;
            std::filesystem::path tempRoot = std::filesystem::temp_directory_path(errorCode);
            if (errorCode || tempRoot.empty())
                tempRoot = std::filesystem::current_path(errorCode);

            const auto uniqueSuffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
            return tempRoot / ("limitless-native-script-build-" + uniqueSuffix + ".log");
        }

        std::string ReadTextFileOrEmpty(const std::filesystem::path& path)
        {
            std::ifstream input(path, std::ios::in | std::ios::binary);
            if (!input.is_open())
                return {};

            return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        }

        bool LooksLikePrefabAssetKey(const std::string& value)
        {
            constexpr std::string_view prefabExtension = ".prefab.json";
            if (value.size() >= prefabExtension.size() &&
                value.compare(value.size() - prefabExtension.size(), prefabExtension.size(), prefabExtension) == 0)
            {
                return true;
            }

            const auto record = Assets::AssetDatabase::GetInstance().FindByKey(value);
            return record.IsSuccess() && record.GetValue().Type == Assets::AssetType::Prefab;
        }

        int RunBuildScriptBlocking(const std::filesystem::path& buildRoot,
                                   const std::filesystem::path& openedProjectRoot,
                                   const std::string& configuration,
                                   const std::string& platform,
                                   bool hasOpenedProject,
                                   bool useInternalBackend,
                                   std::string& outBuildOutput)
        {
            outBuildOutput.clear();
            const std::filesystem::path buildLogPath = BuildNativeScriptBuildLogPath();

#ifdef LT_PLATFORM_WINDOWS
            const std::string scriptName = useInternalBackend
                ? "build-project-scriptcore-windows.bat"
                : "build-scriptcore-windows.bat";
            std::string scriptCommand = "cmd.exe /c \"Scripts\\" + scriptName + " " + configuration + " " + platform;
            if (hasOpenedProject)
                scriptCommand += " \"" + openedProjectRoot.string() + "\"";
            scriptCommand += " > \"" + buildLogPath.string() + "\" 2>&1\"";
            STARTUPINFOA startupInfo{};
            startupInfo.cb = sizeof(startupInfo);
            PROCESS_INFORMATION processInformation{};

            std::string mutableCommandLine = scriptCommand;
            const BOOL created = CreateProcessA(
                nullptr,
                mutableCommandLine.data(),
                nullptr,
                nullptr,
                FALSE,
                0,
                nullptr,
                buildRoot.string().c_str(),
                &startupInfo,
                &processInformation);

            if (!created)
            {
                outBuildOutput = "Failed to start native script build process.";
                return 1;
            }

            WaitForSingleObject(processInformation.hProcess, INFINITE);

            DWORD exitCode = 1;
            (void)GetExitCodeProcess(processInformation.hProcess, &exitCode);
            CloseHandle(processInformation.hThread);
            CloseHandle(processInformation.hProcess);
            outBuildOutput = ReadTextFileOrEmpty(buildLogPath);

            std::error_code cleanupError;
            std::filesystem::remove(buildLogPath, cleanupError);

            return static_cast<int>(exitCode);
#else
            const std::string scriptName = useInternalBackend
                ? "build-project-scriptcore-unix.sh"
                : "build-scriptcore-unix.sh";
            std::string scriptCommand =
                "cd \"" + buildRoot.string() + "\" && bash \"Scripts/" + scriptName + "\" --config \"" + configuration + "\" --platform \"" + platform + "\"";
            if (hasOpenedProject)
                scriptCommand += " --project-root \"" + openedProjectRoot.string() + "\"";
            scriptCommand += " > \"" + buildLogPath.string() + "\" 2>&1";

            const int systemResult = std::system(scriptCommand.c_str());
            outBuildOutput = ReadTextFileOrEmpty(buildLogPath);

            std::error_code cleanupError;
            std::filesystem::remove(buildLogPath, cleanupError);

            if (systemResult == -1)
                return 1;

            if (WIFEXITED(systemResult))
                return WEXITSTATUS(systemResult);

            return systemResult;
#endif
        }

        bool MirrorAllProjectNativeScriptsToGeneratedDirectory(std::string& outError)
        {
            const auto assetsRoot = GetOpenedProjectAssetsRoot();
            if (!assetsRoot.has_value())
            {
                outError = "Cannot mirror scripts: no opened project assets root.";
                return false;
            }

            const std::vector<std::filesystem::path> generatedDirectories = GetGeneratedScriptCoreMirrorDirectories();
            if (generatedDirectories.empty())
            {
                outError = "Cannot mirror scripts: generated ScriptCore mirror directory was not found.";
                return false;
            }

            std::error_code createDirectoriesError;
            for (const std::filesystem::path& generatedDirectory : generatedDirectories)
            {
                std::filesystem::remove_all(generatedDirectory, createDirectoriesError);
                if (createDirectoriesError)
                {
                    outError = "Cannot clear generated ScriptCore mirror directory '" + generatedDirectory.string() + "': " + createDirectoriesError.message();
                    return false;
                }

                std::filesystem::create_directories(generatedDirectory, createDirectoriesError);
                if (createDirectoriesError)
                {
                    outError = "Cannot create generated ScriptCore mirror directory '" + generatedDirectory.string() + "': " + createDirectoriesError.message();
                    return false;
                }
            }

            for (const auto& entry : std::filesystem::recursive_directory_iterator(assetsRoot.value(), std::filesystem::directory_options::skip_permission_denied))
            {
                if (!entry.is_regular_file())
                    continue;
                if (entry.path().extension() != ".cpp")
                    continue;

                const std::filesystem::path sourceCppPath = entry.path();
                const std::filesystem::path sourceHeaderPath = sourceCppPath.parent_path() / (sourceCppPath.stem().string() + ".h");
                if (!std::filesystem::exists(sourceHeaderPath))
                    continue;

                std::error_code relativeError;
                const std::filesystem::path relativeCppPath = std::filesystem::relative(sourceCppPath, assetsRoot.value(), relativeError);
                if (relativeError || relativeCppPath.empty())
                    continue;

                const std::filesystem::path relativeHeaderPath = std::filesystem::relative(sourceHeaderPath, assetsRoot.value(), relativeError);
                if (relativeError || relativeHeaderPath.empty())
                    continue;

                for (const std::filesystem::path& generatedDirectory : generatedDirectories)
                {
                    const std::filesystem::path destinationCppPath = generatedDirectory / relativeCppPath;
                    const std::filesystem::path destinationHeaderPath = generatedDirectory / relativeHeaderPath;

                    std::filesystem::create_directories(destinationCppPath.parent_path(), createDirectoriesError);
                    if (createDirectoriesError)
                    {
                        outError = "Failed to create generated script directory '" + destinationCppPath.parent_path().string() + "': " + createDirectoriesError.message();
                        return false;
                    }

                    std::error_code copyError;
                    std::filesystem::copy_file(sourceCppPath, destinationCppPath, std::filesystem::copy_options::overwrite_existing, copyError);
                    if (copyError)
                    {
                        outError = "Failed to mirror source file '" + sourceCppPath.string() + "' to '" + destinationCppPath.string() + "': " + copyError.message();
                        return false;
                    }

                    std::filesystem::copy_file(sourceHeaderPath, destinationHeaderPath, std::filesystem::copy_options::overwrite_existing, copyError);
                    if (copyError)
                    {
                        outError = "Failed to mirror header file '" + sourceHeaderPath.string() + "' to '" + destinationHeaderPath.string() + "': " + copyError.message();
                        return false;
                    }
                }
            }

            outError.clear();
            return true;
        }

        bool TriggerNativeScriptsBuild(NativeScriptAuthoringState& state)
        {
            if (state.BuildInProgress.load(std::memory_order_relaxed))
                return false;

            std::string mirrorError;
            if (!MirrorAllProjectNativeScriptsToGeneratedDirectory(mirrorError))
            {
                state.StatusMessage = mirrorError;
                state.StatusIsError = true;
                return false;
            }

            const auto buildRoot = FindEngineWorkspaceRoot();
            if (!buildRoot.has_value())
            {
                state.StatusMessage = "Could not locate script build root. Configure Build Settings backend/toolchain root first.";
                state.StatusIsError = true;
                return false;
            }

            bool useInternalBackend = false;
            const auto openedProjectRoot = GetOpenedProjectRoot();
            if (openedProjectRoot.has_value())
            {
                const auto buildSettingsResult = Project::LoadBuildSettings(openedProjectRoot.value());
                if (buildSettingsResult.IsSuccess())
                    useInternalBackend = (buildSettingsResult.GetValue().BuildBackend == Project::BuildBackend::InternalToolchain);
            }

            if (useInternalBackend)
            {
                if (!IsInternalToolchainRootCandidate(buildRoot.value()))
                    useInternalBackend = false;
            }
            else if (IsInternalToolchainRootCandidate(buildRoot.value()))
            {
                useInternalBackend = true;
            }

            if (state.BuildThread && state.BuildThread->joinable())
                state.BuildThread->join();

            state.BuildInProgress.store(true, std::memory_order_relaxed);
            state.LastBuildExitCode.store(-1, std::memory_order_relaxed);
            {
                std::lock_guard<std::mutex> lock(state.LastBuildOutputMutex);
                state.LastBuildOutput.clear();
            }
            state.StatusMessage = "Building native scripts...";
            state.StatusIsError = false;

            const std::filesystem::path settingsRoot = openedProjectRoot.has_value()
                ? openedProjectRoot.value()
                : buildRoot.value();
            const auto [configuration, platform] = GetBuildConfigurationAndPlatform(settingsRoot);
            state.BuildThread = std::make_unique<std::thread>(
                [&state, root = buildRoot.value(), configuration, platform, useInternalBackend, openedProjectRoot]() {
                const std::filesystem::path effectiveProjectRoot = openedProjectRoot.has_value()
                    ? openedProjectRoot.value()
                    : root;
                std::string buildOutput;
                int exitCode = RunBuildScriptBlocking(root, effectiveProjectRoot, configuration, platform, openedProjectRoot.has_value(), useInternalBackend, buildOutput);
                if (exitCode == 0 && openedProjectRoot.has_value())
                {
                    const std::filesystem::path builtScriptCorePath = GetBuiltScriptCoreLibraryPath(root, configuration, platform);
                    const std::filesystem::path projectLocalOutputPath =
                        GetProjectLocalScriptCoreLibraryPath(openedProjectRoot.value(), configuration, platform);
                    if (!std::filesystem::exists(builtScriptCorePath))
                    {
                        buildOutput += "\nBuilt ScriptCore library not found at: " + builtScriptCorePath.string();
                        exitCode = 1;
                    }
                    else
                    {
                        std::error_code createDirectoriesError;
                        std::filesystem::create_directories(projectLocalOutputPath.parent_path(), createDirectoriesError);
                        if (createDirectoriesError)
                        {
                            buildOutput += "\nFailed creating project ScriptCore output directory: " + createDirectoriesError.message();
                            exitCode = 1;
                        }
                        else
                        {
                            std::error_code copyError;
                            std::filesystem::copy_file(
                                builtScriptCorePath,
                                projectLocalOutputPath,
                                std::filesystem::copy_options::overwrite_existing,
                                copyError);
                            if (copyError)
                            {
                                buildOutput += "\nFailed copying ScriptCore to project-local output: " + copyError.message();
                                exitCode = 1;
                            }
                        }
                    }
                }
                {
                    std::lock_guard<std::mutex> lock(state.LastBuildOutputMutex);
                    state.LastBuildOutput = std::move(buildOutput);
                }
                state.LastBuildExitCode.store(exitCode, std::memory_order_relaxed);
                state.BuildInProgress.store(false, std::memory_order_relaxed);
            });

            return true;
        }

        void DrawNativeScriptBuildToast(NativeScriptAuthoringState& state)
        {
            if (!state.BuildToastVisible)
                return;

            constexpr float kToastLifetimeSeconds = 4.0f;
            constexpr float kToastFadeOutSeconds = 0.4f;
            const auto now = std::chrono::steady_clock::now();
            const float elapsedSeconds = std::chrono::duration<float>(now - state.BuildToastShownAt).count();
            if (elapsedSeconds >= kToastLifetimeSeconds)
            {
                state.BuildToastVisible = false;
                return;
            }

            float alpha = 0.95f;
            if (elapsedSeconds > (kToastLifetimeSeconds - kToastFadeOutSeconds))
            {
                const float fadeProgress =
                    (elapsedSeconds - (kToastLifetimeSeconds - kToastFadeOutSeconds)) / kToastFadeOutSeconds;
                alpha = std::clamp(0.95f * (1.0f - fadeProgress), 0.0f, 0.95f);
            }

            ImGuiViewport* viewport = ImGui::GetMainViewport();
            if (viewport)
            {
                const ImVec2 windowPos(
                    viewport->Pos.x + viewport->Size.x - 14.0f,
                    viewport->Pos.y + 60.0f);
                ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always, ImVec2(1.0f, 0.0f));
            }
            ImGui::SetNextWindowBgAlpha(alpha);
            constexpr ImGuiWindowFlags toastFlags =
                ImGuiWindowFlags_NoDecoration |
                ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoFocusOnAppearing |
                ImGuiWindowFlags_NoNav;
            if (ImGui::Begin("##NativeScriptBuildToast", nullptr, toastFlags))
            {
                ImGui::TextUnformatted("Native Script Build");
                ImGui::Separator();
                const ImVec4 toastColor = state.BuildToastIsError
                    ? ImVec4(1.0f, 0.35f, 0.35f, 1.0f)
                    : ImVec4(0.35f, 1.0f, 0.45f, 1.0f);
                ImGui::TextColored(toastColor, "%s", state.BuildToastMessage.c_str());
            }
            ImGui::End();
        }

        void ConsumeFinishedNativeScriptBuildResult(NativeScriptAuthoringState& state, bool updateStatusMessage)
        {
            const int finishedBuildExitCode = state.LastBuildExitCode.exchange(-1, std::memory_order_relaxed);
            if (finishedBuildExitCode < 0)
                return;

            std::string finishedBuildOutput;
            {
                std::lock_guard<std::mutex> lock(state.LastBuildOutputMutex);
                finishedBuildOutput = state.LastBuildOutput;
            }

            if (finishedBuildExitCode == 0)
            {
                state.HasCompletedBuild = true;
                state.LastBuildSucceeded = true;
                state.LastCompletedBuildExitCode = 0;
                state.LastBuildSummary = "Native script build succeeded.";
                state.BuildToastVisible = true;
                state.BuildToastIsError = false;
                state.BuildToastMessage = "Native script build succeeded.";
                state.BuildToastShownAt = std::chrono::steady_clock::now();
                if (updateStatusMessage)
                {
                    state.StatusMessage = "Native script build succeeded.";
                    state.StatusIsError = false;
                }

                LT_INFO("Native scripts: build succeeded.");
                if (!finishedBuildOutput.empty())
                    LT_INFO("Native script build output:\n{}", finishedBuildOutput);
            }
            else
            {
                state.HasCompletedBuild = true;
                state.LastBuildSucceeded = false;
                state.LastCompletedBuildExitCode = finishedBuildExitCode;
                state.LastBuildSummary =
                    "Native script build failed (exit code " + std::to_string(finishedBuildExitCode) + ").";
                state.BuildToastVisible = true;
                state.BuildToastIsError = true;
                state.BuildToastMessage =
                    "Native script build failed (exit code " + std::to_string(finishedBuildExitCode) + ").";
                state.BuildToastShownAt = std::chrono::steady_clock::now();
                if (updateStatusMessage)
                {
                    state.StatusMessage =
                        "Native script build failed (exit code "
                        + std::to_string(finishedBuildExitCode)
                        + "). Check console or build output section below.";
                    state.StatusIsError = true;
                }

                if (!finishedBuildOutput.empty())
                    LT_ERROR("Native script build failed (exit code {}). Output:\n{}", finishedBuildExitCode, finishedBuildOutput);
                else
                    LT_ERROR("Native script build failed (exit code {}) with no build output captured.", finishedBuildExitCode);
            }
        }

        bool HasAnyProjectNativeScriptSources()
        {
            const auto assetsRoot = GetOpenedProjectAssetsRoot();
            if (!assetsRoot.has_value())
                return false;

            for (const auto& entry : std::filesystem::recursive_directory_iterator(assetsRoot.value(), std::filesystem::directory_options::skip_permission_denied))
            {
                if (!entry.is_regular_file())
                    continue;
                if (entry.path().extension() != ".cpp")
                    continue;

                const std::filesystem::path headerPath = entry.path().parent_path() / (entry.path().stem().string() + ".h");
                if (std::filesystem::exists(headerPath))
                    return true;
            }

            return false;
        }

        std::string NormalizeRelativeScriptPath(const std::filesystem::path& path)
        {
            const std::string normalized = path.generic_string();
            if (normalized == ".")
                return {};
            return normalized;
        }

        std::vector<ProjectNativeScriptInfo> DiscoverProjectNativeScriptsFromAssets()
        {
            std::vector<ProjectNativeScriptInfo> discoveredScripts;
            const auto assetsRoot = GetOpenedProjectAssetsRoot();
            if (!assetsRoot.has_value())
                return discoveredScripts;

            std::unordered_set<std::string> uniqueScriptAssetPaths;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(assetsRoot.value(), std::filesystem::directory_options::skip_permission_denied))
            {
                if (!entry.is_regular_file())
                    continue;
                if (entry.path().extension() != ".cpp")
                    continue;

                const std::filesystem::path headerPath = entry.path().parent_path() / (entry.path().stem().string() + ".h");
                if (!std::filesystem::exists(headerPath))
                    continue;

                std::error_code relativeError;
                std::filesystem::path relativePathWithoutExtension = std::filesystem::relative(entry.path(), assetsRoot.value(), relativeError);
                if (relativeError || relativePathWithoutExtension.empty())
                    continue;
                relativePathWithoutExtension.replace_extension();

                const std::string scriptAssetRelativePath = NormalizeRelativeScriptPath(relativePathWithoutExtension);
                if (scriptAssetRelativePath.empty() || !uniqueScriptAssetPaths.insert(scriptAssetRelativePath).second)
                    continue;

                ProjectNativeScriptInfo info{};
                info.ScriptClassName = entry.path().stem().string();
                info.ScriptAssetRelativePath = scriptAssetRelativePath;
                info.FolderRelativePath = NormalizeRelativeScriptPath(relativePathWithoutExtension.parent_path());
                info.DisplayName = relativePathWithoutExtension.filename().string();
                discoveredScripts.push_back(std::move(info));
            }

            std::sort(discoveredScripts.begin(), discoveredScripts.end(), [](const ProjectNativeScriptInfo& left, const ProjectNativeScriptInfo& right) {
                if (left.FolderRelativePath != right.FolderRelativePath)
                    return left.FolderRelativePath < right.FolderRelativePath;
                if (left.DisplayName != right.DisplayName)
                    return left.DisplayName < right.DisplayName;
                return left.ScriptAssetRelativePath < right.ScriptAssetRelativePath;
            });
            return discoveredScripts;
        }

        std::vector<std::string> DiscoverNativeScriptClassNamesFromProjectAssets()
        {
            std::vector<std::string> discoveredClassNames;
            std::unordered_set<std::string> uniqueClassNames;
            for (const auto& scriptInfo : DiscoverProjectNativeScriptsFromAssets())
            {
                if (!scriptInfo.ScriptClassName.empty() && uniqueClassNames.insert(scriptInfo.ScriptClassName).second)
                    discoveredClassNames.push_back(scriptInfo.ScriptClassName);
            }

            std::sort(discoveredClassNames.begin(), discoveredClassNames.end());
            return discoveredClassNames;
        }

        std::vector<ProjectNativeScriptInfo> BuildAvailableProjectScripts()
        {
            return DiscoverProjectNativeScriptsFromAssets();
        }

        std::vector<std::string> BuildAvailableProjectScriptClassNames()
        {
            std::vector<std::string> availableScriptClassNames;
            availableScriptClassNames.reserve(32);
            std::unordered_set<std::string> uniqueClassNames;
            for (const auto& scriptInfo : BuildAvailableProjectScripts())
            {
                if (!scriptInfo.ScriptClassName.empty() && uniqueClassNames.insert(scriptInfo.ScriptClassName).second)
                    availableScriptClassNames.push_back(scriptInfo.ScriptClassName);
            }
            std::sort(availableScriptClassNames.begin(), availableScriptClassNames.end());
            return availableScriptClassNames;
        }

        std::string GetUnqualifiedScriptClassName(std::string_view className)
        {
            const size_t separator = className.rfind("::");
            if (separator == std::string_view::npos)
                return std::string(className);
            return std::string(className.substr(separator + 2));
        }

        std::string ResolveRegisteredScriptClassName(const std::string& requestedClassName)
        {
            if (requestedClassName.empty())
                return {};
            if (NativeScriptRegistry::HasScript(requestedClassName))
                return requestedClassName;

            const auto registeredScriptNames = NativeScriptRegistry::GetRegisteredScriptNames();
            std::string matchedClassName;
            for (const std::string& candidate : registeredScriptNames)
            {
                if (candidate == requestedClassName)
                    return candidate;

                if (GetUnqualifiedScriptClassName(candidate) != requestedClassName)
                    continue;

                if (!matchedClassName.empty())
                    return {};
                matchedClassName = candidate;
            }

            return matchedClassName;
        }

        std::string SanitizeNativeScriptClassName(const char* rawName)
        {
            std::string className = rawName ? rawName : "";
            className.erase(std::remove_if(className.begin(), className.end(), [](unsigned char character) {
                return std::isspace(character) != 0;
            }), className.end());

            std::string sanitized;
            sanitized.reserve(className.size() + 8);
            for (char character : className)
            {
                const unsigned char value = static_cast<unsigned char>(character);
                if (std::isalnum(value) || character == '_')
                    sanitized.push_back(character);
            }

            if (sanitized.empty())
                sanitized = "NewNativeScript";
            if (std::isdigit(static_cast<unsigned char>(sanitized.front())))
                sanitized.insert(0, "Script_");
            return sanitized;
        }

        std::string SanitizeRelativeAssetDirectory(const char* rawPath)
        {
            std::string relativePath = rawPath ? rawPath : "";
            std::replace(relativePath.begin(), relativePath.end(), '\\', '/');

            std::string sanitized;
            sanitized.reserve(relativePath.size());
            for (char character : relativePath)
            {
                const unsigned char value = static_cast<unsigned char>(character);
                if (std::isalnum(value) || character == '_' || character == '-' || character == '/')
                    sanitized.push_back(character);
            }

            while (!sanitized.empty() && (sanitized.front() == '/' || sanitized.front() == '.'))
                sanitized.erase(sanitized.begin());
            while (!sanitized.empty() && sanitized.back() == '/')
                sanitized.pop_back();

            if (sanitized.empty())
                sanitized = "Scripts";
            return sanitized;
        }

        bool LoadTextFileIntoBuffer(const std::filesystem::path& path, std::array<char, kNativeScriptEditorBufferSize>& buffer, std::string& outError)
        {
            std::ifstream input(path, std::ios::in | std::ios::binary);
            if (!input.is_open())
            {
                outError = "Failed to open file: " + path.string();
                return false;
            }

            const std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
            if (content.size() >= buffer.size())
            {
                outError = "File is too large for editor buffer: " + path.string();
                return false;
            }

            std::fill(buffer.begin(), buffer.end(), '\0');
            std::memcpy(buffer.data(), content.data(), content.size());
            return true;
        }

        bool SaveBufferToTextFile(const std::filesystem::path& path, const std::array<char, kNativeScriptEditorBufferSize>& buffer, std::string& outError)
        {
            std::ofstream output(path, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!output.is_open())
            {
                outError = "Failed to open file for writing: " + path.string();
                return false;
            }

            output << buffer.data();
            if (!output.good())
            {
                outError = "Failed to write file: " + path.string();
                return false;
            }

            return true;
        }

        struct ScriptPublicFieldDefinition final
        {
            std::string Name;
            ScriptPropertyValue DefaultValue;
        };

        std::string TrimString(const std::string& value)
        {
            size_t start = 0;
            while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0)
                ++start;

            size_t end = value.size();
            while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0)
                --end;

            return value.substr(start, end - start);
        }

        bool TryParseFloatLiteral(const std::string& rawValue, float& outValue)
        {
            std::string value = TrimString(rawValue);
            if (!value.empty() && (value.back() == 'f' || value.back() == 'F'))
                value.pop_back();
            if (value.empty())
                return false;

            char* parseEnd = nullptr;
            const float parsedValue = std::strtof(value.c_str(), &parseEnd);
            if (parseEnd == value.c_str() || *parseEnd != '\0')
                return false;
            outValue = parsedValue;
            return true;
        }

        bool TryParseIntegerLiteral(const std::string& rawValue, int32_t& outValue)
        {
            const std::string value = TrimString(rawValue);
            if (value.empty())
                return false;

            char* parseEnd = nullptr;
            const long parsedValue = std::strtol(value.c_str(), &parseEnd, 10);
            if (parseEnd == value.c_str() || *parseEnd != '\0')
                return false;
            outValue = static_cast<int32_t>(parsedValue);
            return true;
        }

        bool TryParseVector3Literal(const std::string& rawValue, glm::vec3& outValue)
        {
            const std::regex numberPattern(R"([+-]?\d*\.?\d+(?:[eE][+-]?\d+)?)");
            std::vector<float> values;
            for (std::sregex_iterator iterator(rawValue.begin(), rawValue.end(), numberPattern), end; iterator != end; ++iterator)
            {
                float parsedValue = 0.0f;
                if (!TryParseFloatLiteral(iterator->str(), parsedValue))
                    continue;
                values.push_back(parsedValue);
                if (values.size() == 3)
                    break;
            }

            if (values.size() != 3)
                return false;

            outValue = glm::vec3(values[0], values[1], values[2]);
            return true;
        }

        bool TryBuildDefaultFieldValue(const std::string& typeName,
                                       const std::optional<std::string>& rawInitializer,
                                       ScriptPropertyValue& outValue)
        {
            const std::string initializer = rawInitializer.has_value() ? TrimString(rawInitializer.value()) : std::string();

            if (typeName == "float")
            {
                float value = 0.0f;
                if (!initializer.empty() && !TryParseFloatLiteral(initializer, value))
                    return false;
                outValue = value;
                return true;
            }
            if (typeName == "int" || typeName == "int32_t")
            {
                int32_t value = 0;
                if (!initializer.empty() && !TryParseIntegerLiteral(initializer, value))
                    return false;
                outValue = value;
                return true;
            }
            if (typeName == "bool")
            {
                bool value = false;
                if (!initializer.empty())
                {
                    if (initializer == "true")
                        value = true;
                    else if (initializer == "false")
                        value = false;
                    else
                        return false;
                }
                outValue = value;
                return true;
            }
            if (typeName == "glm::vec3")
            {
                glm::vec3 value(0.0f);
                if (!initializer.empty() && !TryParseVector3Literal(initializer, value))
                    return false;
                outValue = value;
                return true;
            }
            if (typeName == "std::string")
            {
                std::string value;
                if (!initializer.empty())
                {
                    if (initializer.size() < 2 || initializer.front() != '"' || initializer.back() != '"')
                        return false;
                    value = initializer.substr(1, initializer.size() - 2);
                }
                outValue = value;
                return true;
            }
            if (typeName == "Limitless::Entity" || typeName == "Entity")
            {
                ScriptEntityReference value{};
                if (!initializer.empty())
                {
                    if (initializer == "{}" ||
                        initializer == "Entity{}" ||
                        initializer == "Limitless::Entity{}" ||
                        initializer == "Entity()" ||
                        initializer == "Limitless::Entity()")
                    {
                        value.Tag.clear();
                    }
                    else if (initializer.size() >= 2 && initializer.front() == '"' && initializer.back() == '"')
                    {
                        const std::string parsedValue = initializer.substr(1, initializer.size() - 2);
                        if (LooksLikePrefabAssetKey(parsedValue))
                            value.PrefabAssetKey = parsedValue;
                        else
                            value.Tag = parsedValue;
                    }
                    else
                    {
                        return false;
                    }
                }
                outValue = value;
                return true;
            }
            if (typeName == "Limitless::Prefab" ||
                typeName == "Prefab" ||
                typeName == "Limitless::ScriptPrefabReference" ||
                typeName == "ScriptPrefabReference")
            {
                Prefab value{};
                if (!initializer.empty())
                {
                    if (initializer == "{}" ||
                        initializer == "Prefab{}" ||
                        initializer == "Limitless::Prefab{}" ||
                        initializer == "Prefab()" ||
                        initializer == "Limitless::Prefab()" ||
                        initializer == "ScriptPrefabReference{}" ||
                        initializer == "Limitless::ScriptPrefabReference{}" ||
                        initializer == "ScriptPrefabReference()" ||
                        initializer == "Limitless::ScriptPrefabReference()")
                    {
                        value.AssetKey.clear();
                    }
                    else if (initializer.size() >= 2 && initializer.front() == '"' && initializer.back() == '"')
                    {
                        value.AssetKey = initializer.substr(1, initializer.size() - 2);
                    }
                    else
                    {
                        return false;
                    }
                }
                outValue = std::move(value);
                return true;
            }

            return false;
        }

        bool ParsePublicScriptFieldsFromHeader(const std::filesystem::path& headerPath,
                                               std::vector<ScriptPublicFieldDefinition>& outFields,
                                               std::string& outError)
        {
            std::ifstream input(headerPath, std::ios::in | std::ios::binary);
            if (!input.is_open())
            {
                outError = "Failed to open script header: " + headerPath.string();
                return false;
            }

            const std::regex fieldPattern(
                R"(^\s*(?:const\s+)?(?:static\s+)?(float|int32_t|int|bool|glm::vec3|std::string|Limitless::Entity|Entity|Limitless::Prefab|Prefab|Limitless::ScriptPrefabReference|ScriptPrefabReference)\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:=\s*([^;]+))?\s*;\s*$)");

            bool insidePublicSection = false;
            std::string line;
            while (std::getline(input, line))
            {
                const size_t commentIndex = line.find("//");
                const std::string content = TrimString(commentIndex == std::string::npos ? line : line.substr(0, commentIndex));
                if (content.empty())
                    continue;

                if (content == "public:")
                {
                    insidePublicSection = true;
                    continue;
                }
                if (content == "private:" || content == "protected:")
                {
                    insidePublicSection = false;
                    continue;
                }

                if (!insidePublicSection)
                    continue;
                if (content.find('(') != std::string::npos)
                    continue;

                std::smatch fieldMatch;
                if (!std::regex_match(content, fieldMatch, fieldPattern))
                    continue;

                ScriptPublicFieldDefinition fieldDefinition;
                fieldDefinition.Name = fieldMatch[2].str();

                std::optional<std::string> initializer;
                if (fieldMatch[3].matched)
                    initializer = fieldMatch[3].str();

                if (!TryBuildDefaultFieldValue(fieldMatch[1].str(), initializer, fieldDefinition.DefaultValue))
                    continue;

                outFields.push_back(std::move(fieldDefinition));
            }

            outError.clear();
            return true;
        }

        bool ParseLegacyExposedFieldsFromSource(const std::filesystem::path& sourcePath,
                                                std::vector<ScriptPublicFieldDefinition>& outFields,
                                                std::string& outError)
        {
            std::ifstream input(sourcePath, std::ios::in | std::ios::binary);
            if (!input.is_open())
            {
                outError = "Failed to open script source: " + sourcePath.string();
                return false;
            }

            const std::regex callPattern(
                R"LT(GetExposed(Float|Integer|Boolean|Vector3|String|Entity|Prefab)\s*\(\s*"([A-Za-z_][A-Za-z0-9_]*)"\s*,\s*([^)]+)\))LT");

            std::unordered_set<std::string> existingNames;
            for (const auto& existingField : outFields)
                existingNames.insert(existingField.Name);

            std::string line;
            while (std::getline(input, line))
            {
                std::smatch callMatch;
                if (!std::regex_search(line, callMatch, callPattern))
                    continue;

                const std::string functionSuffix = callMatch[1].str();
                const std::string propertyName = callMatch[2].str();
                const std::string fallbackExpression = TrimString(callMatch[3].str());

                if (existingNames.find(propertyName) != existingNames.end())
                    continue;

                ScriptPublicFieldDefinition fieldDefinition;
                fieldDefinition.Name = propertyName;

                bool parsed = false;
                if (functionSuffix == "Float")
                {
                    float value = 0.0f;
                    parsed = TryParseFloatLiteral(fallbackExpression, value);
                    if (parsed)
                        fieldDefinition.DefaultValue = value;
                }
                else if (functionSuffix == "Integer")
                {
                    int32_t value = 0;
                    parsed = TryParseIntegerLiteral(fallbackExpression, value);
                    if (parsed)
                        fieldDefinition.DefaultValue = value;
                }
                else if (functionSuffix == "Boolean")
                {
                    if (fallbackExpression == "true")
                    {
                        fieldDefinition.DefaultValue = true;
                        parsed = true;
                    }
                    else if (fallbackExpression == "false")
                    {
                        fieldDefinition.DefaultValue = false;
                        parsed = true;
                    }
                }
                else if (functionSuffix == "Vector3")
                {
                    glm::vec3 value(0.0f);
                    parsed = TryParseVector3Literal(fallbackExpression, value);
                    if (parsed)
                        fieldDefinition.DefaultValue = value;
                }
                else if (functionSuffix == "String")
                {
                    if (fallbackExpression.size() >= 2 && fallbackExpression.front() == '"' && fallbackExpression.back() == '"')
                    {
                        fieldDefinition.DefaultValue = fallbackExpression.substr(1, fallbackExpression.size() - 2);
                        parsed = true;
                    }
                }
                else if (functionSuffix == "Entity")
                {
                    ScriptEntityReference value{};
                    if (fallbackExpression == "{}" || fallbackExpression == "Entity{}" || fallbackExpression == "Limitless::Entity{}")
                    {
                        parsed = true;
                    }
                    else if (fallbackExpression.size() >= 2 && fallbackExpression.front() == '"' && fallbackExpression.back() == '"')
                    {
                        const std::string parsedValue = fallbackExpression.substr(1, fallbackExpression.size() - 2);
                        if (LooksLikePrefabAssetKey(parsedValue))
                            value.PrefabAssetKey = parsedValue;
                        else
                            value.Tag = parsedValue;
                        parsed = true;
                    }

                    if (parsed)
                        fieldDefinition.DefaultValue = std::move(value);
                }
                else if (functionSuffix == "Prefab")
                {
                    Prefab value{};
                    if (fallbackExpression == "{}" ||
                        fallbackExpression == "Prefab{}" ||
                        fallbackExpression == "Limitless::Prefab{}" ||
                        fallbackExpression == "ScriptPrefabReference{}" ||
                        fallbackExpression == "Limitless::ScriptPrefabReference{}")
                    {
                        parsed = true;
                    }
                    else if (fallbackExpression.size() >= 2 && fallbackExpression.front() == '"' && fallbackExpression.back() == '"')
                    {
                        value.AssetKey = fallbackExpression.substr(1, fallbackExpression.size() - 2);
                        parsed = true;
                    }

                    if (parsed)
                        fieldDefinition.DefaultValue = std::move(value);
                }

                if (!parsed)
                    continue;

                outFields.push_back(std::move(fieldDefinition));
                existingNames.insert(propertyName);
            }

            outError.clear();
            return true;
        }

        bool ResolveNativeScriptFilePaths(const std::string& className,
                                          const std::string& preferredAssetRelativePath,
                                          std::filesystem::path& outHeaderPath,
                                          std::filesystem::path& outSourcePath)
        {
            const auto authoringDirectory = GetAuthoringNativeScriptsDirectory();

            auto tryDirectory = [&](const std::optional<std::filesystem::path>& directory) {
                if (!directory.has_value())
                    return false;
                const std::filesystem::path candidateHeaderPath = directory.value() / (className + ".h");
                const std::filesystem::path candidateSourcePath = directory.value() / (className + ".cpp");
                if (std::filesystem::exists(candidateHeaderPath) && std::filesystem::exists(candidateSourcePath))
                {
                    outHeaderPath = candidateHeaderPath;
                    outSourcePath = candidateSourcePath;
                    return true;
                }
                return false;
            };

            if (!preferredAssetRelativePath.empty())
            {
                if (const auto assetsRoot = GetOpenedProjectAssetsRoot(); assetsRoot.has_value())
                {
                    const std::filesystem::path preferredSourceRoot = assetsRoot.value() / preferredAssetRelativePath;
                    const std::filesystem::path preferredHeaderFile = preferredSourceRoot.string() + ".h";
                    const std::filesystem::path preferredSourceFile = preferredSourceRoot.string() + ".cpp";
                    if (std::filesystem::exists(preferredHeaderFile) && std::filesystem::exists(preferredSourceFile))
                    {
                        outHeaderPath = preferredHeaderFile;
                        outSourcePath = preferredSourceFile;
                        return true;
                    }
                }
            }

            if (tryDirectory(authoringDirectory))
                return true;

            // Recursive fallback: search entire Assets tree for matching .h/.cpp pairs.
            if (const auto assetsRoot = GetOpenedProjectAssetsRoot(); assetsRoot.has_value())
            {
                std::error_code ec;
                for (const auto& entry : std::filesystem::recursive_directory_iterator(
                         assetsRoot.value(), std::filesystem::directory_options::skip_permission_denied, ec))
                {
                    if (!entry.is_regular_file())
                        continue;
                    if (entry.path().stem().string() != className)
                        continue;
                    if (entry.path().extension() != ".h")
                        continue;

                    const std::filesystem::path candidateSourcePath =
                        entry.path().parent_path() / (className + ".cpp");
                    if (std::filesystem::exists(candidateSourcePath))
                    {
                        outHeaderPath = entry.path();
                        outSourcePath = candidateSourcePath;
                        return true;
                    }
                }
            }

            return false;
        }

        bool SynchronizeExposedPropertiesFromScript(NativeScriptEntry& nativeScript,
                                                    std::vector<std::string>& outOrderedFieldNames,
                                                    std::string& outError)
        {
            outOrderedFieldNames.clear();
            outError.clear();

            if (nativeScript.ScriptClassName.empty())
                return true;

            std::filesystem::path headerPath;
            std::filesystem::path sourcePath;
            if (!ResolveNativeScriptFilePaths(nativeScript.ScriptClassName, nativeScript.ScriptAssetRelativePath, headerPath, sourcePath))
            {
                outError = "Script files not found for class '" + nativeScript.ScriptClassName + "'.";
                return false;
            }

            std::vector<ScriptPublicFieldDefinition> fields;
            if (!ParsePublicScriptFieldsFromHeader(headerPath, fields, outError))
                return false;

            if (fields.empty())
            {
                (void)ParseLegacyExposedFieldsFromSource(sourcePath, fields, outError);
            }

            std::unordered_set<std::string> declaredFieldNames;
            declaredFieldNames.reserve(fields.size());
            for (const auto& field : fields)
            {
                declaredFieldNames.insert(field.Name);
                outOrderedFieldNames.push_back(field.Name);

                const auto found = nativeScript.ExposedProperties.find(field.Name);
                if (found == nativeScript.ExposedProperties.end())
                {
                    nativeScript.ExposedProperties.emplace(field.Name, field.DefaultValue);
                    continue;
                }

                if (found->second.index() != field.DefaultValue.index())
                {
                    if (std::holds_alternative<ScriptEntityReference>(field.DefaultValue))
                    {
                        if (const auto* legacyPrefab = std::get_if<Prefab>(&found->second))
                        {
                            ScriptEntityReference migratedReference{};
                            migratedReference.PrefabAssetKey = legacyPrefab->AssetKey;
                            found->second = std::move(migratedReference);
                            continue;
                        }
                    }
                    else if (std::holds_alternative<Prefab>(field.DefaultValue))
                    {
                        if (const auto* entityReference = std::get_if<ScriptEntityReference>(&found->second))
                        {
                            Prefab migratedReference{};
                            migratedReference.AssetKey = entityReference->PrefabAssetKey;
                            found->second = std::move(migratedReference);
                            continue;
                        }
                    }

                    found->second = field.DefaultValue;
                }
            }

            // Keep undeclared/unsupported properties so authoring data is not dropped
            // during automatic inspector synchronization.

            return true;
        }

        bool OpenNativeScriptEditor(const std::string& className,
                                    const std::string& preferredAssetRelativePath,
                                    NativeScriptAuthoringState& state,
                                    std::string& outError)
        {
            std::filesystem::path headerPath;
            std::filesystem::path sourcePath;
            if (!ResolveNativeScriptFilePaths(className, preferredAssetRelativePath, headerPath, sourcePath))
            {
                outError = "Script files do not exist for class '" + className + "'.";
                return false;
            }

            if (!LoadTextFileIntoBuffer(headerPath, state.HeaderBuffer, outError))
                return false;
            if (!LoadTextFileIntoBuffer(sourcePath, state.SourceBuffer, outError))
                return false;

            state.ClassName = className;
            state.AssetRelativePath = preferredAssetRelativePath;
            state.HeaderPath = headerPath;
            state.SourcePath = sourcePath;
            state.HeaderDirty = false;
            state.SourceDirty = false;
            state.EditorWindowOpen = true;
            state.FocusEditorWindowRequested = true;
            state.StatusMessage = "Editing script: " + className;
            state.StatusIsError = false;
            return true;
        }

        bool MirrorScriptToGeneratedDirectory(const NativeScriptAuthoringState& state, std::string& outError)
        {
            std::vector<std::filesystem::path> generatedDirectories = GetGeneratedScriptCoreMirrorDirectories();
            if (generatedDirectories.empty())
            {
                if (const auto inferredProjectRoot = InferProjectRootFromScriptSourcePath(state.SourcePath); inferredProjectRoot.has_value())
                    generatedDirectories.push_back(inferredProjectRoot.value() / "Build" / "Generated" / "ScriptCore");
            }

            if (generatedDirectories.empty())
            {
                outError = "Could not locate generated ScriptCore mirror directory.";
                return true;
            }

            std::filesystem::path relativeMirrorPathWithoutExtension = state.ClassName;
            if (const auto assetsRoot = GetOpenedProjectAssetsRoot(); assetsRoot.has_value())
            {
                std::error_code relativeError;
                const std::filesystem::path scriptRelativePath =
                    std::filesystem::relative(state.SourcePath, assetsRoot.value(), relativeError);
                if (!relativeError && !scriptRelativePath.empty())
                {
                    relativeMirrorPathWithoutExtension = scriptRelativePath;
                    relativeMirrorPathWithoutExtension.replace_extension("");
                }
            }

            std::error_code createDirectoriesError;
            for (const std::filesystem::path& generatedDirectory : generatedDirectories)
            {
                const std::filesystem::path generatedHeaderPath = generatedDirectory / relativeMirrorPathWithoutExtension;
                const std::filesystem::path generatedSourcePath = generatedDirectory / relativeMirrorPathWithoutExtension;
                const std::filesystem::path generatedHeaderFile = generatedHeaderPath.string() + ".h";
                const std::filesystem::path generatedSourceFile = generatedSourcePath.string() + ".cpp";

                std::filesystem::create_directories(generatedHeaderFile.parent_path(), createDirectoriesError);
                if (createDirectoriesError)
                {
                    outError = "Failed to create generated ScriptCore mirror directory '" + generatedHeaderFile.parent_path().string() + "': " + createDirectoriesError.message();
                    return false;
                }

                if (!SaveBufferToTextFile(generatedHeaderFile, state.HeaderBuffer, outError))
                    return false;
                if (!SaveBufferToTextFile(generatedSourceFile, state.SourceBuffer, outError))
                    return false;
            }
            return true;
        }

        void DrawNativeScriptEditorWindowImpl(NativeScriptAuthoringState& state)
        {
            ConsumeFinishedNativeScriptBuildResult(state, state.EditorWindowOpen);
            DrawNativeScriptBuildToast(state);
            if (!state.EditorWindowOpen)
                return;

            if (state.FocusEditorWindowRequested)
                ImGui::SetNextWindowFocus();
            const bool hasUnsavedChanges = state.HeaderDirty || state.SourceDirty;
            const std::string windowTitle = hasUnsavedChanges
                ? "Native Script Editor*"
                : "Native Script Editor";
            EditorPanelStyle::PushPanelVisualStyle();
            if (!ImGui::Begin(windowTitle.c_str(), &state.EditorWindowOpen))
            {
                state.FocusEditorWindowRequested = false;
                ImGui::End();
                EditorPanelStyle::PopPanelVisualStyle();
                return;
            }
            if (state.FocusEditorWindowRequested)
            {
                ImGui::SetWindowFocus();
                state.FocusEditorWindowRequested = false;
            }

            const auto saveEditorFiles = [&](bool forceBuildAfterSave) -> bool
            {
                std::string saveError;
                const bool headerSaved = SaveBufferToTextFile(state.HeaderPath, state.HeaderBuffer, saveError);
                const bool sourceSaved = SaveBufferToTextFile(state.SourcePath, state.SourceBuffer, saveError);
                if (!(headerSaved && sourceSaved))
                {
                    state.StatusMessage = saveError;
                    state.StatusIsError = true;
                    return false;
                }

                state.HeaderDirty = false;
                state.SourceDirty = false;

                const bool mirrorSucceeded = MirrorScriptToGeneratedDirectory(state, saveError);
                if (!mirrorSucceeded)
                {
                    state.StatusMessage = saveError;
                    state.StatusIsError = true;
                }
                else
                {
                    if (!saveError.empty())
                    {
                        state.StatusMessage = "Script files saved. " + saveError + " Build will mirror all scripts.";
                        state.StatusIsError = false;
                    }
                    else
                    {
                        state.StatusMessage = "Script files saved and mirrored to generated build directory.";
                        state.StatusIsError = false;
                    }
                }

                const bool shouldBuildNow = (forceBuildAfterSave || state.AutoBuildAfterSave);
                if (shouldBuildNow)
                    (void)TriggerNativeScriptsBuild(state);

                return mirrorSucceeded;
            };

            const auto reloadEditorFiles = [&]() -> bool
            {
                std::string reloadError;
                const bool headerLoaded = LoadTextFileIntoBuffer(state.HeaderPath, state.HeaderBuffer, reloadError);
                const bool sourceLoaded = LoadTextFileIntoBuffer(state.SourcePath, state.SourceBuffer, reloadError);
                if (headerLoaded && sourceLoaded)
                {
                    state.HeaderDirty = false;
                    state.SourceDirty = false;
                    state.StatusMessage = "Reloaded script files from disk.";
                    state.StatusIsError = false;
                    return true;
                }

                state.StatusMessage = reloadError;
                state.StatusIsError = true;
                return false;
            };

            const ImGuiIO& io = ImGui::GetIO();
            const bool shortcutModifierDown = io.KeyCtrl || io.KeySuper;
            const bool editorWindowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            const bool saveShortcutPressed = editorWindowFocused && shortcutModifierDown && ImGui::IsKeyPressed(ImGuiKey_S, false);
            const bool buildShortcutPressed = editorWindowFocused && shortcutModifierDown && ImGui::IsKeyPressed(ImGuiKey_B, false);
            const bool reloadShortcutPressed = editorWindowFocused && shortcutModifierDown && ImGui::IsKeyPressed(ImGuiKey_R, false);

            ImGui::Text("Class: %s", state.ClassName.c_str());
            ImGui::Text("Header: %s", state.HeaderPath.string().c_str());
            ImGui::Text("Source: %s", state.SourcePath.string().c_str());
            if (const auto generatedDirectory = GetGeneratedScriptCoreMirrorDirectory(); generatedDirectory.has_value())
            {
                std::filesystem::path mirrorPath = generatedDirectory.value() / (state.ClassName + ".cpp");
                if (const auto assetsRoot = GetOpenedProjectAssetsRoot(); assetsRoot.has_value())
                {
                    std::error_code relativeError;
                    const std::filesystem::path relativeSourcePath =
                        std::filesystem::relative(state.SourcePath, assetsRoot.value(), relativeError);
                    if (!relativeError && !relativeSourcePath.empty())
                        mirrorPath = generatedDirectory.value() / relativeSourcePath;
                }
                ImGui::Text("Generated Mirror: %s", mirrorPath.string().c_str());
            }
            ImGui::TextWrapped("After creating or editing scripts, run the build script to compile and register script classes.");
            ImGui::TextDisabled("Shortcuts: Ctrl+S Save, Ctrl+B Save+Build, Ctrl+R Reload");
            ImGui::TextUnformatted("Auto Build On Save");
            ImGui::Checkbox("##AutoBuildOnSave", &state.AutoBuildAfterSave);
            ImGui::SameLine();
            ImGui::TextDisabled("Unsaved: %s", hasUnsavedChanges ? "Yes" : "No");
            if (state.BuildInProgress.load(std::memory_order_relaxed))
                ImGui::TextDisabled("Build in progress...");

            if (ImGui::Button("Save Files", ImVec2(140.0f, 0.0f)))
                (void)saveEditorFiles(false);
            ImGui::SameLine();
            if (ImGui::Button("Reload From Disk", ImVec2(160.0f, 0.0f)))
                (void)reloadEditorFiles();
            ImGui::SameLine();
            ImGui::BeginDisabled(state.BuildInProgress.load(std::memory_order_relaxed));
            if (ImGui::Button("Save + Build", ImVec2(150.0f, 0.0f)))
                (void)saveEditorFiles(true);
            ImGui::EndDisabled();

            if (saveShortcutPressed)
                (void)saveEditorFiles(false);
            if (reloadShortcutPressed)
                (void)reloadEditorFiles();
            if (!state.BuildInProgress.load(std::memory_order_relaxed) && buildShortcutPressed)
                (void)saveEditorFiles(true);

            if (!state.StatusMessage.empty())
            {
                const ImVec4 statusColor = state.StatusIsError
                    ? ImVec4(1.0f, 0.35f, 0.35f, 1.0f)
                    : ImVec4(0.35f, 1.0f, 0.45f, 1.0f);
                ImGui::TextColored(statusColor, "%s", state.StatusMessage.c_str());
            }

            std::string lastBuildOutputSnapshot;
            {
                std::lock_guard<std::mutex> lock(state.LastBuildOutputMutex);
                lastBuildOutputSnapshot = state.LastBuildOutput;
            }
            if (!lastBuildOutputSnapshot.empty() &&
                ImGui::CollapsingHeader("Last Native Script Build Output", ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (ImGui::Button("Copy Build Output", ImVec2(160.0f, 0.0f)))
                    ImGui::SetClipboardText(lastBuildOutputSnapshot.c_str());

                ImGui::BeginChild("NativeScriptBuildOutput", ImVec2(0.0f, 160.0f), true);
                ImGui::TextUnformatted(lastBuildOutputSnapshot.c_str());
                ImGui::EndChild();
            }

            ImGui::Separator();
            if (ImGui::BeginTabBar("NativeScriptEditorTabs"))
            {
                ImGuiTabItemFlags headerTabFlags = ImGuiTabItemFlags_None;
                ImGuiTabItemFlags sourceTabFlags = ImGuiTabItemFlags_None;
                if (state.SelectHeaderTabRequested)
                    headerTabFlags |= ImGuiTabItemFlags_SetSelected;
                else if (state.SelectSourceTabRequested)
                    sourceTabFlags |= ImGuiTabItemFlags_SetSelected;

                const std::string headerTabLabel = state.HeaderDirty ? "Header (.h)*" : "Header (.h)";
                const std::string sourceTabLabel = state.SourceDirty ? "Source (.cpp)*" : "Source (.cpp)";
                const float editorHeight = std::max(240.0f, ImGui::GetContentRegionAvail().y - 12.0f);

                if (ImGui::BeginTabItem(headerTabLabel.c_str(), nullptr, headerTabFlags))
                {
                    ImGui::InputTextMultiline("##NativeScriptHeaderEditor",
                                              state.HeaderBuffer.data(),
                                              state.HeaderBuffer.size(),
                                              ImVec2(-1.0f, editorHeight),
                                              ImGuiInputTextFlags_AllowTabInput);
                    if (ImGui::IsItemEdited())
                        state.HeaderDirty = true;
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem(sourceTabLabel.c_str(), nullptr, sourceTabFlags))
                {
                    ImGui::InputTextMultiline("##NativeScriptSourceEditor",
                                              state.SourceBuffer.data(),
                                              state.SourceBuffer.size(),
                                              ImVec2(-1.0f, editorHeight),
                                              ImGuiInputTextFlags_AllowTabInput);
                    if (ImGui::IsItemEdited())
                        state.SourceDirty = true;
                    ImGui::EndTabItem();
                }
                state.SelectHeaderTabRequested = false;
                state.SelectSourceTabRequested = false;
                ImGui::EndTabBar();
            }

            ImGui::End();
            EditorPanelStyle::PopPanelVisualStyle();
        }
    }

    void RestorePendingNativeScriptEditorSession()
    {
        if (!s_HasPendingNativeScriptEditorSessionRestore)
            return;

        auto& state = GetNativeScriptAuthoringState();
        const auto pendingState = s_PendingNativeScriptEditorSessionState;
        s_HasPendingNativeScriptEditorSessionRestore = false;
        s_PendingNativeScriptEditorSessionState = {};
        state.ShowDebugInfo = pendingState.ShowDebugInfo;

        if (pendingState.IsOpen && !pendingState.LastEditedScriptClassName.empty())
        {
            std::string openError;
            if (!OpenNativeScriptEditor(
                pendingState.LastEditedScriptClassName,
                pendingState.LastEditedScriptAssetRelativePath,
                state,
                openError))
            {
                state.StatusMessage = openError;
                state.StatusIsError = true;
                state.EditorWindowOpen = true;
                state.FocusEditorWindowRequested = true;
            }
        }
        else
        {
            state.EditorWindowOpen = false;
        }
    }

    void DrawNativeScriptEditorWindow()
    {
        DrawNativeScriptEditorWindowImpl(GetNativeScriptAuthoringState());
    }

    bool IsNativeScriptBuildInProgress()
    {
        return GetNativeScriptAuthoringState().BuildInProgress.load(std::memory_order_relaxed);
    }

    bool TriggerNativeScriptBuildFromInspector()
    {
        return TriggerNativeScriptsBuild(GetNativeScriptAuthoringState());
    }

    bool HasAnyProjectNativeScriptSourcesForInspector()
    {
        return HasAnyProjectNativeScriptSources();
    }

    std::vector<std::string> DiscoverNativeScriptClassNamesFromProjectAssetsForInspector()
    {
        return DiscoverNativeScriptClassNamesFromProjectAssets();
    }

    std::vector<ProjectNativeScriptInfo> GetAvailableProjectScriptsForInspector()
    {
        return BuildAvailableProjectScripts();
    }

    std::vector<std::string> GetAvailableProjectScriptClassNamesForInspector()
    {
        return BuildAvailableProjectScriptClassNames();
    }

    std::string ResolveRegisteredScriptClassNameForInspector(const std::string& requestedClassName)
    {
        return ResolveRegisteredScriptClassName(requestedClassName);
    }

    bool SynchronizeExposedPropertiesFromScriptForInspector(NativeScriptEntry& nativeScript,
                                                            std::vector<std::string>& outFieldOrder,
                                                            std::string& outError)
    {
        return SynchronizeExposedPropertiesFromScript(nativeScript, outFieldOrder, outError);
    }

    bool GetNativeScriptDebugInfoEnabled()
    {
        return GetNativeScriptAuthoringState().ShowDebugInfo;
    }

    void SetNativeScriptDebugInfoEnabled(bool enabled)
    {
        GetNativeScriptAuthoringState().ShowDebugInfo = enabled;
    }

    void GetNativeScriptEditorSessionState(NativeScriptEditorSessionState& outState)
    {
        const auto& state = GetNativeScriptAuthoringState();
        outState.IsOpen = state.EditorWindowOpen;
        outState.LastEditedScriptClassName = state.ClassName;
        outState.LastEditedScriptAssetRelativePath = state.AssetRelativePath;
        outState.ShowDebugInfo = state.ShowDebugInfo;
    }

    void ApplyNativeScriptEditorSessionState(const NativeScriptEditorSessionState& state)
    {
        s_PendingNativeScriptEditorSessionState = state;
        s_HasPendingNativeScriptEditorSessionRestore = true;
    }

    bool OpenNativeScriptEditorForAssetKey(const std::string& assetKey)
    {
        const std::filesystem::path assetPath(assetKey);
        std::string extension = assetPath.extension().string();
        for (char& character : extension)
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        if (extension != ".h" && extension != ".cpp" && extension != ".cs")
            return false;

        const std::string normalizedKey = assetPath.generic_string();
        constexpr const char* assetsPrefix = "Assets/";
        if (normalizedKey.rfind(assetsPrefix, 0) != 0)
            return false;

        const bool isManagedScript = (extension == ".cs");

        const bool preferHeaderTab = (extension == ".h");
        const bool preferSourceTab = (extension == ".cpp");

        std::filesystem::path relativeWithoutAssets = normalizedKey.substr(std::strlen(assetsPrefix));
        relativeWithoutAssets.replace_extension("");
        const std::string assetRelativePathWithoutExtension = relativeWithoutAssets.generic_string();
        if (assetRelativePathWithoutExtension.empty())
            return false;

        auto& nativeScriptAuthoringState = GetNativeScriptAuthoringState();
        const std::string className = assetPath.stem().string();

        const auto openManagedWithDefaultApplication = [&](const std::string& optionalWarning = {}) -> bool
        {
            const auto resolvedScriptPathResult = Assets::ResolveAssetKeyToPath(normalizedKey);
            if (resolvedScriptPathResult.IsFailure())
            {
                LT_WARN("Managed scripts: failed resolving script asset path '{}' for external open.", normalizedKey);
                return false;
            }

            std::string openError;
            if (!OpenPathInExternalApplication(resolvedScriptPathResult.GetValue(), openError))
            {
                LT_WARN("Managed scripts: {}", openError);
                return false;
            }

            if (!optionalWarning.empty())
                LT_WARN("Managed scripts: {}", optionalWarning);
            LT_INFO("Managed scripts: opened external editor for '{}'.", resolvedScriptPathResult.GetValue().string());
            return true;
        };

        const auto openInternalEditor = [&](const std::string& optionalStatus = {}, bool isError = false) -> bool
        {
            if (isManagedScript)
                return openManagedWithDefaultApplication(optionalStatus);

            std::string openError;
            if (!OpenNativeScriptEditor(className, assetRelativePathWithoutExtension, nativeScriptAuthoringState, openError))
            {
                nativeScriptAuthoringState.StatusMessage = openError;
                nativeScriptAuthoringState.StatusIsError = true;
                nativeScriptAuthoringState.EditorWindowOpen = true;
                nativeScriptAuthoringState.FocusEditorWindowRequested = true;
                return false;
            }

            nativeScriptAuthoringState.SelectHeaderTabRequested = preferHeaderTab;
            nativeScriptAuthoringState.SelectSourceTabRequested = preferSourceTab;
            if (!optionalStatus.empty())
            {
                nativeScriptAuthoringState.StatusMessage = optionalStatus;
                nativeScriptAuthoringState.StatusIsError = isError;
            }
            return true;
        };

        std::string scriptEditorMode = Project::ScriptEditorMode::Internal;
        const auto openedProjectRoot = GetOpenedProjectRoot();
        if (openedProjectRoot.has_value())
        {
            const auto buildSettingsResult = Project::LoadBuildSettings(openedProjectRoot.value());
            if (buildSettingsResult.IsSuccess())
            {
                const auto& buildSettings = buildSettingsResult.GetValue();
                scriptEditorMode = NormalizeScriptEditorMode(buildSettings.ScriptEditorMode);
            }
        }

#if defined(LT_PLATFORM_WINDOWS)
        bool useInternalBackend = false;
        if (openedProjectRoot.has_value())
        {
            const auto buildSettingsResult = Project::LoadBuildSettings(openedProjectRoot.value());
            if (buildSettingsResult.IsSuccess())
                useInternalBackend = (buildSettingsResult.GetValue().BuildBackend == Project::BuildBackend::InternalToolchain);
        }
        if (scriptEditorMode == Project::ScriptEditorMode::External)
        {
            if (!openedProjectRoot.has_value())
            {
                return openInternalEditor(
                    isManagedScript
                        ? "External Visual Studio requested, but no project is open. Falling back to the default external application."
                        : "External editor requested, but no project is open. Using built-in editor.",
                    true);
            }

            if (!isManagedScript)
            {
                std::string mirrorError;
                if (nativeScriptAuthoringState.BuildInProgress.load(std::memory_order_relaxed))
                {
                    // Avoid clearing/rebuilding Generated/ScriptCore while an active compile is reading it.
                    LT_WARN("Native scripts: skipping full generated mirror refresh because a build is currently running.");
                }
                else if (!MirrorAllProjectNativeScriptsToGeneratedDirectory(mirrorError))
                {
                    return openInternalEditor(
                        "Could not prepare external script mirror (" + mirrorError + "). Using built-in editor.",
                        true);
                }
            }

            const auto buildRoot = FindEngineWorkspaceRoot();
            if (!buildRoot.has_value())
            {
                return openInternalEditor(
                    isManagedScript
                        ? "External Visual Studio could not locate engine/toolchain root. Falling back to the default external application."
                        : "External editor could not locate engine/toolchain root. Using built-in editor.",
                    true);
            }

            if (useInternalBackend)
            {
                if (!IsInternalToolchainRootCandidate(buildRoot.value()))
                    useInternalBackend = false;
            }
            else if (IsInternalToolchainRootCandidate(buildRoot.value()))
            {
                useInternalBackend = true;
            }

            const auto resolvedScriptPathResult = Assets::ResolveAssetKeyToPath(normalizedKey);
            if (resolvedScriptPathResult.IsFailure())
            {
                return openInternalEditor(
                    isManagedScript
                        ? "Failed resolving script asset path for external Visual Studio launch. Falling back to the default external application."
                        : "Failed resolving script asset path for external editor. Using built-in editor.",
                    true);
            }

            const auto [configuration, platform] = GetBuildConfigurationAndPlatform(openedProjectRoot.value());
            NativeScriptExternalEditor::OpenVisualStudioRequest request;
            request.ProjectRoot = openedProjectRoot.value();
            request.BuildRoot = buildRoot.value();
            if (const auto engineSourceRoot = FindEngineSourceWorkspaceRoot(); engineSourceRoot.has_value())
                request.EngineSourceRoot = engineSourceRoot.value();
            request.TargetScriptPath = resolvedScriptPathResult.GetValue();
            request.Configuration = configuration;
            request.Platform = platform;
            request.UseInternalToolchain = useInternalBackend;

            const auto externalOpenResult = NativeScriptExternalEditor::OpenScriptInVisualStudio(request);
            if (externalOpenResult.Launched)
                return true;

            const std::string warningMessage =
                externalOpenResult.ErrorMessage.empty()
                    ? (isManagedScript ? "External Visual Studio launch failed. Falling back to the default external application."
                                       : "External editor launch failed. Using built-in editor.")
                    : (isManagedScript ? externalOpenResult.ErrorMessage + " Falling back to the default external application."
                                       : externalOpenResult.ErrorMessage + " Falling back to built-in editor.");
            LT_WARN("{}: {}", isManagedScript ? "Managed scripts" : "Native scripts", warningMessage);
            return openInternalEditor(warningMessage, true);
        }
#else
        if (scriptEditorMode == Project::ScriptEditorMode::External)
            return openInternalEditor(
                isManagedScript
                    ? "External Visual Studio mode is only available on Windows. Falling back to the default external application."
                    : "External Visual Studio mode is only available on Windows. Using built-in editor.",
                true);
#endif

        return openInternalEditor();
    }

    bool BuildProjectNativeScripts(std::string* outStatusMessage)
    {
        auto& nativeScriptAuthoringState = GetNativeScriptAuthoringState();
        if (nativeScriptAuthoringState.BuildInProgress.load(std::memory_order_relaxed))
        {
            if (outStatusMessage)
                *outStatusMessage = "Native script build already in progress.";
            return false;
        }

        const bool started = TriggerNativeScriptsBuild(nativeScriptAuthoringState);
        if (outStatusMessage)
        {
            if (!nativeScriptAuthoringState.StatusMessage.empty())
                *outStatusMessage = nativeScriptAuthoringState.StatusMessage;
            else if (started)
                *outStatusMessage = "Building native scripts...";
            else
                *outStatusMessage = "Failed to start native script build.";
        }
        return started;
    }

    bool GetLastNativeScriptBuildFailure(std::string* outStatusMessage)
    {
        auto& state = GetNativeScriptAuthoringState();
        if (!state.HasCompletedBuild || state.LastBuildSucceeded)
            return false;

        if (outStatusMessage)
        {
            if (!state.LastBuildSummary.empty())
                *outStatusMessage = state.LastBuildSummary;
            else
                *outStatusMessage = "Native script build failed.";
        }
        return true;
    }

    void OnNativeScriptAssetRenamed(const std::string& oldAssetKey, const std::string& newAssetKey)
    {
        auto parseScriptKey = [](const std::string& key, std::string& outClassName, std::string& outRelativePathWithoutExtension) -> bool {
            if (key.rfind("Assets/", 0) != 0)
                return false;
            std::filesystem::path path = key.substr(std::strlen("Assets/"));
            std::string extension = path.extension().string();
            for (char& character : extension)
                character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            if (extension != ".h" && extension != ".cpp")
                return false;
            path.replace_extension("");
            outRelativePathWithoutExtension = path.generic_string();
            outClassName = path.stem().string();
            return !outClassName.empty() && !outRelativePathWithoutExtension.empty();
        };

        std::string oldClassName;
        std::string oldRelativePath;
        std::string newClassName;
        std::string newRelativePath;
        if (!parseScriptKey(oldAssetKey, oldClassName, oldRelativePath) ||
            !parseScriptKey(newAssetKey, newClassName, newRelativePath))
        {
            return;
        }

        auto& state = GetNativeScriptAuthoringState();
        const bool matchesOpenEditor =
            (state.ClassName == oldClassName) ||
            (!state.AssetRelativePath.empty() && state.AssetRelativePath == oldRelativePath);
        if (!matchesOpenEditor)
            return;

        std::string openError;
        if (!OpenNativeScriptEditor(newClassName, newRelativePath, state, openError))
        {
            state.StatusMessage = openError;
            state.StatusIsError = true;
            state.EditorWindowOpen = true;
            state.FocusEditorWindowRequested = true;
        }
    }
}
