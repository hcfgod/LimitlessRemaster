#include "Project/ProjectSettings.h"

#include "Core/Debug/Log.h"

#include <nlohmann/json.hpp>

#include <fstream>

namespace Limitless::Project
{
    using json = nlohmann::json;

    std::filesystem::path GetProjectSettingsDirectory(const std::filesystem::path& projectRoot)
    {
        return projectRoot / "Project" / "Settings";
    }

    std::filesystem::path GetRenderSettingsPath(const std::filesystem::path& projectRoot)
    {
        return GetProjectSettingsDirectory(projectRoot) / "RenderSettings.json";
    }

    std::filesystem::path GetAudioSettingsPath(const std::filesystem::path& projectRoot)
    {
        return GetProjectSettingsDirectory(projectRoot) / "AudioSettings.json";
    }

    std::filesystem::path GetInputSettingsPath(const std::filesystem::path& projectRoot)
    {
        return GetProjectSettingsDirectory(projectRoot) / "InputSettings.json";
    }

    std::filesystem::path GetLayersSettingsPath(const std::filesystem::path& projectRoot)
    {
        return GetProjectSettingsDirectory(projectRoot) / "Layers.json";
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
                return Result<json>(ErrorCode::FileAccessDenied, "Failed to open settings file: " + path.string());
            }

            json root;
            in >> root;
            return root;
        }
        catch (const std::exception& e)
        {
            return Result<json>(ErrorCode::FileCorrupted, std::string("Failed to parse settings json: ") + e.what());
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
                    return Result<void>(ErrorCode::FileAccessDenied, "Failed to write settings temp file: " + tmp.string());
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
                    return Result<void>(ErrorCode::FileAccessDenied, "Failed to replace settings file: " + ec.message());
                }
            }
        }
        catch (const std::exception& e)
        {
            return Result<void>(ErrorCode::FileAccessDenied, std::string("AtomicWriteJson failed: ") + e.what());
        }

        return Result<void>();
    }

    static json RenderSettingsToJson(const RenderSettings& s)
    {
        json root;
        root["version"] = s.Version;
        root["vSync"] = s.VSync;
        root["msaaSamples"] = s.MsaaSamples;
        root["renderScale"] = s.RenderScale;
        root["clearColor"] = {s.ClearColor[0], s.ClearColor[1], s.ClearColor[2], s.ClearColor[3]};
        return root;
    }

    static RenderSettings RenderSettingsFromJson(const json& root)
    {
        RenderSettings s;
        if (!root.is_object())
        {
            return s;
        }

        s.Version = root.value("version", 1u);
        s.VSync = root.value("vSync", true);
        s.MsaaSamples = root.value("msaaSamples", 1);
        s.RenderScale = root.value("renderScale", 1.0f);

        if (root.contains("clearColor") && root["clearColor"].is_array())
        {
            const auto& c = root["clearColor"];
            for (int i = 0; i < 4 && i < static_cast<int>(c.size()); ++i)
            {
                if (c[i].is_number())
                {
                    s.ClearColor[i] = c[i].get<float>();
                }
            }
        }

        return s;
    }

    static json AudioSettingsToJson(const AudioSettings& s)
    {
        json root;
        root["version"] = s.Version;
        root["masterVolume"] = s.MasterVolume;
        root["muted"] = s.Muted;
        return root;
    }

    static AudioSettings AudioSettingsFromJson(const json& root)
    {
        AudioSettings s;
        if (!root.is_object())
        {
            return s;
        }

        s.Version = root.value("version", 1u);
        s.MasterVolume = root.value("masterVolume", 1.0f);
        s.Muted = root.value("muted", false);
        return s;
    }

    static json InputSettingsToJson(const InputSettings& s)
    {
        json root;
        root["version"] = s.Version;
        root["projectInputActionsKey"] = s.ProjectInputActionsKey;
        return root;
    }

    static InputSettings InputSettingsFromJson(const json& root)
    {
        InputSettings s;
        if (!root.is_object())
        {
            return s;
        }

        s.Version = root.value("version", 1u);
        s.ProjectInputActionsKey = root.value("projectInputActionsKey", std::string{});
        return s;
    }

    static json LayersSettingsToJson(const LayersSettings& s)
    {
        json root;
        root["version"] = s.Version;
        root["layers"] = json::array();
        for (const auto& name : s.Layers)
        {
            root["layers"].push_back(name);
        }
        return root;
    }

    static LayersSettings LayersSettingsFromJson(const json& root)
    {
        LayersSettings s;
        if (!root.is_object())
        {
            return s;
        }

        s.Version = root.value("version", 1u);
        if (root.contains("layers") && root["layers"].is_array())
        {
            for (const auto& layer : root["layers"])
            {
                if (layer.is_string())
                {
                    s.Layers.push_back(layer.get<std::string>());
                }
            }
        }

        if (s.Layers.empty())
        {
            s.Layers = {"Default"};
        }

        return s;
    }

    Result<RenderSettings> LoadRenderSettings(const std::filesystem::path& projectRoot)
    {
        const auto root = TryReadJson(GetRenderSettingsPath(projectRoot));
        if (root.IsFailure())
        {
            return Result<RenderSettings>(root.GetError());
        }
        return RenderSettingsFromJson(root.GetValue());
    }

    Result<AudioSettings> LoadAudioSettings(const std::filesystem::path& projectRoot)
    {
        const auto root = TryReadJson(GetAudioSettingsPath(projectRoot));
        if (root.IsFailure())
        {
            return Result<AudioSettings>(root.GetError());
        }
        return AudioSettingsFromJson(root.GetValue());
    }

    Result<InputSettings> LoadInputSettings(const std::filesystem::path& projectRoot)
    {
        const auto root = TryReadJson(GetInputSettingsPath(projectRoot));
        if (root.IsFailure())
        {
            return Result<InputSettings>(root.GetError());
        }
        return InputSettingsFromJson(root.GetValue());
    }

    Result<LayersSettings> LoadLayersSettings(const std::filesystem::path& projectRoot)
    {
        const auto root = TryReadJson(GetLayersSettingsPath(projectRoot));
        if (root.IsFailure())
        {
            return Result<LayersSettings>(root.GetError());
        }
        return LayersSettingsFromJson(root.GetValue());
    }

    Result<void> SaveRenderSettings(const std::filesystem::path& projectRoot, const RenderSettings& settings)
    {
        return AtomicWriteJson(GetRenderSettingsPath(projectRoot), RenderSettingsToJson(settings));
    }

    Result<void> SaveAudioSettings(const std::filesystem::path& projectRoot, const AudioSettings& settings)
    {
        return AtomicWriteJson(GetAudioSettingsPath(projectRoot), AudioSettingsToJson(settings));
    }

    Result<void> SaveInputSettings(const std::filesystem::path& projectRoot, const InputSettings& settings)
    {
        return AtomicWriteJson(GetInputSettingsPath(projectRoot), InputSettingsToJson(settings));
    }

    Result<void> SaveLayersSettings(const std::filesystem::path& projectRoot, const LayersSettings& settings)
    {
        return AtomicWriteJson(GetLayersSettingsPath(projectRoot), LayersSettingsToJson(settings));
    }
}

