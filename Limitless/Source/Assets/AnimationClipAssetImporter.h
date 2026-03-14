#pragma once

#include "Assets/AnimationClipAsset.h"
#include "Assets/AssetHotReloadManager.h"
#include "Assets/AssetImporter.h"
#include "Assets/AssetLoadCoordinator.h"

namespace Limitless::Assets
{
    template<>
    struct AssetImporter<AnimationClipAsset>
    {
        using AssetT = AnimationClipAsset;
        using Ptr = AnimationClipAsset::Ptr;
        using Settings = AnimationClipAsset::Settings;

        static constexpr AssetType Type = AssetType::AnimationClip;
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
            return AnimationClipAsset::LoadAsync(key, settings);
        }
    };
}

