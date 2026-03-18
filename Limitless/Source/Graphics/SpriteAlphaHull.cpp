#include "Graphics/SpriteAlphaHull.h"

#include "Assets/AssetPaths.h"
#include "Assets/ImageDecode.h"
#include "Assets/SpriteImportSettings.h"
#include "Core/Debug/Log.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace Limitless
{
    // -------------------------------------------------------------------------
    // Cache key
    // -------------------------------------------------------------------------

    struct SpriteAlphaHullCacheKey
    {
        std::string TextureAssetKey;
        int32_t SubSpriteIndex = -1;
        // Pixel rect (used when SubSpriteIndex == -2, meaning "use explicit rect").
        uint32_t RectX = 0, RectY = 0, RectW = 0, RectH = 0;

        bool operator==(const SpriteAlphaHullCacheKey& other) const
        {
            return TextureAssetKey == other.TextureAssetKey
                && SubSpriteIndex == other.SubSpriteIndex
                && RectX == other.RectX && RectY == other.RectY
                && RectW == other.RectW && RectH == other.RectH;
        }
    };

    static constexpr int32_t kExplicitRectSentinel = -2;

    struct SpriteAlphaHullCacheKeyHash
    {
        size_t operator()(const SpriteAlphaHullCacheKey& key) const
        {
            size_t h = std::hash<std::string>{}(key.TextureAssetKey);
            h ^= std::hash<int32_t>{}(key.SubSpriteIndex) + 0x9e3779b9 + (h << 6) + (h >> 2);
            if (key.SubSpriteIndex == kExplicitRectSentinel)
            {
                h ^= std::hash<uint32_t>{}(key.RectX) + 0x9e3779b9 + (h << 6) + (h >> 2);
                h ^= std::hash<uint32_t>{}(key.RectY) + 0x517cc1b7 + (h << 6) + (h >> 2);
                h ^= std::hash<uint32_t>{}(key.RectW) + 0x9e3779b9 + (h << 6) + (h >> 2);
                h ^= std::hash<uint32_t>{}(key.RectH) + 0x517cc1b7 + (h << 6) + (h >> 2);
            }
            return h;
        }
    };

    struct SpriteAlphaHullCacheEntry
    {
        std::vector<glm::vec2> Hull;
        bool Computed = false;
    };

    static std::mutex s_HullCacheMutex;
    static std::unordered_map<SpriteAlphaHullCacheKey, SpriteAlphaHullCacheEntry, SpriteAlphaHullCacheKeyHash> s_HullCache;

    // -------------------------------------------------------------------------
    // Convex hull (Andrew's monotone chain)
    // -------------------------------------------------------------------------

    static float Cross2D(const glm::vec2& o, const glm::vec2& a, const glm::vec2& b)
    {
        return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
    }

    static std::vector<glm::vec2> ComputeConvexHull(std::vector<glm::vec2>& points)
    {
        const size_t n = points.size();
        if (n < 3)
            return points;

        std::sort(points.begin(), points.end(), [](const glm::vec2& a, const glm::vec2& b) {
            return (a.x < b.x) || (a.x == b.x && a.y < b.y);
        });

        std::vector<glm::vec2> hull(2 * n);
        size_t k = 0;

        // Lower hull
        for (size_t i = 0; i < n; ++i)
        {
            while (k >= 2 && Cross2D(hull[k - 2], hull[k - 1], points[i]) <= 0.0f)
                --k;
            hull[k++] = points[i];
        }

        // Upper hull
        const size_t lower = k + 1;
        for (size_t i = n - 1; i > 0; --i)
        {
            while (k >= lower && Cross2D(hull[k - 2], hull[k - 1], points[i - 1]) <= 0.0f)
                --k;
            hull[k++] = points[i - 1];
        }

        hull.resize(k - 1); // Remove last point (duplicate of first)
        return hull;
    }

    // -------------------------------------------------------------------------
    // Hull building from alpha pixels
    // -------------------------------------------------------------------------

    static std::vector<glm::vec2> BuildHullFromAlpha(
        const uint8_t* rgba8Pixels,
        uint32_t fullWidth,
        uint32_t fullHeight,
        uint32_t regionX,
        uint32_t regionY,
        uint32_t regionW,
        uint32_t regionH,
        uint8_t alphaThreshold)
    {
        if (regionW == 0 || regionH == 0)
            return {};

        // Collect per-row min/max opaque X and count opaque pixels.
        std::vector<glm::vec2> extremePoints;
        extremePoints.reserve(regionH * 2);
        uint32_t opaqueCount = 0;
        const uint32_t totalCount = regionW * regionH;

        for (uint32_t ry = 0; ry < regionH; ++ry)
        {
            int32_t minX = -1;
            int32_t maxX = -1;
            for (uint32_t rx = 0; rx < regionW; ++rx)
            {
                const uint32_t px = regionX + rx;
                const uint32_t py = regionY + ry;
                const size_t idx = (static_cast<size_t>(py) * fullWidth + px) * 4 + 3;
                if (rgba8Pixels[idx] >= alphaThreshold)
                {
                    ++opaqueCount;
                    if (minX < 0)
                        minX = static_cast<int32_t>(rx);
                    maxX = static_cast<int32_t>(rx);
                }
            }
            if (minX >= 0)
            {
                const float ny = 0.5f - (static_cast<float>(ry) + 0.5f) / static_cast<float>(regionH);
                const float nxMin = (static_cast<float>(minX) + 0.5f) / static_cast<float>(regionW) - 0.5f;
                const float nxMax = (static_cast<float>(maxX) + 0.5f) / static_cast<float>(regionW) - 0.5f;
                extremePoints.push_back(glm::vec2(nxMin, ny));
                if (maxX != minX)
                    extremePoints.push_back(glm::vec2(nxMax, ny));
            }
        }

        if (extremePoints.size() < 3)
            return extremePoints;

        // If nearly all pixels are opaque, the image likely has no alpha
        // channel (stb_image fills alpha=255 for RGB-only PNGs). The hull
        // would be a rectangle matching the box collider anyway, so return
        // empty to let the caller fall back to the collider shape.
        if (opaqueCount >= totalCount)
            return {};

        return ComputeConvexHull(extremePoints);
    }

    // -------------------------------------------------------------------------
    // Public API
    // -------------------------------------------------------------------------

    std::vector<glm::vec2> GetOrBuildSpriteAlphaHull(
        const std::string& textureAssetKey,
        int32_t subSpriteIndex,
        uint8_t alphaThreshold)
    {
        if (textureAssetKey.empty())
            return {};

        const SpriteAlphaHullCacheKey cacheKey{ textureAssetKey, subSpriteIndex };

        {
            std::lock_guard<std::mutex> lock(s_HullCacheMutex);
            auto it = s_HullCache.find(cacheKey);
            if (it != s_HullCache.end())
                return it->second.Hull;
        }

        // Resolve texture file path
        const auto pathResult = Assets::ResolveAssetKeyToPath(textureAssetKey);
        if (!pathResult.IsSuccess())
        {
            LT_CORE_WARN("SpriteAlphaHull: failed to resolve path for '{}' (sub={})", textureAssetKey, subSpriteIndex);
            std::lock_guard<std::mutex> lock(s_HullCacheMutex);
            s_HullCache[cacheKey] = SpriteAlphaHullCacheEntry{ {}, true };
            return {};
        }

        const std::string filePath = pathResult.GetValue().string();
        auto decodeResult = Assets::DecodeToRGBA8(filePath, false);
        if (!decodeResult.IsSuccess())
        {
            LT_CORE_WARN("SpriteAlphaHull: failed to decode '{}' (sub={})", filePath, subSpriteIndex);
            std::lock_guard<std::mutex> lock(s_HullCacheMutex);
            s_HullCache[cacheKey] = SpriteAlphaHullCacheEntry{ {}, true };
            return {};
        }

        auto& img = decodeResult.GetValue();
        uint32_t regionX = 0;
        uint32_t regionY = 0;
        uint32_t regionW = img.Width;
        uint32_t regionH = img.Height;

        if (subSpriteIndex >= 0)
        {
            const auto settings = Assets::LoadSpriteImportSettings(textureAssetKey);
            if (subSpriteIndex < static_cast<int32_t>(settings.SubSprites.size()))
            {
                const auto& rect = settings.SubSprites[static_cast<size_t>(subSpriteIndex)].RectPixels;
                regionX = static_cast<uint32_t>(std::max(0, rect.x));
                regionY = static_cast<uint32_t>(std::max(0, rect.y));
                regionW = static_cast<uint32_t>(std::max(0, rect.z));
                regionH = static_cast<uint32_t>(std::max(0, rect.w));
                if (regionX + regionW > img.Width)
                    regionW = img.Width > regionX ? img.Width - regionX : 0;
                if (regionY + regionH > img.Height)
                    regionH = img.Height > regionY ? img.Height - regionY : 0;
            }
        }

        auto hull = BuildHullFromAlpha(
            img.Pixels.data(),
            img.Width,
            img.Height,
            regionX,
            regionY,
            regionW,
            regionH,
            alphaThreshold);

        if (hull.size() < 3)
        {
            LT_CORE_WARN("SpriteAlphaHull: hull too small ({} verts) for '{}' (sub={}, img={}x{}, region={}x{})",
                hull.size(), textureAssetKey, subSpriteIndex, img.Width, img.Height, regionW, regionH);
            hull.clear();
        }

        {
            std::lock_guard<std::mutex> lock(s_HullCacheMutex);
            s_HullCache[cacheKey] = SpriteAlphaHullCacheEntry{ hull, true };
        }

        return hull;
    }

    std::vector<glm::vec2> GetOrBuildSpriteAlphaHullForUvRect(
        const std::string& textureAssetKey,
        const glm::vec2& uvMin,
        const glm::vec2& uvMax,
        uint8_t alphaThreshold)
    {
        if (textureAssetKey.empty())
            return {};

        // Quantize UV values to integer 16ths-of-a-texel at 4096 resolution
        // so float equality works reliably for cache lookup.
        // Animation frames snap to exact pixel boundaries so this is lossless.
        auto quantize = [](float v) -> uint32_t {
            return static_cast<uint32_t>(std::round(v * 65536.0f));
        };

        SpriteAlphaHullCacheKey cacheKey;
        cacheKey.TextureAssetKey = textureAssetKey;
        cacheKey.SubSpriteIndex = kExplicitRectSentinel;
        cacheKey.RectX = quantize(uvMin.x);
        cacheKey.RectY = quantize(uvMin.y);
        cacheKey.RectW = quantize(uvMax.x);
        cacheKey.RectH = quantize(uvMax.y);

        // Check cache BEFORE decoding the image.
        {
            std::lock_guard<std::mutex> lock(s_HullCacheMutex);
            auto it = s_HullCache.find(cacheKey);
            if (it != s_HullCache.end())
                return it->second.Hull;
        }

        // Cache miss — decode image from disk.
        const auto pathResult = Assets::ResolveAssetKeyToPath(textureAssetKey);
        if (!pathResult.IsSuccess())
        {
            LT_CORE_WARN("SpriteAlphaHull(uv): failed to resolve path for '{}'", textureAssetKey);
            std::lock_guard<std::mutex> lock(s_HullCacheMutex);
            s_HullCache[cacheKey] = SpriteAlphaHullCacheEntry{ {}, true };
            return {};
        }

        const std::string filePath = pathResult.GetValue().string();
        auto decodeResult = Assets::DecodeToRGBA8(filePath, false);
        if (!decodeResult.IsSuccess())
        {
            LT_CORE_WARN("SpriteAlphaHull(uv): failed to decode '{}'", filePath);
            std::lock_guard<std::mutex> lock(s_HullCacheMutex);
            s_HullCache[cacheKey] = SpriteAlphaHullCacheEntry{ {}, true };
            return {};
        }

        auto& img = decodeResult.GetValue();
        const float texW = static_cast<float>(img.Width);
        const float texH = static_cast<float>(img.Height);

        // Convert OpenGL UV space (Y=0 at image bottom after flip) to
        // image pixel coordinates (Y=0 at image top, no flip).
        const uint32_t regionX = static_cast<uint32_t>(std::max(0.0f, uvMin.x * texW));
        const uint32_t regionY = static_cast<uint32_t>(std::max(0.0f, (1.0f - uvMax.y) * texH));
        uint32_t regionW = static_cast<uint32_t>(std::max(1.0f, (uvMax.x - uvMin.x) * texW));
        uint32_t regionH = static_cast<uint32_t>(std::max(1.0f, (uvMax.y - uvMin.y) * texH));

        // Clamp to image bounds.
        if (regionX + regionW > img.Width)
            regionW = img.Width > regionX ? img.Width - regionX : 0;
        if (regionY + regionH > img.Height)
            regionH = img.Height > regionY ? img.Height - regionY : 0;

        auto hull = BuildHullFromAlpha(
            img.Pixels.data(),
            img.Width,
            img.Height,
            regionX,
            regionY,
            regionW,
            regionH,
            alphaThreshold);

        if (hull.size() < 3)
        {
            LT_CORE_WARN("SpriteAlphaHull(uv): hull too small ({} verts) for '{}' rect=({},{},{},{})",
                hull.size(), textureAssetKey, regionX, regionY, regionW, regionH);
            hull.clear();
        }

        {
            std::lock_guard<std::mutex> lock(s_HullCacheMutex);
            s_HullCache[cacheKey] = SpriteAlphaHullCacheEntry{ hull, true };
        }

        return hull;
    }

    void InvalidateSpriteAlphaHullCache()
    {
        std::lock_guard<std::mutex> lock(s_HullCacheMutex);
        s_HullCache.clear();
    }

    void InvalidateSpriteAlphaHullCacheEntry(const std::string& textureAssetKey, int32_t subSpriteIndex)
    {
        std::lock_guard<std::mutex> lock(s_HullCacheMutex);
        s_HullCache.erase(SpriteAlphaHullCacheKey{ textureAssetKey, subSpriteIndex });
    }
}
