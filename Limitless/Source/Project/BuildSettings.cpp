#include "Project/BuildSettings.h"

#include "Core/Debug/Log.h"

#include <nlohmann/json.hpp>

#include <fstream>

namespace Limitless::Project
{
    using json = nlohmann::json;

    namespace
    {
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
            return BuildSettings{};
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
            out.EngineRootOverride = root.value("engineRootOverride", std::string{});
            out.BuildBackend = SanitizeBuildBackend(root.value("buildBackend", out.BuildBackend));

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
            root["engineRootOverride"] = settings.EngineRootOverride;
            root["buildBackend"] = SanitizeBuildBackend(settings.BuildBackend);

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
            }

            std::error_code renameError;
            std::filesystem::rename(tempPath, settingsPath, renameError);
            if (renameError)
            {
                // Fallback: direct overwrite if rename fails (cross-device, etc.)
                std::filesystem::copy_file(tempPath, settingsPath, std::filesystem::copy_options::overwrite_existing, renameError);
                std::filesystem::remove(tempPath, renameError);
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
}
