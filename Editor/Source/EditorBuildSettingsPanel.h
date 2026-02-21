#pragma once

#include "Project/BuildSettings.h"
#include "Project/GameBuilder.h"

#include <array>
#include <atomic>
#include <functional>
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

        /// Build thread (detached; joins itself).
        std::thread BuildThread;

        /// Status message shown in the panel (set by build thread / UI).
        std::string StatusMessage;

        /// Scene list helper status shown near the "Add Current Scene" action.
        std::string SceneListStatusMessage;
    };

    /// Draw the Build Settings window. Returns true if the window is still open.
    void Draw(bool& showWindow,
              EditorBuildSettingsPanelState& state,
              const std::string& currentSceneAssetKey,
              Scene* currentScene,
              const std::function<bool()>& saveActiveSceneBeforeBuild);
}
