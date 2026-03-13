#pragma once

#include "Assets/TextureAsset.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace Limitless
{
    class VertexBuffer;
    class IndexBuffer;
    class VertexArray;

    inline constexpr int32_t kDefaultTilemapLayerGridDimension = 16;
    inline constexpr int32_t kMaxTilemapLayerGridDimension = 8192;
    inline constexpr int32_t kTilemapPaintedCellChunkDimension = 32;

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
        glm::ivec2 GridSize = glm::ivec2(kDefaultTilemapLayerGridDimension, kDefaultTilemapLayerGridDimension);
        glm::vec2 OriginCell = glm::vec2(0.0f);
    };

    /// A single tilemap layer within a Grid2D hierarchy. Each cell stores an
    /// index into the TileTable; index 0 is always "empty". The TileTable maps
    /// compact IDs to TileAsset keys for rendering and collision.
    struct TilemapLayerComponent
    {
        /// Monotonically increasing revision counter. Bumped on every
        /// mutation (tile paint, TileTable change, resize, undo, etc.).
        /// Used for cheap change detection in the script-runtime access
        /// validator so it never needs to deep-copy or compare Tiles[].
        uint64_t MutationRevision = 0;

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

        /// Cached indices of non-empty cells for efficient rendering.
        /// Rebuilt when RenderCacheDirty is consumed. Avoids iterating
        /// millions of empty cells every frame.
        struct PaintedCell { uint32_t CellIndex; uint32_t TileId; };
        std::vector<PaintedCell> CachedPaintedCells;
        std::vector<uint32_t> CachedPaintedCellRowOffsets;
        std::vector<uint32_t> CachedPaintedCellChunkOffsets;
        std::vector<uint32_t> CachedPaintedCellChunkIndices;

        // -----------------------------------------------------------------
        // Persistent chunk render/lighting caches (not serialized).
        // Each chunk covers kTilemapPaintedCellChunkDimension^2 cells.
        // -----------------------------------------------------------------
        struct ChunkVertex
        {
            glm::vec3 Position{0.0f};
            glm::vec2 UV{0.0f};
            glm::vec4 Color{1.0f};
            int32_t TexIndex = 0;
        };

        struct ChunkRenderCache
        {
            std::vector<ChunkVertex> CpuVertices;
            std::vector<uint32_t> CpuIndices;
            std::shared_ptr<VertexArray> GpuVertexArray;
            std::shared_ptr<VertexBuffer> GpuVertexBuffer;
            std::shared_ptr<IndexBuffer> GpuIndexBuffer;
            std::vector<Assets::TextureAsset::Ptr> Textures;
            glm::vec2 LocalMin{0.0f};
            glm::vec2 LocalMax{0.0f};
            uint32_t QuadCount = 0;
            bool Dirty = true;
            bool GpuDirty = true;
            bool Empty = true;
        };

        struct ChunkLightingCache
        {
            std::vector<glm::vec4> OccluderEdges;
            glm::vec2 LocalMin{0.0f};
            glm::vec2 LocalMax{0.0f};
            bool Dirty = true;
            bool Empty = true;
        };

        std::vector<ChunkRenderCache> ChunkRenderCaches;
        std::vector<ChunkLightingCache> ChunkLightingCaches;
        int32_t RenderOrder = 0;
        glm::ivec2 CachedPaintedCellChunkGridSize = glm::ivec2(0);
        glm::ivec2 ChunkGridSize = glm::ivec2(0);
        bool CollisionEnabled = false;
        bool CastShadows = false;
        bool RenderCacheDirty = true;
        bool PaintedCellCacheDirty = true;
        bool PaintedCellRowOffsetsDirty = true;
        bool PaintedCellChunkCacheDirty = true;
        bool ChunkTopologyDirty = true;

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
            for (auto& rc : ChunkRenderCaches) rc.Dirty = true;
            ++MutationRevision;
            return static_cast<uint32_t>(TileTable.size() - 1);
        }
    };

    inline glm::vec2 GetTilemapLayerFirstCellCenter(const Grid2DComponent& grid, const TilemapLayerComponent& layer)
    {
        const glm::vec2 cellSize(std::max(0.001f, grid.CellSize.x), std::max(0.001f, grid.CellSize.y));
        return glm::vec2(grid.OriginCell.x * cellSize.x, grid.OriginCell.y * cellSize.y);
    }

    inline glm::ivec2 GetClampedGrid2DLayoutSize(const glm::ivec2& requestedGridSize)
    {
        return glm::ivec2(
            std::clamp(requestedGridSize.x, 1, kMaxTilemapLayerGridDimension),
            std::clamp(requestedGridSize.y, 1, kMaxTilemapLayerGridDimension));
    }

    inline int64_t GetTilemapCellCount64(const Grid2DComponent& grid)
    {
        return static_cast<int64_t>(std::max(1, grid.GridSize.x)) *
               static_cast<int64_t>(std::max(1, grid.GridSize.y));
    }

    inline int32_t GetTilemapCellCount(const Grid2DComponent& grid)
    {
        const int64_t count = GetTilemapCellCount64(grid);
        if (count > static_cast<int64_t>(std::numeric_limits<int32_t>::max()))
            return std::numeric_limits<int32_t>::max();
        return static_cast<int32_t>(count);
    }

    inline void EnsureTilemapLayerStorage(const Grid2DComponent& grid, TilemapLayerComponent& layer)
    {
        const int64_t cellCount64 = GetTilemapCellCount64(grid);
        if (cellCount64 <= 0)
            return;
        const size_t cellCount = static_cast<size_t>(cellCount64);
        if (layer.Tiles.size() != cellCount)
        {
            layer.Tiles.resize(cellCount, 0u);
            layer.PaintedCellCacheDirty = true;
            layer.PaintedCellRowOffsetsDirty = true;
            layer.PaintedCellChunkCacheDirty = true;
            layer.ChunkTopologyDirty = true;
            ++layer.MutationRevision;
        }
    }

    inline void ResizeTilemapLayerStorage(TilemapLayerComponent& layer,
                                          const glm::ivec2& previousGridSize,
                                          const glm::ivec2& requestedGridSize,
                                          const glm::ivec2& destinationOffset)
    {
        const glm::ivec2 clampedGridSize = GetClampedGrid2DLayoutSize(requestedGridSize);
        const int32_t oldWidth = std::max(1, previousGridSize.x);
        const int32_t oldHeight = std::max(1, previousGridSize.y);
        const int32_t newWidth = std::max(1, clampedGridSize.x);
        const int32_t newHeight = std::max(1, clampedGridSize.y);

        const int64_t newCellCount = static_cast<int64_t>(newWidth) * static_cast<int64_t>(newHeight);

        // Move (not copy!) the old data so we never hold two full-size
        // allocations simultaneously — prevents OOM for large grids.
        std::vector<uint32_t> oldTiles = std::move(layer.Tiles);
        layer.Tiles.assign(static_cast<size_t>(newCellCount), 0u);

        // Row-wise memcpy instead of per-cell loop for cache-friendly copy.
        for (int32_t y = 0; y < oldHeight; ++y)
        {
            const int32_t newY = y + destinationOffset.y;
            if (newY < 0 || newY >= newHeight)
                continue;

            const int32_t srcStartX = std::max(0, -destinationOffset.x);
            const int32_t dstStartX = std::max(0, destinationOffset.x);
            const int32_t copyWidth = std::min(oldWidth - srcStartX, newWidth - dstStartX);
            if (copyWidth <= 0)
                continue;

            const size_t srcIdx = static_cast<size_t>(y) * static_cast<size_t>(oldWidth) + static_cast<size_t>(srcStartX);
            const size_t dstIdx = static_cast<size_t>(newY) * static_cast<size_t>(newWidth) + static_cast<size_t>(dstStartX);
            if (srcIdx + static_cast<size_t>(copyWidth) > oldTiles.size() ||
                dstIdx + static_cast<size_t>(copyWidth) > layer.Tiles.size())
                continue;

            std::memcpy(&layer.Tiles[dstIdx], &oldTiles[srcIdx], static_cast<size_t>(copyWidth) * sizeof(uint32_t));
        }

        layer.RenderCacheDirty = true;
        layer.PaintedCellCacheDirty = true;
        layer.PaintedCellRowOffsetsDirty = true;
        layer.PaintedCellChunkCacheDirty = true;
        layer.ChunkTopologyDirty = true;
        ++layer.MutationRevision;
    }

    inline void RebuildPaintedCellCache(TilemapLayerComponent& layer)
    {
        layer.CachedPaintedCells.clear();
        for (size_t i = 0; i < layer.Tiles.size(); ++i)
        {
            const uint32_t tileId = layer.Tiles[i];
            if (tileId != 0u)
                layer.CachedPaintedCells.push_back({ static_cast<uint32_t>(i), tileId });
        }
        layer.PaintedCellCacheDirty = false;
        layer.PaintedCellRowOffsetsDirty = true;
        layer.PaintedCellChunkCacheDirty = true;
    }

    /// Lightweight incremental update: append a newly-painted cell to the
    /// sparse cache without scanning the whole Tiles array. Stale entries
    /// (overwritten or erased cells) are harmless because the renderer
    /// re-reads the live tile value from Tiles[] each frame.
    inline void AppendPaintedCellToCache(TilemapLayerComponent& layer, uint32_t cellIndex, uint32_t tileId)
    {
        if (tileId != 0u)
            layer.CachedPaintedCells.push_back({ cellIndex, tileId });
        layer.PaintedCellRowOffsetsDirty = true;
        layer.PaintedCellChunkCacheDirty = true;
    }

    inline void RebuildPaintedCellRowOffsets(const Grid2DComponent& grid, TilemapLayerComponent& layer)
    {
        const int32_t width = std::max(1, grid.GridSize.x);
        const int32_t height = std::max(1, grid.GridSize.y);

        std::sort(layer.CachedPaintedCells.begin(), layer.CachedPaintedCells.end(),
            [](const TilemapLayerComponent::PaintedCell& a, const TilemapLayerComponent::PaintedCell& b) {
                return a.CellIndex < b.CellIndex;
            });

        size_t writeIndex = 0;
        for (size_t readIndex = 0; readIndex < layer.CachedPaintedCells.size();)
        {
            const uint32_t cellIndex = layer.CachedPaintedCells[readIndex].CellIndex;
            size_t nextIndex = readIndex + 1;
            while (nextIndex < layer.CachedPaintedCells.size() &&
                   layer.CachedPaintedCells[nextIndex].CellIndex == cellIndex)
            {
                ++nextIndex;
            }

            if (cellIndex < layer.Tiles.size())
            {
                const uint32_t liveTileId = layer.Tiles[cellIndex];
                if (liveTileId != 0u)
                    layer.CachedPaintedCells[writeIndex++] = { cellIndex, liveTileId };
            }

            readIndex = nextIndex;
        }

        layer.CachedPaintedCells.resize(writeIndex);
        const uint32_t paintedCellCount = static_cast<uint32_t>(layer.CachedPaintedCells.size());
        layer.CachedPaintedCellRowOffsets.assign(static_cast<size_t>(height) + 1u, paintedCellCount);
        layer.CachedPaintedCellRowOffsets[static_cast<size_t>(height)] = paintedCellCount;

        for (uint32_t index = 0; index < paintedCellCount; ++index)
        {
            const auto& paintedCell = layer.CachedPaintedCells[index];
            if (paintedCell.CellIndex >= layer.Tiles.size())
                continue;

            const int32_t row = static_cast<int32_t>(paintedCell.CellIndex / static_cast<uint32_t>(width));
            if (row < 0 || row >= height)
                continue;

            if (layer.CachedPaintedCellRowOffsets[static_cast<size_t>(row)] == paintedCellCount)
                layer.CachedPaintedCellRowOffsets[static_cast<size_t>(row)] = index;
            layer.CachedPaintedCellRowOffsets[static_cast<size_t>(row) + 1u] = index + 1u;
        }

        for (int32_t row = height - 1; row >= 0; --row)
        {
            if (layer.CachedPaintedCellRowOffsets[static_cast<size_t>(row)] == paintedCellCount)
                layer.CachedPaintedCellRowOffsets[static_cast<size_t>(row)] = layer.CachedPaintedCellRowOffsets[static_cast<size_t>(row) + 1u];
        }

        layer.PaintedCellRowOffsetsDirty = false;
        layer.PaintedCellChunkCacheDirty = true;
    }

    inline void RebuildPaintedCellChunkCache(const Grid2DComponent& grid, TilemapLayerComponent& layer)
    {
        const int32_t width = std::max(1, grid.GridSize.x);
        const int32_t height = std::max(1, grid.GridSize.y);
        const int32_t chunkDimension = std::max(1, kTilemapPaintedCellChunkDimension);
        const int32_t chunkCountX = std::max(1, (width + chunkDimension - 1) / chunkDimension);
        const int32_t chunkCountY = std::max(1, (height + chunkDimension - 1) / chunkDimension);
        const size_t totalChunkCount = static_cast<size_t>(chunkCountX) * static_cast<size_t>(chunkCountY);

        layer.CachedPaintedCellChunkGridSize = glm::ivec2(chunkCountX, chunkCountY);
        layer.CachedPaintedCellChunkOffsets.assign(totalChunkCount + 1u, 0u);
        layer.CachedPaintedCellChunkIndices.clear();

        if (layer.CachedPaintedCells.empty())
        {
            layer.PaintedCellChunkCacheDirty = false;
            return;
        }

        std::vector<uint32_t> chunkCounts(totalChunkCount, 0u);
        for (uint32_t paintedCellIndex = 0; paintedCellIndex < static_cast<uint32_t>(layer.CachedPaintedCells.size()); ++paintedCellIndex)
        {
            const auto& paintedCell = layer.CachedPaintedCells[paintedCellIndex];
            if (paintedCell.CellIndex >= layer.Tiles.size())
                continue;

            const int32_t cellX = static_cast<int32_t>(paintedCell.CellIndex % static_cast<uint32_t>(width));
            const int32_t cellY = static_cast<int32_t>(paintedCell.CellIndex / static_cast<uint32_t>(width));
            const int32_t chunkX = std::clamp(cellX / chunkDimension, 0, chunkCountX - 1);
            const int32_t chunkY = std::clamp(cellY / chunkDimension, 0, chunkCountY - 1);
            const size_t chunkIndex = static_cast<size_t>(chunkY) * static_cast<size_t>(chunkCountX) + static_cast<size_t>(chunkX);
            ++chunkCounts[chunkIndex];
        }

        uint32_t runningOffset = 0u;
        for (size_t chunkIndex = 0; chunkIndex < totalChunkCount; ++chunkIndex)
        {
            layer.CachedPaintedCellChunkOffsets[chunkIndex] = runningOffset;
            runningOffset += chunkCounts[chunkIndex];
        }
        layer.CachedPaintedCellChunkOffsets[totalChunkCount] = runningOffset;
        layer.CachedPaintedCellChunkIndices.assign(static_cast<size_t>(runningOffset), 0u);

        std::vector<uint32_t> writeOffsets(layer.CachedPaintedCellChunkOffsets.begin(), layer.CachedPaintedCellChunkOffsets.end() - 1);
        for (uint32_t paintedCellIndex = 0; paintedCellIndex < static_cast<uint32_t>(layer.CachedPaintedCells.size()); ++paintedCellIndex)
        {
            const auto& paintedCell = layer.CachedPaintedCells[paintedCellIndex];
            if (paintedCell.CellIndex >= layer.Tiles.size())
                continue;

            const int32_t cellX = static_cast<int32_t>(paintedCell.CellIndex % static_cast<uint32_t>(width));
            const int32_t cellY = static_cast<int32_t>(paintedCell.CellIndex / static_cast<uint32_t>(width));
            const int32_t chunkX = std::clamp(cellX / chunkDimension, 0, chunkCountX - 1);
            const int32_t chunkY = std::clamp(cellY / chunkDimension, 0, chunkCountY - 1);
            const size_t chunkIndex = static_cast<size_t>(chunkY) * static_cast<size_t>(chunkCountX) + static_cast<size_t>(chunkX);
            layer.CachedPaintedCellChunkIndices[writeOffsets[chunkIndex]++] = paintedCellIndex;
        }

        layer.PaintedCellChunkCacheDirty = false;
    }

    // -----------------------------------------------------------------
    // Chunk topology rebuild and dirty propagation helpers
    // -----------------------------------------------------------------

    inline void RebuildChunkTopology(const Grid2DComponent& grid, TilemapLayerComponent& layer)
    {
        const int32_t width = std::max(1, grid.GridSize.x);
        const int32_t height = std::max(1, grid.GridSize.y);
        const int32_t chunkDim = std::max(1, kTilemapPaintedCellChunkDimension);
        const int32_t chunkCountX = std::max(1, (width + chunkDim - 1) / chunkDim);
        const int32_t chunkCountY = std::max(1, (height + chunkDim - 1) / chunkDim);
        const size_t totalChunks = static_cast<size_t>(chunkCountX) * static_cast<size_t>(chunkCountY);

        layer.ChunkGridSize = glm::ivec2(chunkCountX, chunkCountY);

        layer.ChunkRenderCaches.resize(totalChunks);
        layer.ChunkLightingCaches.resize(totalChunks);

        const glm::vec2 cellSize(std::max(0.001f, grid.CellSize.x), std::max(0.001f, grid.CellSize.y));
        const glm::vec2 cellGap(grid.CellGap);
        const glm::vec2 stride = cellSize + cellGap;
        const glm::vec2 origin(grid.OriginCell.x * cellSize.x, grid.OriginCell.y * cellSize.y);

        for (int32_t cy = 0; cy < chunkCountY; ++cy)
        {
            for (int32_t cx = 0; cx < chunkCountX; ++cx)
            {
                const size_t ci = static_cast<size_t>(cy) * static_cast<size_t>(chunkCountX) + static_cast<size_t>(cx);

                const int32_t cellMinX = cx * chunkDim;
                const int32_t cellMinY = cy * chunkDim;
                const int32_t cellMaxX = std::min(cellMinX + chunkDim, width);
                const int32_t cellMaxY = std::min(cellMinY + chunkDim, height);

                const glm::vec2 localMin = origin + glm::vec2(
                    static_cast<float>(cellMinX) * stride.x - cellSize.x * 0.5f,
                    static_cast<float>(cellMinY) * stride.y - cellSize.y * 0.5f);
                const glm::vec2 localMax = origin + glm::vec2(
                    static_cast<float>(cellMaxX) * stride.x - cellGap.x - cellSize.x * 0.5f + cellSize.x,
                    static_cast<float>(cellMaxY) * stride.y - cellGap.y - cellSize.y * 0.5f + cellSize.y);

                auto& rc = layer.ChunkRenderCaches[ci];
                rc.LocalMin = localMin;
                rc.LocalMax = localMax;
                rc.Dirty = true;
                rc.GpuDirty = true;

                auto& lc = layer.ChunkLightingCaches[ci];
                lc.LocalMin = localMin;
                lc.LocalMax = localMax;
                lc.Dirty = true;
            }
        }

        layer.ChunkTopologyDirty = false;
    }

    inline void MarkChunkDirtyForCell(const Grid2DComponent& grid, TilemapLayerComponent& layer, uint32_t cellIndex)
    {
        if (layer.ChunkGridSize.x <= 0 || layer.ChunkGridSize.y <= 0)
            return;

        const int32_t width = std::max(1, grid.GridSize.x);
        const int32_t chunkDim = std::max(1, kTilemapPaintedCellChunkDimension);
        const int32_t cellX = static_cast<int32_t>(cellIndex % static_cast<uint32_t>(width));
        const int32_t cellY = static_cast<int32_t>(cellIndex / static_cast<uint32_t>(width));
        const int32_t chunkX = std::clamp(cellX / chunkDim, 0, layer.ChunkGridSize.x - 1);
        const int32_t chunkY = std::clamp(cellY / chunkDim, 0, layer.ChunkGridSize.y - 1);
        const size_t ci = static_cast<size_t>(chunkY) * static_cast<size_t>(layer.ChunkGridSize.x) + static_cast<size_t>(chunkX);

        if (ci < layer.ChunkRenderCaches.size())
            layer.ChunkRenderCaches[ci].Dirty = true;
        if (ci < layer.ChunkLightingCaches.size())
            layer.ChunkLightingCaches[ci].Dirty = true;
    }

    inline void MarkChunkNeighborsDirtyForCell(const Grid2DComponent& grid, TilemapLayerComponent& layer, uint32_t cellIndex)
    {
        if (layer.ChunkGridSize.x <= 0 || layer.ChunkGridSize.y <= 0)
            return;

        const int32_t width = std::max(1, grid.GridSize.x);
        const int32_t chunkDim = std::max(1, kTilemapPaintedCellChunkDimension);
        const int32_t cellX = static_cast<int32_t>(cellIndex % static_cast<uint32_t>(width));
        const int32_t cellY = static_cast<int32_t>(cellIndex / static_cast<uint32_t>(width));
        const int32_t chunkX = cellX / chunkDim;
        const int32_t chunkY = cellY / chunkDim;
        const int32_t localX = cellX - chunkX * chunkDim;
        const int32_t localY = cellY - chunkY * chunkDim;

        auto dirtyChunk = [&](int32_t cx, int32_t cy)
        {
            if (cx < 0 || cy < 0 || cx >= layer.ChunkGridSize.x || cy >= layer.ChunkGridSize.y)
                return;
            const size_t ci = static_cast<size_t>(cy) * static_cast<size_t>(layer.ChunkGridSize.x) + static_cast<size_t>(cx);
            if (ci < layer.ChunkLightingCaches.size())
                layer.ChunkLightingCaches[ci].Dirty = true;
        };

        if (localX == 0)             dirtyChunk(chunkX - 1, chunkY);
        if (localX == chunkDim - 1)  dirtyChunk(chunkX + 1, chunkY);
        if (localY == 0)             dirtyChunk(chunkX, chunkY - 1);
        if (localY == chunkDim - 1)  dirtyChunk(chunkX, chunkY + 1);
    }

    inline void MarkAllChunksDirty(TilemapLayerComponent& layer)
    {
        for (auto& rc : layer.ChunkRenderCaches) { rc.Dirty = true; rc.GpuDirty = true; }
        for (auto& lc : layer.ChunkLightingCaches) { lc.Dirty = true; }
    }

    inline bool IsGrid2DCellInBounds(const Grid2DComponent& grid, int32_t cellX, int32_t cellY)
    {
        return cellX >= 0 && cellY >= 0 &&
            cellX < std::max(1, grid.GridSize.x) &&
            cellY < std::max(1, grid.GridSize.y);
    }

    inline size_t Grid2DCellToIndex(const Grid2DComponent& grid, int32_t cellX, int32_t cellY)
    {
        const int32_t width = std::max(1, grid.GridSize.x);
        return static_cast<size_t>(cellY * width + cellX);
    }
}
