#include "Project/ProjectDefinition.h"

#include "Core/Debug/Log.h"

#include <nlohmann/json.hpp>

#include <fstream>

namespace Limitless::Project
{
    using json = nlohmann::json;

    std::filesystem::path GetProjectFilePathForRoot(const std::filesystem::path& projectRoot)
    {
        return projectRoot / "Project" / "Project.json";
    }

    static Result<ProjectDefinition> ProjectDefinitionFromJson(const json& root)
    {
        if (!root.is_object())
        {
            return Result<ProjectDefinition>(ErrorCode::FileCorrupted, "ProjectDefinition: root must be an object");
        }

        ProjectDefinition out;
        out.Version = root.value("version", 1u);
        out.ProjectGuid = root.value("projectGuid", std::string{});
        out.ProjectName = root.value("projectName", std::string{});
        out.CreatedUtc = root.value("createdUtc", std::string{});
        out.AssetRootRelative = root.value("assetRootRelative", std::string{"Assets"});
        out.BuildRootRelative = root.value("buildRootRelative", std::string{"Build"});
        out.SettingsVersion = root.value("settingsVersion", 1u);

        if (root.contains("defaultScene") && root["defaultScene"].is_object())
        {
            const auto& ds = root["defaultScene"];
            out.DefaultScene.Guid = ds.value("guid", std::string{});
            out.DefaultScene.Key = ds.value("key", std::string{});
        }

        return out;
    }

    static json ProjectDefinitionToJson(const ProjectDefinition& d)
    {
        json root;
        root["version"] = d.Version;
        root["projectGuid"] = d.ProjectGuid;
        root["projectName"] = d.ProjectName;
        root["createdUtc"] = d.CreatedUtc;
        root["assetRootRelative"] = d.AssetRootRelative;
        root["buildRootRelative"] = d.BuildRootRelative;
        root["settingsVersion"] = d.SettingsVersion;

        json ds;
        ds["guid"] = d.DefaultScene.Guid;
        ds["key"] = d.DefaultScene.Key;
        root["defaultScene"] = std::move(ds);

        return root;
    }

    Result<ProjectDefinition> LoadProjectDefinitionFromFile(const std::filesystem::path& projectFilePath)
    {
        if (projectFilePath.empty())
        {
            return Result<ProjectDefinition>(ErrorCode::InvalidArgument, "LoadProjectDefinitionFromFile: path is empty");
        }

        if (!std::filesystem::exists(projectFilePath))
        {
            return Result<ProjectDefinition>(ErrorCode::FileNotFound, "Project file not found: " + projectFilePath.string());
        }

        try
        {
            std::ifstream in(projectFilePath, std::ios::in | std::ios::binary);
            if (!in.is_open())
            {
                return Result<ProjectDefinition>(ErrorCode::FileAccessDenied, "Failed to open project file: " + projectFilePath.string());
            }

            json root;
            in >> root;
            return ProjectDefinitionFromJson(root);
        }
        catch (const std::exception& e)
        {
            return Result<ProjectDefinition>(ErrorCode::FileCorrupted, std::string("Project file parse error: ") + e.what());
        }
    }

    Result<void> SaveProjectDefinitionToFile(const std::filesystem::path& projectFilePath, const ProjectDefinition& definition)
    {
        if (projectFilePath.empty())
        {
            return Result<void>(ErrorCode::InvalidArgument, "SaveProjectDefinitionToFile: path is empty");
        }

        try
        {
            if (projectFilePath.has_parent_path())
            {
                std::filesystem::create_directories(projectFilePath.parent_path());
            }

            const json root = ProjectDefinitionToJson(definition);

            // Write as a normal text file. This is editor tooling; the file is tiny.
            std::ofstream out(projectFilePath, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!out.is_open())
            {
                return Result<void>(ErrorCode::FileAccessDenied, "Failed to write project file: " + projectFilePath.string());
            }

            out << root.dump(2);
        }
        catch (const std::exception& e)
        {
            LT_CORE_WARN("SaveProjectDefinitionToFile failed: {}", e.what());
            return Result<void>(ErrorCode::FileAccessDenied, std::string("SaveProjectDefinitionToFile failed: ") + e.what());
        }

        return Result<void>();
    }
}

