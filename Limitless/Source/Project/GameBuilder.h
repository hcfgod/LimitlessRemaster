#pragma once

#include "Project/BuildSettings.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Limitless::Project
{
    // -------------------------------------------------------------------------
    // GameBuilder
    //
    // Orchestrates the full "Build Game" pipeline:
    //   1. Validate build scenes and output directory
    //   2. Build asset bundle to output/AssetBundle/
    //   3. Build ScriptCore DLL/SO for the selected configuration
    //   4. Copy Runtime executable (renamed to project name)
    //   5. Copy runtime DLLs (shaderc, ScriptCore) and config.json
    //   6. Write GameBootstrap.json with startup scene and scene list
    // -------------------------------------------------------------------------

    struct GameBuildRequest final
    {
        /// Where the built game files will be placed.
        std::filesystem::path OutputDirectory;

        /// Build settings (scenes, configuration, compression).
        BuildSettings Settings;

        /// The opened project root (e.g. C:/Users/user/MyGame).
        std::filesystem::path ProjectRoot;

        /// The engine workspace root (e.g. C:/Dev/LimitlessRemaster).
        std::filesystem::path EngineRoot;

        /// Project name used for the output executable.
        std::string ProjectName = "Game";
    };

    struct GameBuildResult final
    {
        bool Success = false;
        std::string ErrorMessage;
        std::filesystem::path OutputExecutablePath;
        float ElapsedSeconds = 0.0f;

        /// Per-step status messages for UI display.
        std::vector<std::string> StepLog;
    };

    class GameBuilder final
    {
    public:
        /// Build the game to the output directory. Blocking call.
        static GameBuildResult BuildGame(const GameBuildRequest& request);

        /// Build, then launch the resulting executable. Blocking build, async launch.
        static GameBuildResult BuildAndRunGame(const GameBuildRequest& request);

    private:
        static bool IsInternalBackend(const GameBuildRequest& request);
        static bool ValidateRequest(const GameBuildRequest& request, GameBuildResult& result);
        static bool BuildAssetBundle(const GameBuildRequest& request, GameBuildResult& result);
        static bool BuildScriptCore(const GameBuildRequest& request, GameBuildResult& result);
        static bool CopyRuntimeFiles(const GameBuildRequest& request, GameBuildResult& result);
        static bool WriteGameBootstrap(const GameBuildRequest& request, GameBuildResult& result);
        static void LaunchExecutable(const std::filesystem::path& executablePath);
    };
}
