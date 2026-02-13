#include "EditorBuildAndRunPanel.h"

#include "Core/Debug/Log.h"
#include "Project/ProjectManager.h"

#include "imgui/imgui.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

#ifdef LT_PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#endif

namespace Limitless::EditorBuildAndRunPanel
{
    namespace
    {
        void SetStatus(EditorBuildAndRunPanelState& state, bool isError, const std::string& msg)
        {
            state.StatusIsError = isError;
            state.StatusMessage = msg;
        }

        std::string ToLower(std::string s)
        {
            for (char& c : s)
            {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            return s;
        }

        std::filesystem::path ComputeWindowsOutputDirectory(const std::filesystem::path& projectRoot,
                                                            const std::string& configuration,
                                                            const std::string& platform)
        {
            const std::string configLower = ToLower(configuration);
            const std::string platformLower = (platform == "ARM64") ? "arm64" : "x64";
            const std::string cfgShortName = configLower + "_" + platformLower;

            // Premake output pattern used by the repo:
            // Build/%{cfg.shortname}-%{cfg.system}-%{cfg.platform}
            return projectRoot / "Build" / (cfgShortName + "-windows-" + platform);
        }

        bool LaunchProcessDetachedWindows(const std::filesystem::path& workingDirectory,
                                          const std::string& commandLine)
        {
#ifdef LT_PLATFORM_WINDOWS
            STARTUPINFOA si{};
            si.cb = sizeof(si);
            PROCESS_INFORMATION pi{};

            // CreateProcess mutates the command line buffer.
            std::string cmd = commandLine;
            const BOOL ok = CreateProcessA(
                nullptr,
                cmd.data(),
                nullptr,
                nullptr,
                FALSE,
                CREATE_NEW_CONSOLE,
                nullptr,
                workingDirectory.empty() ? nullptr : workingDirectory.string().c_str(),
                &si,
                &pi);

            if (!ok)
            {
                return false;
            }

            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return true;
#else
            (void)workingDirectory;
            (void)commandLine;
            return false;
#endif
        }

        int RunBuildScriptBlockingWindows(const std::filesystem::path& projectRoot,
                                          const std::string& configuration,
                                          const std::string& platform)
        {
#ifdef LT_PLATFORM_WINDOWS
            // Use cmd.exe so batch scripts behave consistently.
            // Note: build script already bootstraps premake and MSBuild discovery.
            const std::string script = "Scripts\\build-windows.bat";
            const std::string cmd = "cmd.exe /c \"" + script + " " + configuration + " " + platform + "\"";

            STARTUPINFOA si{};
            si.cb = sizeof(si);
            PROCESS_INFORMATION pi{};

            std::string mutableCmd = cmd;
            const BOOL ok = CreateProcessA(
                nullptr,
                mutableCmd.data(),
                nullptr,
                nullptr,
                FALSE,
                0,
                nullptr,
                projectRoot.string().c_str(),
                &si,
                &pi);

            if (!ok)
            {
                return 1;
            }

            WaitForSingleObject(pi.hProcess, INFINITE);

            DWORD exitCode = 1;
            (void)GetExitCodeProcess(pi.hProcess, &exitCode);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return static_cast<int>(exitCode);
#else
            (void)projectRoot;
            (void)configuration;
            (void)platform;
            return 1;
#endif
        }

        void EnsureLoaded(EditorBuildAndRunPanelState& state, const std::filesystem::path& projectRoot)
        {
            if (state.Loaded)
            {
                return;
            }

            const auto loaded = Project::LoadBuildTargetsSettings(projectRoot);
            state.Settings = loaded.IsSuccess() ? loaded.GetValue() : Project::BuildTargetsSettings{};
            state.Loaded = true;
        }

        int FindActiveTargetIndex(const Project::BuildTargetsSettings& settings)
        {
            for (int i = 0; i < static_cast<int>(settings.Targets.size()); ++i)
            {
                if (settings.Targets[i].Id == settings.ActiveTargetId)
                {
                    return i;
                }
            }
            return settings.Targets.empty() ? -1 : 0;
        }
    }

    void Draw(bool& open, EditorBuildAndRunPanelState& state)
    {
        if (!open)
        {
            return;
        }

        if (!ImGui::Begin("Build And Run", &open))
        {
            ImGui::End();
            return;
        }

        const auto& pm = Project::ProjectManager::GetInstance();
        if (!pm.HasOpenProject())
        {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "No project is open.");
            ImGui::End();
            return;
        }

        const std::filesystem::path projectRoot = pm.GetProjectRoot();
        EnsureLoaded(state, projectRoot);

        // Target selection
        int activeIndex = FindActiveTargetIndex(state.Settings);
        if (activeIndex < 0)
        {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "No build targets configured.");
            ImGui::End();
            return;
        }

        ImGui::TextDisabled("Target");
        if (ImGui::BeginCombo("Active Target", state.Settings.Targets[activeIndex].Id.c_str()))
        {
            for (int i = 0; i < static_cast<int>(state.Settings.Targets.size()); ++i)
            {
                const bool selected = (i == activeIndex);
                if (ImGui::Selectable(state.Settings.Targets[i].Id.c_str(), selected))
                {
                    state.Settings.ActiveTargetId = state.Settings.Targets[i].Id;
                    (void)Project::SaveBuildTargetsSettings(projectRoot, state.Settings);
                    activeIndex = i;
                }
                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::TextDisabled("Configuration");
        const char* configurations[] = {"Debug", "Release", "Dist"};
        int configIndex = 0;
        if (state.Settings.Configuration == "Release") configIndex = 1;
        else if (state.Settings.Configuration == "Dist") configIndex = 2;
        if (ImGui::Combo("##Configuration", &configIndex, configurations, IM_ARRAYSIZE(configurations)))
        {
            state.Settings.Configuration = configurations[configIndex];
            (void)Project::SaveBuildTargetsSettings(projectRoot, state.Settings);
        }

        ImGui::TextDisabled("Platform");
        const char* platforms[] = {"x64", "ARM64"};
        int platformIndex = (state.Settings.Platform == "ARM64") ? 1 : 0;
        if (ImGui::Combo("##Platform", &platformIndex, platforms, IM_ARRAYSIZE(platforms)))
        {
            state.Settings.Platform = platforms[platformIndex];
            (void)Project::SaveBuildTargetsSettings(projectRoot, state.Settings);
        }

        ImGui::Checkbox("Auto-run after build", &state.Settings.AutoRunAfterBuild);
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            (void)Project::SaveBuildTargetsSettings(projectRoot, state.Settings);
        }

        ImGui::Separator();

        const bool building = state.BuildInProgress.load(std::memory_order_relaxed);
        if (building)
        {
            ImGui::TextDisabled("Build in progress...");
        }

        ImGui::BeginDisabled(building);
        if (ImGui::Button("Build", ImVec2(120, 0)))
        {
            // Join previous thread if needed (should be finished).
            if (state.BuildThread.joinable())
            {
                state.BuildThread.join();
            }

            state.BuildInProgress.store(true, std::memory_order_relaxed);
            SetStatus(state, false, "Building...");

            const std::string configuration = state.Settings.Configuration;
            const std::string platform = state.Settings.Platform;

            state.BuildThread = std::thread([&state, projectRoot, configuration, platform]() {
                const int exitCode = RunBuildScriptBlockingWindows(projectRoot, configuration, platform);
                state.LastBuildExitCode.store(exitCode, std::memory_order_relaxed);
                state.BuildInProgress.store(false, std::memory_order_relaxed);
            });
        }

        ImGui::SameLine();

        if (ImGui::Button("Run", ImVec2(120, 0)))
        {
            const auto& target = state.Settings.Targets[activeIndex];
            const std::filesystem::path outDir = ComputeWindowsOutputDirectory(projectRoot, state.Settings.Configuration, state.Settings.Platform);
            const std::filesystem::path exePath = outDir / target.ProjectName / (target.ProjectName + ".exe");
            if (!std::filesystem::exists(exePath))
            {
                SetStatus(state, true, "Executable not found: " + exePath.string());
            }
            else
            {
                const std::string cmd = "\"" + exePath.string() + "\" " + target.Arguments;
                if (!LaunchProcessDetachedWindows(exePath.parent_path(), cmd))
                {
                    SetStatus(state, true, "Failed to launch executable.");
                }
                else
                {
                    SetStatus(state, false, "Launched: " + exePath.string());
                }
            }
        }

        ImGui::SameLine();

        if (ImGui::Button("Build And Run", ImVec2(160, 0)))
        {
            // Join previous thread if needed.
            if (state.BuildThread.joinable())
            {
                state.BuildThread.join();
            }

            state.BuildInProgress.store(true, std::memory_order_relaxed);
            SetStatus(state, false, "Building...");

            const std::string configuration = state.Settings.Configuration;
            const std::string platform = state.Settings.Platform;
            const Project::BuildTarget target = state.Settings.Targets[activeIndex];

            state.BuildThread = std::thread([&state, projectRoot, configuration, platform, target]() {
                const int exitCode = RunBuildScriptBlockingWindows(projectRoot, configuration, platform);
                state.LastBuildExitCode.store(exitCode, std::memory_order_relaxed);

                if (exitCode == 0)
                {
                    const std::filesystem::path outDir = ComputeWindowsOutputDirectory(projectRoot, configuration, platform);
                    const std::filesystem::path exePath = outDir / target.ProjectName / (target.ProjectName + ".exe");
                    if (std::filesystem::exists(exePath))
                    {
                        const std::string cmd = "\"" + exePath.string() + "\" " + target.Arguments;
                        (void)LaunchProcessDetachedWindows(exePath.parent_path(), cmd);
                    }
                }

                state.BuildInProgress.store(false, std::memory_order_relaxed);
            });
        }
        ImGui::EndDisabled();

        // Update status when build finishes.
        if (!building && state.LastBuildExitCode.load(std::memory_order_relaxed) >= 0)
        {
            const int code = state.LastBuildExitCode.load(std::memory_order_relaxed);
            if (code == 0)
            {
                SetStatus(state, false, "Build succeeded.");
            }
            else
            {
                SetStatus(state, true, "Build failed (exit code " + std::to_string(code) + ").");
            }
            state.LastBuildExitCode.store(-1, std::memory_order_relaxed);
        }

        if (!state.StatusMessage.empty())
        {
            const ImVec4 color = state.StatusIsError ? ImVec4(1.0f, 0.35f, 0.35f, 1.0f) : ImVec4(0.35f, 1.0f, 0.35f, 1.0f);
            ImGui::Separator();
            ImGui::TextColored(color, "%s", state.StatusMessage.c_str());
        }

        ImGui::End();
    }

    void Shutdown(EditorBuildAndRunPanelState& state)
    {
        // Ensure we never destroy a joinable thread at shutdown (std::terminate).
        if (state.BuildThread.joinable())
        {
            state.BuildThread.join();
        }
        state.BuildInProgress.store(false, std::memory_order_relaxed);
        state.LastBuildExitCode.store(-1, std::memory_order_relaxed);
    }
}

