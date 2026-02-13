#include "Project/ProjectSettings.h"

#include "Core/Debug/Log.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <set>

namespace Limitless::Project
{
    using json = nlohmann::json;
    namespace
    {
        constexpr const char* kInputActionsSuffix = ".inputactions.json";

        std::string TrimCopy(const std::string& value)
        {
            size_t begin = 0;
            while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])))
                ++begin;

            size_t end = value.size();
            while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])))
                --end;

            return value.substr(begin, end - begin);
        }

        std::string CanonicalizeAlias(const std::string& alias)
        {
            std::string normalized = TrimCopy(alias);
            std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
            return normalized;
        }

        bool EndsWithCaseInsensitive(const std::string& value, const std::string& suffix)
        {
            if (value.size() < suffix.size())
                return false;
            const size_t offset = value.size() - suffix.size();
            for (size_t index = 0; index < suffix.size(); ++index)
            {
                const unsigned char left = static_cast<unsigned char>(value[offset + index]);
                const unsigned char right = static_cast<unsigned char>(suffix[index]);
                if (std::tolower(left) != std::tolower(right))
                    return false;
            }
            return true;
        }

        std::string DefaultAliasFromInputActionsKey(const std::string& key)
        {
            std::filesystem::path path(key);
            const std::string fileName = path.filename().string();
            if (fileName.empty())
                return "InputActions";
            if (EndsWithCaseInsensitive(fileName, kInputActionsSuffix))
                return fileName.substr(0, fileName.size() - std::char_traits<char>::length(kInputActionsSuffix));
            return path.stem().string();
        }
    }

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
        root["additionalInputActionsAssets"] = json::array();
        for (const auto& entry : s.AdditionalInputActionsAssets)
        {
            if (entry.AssetKey.empty())
                continue;
            json entryJson = json::object();
            entryJson["alias"] = entry.Alias;
            entryJson["key"] = entry.AssetKey;
            root["additionalInputActionsAssets"].push_back(std::move(entryJson));
        }

        // Keep writing legacy list for compatibility with older editor/runtime revisions.
        root["additionalInputActionsKeys"] = json::array();
        for (const auto& key : CollectAdditionalInputActionsAssetKeys(s))
        {
            if (!key.empty())
                root["additionalInputActionsKeys"].push_back(key);
        }
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
        if (root.contains("additionalInputActionsAssets") && root["additionalInputActionsAssets"].is_array())
        {
            for (const auto& value : root["additionalInputActionsAssets"])
            {
                if (!value.is_object())
                    continue;

                InputActionsAssetAliasEntry entry{};
                entry.Alias = value.value("alias", std::string{});
                entry.AssetKey = value.value("key", std::string{});
                entry.Alias = TrimCopy(entry.Alias);
                entry.AssetKey = TrimCopy(entry.AssetKey);
                if (!entry.AssetKey.empty())
                    s.AdditionalInputActionsAssets.push_back(std::move(entry));
            }
        }

        if (root.contains("additionalInputActionsKeys") && root["additionalInputActionsKeys"].is_array())
        {
            for (const auto& value : root["additionalInputActionsKeys"])
            {
                if (!value.is_string())
                    continue;

                const std::string key = value.get<std::string>();
                if (!key.empty())
                    s.AdditionalInputActionsKeys.push_back(key);
            }
        }

        // Merge and sanitize aliases/keys from both canonical and legacy representations.
        std::set<std::string> usedAliases;
        std::set<std::string> usedKeys;
        std::vector<InputActionsAssetAliasEntry> normalizedEntries;

        auto tryAppendEntry = [&](InputActionsAssetAliasEntry entry) {
            entry.AssetKey = TrimCopy(entry.AssetKey);
            if (entry.AssetKey.empty())
                return;
            const std::string canonicalKey = entry.AssetKey;
            if (usedKeys.find(canonicalKey) != usedKeys.end())
                return;

            entry.Alias = TrimCopy(entry.Alias);
            if (entry.Alias.empty())
                entry.Alias = DefaultAliasFromInputActionsKey(entry.AssetKey);

            std::string candidateAlias = entry.Alias;
            std::string canonicalAlias = CanonicalizeAlias(candidateAlias);
            int32_t suffixIndex = 2;
            while (canonicalAlias.empty() || usedAliases.find(canonicalAlias) != usedAliases.end())
            {
                candidateAlias = entry.Alias + std::to_string(suffixIndex);
                canonicalAlias = CanonicalizeAlias(candidateAlias);
                ++suffixIndex;
            }

            entry.Alias = candidateAlias;
            usedAliases.insert(canonicalAlias);
            usedKeys.insert(canonicalKey);
            normalizedEntries.push_back(std::move(entry));
        };

        for (const auto& entry : s.AdditionalInputActionsAssets)
            tryAppendEntry(entry);
        for (const auto& key : s.AdditionalInputActionsKeys)
            tryAppendEntry(InputActionsAssetAliasEntry{ DefaultAliasFromInputActionsKey(key), key });

        s.AdditionalInputActionsAssets = std::move(normalizedEntries);
        s.AdditionalInputActionsKeys = CollectAdditionalInputActionsAssetKeys(s);

        std::sort(s.AdditionalInputActionsKeys.begin(), s.AdditionalInputActionsKeys.end());
        s.AdditionalInputActionsKeys.erase(
            std::unique(s.AdditionalInputActionsKeys.begin(), s.AdditionalInputActionsKeys.end()),
            s.AdditionalInputActionsKeys.end());
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

    std::vector<std::string> CollectAdditionalInputActionsAssetKeys(const InputSettings& settings)
    {
        std::vector<std::string> keys;
        std::set<std::string> uniqueKeys;
        for (const auto& entry : settings.AdditionalInputActionsAssets)
        {
            const std::string key = TrimCopy(entry.AssetKey);
            if (!key.empty() && uniqueKeys.insert(key).second)
                keys.push_back(key);
        }
        for (const auto& key : settings.AdditionalInputActionsKeys)
        {
            const std::string trimmed = TrimCopy(key);
            if (!trimmed.empty() && uniqueKeys.insert(trimmed).second)
                keys.push_back(trimmed);
        }
        return keys;
    }

    Result<std::string> ResolveInputActionsAssetKeyByAlias(const InputSettings& settings, const std::string& alias)
    {
        const std::string canonicalAlias = CanonicalizeAlias(alias);
        if (canonicalAlias.empty())
            return Result<std::string>(ErrorCode::InvalidArgument, "ResolveInputActionsAssetKeyByAlias: alias is empty");

        if (canonicalAlias == "default")
        {
            if (settings.ProjectInputActionsKey.empty())
                return Result<std::string>(ErrorCode::ResourceNotFound, "ResolveInputActionsAssetKeyByAlias: default input actions key is not set");
            return settings.ProjectInputActionsKey;
        }

        for (const auto& entry : settings.AdditionalInputActionsAssets)
        {
            if (entry.AssetKey.empty())
                continue;
            if (CanonicalizeAlias(entry.Alias) == canonicalAlias)
                return entry.AssetKey;
        }

        return Result<std::string>(ErrorCode::ResourceNotFound, "ResolveInputActionsAssetKeyByAlias: alias not found");
    }

    Result<std::string> ResolveInputActionsAssetKeyByAlias(const std::filesystem::path& projectRoot, const std::string& alias)
    {
        const auto settingsResult = LoadInputSettings(projectRoot);
        if (settingsResult.IsFailure())
            return Result<std::string>(settingsResult.GetError());
        return ResolveInputActionsAssetKeyByAlias(settingsResult.GetValue(), alias);
    }
}

