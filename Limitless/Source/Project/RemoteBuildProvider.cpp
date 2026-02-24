#include "Project/RemoteBuildProvider.h"

#include "Core/Debug/Log.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

namespace Limitless::Project
{
    namespace
    {
        using json = nlohmann::json;

        std::string EscapeCommandPath(const std::filesystem::path& path)
        {
            return "\"" + path.string() + "\"";
        }

        std::string ReadTextFile(const std::filesystem::path& filePath)
        {
            std::ifstream input(filePath, std::ios::in | std::ios::binary);
            if (!input.is_open())
                return {};

            return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        }

        std::filesystem::path CreateStagingDirectory(GameBuildResult& result)
        {
            std::error_code errorCode;
            const std::filesystem::path tempRoot = std::filesystem::temp_directory_path(errorCode);
            if (errorCode)
            {
                result.ErrorMessage = "Failed resolving temp directory for remote build staging: " + errorCode.message();
                return {};
            }

            const std::uint64_t stamp =
                static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
            const std::filesystem::path stagingRoot =
                tempRoot / "LimitlessRemoteBuild" / ("job-" + std::to_string(stamp));

            std::filesystem::create_directories(stagingRoot, errorCode);
            if (errorCode)
            {
                result.ErrorMessage = "Failed creating remote build staging directory '" + stagingRoot.string()
                    + "': " + errorCode.message();
                return {};
            }

            return stagingRoot;
        }

        bool ParseManifestFile(const std::filesystem::path& manifestPath,
                               RemoteBuildArtifactManifest& manifest,
                               GameBuildResult& result)
        {
            if (!std::filesystem::exists(manifestPath))
            {
                result.ErrorMessage = "Remote build manifest not found: " + manifestPath.string();
                return false;
            }

            try
            {
                std::ifstream input(manifestPath, std::ios::in | std::ios::binary);
                if (!input.is_open())
                {
                    result.ErrorMessage = "Failed to open remote build manifest: " + manifestPath.string();
                    return false;
                }

                json root;
                input >> root;

                manifest.RuntimeDirectory = root.value("runtimeDirectory", std::string{});
                manifest.ScriptCoreLibraryPath = root.value("scriptCoreLibraryPath", std::string{});
                if (root.contains("dynamicLibraryDirectories") && root["dynamicLibraryDirectories"].is_array())
                {
                    for (const auto& item : root["dynamicLibraryDirectories"])
                    {
                        if (!item.is_string())
                            continue;
                        manifest.DynamicLibraryDirectories.emplace_back(item.get<std::string>());
                    }
                }
            }
            catch (const std::exception& e)
            {
                result.ErrorMessage = std::string("Failed parsing remote build manifest: ") + e.what();
                return false;
            }

            if (manifest.RuntimeDirectory.empty() || !std::filesystem::is_directory(manifest.RuntimeDirectory))
            {
                result.ErrorMessage = "Remote build runtime directory is missing or invalid in manifest.";
                return false;
            }

            if (manifest.ScriptCoreLibraryPath.empty() || !std::filesystem::exists(manifest.ScriptCoreLibraryPath))
            {
                result.ErrorMessage = "Remote build ScriptCore library is missing or invalid in manifest.";
                return false;
            }

            if (manifest.DynamicLibraryDirectories.empty())
                manifest.DynamicLibraryDirectories.push_back(manifest.RuntimeDirectory);

            return true;
        }
    }

    bool FetchRemoteBuildArtifacts(const GameBuildRequest& request,
                                   const std::filesystem::path& generatedScriptsDirectory,
                                   RemoteBuildArtifactManifest& manifest,
                                   GameBuildResult& result)
    {
        manifest = {};
        const std::string targetOS = request.Settings.TargetOS.empty()
            ? GetHostBuildTargetOS()
            : request.Settings.TargetOS;
        const std::string targetArchitecture = request.Settings.TargetArchitecture.empty()
            ? GetHostBuildTargetArchitecture()
            : request.Settings.TargetArchitecture;
        const std::string remoteEndpoint = ResolveRemoteBuildEndpoint(request.Settings, targetOS);
        if (remoteEndpoint.empty())
        {
            result.ErrorMessage = "Remote build endpoint is empty for target OS '" + targetOS + "'.";
            return false;
        }

        if (!std::filesystem::is_directory(generatedScriptsDirectory))
        {
            result.ErrorMessage = "Generated script mirror not found for remote build: " + generatedScriptsDirectory.string();
            return false;
        }

        const std::filesystem::path scriptPath = request.EngineRoot / "Scripts" / "remote_build_client.py";
        if (!std::filesystem::exists(scriptPath))
        {
            result.ErrorMessage = "Remote build client script not found: " + scriptPath.string();
            return false;
        }

        manifest.StagingRoot = CreateStagingDirectory(result);
        if (manifest.StagingRoot.empty())
            return false;

        const std::filesystem::path outputDirectory = manifest.StagingRoot / "client-output";
        std::error_code createDirError;
        std::filesystem::create_directories(outputDirectory, createDirError);
        if (createDirError)
        {
            result.ErrorMessage = "Failed creating remote build client output directory: " + createDirError.message();
            return false;
        }

        const std::filesystem::path logPath = outputDirectory / "remote-build-client.log";

        std::string command;
#if defined(LT_PLATFORM_WINDOWS)
        command = "cd /d " + EscapeCommandPath(request.EngineRoot) + " && python "
            + EscapeCommandPath(scriptPath)
            + " --endpoint " + EscapeCommandPath(remoteEndpoint)
            + " --target-os " + EscapeCommandPath(targetOS)
            + " --target-arch " + EscapeCommandPath(targetArchitecture)
            + " --config " + EscapeCommandPath(request.Settings.BuildConfiguration)
            + " --build-backend " + EscapeCommandPath(request.Settings.BuildBackend)
            + " --generated-scripts-dir " + EscapeCommandPath(generatedScriptsDirectory)
            + " --output-dir " + EscapeCommandPath(outputDirectory)
            + " --pool " + EscapeCommandPath(request.Settings.RemoteBuildPool)
            + " --timeout-seconds " + std::to_string(request.Settings.RemoteBuildTimeoutSeconds)
            + " --poll-interval-seconds " + std::to_string(request.Settings.RemoteBuildPollIntervalSeconds)
            + " --max-retries " + std::to_string(request.Settings.RemoteBuildMaxRetries);
#else
        command = "cd " + EscapeCommandPath(request.EngineRoot) + " && python3 "
            + EscapeCommandPath(scriptPath)
            + " --endpoint " + EscapeCommandPath(remoteEndpoint)
            + " --target-os " + EscapeCommandPath(targetOS)
            + " --target-arch " + EscapeCommandPath(targetArchitecture)
            + " --config " + EscapeCommandPath(request.Settings.BuildConfiguration)
            + " --build-backend " + EscapeCommandPath(request.Settings.BuildBackend)
            + " --generated-scripts-dir " + EscapeCommandPath(generatedScriptsDirectory)
            + " --output-dir " + EscapeCommandPath(outputDirectory)
            + " --pool " + EscapeCommandPath(request.Settings.RemoteBuildPool)
            + " --timeout-seconds " + std::to_string(request.Settings.RemoteBuildTimeoutSeconds)
            + " --poll-interval-seconds " + std::to_string(request.Settings.RemoteBuildPollIntervalSeconds)
            + " --max-retries " + std::to_string(request.Settings.RemoteBuildMaxRetries);
#endif
        if (!request.Settings.RemoteBuildAuthToken.empty())
            command += " --auth-token " + EscapeCommandPath(request.Settings.RemoteBuildAuthToken);
        command += " > " + EscapeCommandPath(logPath) + " 2>&1";

        result.StepLog.push_back("Dispatching remote build to: " + remoteEndpoint);
        const int exitCode = std::system(command.c_str());
        const std::string clientLog = ReadTextFile(logPath);
        if (!clientLog.empty())
        {
            std::istringstream lineStream(clientLog);
            std::string line;
            while (std::getline(lineStream, line))
            {
                if (!line.empty())
                    result.StepLog.push_back("[remote] " + line);
            }
        }

        if (exitCode != 0)
        {
            result.ErrorMessage = "Remote build client failed (exit code " + std::to_string(exitCode) + ").";
            return false;
        }

        const std::filesystem::path manifestPath = outputDirectory / "remote_artifact_manifest.json";
        if (!ParseManifestFile(manifestPath, manifest, result))
            return false;

        result.StepLog.push_back("Remote artifact staging ready: " + manifest.RuntimeDirectory.string());
        return true;
    }
}
