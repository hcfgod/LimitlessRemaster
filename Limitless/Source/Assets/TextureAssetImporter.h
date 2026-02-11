#pragma once

#include "Assets/AssetImporter.h"
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

            const auto recordResult = AssetDatabase::GetInstance().ImportOrUpdate(
                key,
                Type,
                isDefaultSettings ? nlohmann::json::object() : SettingsToJson(settings));
            if (recordResult.IsSuccess())
            {
                // No deps for textures, but make sure we clear the field for correctness.
                AssetDatabase::GetInstance().SetDependencies(recordResult.GetValue().Guid, {});
            }
            AssetHotReloadManager::GetInstance().WatchKey(key);
            // Load actual asset.
            return TextureAsset::LoadAsync(key, settings, generation);
        }
    };
}

