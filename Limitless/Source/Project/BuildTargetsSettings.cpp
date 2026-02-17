#include "Project/BuildTargetsSettings.h"

#include "Project/ProjectSettings.h"

#include <nlohmann/json.hpp>

#include <fstream>

namespace Limitless::Project
{
    using json = nlohmann::json;

    std::filesystem::path GetBuildTargetsSettingsPath(const std::filesystem::path& projectRoot)
    {
        return GetProjectSettingsDirectory(projectRoot) / "BuildTargets.json";
    }

    static BuildTargetsSettings DefaultBuildTargets()
    {
        BuildTargetsSettings s;
        s.Version = 1;
        s.Configuration = "Debug";
        s.Platform = "x64";
        s.AutoRunAfterBuild = false;
        s.Targets = {
            {"Editor", "Editor", ""},
            {"Runtime", "Runtime", ""}
        };
        s.ActiveTargetId = "Editor";
        return s;
    }

    static BuildTargetsSettings FromJson(const json& root)
    {
        BuildTargetsSettings s = DefaultBuildTargets();
        if (!root.is_object())
        {
            return s;
        }

        s.Version = root.value("version", 1u);
        s.ActiveTargetId = root.value("activeTargetId", s.ActiveTargetId);
        s.Configuration = root.value("configuration", s.Configuration);
        s.Platform = root.value("platform", s.Platform);
        s.AutoRunAfterBuild = root.value("autoRunAfterBuild", s.AutoRunAfterBuild);

        if (root.contains("targets") && root["targets"].is_array())
        {
            s.Targets.clear();
            for (const auto& t : root["targets"])
            {
                if (!t.is_object())
                {
                    continue;
                }
                BuildTarget bt;
                bt.Id = t.value("id", std::string{});
                bt.ProjectName = t.value("projectName", std::string{});
                bt.Arguments = t.value("arguments", std::string{});
                if (bt.Id.empty() || bt.ProjectName.empty())
                {
                    continue;
                }
                s.Targets.push_back(std::move(bt));
            }
        }

        if (s.Targets.empty())
        {
            s = DefaultBuildTargets();
        }

        // Backward compatibility: migrate legacy "Sandbox" target ids/names.
        if (s.ActiveTargetId == "Sandbox")
            s.ActiveTargetId = "Runtime";

        bool hasRuntimeTarget = false;
        for (auto& target : s.Targets)
        {
            if (target.Id == "Sandbox")
                target.Id = "Runtime";
            if (target.ProjectName == "Sandbox")
                target.ProjectName = "Runtime";
            if (target.Id == "Runtime")
                hasRuntimeTarget = true;
        }

        if (!hasRuntimeTarget)
            s.Targets.push_back({ "Runtime", "Runtime", "" });

        if (s.ActiveTargetId.empty() && !s.Targets.empty())
        {
            s.ActiveTargetId = s.Targets.front().Id;
        }

        return s;
    }

    static json ToJson(const BuildTargetsSettings& s)
    {
        json root;
        root["version"] = s.Version;
        root["activeTargetId"] = s.ActiveTargetId;
        root["configuration"] = s.Configuration;
        root["platform"] = s.Platform;
        root["autoRunAfterBuild"] = s.AutoRunAfterBuild;

        root["targets"] = json::array();
        for (const auto& t : s.Targets)
        {
            json j;
            j["id"] = t.Id;
            j["projectName"] = t.ProjectName;
            j["arguments"] = t.Arguments;
            root["targets"].push_back(std::move(j));
        }

        return root;
    }

    static Result<json> TryReadJson(const std::filesystem::path& path)
    {
        if (path.empty() || !std::filesystem::exists(path))
        {
            return json::object();
        }

        try
        {
            std::ifstream in(path, std::ios::in | std::ios::binary);
            if (!in.is_open())
            {
                return Result<json>(ErrorCode::FileAccessDenied, "Failed to open BuildTargets settings: " + path.string());
            }
            json root;
            in >> root;
            return root;
        }
        catch (const std::exception& e)
        {
            return Result<json>(ErrorCode::FileCorrupted, std::string("Failed to parse BuildTargets settings: ") + e.what());
        }
    }

    static Result<void> AtomicWriteJson(const std::filesystem::path& path, const json& root)
    {
        if (path.empty())
        {
            return Result<void>(ErrorCode::InvalidArgument, "AtomicWriteJson: path is empty");
        }

        try
        {
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);

            const std::filesystem::path tmp = path.string() + ".tmp";
            {
                std::ofstream out(tmp, std::ios::out | std::ios::binary | std::ios::trunc);
                if (!out.is_open())
                {
                    return Result<void>(ErrorCode::FileAccessDenied, "Failed to write BuildTargets temp file: " + tmp.string());
                }
                out << root.dump(2);
                out.flush();
            }

            std::filesystem::rename(tmp, path, ec);
            if (ec)
            {
                ec.clear();
                std::filesystem::remove(path, ec);
                ec.clear();
                std::filesystem::rename(tmp, path, ec);
                if (ec)
                {
                    return Result<void>(ErrorCode::FileAccessDenied, "Failed to replace BuildTargets file: " + ec.message());
                }
            }
        }
        catch (const std::exception& e)
        {
            return Result<void>(ErrorCode::FileAccessDenied, std::string("AtomicWriteJson failed: ") + e.what());
        }

        return Result<void>();
    }

    Result<BuildTargetsSettings> LoadBuildTargetsSettings(const std::filesystem::path& projectRoot)
    {
        const auto root = TryReadJson(GetBuildTargetsSettingsPath(projectRoot));
        if (root.IsFailure())
        {
            return Result<BuildTargetsSettings>(root.GetError());
        }

        if (!root.GetValue().is_object() || root.GetValue().empty())
        {
            return DefaultBuildTargets();
        }

        return FromJson(root.GetValue());
    }

    Result<void> SaveBuildTargetsSettings(const std::filesystem::path& projectRoot, const BuildTargetsSettings& settings)
    {
        return AtomicWriteJson(GetBuildTargetsSettingsPath(projectRoot), ToJson(settings));
    }
}

