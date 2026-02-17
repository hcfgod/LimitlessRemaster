#pragma once

#include "Core/Error.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Limitless::Project
{
    // -------------------------------------------------------------------------
    // Build Settings
    //
    // Persisted per-project at <ProjectRoot>/Project/Settings/BuildSettings.json.
    // Stores the scene list, build configuration, compression settings, and the
    // last output directory used by the Build Game workflow.
    // -------------------------------------------------------------------------

    /// A single entry in the ordered "Scenes In Build" list (Unity-style).
    struct BuildSceneEntry final
    {
        /// Asset key of the scene (e.g. "Assets/Scenes/MainMenu.scene.json").
        std::string Key;

        /// Asset GUID for stable reference across renames.
        std::string Guid;

        /// Whether this scene is included in the build.  Disabled scenes are
        /// kept in the list for convenience but skipped during build.
        bool Enabled = true;
    };

    /// Full build settings for a project.
    struct BuildSettings final
    {
        uint32_t Version = 1;

        /// Ordered list of scenes to include in the build.
        /// The first enabled scene is the startup scene.
        std::vector<BuildSceneEntry> BuildScenes;

        /// Build configuration: "Debug", "Release", or "Dist".
        std::string BuildConfiguration = "Release";

        /// Asset bundle compression mode: "None" or "Zstd".
        std::string CompressionMode = "Zstd";

        /// Zstd compression level (1-22, default 3).
        int ZstdCompressionLevel = 3;

        /// Last output directory chosen by the user (remembered across sessions).
        std::string LastOutputDirectory;
    };

    /// Returns the filesystem path for BuildSettings.json.
    [[nodiscard]] std::filesystem::path GetBuildSettingsPath(const std::filesystem::path& projectRoot);

    /// Load build settings from disk. Returns defaults if the file does not exist.
    [[nodiscard]] Result<BuildSettings> LoadBuildSettings(const std::filesystem::path& projectRoot);

    /// Save build settings to disk (atomic write via temp file).
    [[nodiscard]] Result<void> SaveBuildSettings(const std::filesystem::path& projectRoot, const BuildSettings& settings);

    /// Returns the asset key of the first enabled build scene, or empty string if none.
    [[nodiscard]] std::string GetStartupSceneKey(const BuildSettings& settings);

    /// Returns the list of enabled scene keys in build order.
    [[nodiscard]] std::vector<std::string> GetEnabledBuildSceneKeys(const BuildSettings& settings);
}
