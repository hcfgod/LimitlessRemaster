#pragma once

#include "Assets/AnimatorControllerAsset.h"
#include "Assets/AssetHotReloadManager.h"
#include "Assets/AssetImporter.h"
#include "Assets/AssetLoadCoordinator.h"

namespace Limitless::Assets
{
    template<>
    struct AssetImporter<AnimatorControllerAsset>
    {
        using AssetT = AnimatorControllerAsset;
        using Ptr = AnimatorControllerAsset::Ptr;
        using Settings = AnimatorControllerAsset::Settings;

        static constexpr AssetType Type = AssetType::AnimatorController;
        static constexpr uint32_t Version = 1u;

        static nlohmann::json SettingsToJson(const Settings& settings)
        {
            nlohmann::json root = nlohmann::json::object();
            root["validateStrictly"] = settings.ValidateStrictly;
            return root;
        }

        static Async::Task<Ptr> LoadAsync(const std::string& key,
                                          const Settings& settings = Settings{},
                                          uint64_t generation = AssetLoadCoordinator::GetGeneration())
        {
            const auto importResult = AssetDatabase::GetInstance().ImportOrUpdate(key, Type, SettingsToJson(settings), Version);
            if (importResult.IsSuccess())
            {
                AssetDatabase::GetInstance().SetDependencies(importResult.GetValue().Guid, {});
            }

            AssetHotReloadManager::GetInstance().WatchKey(key);
            (void)generation;
            return AnimatorControllerAsset::LoadAsync(key, settings);
        }
    };
}

