#pragma once

#include "Assets/AssetImporter.h"
#include "Assets/AssetHotReloadManager.h"
#include "Assets/AssetLoadCoordinator.h"
#include "Assets/InputActionsAssetResource.h"

namespace Limitless::Assets
{
    template<>
    struct AssetImporter<InputActionsAssetResource>
    {
        using AssetT = InputActionsAssetResource;
        using Ptr = InputActionsAssetResource::Ptr;
        using Settings = InputActionsAssetResource::Settings;

        static constexpr AssetType Type = AssetType::InputActions;

        static nlohmann::json SettingsToJson(const Settings&)
        {
            return nlohmann::json::object();
        }

        static Async::Task<Ptr> LoadAsync(const std::string& key, const Settings& settings = Settings{}, uint64_t generation = AssetLoadCoordinator::GetGeneration())
        {
            const auto recordResult = AssetDatabase::GetInstance().ImportOrUpdate(key, Type, SettingsToJson(settings));
            if (recordResult.IsSuccess())
            {
                AssetDatabase::GetInstance().SetDependencies(recordResult.GetValue().Guid, {});
            }

            AssetHotReloadManager::GetInstance().WatchKey(key);
            (void)generation;
            return InputActionsAssetResource::LoadAsync(key, settings);
        }
    };
}

