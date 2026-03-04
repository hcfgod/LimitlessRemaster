#pragma once

#include "Assets/TextureAsset.h"

#include <algorithm>
#include <cstdint>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace Limitless
{
    // -------------------------------------------------------------------------
    // Grid2D + TilemapLayer system (Unity-style Tile Palette architecture)
    //
    // Grid2DComponent lives on a parent entity and defines the grid layout.
    // TilemapLayerComponent lives on child entities - one per layer.
    // Each layer stores a tile table mapping compact IDs to TileAsset keys,
    // and a per-cell array of tile IDs.
    // -------------------------------------------------------------------------

    /// Defines the grid layout for a tilemap hierarchy. Place on a parent entity
    /// whose children carry TilemapLayerComponent for per-layer tile data.
    struct Grid2DComponent
    {
        glm::vec2 CellSize = glm::vec2(1.0f, 1.0f);
        glm::vec2 CellGap = glm::vec2(0.0f, 0.0f);
    };

    /// A single tilemap layer within a Grid2D hierarchy. Each cell stores an
    /// index into the TileTable; index 0 is always "empty". The TileTable maps
    /// compact IDs to TileAsset keys for rendering and collision.
    struct TilemapLayerComponent
    {
        glm::ivec2 GridSize = glm::ivec2(64, 64);
        int32_t RenderOrder = 0;
        bool CollisionEnabled = false;
        bool CastShadows = false;

        /// Maps compact tile IDs to TileAsset keys. Index 0 is reserved (empty).
        std::vector<std::string> TileTable;

        /// Per-cell tile ID (index into TileTable). 0 = empty.
        std::vector<uint32_t> Tiles;

        /// Transient render cache - not serialized. Rebuilt lazily when dirty.
        struct CachedTileRenderData
        {
            Assets::TextureAsset::Ptr Texture;
            glm::vec2 UvMin = glm::vec2(0.0f);
            glm::vec2 UvMax = glm::vec2(1.0f);
            glm::vec4 Color = glm::vec4(1.0f);
        };
        std::vector<CachedTileRenderData> CachedTileRender;
        bool RenderCacheDirty = true;

        int32_t GetCellCount() const
        {
            return std::max(1, GridSize.x) * std::max(1, GridSize.y);
        }

        void EnsureStorage()
        {
            const size_t cellCount = static_cast<size_t>(GetCellCount());
            if (Tiles.size() != cellCount)
                Tiles.resize(cellCount, 0u);
        }

        /// Returns the existing tile table index for the given key, or appends
        /// a new entry and returns the new index. Index 0 is reserved for "no tile".
        uint32_t GetOrAddTileTableEntry(const std::string& tileAssetKey)
        {
            if (tileAssetKey.empty())
                return 0;

            // Ensure index 0 is the empty sentinel so tileId==0 always means "no tile".
            if (TileTable.empty())
                TileTable.emplace_back();

            for (size_t i = 1; i < TileTable.size(); ++i)
            {
                if (TileTable[i] == tileAssetKey)
                    return static_cast<uint32_t>(i);
            }
            TileTable.push_back(tileAssetKey);
            RenderCacheDirty = true;
            return static_cast<uint32_t>(TileTable.size() - 1);
        }

        void ResizeGrid(const glm::ivec2& requestedGridSize)
        {
            const glm::ivec2 previousGridSize = GridSize;
            GridSize = glm::ivec2(std::max(1, requestedGridSize.x), std::max(1, requestedGridSize.y));

            const int32_t oldWidth = std::max(1, previousGridSize.x);
            const int32_t newWidth = std::max(1, GridSize.x);
            const int32_t newHeight = std::max(1, GridSize.y);
            const int32_t copyWidth = std::min(oldWidth, newWidth);
            const int32_t copyHeight = std::min(std::max(1, previousGridSize.y), newHeight);

            const std::vector<uint32_t> oldTiles = Tiles;
            Tiles.assign(static_cast<size_t>(newWidth * newHeight), 0u);

            for (int32_t y = 0; y < copyHeight; ++y)
            {
                for (int32_t x = 0; x < copyWidth; ++x)
                {
                    const size_t oldIndex = static_cast<size_t>(y * oldWidth + x);
                    const size_t newIndex = static_cast<size_t>(y * newWidth + x);
                    if (oldIndex < oldTiles.size())
                        Tiles[newIndex] = oldTiles[oldIndex];
                }
            }
        }
    };

    inline bool IsLayerCellInBounds(const TilemapLayerComponent& layer, int32_t cellX, int32_t cellY)
    {
        return cellX >= 0 && cellY >= 0 &&
            cellX < std::max(1, layer.GridSize.x) &&
            cellY < std::max(1, layer.GridSize.y);
    }

    inline size_t LayerCellToIndex(const TilemapLayerComponent& layer, int32_t cellX, int32_t cellY)
    {
        const int32_t width = std::max(1, layer.GridSize.x);
        return static_cast<size_t>(cellY * width + cellX);
    }
}
