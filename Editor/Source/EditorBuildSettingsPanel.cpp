#include "PrecompiledHeader.h"
#include "EditorBuildSettingsPanel.h"

#include "Project/BuildSettings.h"
#include "Project/GameBuilder.h"
#include "Project/ProjectManager.h"
#include "Project/ProjectDefinition.h"
#include "Platform/Platform.h"

#include "imgui/imgui.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>

namespace Limitless::EditorBuildSettingsPanel
{
    namespace
    {
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

            // Populate output directory buffer from saved settings.
            if (!state.Settings.LastOutputDirectory.empty())
            {
                std::memset(state.OutputDirectoryBuffer.data(), 0, state.OutputDirectoryBuffer.size());
                const auto& dir = state.Settings.LastOutputDirectory;
                std::memcpy(state.OutputDirectoryBuffer.data(), dir.c_str(),
                            std::min(dir.size(), state.OutputDirectoryBuffer.size() - 1));
            }

            state.SettingsLoaded = true;
        }

        /// Persist settings to disk.
        void SaveSettings(EditorBuildSettingsPanelState& state)
        {
            auto& projectManager = Project::ProjectManager::GetInstance();
            if (!projectManager.HasOpenProject())
                return;

            // Update output directory from the buffer.
            state.Settings.LastOutputDirectory = std::string(state.OutputDirectoryBuffer.data());

            const auto projectRoot = projectManager.GetProjectRoot();
            (void)Project::SaveBuildSettings(projectRoot, state.Settings);
        }

        /// Returns true when `candidate` looks like the engine workspace root.
        /// We check for the Scripts/ directory which only exists at the top level.
        bool IsEngineWorkspaceRoot(const std::filesystem::path& candidate)
        {
            std::error_code errorCode;
            // The Scripts/ folder with the build scripts is the most reliable marker.
            if (std::filesystem::is_directory(candidate / "Scripts", errorCode))
                return true;
            // Fallback: the solution file only exists at the workspace root.
            if (std::filesystem::exists(candidate / "LimitlessRemaster.sln", errorCode))
                return true;
            return false;
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

        /// Start a build in a background thread.
        void StartBuild(EditorBuildSettingsPanelState& state, bool runAfterBuild)
        {
            if (state.BuildInProgress.load())
                return;

            auto& projectManager = Project::ProjectManager::GetInstance();
            if (!projectManager.HasOpenProject())
            {
                state.StatusMessage = "No project is open.";
                return;
            }

            // Save settings before building.
            SaveSettings(state);

            Project::GameBuildRequest request;
            request.OutputDirectory = std::string(state.OutputDirectoryBuffer.data());
            request.Settings = state.Settings;
            request.ProjectRoot = projectManager.GetProjectRoot();
            request.EngineRoot = FindEngineRoot();

            // Use project name from ProjectDefinition.
            const auto definition = projectManager.GetProjectDefinition();
            if (definition.has_value() && !definition->ProjectName.empty())
                request.ProjectName = definition->ProjectName;
            else
                request.ProjectName = "Game";

            if (request.OutputDirectory.empty())
            {
                state.StatusMessage = "Please set an output directory.";
                return;
            }

            if (request.EngineRoot.empty())
            {
                state.StatusMessage = "Could not locate engine workspace root.";
                return;
            }

            state.BuildInProgress.store(true);
            state.StatusMessage = "Building...";

            // Launch build on a detached background thread.
            if (state.BuildThread.joinable())
                state.BuildThread.join();

            state.BuildThread = std::thread([&state, request, runAfterBuild]()
            {
                Project::GameBuildResult result;
                if (runAfterBuild)
                    result = Project::GameBuilder::BuildAndRunGame(request);
                else
                    result = Project::GameBuilder::BuildGame(request);

                state.LastBuildResult = std::move(result);

                if (state.LastBuildResult.Success)
                    state.StatusMessage = "Build succeeded (" + std::to_string(state.LastBuildResult.ElapsedSeconds) + "s).";
                else
                    state.StatusMessage = "Build failed: " + state.LastBuildResult.ErrorMessage;

                state.BuildInProgress.store(false);
            });

            state.BuildThread.detach();
        }
    }

    // -------------------------------------------------------------------------
    // Draw
    // -------------------------------------------------------------------------

    void Draw(bool& showWindow,
              EditorBuildSettingsPanelState& state,
              const std::string& currentSceneAssetKey,
              Scene* /*currentScene*/)
    {
        if (!showWindow)
            return;

        EnsureSettingsLoaded(state);

        ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Build Settings", &showWindow))
        {
            ImGui::End();
            return;
        }

        const bool buildInProgress = state.BuildInProgress.load();

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

        // Add Open Scene button.
        if (ImGui::Button("Add Open Scene"))
        {
            if (!currentSceneAssetKey.empty())
            {
                // Check for duplicates.
                bool alreadyAdded = false;
                for (const auto& entry : scenes)
                {
                    if (entry.Key == currentSceneAssetKey)
                    {
                        alreadyAdded = true;
                        break;
                    }
                }
                if (!alreadyAdded)
                {
                    Project::BuildSceneEntry newEntry;
                    newEntry.Key = currentSceneAssetKey;
                    newEntry.Enabled = true;
                    scenes.push_back(std::move(newEntry));
                }
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // -----------------------------------------------------------------
        // Build Configuration
        // -----------------------------------------------------------------
        ImGui::SeparatorText("Build Configuration");

        const char* configOptions[] = { "Debug", "Release", "Dist" };
        int currentConfigIndex = 1; // Default to Release.
        for (int i = 0; i < 3; ++i)
        {
            if (state.Settings.BuildConfiguration == configOptions[i])
                currentConfigIndex = i;
        }
        if (ImGui::Combo("Configuration", &currentConfigIndex, configOptions, 3))
            state.Settings.BuildConfiguration = configOptions[currentConfigIndex];

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

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // -----------------------------------------------------------------
        // Build / Build & Run buttons
        // -----------------------------------------------------------------
        ImGui::BeginDisabled(buildInProgress);

        if (ImGui::Button("Build", ImVec2(120, 30)))
        {
            SaveSettings(state);
            StartBuild(state, false);
        }

        ImGui::SameLine();

        if (ImGui::Button("Build And Run", ImVec2(140, 30)))
        {
            SaveSettings(state);
            StartBuild(state, true);
        }

        ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Save Settings", ImVec2(120, 30)))
            SaveSettings(state);

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
            if (state.LastBuildResult.Success)
                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "%s", state.StatusMessage.c_str());
            else
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", state.StatusMessage.c_str());
        }

        // Show step log from last build.
        if (!state.LastBuildResult.StepLog.empty())
        {
            if (ImGui::CollapsingHeader("Build Log", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::BeginChild("BuildLog", ImVec2(0, 150), true);
                for (const auto& line : state.LastBuildResult.StepLog)
                    ImGui::TextWrapped("%s", line.c_str());
                ImGui::EndChild();
            }
        }

        ImGui::End();
    }
}
