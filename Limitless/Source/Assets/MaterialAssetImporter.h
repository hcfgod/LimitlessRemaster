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
                // Deps are discovered from the material JSON; we update on Reload().
                AssetDatabase::GetInstance().SetDependencies(recordResult.GetValue().Guid, {});
            }

            AssetHotReloadManager::GetInstance().WatchKey(key);
            (void)generation;
            auto task = MaterialAsset::LoadAsync(key, settings);

            // After initial load completes, trigger a dependency write by calling Reload once.
            // This is cheap (re-reads JSON) and sets deps immediately for cascading reload.
            Async::GetAsyncIO().RunAsync([task]() mutable {
                if (!task.IsValid())
                {
                    return;
                }
                task.Wait();
                auto asset = task.Get();
                if (asset)
                {
                    (void)asset->Reload();
                }
            });

            return task;
        }
    };
}

