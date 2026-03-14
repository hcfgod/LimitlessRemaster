#pragma once

#include "Assets/AssetImporter.h"
#include "Assets/ShaderAsset.h"
#include "Assets/AssetHotReloadManager.h"
#include "Assets/AssetLoadCoordinator.h"

namespace Limitless::Assets
{
    template<>
    struct AssetImporter<ShaderAsset>
    {
        using AssetT = ShaderAsset;
        using Ptr = ShaderAsset::Ptr;
        using Settings = ShaderAsset::Settings;

        static constexpr AssetType Type = AssetType::Shader;
        static constexpr uint32_t Version = 1u;

        static nlohmann::json SettingsToJson(const Settings& s)
        {
            nlohmann::json j = nlohmann::json::object();
            j["name"] = s.Name;
            return j;
        }

        static Async::Task<Ptr> LoadAsync(const std::string& key, const Settings& settings = Settings{}, uint64_t generation = AssetLoadCoordinator::GetGeneration())
        {
            const auto recordResult = AssetDatabase::GetInstance().ImportOrUpdate(key, Type, SettingsToJson(settings), Version);
            if (recordResult.IsSuccess())
            {
                AssetDatabase::GetInstance().SetDependencies(recordResult.GetValue().Guid, {});
            }
            AssetHotReloadManager::GetInstance().WatchKey(key);

            return ShaderAsset::LoadAsync(key, settings, generation);
        }
    };
}

