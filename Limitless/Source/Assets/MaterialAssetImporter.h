#pragma once

#include "Assets/AssetImporter.h"
#include "Assets/AssetHotReloadManager.h"
#include "Assets/AssetLoadCoordinator.h"
#include "Assets/MaterialAsset.h"

namespace Limitless::Assets
{
    template<>
    struct AssetImporter<MaterialAsset>
    {
        using AssetT = MaterialAsset;
        using Ptr = MaterialAsset::Ptr;
        using Settings = MaterialAsset::Settings;

        static constexpr AssetType Type = AssetType::Material;

        static nlohmann::json SettingsToJson(const Settings&)
        {
            return nlohmann::json::object();
        }

        static Async::Task<Ptr> LoadAsync(const std::string& key, const Settings& settings = Settings{}, uint64_t generation = AssetLoadCoordinator::GetGeneration())
        {
            const auto recordResult = AssetDatabase::GetInstance().ImportOrUpdate(key, Type, SettingsToJson(settings));
            if (recordResult.IsSuccess())
            {
                // Deps are discovered from the material JSON during load/reload.
                AssetDatabase::GetInstance().SetDependencies(recordResult.GetValue().Guid, {});
            }

            AssetHotReloadManager::GetInstance().WatchKey(key);
            (void)generation;
            return MaterialAsset::LoadAsync(key, settings);
        }
    };
}

