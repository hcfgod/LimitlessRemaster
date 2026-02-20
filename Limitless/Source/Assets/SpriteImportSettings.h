#pragma once

#include "Core/Error.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace Limitless::Assets
{
    /// A named rectangular region within a sprite sheet (pixel space, origin top-left).
    struct SpriteSubRect
    {
        std::string Name;
        glm::ivec4 RectPixels = glm::ivec4(0); // x, y, width, height
    };

    /// Import-time metadata for texture assets that act as sprite sheets.
    /// Persisted as extra fields inside the texture's `.meta` JSON file.
    struct SpriteImportSettings
    {
        enum class SpriteMode : uint8_t
        {
            Single   = 0,
            Multiple = 1
        };

        SpriteMode Mode = SpriteMode::Single;
        float PixelsPerUnit = 16.0f;
        std::vector<SpriteSubRect> SubSprites;
    };

    /// Load sprite import settings from the `.meta` file of the given texture asset key.
    /// Returns default settings if the meta file doesn't contain sprite fields.
    [[nodiscard]] SpriteImportSettings LoadSpriteImportSettings(const std::string& textureAssetKey);

    /// Persist sprite import settings into the `.meta` file of the given texture asset key.
    /// Preserves all existing meta fields (guid, deps, etc.).
    [[nodiscard]] Result<void> SaveSpriteImportSettings(const std::string& textureAssetKey,
                                                        const SpriteImportSettings& settings);

    /// Compute normalized UV coordinates for a sub-sprite rect within a texture of the given dimensions.
    /// Returns { uvMinX, uvMinY, uvMaxX, uvMaxY }.
    [[nodiscard]] glm::vec4 ComputeSubSpriteUvs(const glm::ivec4& rectPixels,
                                                 uint32_t textureWidth,
                                                 uint32_t textureHeight);
}
