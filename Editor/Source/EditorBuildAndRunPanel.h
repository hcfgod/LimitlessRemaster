#pragma once

#include "Project/BuildTargetsSettings.h"

#include <atomic>
#include <string>
#include <thread>

namespace Limitless::EditorBuildAndRunPanel
{
    struct EditorBuildAndRunPanelState final
    {
        bool Loaded = false;
        Project::BuildTargetsSettings Settings;

        std::string StatusMessage;
        bool StatusIsError = false;

        std::atomic<bool> BuildInProgress{false};
        std::atomic<int> LastBuildExitCode{-1};
        std::thread BuildThread;
    };

    void Draw(bool& open, EditorBuildAndRunPanelState& state);
    void Shutdown(EditorBuildAndRunPanelState& state);
}

