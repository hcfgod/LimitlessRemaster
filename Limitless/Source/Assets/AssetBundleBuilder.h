#pragma once

#include "Assets/AssetBundle.h"
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
            // Per-entry compression for stored payloads.
            // When set to Zstd but the build does not have Zstd enabled, the builder will fall back to None.
            AssetBundleCompression Compression = AssetBundleCompression::Zstd;

            // Zstd compression level (only used when Compression == Zstd).
            int ZstdCompressionLevel = 3;
        };

        // NOTE:
        // We intentionally provide overloads instead of default-argument `Settings{}` here.
        // Some toolchains (notably GCC/Clang in certain modes) reject default arguments for
        // nested types with default member initializers.
        static Result<void> BuildProjectAssetBundle();
        static Result<void> BuildProjectAssetBundle(Settings settings);

        static Result<void> BuildAssetBundleToDirectory(const std::filesystem::path& outputDirectory);
        static Result<void> BuildAssetBundleToDirectory(const std::filesystem::path& outputDirectory, Settings settings);

    private:
        static Result<void> BuildAtOutputDirectory(const std::filesystem::path& outputDirectory, Settings settings);
    };
}

