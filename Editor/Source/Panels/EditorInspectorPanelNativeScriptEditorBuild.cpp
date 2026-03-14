#include "PrecompiledHeader.h"
#include "EditorInspectorPanelNativeScriptEditorShared.h"

#include "Project/BuildSettings.h"
#include "Project/ProjectManager.h"
#include "Core/Debug/Log.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#ifdef LT_PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#else
    #include <sys/wait.h>
    #include <cstdlib>
#endif

namespace Limitless::EditorInspectorPanel::Internal
{

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
            const std::filesystem::path scriptPath = buildRoot / "Scripts" / scriptName;
            std::string scriptCommand = "cmd.exe /d /s /c \"\"" + scriptPath.string() + "\" " + configuration + " " + platform;
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

            std::vector<std::pair<std::filesystem::path, std::filesystem::path>> filesToMirror;
            std::set<std::filesystem::path> expectedRelativePaths;

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

                filesToMirror.push_back({sourceCppPath, relativeCppPath});
                filesToMirror.push_back({sourceHeaderPath, relativeHeaderPath});
                expectedRelativePaths.insert(relativeCppPath);
                expectedRelativePaths.insert(relativeHeaderPath);
            }

            std::error_code mirrorError;
            for (const std::filesystem::path& generatedDirectory : generatedDirectories)
            {
                std::filesystem::create_directories(generatedDirectory, mirrorError);
                if (mirrorError)
                {
                    outError = "Cannot create generated ScriptCore mirror directory '" + generatedDirectory.string() + "': " + mirrorError.message();
                    return false;
                }

                for (const auto& [sourcePath, relativePath] : filesToMirror)
                {
                    const std::filesystem::path destPath = generatedDirectory / relativePath;

                    std::filesystem::create_directories(destPath.parent_path(), mirrorError);
                    if (mirrorError)
                    {
                        outError = "Failed to create generated script directory '" + destPath.parent_path().string() + "': " + mirrorError.message();
                        return false;
                    }

                    bool needsCopy = true;
                    if (std::filesystem::exists(destPath, mirrorError))
                    {
                        const auto srcTime = std::filesystem::last_write_time(sourcePath, mirrorError);
                        if (!mirrorError)
                        {
                            const auto dstTime = std::filesystem::last_write_time(destPath, mirrorError);
                            if (!mirrorError && srcTime <= dstTime)
                                needsCopy = false;
                        }
                        mirrorError.clear();
                    }

                    if (needsCopy)
                    {
                        std::filesystem::copy_file(sourcePath, destPath, std::filesystem::copy_options::overwrite_existing, mirrorError);
                        if (mirrorError)
                        {
                            outError = "Failed to mirror file '" + sourcePath.string() + "' to '" + destPath.string() + "': " + mirrorError.message();
                            return false;
                        }
                    }
                }

                if (std::filesystem::exists(generatedDirectory))
                {
                    for (const auto& existingEntry : std::filesystem::recursive_directory_iterator(generatedDirectory, std::filesystem::directory_options::skip_permission_denied))
                    {
                        if (!existingEntry.is_regular_file())
                            continue;
                        std::error_code relErr;
                        const std::filesystem::path relPath = std::filesystem::relative(existingEntry.path(), generatedDirectory, relErr);
                        if (relErr)
                            continue;
                        if (expectedRelativePaths.find(relPath) == expectedRelativePaths.end())
                            std::filesystem::remove(existingEntry.path(), relErr);
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
}

