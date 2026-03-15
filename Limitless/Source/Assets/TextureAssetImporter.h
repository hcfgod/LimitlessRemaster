#pragma once

#include "Assets/AssetImporter.h"
#include "Assets/GeneratedAssetRuntimeRegistry.h"
#include "Assets/TextureAsset.h"
#include "Assets/AssetHotReloadManager.h"
#include "Assets/AssetLoadCoordinator.h"

#include "Graphics/Texture.h"
#include "Assets/TextureSpecificationJson.h"

namespace Limitless::Assets
{
    template<>
    struct AssetImporter<TextureAsset>
    {
        using AssetT = TextureAsset;
        using Ptr = TextureAsset::Ptr;
        using Settings = TextureSpecification;

        static constexpr AssetType Type = AssetType::Texture2D;
        static constexpr uint32_t Version = 1u;

        static nlohmann::json SettingsToJson(const Settings& s)
        {
            nlohmann::json j = nlohmann::json::object();
            j["generateMipmaps"] = s.GenerateMipmaps;
            j["flipVerticallyOnLoad"] = s.FlipVerticallyOnLoad;
            j["minFilter"] = static_cast<int>(s.MinFilter);
            j["magFilter"] = static_cast<int>(s.MagFilter);
            j["wrapU"] = static_cast<int>(s.WrapU);
            j["wrapV"] = static_cast<int>(s.WrapV);
            return j;
        }

        static Async::Task<Ptr> LoadAsync(const std::string& key, const Settings& settings = Settings{}, uint64_t generation = AssetLoadCoordinator::GetGeneration())
        {
            // Register/import metadata.
            // IMPORTANT:
            // Treat default settings as "no explicit importer override".
            // This prevents incidental loads (example: a Material resolving its mainTexture)
            // from overwriting a texture's canonical importer settings stored in the database.
            const bool isDefaultSettings = IsDefaultTextureSpecification(settings);

            Settings resolvedSettings = settings;
            const auto generatedRecordResult = FindGeneratedAssetRecord(key, Type);
            if (generatedRecordResult.IsSuccess())
            {
                AssetDatabase::GetInstance().SetDependencies(generatedRecordResult.GetValue().Guid, {});

                if (isDefaultSettings)
                    resolvedSettings = TextureSpecificationFromImporterSettingsJson(generatedRecordResult.GetValue().ImporterSettings);
            }
            else
            {
                const auto recordResult = AssetDatabase::GetInstance().ImportOrUpdate(
                    key,
                    Type,
                    isDefaultSettings ? nlohmann::json::object() : SettingsToJson(settings),
                    Version);
                if (recordResult.IsSuccess())
                {
                    AssetDatabase::GetInstance().SetDependencies(recordResult.GetValue().Guid, {});
                    if (isDefaultSettings)
                        resolvedSettings = TextureSpecificationFromImporterSettingsJson(recordResult.GetValue().ImporterSettings);
                }
            }
            AssetHotReloadManager::GetInstance().WatchKey(key);
            return TextureAsset::LoadAsync(key, resolvedSettings, generation);
        }
    };
}

