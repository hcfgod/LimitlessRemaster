#pragma once

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace Limitless::Assets
{
    /// Legacy tileset metadata retained for compatibility tooling.
    struct TilesetAssetDefinition
    {
        std::string TextureKey;
        glm::ivec2 TileSizePixels = glm::ivec2(16, 16);
        glm::ivec2 MarginPixels = glm::ivec2(0, 0);
        glm::ivec2 SpacingPixels = glm::ivec2(0, 0);
        std::vector<glm::ivec4> ExplicitTileRectsPixels;
    };

    /// Loads a .tileset.json asset by key and parses it into a definition.
    bool TryLoadTilesetAssetDefinition(const std::string& tilesetAssetKey,
                                       TilesetAssetDefinition& outDefinition,
                                       std::string* outError = nullptr);

    /// Finds or creates a sidecar .tileset.json for a texture key and returns the tileset key.
    /// This enables an automatic tilemap workflow similar to Unity/Godot importers.
    bool TryEnsureTilesetAssetForTextureKey(const std::string& textureAssetKey,
                                            const glm::ivec2& preferredTileSizePixels,
                                            std::string& outTilesetAssetKey,
                                            std::string* outError = nullptr);
}
