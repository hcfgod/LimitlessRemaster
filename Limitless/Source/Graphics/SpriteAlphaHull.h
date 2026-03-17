#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Limitless
{
    // Computes a convex hull (in local sprite space, centered at origin) from the
    // opaque pixels of a sprite texture.  Results are cached per texture asset key
    // + sub-sprite index so the (relatively expensive) CPU decode + hull computation
    // only happens once per unique sprite appearance.
    //
    // The returned polygon is in local space [-0.5, 0.5] matching the unit quad
    // used by the sprite renderer, wound counter-clockwise.
    //
    // Returns an empty vector if the texture cannot be loaded or has no opaque pixels.
    [[nodiscard]] std::vector<glm::vec2> GetOrBuildSpriteAlphaHull(
        const std::string& textureAssetKey,
        int32_t subSpriteIndex,
        uint8_t alphaThreshold = 32);

    // Overload that takes a UV rect (in OpenGL UV space where Y=0 is image bottom).
    // Used for animated sprites where the animator provides UV sub-rects.
    // Resolves pixel coordinates internally from the disk-decoded image dimensions
    // so it works even when the GPU texture hasn't been cached yet.
    [[nodiscard]] std::vector<glm::vec2> GetOrBuildSpriteAlphaHullForUvRect(
        const std::string& textureAssetKey,
        const glm::vec2& uvMin,
        const glm::vec2& uvMax,
        uint8_t alphaThreshold = 32);

    // Drop all cached hulls (e.g. on project close or asset hot-reload).
    void InvalidateSpriteAlphaHullCache();

    // Drop a single entry (e.g. after a texture re-import).
    void InvalidateSpriteAlphaHullCacheEntry(const std::string& textureAssetKey, int32_t subSpriteIndex);
}
