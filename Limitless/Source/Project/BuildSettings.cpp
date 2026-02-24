#include "Project/BuildSettings.h"

#include "Core/Debug/Log.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>

namespace Limitless::Project
{
    using json = nlohmann::json;

    namespace
    {
        std::string TrimCopy(std::string value)
        {
            auto isWhitespace = [](unsigned char character)
            {
                return std::isspace(character) != 0;
            };

            value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](char character) {
                            return !isWhitespace(static_cast<unsigned char>(character));
                        }));
            value.erase(std::find_if(value.rbegin(), value.rend(), [&](char character) {
                            return !isWhitespace(static_cast<unsigned char>(character));
                        }).base(),
                        value.end());
            return value;
        }

        std::string NormalizeLower(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return value;
        }

        std::string SanitizeBuildConfiguration(std::string /*configuration*/)
        {
            // Build-game workflow now always ships Dist binaries.
            return "Dist";
        }

        std::string SanitizeBuildBackend(std::string backend)
        {
            if (backend == BuildBackend::LegacySdk || backend == BuildBackend::InternalToolchain)
                return backend;
            return BuildBackend::LegacySdk;
        }

        std::string SanitizeTargetOS(std::string targetOS)
        {
            const std::string normalized = NormalizeLower(TrimCopy(std::move(targetOS)));
            if (normalized == "windows" || normalized == "win")
                return BuildTargetOS::Windows;
            if (normalized == "macos" || normalized == "macosx" || normalized == "darwin")
                return BuildTargetOS::MacOS;
            if (normalized == "linux")
                return BuildTargetOS::Linux;
            return GetHostBuildTargetOS();
        }

        std::string SanitizeTargetArchitecture(std::string architecture)
        {
            const std::string normalized = NormalizeLower(TrimCopy(std::move(architecture)));
            if (normalized == "x64" || normalized == "amd64" || normalized == "x86_64")
                return BuildTargetArchitecture::X64;
            if (normalized == "arm64" || normalized == "aarch64")
                return BuildTargetArchitecture::ARM64;
            return GetHostBuildTargetArchitecture();
        }

        std::string SanitizeExecutionMode(std::string mode)
        {
            if (mode == BuildExecutionMode::Auto ||
                mode == BuildExecutionMode::Local ||
                mode == BuildExecutionMode::Remote)
            {
                return mode;
            }
            return BuildExecutionMode::Auto;
        }

        int ClampInt(const int value, const int minValue, const int maxValue)
        {
            return std::max(minValue, std::min(maxValue, value));
        }

        std::string SanitizeScriptEditorMode(std::string mode)
        {
            if (mode == ScriptEditorMode::Internal || mode == ScriptEditorMode::External)
                return mode;
            return ScriptEditorMode::Internal;
        }

        std::string SanitizeScriptCompileFailurePolicy(std::string policy)
        {
            if (policy == ScriptCompileFailurePolicy::SafeMode ||
                policy == ScriptCompileFailurePolicy::BlockPlay)
            {
                return policy;
            }
            return ScriptCompileFailurePolicy::SafeMode;
        }
    }

    std::filesystem::path GetBuildSettingsPath(const std::filesystem::path& projectRoot)
    {
        return projectRoot / "Project" / "Settings" / "BuildSettings.json";
    }

    Result<BuildSettings> LoadBuildSettings(const std::filesystem::path& projectRoot)
    {
        const auto settingsPath = GetBuildSettingsPath(projectRoot);
        if (!std::filesystem::exists(settingsPath))
        {
            // No settings file yet -- return defaults (not an error).
            BuildSettings defaults;
            defaults.TargetOS = GetHostBuildTargetOS();
            defaults.TargetArchitecture = GetHostBuildTargetArchitecture();
            return defaults;
        }

        try
        {
            std::ifstream inputStream(settingsPath, std::ios::in | std::ios::binary);
            if (!inputStream.is_open())
                return Result<BuildSettings>(ErrorCode::FileAccessDenied, "Failed to open BuildSettings.json: " + settingsPath.string());

            json root;
            inputStream >> root;

            BuildSettings out;
            out.Version = root.value("version", 1u);
            out.BuildConfiguration = SanitizeBuildConfiguration(root.value("buildConfiguration", std::string{"Dist"}));
            out.CompressionMode = root.value("compressionMode", std::string{"Zstd"});
            out.ZstdCompressionLevel = root.value("zstdCompressionLevel", 3);
            out.LastOutputDirectory = root.value("lastOutputDirectory", std::string{});
            out.GameWindowIconPath = root.value("gameWindowIconPath", std::string{});
            out.EngineRootOverride = root.value("engineRootOverride", std::string{});
            out.BuildBackend = SanitizeBuildBackend(root.value("buildBackend", out.BuildBackend));
            out.TargetOS = SanitizeTargetOS(root.value("targetOS", out.TargetOS));
            out.TargetArchitecture = SanitizeTargetArchitecture(root.value("targetArchitecture", out.TargetArchitecture));
            out.ExecutionMode = SanitizeExecutionMode(root.value("executionMode", out.ExecutionMode));
            out.RemoteBuildEndpoint = TrimCopy(root.value("remoteBuildEndpoint", out.RemoteBuildEndpoint));
            out.UseTargetEndpointRouting = root.value("useTargetEndpointRouting", out.UseTargetEndpointRouting);
            out.RemoteBuildEndpointWindows = TrimCopy(
                root.value("remoteBuildEndpointWindows", out.RemoteBuildEndpointWindows));
            out.RemoteBuildEndpointMacOS = TrimCopy(
                root.value("remoteBuildEndpointMacOS", out.RemoteBuildEndpointMacOS));
            out.RemoteBuildEndpointLinux = TrimCopy(
                root.value("remoteBuildEndpointLinux", out.RemoteBuildEndpointLinux));
            out.RemoteBuildPool = TrimCopy(root.value("remoteBuildPool", out.RemoteBuildPool));
            if (out.RemoteBuildPool.empty())
                out.RemoteBuildPool = "default";
            out.RemoteBuildAuthToken = root.value("remoteBuildAuthToken", out.RemoteBuildAuthToken);
            out.AllowLocalBuildFallback = root.value("allowLocalBuildFallback", out.AllowLocalBuildFallback);
            out.RemoteBuildTimeoutSeconds =
                ClampInt(root.value("remoteBuildTimeoutSeconds", out.RemoteBuildTimeoutSeconds), 30, 7200);
            out.RemoteBuildPollIntervalSeconds =
                ClampInt(root.value("remoteBuildPollIntervalSeconds", out.RemoteBuildPollIntervalSeconds), 1, 60);
            out.RemoteBuildMaxRetries =
                ClampInt(root.value("remoteBuildMaxRetries", out.RemoteBuildMaxRetries), 0, 10);
            out.ScriptEditorMode = SanitizeScriptEditorMode(root.value("scriptEditorMode", out.ScriptEditorMode));
            out.ScriptCompileFailurePolicy = SanitizeScriptCompileFailurePolicy(
                root.value("scriptCompileFailurePolicy", out.ScriptCompileFailurePolicy));

            if (root.contains("buildScenes") && root["buildScenes"].is_array())
            {
                for (const auto& sceneEntry : root["buildScenes"])
                {
                    BuildSceneEntry entry;
                    entry.Key = sceneEntry.value("key", std::string{});
                    entry.Guid = sceneEntry.value("guid", std::string{});
                    entry.Enabled = sceneEntry.value("enabled", true);
                    if (!entry.Key.empty())
                        out.BuildScenes.push_back(std::move(entry));
                }
            }

            return out;
        }
        catch (const std::exception& e)
        {
            return Result<BuildSettings>(ErrorCode::FileCorrupted, std::string("BuildSettings parse error: ") + e.what());
        }
    }

    Result<void> SaveBuildSettings(const std::filesystem::path& projectRoot, const BuildSettings& settings)
    {
        const auto settingsPath = GetBuildSettingsPath(projectRoot);

        try
        {
            if (settingsPath.has_parent_path())
                std::filesystem::create_directories(settingsPath.parent_path());

            json root;
            root["version"] = settings.Version;
            root["buildConfiguration"] = SanitizeBuildConfiguration(settings.BuildConfiguration);
            root["compressionMode"] = settings.CompressionMode;
            root["zstdCompressionLevel"] = settings.ZstdCompressionLevel;
            root["lastOutputDirectory"] = settings.LastOutputDirectory;
            root["gameWindowIconPath"] = settings.GameWindowIconPath;
            root["engineRootOverride"] = settings.EngineRootOverride;
            root["buildBackend"] = SanitizeBuildBackend(settings.BuildBackend);
            root["targetOS"] = SanitizeTargetOS(settings.TargetOS);
            root["targetArchitecture"] = SanitizeTargetArchitecture(settings.TargetArchitecture);
            root["executionMode"] = SanitizeExecutionMode(settings.ExecutionMode);
            root["remoteBuildEndpoint"] = TrimCopy(settings.RemoteBuildEndpoint);
            root["useTargetEndpointRouting"] = settings.UseTargetEndpointRouting;
            root["remoteBuildEndpointWindows"] = TrimCopy(settings.RemoteBuildEndpointWindows);
            root["remoteBuildEndpointMacOS"] = TrimCopy(settings.RemoteBuildEndpointMacOS);
            root["remoteBuildEndpointLinux"] = TrimCopy(settings.RemoteBuildEndpointLinux);
            root["remoteBuildPool"] = settings.RemoteBuildPool.empty() ? "default" : TrimCopy(settings.RemoteBuildPool);
            root["remoteBuildAuthToken"] = settings.RemoteBuildAuthToken;
            root["allowLocalBuildFallback"] = settings.AllowLocalBuildFallback;
            root["remoteBuildTimeoutSeconds"] = ClampInt(settings.RemoteBuildTimeoutSeconds, 30, 7200);
            root["remoteBuildPollIntervalSeconds"] = ClampInt(settings.RemoteBuildPollIntervalSeconds, 1, 60);
            root["remoteBuildMaxRetries"] = ClampInt(settings.RemoteBuildMaxRetries, 0, 10);
            root["scriptEditorMode"] = SanitizeScriptEditorMode(settings.ScriptEditorMode);
            root["scriptCompileFailurePolicy"] =
                SanitizeScriptCompileFailurePolicy(settings.ScriptCompileFailurePolicy);

            json scenesArray = json::array();
            for (const auto& entry : settings.BuildScenes)
            {
                json sceneObject;
                sceneObject["key"] = entry.Key;
                sceneObject["guid"] = entry.Guid;
                sceneObject["enabled"] = entry.Enabled;
                scenesArray.push_back(std::move(sceneObject));
            }
            root["buildScenes"] = std::move(scenesArray);

            // Atomic write: write to temp, then rename.
            const auto tempPath = settingsPath.string() + ".tmp";
            {
                std::ofstream outputStream(tempPath, std::ios::out | std::ios::binary | std::ios::trunc);
                if (!outputStream.is_open())
                    return Result<void>(ErrorCode::FileAccessDenied, "Failed to write BuildSettings temp file.");
                outputStream << root.dump(2);
                if (!outputStream.good())
                {
                    std::error_code cleanupError;
                    std::filesystem::remove(tempPath, cleanupError);
                    return Result<void>(ErrorCode::FileAccessDenied, "Failed writing BuildSettings temp file contents.");
                }
            }

            std::error_code renameError;
            std::filesystem::rename(tempPath, settingsPath, renameError);
            if (renameError)
            {
                // Fallback: direct overwrite if rename fails (cross-device, etc.)
                std::error_code copyError;
                std::filesystem::copy_file(tempPath, settingsPath, std::filesystem::copy_options::overwrite_existing, copyError);
                if (copyError)
                {
                    std::error_code cleanupError;
                    std::filesystem::remove(tempPath, cleanupError);
                    return Result<void>(
                        ErrorCode::FileAccessDenied,
                        "Failed to finalize BuildSettings save. Rename failed: " + renameError.message()
                        + "; fallback copy failed: " + copyError.message());
                }

                std::error_code cleanupError;
                std::filesystem::remove(tempPath, cleanupError);
                if (cleanupError)
                    LT_CORE_WARN("SaveBuildSettings: copied fallback succeeded but failed to remove temp file '{}': {}",
                                 tempPath,
                                 cleanupError.message());
            }
        }
        catch (const std::exception& e)
        {
            LT_CORE_WARN("SaveBuildSettings failed: {}", e.what());
            return Result<void>(ErrorCode::FileAccessDenied, std::string("SaveBuildSettings failed: ") + e.what());
        }

        return Result<void>();
    }

    std::string GetStartupSceneKey(const BuildSettings& settings)
    {
        for (const auto& entry : settings.BuildScenes)
        {
            if (entry.Enabled && !entry.Key.empty())
                return entry.Key;
        }
        return {};
    }

    std::vector<std::string> GetEnabledBuildSceneKeys(const BuildSettings& settings)
    {
        std::vector<std::string> result;
        result.reserve(settings.BuildScenes.size());
        for (const auto& entry : settings.BuildScenes)
        {
            if (entry.Enabled && !entry.Key.empty())
                result.push_back(entry.Key);
        }
        return result;
    }

    std::string GetHostBuildTargetOS()
    {
#if defined(LT_PLATFORM_WINDOWS)
        return BuildTargetOS::Windows;
#elif defined(LT_PLATFORM_MACOS)
        return BuildTargetOS::MacOS;
#else
        return BuildTargetOS::Linux;
#endif
    }

    std::string GetHostBuildTargetArchitecture()
    {
#if defined(LT_ARCHITECTURE_ARM64)
        return BuildTargetArchitecture::ARM64;
#else
        return BuildTargetArchitecture::X64;
#endif
    }

    std::string ResolveRemoteBuildEndpoint(const BuildSettings& settings, const std::string& targetOS)
    {
        if (settings.UseTargetEndpointRouting)
        {
            if (targetOS == BuildTargetOS::Windows && !settings.RemoteBuildEndpointWindows.empty())
                return settings.RemoteBuildEndpointWindows;
            if (targetOS == BuildTargetOS::MacOS && !settings.RemoteBuildEndpointMacOS.empty())
                return settings.RemoteBuildEndpointMacOS;
            if (targetOS == BuildTargetOS::Linux && !settings.RemoteBuildEndpointLinux.empty())
                return settings.RemoteBuildEndpointLinux;
        }

        return settings.RemoteBuildEndpoint;
    }
}
