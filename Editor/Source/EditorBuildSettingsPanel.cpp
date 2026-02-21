#include "PrecompiledHeader.h"
#include "EditorBuildSettingsPanel.h"

#include "Assets/AssetDatabase.h"
#include "Project/BuildSettings.h"
#include "Project/GameBuilder.h"
#include "Project/ProjectManager.h"
#include "Project/ProjectDefinition.h"
#include "Platform/Platform.h"

#include "imgui/imgui.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

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

        void CopyStringToBuffer(const std::string& source, std::array<char, 512>& destination)
        {
            std::memset(destination.data(), 0, destination.size());
            std::memcpy(destination.data(), source.c_str(), std::min(source.size(), destination.size() - 1));
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

            // Populate output directory buffer from saved settings.
            if (!state.Settings.LastOutputDirectory.empty())
                CopyStringToBuffer(state.Settings.LastOutputDirectory, state.OutputDirectoryBuffer);
            // Root is auto-detected at build-time; clear persisted manual overrides.
            state.Settings.EngineRootOverride.clear();

            AutoResolveBackendMode(state);

            state.SettingsLoaded = true;
        }

        /// Persist settings to disk.
        void SaveSettings(EditorBuildSettingsPanelState& state)
        {
            auto& projectManager = Project::ProjectManager::GetInstance();
            if (!projectManager.HasOpenProject())
                return;

            // Update output directory from the buffer.
            state.Settings.LastOutputDirectory = TrimCopy(std::string(state.OutputDirectoryBuffer.data()));
            state.Settings.EngineRootOverride.clear();
            state.Settings.BuildConfiguration = "Dist";
            state.Settings.BuildBackend = NormalizeBuildBackend(state.Settings.BuildBackend);

            const auto projectRoot = projectManager.GetProjectRoot();
            (void)Project::SaveBuildSettings(projectRoot, state.Settings);
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
            request.Settings.BuildConfiguration = "Dist";
            request.Settings.BuildBackend = NormalizeBuildBackend(request.Settings.BuildBackend);
            request.ProjectRoot = projectManager.GetProjectRoot();
            request.EngineRoot = ResolveBuildBackendRoot(state);

            if (request.Settings.BuildBackend == Project::BuildBackend::LegacySdk &&
                IsInternalToolchainRoot(request.EngineRoot) &&
                !IsEngineWorkspaceRoot(request.EngineRoot))
            {
                request.Settings.BuildBackend = Project::BuildBackend::InternalToolchain;
                state.Settings.BuildBackend = Project::BuildBackend::InternalToolchain;
                SaveSettings(state);
            }

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
                if (request.Settings.BuildBackend == Project::BuildBackend::InternalToolchain)
                    state.StatusMessage = "Could not locate internal toolchain root.";
                else
                    state.StatusMessage = "Could not locate engine workspace root.";
                return;
            }

            const auto healthIssues = GetBuildBackendHealthIssues(state, request.EngineRoot);
            if (!healthIssues.empty())
            {
                state.StatusMessage = "Build backend is not ready: " + healthIssues.front();
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
              Scene* currentScene)
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
        const bool useInternalBackend = (state.Settings.BuildBackend == Project::BuildBackend::InternalToolchain);

        const std::filesystem::path healthRoot = ResolveBuildBackendRoot(state);
        const char* resolvedRootLabel = useInternalBackend ? "Toolchain Root (auto)" : "Engine Root (auto)";
        if (!healthRoot.empty())
            ImGui::Text("%s: %s", resolvedRootLabel, healthRoot.string().c_str());
        else
            ImGui::TextDisabled("%s: <not found>", resolvedRootLabel);

        const auto healthIssues = GetBuildBackendHealthIssues(state, healthRoot);
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
