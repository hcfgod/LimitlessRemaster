#include "Audio/AudioMixerAsset.h"

#include "Assets/AssetPaths.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <unordered_set>

namespace Limitless::Audio
{
    namespace
    {
        void EnsureDefaultMixerGroups(AudioMixerDefinition& definition)
        {
            std::unordered_set<std::string> existingNames;
            existingNames.reserve(definition.Groups.size());
            for (const auto& group : definition.Groups)
                existingNames.insert(group.Name);

            const auto ensureGroup = [&](const char* groupName) {
                if (!groupName || !groupName[0])
                    return;
                if (existingNames.contains(groupName))
                    return;
                definition.Groups.push_back(AudioMixerGroupEntry{ groupName, 1.0f });
                existingNames.insert(groupName);
            };

            ensureGroup("Master");
            ensureGroup("SFX");
            ensureGroup("Music");
            ensureGroup("UI");
        }
    }

    void NormalizeAudioMixerDefinition(AudioMixerDefinition& definition)
    {
        std::vector<AudioMixerGroupEntry> normalized;
        normalized.reserve(definition.Groups.size());

        std::unordered_set<std::string> seenNames;
        seenNames.reserve(definition.Groups.size());
        for (const auto& group : definition.Groups)
        {
            const std::string name = group.Name;
            if (name.empty() || seenNames.contains(name))
                continue;

            AudioMixerGroupEntry normalizedGroup{};
            normalizedGroup.Name = name;
            normalizedGroup.Volume = std::max(0.0f, group.Volume);
            normalized.push_back(std::move(normalizedGroup));
            seenNames.insert(name);
        }

        definition.Groups = std::move(normalized);
        EnsureDefaultMixerGroups(definition);
    }

    bool LoadAudioMixerDefinitionFromAssetKey(const std::string& assetKey,
                                              AudioMixerDefinition& outDefinition,
                                              std::filesystem::path* outResolvedPath)
    {
        if (assetKey.empty())
            return false;

        const auto resolvedResult = Assets::ResolveAssetKeyToPath(assetKey);
        if (resolvedResult.IsFailure())
            return false;

        const std::filesystem::path resolvedPath = resolvedResult.GetValue();
        if (outResolvedPath)
            *outResolvedPath = resolvedPath;

        std::ifstream input(resolvedPath, std::ios::in | std::ios::binary);
        if (!input.is_open())
            return false;

        nlohmann::json rootJson{};
        try
        {
            input >> rootJson;
        }
        catch (...)
        {
            return false;
        }

        AudioMixerDefinition definition{};
        if (rootJson.is_object())
        {
            definition.Version = rootJson.value("Version", 1u);
            if (rootJson.contains("Groups") && rootJson["Groups"].is_array())
            {
                for (const auto& groupJson : rootJson["Groups"])
                {
                    if (!groupJson.is_object())
                        continue;

                    AudioMixerGroupEntry group{};
                    group.Name = groupJson.value("Name", std::string{});
                    group.Volume = groupJson.value("Volume", 1.0f);
                    definition.Groups.push_back(std::move(group));
                }
            }
        }

        NormalizeAudioMixerDefinition(definition);
        outDefinition = std::move(definition);
        return true;
    }

    bool SaveAudioMixerDefinitionToPath(const std::filesystem::path& path,
                                        const AudioMixerDefinition& definition)
    {
        if (path.empty())
            return false;

        std::error_code errorCode;
        std::filesystem::create_directories(path.parent_path(), errorCode);
        if (errorCode)
            return false;

        AudioMixerDefinition normalizedDefinition = definition;
        NormalizeAudioMixerDefinition(normalizedDefinition);

        nlohmann::json groupsJson = nlohmann::json::array();
        for (const auto& group : normalizedDefinition.Groups)
        {
            groupsJson.push_back({
                { "Name", group.Name },
                { "Volume", group.Volume }
            });
        }

        const nlohmann::json rootJson = {
            { "Version", normalizedDefinition.Version },
            { "Groups", std::move(groupsJson) }
        };

        std::ofstream output(path, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!output.is_open())
            return false;

        output << rootJson.dump(2);
        output.flush();
        return true;
    }
}
