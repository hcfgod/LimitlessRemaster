#pragma once

#include "Core/Error.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <string>

namespace Limitless::Assets
{
    /// Runtime data for a single tile. Persisted as a `.tile.json` file on disk.
    /// Each tile references a sprite (texture + optional sub-sprite index) and carries
    /// per-tile metadata such as color tint and collider type.
    struct TileAssetData
    {
        /// Texture asset key for the sprite sheet or standalone sprite.
        std::string SpriteTextureKey;

        /// Index into the texture's SpriteImportSettings::SubSprites list.
        /// -1 means use the full texture as the sprite.
        int32_t SubSpriteIndex = -1;

        /// Per-tile color tint applied during rendering.
        glm::vec4 Color = glm::vec4(1.0f);

        /// Collider shape hint used by the tilemap collision generator.
        enum class ColliderType : uint8_t
        {
            None   = 0,
            Grid   = 1,
            Sprite = 2
        };
        ColliderType Collider = ColliderType::Grid;
    };

    /// Load a `.tile.json` asset from disk by its asset key.
    [[nodiscard]] Result<TileAssetData> LoadTileAssetData(const std::string& tileAssetKey);

    /// Save a `.tile.json` asset to disk at the location resolved from the asset key.
    [[nodiscard]] Result<void> SaveTileAssetData(const std::string& tileAssetKey,
                                                  const TileAssetData& data);

    /// Create a new `.tile.json` file at the given absolute path and return its asset key.
    /// The caller is responsible for registering the asset with AssetDatabase afterward.
    [[nodiscard]] Result<void> WriteTileAssetFile(const std::filesystem::path& absolutePath,
                                                  const TileAssetData& data);
}
