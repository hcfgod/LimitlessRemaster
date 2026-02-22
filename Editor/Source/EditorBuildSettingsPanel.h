#pragma once

#include "Project/BuildSettings.h"
#include "Project/GameBuilder.h"

#include <array>
#include <atomic>
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

        /// Build in progress flag (set by build thread, read by UI thread).
        std::atomic<bool> BuildInProgress{false};

        /// Build result from the last completed build.
        Project::GameBuildResult LastBuildResult;

        struct BuildJobState final
        {
            std::mutex Mutex;
            bool Completed = false;
            Project::GameBuildResult Result;
        };

        /// Build worker thread.
        std::thread BuildThread;

        /// Shared build job state held by worker and UI threads.
        std::shared_ptr<BuildJobState> ActiveBuildJob;

        /// Status message shown in the panel (UI thread only).
        std::string StatusMessage;
        bool StatusIsError = false;

        /// Scene list helper status shown near the "Add Current Scene" action.
        std::string SceneListStatusMessage;

        ~EditorBuildSettingsPanelState()
        {
            if (BuildThread.joinable())
                BuildThread.detach();
        }
    };

    /// Draw the Build Settings window. Returns true if the window is still open.
    void Draw(bool& showWindow,
              EditorBuildSettingsPanelState& state,
              const std::string& currentSceneAssetKey,
              Scene* currentScene,
              const std::function<bool()>& saveActiveSceneBeforeBuild);

    /// Ensures any in-flight build work is finalized before panel owner shutdown.
    void Shutdown(EditorBuildSettingsPanelState& state);
}
