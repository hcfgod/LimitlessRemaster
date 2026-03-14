#include "Project/GameBuilderInternal.h"

#include "Assets/AssetBundleBuilder.h"
#include "Core/Debug/Log.h"
#include "Project/ProjectDefinition.h"
#include "Project/RemoteBuildProvider.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace Limitless::Project
{
    // Anonymous namespace helpers and free functions have been moved to
    // GameBuilderHelpers.cpp. Platform finalization, CopyRuntimeFiles, and
    // LaunchExecutable have been moved to GameBuilderPlatform.cpp.
    // Shared declarations live in GameBuilderInternal.h.

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

        layout.ManagedPayloadDirectory = layout.RuntimeDirectory / "Managed";

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
        layout.ManagedPayloadDirectory = remoteManifest.ManagedPayloadDirectory;
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
