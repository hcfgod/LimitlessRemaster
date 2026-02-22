#include "Project/GameBuilder.h"

#include "Assets/AssetBundleBuilder.h"
#include "Core/Debug/Log.h"
#include "Project/ProjectDefinition.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace Limitless::Project
{
    using json = nlohmann::json;

    // -------------------------------------------------------------------------
    // Helpers
    // -------------------------------------------------------------------------

    namespace
    {
        std::string GetScriptCoreLibraryName();

        /// Converts a user-facing configuration name (e.g. "Release") to the
        /// premake cfg.shortname token (e.g. "release_x64").
        /// Premake shortname format: lowercase(config)_lowercase(platform).
        std::string ToPremakeShortname(const std::string& configuration)
        {
            std::string lower = configuration;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

#if defined(LT_ARCHITECTURE_ARM64)
            return lower + "_arm64";
#else
            return lower + "_x64";
#endif
        }

        /// Returns the Runtime build output directory for a given config.
        /// Example: <EngineRoot>/Build/release_x64-windows-x64/Runtime/
        std::filesystem::path GetConfigPlatformBuildFolder(const std::filesystem::path& engineRoot, const std::string& configuration)
        {
#if defined(LT_PLATFORM_WINDOWS)
            const std::string platformToken = "windows";
#elif defined(LT_PLATFORM_MACOS)
            const std::string platformToken = "macosx";
#else
            const std::string platformToken = "linux";
#endif

#if defined(LT_ARCHITECTURE_ARM64)
            const std::string architectureToken = "x64"; // premake uses "x64" even in dir suffix
#else
            const std::string architectureToken = "x64";
#endif

            const std::string shortname = ToPremakeShortname(configuration);
            const std::string folderName = shortname + "-" + platformToken + "-" + architectureToken;
            return engineRoot / "Build" / folderName;
        }

        std::filesystem::path GetRuntimeBuildDirectory(const std::filesystem::path& engineRoot, const std::string& configuration)
        {
            return GetConfigPlatformBuildFolder(engineRoot, configuration) / "Runtime";
        }

        /// Returns the ScriptCore build output directory for a given config.
        std::filesystem::path GetScriptCoreBuildDirectory(const std::filesystem::path& engineRoot, const std::string& configuration)
        {
            return GetConfigPlatformBuildFolder(engineRoot, configuration) / "Editor";
        }

        std::filesystem::path GetInternalRuntimeTemplateDirectory(const std::filesystem::path& toolchainRoot, const std::string& configuration)
        {
            const std::string folderName = GetConfigPlatformBuildFolder(std::filesystem::path(), configuration).filename().string();
            return toolchainRoot / "RuntimeTemplates" / folderName;
        }

        std::filesystem::path GetProjectScriptCoreOutputDirectory(const GameBuildRequest& request)
        {
            const std::string folderName = GetConfigPlatformBuildFolder(std::filesystem::path(), request.Settings.BuildConfiguration).filename().string();
            return request.ProjectRoot / "Build" / "ScriptCore" / folderName;
        }

        std::filesystem::path GetProjectScriptCoreLibraryPath(const GameBuildRequest& request)
        {
            return GetProjectScriptCoreOutputDirectory(request) / GetScriptCoreLibraryName();
        }

        std::string GetRuntimeExecutableName()
        {
#if defined(LT_PLATFORM_WINDOWS)
            return "Runtime.exe";
#else
            return "Runtime";
#endif
        }

        std::string GetGameExecutableName(const std::string& projectName)
        {
#if defined(LT_PLATFORM_WINDOWS)
            return projectName + ".exe";
#else
            return projectName;
#endif
        }

        std::string GetScriptCoreLibraryName()
        {
#if defined(LT_PLATFORM_WINDOWS)
            return "ScriptCore.dll";
#elif defined(LT_PLATFORM_MACOS)
            return "libScriptCore.dylib";
#else
            return "libScriptCore.so";
#endif
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

        std::string GetBuildPlatformArg()
        {
#if defined(LT_ARCHITECTURE_ARM64)
            return "ARM64";
#else
            return "x64";
#endif
        }

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
        void CopyDllsFromDirectory(const std::filesystem::path& sourceDirectory,
                                   const std::filesystem::path& destinationDirectory,
                                   const std::string& extension,
                                   GameBuildResult& result)
        {
            std::error_code iterateError;
            for (const auto& entry : std::filesystem::directory_iterator(sourceDirectory, iterateError))
            {
                if (entry.is_regular_file() && entry.path().extension().string() == extension)
                {
                    const auto destPath = destinationDirectory / entry.path().filename();
                    CopySingleFile(entry.path(), destPath, result);
                }
            }
        }

        int RunCommand(const std::string& command)
        {
            LT_CORE_INFO("GameBuilder: executing: {}", command);
            return std::system(command.c_str());
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

        if (IsInternalBackend(request))
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

        result.StepLog.push_back("Validation passed: " + std::to_string(enabledScenes.size()) + " scene(s) enabled.");
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
        const auto scriptPath = request.EngineRoot / "Scripts" / GetBuildScriptName(internalBackend);
        if (!std::filesystem::exists(scriptPath))
        {
            result.ErrorMessage = "Build script not found: " + scriptPath.string();
            return false;
        }

        const std::string configArg = request.Settings.BuildConfiguration;
        const std::string platformArg = GetBuildPlatformArg();

#if defined(LT_PLATFORM_WINDOWS)
        std::string command = "cd /d \"" + request.EngineRoot.string() + "\" && call \""
            + scriptPath.string() + "\" " + configArg + " " + platformArg;
        if (internalBackend)
            command += " \"" + request.ProjectRoot.string() + "\"";
#else
        std::string command = "cd \"" + request.EngineRoot.string() + "\" && bash \""
            + scriptPath.string() + "\" --config " + configArg + " --platform " + platformArg;
        if (internalBackend)
            command += " --project-root \"" + request.ProjectRoot.string() + "\"";
#endif

        const int exitCode = RunCommand(command);
        if (exitCode != 0)
        {
            result.ErrorMessage = "ScriptCore build failed (exit code " + std::to_string(exitCode) + ").";
            return false;
        }

        if (IsInternalBackend(request))
        {
            const std::filesystem::path builtScriptCorePath =
                GetScriptCoreBuildDirectory(request.EngineRoot, request.Settings.BuildConfiguration)
                / GetScriptCoreLibraryName();
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

    bool GameBuilder::CopyRuntimeFiles(const GameBuildRequest& request, GameBuildResult& result)
    {
        result.StepLog.push_back("Copying runtime files...");

        const std::string config = request.Settings.BuildConfiguration;
        const std::string projectName = request.ProjectName.empty() ? "Game" : request.ProjectName;
        const bool useInternalBackend = IsInternalBackend(request);
        const std::filesystem::path runtimeDir = useInternalBackend
            ? GetInternalRuntimeTemplateDirectory(request.EngineRoot, config)
            : GetRuntimeBuildDirectory(request.EngineRoot, config);

        if (useInternalBackend)
        {
            if (!std::filesystem::is_directory(runtimeDir))
            {
                result.ErrorMessage = "Internal runtime template directory not found: " + runtimeDir.string();
                return false;
            }
            result.StepLog.push_back("Using internal runtime templates: " + runtimeDir.string());
        }
        else
        {
            // Always build the runtime executable for the selected configuration before copying.
            // This prevents shipping stale binaries (e.g. old Runtime.exe still booting TestLayer).
#if defined(LT_PLATFORM_WINDOWS)
            const auto runtimeBuildScript = request.EngineRoot / "Scripts" / "build-runtime-windows.bat";
            const auto fallbackBuildScript = request.EngineRoot / "Scripts" / "build-windows.bat";
            const auto mainBuildScript = std::filesystem::exists(runtimeBuildScript)
                ? runtimeBuildScript
                : fallbackBuildScript;
            if (std::filesystem::exists(mainBuildScript))
            {
                result.StepLog.push_back("Building Runtime (" + config + ")...");
                const std::string buildCommand = "cd /d \"" + request.EngineRoot.string()
                    + "\" && call \"" + mainBuildScript.string() + "\" " + config + " " + GetBuildPlatformArg();
                const int buildExitCode = RunCommand(buildCommand);
                if (buildExitCode != 0)
                {
                    result.ErrorMessage = "Failed to build Runtime (exit code " + std::to_string(buildExitCode) + ").";
                    return false;
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
                    buildCommand += " --platform " + GetBuildPlatformArg();
                const int buildExitCode = RunCommand(buildCommand);
                if (buildExitCode != 0)
                {
                    result.ErrorMessage = "Failed to build Runtime (exit code " + std::to_string(buildExitCode) + ").";
                    return false;
                }
            }
#endif
        }

        // 1. Copy Runtime executable (renamed to project name).
        const auto runtimeExePath = runtimeDir / GetRuntimeExecutableName();
        const auto gameExePath = request.OutputDirectory / GetGameExecutableName(projectName);
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

        // 2. Copy config.json.
        const auto sourceConfig = runtimeDir / "config.json";
        if (std::filesystem::exists(sourceConfig))
        {
            CopySingleFile(sourceConfig, request.OutputDirectory / "config.json", result);
            try
            {
                // Stamp the shipped game window title with the project name.
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

                    std::ofstream out(outputConfigPath, std::ios::out | std::ios::binary | std::ios::trunc);
                    if (out.is_open())
                    {
                        out << configRoot.dump(4);
                        result.StepLog.push_back("Updated config.json window title to '" + projectName + "'.");
                    }
                }
            }
            catch (const std::exception& e)
            {
                result.StepLog.push_back(std::string("Warning: failed to update window title in config.json: ") + e.what());
            }
        }

        // 2b. Copy runtime window icon so shipped config can resolve `window.icon`.
        const auto sourceWindowIcon = runtimeDir / "LimitlessLogo.ico";
        if (std::filesystem::exists(sourceWindowIcon))
        {
            CopySingleFile(sourceWindowIcon, request.OutputDirectory / "LimitlessLogo.ico", result);
        }

        // 3. Copy ScriptCore DLL (prefer project-local staging in internal mode).
        std::filesystem::path scriptCorePath = GetScriptCoreBuildDirectory(request.EngineRoot, config) / GetScriptCoreLibraryName();
        if (useInternalBackend)
        {
            const std::filesystem::path projectLocalScriptCore = GetProjectScriptCoreLibraryPath(request);
            if (std::filesystem::exists(projectLocalScriptCore))
                scriptCorePath = projectLocalScriptCore;
        }
        if (std::filesystem::exists(scriptCorePath))
        {
            CopySingleFile(scriptCorePath, request.OutputDirectory / GetScriptCoreLibraryName(), result);
            result.StepLog.push_back("Copied ScriptCore library.");
        }
        else
        {
            result.StepLog.push_back("Warning: ScriptCore library not found at " + scriptCorePath.string());
        }

        // 4. Copy runtime DLLs (shaderc, ffmpeg, etc.) from the Runtime build directory.
#if defined(LT_PLATFORM_WINDOWS)
        if (std::filesystem::is_directory(runtimeDir))
            CopyDllsFromDirectory(runtimeDir, request.OutputDirectory, ".dll", result);
#elif defined(LT_PLATFORM_LINUX)
        if (std::filesystem::is_directory(runtimeDir))
            CopyDllsFromDirectory(runtimeDir, request.OutputDirectory, ".so", result);
#endif

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

    void GameBuilder::LaunchExecutable(const std::filesystem::path& executablePath)
    {
        if (executablePath.empty() || !std::filesystem::exists(executablePath))
            return;

        const std::string command =
#if defined(LT_PLATFORM_WINDOWS)
            "start \"\" \"" + executablePath.string() + "\"";
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

        if (!BuildScriptCore(request, result))
        {
            result.StepLog.push_back("Build failed: " + result.ErrorMessage);
            return result;
        }

        if (!CopyRuntimeFiles(request, result))
        {
            result.StepLog.push_back("Build failed: " + result.ErrorMessage);
            return result;
        }

        if (!WriteGameBootstrap(request, result))
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
            result.StepLog.push_back("Launching game...");
            LaunchExecutable(result.OutputExecutablePath);
        }
        return result;
    }
}
