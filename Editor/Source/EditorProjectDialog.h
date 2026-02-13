#pragma once

#include <array>
#include <string>

namespace Limitless::EditorProjectDialog
{
    enum class ProjectDialogMode
    {
        Open,
        Create
    };

    struct EditorProjectDialogState
    {
        bool IsOpen = false;
        bool RequestOpenPopup = false;
        ProjectDialogMode Mode = ProjectDialogMode::Open;

        std::array<char, 512> ProjectRootPathBuffer{};
        std::array<char, 256> ProjectNameBuffer{};

        std::string StatusMessage;
        bool StatusIsError = false;
    };

    void RequestOpen(EditorProjectDialogState& state, ProjectDialogMode mode);

    /// Draws the modal dialog. Returns true if a project was successfully opened/created this frame.
    bool Draw(EditorProjectDialogState& state);
}

