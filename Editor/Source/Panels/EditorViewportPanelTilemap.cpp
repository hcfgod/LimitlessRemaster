#include "EditorViewportPanelShared.h"

#include "Core/Debug/Log.h"
#include "Core/Time.h"
#include "Graphics/Camera/Camera.h"
#include "Scene/Scene.h"
#include "Undo/EditorUndoService.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Limitless::EditorViewportPanel::Internal
{
    namespace
    {
        // ----- Grid2D undo/redo support ------------------------------------------

        struct Grid2DCellEdit
        {
            int32_t BeforeCellX = 0;
            int32_t BeforeCellY = 0;
            int32_t AfterCellX = 0;
            int32_t AfterCellY = 0;
            uint32_t PreviousTile = 0;
            uint32_t NewTile = 0;
        };

        struct Grid2DLayerLayoutState
        {
            glm::ivec2 GridSize = glm::ivec2(1);
            glm::vec2 OriginCell = glm::vec2(0.0f);
        };

        int64_t MakeGrid2DCellEditKey(int32_t cellX, int32_t cellY)
        {
            const uint64_t encodedKey =
                (static_cast<uint64_t>(static_cast<uint32_t>(cellX)) << 32) |
                static_cast<uint32_t>(cellY);
            return static_cast<int64_t>(encodedKey);
        }

        Grid2DLayerLayoutState CaptureGrid2DLayout(const Grid2DComponent& grid)
        {
            Grid2DLayerLayoutState state;
            state.GridSize = grid.GridSize;
            state.OriginCell = grid.OriginCell;
            return state;
        }

        glm::ivec2 ComputeGrid2DResizeDestinationOffset(const Grid2DComponent& grid,
                                                        const Grid2DLayerLayoutState& targetLayout)
        {
            return glm::ivec2(
                static_cast<int32_t>(std::lround(grid.OriginCell.x - targetLayout.OriginCell.x)),
                static_cast<int32_t>(std::lround(grid.OriginCell.y - targetLayout.OriginCell.y)));
        }

        bool AreGrid2DLayoutsEqual(const Grid2DLayerLayoutState& lhs,
                                   const Grid2DLayerLayoutState& rhs)
        {
            return lhs.GridSize == rhs.GridSize &&
                   glm::length(lhs.OriginCell - rhs.OriginCell) <= 0.0001f;
        }

        bool ApplyGrid2DLayout(Scene& scene,
                               entt::entity gridEntity,
                               const Grid2DLayerLayoutState& targetLayout)
        {
            if (!scene.IsValid(gridEntity))
                return false;
            auto& registry = scene.GetRegistry();

            auto* grid = registry.try_get<Grid2DComponent>(gridEntity);
            if (!grid)
                return false;

            const glm::ivec2 clampedGridSize = GetClampedGrid2DLayoutSize(targetLayout.GridSize);
            const Grid2DLayerLayoutState resolvedTarget{ clampedGridSize, targetLayout.OriginCell };
            const glm::ivec2 previousGridSize = grid->GridSize;
            const glm::ivec2 destinationOffset = ComputeGrid2DResizeDestinationOffset(*grid, resolvedTarget);

            if (!AreGrid2DLayoutsEqual(CaptureGrid2DLayout(*grid), resolvedTarget))
            {
                const auto children = scene.GetChildren(gridEntity);
                for (entt::entity child : children)
                {
                    auto* layer = registry.try_get<TilemapLayerComponent>(child);
                    if (!layer)
                        continue;
                    ResizeTilemapLayerStorage(*layer, previousGridSize, clampedGridSize, destinationOffset);
                }

                grid->GridSize = clampedGridSize;
                grid->OriginCell = resolvedTarget.OriginCell;
            }

            const auto children = scene.GetChildren(gridEntity);
            for (entt::entity child : children)
            {
                auto* layer = registry.try_get<TilemapLayerComponent>(child);
                if (!layer)
                    continue;
                EnsureTilemapLayerStorage(*grid, *layer);
            }
            return true;
        }

        class Grid2DPaintCommand final : public IEditorCommand
        {
        public:
            Grid2DPaintCommand(std::string label,
                               EditorUndoService* undoService,
                               entt::entity gridEntity,
                               entt::entity layerEntity,
                               Grid2DLayerLayoutState beforeLayout,
                               Grid2DLayerLayoutState afterLayout,
                               std::vector<Grid2DCellEdit> edits)
                : m_Label(std::move(label)),
                  m_UndoService(undoService),
                  m_GridEntity(gridEntity),
                  m_LayerEntity(layerEntity),
                  m_BeforeLayout(std::move(beforeLayout)),
                  m_AfterLayout(std::move(afterLayout)),
                  m_Edits(std::move(edits))
            {
            }

            bool Undo() override { return Apply(false); }
            bool Redo() override { return Apply(true); }
            const std::string& GetLabel() const override { return m_Label; }

        private:
            bool Apply(bool applyNewValues)
            {
                if (!m_UndoService)
                    return false;
                Scene* scene = m_UndoService->GetActiveScene();
                if (!scene || !scene->IsValid(m_LayerEntity))
                    return false;

                const Grid2DLayerLayoutState& targetLayout = applyNewValues ? m_AfterLayout : m_BeforeLayout;
                if (!ApplyGrid2DLayout(*scene, m_GridEntity, targetLayout))
                    return false;

                auto& registry = scene->GetRegistry();
                auto* grid = registry.try_get<Grid2DComponent>(m_GridEntity);
                auto* layer = registry.try_get<TilemapLayerComponent>(m_LayerEntity);
                if (!grid || !layer)
                    return false;
                EnsureTilemapLayerStorage(*grid, *layer);

                for (const Grid2DCellEdit& edit : m_Edits)
                {
                    const int32_t cellX = applyNewValues ? edit.AfterCellX : edit.BeforeCellX;
                    const int32_t cellY = applyNewValues ? edit.AfterCellY : edit.BeforeCellY;
                    if (!IsGrid2DCellInBounds(*grid, cellX, cellY))
                        continue;
                    const size_t idx = Grid2DCellToIndex(*grid, cellX, cellY);
                    if (idx >= layer->Tiles.size())
                        continue;
                    layer->Tiles[idx] = applyNewValues ? edit.NewTile : edit.PreviousTile;
                }
                layer->RenderCacheDirty = true;
                layer->PaintedCellCacheDirty = true;
                layer->ChunkTopologyDirty = true;
                MarkAllChunksDirty(*layer);
                ++layer->MutationRevision;
                return true;
            }

            std::string m_Label;
            EditorUndoService* m_UndoService = nullptr;
            entt::entity m_GridEntity = entt::null;
            entt::entity m_LayerEntity = entt::null;
            Grid2DLayerLayoutState m_BeforeLayout{};
            Grid2DLayerLayoutState m_AfterLayout{};
            std::vector<Grid2DCellEdit> m_Edits;
        };

        struct Grid2DPaintDragState
        {
            bool Active = false;
            entt::entity GridEntity = entt::null;
            entt::entity LayerEntity = entt::null;
            glm::ivec2 CumulativeDestinationOffset = glm::ivec2(0);
            Grid2DLayerLayoutState InitialLayout{};
            std::unordered_map<int64_t, Grid2DCellEdit> PendingEdits;
        };

        Grid2DPaintDragState& GetGrid2DPaintDragState()
        {
            static Grid2DPaintDragState state;
            return state;
        }

        void OffsetPendingGrid2DCellEdits(Grid2DPaintDragState& dragState, const glm::ivec2& offset)
        {
            if (offset == glm::ivec2(0))
                return;

            std::unordered_map<int64_t, Grid2DCellEdit> shiftedEdits;
            shiftedEdits.reserve(dragState.PendingEdits.size());
            for (auto& [_, edit] : dragState.PendingEdits)
            {
                edit.AfterCellX += offset.x;
                edit.AfterCellY += offset.y;
                shiftedEdits.emplace(MakeGrid2DCellEditKey(edit.AfterCellX, edit.AfterCellY), edit);
            }
            dragState.PendingEdits = std::move(shiftedEdits);
            dragState.CumulativeDestinationOffset += offset;
        }

        bool EnsureGrid2DPaintBoundsVisible(Scene& scene,
                                            entt::entity gridEntity,
                                            const Grid2DComponent& grid,
                                            const glm::ivec2& minCell,
                                            const glm::ivec2& maxCell,
                                            Grid2DPaintDragState& dragState,
                                            glm::ivec2& outCellOffset)
        {
            constexpr int32_t kGrid2DPaintGrowthPadding = 2;
            outCellOffset = glm::ivec2(0);

            const int32_t gridWidth = std::max(1, grid.GridSize.x);
            const int32_t gridHeight = std::max(1, grid.GridSize.y);
            const int32_t neededLeft = std::max(0, -minCell.x);
            const int32_t neededBottom = std::max(0, -minCell.y);
            const int32_t neededRight = std::max(0, maxCell.x - (gridWidth - 1));
            const int32_t neededTop = std::max(0, maxCell.y - (gridHeight - 1));
            const int32_t addLeft = neededLeft > 0 ? neededLeft + kGrid2DPaintGrowthPadding : 0;
            const int32_t addBottom = neededBottom > 0 ? neededBottom + kGrid2DPaintGrowthPadding : 0;
            const int32_t addRight = neededRight > 0 ? neededRight + kGrid2DPaintGrowthPadding : 0;
            const int32_t addTop = neededTop > 0 ? neededTop + kGrid2DPaintGrowthPadding : 0;

            if (addLeft == 0 && addBottom == 0 && addRight == 0 && addTop == 0)
                return true;

            const glm::ivec2 requestedGridSize(
                gridWidth + addLeft + addRight,
                gridHeight + addBottom + addTop);
            const int64_t requestedCellCount =
                static_cast<int64_t>(requestedGridSize.x) * static_cast<int64_t>(requestedGridSize.y);
            const double requestedTilesMiB =
                static_cast<double>(requestedCellCount * static_cast<int64_t>(sizeof(uint32_t))) / (1024.0 * 1024.0);
            if (requestedGridSize.x > kMaxTilemapLayerGridDimension ||
                requestedGridSize.y > kMaxTilemapLayerGridDimension)
            {
                LT_CORE_WARN("Tilemap paint expansion blocked for grid entity {} (current={}x{}, requested={}x{}, addL={}, addB={}, addR={}, addT={})",
                    static_cast<uint32_t>(gridEntity),
                    gridWidth,
                    gridHeight,
                    requestedGridSize.x,
                    requestedGridSize.y,
                    addLeft,
                    addBottom,
                    addRight,
                    addTop);
                return false;
            }

            const glm::ivec2 destinationOffset(addLeft, addBottom);
            Grid2DLayerLayoutState targetLayout = CaptureGrid2DLayout(grid);
            targetLayout.GridSize = requestedGridSize;
            targetLayout.OriginCell -= glm::vec2(destinationOffset);

            if (!ApplyGrid2DLayout(scene, gridEntity, targetLayout))
            {
                LT_CORE_WARN("Tilemap paint expansion failed for grid entity {} (current={}x{}, requested={}x{}, cells={}, tileDataMiB={:.2f})",
                    static_cast<uint32_t>(gridEntity),
                    gridWidth,
                    gridHeight,
                    requestedGridSize.x,
                    requestedGridSize.y,
                    requestedCellCount,
                    requestedTilesMiB);
                return false;
            }

            OffsetPendingGrid2DCellEdits(dragState, destinationOffset);
            outCellOffset = destinationOffset;
            return true;
        }

        /// Record a single cell edit for the Grid2D undo system. Writes the new
        /// value immediately and stores the old value for undo.
        void StageGrid2DEdit(const Grid2DComponent& grid,
                             TilemapLayerComponent& layer,
                             const glm::ivec2& cell,
                             uint32_t newTileValue,
                             Grid2DPaintDragState& dragState)
        {
            if (!IsGrid2DCellInBounds(grid, cell.x, cell.y))
                return;
            const size_t idx = Grid2DCellToIndex(grid, cell.x, cell.y);
            if (idx >= layer.Tiles.size())
                return;

            const uint32_t oldValue = layer.Tiles[idx];
            if (oldValue == newTileValue)
                return;

            // Only record the first change per cell within a single stroke.
            const int64_t editKey = MakeGrid2DCellEditKey(cell.x, cell.y);
            if (dragState.PendingEdits.find(editKey) == dragState.PendingEdits.end())
            {
                Grid2DCellEdit edit;
                edit.BeforeCellX = cell.x - dragState.CumulativeDestinationOffset.x;
                edit.BeforeCellY = cell.y - dragState.CumulativeDestinationOffset.y;
                edit.AfterCellX = cell.x;
                edit.AfterCellY = cell.y;
                edit.PreviousTile = oldValue;
                edit.NewTile = newTileValue;
                dragState.PendingEdits.emplace(editKey, edit);
            }
            else
            {
                dragState.PendingEdits[editKey].NewTile = newTileValue;
            }

            layer.Tiles[idx] = newTileValue;
            // Only mark render cache dirty if this tile type isn't cached yet.
            // The render cache maps tile-table indices to textures/colors and
            // doesn't need rebuilding when we merely change which cell holds
            // an already-cached tile ID. This avoids a full rebuild every frame
            // during continuous painting.
            if (newTileValue != 0u &&
                (newTileValue >= layer.CachedTileRender.size() ||
                 !layer.CachedTileRender[newTileValue].Texture))
            {
                layer.RenderCacheDirty = true;
            }
            ++layer.MutationRevision;

            // Incremental sparse-cache update: only append when a previously
            // empty cell is painted for the first time.  Overwrites and erases
            // are handled by the live Tiles[] lookup in the renderer.
            if (oldValue == 0u && newTileValue != 0u && !layer.PaintedCellCacheDirty)
                AppendPaintedCellToCache(layer, static_cast<uint32_t>(idx), newTileValue);

            MarkChunkDirtyForCell(grid, layer, static_cast<uint32_t>(idx));
            MarkChunkNeighborsDirtyForCell(grid, layer, static_cast<uint32_t>(idx));
        }
    }

    /// Compute the first cell center for a Grid2D/TilemapLayer combination.
    glm::vec2 GetGrid2DFirstCellCenter(const Grid2DComponent& grid, const TilemapLayerComponent& layer)
    {
        return GetTilemapLayerFirstCellCenter(grid, layer);
    }

    bool TryGetGrid2DCellUnderCursor(const Camera& camera,
                                     const glm::mat4& worldTransform,
                                     const Grid2DComponent& grid,
                                     const TilemapLayerComponent& layer,
                                     const ImVec2& viewportMin,
                                     const ImVec2& viewportMax,
                                     const ImVec2& mousePosition,
                                     glm::ivec2& outCell)
    {
        glm::vec3 rayOrigin(0.0f);
        glm::vec3 rayDirection(0.0f);
        if (!TryComputeViewportRay(camera, viewportMin, viewportMax, mousePosition, rayOrigin, rayDirection))
            return false;

        glm::vec3 planeNormal = glm::vec3(worldTransform[2]);
        if (glm::length(planeNormal) <= 0.000001f)
            planeNormal = glm::vec3(0.0f, 0.0f, 1.0f);
        else
            planeNormal = glm::normalize(planeNormal);

        const glm::vec3 planePoint = glm::vec3(worldTransform[3]);
        glm::vec3 worldPosition(0.0f);
        if (!TryIntersectRayWithPlane(rayOrigin, rayDirection, planePoint, planeNormal, worldPosition))
            return false;

        const glm::mat4 inverseTransform = glm::inverse(worldTransform);
        const glm::vec4 localPosition = inverseTransform * glm::vec4(worldPosition, 1.0f);
        const glm::vec2 cellSize(std::max(0.001f, grid.CellSize.x), std::max(0.001f, grid.CellSize.y));
        const glm::vec2 firstCellCenter = GetGrid2DFirstCellCenter(grid, layer);
        outCell = glm::ivec2(
            static_cast<int32_t>(std::floor((localPosition.x - firstCellCenter.x) / cellSize.x + 0.5f)),
            static_cast<int32_t>(std::floor((localPosition.y - firstCellCenter.y) / cellSize.y + 0.5f)));
        return true;
    }

    /// Simplified Grid2D tilemap editing for the new component architecture.
    /// Uses the active palette tile IDs to paint onto TilemapLayerComponent cells.
    bool DrawAndHandleGrid2DEditing(ImDrawList* drawList,
                                    Scene& scene,
                                    const Camera& camera,
                                    entt::entity gridEntity,
                                    entt::entity layerEntity,
                                    const ImVec2& viewportMin,
                                    const ImVec2& viewportMax,
                                    float viewportWidth,
                                    float viewportHeight,
                                    EditorPlayModeState playModeState,
                                    EditorUndoService* undoService,
                                    TilemapEditorState& tilemapEditorState,
                                    const std::string& activePaletteKey)
    {
        (void)activePaletteKey;
        tilemapEditorState.HasHoveredCell = false;
        if (!drawList || gridEntity == entt::null || layerEntity == entt::null)
            return false;
        if (!tilemapEditorState.Enabled)
            return false;
        if (!scene.IsValid(gridEntity) || !scene.IsValid(layerEntity))
            return false;

        auto& registry = scene.GetRegistry();
        auto* grid = registry.try_get<Grid2DComponent>(gridEntity);
        auto* layer = registry.try_get<TilemapLayerComponent>(layerEntity);
        if (!grid || !layer)
            return false;

        EnsureTilemapLayerStorage(*grid, *layer);
        tilemapEditorState.BrushSize = std::max(1, tilemapEditorState.BrushSize);

        const ImVec2 mousePosition = ImGui::GetMousePos();
        const bool mouseInViewport = mousePosition.x >= viewportMin.x && mousePosition.x <= viewportMax.x &&
                                     mousePosition.y >= viewportMin.y && mousePosition.y <= viewportMax.y;

        const float fixedDelta = Time::GetFixedDeltaTimeSeconds();
        const float interpolationAlpha = (fixedDelta > 0.0f)
            ? std::clamp(Time::GetFixedTimeAccumulatorSeconds() / fixedDelta, 0.0f, 1.0f)
            : 1.0f;
        const glm::mat4 worldTransform = scene.GetWorldTransformMatrixForRendering(gridEntity, interpolationAlpha);

        glm::ivec2 hoveredCell(0);
        const bool hasHoveredCell = mouseInViewport &&
            TryGetGrid2DCellUnderCursor(camera, worldTransform, *grid, *layer,
                                        viewportMin, viewportMax, mousePosition, hoveredCell);
        if (hasHoveredCell)
        {
            tilemapEditorState.HasHoveredCell = true;
            tilemapEditorState.HoveredCell = hoveredCell;
        }

        const glm::vec2 cellSize(std::max(0.001f, grid->CellSize.x), std::max(0.001f, grid->CellSize.y));

        // Cell highlight helper -- clips each edge individually so the
        // highlight remains visible when corners go behind the camera.
        auto drawCellHighlight = [&](const glm::ivec2& cell, ImU32 color, float thickness) {
            const glm::vec2 firstCellCenter = GetGrid2DFirstCellCenter(*grid, *layer);
            const glm::vec2 localCellCenter = firstCellCenter + glm::vec2(
                static_cast<float>(cell.x) * cellSize.x,
                static_cast<float>(cell.y) * cellSize.y);
            const glm::vec2 localMin = localCellCenter - cellSize * 0.5f;
            const glm::vec2 localMax = localCellCenter + cellSize * 0.5f;
            const glm::vec3 worldCorners[4] = {
                glm::vec3(worldTransform * glm::vec4(localMin.x, localMin.y, 0.0f, 1.0f)),
                glm::vec3(worldTransform * glm::vec4(localMax.x, localMin.y, 0.0f, 1.0f)),
                glm::vec3(worldTransform * glm::vec4(localMax.x, localMax.y, 0.0f, 1.0f)),
                glm::vec3(worldTransform * glm::vec4(localMin.x, localMax.y, 0.0f, 1.0f))
            };
            for (int i = 0; i < 4; ++i)
            {
                ImVec2 screenA, screenB;
                if (ProjectLineSegmentClipped(camera, viewportMin, viewportWidth, viewportHeight,
                        worldCorners[i], worldCorners[(i + 1) % 4], screenA, screenB))
                {
                    drawList->AddLine(screenA, screenB, color, thickness);
                }
            }
        };

        const bool canEdit = playModeState == EditorPlayModeState::Edit;
        // Avoid gating painting on ImGui's active-item state; non-interactive
        // viewport items (e.g. scene image) can keep an item active and block paint.
        const bool canCaptureMouse = canEdit && mouseInViewport;
        const bool leftMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        bool capturedMouseInput = false;

        const uint32_t paintTileValue = [&]() -> uint32_t {
            if (tilemapEditorState.PaintMode == TilemapPaintMode::Erase)
                return 0u;
            if (!tilemapEditorState.ActiveTileAssetKey.empty())
                return layer->GetOrAddTileTableEntry(tilemapEditorState.ActiveTileAssetKey);
            if (!tilemapEditorState.StampTileAssetKeys.empty() &&
                !tilemapEditorState.StampTileAssetKeys[0].empty())
            {
                return layer->GetOrAddTileTableEntry(tilemapEditorState.StampTileAssetKeys[0]);
            }
            return 0u;
        }();

        auto& paintDragState = GetGrid2DPaintDragState();

        auto finalizeGrid2DStroke = [&](const char* label) {
            if (!paintDragState.Active && paintDragState.PendingEdits.empty())
                return;
            if (!undoService)
            {
                paintDragState = {};
                return;
            }

            auto* commandLayer = registry.try_get<TilemapLayerComponent>(paintDragState.LayerEntity);
            auto* commandGrid = registry.try_get<Grid2DComponent>(paintDragState.GridEntity);
            if (!commandLayer || !commandGrid)
            {
                paintDragState = {};
                return;
            }

            EnsureTilemapLayerStorage(*commandGrid, *commandLayer);

            std::vector<Grid2DCellEdit> edits;
            edits.reserve(paintDragState.PendingEdits.size());
            for (auto& [_, edit] : paintDragState.PendingEdits)
                edits.push_back(edit);

            const Grid2DLayerLayoutState finalLayout = CaptureGrid2DLayout(*commandGrid);
            if (!edits.empty() ||
                !AreGrid2DLayoutsEqual(finalLayout, paintDragState.InitialLayout))
            {
                auto command = std::make_unique<Grid2DPaintCommand>(
                    label ? std::string(label) : std::string("Paint Grid2D"),
                    undoService,
                    paintDragState.GridEntity,
                    paintDragState.LayerEntity,
                    paintDragState.InitialLayout,
                    finalLayout,
                    std::move(edits));
                (void)undoService->ExecuteCommand(std::move(command));
            }

            paintDragState = {};
        };

        if (paintDragState.Active && paintDragState.LayerEntity != layerEntity)
            finalizeGrid2DStroke("Paint Grid2D");

        const bool leftMousePressed = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        if (canCaptureMouse && leftMousePressed && hasHoveredCell)
        {
            paintDragState.Active = true;
            paintDragState.GridEntity = gridEntity;
            paintDragState.LayerEntity = layerEntity;
            paintDragState.InitialLayout = CaptureGrid2DLayout(*grid);
            paintDragState.CumulativeDestinationOffset = glm::ivec2(0);
            paintDragState.PendingEdits.clear();
            capturedMouseInput = true;
        }

        if (paintDragState.Active && paintDragState.LayerEntity == layerEntity &&
            leftMouseDown)
        {
            capturedMouseInput = true;

            if (hasHoveredCell && canCaptureMouse)
            {
                glm::ivec2 adjustedHoveredCell = hoveredCell;
                if (tilemapEditorState.PaintMode != TilemapPaintMode::Erase)
                {
                    glm::ivec2 minPaintCell = adjustedHoveredCell;
                    glm::ivec2 maxPaintCell = adjustedHoveredCell;
                    if (tilemapEditorState.HasStamp() &&
                        (tilemapEditorState.StampSize.x > 1 || tilemapEditorState.StampSize.y > 1))
                    {
                        maxPaintCell += glm::ivec2(
                            std::max(0, tilemapEditorState.StampSize.x - 1),
                            std::max(0, tilemapEditorState.StampSize.y - 1));
                    }
                    else
                    {
                        const int32_t brushSize = std::max(1, tilemapEditorState.BrushSize);
                        const int32_t startOffset = (brushSize - 1) / 2;
                        minPaintCell -= glm::ivec2(startOffset);
                        maxPaintCell = minPaintCell + glm::ivec2(brushSize - 1, brushSize - 1);
                    }

                    glm::ivec2 expansionOffset(0);
                    if (EnsureGrid2DPaintBoundsVisible(scene, gridEntity, *grid, minPaintCell, maxPaintCell, paintDragState, expansionOffset))
                    {
                        adjustedHoveredCell += expansionOffset;
                        hoveredCell = adjustedHoveredCell;
                        tilemapEditorState.HoveredCell = adjustedHoveredCell;
                    }
                }

                if (tilemapEditorState.PaintMode == TilemapPaintMode::Erase)
                {
                    const int32_t brushSize = std::max(1, tilemapEditorState.BrushSize);
                    const int32_t startOffset = (brushSize - 1) / 2;
                    for (int32_t by = 0; by < brushSize; ++by)
                        for (int32_t bx = 0; bx < brushSize; ++bx)
                            StageGrid2DEdit(*grid, *layer,
                                adjustedHoveredCell - glm::ivec2(startOffset) + glm::ivec2(bx, by),
                                0u, paintDragState);
                }
                else if (tilemapEditorState.HasStamp() &&
                         (tilemapEditorState.StampSize.x > 1 || tilemapEditorState.StampSize.y > 1))
                {
                    for (int32_t sy = 0; sy < tilemapEditorState.StampSize.y; ++sy)
                    {
                        for (int32_t sx = 0; sx < tilemapEditorState.StampSize.x; ++sx)
                        {
                            // Flip stamp Y: palette row 0 (visual top) maps to the
                            // highest scene Y (visual top), since scene Y-axis is up
                            // while palette Y-axis is down.
                            const int32_t sceneYOffset = tilemapEditorState.StampSize.y - 1 - sy;
                            const glm::ivec2 cell = adjustedHoveredCell + glm::ivec2(sx, sceneYOffset);
                            const size_t stampIdx = static_cast<size_t>(
                                sy * tilemapEditorState.StampSize.x + sx);
                            if (stampIdx >= tilemapEditorState.StampTileAssetKeys.size())
                                continue;
                            const std::string& stampKey = tilemapEditorState.StampTileAssetKeys[stampIdx];
                            const uint32_t resolvedId = stampKey.empty()
                                ? 0u : layer->GetOrAddTileTableEntry(stampKey);
                            StageGrid2DEdit(*grid, *layer, cell, resolvedId, paintDragState);
                        }
                    }
                }
                else
                {
                    const int32_t brushSize = std::max(1, tilemapEditorState.BrushSize);
                    const int32_t startOffset = (brushSize - 1) / 2;
                    for (int32_t by = 0; by < brushSize; ++by)
                        for (int32_t bx = 0; bx < brushSize; ++bx)
                            StageGrid2DEdit(*grid, *layer,
                                adjustedHoveredCell - glm::ivec2(startOffset) + glm::ivec2(bx, by),
                                paintTileValue, paintDragState);
                }
            }
        }

        // Grid overlay -- uses near-plane clipping so lines that are
        // partially behind the camera still render their visible portion.
        // For large grids, skip the overlay entirely to avoid thousands
        // of ImGui line draws that would freeze the editor.
        constexpr int32_t kMaxGridOverlayLinesPerAxis = 256;
        if (tilemapEditorState.ShowGridOverlay &&
            grid->GridSize.x <= kMaxGridOverlayLinesPerAxis &&
            grid->GridSize.y <= kMaxGridOverlayLinesPerAxis)
        {
            const glm::vec2 firstCellCenter = GetGrid2DFirstCellCenter(*grid, *layer);
            const glm::vec2 gridBoundaryMin = firstCellCenter - cellSize * 0.5f;
            const glm::vec2 gridBoundaryMax = gridBoundaryMin + glm::vec2(grid->GridSize) * cellSize;

            for (int32_t x = 0; x <= std::max(1, grid->GridSize.x); ++x)
            {
                const float localX = gridBoundaryMin.x + static_cast<float>(x) * cellSize.x;
                const glm::vec3 worldStart = glm::vec3(worldTransform * glm::vec4(localX, gridBoundaryMin.y, 0.0f, 1.0f));
                const glm::vec3 worldEnd   = glm::vec3(worldTransform * glm::vec4(localX, gridBoundaryMax.y, 0.0f, 1.0f));
                ImVec2 screenStart, screenEnd;
                if (ProjectLineSegmentClipped(camera, viewportMin, viewportWidth, viewportHeight,
                        worldStart, worldEnd, screenStart, screenEnd))
                {
                    drawList->AddLine(screenStart, screenEnd, IM_COL32(80, 170, 255, 120), 1.0f);
                }
            }
            for (int32_t y = 0; y <= std::max(1, grid->GridSize.y); ++y)
            {
                const float localY = gridBoundaryMin.y + static_cast<float>(y) * cellSize.y;
                const glm::vec3 worldStart = glm::vec3(worldTransform * glm::vec4(gridBoundaryMin.x, localY, 0.0f, 1.0f));
                const glm::vec3 worldEnd   = glm::vec3(worldTransform * glm::vec4(gridBoundaryMax.x, localY, 0.0f, 1.0f));
                ImVec2 screenStart, screenEnd;
                if (ProjectLineSegmentClipped(camera, viewportMin, viewportWidth, viewportHeight,
                        worldStart, worldEnd, screenStart, screenEnd))
                {
                    drawList->AddLine(screenStart, screenEnd, IM_COL32(80, 170, 255, 120), 1.0f);
                }
            }
        }

        if (paintDragState.Active && !leftMouseDown)
        {
            const char* label = (tilemapEditorState.PaintMode == TilemapPaintMode::Erase)
                ? "Erase Grid2D Tiles" : "Paint Grid2D Tiles";
            finalizeGrid2DStroke(label);
        }

        if (hasHoveredCell)
        {
            const ImU32 highlightColor = (tilemapEditorState.PaintMode == TilemapPaintMode::Erase)
                ? IM_COL32(255, 80, 80, 200) : IM_COL32(85, 200, 255, 200);
            drawCellHighlight(hoveredCell, highlightColor, 2.0f);

            if (tilemapEditorState.HasStamp() &&
                (tilemapEditorState.StampSize.x > 1 || tilemapEditorState.StampSize.y > 1))
            {
                for (int32_t sy = 0; sy < tilemapEditorState.StampSize.y; ++sy)
                {
                    for (int32_t sx = 0; sx < tilemapEditorState.StampSize.x; ++sx)
                    {
                        const int32_t sceneYOffset = tilemapEditorState.StampSize.y - 1 - sy;
                        const glm::ivec2 previewCell = hoveredCell + glm::ivec2(sx, sceneYOffset);
                        if (previewCell == hoveredCell)
                            continue;
                        drawCellHighlight(previewCell, IM_COL32(85, 200, 255, 100), 1.0f);
                    }
                }
            }
        }

        return capturedMouseInput;
    }
}
