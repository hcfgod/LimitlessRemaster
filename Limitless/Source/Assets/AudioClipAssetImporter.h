#pragma once

#include "Assets/AssetImporter.h"
#include "Assets/AudioClipAsset.h"
#include "Assets/AssetHotReloadManager.h"
#include "Assets/AssetLoadCoordinator.h"

namespace Limitless::Assets
{
    template<>
    struct AssetImporter<AudioClipAsset>
    {
        using AssetT = AudioClipAsset;
        using Ptr = AudioClipAsset::Ptr;
        using Settings = AudioClipAsset::Settings;

        static constexpr AssetType Type = AssetType::AudioClip;

        static nlohmann::json SettingsToJson(const Settings& s)
        {
            nlohmann::json j = nlohmann::json::object();
            j["targetSampleRateHz"] = s.TargetSampleRateHz;
            j["targetChannelCount"] = s.TargetChannelCount;
            return j;
        }

        static Async::Task<Ptr> LoadAsync(const std::string& key, const Settings& settings = Settings{}, uint64_t generation = AssetLoadCoordinator::GetGeneration())
        {
            const auto recordResult = AssetDatabase::GetInstance().ImportOrUpdate(key, Type, SettingsToJson(settings));
            if (recordResult.IsSuccess())
            {
                AssetDatabase::GetInstance().SetDependencies(recordResult.GetValue().Guid, {});
            }
            AssetHotReloadManager::GetInstance().WatchKey(key);

            return AudioClipAsset::LoadAsync(key, settings, generation);
        }
    };
}

