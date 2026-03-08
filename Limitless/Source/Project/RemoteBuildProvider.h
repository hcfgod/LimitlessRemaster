#pragma once

#include "Project/GameBuilder.h"

#include <filesystem>
#include <vector>

namespace Limitless::Project
{
    struct RemoteBuildArtifactManifest final
    {
        std::filesystem::path StagingRoot;
        std::filesystem::path RuntimeDirectory;
        std::filesystem::path ScriptCoreLibraryPath;
        std::filesystem::path ManagedPayloadDirectory;
        std::vector<std::filesystem::path> DynamicLibraryDirectories;
    };

    /// Dispatches a remote desktop build and stages returned artifacts locally.
    /// Returns false and writes result.ErrorMessage on failure.
    bool FetchRemoteBuildArtifacts(const GameBuildRequest& request,
                                   const std::filesystem::path& generatedScriptsDirectory,
                                   RemoteBuildArtifactManifest& manifest,
                                   GameBuildResult& result);
}
