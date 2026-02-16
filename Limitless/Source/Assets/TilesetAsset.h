#pragma once

#include <glm/glm.hpp>

#include <string>

namespace Limitless::Assets
{
    /// Serializable tileset metadata used by TilemapComponent.
    struct TilesetAssetDefinition
    {
        std::string TextureKey;
        glm::ivec2 TileSizePixels = glm::ivec2(16, 16);
    };

    /// Loads a .tileset.json asset by key and parses it into a definition.
    bool TryLoadTilesetAssetDefinition(const std::string& tilesetAssetKey,
                                       TilesetAssetDefinition& outDefinition,
                                       std::string* outError = nullptr);
}
