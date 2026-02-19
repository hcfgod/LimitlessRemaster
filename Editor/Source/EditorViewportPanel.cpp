#include "EditorViewportPanel.h"

#include "Assets/AssetLoadProgress.h"
#include "Editor/EditorCameraController.h"
#include "Graphics/Camera/Camera.h"
#include "Graphics/Camera/OrthographicCamera2D.h"
#include "Graphics/Camera/PerspectiveCamera3D.h"
#include "Graphics/Framebuffer.h"
#include "Graphics/Renderer2D.h"
#include "Scene/Scene.h"
#include "Undo/EditorUndoService.h"
#include "imgui/imgui.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <optional>
#include <queue>
#include <string>
#include <unordered_map>

#include <glm/glm.hpp>

namespace Limitless::EditorViewportPanel
{
    namespace
    {
        bool TryComputeDropWorldPosition(const Camera& camera,
                                         const ImVec2& viewportMin,
                                         const ImVec2& viewportMax,
                                         const ImVec2& mouseScreenPosition,
                                         glm::vec3& outWorldPosition);

        struct TilemapCellEditRecord
        {
            int32_t LayerIndex = 0;
            int32_t CellX = 0;
            int32_t CellY = 0;
            uint32_t PreviousTile = 0;
            uint32_t NewTile = 0;
            uint32_t PreviousData = 0;
            uint32_t NewData = 0;
        };

        class TilemapPaintCommand final : public IEditorCommand
        {
        public:
            TilemapPaintCommand(std::string label,
                                EditorUndoService* undoService,
                                entt::entity targetEntity,
                                std::vector<TilemapCellEditRecord> edits)
                : m_Label(std::move(label)),
                  m_UndoService(undoService),
                  m_TargetEntity(targetEntity),
                  m_Edits(std::move(edits))
            {
            }

            bool Undo() override
            {
                return Apply(false);
            }

            bool Redo() override
            {
                return Apply(true);
            }

            const std::string& GetLabel() const override
            {
                return m_Label;
            }

        private:
            bool Apply(bool applyNewValues)
            {
                if (!m_UndoService)
                    return false;
                Scene* scene = m_UndoService->GetActiveScene();
                if (!scene || !scene->IsValid(m_TargetEntity))
                    return false;

                auto& registry = scene->GetRegistry();
                auto* tilemap = registry.try_get<TilemapComponent>(m_TargetEntity);
                if (!tilemap)
                    return false;
                tilemap->EnsureLayerStorage();

                for (const TilemapCellEditRecord& edit : m_Edits)
                {
                    if (edit.LayerIndex < 0 || edit.LayerIndex >= static_cast<int32_t>(tilemap->Layers.size()))
                        continue;
                    if (!IsTilemapCellInBounds(*tilemap, edit.CellX, edit.CellY))
                        continue;

                    const size_t cellIndex = TilemapCellToIndex(*tilemap, edit.CellX, edit.CellY);
                    auto& layer = tilemap->Layers[static_cast<size_t>(edit.LayerIndex)];
                    if (cellIndex >= layer.Tiles.size() || cellIndex >= layer.PerTileData.size())
                        continue;

                    layer.Tiles[cellIndex] = applyNewValues ? edit.NewTile : edit.PreviousTile;
                    layer.PerTileData[cellIndex] = applyNewValues ? edit.NewData : edit.PreviousData;
                }
                return true;
            }

        private:
            std::string m_Label;
            EditorUndoService* m_UndoService = nullptr;
            entt::entity m_TargetEntity = entt::null;
            std::vector<TilemapCellEditRecord> m_Edits;
        };

        struct TilemapPaintDragState final
        {
            bool Active = false;
            entt::entity Entity = entt::null;
            glm::ivec2 StartCell = glm::ivec2(0);
            glm::ivec2 CurrentCell = glm::ivec2(0);
            std::unordered_map<uint64_t, TilemapCellEditRecord> PendingEdits;
        };

        TilemapPaintDragState& GetTilemapPaintDragState()
        {
            static TilemapPaintDragState state;
            return state;
        }

        uint64_t BuildTilemapCellEditKey(int32_t layerIndex, size_t cellIndex)
        {
            return (static_cast<uint64_t>(static_cast<uint32_t>(layerIndex)) << 32u) |
                   static_cast<uint64_t>(static_cast<uint32_t>(cellIndex));
        }

        glm::vec2 GetTilemapFirstCellCenter(const TilemapComponent& tilemap)
        {
            const int32_t gridWidth = std::max(1, tilemap.GridSize.x);
            const int32_t gridHeight = std::max(1, tilemap.GridSize.y);
            const glm::vec2 safeCellSize(std::max(0.001f, tilemap.CellSize.x), std::max(0.001f, tilemap.CellSize.y));
            return -0.5f * glm::vec2(gridWidth - 1, gridHeight - 1) * safeCellSize;
        }

        bool TryGetTilemapHoveredCell(const Camera& camera,
                                      const Scene& scene,
                                      entt::entity tilemapEntity,
                                      const TilemapComponent& tilemap,
                                      const ImVec2& viewportMin,
                                      const ImVec2& viewportMax,
                                      const ImVec2& mousePosition,
                                      glm::ivec2& outCell)
        {
            glm::vec3 worldPosition(0.0f);
            if (!TryComputeDropWorldPosition(camera, viewportMin, viewportMax, mousePosition, worldPosition))
                return false;

            const glm::mat4 inverseTransform = glm::inverse(scene.GetWorldTransformMatrix(tilemapEntity));
            const glm::vec4 localPosition = inverseTransform * glm::vec4(worldPosition, 1.0f);
            const glm::vec2 firstCellCenter = GetTilemapFirstCellCenter(tilemap);
            const glm::vec2 safeCellSize(std::max(0.001f, tilemap.CellSize.x), std::max(0.001f, tilemap.CellSize.y));
            const glm::vec2 mapMin = firstCellCenter - safeCellSize * 0.5f;
            const int32_t cellX = static_cast<int32_t>(std::floor((localPosition.x - mapMin.x) / safeCellSize.x));
            const int32_t cellY = static_cast<int32_t>(std::floor((localPosition.y - mapMin.y) / safeCellSize.y));
            if (!IsTilemapCellInBounds(tilemap, cellX, cellY))
                return false;
            outCell = glm::ivec2(cellX, cellY);
            return true;
        }

        void AddBrushCells(const TilemapComponent& tilemap,
                           const glm::ivec2& centerCell,
                           int32_t brushSize,
                           std::vector<glm::ivec2>& outCells)
        {
            outCells.clear();
            const int32_t clampedBrushSize = std::max(1, brushSize);
            const int32_t startOffset = (clampedBrushSize - 1) / 2;
            const glm::ivec2 start = centerCell - glm::ivec2(startOffset, startOffset);
            for (int32_t y = 0; y < clampedBrushSize; ++y)
            {
                for (int32_t x = 0; x < clampedBrushSize; ++x)
                {
                    const glm::ivec2 cell = start + glm::ivec2(x, y);
                    if (!IsTilemapCellInBounds(tilemap, cell.x, cell.y))
                        continue;
                    outCells.push_back(cell);
                }
            }
        }

        void AddRectangleCells(const TilemapComponent& tilemap,
                               const glm::ivec2& startCell,
                               const glm::ivec2& endCell,
                               std::vector<glm::ivec2>& outCells)
        {
            outCells.clear();
            const int32_t minX = std::min(startCell.x, endCell.x);
            const int32_t minY = std::min(startCell.y, endCell.y);
            const int32_t maxX = std::max(startCell.x, endCell.x);
            const int32_t maxY = std::max(startCell.y, endCell.y);
            for (int32_t y = minY; y <= maxY; ++y)
            {
                for (int32_t x = minX; x <= maxX; ++x)
                {
                    if (!IsTilemapCellInBounds(tilemap, x, y))
                        continue;
                    outCells.emplace_back(x, y);
                }
            }
        }

        bool StageTilemapEdit(TilemapComponent& tilemap,
                              int32_t layerIndex,
                              const glm::ivec2& cell,
                              uint32_t tileValue,
                              bool writeCustomData,
                              uint32_t customData,
                              std::unordered_map<uint64_t, TilemapCellEditRecord>& pendingEdits)
        {
            if (layerIndex < 0 || layerIndex >= static_cast<int32_t>(tilemap.Layers.size()))
                return false;
            if (!IsTilemapCellInBounds(tilemap, cell.x, cell.y))
                return false;

            tilemap.EnsureLayerStorage();
            const size_t cellIndex = TilemapCellToIndex(tilemap, cell.x, cell.y);
            auto& layer = tilemap.Layers[static_cast<size_t>(layerIndex)];
            if (cellIndex >= layer.Tiles.size() || cellIndex >= layer.PerTileData.size())
                return false;

            const uint32_t oldTile = layer.Tiles[cellIndex];
            const uint32_t oldData = layer.PerTileData[cellIndex];
            const uint32_t newData = writeCustomData ? customData : oldData;
            if (oldTile == tileValue && oldData == newData)
                return false;

            const uint64_t key = BuildTilemapCellEditKey(layerIndex, cellIndex);
            auto editIt = pendingEdits.find(key);
            if (editIt == pendingEdits.end())
            {
                TilemapCellEditRecord edit;
                edit.LayerIndex = layerIndex;
                edit.CellX = cell.x;
                edit.CellY = cell.y;
                edit.PreviousTile = oldTile;
                edit.NewTile = tileValue;
                edit.PreviousData = oldData;
                edit.NewData = newData;
                pendingEdits.emplace(key, std::move(edit));
            }
            else
            {
                editIt->second.NewTile = tileValue;
                editIt->second.NewData = newData;
            }

            layer.Tiles[cellIndex] = tileValue;
            layer.PerTileData[cellIndex] = newData;
            return true;
        }
        // Renderer2D quad vertices in local space (must match Renderer2D::DrawQuad).
        constexpr std::array<glm::vec4, 4> kQuadLocalPositions = {
            glm::vec4(-0.5f, -0.5f, 0.0f, 1.0f),
            glm::vec4(0.5f, -0.5f, 0.0f, 1.0f),
            glm::vec4(0.5f, 0.5f, 0.0f, 1.0f),
            glm::vec4(-0.5f, 0.5f, 0.0f, 1.0f),
        };

        float Sign2D(const ImVec2& p1, const ImVec2& p2, const ImVec2& p3)
        {
            return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y);
        }

        bool PointInTriangle(const ImVec2& point, const ImVec2& a, const ImVec2& b, const ImVec2& c)
        {
            const float d1 = Sign2D(point, a, b);
            const float d2 = Sign2D(point, b, c);
            const float d3 = Sign2D(point, c, a);

            const bool hasNeg = (d1 < 0.0f) || (d2 < 0.0f) || (d3 < 0.0f);
            const bool hasPos = (d1 > 0.0f) || (d2 > 0.0f) || (d3 > 0.0f);
            return !(hasNeg && hasPos);
        }

        bool PointInProjectedQuad(const ImVec2& point, const std::array<ImVec2, 4>& quad)
        {
            // Quad is convex; treat as two triangles (0,1,2) and (2,3,0).
            return PointInTriangle(point, quad[0], quad[1], quad[2]) || PointInTriangle(point, quad[2], quad[3], quad[0]);
        }

        bool TryComputeDropWorldPosition(const Camera& camera,
                                         const ImVec2& viewportMin,
                                         const ImVec2& viewportMax,
                                         const ImVec2& mouseScreenPosition,
                                         glm::vec3& outWorldPosition)
        {
            const float viewportWidth = viewportMax.x - viewportMin.x;
            const float viewportHeight = viewportMax.y - viewportMin.y;
            if (viewportWidth <= 0.0f || viewportHeight <= 0.0f)
                return false;

            const float normalizedX = (mouseScreenPosition.x - viewportMin.x) / viewportWidth;
            const float normalizedY = (mouseScreenPosition.y - viewportMin.y) / viewportHeight;
            const float ndcX = normalizedX * 2.0f - 1.0f;
            const float ndcY = 1.0f - normalizedY * 2.0f;

            const glm::mat4 inverseViewProjection = glm::inverse(camera.GetViewProjectionMatrix());
            if (camera.GetType() == CameraType::Orthographic2D)
            {
                const glm::vec4 world = inverseViewProjection * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
                if (world.w == 0.0f)
                    return false;
                outWorldPosition = glm::vec3(world) / world.w;
                outWorldPosition.z = 0.0f;
                return true;
            }

            const glm::vec4 nearWorldH = inverseViewProjection * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
            const glm::vec4 farWorldH = inverseViewProjection * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
            if (nearWorldH.w == 0.0f || farWorldH.w == 0.0f)
                return false;

            const glm::vec3 nearWorld = glm::vec3(nearWorldH) / nearWorldH.w;
            const glm::vec3 farWorld = glm::vec3(farWorldH) / farWorldH.w;
            const glm::vec3 direction = glm::normalize(farWorld - nearWorld);

            // Place prefab where the ray intersects the world Z=0 plane.
            constexpr float kTargetPlaneZ = 0.0f;
            if (std::abs(direction.z) > 0.0001f)
            {
                const float distance = (kTargetPlaneZ - nearWorld.z) / direction.z;
                outWorldPosition = nearWorld + direction * distance;
            }
            else
            {
                outWorldPosition = nearWorld + direction * 5.0f;
                outWorldPosition.z = kTargetPlaneZ;
            }

            return true;
        }

        std::optional<entt::entity> PickTopmostSpriteEntityAtPoint(Scene& scene,
                                                                   const Camera& camera,
                                                                   const ImVec2& viewportMin,
                                                                   float viewportWidth,
                                                                   float viewportHeight,
                                                                   const ImVec2& mouseScreenPosition)
        {
            auto& registry = scene.GetRegistry();
            auto view = registry.view<TransformComponent, SpriteComponent>();
            if (view.begin() == view.end())
                return std::nullopt;

            const glm::mat4& viewProjection = camera.GetViewProjectionMatrix();
            entt::entity bestEntity = entt::null;
            float bestWorldZ = -std::numeric_limits<float>::infinity();
            int32_t bestSiblingOrder = std::numeric_limits<int32_t>::min();

            for (entt::entity entity : view)
            {
                const glm::mat4 model = scene.GetWorldTransformMatrix(entity);

                std::array<ImVec2, 4> projected{};
                bool anyBehindCamera = false;
                for (size_t i = 0; i < projected.size(); ++i)
                {
                    const glm::vec4 world = model * kQuadLocalPositions[i];
                    const glm::vec4 clip = viewProjection * world;
                    if (clip.w == 0.0f)
                    {
                        anyBehindCamera = true;
                        break;
                    }

                    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
                    // Outside clip volume is fine; the point-in-quad test will fail naturally.
                    const float pixelX = (ndc.x * 0.5f + 0.5f) * viewportWidth;
                    const float pixelY = (1.0f - (ndc.y * 0.5f + 0.5f)) * viewportHeight;
                    projected[i] = ImVec2(viewportMin.x + pixelX, viewportMin.y + pixelY);
                }

                if (anyBehindCamera)
                    continue;

                if (!PointInProjectedQuad(mouseScreenPosition, projected))
                    continue;

                const glm::mat4 worldTransform = model;
                const float worldZ = worldTransform[3].z;
                const auto* hierarchy = registry.try_get<HierarchyComponent>(entity);
                const int32_t siblingOrder = hierarchy ? hierarchy->SiblingOrder : 0;

                // Topmost = rendered last. Render sort uses increasing Z and increasing sibling order.
                if (worldZ > bestWorldZ || (worldZ == bestWorldZ && siblingOrder > bestSiblingOrder))
                {
                    bestWorldZ = worldZ;
                    bestSiblingOrder = siblingOrder;
                    bestEntity = entity;
                }
            }

            if (bestEntity == entt::null)
                return std::nullopt;
            return bestEntity;
        }

        bool WorldToViewportPoint(const Camera& camera,
                                  const ImVec2& viewportMin,
                                  float viewportWidth,
                                  float viewportHeight,
                                  const glm::vec3& worldPoint,
                                  ImVec2& outPoint)
        {
            if (viewportWidth <= 0.0f || viewportHeight <= 0.0f)
                return false;

            const glm::vec4 clip = camera.GetViewProjectionMatrix() * glm::vec4(worldPoint, 1.0f);
            if (std::abs(clip.w) <= 0.000001f)
                return false;

            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            const float pixelX = (ndc.x * 0.5f + 0.5f) * viewportWidth;
            const float pixelY = (1.0f - (ndc.y * 0.5f + 0.5f)) * viewportHeight;
            outPoint = ImVec2(viewportMin.x + pixelX, viewportMin.y + pixelY);
            return true;
        }

        enum class ColliderHandleKind : uint8_t
        {
            None = 0,
            BoxOffset,
            BoxCorner0,
            BoxCorner1,
            BoxCorner2,
            BoxCorner3,
            CircleOffset,
            CircleRadius
        };

        struct ColliderDragState final
        {
            bool Active = false;
            entt::entity Entity = entt::null;
            ColliderHandleKind Handle = ColliderHandleKind::None;
            const char* CommitLabel = nullptr;
        };

        ColliderDragState& GetColliderDragState()
        {
            static ColliderDragState state;
            return state;
        }

        enum class LightingHandleKind : uint8_t
        {
            None = 0,
            DirectionalDirection,
            PointRadius,
            OccluderPoint
        };

        struct LightingDragState final
        {
            bool Active = false;
            entt::entity Entity = entt::null;
            LightingHandleKind Handle = LightingHandleKind::None;
            int PointIndex = -1;
            const char* CommitLabel = nullptr;
        };

        LightingDragState& GetLightingDragState()
        {
            static LightingDragState state;
            return state;
        }

        bool IsMouseNearPoint(const ImVec2& mousePosition, const ImVec2& point, float radiusPixels)
        {
            const float dx = mousePosition.x - point.x;
            const float dy = mousePosition.y - point.y;
            return (dx * dx + dy * dy) <= radiusPixels * radiusPixels;
        }

        bool DrawAndHandleColliderGizmos(ImDrawList* drawList,
                                         Scene& scene,
                                         const Camera& camera,
                                         entt::entity selectedEntity,
                                         const ImVec2& viewportMin,
                                         const ImVec2& viewportMax,
                                         float viewportWidth,
                                         float viewportHeight,
                                         EditorPlayModeState playModeState,
                                         EditorUndoService* undoService)
        {
            if (!drawList || selectedEntity == entt::null || !scene.IsValid(selectedEntity))
                return false;

            auto& dragState = GetColliderDragState();
            if (!scene.IsEntityEnabledInHierarchy(selectedEntity))
            {
                if (dragState.Active && dragState.Entity == selectedEntity && undoService)
                    undoService->CancelInteractiveSceneMutation();
                if (dragState.Entity == selectedEntity)
                    dragState = {};
                return false;
            }
            auto& registry = scene.GetRegistry();
            if (!registry.try_get<TransformComponent>(selectedEntity))
                return false;

            const bool canEdit = playModeState == EditorPlayModeState::Edit;
            const glm::mat4 worldTransform = scene.GetWorldTransformMatrix(selectedEntity);
            const glm::mat4 inverseWorldTransform = glm::inverse(worldTransform);
            const ImVec2 mousePosition = ImGui::GetMousePos();
            const bool mouseInViewport = mousePosition.x >= viewportMin.x && mousePosition.x <= viewportMax.x &&
                                         mousePosition.y >= viewportMin.y && mousePosition.y <= viewportMax.y;
            constexpr float handleRadiusPixels = 7.0f;

            // If the selected entity changed mid-drag, abort the pending operation safely.
            if (dragState.Active && dragState.Entity != selectedEntity)
            {
                if (undoService)
                    undoService->CancelInteractiveSceneMutation();
                dragState = {};
            }

            auto commitOrCancelDrag = [&](bool commit) {
                if (!dragState.Active)
                    return;
                if (undoService)
                {
                    if (commit && dragState.CommitLabel)
                        (void)undoService->CommitInteractiveSceneMutation(dragState.CommitLabel);
                    else
                        undoService->CancelInteractiveSceneMutation();
                }
                dragState = {};
            };

            auto updateLocalPointFromMouse = [&](glm::vec2& localPointOut) -> bool {
                glm::vec3 worldPoint{};
                if (!TryComputeDropWorldPosition(camera, viewportMin, viewportMax, mousePosition, worldPoint))
                    return false;
                const glm::vec4 localPoint = inverseWorldTransform * glm::vec4(worldPoint, 1.0f);
                localPointOut = glm::vec2(localPoint.x, localPoint.y);
                return true;
            };

            if (auto* boxCollider2D = registry.try_get<BoxCollider2DComponent>(selectedEntity))
            {
                const glm::vec2 halfSize = boxCollider2D->Size * 0.5f;
                const glm::vec3 localCorners[4] = {
                    glm::vec3(boxCollider2D->Offset.x - halfSize.x, boxCollider2D->Offset.y - halfSize.y, 0.0f),
                    glm::vec3(boxCollider2D->Offset.x + halfSize.x, boxCollider2D->Offset.y - halfSize.y, 0.0f),
                    glm::vec3(boxCollider2D->Offset.x + halfSize.x, boxCollider2D->Offset.y + halfSize.y, 0.0f),
                    glm::vec3(boxCollider2D->Offset.x - halfSize.x, boxCollider2D->Offset.y + halfSize.y, 0.0f)
                };

                ImVec2 projectedCorners[4]{};
                bool valid = true;
                for (int i = 0; i < 4; ++i)
                {
                    const glm::vec4 worldCorner = worldTransform * glm::vec4(localCorners[i], 1.0f);
                    if (!WorldToViewportPoint(camera, viewportMin, viewportWidth, viewportHeight, glm::vec3(worldCorner), projectedCorners[i]))
                    {
                        valid = false;
                        break;
                    }
                }

                ImVec2 projectedOffset{};
                const glm::vec4 worldOffset = worldTransform * glm::vec4(boxCollider2D->Offset, 0.0f, 1.0f);
                valid = valid && WorldToViewportPoint(camera, viewportMin, viewportWidth, viewportHeight, glm::vec3(worldOffset), projectedOffset);
                if (valid)
                {
                    drawList->AddLine(projectedCorners[0], projectedCorners[1], IM_COL32(90, 200, 255, 255), 2.0f);
                    drawList->AddLine(projectedCorners[1], projectedCorners[2], IM_COL32(90, 200, 255, 255), 2.0f);
                    drawList->AddLine(projectedCorners[2], projectedCorners[3], IM_COL32(90, 200, 255, 255), 2.0f);
                    drawList->AddLine(projectedCorners[3], projectedCorners[0], IM_COL32(90, 200, 255, 255), 2.0f);

                    drawList->AddCircleFilled(projectedOffset, handleRadiusPixels, IM_COL32(75, 220, 140, 245));
                    for (const ImVec2& point : projectedCorners)
                        drawList->AddCircleFilled(point, handleRadiusPixels, IM_COL32(90, 200, 255, 245));

                    ColliderHandleKind hoveredHandle = ColliderHandleKind::None;
                    if (canEdit && mouseInViewport && !dragState.Active)
                    {
                        if (IsMouseNearPoint(mousePosition, projectedOffset, handleRadiusPixels + 3.0f))
                        {
                            hoveredHandle = ColliderHandleKind::BoxOffset;
                        }
                        else
                        {
                            for (int i = 0; i < 4; ++i)
                            {
                                if (!IsMouseNearPoint(mousePosition, projectedCorners[i], handleRadiusPixels + 3.0f))
                                    continue;
                                hoveredHandle = static_cast<ColliderHandleKind>(static_cast<uint8_t>(ColliderHandleKind::BoxCorner0) + static_cast<uint8_t>(i));
                                break;
                            }
                        }
                    }

                    if (hoveredHandle != ColliderHandleKind::None)
                    {
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                        {
                            if (undoService)
                                undoService->BeginInteractiveSceneMutation();
                            dragState.Active = true;
                            dragState.Entity = selectedEntity;
                            dragState.Handle = hoveredHandle;
                            dragState.CommitLabel = (hoveredHandle == ColliderHandleKind::BoxOffset)
                                ? "Edit Box Collider Offset"
                                : "Edit Box Collider Size";
                        }
                    }

                    if (dragState.Active && dragState.Entity == selectedEntity &&
                        (dragState.Handle == ColliderHandleKind::BoxOffset ||
                         dragState.Handle == ColliderHandleKind::BoxCorner0 ||
                         dragState.Handle == ColliderHandleKind::BoxCorner1 ||
                         dragState.Handle == ColliderHandleKind::BoxCorner2 ||
                         dragState.Handle == ColliderHandleKind::BoxCorner3))
                    {
                        glm::vec2 localPoint(0.0f);
                        if (updateLocalPointFromMouse(localPoint))
                        {
                            if (dragState.Handle == ColliderHandleKind::BoxOffset)
                            {
                                boxCollider2D->Offset = localPoint;
                            }
                            else
                            {
                                const glm::vec2 delta = localPoint - boxCollider2D->Offset;
                                boxCollider2D->Size = glm::max(glm::abs(glm::vec2(delta.x, delta.y)) * 2.0f, glm::vec2(0.02f));
                            }
                        }

                        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                            commitOrCancelDrag(true);
                    }
                }
            }

            if (auto* circleCollider2D = registry.try_get<CircleCollider2DComponent>(selectedEntity))
            {
                const glm::vec4 worldCenter = worldTransform * glm::vec4(circleCollider2D->Offset.x, circleCollider2D->Offset.y, 0.0f, 1.0f);
                const glm::vec4 worldRadiusPoint = worldTransform * glm::vec4(circleCollider2D->Offset.x + circleCollider2D->Radius, circleCollider2D->Offset.y, 0.0f, 1.0f);

                ImVec2 centerPoint{};
                ImVec2 radiusPoint{};
                if (WorldToViewportPoint(camera, viewportMin, viewportWidth, viewportHeight, glm::vec3(worldCenter), centerPoint) &&
                    WorldToViewportPoint(camera, viewportMin, viewportWidth, viewportHeight, glm::vec3(worldRadiusPoint), radiusPoint))
                {
                    const float radiusPixels = std::sqrt((radiusPoint.x - centerPoint.x) * (radiusPoint.x - centerPoint.x) +
                                                         (radiusPoint.y - centerPoint.y) * (radiusPoint.y - centerPoint.y));
                    drawList->AddCircle(centerPoint, radiusPixels, IM_COL32(255, 190, 70, 255), 48, 2.0f);
                    drawList->AddCircleFilled(centerPoint, handleRadiusPixels, IM_COL32(75, 220, 140, 245));
                    drawList->AddCircleFilled(radiusPoint, handleRadiusPixels, IM_COL32(255, 190, 70, 245));

                    ColliderHandleKind hoveredHandle = ColliderHandleKind::None;
                    if (canEdit && mouseInViewport && !dragState.Active)
                    {
                        if (IsMouseNearPoint(mousePosition, centerPoint, handleRadiusPixels + 3.0f))
                            hoveredHandle = ColliderHandleKind::CircleOffset;
                        else if (IsMouseNearPoint(mousePosition, radiusPoint, handleRadiusPixels + 3.0f))
                            hoveredHandle = ColliderHandleKind::CircleRadius;
                    }

                    if (hoveredHandle != ColliderHandleKind::None)
                    {
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                        {
                            if (undoService)
                                undoService->BeginInteractiveSceneMutation();
                            dragState.Active = true;
                            dragState.Entity = selectedEntity;
                            dragState.Handle = hoveredHandle;
                            dragState.CommitLabel = (hoveredHandle == ColliderHandleKind::CircleOffset)
                                ? "Edit Circle Collider Offset"
                                : "Edit Circle Collider Radius";
                        }
                    }

                    if (dragState.Active && dragState.Entity == selectedEntity &&
                        (dragState.Handle == ColliderHandleKind::CircleOffset ||
                         dragState.Handle == ColliderHandleKind::CircleRadius))
                    {
                        glm::vec2 localPoint(0.0f);
                        if (updateLocalPointFromMouse(localPoint))
                        {
                            if (dragState.Handle == ColliderHandleKind::CircleOffset)
                            {
                                circleCollider2D->Offset = localPoint;
                            }
                            else
                            {
                                circleCollider2D->Radius = std::max(0.01f, glm::length(localPoint - circleCollider2D->Offset));
                            }
                        }

                        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                            commitOrCancelDrag(true);
                    }
                }
            }

            if (dragState.Active && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
                commitOrCancelDrag(true);

            if (auto* joint2D = registry.try_get<Joint2DComponent>(selectedEntity))
            {
                if (joint2D->ConnectedEntity != entt::null && scene.IsValid(joint2D->ConnectedEntity))
                {
                    const glm::mat4 connectedWorldTransform = scene.GetWorldTransformMatrix(joint2D->ConnectedEntity);
                    const glm::vec4 worldAnchorA = worldTransform * glm::vec4(joint2D->AnchorA, 0.0f, 1.0f);
                    const glm::vec4 worldAnchorB = connectedWorldTransform * glm::vec4(joint2D->AnchorB, 0.0f, 1.0f);

                    ImVec2 projectedA{};
                    ImVec2 projectedB{};
                    if (WorldToViewportPoint(camera, viewportMin, viewportWidth, viewportHeight, glm::vec3(worldAnchorA), projectedA) &&
                        WorldToViewportPoint(camera, viewportMin, viewportWidth, viewportHeight, glm::vec3(worldAnchorB), projectedB))
                    {
                        drawList->AddLine(projectedA, projectedB, IM_COL32(130, 255, 130, 255), 2.0f);
                        drawList->AddCircleFilled(projectedA, 4.0f, IM_COL32(130, 255, 130, 255));
                        drawList->AddCircleFilled(projectedB, 4.0f, IM_COL32(130, 255, 130, 255));
                    }
                }
            }

            return dragState.Active;
        }

        bool DrawAndHandleLightingGizmos(ImDrawList* drawList,
                                         Scene& scene,
                                         const Camera& camera,
                                         entt::entity selectedEntity,
                                         const ImVec2& viewportMin,
                                         const ImVec2& viewportMax,
                                         float viewportWidth,
                                         float viewportHeight,
                                         EditorPlayModeState playModeState,
                                         EditorUndoService* undoService)
        {
            if (!drawList || selectedEntity == entt::null || !scene.IsValid(selectedEntity))
                return false;

            auto& dragState = GetLightingDragState();
            if (!scene.IsEntityEnabledInHierarchy(selectedEntity))
            {
                if (dragState.Active && dragState.Entity == selectedEntity && undoService)
                    undoService->CancelInteractiveSceneMutation();
                if (dragState.Entity == selectedEntity)
                    dragState = {};
                return false;
            }
            auto& registry = scene.GetRegistry();
            auto* transform = registry.try_get<TransformComponent>(selectedEntity);
            if (!transform)
                return false;

            const bool canEdit = playModeState == EditorPlayModeState::Edit;
            const glm::mat4 worldTransform = scene.GetWorldTransformMatrix(selectedEntity);
            const glm::mat4 inverseWorldTransform = glm::inverse(worldTransform);
            const ImVec2 mousePosition = ImGui::GetMousePos();
            const bool mouseInViewport = mousePosition.x >= viewportMin.x && mousePosition.x <= viewportMax.x &&
                                         mousePosition.y >= viewportMin.y && mousePosition.y <= viewportMax.y;
            constexpr float handleRadiusPixels = 7.0f;

            if (dragState.Active && dragState.Entity != selectedEntity)
            {
                if (undoService)
                    undoService->CancelInteractiveSceneMutation();
                dragState = {};
            }

            auto commitOrCancelDrag = [&](bool commit) {
                if (!dragState.Active)
                    return;
                if (undoService)
                {
                    if (commit && dragState.CommitLabel)
                        (void)undoService->CommitInteractiveSceneMutation(dragState.CommitLabel);
                    else
                        undoService->CancelInteractiveSceneMutation();
                }
                dragState = {};
            };

            auto updateWorldPointFromMouse = [&](glm::vec3& worldPointOut) -> bool {
                return TryComputeDropWorldPosition(camera, viewportMin, viewportMax, mousePosition, worldPointOut);
            };

            if (auto* directionalLight = registry.try_get<DirectionalLight2DComponent>(selectedEntity))
            {
                glm::vec2 worldDirection = directionalLight->UseEntityRotation
                    ? glm::vec2(worldTransform[0].x, worldTransform[0].y)
                    : directionalLight->Direction;
                if (glm::length(worldDirection) <= 0.0001f)
                    worldDirection = glm::vec2(0.0f, -1.0f);
                worldDirection = glm::normalize(worldDirection);

                const glm::vec3 worldOrigin = glm::vec3(worldTransform[3]);
                constexpr float arrowLengthWorld = 1.5f;
                const glm::vec3 worldEndpoint = worldOrigin + glm::vec3(worldDirection * arrowLengthWorld, 0.0f);

                ImVec2 originPoint{};
                ImVec2 endpointPoint{};
                if (WorldToViewportPoint(camera, viewportMin, viewportWidth, viewportHeight, worldOrigin, originPoint) &&
                    WorldToViewportPoint(camera, viewportMin, viewportWidth, viewportHeight, worldEndpoint, endpointPoint))
                {
                    drawList->AddLine(originPoint, endpointPoint, IM_COL32(255, 230, 110, 240), 2.0f);
                    drawList->AddCircleFilled(originPoint, handleRadiusPixels - 1.0f, IM_COL32(255, 230, 110, 220));
                    drawList->AddCircleFilled(endpointPoint, handleRadiusPixels, IM_COL32(255, 180, 60, 245));

                    if (canEdit && mouseInViewport && !dragState.Active &&
                        IsMouseNearPoint(mousePosition, endpointPoint, handleRadiusPixels + 3.0f))
                    {
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                        {
                            if (undoService)
                                undoService->BeginInteractiveSceneMutation();
                            dragState.Active = true;
                            dragState.Entity = selectedEntity;
                            dragState.Handle = LightingHandleKind::DirectionalDirection;
                            dragState.PointIndex = -1;
                            dragState.CommitLabel = directionalLight->UseEntityRotation
                                ? "Edit Directional Light Rotation"
                                : "Edit Directional Light Direction";
                        }
                    }
                }
            }

            if (auto* pointLight = registry.try_get<PointLight2DComponent>(selectedEntity))
            {
                const glm::vec4 worldCenter4 = worldTransform * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
                const glm::vec4 worldRadiusPoint4 = worldTransform * glm::vec4(pointLight->Radius, 0.0f, 0.0f, 1.0f);

                ImVec2 centerPoint{};
                ImVec2 radiusPoint{};
                if (WorldToViewportPoint(camera, viewportMin, viewportWidth, viewportHeight, glm::vec3(worldCenter4), centerPoint) &&
                    WorldToViewportPoint(camera, viewportMin, viewportWidth, viewportHeight, glm::vec3(worldRadiusPoint4), radiusPoint))
                {
                    const float radiusPixels = std::sqrt((radiusPoint.x - centerPoint.x) * (radiusPoint.x - centerPoint.x) +
                                                         (radiusPoint.y - centerPoint.y) * (radiusPoint.y - centerPoint.y));
                    drawList->AddCircle(centerPoint, radiusPixels, IM_COL32(255, 185, 80, 235), 64, 2.0f);
                    drawList->AddCircleFilled(radiusPoint, handleRadiusPixels, IM_COL32(255, 185, 80, 245));

                    if (canEdit && mouseInViewport && !dragState.Active &&
                        IsMouseNearPoint(mousePosition, radiusPoint, handleRadiusPixels + 3.0f))
                    {
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                        {
                            if (undoService)
                                undoService->BeginInteractiveSceneMutation();
                            dragState.Active = true;
                            dragState.Entity = selectedEntity;
                            dragState.Handle = LightingHandleKind::PointRadius;
                            dragState.PointIndex = -1;
                            dragState.CommitLabel = "Edit Point Light Radius";
                        }
                    }
                }
            }

            if (auto* shadowOccluder = registry.try_get<ShadowOccluder2DComponent>(selectedEntity))
            {
                if (shadowOccluder->Source == ShadowOccluder2DComponent::SourceMode::ManualPolygon &&
                    shadowOccluder->PolygonPoints.size() >= 2)
                {
                    std::vector<ImVec2> projectedPoints;
                    projectedPoints.reserve(shadowOccluder->PolygonPoints.size());
                    for (const glm::vec2& localPoint : shadowOccluder->PolygonPoints)
                    {
                        const glm::vec4 worldPoint = worldTransform * glm::vec4(localPoint, 0.0f, 1.0f);
                        ImVec2 projectedPoint{};
                        if (!WorldToViewportPoint(camera, viewportMin, viewportWidth, viewportHeight, glm::vec3(worldPoint), projectedPoint))
                            continue;
                        projectedPoints.push_back(projectedPoint);
                    }

                    if (projectedPoints.size() >= 2)
                    {
                        for (size_t pointIndex = 0; pointIndex + 1 < projectedPoints.size(); ++pointIndex)
                            drawList->AddLine(projectedPoints[pointIndex], projectedPoints[pointIndex + 1], IM_COL32(140, 240, 255, 235), 2.0f);
                        if (shadowOccluder->Closed && projectedPoints.size() >= 3)
                            drawList->AddLine(projectedPoints.back(), projectedPoints.front(), IM_COL32(140, 240, 255, 235), 2.0f);

                        for (size_t pointIndex = 0; pointIndex < projectedPoints.size(); ++pointIndex)
                        {
                            drawList->AddCircleFilled(projectedPoints[pointIndex], handleRadiusPixels - 1.0f, IM_COL32(90, 200, 255, 240));
                            if (!canEdit || !mouseInViewport || dragState.Active)
                                continue;
                            if (!IsMouseNearPoint(mousePosition, projectedPoints[pointIndex], handleRadiusPixels + 3.0f))
                                continue;
                            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                            {
                                if (undoService)
                                    undoService->BeginInteractiveSceneMutation();
                                dragState.Active = true;
                                dragState.Entity = selectedEntity;
                                dragState.Handle = LightingHandleKind::OccluderPoint;
                                dragState.PointIndex = static_cast<int>(pointIndex);
                                dragState.CommitLabel = "Edit Shadow Occluder Point";
                            }
                            break;
                        }
                    }
                }
            }

            if (dragState.Active && dragState.Entity == selectedEntity)
            {
                glm::vec3 mouseWorldPoint(0.0f);
                const bool hasMouseWorldPoint = updateWorldPointFromMouse(mouseWorldPoint);

                if (hasMouseWorldPoint && dragState.Handle == LightingHandleKind::DirectionalDirection)
                {
                    if (auto* directionalLight = registry.try_get<DirectionalLight2DComponent>(selectedEntity))
                    {
                        const glm::vec3 worldOrigin = glm::vec3(worldTransform[3]);
                        glm::vec2 direction = glm::vec2(mouseWorldPoint.x - worldOrigin.x, mouseWorldPoint.y - worldOrigin.y);
                        if (glm::length(direction) > 0.0001f)
                        {
                            direction = glm::normalize(direction);
                            if (directionalLight->UseEntityRotation)
                            {
                                if (auto* mutableTransform = registry.try_get<TransformComponent>(selectedEntity))
                                    mutableTransform->Rotation.z = glm::degrees(std::atan2(direction.y, direction.x));
                            }
                            else
                            {
                                directionalLight->Direction = direction;
                            }
                        }
                    }
                }
                else if (hasMouseWorldPoint && dragState.Handle == LightingHandleKind::PointRadius)
                {
                    if (auto* pointLight = registry.try_get<PointLight2DComponent>(selectedEntity))
                    {
                        const glm::vec4 localPoint = inverseWorldTransform * glm::vec4(mouseWorldPoint, 1.0f);
                        pointLight->Radius = std::max(0.01f, glm::length(glm::vec2(localPoint.x, localPoint.y)));
                    }
                }
                else if (hasMouseWorldPoint && dragState.Handle == LightingHandleKind::OccluderPoint && dragState.PointIndex >= 0)
                {
                    if (auto* shadowOccluder = registry.try_get<ShadowOccluder2DComponent>(selectedEntity))
                    {
                        if (dragState.PointIndex < static_cast<int>(shadowOccluder->PolygonPoints.size()))
                        {
                            const glm::vec4 localPoint = inverseWorldTransform * glm::vec4(mouseWorldPoint, 1.0f);
                            shadowOccluder->PolygonPoints[dragState.PointIndex] = glm::vec2(localPoint.x, localPoint.y);
                        }
                    }
                }

                if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                    commitOrCancelDrag(true);
            }

            if (dragState.Active && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
                commitOrCancelDrag(true);

            return dragState.Active;
        }

        bool DrawAndHandleTilemapEditing(ImDrawList* drawList,
                                         Scene& scene,
                                         const Camera& camera,
                                         entt::entity selectedEntity,
                                         const ImVec2& viewportMin,
                                         const ImVec2& viewportMax,
                                         float viewportWidth,
                                         float viewportHeight,
                                         EditorPlayModeState playModeState,
                                         EditorUndoService* undoService,
                                         TilemapEditorState& tilemapEditorState)
        {
            tilemapEditorState.HasHoveredCell = false;
            if (!drawList || selectedEntity == entt::null || !scene.IsValid(selectedEntity))
                return false;
            if (!tilemapEditorState.Enabled)
                return false;
            if (!scene.IsEntityEnabledInHierarchy(selectedEntity))
            {
                auto& paintDragState = GetTilemapPaintDragState();
                if (paintDragState.Active && paintDragState.Entity == selectedEntity)
                    paintDragState = {};
                return false;
            }

            auto& registry = scene.GetRegistry();
            auto* tilemap = registry.try_get<TilemapComponent>(selectedEntity);
            if (!tilemap)
                return false;
            tilemap->EnsureLayerStorage();
            if (tilemap->Layers.empty())
                return false;

            tilemapEditorState.ActiveLayerIndex = std::clamp(tilemapEditorState.ActiveLayerIndex, 0, static_cast<int32_t>(tilemap->Layers.size()) - 1);
            tilemapEditorState.BrushSize = std::max(1, tilemapEditorState.BrushSize);

            auto& paintDragState = GetTilemapPaintDragState();
            auto finalizeStroke = [&](const char* label) {
                if (!paintDragState.Active && paintDragState.PendingEdits.empty())
                    return;
                if (!undoService)
                {
                    paintDragState = {};
                    return;
                }

                std::vector<TilemapCellEditRecord> edits;
                edits.reserve(paintDragState.PendingEdits.size());
                for (auto& [_, edit] : paintDragState.PendingEdits)
                    edits.push_back(edit);

                if (!edits.empty())
                {
                    std::sort(edits.begin(), edits.end(), [](const TilemapCellEditRecord& left, const TilemapCellEditRecord& right) {
                        if (left.LayerIndex != right.LayerIndex)
                            return left.LayerIndex < right.LayerIndex;
                        if (left.CellY != right.CellY)
                            return left.CellY < right.CellY;
                        return left.CellX < right.CellX;
                    });
                    auto command = std::make_unique<TilemapPaintCommand>(
                        label ? std::string(label) : std::string("Paint Tilemap"),
                        undoService,
                        selectedEntity,
                        std::move(edits));
                    (void)undoService->ExecuteCommand(std::move(command));
                }

                paintDragState = {};
            };

            if (paintDragState.Active && paintDragState.Entity != selectedEntity)
                finalizeStroke("Paint Tilemap");

            const ImVec2 mousePosition = ImGui::GetMousePos();
            const bool mouseInViewport = mousePosition.x >= viewportMin.x && mousePosition.x <= viewportMax.x &&
                                         mousePosition.y >= viewportMin.y && mousePosition.y <= viewportMax.y;
            glm::ivec2 hoveredCell(0);
            const bool hasHoveredCell = mouseInViewport &&
                TryGetTilemapHoveredCell(camera, scene, selectedEntity, *tilemap, viewportMin, viewportMax, mousePosition, hoveredCell);
            if (hasHoveredCell)
            {
                tilemapEditorState.HasHoveredCell = true;
                tilemapEditorState.HoveredCell = hoveredCell;
            }

            const glm::vec2 safeCellSize(std::max(0.001f, tilemap->CellSize.x), std::max(0.001f, tilemap->CellSize.y));
            const glm::vec2 firstCellCenter = GetTilemapFirstCellCenter(*tilemap);
            const glm::vec2 gridBoundaryMin = firstCellCenter - safeCellSize * 0.5f;
            const glm::vec2 gridBoundaryMax = gridBoundaryMin + glm::vec2(tilemap->GridSize) * safeCellSize;
            const glm::mat4 worldTransform = scene.GetWorldTransformMatrix(selectedEntity);

            if (tilemapEditorState.ShowGridOverlay)
            {
                for (int32_t x = 0; x <= std::max(1, tilemap->GridSize.x); ++x)
                {
                    const float localX = gridBoundaryMin.x + static_cast<float>(x) * safeCellSize.x;
                    const glm::vec4 worldStart = worldTransform * glm::vec4(localX, gridBoundaryMin.y, 0.0f, 1.0f);
                    const glm::vec4 worldEnd = worldTransform * glm::vec4(localX, gridBoundaryMax.y, 0.0f, 1.0f);
                    ImVec2 screenStart;
                    ImVec2 screenEnd;
                    if (WorldToViewportPoint(camera, viewportMin, viewportWidth, viewportHeight, glm::vec3(worldStart), screenStart) &&
                        WorldToViewportPoint(camera, viewportMin, viewportWidth, viewportHeight, glm::vec3(worldEnd), screenEnd))
                    {
                        drawList->AddLine(screenStart, screenEnd, IM_COL32(80, 170, 255, 120), 1.0f);
                    }
                }
                for (int32_t y = 0; y <= std::max(1, tilemap->GridSize.y); ++y)
                {
                    const float localY = gridBoundaryMin.y + static_cast<float>(y) * safeCellSize.y;
                    const glm::vec4 worldStart = worldTransform * glm::vec4(gridBoundaryMin.x, localY, 0.0f, 1.0f);
                    const glm::vec4 worldEnd = worldTransform * glm::vec4(gridBoundaryMax.x, localY, 0.0f, 1.0f);
                    ImVec2 screenStart;
                    ImVec2 screenEnd;
                    if (WorldToViewportPoint(camera, viewportMin, viewportWidth, viewportHeight, glm::vec3(worldStart), screenStart) &&
                        WorldToViewportPoint(camera, viewportMin, viewportWidth, viewportHeight, glm::vec3(worldEnd), screenEnd))
                    {
                        drawList->AddLine(screenStart, screenEnd, IM_COL32(80, 170, 255, 120), 1.0f);
                    }
                }
            }

            auto drawCellHighlight = [&](const glm::ivec2& cell, ImU32 color, float thickness) {
                const glm::vec2 localCellCenter = firstCellCenter + glm::vec2(static_cast<float>(cell.x) * safeCellSize.x, static_cast<float>(cell.y) * safeCellSize.y);
                const glm::vec2 localMin = localCellCenter - safeCellSize * 0.5f;
                const glm::vec2 localMax = localCellCenter + safeCellSize * 0.5f;
                const glm::vec4 worldMin = worldTransform * glm::vec4(localMin.x, localMin.y, 0.0f, 1.0f);
                const glm::vec4 worldMax = worldTransform * glm::vec4(localMax.x, localMax.y, 0.0f, 1.0f);
                ImVec2 screenMin;
                ImVec2 screenMax;
                if (!WorldToViewportPoint(camera, viewportMin, viewportWidth, viewportHeight, glm::vec3(worldMin), screenMin))
                    return;
                if (!WorldToViewportPoint(camera, viewportMin, viewportWidth, viewportHeight, glm::vec3(worldMax), screenMax))
                    return;
                ImVec2 minPoint(std::min(screenMin.x, screenMax.x), std::min(screenMin.y, screenMax.y));
                ImVec2 maxPoint(std::max(screenMin.x, screenMax.x), std::max(screenMin.y, screenMax.y));
                drawList->AddRect(minPoint, maxPoint, color, 0.0f, 0, thickness);
            };

            const bool canEdit = playModeState == EditorPlayModeState::Edit;
            const bool canCaptureMouse = canEdit && mouseInViewport && !ImGui::IsAnyItemActive();
            const bool leftMousePressed = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
            const bool leftMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
            const bool leftMouseReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
            const uint32_t paintTileValue = (tilemapEditorState.PaintMode == TilemapPaintMode::Erase) ? 0u : std::max(1u, tilemapEditorState.ActiveTileId);

            std::vector<glm::ivec2> pendingCells;

            if (tilemapEditorState.PaintMode == TilemapPaintMode::Fill)
            {
                if (canCaptureMouse && leftMousePressed && hasHoveredCell)
                {
                    const int32_t layerIndex = tilemapEditorState.ActiveLayerIndex;
                    if (layerIndex >= 0 && layerIndex < static_cast<int32_t>(tilemap->Layers.size()))
                    {
                        auto& layer = tilemap->Layers[static_cast<size_t>(layerIndex)];
                        const size_t startIndex = TilemapCellToIndex(*tilemap, hoveredCell.x, hoveredCell.y);
                        if (startIndex < layer.Tiles.size())
                        {
                            const uint32_t oldTile = layer.Tiles[startIndex];
                            const uint32_t oldData = layer.PerTileData[startIndex];
                            const uint32_t newData = tilemapEditorState.PaintCustomData
                                ? tilemapEditorState.ActiveCustomData
                                : oldData;
                            if (!(oldTile == paintTileValue && oldData == newData))
                            {
                                const int32_t gridWidth = std::max(1, tilemap->GridSize.x);
                                const int32_t gridHeight = std::max(1, tilemap->GridSize.y);
                                std::vector<uint8_t> visited(static_cast<size_t>(gridWidth * gridHeight), 0u);
                                std::queue<glm::ivec2> frontier;
                                frontier.push(hoveredCell);
                                while (!frontier.empty())
                                {
                                    const glm::ivec2 cell = frontier.front();
                                    frontier.pop();
                                    if (!IsTilemapCellInBounds(*tilemap, cell.x, cell.y))
                                        continue;
                                    const size_t cellIndex = TilemapCellToIndex(*tilemap, cell.x, cell.y);
                                    if (cellIndex >= visited.size() || visited[cellIndex] != 0u)
                                        continue;
                                    visited[cellIndex] = 1u;
                                    if (cellIndex >= layer.Tiles.size() || layer.Tiles[cellIndex] != oldTile)
                                        continue;
                                    pendingCells.push_back(cell);
                                    frontier.push(cell + glm::ivec2(1, 0));
                                    frontier.push(cell + glm::ivec2(-1, 0));
                                    frontier.push(cell + glm::ivec2(0, 1));
                                    frontier.push(cell + glm::ivec2(0, -1));
                                }

                                std::unordered_map<uint64_t, TilemapCellEditRecord> fillEdits;
                                for (const glm::ivec2& cell : pendingCells)
                                    (void)StageTilemapEdit(*tilemap, layerIndex, cell, paintTileValue,
                                                           tilemapEditorState.PaintCustomData,
                                                           tilemapEditorState.ActiveCustomData,
                                                           fillEdits);

                                if (!fillEdits.empty() && undoService)
                                {
                                    std::vector<TilemapCellEditRecord> edits;
                                    edits.reserve(fillEdits.size());
                                    for (auto& [_, edit] : fillEdits)
                                        edits.push_back(edit);
                                    auto command = std::make_unique<TilemapPaintCommand>("Fill Tilemap", undoService, selectedEntity, std::move(edits));
                                    (void)undoService->ExecuteCommand(std::move(command));
                                }
                            }
                        }
                    }
                }
            }
            else if (tilemapEditorState.PaintMode == TilemapPaintMode::Rectangle)
            {
                if (canCaptureMouse && leftMousePressed && hasHoveredCell)
                {
                    paintDragState.Active = true;
                    paintDragState.Entity = selectedEntity;
                    paintDragState.StartCell = hoveredCell;
                    paintDragState.CurrentCell = hoveredCell;
                    paintDragState.PendingEdits.clear();
                }

                if (paintDragState.Active && paintDragState.Entity == selectedEntity)
                {
                    if (hasHoveredCell)
                        paintDragState.CurrentCell = hoveredCell;
                    AddRectangleCells(*tilemap, paintDragState.StartCell, paintDragState.CurrentCell, pendingCells);
                    for (const glm::ivec2& cell : pendingCells)
                        drawCellHighlight(cell, IM_COL32(255, 180, 85, 180), 2.0f);

                    if (leftMouseReleased)
                    {
                        for (const glm::ivec2& cell : pendingCells)
                        {
                            (void)StageTilemapEdit(*tilemap,
                                                   tilemapEditorState.ActiveLayerIndex,
                                                   cell,
                                                   paintTileValue,
                                                   tilemapEditorState.PaintCustomData,
                                                   tilemapEditorState.ActiveCustomData,
                                                   paintDragState.PendingEdits);
                        }
                        finalizeStroke("Rectangle Paint Tilemap");
                    }
                    else if (!leftMouseDown)
                    {
                        paintDragState = {};
                    }
                }
            }
            else
            {
                if (canCaptureMouse && leftMousePressed && hasHoveredCell)
                {
                    paintDragState.Active = true;
                    paintDragState.Entity = selectedEntity;
                    paintDragState.StartCell = hoveredCell;
                    paintDragState.CurrentCell = hoveredCell;
                    paintDragState.PendingEdits.clear();
                }

                if (paintDragState.Active && paintDragState.Entity == selectedEntity && leftMouseDown)
                {
                    if (hasHoveredCell)
                    {
                        paintDragState.CurrentCell = hoveredCell;
                        AddBrushCells(*tilemap, hoveredCell, tilemapEditorState.BrushSize, pendingCells);
                        for (const glm::ivec2& cell : pendingCells)
                        {
                            (void)StageTilemapEdit(*tilemap,
                                                   tilemapEditorState.ActiveLayerIndex,
                                                   cell,
                                                   paintTileValue,
                                                   tilemapEditorState.PaintCustomData,
                                                   tilemapEditorState.ActiveCustomData,
                                                   paintDragState.PendingEdits);
                        }
                    }
                }

                if (paintDragState.Active && leftMouseReleased)
                {
                    finalizeStroke(tilemapEditorState.PaintMode == TilemapPaintMode::Erase ? "Erase Tilemap" : "Paint Tilemap");
                }
                else if (paintDragState.Active && !leftMouseDown)
                {
                    finalizeStroke(tilemapEditorState.PaintMode == TilemapPaintMode::Erase ? "Erase Tilemap" : "Paint Tilemap");
                }
            }

            if (hasHoveredCell)
            {
                AddBrushCells(*tilemap, hoveredCell, tilemapEditorState.BrushSize, pendingCells);
                const ImU32 previewColor = (tilemapEditorState.PaintMode == TilemapPaintMode::Erase)
                    ? IM_COL32(255, 90, 90, 180)
                    : IM_COL32(100, 255, 120, 180);
                for (const glm::ivec2& cell : pendingCells)
                    drawCellHighlight(cell, previewColor, 2.0f);
            }

            return paintDragState.Active;
        }

        void DrawSelectedPhysicsOverlays(ImDrawList* drawList,
                                         Scene& scene,
                                         const Camera& camera,
                                         entt::entity selectedEntity,
                                         const ImVec2& viewportMin,
                                         const ImVec2& viewportMax,
                                         float viewportWidth,
                                         float viewportHeight,
                                         EditorPlayModeState playModeState,
                                         EditorUndoService* undoService)
        {
            (void)DrawAndHandleColliderGizmos(
                drawList, scene, camera, selectedEntity, viewportMin, viewportMax, viewportWidth, viewportHeight, playModeState, undoService);
            (void)DrawAndHandleLightingGizmos(
                drawList, scene, camera, selectedEntity, viewportMin, viewportMax, viewportWidth, viewportHeight, playModeState, undoService);
        }
    }

    void Draw(uint32_t& sceneViewWidthPixels,
              uint32_t& sceneViewHeightPixels,
              std::shared_ptr<Framebuffer>& sceneViewFramebuffer,
              bool& sceneViewFocused,
              bool& sceneViewHovered,
              uint32_t& gameViewWidthPixels,
              uint32_t& gameViewHeightPixels,
              std::shared_ptr<Framebuffer>& gameViewFramebuffer,
              bool& gameViewFocused,
              bool& gameViewHovered,
              bool& focusGameViewRequested,
              EditorCameraController* editorCameraController,
              Camera* sceneViewCamera,
              Camera* gameViewCamera,
              Scene* scene,
              EditorPlayModeState playModeState,
              const std::function<void(uint32_t, uint32_t)>& ensureSceneViewFramebuffer,
              const std::function<void(uint32_t, uint32_t)>& ensureGameViewFramebuffer,
              const char* scenePayloadId,
              const std::function<void(const std::string&)>& onSceneDropped,
              const char* prefabPayloadId,
              const std::function<void(const std::string&, const glm::vec3&)>& onPrefabDropped,
              entt::entity& selectedEntity,
              EditorUndoService* undoService,
              const char* materialPayloadId,
              std::string& selectedTextureAssetKey,
              Assets::TextureAsset::Ptr& cachedTextureAsset,
              std::string& selectedMaterialAssetKey,
              Assets::MaterialAsset::Ptr& cachedMaterialAsset,
              std::string& selectedNativeScriptAssetKey,
              bool showFpsOverlay,
              TilemapEditorState* tilemapEditorState,
              bool showMissingGameplayCameraOverlay)
    {
        (void)editorCameraController;

        auto sanitizeViewportDimension = [](float value) -> uint32_t {
            if (!std::isfinite(value) || value <= 1.0f)
                return 0;
            return static_cast<uint32_t>(std::floor(value));
        };

        auto drawLoadingOverlay = [scene](const ImVec2& minPos, const ImVec2& maxPos) {
            const ImVec2 center((minPos.x + maxPos.x) * 0.5f, (minPos.y + maxPos.y) * 0.5f);
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(minPos, maxPos, IM_COL32(0, 0, 0, 255));

            bool sceneObjectsReady = scene ? scene->IsSceneObjectsInitialized() : false;
            bool physicsReady = scene ? scene->IsPhysicsWorldInitializedForLoading() : false;
            const bool shaderReady = Renderer2D::IsShaderReady();

            float assetProgressAverage = 1.0f;
            std::string assetStatusText;
            const std::vector<std::string> activeProgressKeys = Assets::AssetLoadProgress::GetActiveKeys();
            if (!activeProgressKeys.empty())
            {
                float accumulatedProgress = 0.0f;
                for (const std::string& key : activeProgressKeys)
                {
                    const auto info = Assets::AssetLoadProgress::GetProgress(key);
                    if (!info.has_value())
                    {
                        accumulatedProgress += 1.0f;
                        continue;
                    }

                    accumulatedProgress += std::clamp(info->Progress, 0.0f, 1.0f);
                    if (assetStatusText.empty() && !info->Status.empty())
                        assetStatusText = info->Status;
                }
                assetProgressAverage = accumulatedProgress / static_cast<float>(activeProgressKeys.size());
            }

            std::string loadingText = "Loading scene...";
            if (!sceneObjectsReady)
                loadingText = "Initializing scene objects...";
            else if (!physicsReady)
                loadingText = "Initializing physics world...";
            else if (!shaderReady)
                loadingText = "Compiling shaders...";
            else if (!assetStatusText.empty())
                loadingText = assetStatusText;
            else
                loadingText = "Loading assets...";

            const float sceneObjectsProgress = sceneObjectsReady ? 1.0f : 0.0f;
            const float physicsProgress = physicsReady ? 1.0f : 0.0f;
            const float shaderProgress = shaderReady ? 1.0f : 0.0f;
            const float progressValue = std::clamp(
                (sceneObjectsProgress + physicsProgress + shaderProgress + assetProgressAverage) * 0.25f,
                0.0f,
                1.0f);

            const ImVec2 textSize = ImGui::CalcTextSize(loadingText.c_str());
            drawList->AddText(ImVec2(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f - 24.0f),
                              IM_COL32(255, 255, 255, 255),
                              loadingText.c_str());

            const float barWidth = 200.0f;
            const float barHeight = 8.0f;
            const ImVec2 barMin(center.x - barWidth * 0.5f, center.y - barHeight * 0.5f + 8.0f);
            const ImVec2 barMax(center.x + barWidth * 0.5f, center.y + barHeight * 0.5f + 8.0f);
            drawList->AddRectFilled(barMin, barMax, IM_COL32(50, 50, 55, 255));
            const ImVec2 fillMax(barMin.x + barWidth * progressValue, barMax.y);
            drawList->AddRectFilled(barMin, fillMax, IM_COL32(80, 140, 220, 255));
        };

        auto drawShaderCompileOverlay = [](const ImVec2& minPos, const ImVec2& maxPos) {
            const ImVec2 center((minPos.x + maxPos.x) * 0.5f, (minPos.y + maxPos.y) * 0.5f);
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(minPos, maxPos, IM_COL32(0, 0, 0, 255));

            const char* loadingText = "Compiling shaders...";
            float progressValue = 0.0f;
            const auto progressInfo = Assets::AssetLoadProgress::GetProgress(Renderer2D::GetDefaultShaderKey());
            if (progressInfo.has_value())
            {
                loadingText = progressInfo->Status.empty() ? "Compiling shaders..." : progressInfo->Status.c_str();
                progressValue = progressInfo->Progress;
            }
            else
            {
                progressValue = std::fmod(static_cast<float>(ImGui::GetTime() * 0.8), 1.0f);
            }

            const ImVec2 textSize = ImGui::CalcTextSize(loadingText);
            drawList->AddText(ImVec2(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f - 24.0f),
                              IM_COL32(255, 255, 255, 255),
                              loadingText);

            const float barWidth = 200.0f;
            const float barHeight = 8.0f;
            const ImVec2 barMin(center.x - barWidth * 0.5f, center.y - barHeight * 0.5f + 8.0f);
            const ImVec2 barMax(center.x + barWidth * 0.5f, center.y + barHeight * 0.5f + 8.0f);
            drawList->AddRectFilled(barMin, barMax, IM_COL32(50, 50, 55, 255));
            const ImVec2 fillMax(barMin.x + barWidth * progressValue, barMax.y);
            drawList->AddRectFilled(barMin, fillMax, IM_COL32(80, 140, 220, 255));
        };

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("Scene View");

        sceneViewFocused = ImGui::IsWindowFocused();
        sceneViewHovered = ImGui::IsWindowHovered();
        const bool skipSceneRender = ImGui::IsWindowCollapsed();
        const ImVec2 sceneViewSize = ImGui::GetContentRegionAvail();
        const uint32_t sceneWidth = sanitizeViewportDimension(sceneViewSize.x);
        const uint32_t sceneHeight = sanitizeViewportDimension(sceneViewSize.y);

        if (!skipSceneRender && sceneWidth > 0 && sceneHeight > 0)
        {
            ensureSceneViewFramebuffer(sceneWidth, sceneHeight);
            sceneViewWidthPixels = sceneWidth;
            sceneViewHeightPixels = sceneHeight;

            if (sceneViewCamera)
                sceneViewCamera->SetViewportSize(sceneWidth, sceneHeight);

            const bool isSceneLoading = scene && scene->GetLoadState() == Scene::LoadState::Loading;
            if (sceneViewCamera && scene && sceneViewFramebuffer && !isSceneLoading)
                SceneRenderer::RenderToViewport(*scene, *sceneViewCamera, sceneViewFramebuffer, sceneWidth, sceneHeight);

            if (sceneViewFramebuffer && sceneViewFramebuffer->GetColorAttachment())
            {
                ImGui::Image(
                    (ImTextureID)(void*)(uintptr_t)sceneViewFramebuffer->GetColorAttachment()->GetRendererID(),
                    ImVec2(static_cast<float>(sceneWidth), static_cast<float>(sceneHeight)),
                    ImVec2(0, 1),
                    ImVec2(1, 0));

                if (scene && sceneViewCamera && !isSceneLoading)
                {
                    const ImVec2 viewportMin = ImGui::GetItemRectMin();
                    const ImVec2 viewportMax = ImGui::GetItemRectMax();
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    DrawSelectedPhysicsOverlays(drawList,
                                                *scene,
                                                *sceneViewCamera,
                                                selectedEntity,
                                                viewportMin,
                                                viewportMax,
                                                static_cast<float>(sceneWidth),
                                                static_cast<float>(sceneHeight),
                                                playModeState,
                                                undoService);
                    if (tilemapEditorState)
                    {
                        (void)DrawAndHandleTilemapEditing(drawList,
                                                          *scene,
                                                          *sceneViewCamera,
                                                          selectedEntity,
                                                          viewportMin,
                                                          viewportMax,
                                                          static_cast<float>(sceneWidth),
                                                          static_cast<float>(sceneHeight),
                                                          playModeState,
                                                          undoService,
                                                          *tilemapEditorState);
                    }
                }

                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(scenePayloadId))
                    {
                        const char* key = static_cast<const char*>(payload->Data);
                        if (key && key[0] && onSceneDropped)
                            onSceneDropped(key);
                    }
                    if (prefabPayloadId)
                    {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(prefabPayloadId))
                        {
                            const char* key = static_cast<const char*>(payload->Data);
                            if (key && key[0] && onPrefabDropped)
                            {
                                glm::vec3 worldPosition(0.0f);
                                if (sceneViewCamera)
                                {
                                    const ImVec2 viewportMin = ImGui::GetItemRectMin();
                                    const ImVec2 viewportMax = ImGui::GetItemRectMax();
                                    const ImVec2 mousePos = ImGui::GetMousePos();
                                    if (!TryComputeDropWorldPosition(*sceneViewCamera, viewportMin, viewportMax, mousePos, worldPosition))
                                        worldPosition = glm::vec3(0.0f);
                                }
                                onPrefabDropped(key, worldPosition);
                            }
                        }
                    }
                    if (materialPayloadId)
                    {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(materialPayloadId))
                        {
                            const char* key = static_cast<const char*>(payload->Data);
                            if (key && key[0] && scene && sceneViewCamera)
                            {
                                const ImVec2 viewportMin = ImGui::GetItemRectMin();
                                const ImVec2 viewportMax = ImGui::GetItemRectMax();
                                const ImVec2 mousePos = ImGui::GetMousePos();

                                entt::entity targetEntity = entt::null;
                                if (mousePos.x >= viewportMin.x && mousePos.x <= viewportMax.x &&
                                    mousePos.y >= viewportMin.y && mousePos.y <= viewportMax.y)
                                {
                                    const float viewportWidth = viewportMax.x - viewportMin.x;
                                    const float viewportHeight = viewportMax.y - viewportMin.y;
                                    const auto picked = PickTopmostSpriteEntityAtPoint(*scene, *sceneViewCamera, viewportMin, viewportWidth, viewportHeight, mousePos);
                                    if (picked.has_value())
                                        targetEntity = *picked;
                                }

                                if (targetEntity == entt::null && selectedEntity != entt::null && scene->IsValid(selectedEntity))
                                    targetEntity = selectedEntity;

                                if (targetEntity != entt::null && scene->IsValid(targetEntity))
                                {
                                    auto& registry = scene->GetRegistry();
                                    if (registry.all_of<SpriteComponent>(targetEntity))
                                    {
                                        auto* material = registry.try_get<MaterialComponent>(targetEntity);
                                        if (!material)
                                            material = &registry.emplace<MaterialComponent>(targetEntity);

                                        material->MaterialKey = key;
                                        material->CachedMaterial.reset();
                                        material->MaterialLoadAttempted = false;

                                        selectedEntity = targetEntity;
                                        selectedTextureAssetKey.clear();
                                        cachedTextureAsset.reset();
                                        selectedMaterialAssetKey.clear();
                                        cachedMaterialAsset.reset();
                                        selectedNativeScriptAssetKey.clear();
                                    }
                                }
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                if (isSceneLoading)
                {
                    drawLoadingOverlay(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
                }
                else if (!Renderer2D::IsShaderReady())
                {
                    drawShaderCompileOverlay(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
                }
                else if (!sceneViewCamera)
                {
                    const ImVec2 minPos = ImGui::GetItemRectMin();
                    const ImVec2 maxPos = ImGui::GetItemRectMax();
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    drawList->AddRectFilled(minPos, maxPos, IM_COL32(0, 0, 0, 180));

                    const char* text = "Scene View: Editor camera is unavailable.";
                    const ImVec2 textSize = ImGui::CalcTextSize(text);
                    const ImVec2 center((minPos.x + maxPos.x) * 0.5f, (minPos.y + maxPos.y) * 0.5f);
                    drawList->AddText(ImVec2(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f), IM_COL32(255, 200, 120, 255), text);
                }

                if (showFpsOverlay)
                {
                    const ImVec2 minPos = ImGui::GetItemRectMin();
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    struct FpsOverlayHistory
                    {
                        std::array<float, 180> FrameTimesMs{};
                        size_t NextIndex = 0;
                        size_t SampleCount = 0;
                    };
                    static FpsOverlayHistory fpsHistory{};

                    const float deltaTimeMs = std::max(0.0f, ImGui::GetIO().DeltaTime * 1000.0f);
                    if (deltaTimeMs > 0.0f)
                    {
                        fpsHistory.FrameTimesMs[fpsHistory.NextIndex] = deltaTimeMs;
                        fpsHistory.NextIndex = (fpsHistory.NextIndex + 1) % fpsHistory.FrameTimesMs.size();
                        fpsHistory.SampleCount = std::min(fpsHistory.SampleCount + 1, fpsHistory.FrameTimesMs.size());
                    }

                    float minFrameMs = 0.0f;
                    float maxFrameMs = 0.0f;
                    float avgFrameMs = 0.0f;
                    if (fpsHistory.SampleCount > 0)
                    {
                        minFrameMs = std::numeric_limits<float>::max();
                        for (size_t sampleIndex = 0; sampleIndex < fpsHistory.SampleCount; ++sampleIndex)
                        {
                            const size_t readIndex =
                                (fpsHistory.NextIndex + fpsHistory.FrameTimesMs.size() - fpsHistory.SampleCount + sampleIndex) %
                                fpsHistory.FrameTimesMs.size();
                            const float sampleMs = fpsHistory.FrameTimesMs[readIndex];
                            minFrameMs = std::min(minFrameMs, sampleMs);
                            maxFrameMs = std::max(maxFrameMs, sampleMs);
                            avgFrameMs += sampleMs;
                        }
                        avgFrameMs /= static_cast<float>(fpsHistory.SampleCount);
                    }

                    const float fps = (deltaTimeMs > 0.01f) ? (1000.0f / deltaTimeMs) : ImGui::GetIO().Framerate;
                    const ImU32 statusColor = (deltaTimeMs <= 16.67f)
                        ? IM_COL32(120, 255, 130, 255)
                        : ((deltaTimeMs <= 33.33f) ? IM_COL32(255, 220, 100, 255) : IM_COL32(255, 120, 120, 255));

                    const ImVec2 panelMin(minPos.x + 10.0f, minPos.y + 10.0f);
                    const ImVec2 panelMax(panelMin.x + 280.0f, panelMin.y + 130.0f);
                    drawList->AddRectFilled(panelMin, panelMax, IM_COL32(0, 0, 0, 165), 4.0f);
                    drawList->AddRect(panelMin, panelMax, IM_COL32(255, 255, 255, 32), 4.0f);

                    char titleBuffer[96]{};
                    std::snprintf(titleBuffer, sizeof(titleBuffer), "FPS %d", static_cast<int>(std::round(fps)));
                    drawList->AddText(ImVec2(panelMin.x + 8.0f, panelMin.y + 6.0f), statusColor, titleBuffer);

                    char frameAvgBuffer[160]{};
                    std::snprintf(frameAvgBuffer,
                                  sizeof(frameAvgBuffer),
                                  "Frame %.2f ms | Avg %.2f ms",
                                  deltaTimeMs,
                                  avgFrameMs);
                    drawList->AddText(ImVec2(panelMin.x + 8.0f, panelMin.y + 24.0f), IM_COL32(215, 230, 255, 255), frameAvgBuffer);

                    char minMaxBuffer[160]{};
                    std::snprintf(minMaxBuffer,
                                  sizeof(minMaxBuffer),
                                  "Min %.2f ms | Max %.2f ms",
                                  minFrameMs,
                                  maxFrameMs);
                    drawList->AddText(ImVec2(panelMin.x + 8.0f, panelMin.y + 40.0f), IM_COL32(215, 230, 255, 255), minMaxBuffer);

                    const ImVec2 graphMin(panelMin.x + 8.0f, panelMin.y + 58.0f);
                    const ImVec2 graphMax(panelMax.x - 8.0f, panelMax.y - 8.0f);
                    drawList->AddRectFilled(graphMin, graphMax, IM_COL32(20, 24, 30, 220), 3.0f);
                    drawList->AddRect(graphMin, graphMax, IM_COL32(255, 255, 255, 20), 3.0f);

                    const float graphHeight = graphMax.y - graphMin.y;
                    const float graphWidth = graphMax.x - graphMin.x;
                    const float graphMaxMs = std::max(50.0f, maxFrameMs * 1.2f);
                    auto msToY = [&](float milliseconds) {
                        const float normalized = std::clamp(milliseconds / graphMaxMs, 0.0f, 1.0f);
                        return graphMax.y - normalized * graphHeight;
                    };

                    const float y60 = msToY(16.67f);
                    const float y30 = msToY(33.33f);
                    drawList->AddLine(ImVec2(graphMin.x, y60), ImVec2(graphMax.x, y60), IM_COL32(110, 255, 120, 70), 1.0f);
                    drawList->AddLine(ImVec2(graphMin.x, y30), ImVec2(graphMax.x, y30), IM_COL32(255, 220, 90, 70), 1.0f);

                    if (fpsHistory.SampleCount >= 2)
                    {
                        const size_t baseIndex = (fpsHistory.NextIndex + fpsHistory.FrameTimesMs.size() - fpsHistory.SampleCount) % fpsHistory.FrameTimesMs.size();
                        for (size_t pointIndex = 1; pointIndex < fpsHistory.SampleCount; ++pointIndex)
                        {
                            const size_t sampleIndexA = (baseIndex + pointIndex - 1) % fpsHistory.FrameTimesMs.size();
                            const size_t sampleIndexB = (baseIndex + pointIndex) % fpsHistory.FrameTimesMs.size();
                            const float sampleA = fpsHistory.FrameTimesMs[sampleIndexA];
                            const float sampleB = fpsHistory.FrameTimesMs[sampleIndexB];

                            const float xA = graphMin.x + (static_cast<float>(pointIndex - 1) / static_cast<float>(fpsHistory.SampleCount - 1)) * graphWidth;
                            const float xB = graphMin.x + (static_cast<float>(pointIndex) / static_cast<float>(fpsHistory.SampleCount - 1)) * graphWidth;
                            const float yA = msToY(sampleA);
                            const float yB = msToY(sampleB);

                            drawList->AddLine(ImVec2(xA, yA), ImVec2(xB, yB), IM_COL32(120, 200, 255, 220), 1.8f);
                        }

                        drawList->AddCircleFilled(ImVec2(graphMax.x, msToY(deltaTimeMs)), 2.5f, statusColor);
                    }
                }
            }
        }

        ImGui::End();
        ImGui::PopStyleVar();

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        if (focusGameViewRequested)
        {
            ImGui::SetNextWindowFocus();
            focusGameViewRequested = false;
        }
        ImGui::Begin("Game View");
        SceneRenderer::SetUiInputViewportRectPixels(0.0f, 0.0f, 0.0f, 0.0f, false);

        gameViewFocused = ImGui::IsWindowFocused();
        gameViewHovered = ImGui::IsWindowHovered();
        const bool skipGameRender = ImGui::IsWindowCollapsed();
        const ImVec2 gameViewSize = ImGui::GetContentRegionAvail();
        const uint32_t gameWidth = sanitizeViewportDimension(gameViewSize.x);
        const uint32_t gameHeight = sanitizeViewportDimension(gameViewSize.y);

        if (!skipGameRender && gameWidth > 0 && gameHeight > 0)
        {
            ensureGameViewFramebuffer(gameWidth, gameHeight);
            gameViewWidthPixels = gameWidth;
            gameViewHeightPixels = gameHeight;

            if (gameViewCamera)
                gameViewCamera->SetViewportSize(gameWidth, gameHeight);

            const bool isSceneLoading = scene && scene->GetLoadState() == Scene::LoadState::Loading;
            if (gameViewCamera && scene && gameViewFramebuffer && !isSceneLoading)
                SceneRenderer::RenderToViewport(*scene, *gameViewCamera, gameViewFramebuffer, gameWidth, gameHeight);

            if (gameViewFramebuffer && gameViewFramebuffer->GetColorAttachment())
            {
                ImGui::Image(
                    (ImTextureID)(void*)(uintptr_t)gameViewFramebuffer->GetColorAttachment()->GetRendererID(),
                    ImVec2(static_cast<float>(gameWidth), static_cast<float>(gameHeight)),
                    ImVec2(0, 1),
                    ImVec2(1, 0));

                const ImVec2 minPos = ImGui::GetItemRectMin();
                const ImVec2 maxPos = ImGui::GetItemRectMax();
                SceneRenderer::SetUiInputViewportRectPixels(
                    minPos.x,
                    minPos.y,
                    maxPos.x - minPos.x,
                    maxPos.y - minPos.y,
                    true);
                if (isSceneLoading)
                {
                    drawLoadingOverlay(minPos, maxPos);
                }
                else if (!Renderer2D::IsShaderReady())
                {
                    drawShaderCompileOverlay(minPos, maxPos);
                }
                else if (showMissingGameplayCameraOverlay)
                {
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    drawList->AddRectFilled(minPos, maxPos, IM_COL32(0, 0, 0, 180));

                    const char* text = "Game View: No active gameplay camera.\nAdd a Camera Component to an entity and set it as Primary.";
                    const ImVec2 textSize = ImGui::CalcTextSize(text);
                    const ImVec2 center((minPos.x + maxPos.x) * 0.5f, (minPos.y + maxPos.y) * 0.5f);
                    drawList->AddText(ImVec2(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f), IM_COL32(255, 200, 120, 255), text);
                }
            }
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }
}
