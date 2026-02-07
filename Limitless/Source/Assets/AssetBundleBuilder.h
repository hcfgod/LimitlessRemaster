#pragma once

#include "Core/Error.h"

#include <filesystem>

namespace Limitless::Assets
{
    // -----------------------------------------------------------------------------
    // AssetBundleBuilder
    //
    // Builds a runtime AssetBundle (manifest + data file) from project assets.
    //
    // Output (default):
    // - <ProjectRoot>/Build/AssetBundle/AssetBundleManifest.json
    // - <ProjectRoot>/Build/AssetBundle/AssetBundle.bin
    // -----------------------------------------------------------------------------
    class AssetBundleBuilder final
    {
    public:
        struct Settings
        {
            // Reserved for future:
            // - incremental builds
            // - compression
            // - cooked formats per type
        };

        static Result<void> BuildProjectAssetBundle(Settings settings = Settings{});
        static Result<void> BuildAssetBundleToDirectory(const std::filesystem::path& outputDirectory, Settings settings = Settings{});

    private:
        static Result<void> BuildAtOutputDirectory(const std::filesystem::path& outputDirectory, Settings settings);
    };
}

