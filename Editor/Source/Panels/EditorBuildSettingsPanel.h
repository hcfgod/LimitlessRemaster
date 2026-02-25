#pragma once

#include "Project/BuildSettings.h"
#include "Project/GameBuilder.h"

#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Limitless
{
    class Scene;
}

namespace Limitless::EditorBuildSettingsPanel
{
    struct EditorBuildSettingsPanelState;
    void Shutdown(EditorBuildSettingsPanelState& state);

    // -------------------------------------------------------------------------
    // Build Settings Panel State
    //
    // Persistent state for the Build Settings window. Owned by EditorLayer
    // and passed into Draw().
    // -------------------------------------------------------------------------
    struct EditorBuildSettingsPanelState
    {
        /// Build settings loaded from disk. Modified interactively by the panel.
        Project::BuildSettings Settings;

        /// Whether settings have been loaded from the project yet.
        bool SettingsLoaded = false;

        /// Output folder path buffer (editable text field).
        std::array<char, 512> OutputDirectoryBuffer{};

        /// Optional shipped game window icon path buffer.
        std::array<char, 512> WindowIconPathBuffer{};

        /// Remote build endpoint buffer.
        std::array<char, 512> RemoteBuildEndpointBuffer{};

        /// Target-routed remote endpoint buffers.
        std::array<char, 512> RemoteBuildEndpointWindowsBuffer{};
        std::array<char, 512> RemoteBuildEndpointMacOSBuffer{};
        std::array<char, 512> RemoteBuildEndpointLinuxBuffer{};

        /// Remote build worker pool buffer.
        std::array<char, 128> RemoteBuildPoolBuffer{};

        /// Remote build auth token buffer.
        std::array<char, 512> RemoteBuildAuthTokenBuffer{};

        /// Build in progress flag (set by build thread, read by UI thread).
        std::atomic<bool> BuildInProgress{false};

        /// Build result from the last completed build.
        Project::GameBuildResult LastBuildResult;

        struct BuildJobState final
        {
            std::mutex Mutex;
            bool Completed = false;
            Project::GameBuildResult Result;
            std::vector<std::string> LiveLog;
        };

        /// Build worker thread.
        std::thread BuildThread;

        /// Shared build job state held by worker and UI threads.
        std::shared_ptr<BuildJobState> ActiveBuildJob;

        /// Status message shown in the panel (UI thread only).
        std::string StatusMessage;
        bool StatusIsError = false;

        /// Build timer metadata (UI thread only).
        bool BuildTimerActive = false;
        std::chrono::steady_clock::time_point BuildStartTime{};

        /// Scene list helper status shown near the "Add Current Scene" action.
        std::string SceneListStatusMessage;

        ~EditorBuildSettingsPanelState()
        {
            Shutdown(*this);
        }
    };

    /// Draw the Build Settings window. Returns true if the window is still open.
    void Draw(bool& showWindow,
              EditorBuildSettingsPanelState& state,
              const std::string& currentSceneAssetKey,
              Scene* currentScene,
              const std::function<bool()>& saveActiveSceneBeforeBuild);

}
