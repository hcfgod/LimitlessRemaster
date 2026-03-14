#include "EditorViewportPanelShared.h"

#include "Graphics/Camera/Camera.h"
#include "Scene/Scene.h"
#include "Undo/EditorUndoService.h"

#include <cmath>
#include <vector>

namespace Limitless::EditorViewportPanel::Internal
{
    namespace
    {
        enum class ColliderHandleKind : uint8_t
        {
            None = 0,
            BoxOffset,
            BoxCorner0,
            BoxCorner1,
            BoxCorner2,
            BoxCorner3,
            CircleOffset,
            CircleRadius,
            PolygonOffset,
            PolygonPoint,
            EdgeOffset,
            EdgePointA,
            EdgePointB,
            CapsuleOffset,
            CapsuleCorner
        };

        struct ColliderDragState final
        {
            bool Active = false;
            entt::entity Entity = entt::null;
            ColliderHandleKind Handle = ColliderHandleKind::None;
            const char* CommitLabel = nullptr;
            int PointIndex = -1;
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

            auto projectLocalPoint = [&](const glm::vec2& localPoint, ImVec2& outPoint) -> bool {
                const glm::vec4 worldPoint = worldTransform * glm::vec4(localPoint.x, localPoint.y, 0.0f, 1.0f);
                return WorldToViewportPoint(camera, viewportMin, viewportWidth, viewportHeight, glm::vec3(worldPoint), outPoint);
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

            if (auto* polygonCollider2D = registry.try_get<PolygonCollider2DComponent>(selectedEntity))
            {
                ImVec2 projectedOffset{};
                bool valid = projectLocalPoint(polygonCollider2D->Offset, projectedOffset);
                std::vector<ImVec2> projectedPoints(polygonCollider2D->Points.size());
                for (size_t pointIndex = 0; valid && pointIndex < polygonCollider2D->Points.size(); ++pointIndex)
                    valid = projectLocalPoint(polygonCollider2D->Offset + polygonCollider2D->Points[pointIndex], projectedPoints[pointIndex]);

                if (valid && projectedPoints.size() >= 2)
                {
                    for (size_t pointIndex = 0; pointIndex < projectedPoints.size(); ++pointIndex)
                    {
                        const ImVec2& pointA = projectedPoints[pointIndex];
                        const ImVec2& pointB = projectedPoints[(pointIndex + 1) % projectedPoints.size()];
                        drawList->AddLine(pointA, pointB, IM_COL32(220, 120, 255, 255), 2.0f);
                    }

                    drawList->AddCircleFilled(projectedOffset, handleRadiusPixels, IM_COL32(75, 220, 140, 245));
                    for (const ImVec2& point : projectedPoints)
                        drawList->AddCircleFilled(point, handleRadiusPixels, IM_COL32(220, 120, 255, 245));

                    ColliderHandleKind hoveredHandle = ColliderHandleKind::None;
                    int hoveredPointIndex = -1;
                    if (canEdit && mouseInViewport && !dragState.Active)
                    {
                        if (IsMouseNearPoint(mousePosition, projectedOffset, handleRadiusPixels + 3.0f))
                        {
                            hoveredHandle = ColliderHandleKind::PolygonOffset;
                        }
                        else
                        {
                            for (size_t pointIndex = 0; pointIndex < projectedPoints.size(); ++pointIndex)
                            {
                                if (!IsMouseNearPoint(mousePosition, projectedPoints[pointIndex], handleRadiusPixels + 3.0f))
                                    continue;
                                hoveredHandle = ColliderHandleKind::PolygonPoint;
                                hoveredPointIndex = static_cast<int>(pointIndex);
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
                            dragState.PointIndex = hoveredPointIndex;
                            dragState.CommitLabel = (hoveredHandle == ColliderHandleKind::PolygonOffset)
                                ? "Edit Polygon Collider Offset"
                                : "Edit Polygon Collider Point";
                        }
                    }

                    if (dragState.Active && dragState.Entity == selectedEntity &&
                        (dragState.Handle == ColliderHandleKind::PolygonOffset || dragState.Handle == ColliderHandleKind::PolygonPoint))
                    {
                        glm::vec2 localPoint(0.0f);
                        if (updateLocalPointFromMouse(localPoint))
                        {
                            if (dragState.Handle == ColliderHandleKind::PolygonOffset)
                            {
                                polygonCollider2D->Offset = localPoint;
                            }
                            else if (dragState.PointIndex >= 0 && dragState.PointIndex < static_cast<int>(polygonCollider2D->Points.size()))
                            {
                                polygonCollider2D->Points[static_cast<size_t>(dragState.PointIndex)] = localPoint - polygonCollider2D->Offset;
                            }
                        }

                        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                            commitOrCancelDrag(true);
                    }
                }
            }

            if (auto* edgeCollider2D = registry.try_get<EdgeCollider2DComponent>(selectedEntity))
            {
                ImVec2 projectedOffset{};
                ImVec2 projectedPointA{};
                ImVec2 projectedPointB{};
                const bool valid = projectLocalPoint(edgeCollider2D->Offset, projectedOffset) &&
                                   projectLocalPoint(edgeCollider2D->Offset + edgeCollider2D->PointA, projectedPointA) &&
                                   projectLocalPoint(edgeCollider2D->Offset + edgeCollider2D->PointB, projectedPointB);
                if (valid)
                {
                    drawList->AddLine(projectedPointA, projectedPointB, IM_COL32(255, 110, 110, 255), 2.0f);
                    drawList->AddCircleFilled(projectedOffset, handleRadiusPixels, IM_COL32(75, 220, 140, 245));
                    drawList->AddCircleFilled(projectedPointA, handleRadiusPixels, IM_COL32(255, 110, 110, 245));
                    drawList->AddCircleFilled(projectedPointB, handleRadiusPixels, IM_COL32(255, 110, 110, 245));

                    ColliderHandleKind hoveredHandle = ColliderHandleKind::None;
                    if (canEdit && mouseInViewport && !dragState.Active)
                    {
                        if (IsMouseNearPoint(mousePosition, projectedOffset, handleRadiusPixels + 3.0f))
                            hoveredHandle = ColliderHandleKind::EdgeOffset;
                        else if (IsMouseNearPoint(mousePosition, projectedPointA, handleRadiusPixels + 3.0f))
                            hoveredHandle = ColliderHandleKind::EdgePointA;
                        else if (IsMouseNearPoint(mousePosition, projectedPointB, handleRadiusPixels + 3.0f))
                            hoveredHandle = ColliderHandleKind::EdgePointB;
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
                            dragState.CommitLabel = (hoveredHandle == ColliderHandleKind::EdgeOffset)
                                ? "Edit Edge Collider Offset"
                                : "Edit Edge Collider Point";
                            dragState.PointIndex = -1;
                        }
                    }

                    if (dragState.Active && dragState.Entity == selectedEntity &&
                        (dragState.Handle == ColliderHandleKind::EdgeOffset ||
                         dragState.Handle == ColliderHandleKind::EdgePointA ||
                         dragState.Handle == ColliderHandleKind::EdgePointB))
                    {
                        glm::vec2 localPoint(0.0f);
                        if (updateLocalPointFromMouse(localPoint))
                        {
                            if (dragState.Handle == ColliderHandleKind::EdgeOffset)
                            {
                                edgeCollider2D->Offset = localPoint;
                            }
                            else if (dragState.Handle == ColliderHandleKind::EdgePointA)
                            {
                                edgeCollider2D->PointA = localPoint - edgeCollider2D->Offset;
                            }
                            else
                            {
                                edgeCollider2D->PointB = localPoint - edgeCollider2D->Offset;
                            }
                        }

                        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                            commitOrCancelDrag(true);
                    }
                }
            }

            if (auto* capsuleCollider2D = registry.try_get<CapsuleCollider2DComponent>(selectedEntity))
            {
                const glm::vec2 halfSize = capsuleCollider2D->Size * 0.5f;
                const bool vertical = capsuleCollider2D->Direction == CapsuleCollider2DComponent::Orientation::Vertical;
                const float radius = vertical ? halfSize.x : halfSize.y;
                const float segmentHalf = vertical
                    ? std::max(0.0f, halfSize.y - radius)
                    : std::max(0.0f, halfSize.x - radius);
                const glm::vec2 centerA = capsuleCollider2D->Offset + (vertical ? glm::vec2(0.0f, -segmentHalf) : glm::vec2(-segmentHalf, 0.0f));
                const glm::vec2 centerB = capsuleCollider2D->Offset + (vertical ? glm::vec2(0.0f, segmentHalf) : glm::vec2(segmentHalf, 0.0f));
                const glm::vec2 cornerPoint = capsuleCollider2D->Offset + halfSize;

                ImVec2 projectedOffset{};
                ImVec2 projectedCenterA{};
                ImVec2 projectedCenterB{};
                ImVec2 projectedRadiusPointA{};
                ImVec2 projectedRadiusPointB{};
                ImVec2 projectedCorner{};
                const bool valid = projectLocalPoint(capsuleCollider2D->Offset, projectedOffset) &&
                                   projectLocalPoint(centerA, projectedCenterA) &&
                                   projectLocalPoint(centerB, projectedCenterB) &&
                                   projectLocalPoint(centerA + (vertical ? glm::vec2(radius, 0.0f) : glm::vec2(0.0f, radius)), projectedRadiusPointA) &&
                                   projectLocalPoint(centerB + (vertical ? glm::vec2(radius, 0.0f) : glm::vec2(0.0f, radius)), projectedRadiusPointB) &&
                                   projectLocalPoint(cornerPoint, projectedCorner);
                if (valid)
                {
                    const float radiusPixelsA = std::sqrt((projectedRadiusPointA.x - projectedCenterA.x) * (projectedRadiusPointA.x - projectedCenterA.x) +
                                                          (projectedRadiusPointA.y - projectedCenterA.y) * (projectedRadiusPointA.y - projectedCenterA.y));
                    const float radiusPixelsB = std::sqrt((projectedRadiusPointB.x - projectedCenterB.x) * (projectedRadiusPointB.x - projectedCenterB.x) +
                                                          (projectedRadiusPointB.y - projectedCenterB.y) * (projectedRadiusPointB.y - projectedCenterB.y));
                    drawList->AddCircle(projectedCenterA, radiusPixelsA, IM_COL32(120, 255, 180, 255), 32, 2.0f);
                    drawList->AddCircle(projectedCenterB, radiusPixelsB, IM_COL32(120, 255, 180, 255), 32, 2.0f);

                    ImVec2 sideA0{};
                    ImVec2 sideA1{};
                    ImVec2 sideB0{};
                    ImVec2 sideB1{};
                    const glm::vec2 sideOffset0 = vertical ? glm::vec2(radius, 0.0f) : glm::vec2(0.0f, radius);
                    const glm::vec2 sideOffset1 = vertical ? glm::vec2(-radius, 0.0f) : glm::vec2(0.0f, -radius);
                    if (projectLocalPoint(centerA + sideOffset0, sideA0) &&
                        projectLocalPoint(centerB + sideOffset0, sideA1) &&
                        projectLocalPoint(centerA + sideOffset1, sideB0) &&
                        projectLocalPoint(centerB + sideOffset1, sideB1))
                    {
                        drawList->AddLine(sideA0, sideA1, IM_COL32(120, 255, 180, 255), 2.0f);
                        drawList->AddLine(sideB0, sideB1, IM_COL32(120, 255, 180, 255), 2.0f);
                    }

                    drawList->AddCircleFilled(projectedOffset, handleRadiusPixels, IM_COL32(75, 220, 140, 245));
                    drawList->AddCircleFilled(projectedCorner, handleRadiusPixels, IM_COL32(120, 255, 180, 245));

                    ColliderHandleKind hoveredHandle = ColliderHandleKind::None;
                    if (canEdit && mouseInViewport && !dragState.Active)
                    {
                        if (IsMouseNearPoint(mousePosition, projectedOffset, handleRadiusPixels + 3.0f))
                            hoveredHandle = ColliderHandleKind::CapsuleOffset;
                        else if (IsMouseNearPoint(mousePosition, projectedCorner, handleRadiusPixels + 3.0f))
                            hoveredHandle = ColliderHandleKind::CapsuleCorner;
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
                            dragState.CommitLabel = (hoveredHandle == ColliderHandleKind::CapsuleOffset)
                                ? "Edit Capsule Collider Offset"
                                : "Edit Capsule Collider Size";
                            dragState.PointIndex = -1;
                        }
                    }

                    if (dragState.Active && dragState.Entity == selectedEntity &&
                        (dragState.Handle == ColliderHandleKind::CapsuleOffset || dragState.Handle == ColliderHandleKind::CapsuleCorner))
                    {
                        glm::vec2 localPoint(0.0f);
                        if (updateLocalPointFromMouse(localPoint))
                        {
                            if (dragState.Handle == ColliderHandleKind::CapsuleOffset)
                            {
                                capsuleCollider2D->Offset = localPoint;
                            }
                            else
                            {
                                const glm::vec2 delta = localPoint - capsuleCollider2D->Offset;
                                capsuleCollider2D->Size = glm::max(glm::abs(delta) * 2.0f, glm::vec2(0.02f));
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
                                {
                                    mutableTransform->Rotation.z = glm::degrees(std::atan2(direction.y, direction.x));
                                    scene.MarkTransformDirty(selectedEntity);
                                }
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
    }

    bool DrawSelectedPhysicsOverlays(ImDrawList* drawList,
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
        const bool colliderCapturedInput = DrawAndHandleColliderGizmos(
            drawList, scene, camera, selectedEntity, viewportMin, viewportMax, viewportWidth, viewportHeight, playModeState, undoService);
        const bool lightingCapturedInput = !colliderCapturedInput && DrawAndHandleLightingGizmos(
            drawList, scene, camera, selectedEntity, viewportMin, viewportMax, viewportWidth, viewportHeight, playModeState, undoService);
        return colliderCapturedInput || lightingCapturedInput;
    }
}
