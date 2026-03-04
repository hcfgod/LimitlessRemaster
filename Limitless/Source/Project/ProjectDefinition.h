#pragma once

#include "Core/Error.h"

#include <filesystem>
#include <string>

namespace Limitless::Project
{
    /// Minimal persistent project descriptor stored at `<ProjectRoot>/Project/Project.json`.
    ///
    /// This is the project's "solution marker" (Unity-style) that makes root discovery deterministic
    /// and centralizes project-level settings like asset/build roots and default scene.
    ///
    /// Notes:
    /// - The file is intended to be committed to source control.
    /// - GUID is stable-by-source-control (random at creation, never changes after).
    struct ProjectDefinition final
    {
        uint32_t Version = 1;
        std::string ProjectGuid;
        std::string ProjectName;
        std::string CreatedUtc;

        std::string AssetRootRelative = "Assets";
        std::string BuildRootRelative = "Build";

        struct DefaultSceneReference
        {
            std::string Guid;
            std::string Key;
        } DefaultScene;

        uint32_t SettingsVersion = 1;
    };

    /// Returns `<projectRoot>/Project/Project.json`.
    [[nodiscard]] std::filesystem::path GetProjectFilePathForRoot(const std::filesystem::path& projectRoot);

    [[nodiscard]] Result<ProjectDefinition> LoadProjectDefinitionFromFile(const std::filesystem::path& projectFilePath);
    [[nodiscard]] Result<void> SaveProjectDefinitionToFile(const std::filesystem::path& projectFilePath, const ProjectDefinition& definition);
}

