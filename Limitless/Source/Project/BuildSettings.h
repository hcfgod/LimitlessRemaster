#pragma once

#include "Core/Error.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Limitless::Project
{
    namespace BuildBackend
    {
        inline constexpr const char* LegacySdk = "LegacySdk";
        inline constexpr const char* InternalToolchain = "InternalToolchain";
    }

    namespace BuildTargetOS
    {
        inline constexpr const char* Windows = "Windows";
        inline constexpr const char* MacOS = "macOS";
        inline constexpr const char* Linux = "Linux";
    }

    namespace BuildTargetArchitecture
    {
        inline constexpr const char* X64 = "x64";
        inline constexpr const char* ARM64 = "ARM64";
    }

    namespace BuildExecutionMode
    {
        inline constexpr const char* Auto = "Auto";
        inline constexpr const char* Local = "Local";
        inline constexpr const char* Remote = "Remote";
    }

    namespace ScriptEditorMode
    {
        inline constexpr const char* Internal = "Internal";
        inline constexpr const char* External = "External";
    }

    namespace ScriptCompileFailurePolicy
    {
        inline constexpr const char* SafeMode = "SafeMode";
        inline constexpr const char* BlockPlay = "BlockPlay";
    }

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

        /// Build configuration for shipped builds (fixed to "Dist").
        std::string BuildConfiguration = "Dist";

        /// Asset bundle compression mode: "None" or "Zstd".
        std::string CompressionMode = "Zstd";

        /// Zstd compression level (1-22, default 3).
        int ZstdCompressionLevel = 3;

        /// Last output directory chosen by the user (remembered across sessions).
        std::string LastOutputDirectory;

        /// Optional project-specific icon path copied into shipped output and
        /// written into Runtime config as `window.icon`.
        /// Supports absolute paths and project-relative paths (e.g. `Assets/Icons/Game.ico`).
        std::string GameWindowIconPath;

        /// Optional override to locate the engine workspace/toolchain root when
        /// the editor is running outside the source workspace.
        std::string EngineRootOverride;

        /// Build backend mode used by editor/game build workflows.
        /// - LegacySdk: existing source-workspace scripts + build paths.
        /// - InternalToolchain: install-relative bundled toolchain layout.
        std::string BuildBackend = BuildBackend::LegacySdk;

        /// Target operating system for game exports.
        std::string TargetOS = BuildTargetOS::Windows;

        /// Target architecture for game exports.
        std::string TargetArchitecture = BuildTargetArchitecture::X64;

        /// Build execution mode:
        /// - Auto: choose local/remote based on host-target pairing.
        /// - Local: build on the editor host.
        /// - Remote: dispatch to remote native build worker.
        std::string ExecutionMode = BuildExecutionMode::Auto;

        /// Default remote build API endpoint fallback (example: http://10.0.0.12:8080).
        std::string RemoteBuildEndpoint;

        /// Route remote requests to target-specific endpoints when available.
        bool UseTargetEndpointRouting = true;

        /// Optional target-specific endpoints used when routing is enabled.
        std::string RemoteBuildEndpointWindows;
        std::string RemoteBuildEndpointMacOS;
        std::string RemoteBuildEndpointLinux;

        /// Optional worker pool label for routing.
        std::string RemoteBuildPool = "default";

        /// Optional auth token used by remote build API.
        std::string RemoteBuildAuthToken;

        /// If remote dispatch fails, optionally attempt a local build fallback.
        bool AllowLocalBuildFallback = true;

        /// Remote orchestration timeout in seconds.
        int RemoteBuildTimeoutSeconds = 1200;

        /// Poll interval for remote job status checks in seconds.
        int RemoteBuildPollIntervalSeconds = 2;

        /// Max request retries for remote orchestration.
        int RemoteBuildMaxRetries = 3;

        /// Native script editor mode used by authoring UI.
        /// - Internal: built-in script editor window.
        /// - External: launch host editor integration (Visual Studio on Windows).
        std::string ScriptEditorMode = ScriptEditorMode::Internal;

        /// Play mode behavior when native script compilation/build has failed.
        /// - SafeMode: enter play with script execution disabled.
        /// - BlockPlay: prevent entering play mode until scripts build successfully.
        std::string ScriptCompileFailurePolicy = ScriptCompileFailurePolicy::SafeMode;
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

    /// Returns the normalized host platform label for build targeting.
    [[nodiscard]] std::string GetHostBuildTargetOS();

    /// Returns the normalized host architecture label for build targeting.
    [[nodiscard]] std::string GetHostBuildTargetArchitecture();

    /// Resolves remote endpoint with target-specific routing + fallback endpoint.
    [[nodiscard]] std::string ResolveRemoteBuildEndpoint(const BuildSettings& settings, const std::string& targetOS);
}
