#pragma once

#include "Core/Error.h"

#include <glm/glm.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace Limitless::Assets
{
    /// Persistent data for a Tile Palette asset (`.tilepalette.json`).
    /// A palette is a visual grid of tile asset references used by the editor
    /// for brush selection and painting. It is not needed at runtime — tilemap
    /// layers store their own tile tables.
    struct TilePaletteData
    {
        /// Dimensions of the palette grid (columns x rows).
        glm::ivec2 GridSize = glm::ivec2(0, 0);

        /// Row-major list of tile asset keys. Empty string means an empty cell.
        /// Length should always equal GridSize.x * GridSize.y.
        std::vector<std::string> TileAssetKeys;

        /// The source texture asset key used to populate this palette (informational).
        std::string SourceTextureKey;
    };

    /// Load a `.tilepalette.json` asset from disk.
    [[nodiscard]] Result<TilePaletteData> LoadTilePaletteData(const std::string& paletteAssetKey);

    /// Save a `.tilepalette.json` asset to disk.
    [[nodiscard]] Result<void> SaveTilePaletteData(const std::string& paletteAssetKey,
                                                    const TilePaletteData& data);

    /// Write a `.tilepalette.json` file at the given absolute path.
    [[nodiscard]] Result<void> WriteTilePaletteFile(const std::filesystem::path& absolutePath,
                                                     const TilePaletteData& data);

    /// Populate a palette by auto-generating Tile assets from a sliced sprite sheet.
    /// Creates `.tile.json` files in a subfolder next to the palette, registers them
    /// with AssetDatabase, and fills the palette grid in spatial order.
    ///
    /// @param paletteAssetKey  The asset key of the palette being populated.
    /// @param textureAssetKey  The sliced sprite sheet texture key.
    /// @param outData          Receives the fully populated palette data.
    [[nodiscard]] Result<void> PopulatePaletteFromSpriteSheet(const std::string& paletteAssetKey,
                                                              const std::string& textureAssetKey,
                                                              TilePaletteData& outData);
}
