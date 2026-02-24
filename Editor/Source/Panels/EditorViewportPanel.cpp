#include "EditorViewportPanel.h"

#include "Assets/AssetLoadProgress.h"
#include "Assets/LoadingScreen.h"
#include "Editor/EditorCameraController.h"
#include "Graphics/Camera/Camera.h"
#include "Graphics/Camera/OrthographicCamera2D.h"
#include "Graphics/Camera/PerspectiveCamera3D.h"
#include "Graphics/Framebuffer.h"
#include "Graphics/Renderer2D.h"
#include "Core/Time.h"
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

        // ----- Grid2D undo/redo support ------------------------------------------

        struct Grid2DCellEdit
        {
            int32_t CellX = 0;
            int32_t CellY = 0;
            uint32_t PreviousTile = 0;
            uint32_t NewTile = 0;
        };

        class Grid2DPaintCommand final : public IEditorCommand
        {
        public:
            Grid2DPaintCommand(std::string label,
                               EditorUndoService* undoService,
                               entt::entity layerEntity,
                               std::vector<Grid2DCellEdit> edits)
                : m_Label(std::move(label)),
                  m_UndoService(undoService),
                  m_LayerEntity(layerEntity),
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

                auto& registry = scene->GetRegistry();
                auto* layer = registry.try_get<TilemapLayerComponent>(m_LayerEntity);
                if (!layer)
                    return false;
                layer->EnsureStorage();

                for (const Grid2DCellEdit& edit : m_Edits)
                {
                    if (!IsLayerCellInBounds(*layer, edit.CellX, edit.CellY))
                        continue;
                    const size_t idx = LayerCellToIndex(*layer, edit.CellX, edit.CellY);
                    if (idx >= layer->Tiles.size())
                        continue;
                    layer->Tiles[idx] = applyNewValues ? edit.NewTile : edit.PreviousTile;
                }
                layer->RenderCacheDirty = true;
                return true;
            }

            std::string m_Label;
            EditorUndoService* m_UndoService = nullptr;
            entt::entity m_LayerEntity = entt::null;
            std::vector<Grid2DCellEdit> m_Edits;
        };

        struct Grid2DPaintDragState
        {
            bool Active = false;
            entt::entity LayerEntity = entt::null;
            std::unordered_map<size_t, Grid2DCellEdit> PendingEdits;
        };

        Grid2DPaintDragState& GetGrid2DPaintDragState()
        {
            static Grid2DPaintDragState state;
            return state;
        }

        /// Record a single cell edit for the Grid2D undo system. Writes the new
        /// value immediately and stores the old value for undo.
        void StageGrid2DEdit(TilemapLayerComponent& layer,
                             const glm::ivec2& cell,
                             uint32_t newTileValue,
                             Grid2DPaintDragState& dragState)
        {
            if (!IsLayerCellInBounds(layer, cell.x, cell.y))
                return;
            const size_t idx = LayerCellToIndex(layer, cell.x, cell.y);
            if (idx >= layer.Tiles.size())
                return;

            const uint32_t oldValue = layer.Tiles[idx];
            if (oldValue == newTileValue)
                return;

            // Only record the first change per cell within a single stroke.
            if (dragState.PendingEdits.find(idx) == dragState.PendingEdits.end())
            {
                Grid2DCellEdit edit;
                edit.CellX = cell.x;
                edit.CellY = cell.y;
                edit.PreviousTile = oldValue;
                edit.NewTile = newTileValue;
                dragState.PendingEdits.emplace(idx, edit);
            }
            else
            {
                dragState.PendingEdits[idx].NewTile = newTileValue;
            }

            layer.Tiles[idx] = newTileValue;
            layer.RenderCacheDirty = true;
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

        bool TryComputeViewportRay(const Camera& camera,
                                   const ImVec2& viewportMin,
                                   const ImVec2& viewportMax,
                                   const ImVec2& mouseScreenPosition,
                                   glm::vec3& outRayOrigin,
                                   glm::vec3& outRayDirection)
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
            const glm::vec4 nearWorldH = inverseViewProjection * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
            const glm::vec4 farWorldH = inverseViewProjection * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
            if (std::abs(nearWorldH.w) <= 0.000001f || std::abs(farWorldH.w) <= 0.000001f)
                return false;

            const glm::vec3 nearWorld = glm::vec3(nearWorldH) / nearWorldH.w;
            const glm::vec3 farWorld = glm::vec3(farWorldH) / farWorldH.w;
            const glm::vec3 direction = farWorld - nearWorld;
            const float directionLength = glm::length(direction);
            if (directionLength <= 0.000001f)
                return false;

            outRayOrigin = nearWorld;
            outRayDirection = direction / directionLength;
            return true;
        }

        /// Project a world-space line segment to screen space with near-plane
        /// clipping. When one endpoint is behind the camera, the segment is
        /// clipped at the near plane so the visible portion still draws.
        bool ProjectLineSegmentClipped(const Camera& camera,
                                       const ImVec2& viewportMin,
                                       float viewportWidth,
                                       float viewportHeight,
                                       const glm::vec3& worldA,
                                       const glm::vec3& worldB,
                                       ImVec2& outScreenA,
                                       ImVec2& outScreenB)
        {
            if (viewportWidth <= 0.0f || viewportHeight <= 0.0f)
                return false;

            const glm::mat4& vp = camera.GetViewProjectionMatrix();
            glm::vec4 clipA = vp * glm::vec4(worldA, 1.0f);
            glm::vec4 clipB = vp * glm::vec4(worldB, 1.0f);

            constexpr float kNearEpsilon = 0.001f;
            const bool aInFront = clipA.w > kNearEpsilon;
            const bool bInFront = clipB.w > kNearEpsilon;

            if (!aInFront && !bInFront)
                return false;

            if (!aInFront || !bInFront)
            {
                const float t = (kNearEpsilon - clipA.w) / (clipB.w - clipA.w);
                const glm::vec4 clipped = clipA + std::clamp(t, 0.0f, 1.0f) * (clipB - clipA);
                if (!aInFront)
                    clipA = clipped;
                else
                    clipB = clipped;
            }

            auto clipToScreen = [&](const glm::vec4& clip, ImVec2& out) {
                const glm::vec3 ndc = glm::vec3(clip) / clip.w;
                out.x = viewportMin.x + (ndc.x * 0.5f + 0.5f) * viewportWidth;
                out.y = viewportMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * viewportHeight;
            };

            clipToScreen(clipA, outScreenA);
            clipToScreen(clipB, outScreenB);
            return true;
        }

        bool TryIntersectRayWithPlane(const glm::vec3& rayOrigin,
                                      const glm::vec3& rayDirection,
                                      const glm::vec3& planePoint,
                                      const glm::vec3& planeNormal,
                                      glm::vec3& outIntersectionPoint)
        {
            const float denominator = glm::dot(rayDirection, planeNormal);
            if (std::abs(denominator) <= 0.000001f)
                return false;

            const float distance = glm::dot(planePoint - rayOrigin, planeNormal) / denominator;
            if (distance < 0.0f)
                return false;

            outIntersectionPoint = rayOrigin + rayDirection * distance;
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
            // Prevent perspective back-projection artifacts when points move behind
            // the camera; these can make editor overlays appear to "warp" with
            // camera rotation.
            if (camera.GetType() == CameraType::Perspective3D && clip.w <= 0.0f)
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

        // -----------------------------------------------------------------
        // Grid2D + TilemapLayerComponent editing
        // -----------------------------------------------------------------

        /// Compute the first cell center for a Grid2D/TilemapLayer combination.
        glm::vec2 GetGrid2DFirstCellCenter(const Grid2DComponent& grid, const TilemapLayerComponent& layer)
        {
            const int32_t gridWidth  = std::max(1, layer.GridSize.x);
            const int32_t gridHeight = std::max(1, layer.GridSize.y);
            const glm::vec2 cellSize(std::max(0.001f, grid.CellSize.x), std::max(0.001f, grid.CellSize.y));
            return -0.5f * glm::vec2(gridWidth - 1, gridHeight - 1) * cellSize;
        }

        bool TryGetGrid2DHoveredCell(const Camera& camera,
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
            const glm::vec2 firstCellCenter = GetGrid2DFirstCellCenter(grid, layer);
            const glm::vec2 cellSize(std::max(0.001f, grid.CellSize.x), std::max(0.001f, grid.CellSize.y));
            const glm::vec2 mapMin = firstCellCenter - cellSize * 0.5f;
            const int32_t cellX = static_cast<int32_t>(std::floor((localPosition.x - mapMin.x) / cellSize.x));
            const int32_t cellY = static_cast<int32_t>(std::floor((localPosition.y - mapMin.y) / cellSize.y));
            if (!IsLayerCellInBounds(layer, cellX, cellY))
                return false;
            outCell = glm::ivec2(cellX, cellY);
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

            layer->EnsureStorage();
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
                TryGetGrid2DHoveredCell(camera, worldTransform, *grid, *layer,
                                        viewportMin, viewportMax, mousePosition, hoveredCell);
            if (hasHoveredCell)
            {
                tilemapEditorState.HasHoveredCell = true;
                tilemapEditorState.HoveredCell = hoveredCell;
            }

            const glm::vec2 cellSize(std::max(0.001f, grid->CellSize.x), std::max(0.001f, grid->CellSize.y));
            const glm::vec2 firstCellCenter = GetGrid2DFirstCellCenter(*grid, *layer);
            const glm::vec2 gridBoundaryMin = firstCellCenter - cellSize * 0.5f;
            const glm::vec2 gridBoundaryMax = gridBoundaryMin + glm::vec2(layer->GridSize) * cellSize;

            // Grid overlay -- uses near-plane clipping so lines that are
            // partially behind the camera still render their visible portion.
            if (tilemapEditorState.ShowGridOverlay)
            {
                for (int32_t x = 0; x <= std::max(1, layer->GridSize.x); ++x)
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
                for (int32_t y = 0; y <= std::max(1, layer->GridSize.y); ++y)
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

            // Cell highlight helper -- clips each edge individually so the
            // highlight remains visible when corners go behind the camera.
            auto drawCellHighlight = [&](const glm::ivec2& cell, ImU32 color, float thickness) {
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

            // Resolve the active tile's TileTable entry so rendering can find it.
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

            // Undo-aware painting using Grid2DPaintDragState.
            auto& paintDragState = GetGrid2DPaintDragState();

            auto finalizeGrid2DStroke = [&](const char* label) {
                if (!paintDragState.Active && paintDragState.PendingEdits.empty())
                    return;
                if (!undoService)
                {
                    paintDragState = {};
                    return;
                }

                std::vector<Grid2DCellEdit> edits;
                edits.reserve(paintDragState.PendingEdits.size());
                for (auto& [_, edit] : paintDragState.PendingEdits)
                    edits.push_back(edit);

                if (!edits.empty())
                {
                    auto command = std::make_unique<Grid2DPaintCommand>(
                        label ? std::string(label) : std::string("Paint Grid2D"),
                        undoService, layerEntity, std::move(edits));
                    (void)undoService->ExecuteCommand(std::move(command));
                }
                paintDragState = {};
            };

            // If layer changed mid-stroke, finalize the previous one.
            if (paintDragState.Active && paintDragState.LayerEntity != layerEntity)
                finalizeGrid2DStroke("Paint Grid2D");

            // Begin stroke on mouse press.
            const bool leftMousePressed = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
            if (canCaptureMouse && leftMousePressed && hasHoveredCell)
            {
                paintDragState.Active = true;
                paintDragState.LayerEntity = layerEntity;
                paintDragState.PendingEdits.clear();
            }

            // Continue stroke while mouse is held.
            if (paintDragState.Active && paintDragState.LayerEntity == layerEntity &&
                hasHoveredCell && canCaptureMouse && leftMouseDown)
            {
                if (tilemapEditorState.PaintMode == TilemapPaintMode::Erase)
                {
                    const int32_t brushSize = std::max(1, tilemapEditorState.BrushSize);
                    const int32_t startOffset = (brushSize - 1) / 2;
                    for (int32_t by = 0; by < brushSize; ++by)
                        for (int32_t bx = 0; bx < brushSize; ++bx)
                            StageGrid2DEdit(*layer,
                                hoveredCell - glm::ivec2(startOffset) + glm::ivec2(bx, by),
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
                            const glm::ivec2 cell = hoveredCell + glm::ivec2(sx, sceneYOffset);
                            const size_t stampIdx = static_cast<size_t>(
                                sy * tilemapEditorState.StampSize.x + sx);
                            if (stampIdx >= tilemapEditorState.StampTileAssetKeys.size())
                                continue;
                            const std::string& stampKey = tilemapEditorState.StampTileAssetKeys[stampIdx];
                            const uint32_t resolvedId = stampKey.empty()
                                ? 0u : layer->GetOrAddTileTableEntry(stampKey);
                            StageGrid2DEdit(*layer, cell, resolvedId, paintDragState);
                        }
                    }
                }
                else
                {
                    const int32_t brushSize = std::max(1, tilemapEditorState.BrushSize);
                    const int32_t startOffset = (brushSize - 1) / 2;
                    for (int32_t by = 0; by < brushSize; ++by)
                        for (int32_t bx = 0; bx < brushSize; ++bx)
                            StageGrid2DEdit(*layer,
                                hoveredCell - glm::ivec2(startOffset) + glm::ivec2(bx, by),
                                paintTileValue, paintDragState);
                }
            }

            // Finalize stroke on mouse release.
            if (paintDragState.Active && !leftMouseDown)
            {
                const char* label = (tilemapEditorState.PaintMode == TilemapPaintMode::Erase)
                    ? "Erase Grid2D Tiles" : "Paint Grid2D Tiles";
                finalizeGrid2DStroke(label);
            }

            // Hover highlight.
            if (hasHoveredCell)
            {
                const ImU32 highlightColor = (tilemapEditorState.PaintMode == TilemapPaintMode::Erase)
                    ? IM_COL32(255, 80, 80, 200) : IM_COL32(85, 200, 255, 200);
                drawCellHighlight(hoveredCell, highlightColor, 2.0f);

                // Stamp preview.
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
                            if (IsLayerCellInBounds(*layer, previewCell.x, previewCell.y))
                                drawCellHighlight(previewCell, IM_COL32(85, 200, 255, 100), 1.0f);
                        }
                    }
                }
            }

            return hasHoveredCell;
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
              bool& sceneViewRectValid,
              glm::vec2& sceneViewRectMinPixels,
              glm::vec2& sceneViewRectMaxPixels,
              uint32_t& gameViewWidthPixels,
              uint32_t& gameViewHeightPixels,
              std::shared_ptr<Framebuffer>& gameViewFramebuffer,
              bool& gameViewFocused,
              bool& gameViewHovered,
              bool& gameViewRectValid,
              glm::vec2& gameViewRectMinPixels,
              glm::vec2& gameViewRectMaxPixels,
              bool& focusSceneViewRequested,
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
        sceneViewRectValid = false;
        sceneViewRectMinPixels = glm::vec2(0.0f);
        sceneViewRectMaxPixels = glm::vec2(0.0f);
        gameViewRectValid = false;
        gameViewRectMinPixels = glm::vec2(0.0f);
        gameViewRectMaxPixels = glm::vec2(0.0f);

        auto sanitizeViewportDimension = [](float value) -> uint32_t {
            if (!std::isfinite(value) || value <= 1.0f)
                return 0;
            return static_cast<uint32_t>(std::floor(value));
        };

        auto drawLoadingOverlay = [scene](const ImVec2& minPos, const ImVec2& maxPos) {
            const LoadingScreen::Context ctx = LoadingScreen::BuildContext(
                scene, Renderer2D::IsShaderReady(), Renderer2D::GetDefaultShaderKey());
            const LoadingScreen::State state = LoadingScreen::GetState(ctx);
            if (!state.IsLoading)
                return;

            const ImVec2 center((minPos.x + maxPos.x) * 0.5f, (minPos.y + maxPos.y) * 0.5f);
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(minPos, maxPos, IM_COL32(0, 0, 0, 255));

            const char* loadingText = state.StatusText.empty() ? "Loading..." : state.StatusText.c_str();
            const float progressValue = std::clamp(state.Progress, 0.0f, 1.0f);

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
        if (focusSceneViewRequested)
        {
            ImGui::SetNextWindowFocus();
            focusSceneViewRequested = false;
        }
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
                const ImVec2 sceneRectMin = ImGui::GetItemRectMin();
                const ImVec2 sceneRectMax = ImGui::GetItemRectMax();
                sceneViewRectValid = true;
                sceneViewRectMinPixels = glm::vec2(sceneRectMin.x, sceneRectMin.y);
                sceneViewRectMaxPixels = glm::vec2(sceneRectMax.x, sceneRectMax.y);

                if (scene && sceneViewCamera && !isSceneLoading)
                {
                    const ImVec2 viewportMin = sceneRectMin;
                    const ImVec2 viewportMax = sceneRectMax;
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
                        // Grid2D + TilemapLayer editing.
                        {
                            auto& reg = scene->GetRegistry();
                            entt::entity gridEntity = tilemapEditorState->ActiveGridEntity;
                            entt::entity layerEntity = tilemapEditorState->ActiveLayerEntity;

                            const bool preferredTargetsValid =
                                gridEntity != entt::null &&
                                layerEntity != entt::null &&
                                scene->IsValid(gridEntity) &&
                                scene->IsValid(layerEntity) &&
                                reg.all_of<Grid2DComponent>(gridEntity) &&
                                reg.all_of<TilemapLayerComponent>(layerEntity);

                            if (!preferredTargetsValid && reg.all_of<Grid2DComponent>(selectedEntity))
                            {
                                gridEntity = selectedEntity;
                                for (entt::entity child : scene->GetChildren(selectedEntity))
                                {
                                    if (reg.all_of<TilemapLayerComponent>(child))
                                    { layerEntity = child; break; }
                                }
                            }
                            else if (!preferredTargetsValid && reg.all_of<TilemapLayerComponent>(selectedEntity))
                            {
                                layerEntity = selectedEntity;
                                entt::entity parent = scene->GetParent(selectedEntity);
                                if (parent != entt::null && scene->IsValid(parent) &&
                                    reg.all_of<Grid2DComponent>(parent))
                                    gridEntity = parent;
                            }

                            if (gridEntity != entt::null && layerEntity != entt::null)
                            {
                                (void)DrawAndHandleGrid2DEditing(drawList,
                                    *scene,
                                    *sceneViewCamera,
                                    gridEntity,
                                    layerEntity,
                                    viewportMin,
                                    viewportMax,
                                    static_cast<float>(sceneWidth),
                                    static_cast<float>(sceneHeight),
                                    playModeState,
                                    undoService,
                                    *tilemapEditorState,
                                    std::string{});
                            }
                        }
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
                            std::string key;
                            if (payload->Data && payload->DataSize > 0)
                            {
                                const auto* keyChars = static_cast<const char*>(payload->Data);
                                const int keyLength = std::max(0, payload->DataSize - 1);
                                key.assign(keyChars, keyChars + keyLength);
                            }
                            if (!key.empty() && onPrefabDropped)
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

                if (LoadingScreen::GetState(LoadingScreen::BuildContext(scene, Renderer2D::IsShaderReady(), Renderer2D::GetDefaultShaderKey())).IsLoading)
                {
                    drawLoadingOverlay(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
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
                const ImVec2 gameRectMin = ImGui::GetItemRectMin();
                const ImVec2 gameRectMax = ImGui::GetItemRectMax();
                gameViewRectValid = true;
                gameViewRectMinPixels = glm::vec2(gameRectMin.x, gameRectMin.y);
                gameViewRectMaxPixels = glm::vec2(gameRectMax.x, gameRectMax.y);

                const ImVec2 minPos = gameRectMin;
                const ImVec2 maxPos = gameRectMax;
                SceneRenderer::SetUiInputViewportRectPixels(
                    minPos.x,
                    minPos.y,
                    maxPos.x - minPos.x,
                    maxPos.y - minPos.y,
                    true);
                if (LoadingScreen::GetState(LoadingScreen::BuildContext(scene, Renderer2D::IsShaderReady(), Renderer2D::GetDefaultShaderKey())).IsLoading)
                {
                    drawLoadingOverlay(minPos, maxPos);
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
