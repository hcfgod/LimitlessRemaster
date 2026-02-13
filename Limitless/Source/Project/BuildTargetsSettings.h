#pragma once

#include "Core/Error.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Limitless::Project
{
    struct BuildTarget final
    {
        std::string Id;
        std::string ProjectName;
        std::string Arguments;
    };

    struct BuildTargetsSettings final
    {
        uint32_t Version = 1;

        std::string ActiveTargetId;
        std::string Configuration = "Debug"; // Debug | Release | Dist
        std::string Platform = "x64";        // x64 | ARM64 (Windows)
        bool AutoRunAfterBuild = false;

        std::vector<BuildTarget> Targets;
    };

    [[nodiscard]] std::filesystem::path GetBuildTargetsSettingsPath(const std::filesystem::path& projectRoot);
    [[nodiscard]] Result<BuildTargetsSettings> LoadBuildTargetsSettings(const std::filesystem::path& projectRoot);
    [[nodiscard]] Result<void> SaveBuildTargetsSettings(const std::filesystem::path& projectRoot, const BuildTargetsSettings& settings);
}

