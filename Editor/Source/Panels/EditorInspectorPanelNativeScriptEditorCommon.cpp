#include "PrecompiledHeader.h"
#include "EditorInspectorPanelNativeScriptEditorShared.h"

#include "Assets/AssetDatabase.h"
#include "Assets/AssetPaths.h"
#include "Core/Debug/Log.h"
#include "Platform/Platform.h"
#include "Project/BuildSettings.h"
#include "Project/BuildTargetsSettings.h"
#include "Project/ProjectManager.h"
#include "Scripting/NativeScriptRegistry.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
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
    #include <unistd.h>
#endif

namespace Limitless::EditorInspectorPanel::Internal
{

        NativeScriptAuthoringState& GetNativeScriptAuthoringState()
        {
            static NativeScriptAuthoringState state;
            return state;
        }

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

        bool HeaderFileContainsScriptableEntityDerivation(const std::filesystem::path& headerPath)
        {
            std::ifstream headerFile(headerPath, std::ios::in);
            if (!headerFile.is_open())
                return true;

            std::string line;
            while (std::getline(headerFile, line))
            {
                if (line.find("ScriptableEntity") != std::string::npos)
                {
                    if (line.find(":") != std::string::npos)
                        return true;
                }
            }
            return false;
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
                info.LikelyDerivesFromScriptableEntity = HeaderFileContainsScriptableEntityDerivation(headerPath);
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

        bool SaveBufferToTextFile(const std::filesystem::path& path,
                                  const std::array<char, kNativeScriptEditorBufferSize>& buffer,
                                  std::string& outError,
                                  bool* outFileChanged)
        {
            if (outFileChanged)
                *outFileChanged = false;

            const std::string desiredContent(buffer.data());
            std::error_code directoryError;
            if (path.has_parent_path())
                std::filesystem::create_directories(path.parent_path(), directoryError);
            if (directoryError)
            {
                outError = "Failed to create parent directory for file: " + path.string();
                return false;
            }

            std::error_code existsError;
            if (std::filesystem::exists(path, existsError))
            {
                std::ifstream input(path, std::ios::in | std::ios::binary);
                if (input.is_open())
                {
                    const std::string existingContent((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
                    if (existingContent == desiredContent)
                        return true;
                }
            }

            std::ofstream output(path, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!output.is_open())
            {
                outError = "Failed to open file for writing: " + path.string();
                return false;
            }

            output << desiredContent;
            if (!output.good())
            {
                outError = "Failed to write file: " + path.string();
                return false;
            }

            if (outFileChanged)
                *outFileChanged = true;
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
}

