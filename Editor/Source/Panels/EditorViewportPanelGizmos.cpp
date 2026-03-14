#include "EditorViewportPanelShared.h"

#include "Graphics/Camera/Camera.h"
#include "Scene/Scene.h"
#include "Undo/EditorUndoService.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace Limitless::EditorViewportPanel::Internal
{
    namespace
    {
        constexpr float kGizmoAxisLength = 80.0f;
        constexpr float kGizmoArrowSize = 10.0f;
        constexpr float kGizmoHandleRadius = 7.0f;
        constexpr float kGizmoHitRadius = 12.0f;
        constexpr float kGizmoPlaneHandleSize = 22.0f;
        constexpr float kGizmoRotateRadius = 60.0f;
        constexpr float kGizmoScaleBoxSize = 6.0f;
        constexpr ImU32 kGizmoColorX = IM_COL32(230, 60, 60, 255);
        constexpr ImU32 kGizmoColorY = IM_COL32(80, 200, 60, 255);
        constexpr ImU32 kGizmoColorZ = IM_COL32(60, 120, 230, 255);
        constexpr ImU32 kGizmoColorXY = IM_COL32(255, 220, 60, 180);
        constexpr ImU32 kGizmoColorActive = IM_COL32(255, 220, 60, 255);
        constexpr ImU32 kGizmoColorRotateRing = IM_COL32(120, 180, 255, 200);

        ImU32 AxisColor(int axis, int activeAxis)
        {
            if (axis == activeAxis)
                return kGizmoColorActive;
            switch (axis)
            {
                case 0: return kGizmoColorX;
                case 1: return kGizmoColorY;
                case 2: return kGizmoColorZ;
                default: return IM_COL32(200, 200, 200, 255);
            }
        }

        glm::vec3 GetGizmoAxisDirection(int axis)
        {
            switch (axis)
            {
                case 0: return glm::vec3(1.0f, 0.0f, 0.0f);
                case 1: return glm::vec3(0.0f, 1.0f, 0.0f);
                default: return glm::vec3(0.0f, 0.0f, 1.0f);
            }
        }

        glm::vec3 GetCameraForwardDirection(const Camera& camera)
        {
            const glm::mat4 inverseView = glm::inverse(camera.GetViewMatrix());
            const glm::vec3 forward = glm::vec3(inverseView * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f));
            const float length = glm::length(forward);
            if (length <= 0.000001f)
                return glm::vec3(0.0f, 0.0f, -1.0f);
            return forward / length;
        }

        glm::vec3 ComputeAxisDragPlaneNormal(const Camera& camera, const glm::vec3& axisDirection, const glm::vec3& gizmoOrigin)
        {
            glm::vec3 viewDirection = -GetCameraForwardDirection(camera);
            if (camera.GetType() == CameraType::Perspective3D)
            {
                const glm::mat4 inverseView = glm::inverse(camera.GetViewMatrix());
                const glm::vec3 cameraPosition = glm::vec3(inverseView[3]);
                const glm::vec3 toCamera = cameraPosition - gizmoOrigin;
                const float toCameraLength = glm::length(toCamera);
                if (toCameraLength > 0.000001f)
                    viewDirection = toCamera / toCameraLength;
            }

            glm::vec3 planeNormal = viewDirection - axisDirection * glm::dot(viewDirection, axisDirection);
            const float planeNormalLength = glm::length(planeNormal);
            if (planeNormalLength > 0.000001f)
                return planeNormal / planeNormalLength;

            const glm::vec3 fallbackDirection = std::abs(axisDirection.z) < 0.999f
                ? glm::vec3(0.0f, 0.0f, 1.0f)
                : glm::vec3(0.0f, 1.0f, 0.0f);
            planeNormal = fallbackDirection - axisDirection * glm::dot(fallbackDirection, axisDirection);
            const float fallbackLength = glm::length(planeNormal);
            if (fallbackLength <= 0.000001f)
                return glm::vec3(0.0f, 0.0f, 1.0f);
            return planeNormal / fallbackLength;
        }

        bool TryIntersectMouseWithPlane(const Camera& camera,
                                        const ImVec2& viewportMin,
                                        const ImVec2& viewportMax,
                                        const ImVec2& mouseScreenPosition,
                                        const glm::vec3& planePoint,
                                        const glm::vec3& planeNormal,
                                        glm::vec3& outIntersectionPoint)
        {
            glm::vec3 rayOrigin(0.0f);
            glm::vec3 rayDirection(0.0f);
            if (!TryComputeViewportRay(camera, viewportMin, viewportMax, mouseScreenPosition, rayOrigin, rayDirection))
                return false;

            const float planeNormalLength = glm::length(planeNormal);
            if (planeNormalLength <= 0.000001f)
                return false;

            return TryIntersectRayWithPlane(rayOrigin,
                                            rayDirection,
                                            planePoint,
                                            planeNormal / planeNormalLength,
                                            outIntersectionPoint);
        }
    }

    bool DrawAndHandleTransformGizmos(ImDrawList* drawList,
                                      Scene& scene,
                                      const Camera& camera,
                                      entt::entity selectedEntity,
                                      const std::vector<entt::entity>& multiSelectedEntities,
                                      const ImVec2& viewportMin,
                                      const ImVec2& viewportMax,
                                      float viewportWidth,
                                      float viewportHeight,
                                      EditorPlayModeState playModeState,
                                      EditorUndoService* undoService,
                                      TransformGizmoState& gizmoState)
    {
        if (!drawList || gizmoState.Mode == TransformGizmoMode::None)
            return false;
        if (selectedEntity == entt::null || !scene.IsValid(selectedEntity))
        {
            if (gizmoState.DragActive)
            {
                if (undoService)
                    undoService->CancelInteractiveSceneMutation();
                gizmoState.DragActive = false;
            }
            return false;
        }

        auto& registry = scene.GetRegistry();
        auto* transform = registry.try_get<TransformComponent>(selectedEntity);
        if (!transform)
            return false;

        const bool canEdit = playModeState == EditorPlayModeState::Edit;
        const ImVec2 mousePos = ImGui::GetMousePos();
        const bool mouseInViewport = mousePos.x >= viewportMin.x && mousePos.x <= viewportMax.x &&
                                     mousePos.y >= viewportMin.y && mousePos.y <= viewportMax.y;

        const glm::mat4 worldTransform = scene.GetWorldTransformMatrix(selectedEntity);
        const glm::vec3 entityWorldPos = glm::vec3(worldTransform[3]);
        ImVec2 originScreen{};
        if (!WorldToViewportPoint(camera, viewportMin, viewportWidth, viewportHeight, entityWorldPos, originScreen))
            return false;

        float pixelsPerUnit = 1.0f;
        {
            const glm::vec3 offsetPoint = entityWorldPos + glm::vec3(1.0f, 0.0f, 0.0f);
            ImVec2 offsetScreen{};
            if (WorldToViewportPoint(camera, viewportMin, viewportWidth, viewportHeight, offsetPoint, offsetScreen))
            {
                const float dx = offsetScreen.x - originScreen.x;
                const float dy = offsetScreen.y - originScreen.y;
                pixelsPerUnit = std::sqrt(dx * dx + dy * dy);
            }
        }
        if (pixelsPerUnit < 0.001f)
            return false;

        const float worldAxisLength = kGizmoAxisLength / pixelsPerUnit;
        std::array<ImVec2, 3> axisEnds{};
        std::array<bool, 3> axisVisible{};
        std::array<float, 3> axisScreenLengths{};
        for (int axis = 0; axis < 3; ++axis)
        {
            const glm::vec3 worldEnd = entityWorldPos + GetGizmoAxisDirection(axis) * worldAxisLength;
            ImVec2 clippedStart{};
            ImVec2 clippedEnd{};
            axisVisible[axis] = ProjectLineSegmentClipped(camera,
                                                          viewportMin,
                                                          viewportWidth,
                                                          viewportHeight,
                                                          entityWorldPos,
                                                          worldEnd,
                                                          clippedStart,
                                                          clippedEnd);
            axisEnds[axis] = axisVisible[axis] ? clippedEnd : originScreen;
            const float dx = axisEnds[axis].x - originScreen.x;
            const float dy = axisEnds[axis].y - originScreen.y;
            axisScreenLengths[axis] = std::sqrt(dx * dx + dy * dy);
        }

        if (gizmoState.DragActive && gizmoState.DragEntity != selectedEntity)
        {
            if (undoService)
                undoService->CancelInteractiveSceneMutation();
            gizmoState.DragActive = false;
        }

        const int activeAxis = gizmoState.DragActive ? gizmoState.DragAxis : -1;

        if (gizmoState.Mode == TransformGizmoMode::Translate)
        {
            auto drawArrowHead = [&](const ImVec2& endPoint, int axis) {
                const float dx = endPoint.x - originScreen.x;
                const float dy = endPoint.y - originScreen.y;
                const float length = std::sqrt(dx * dx + dy * dy);
                if (length <= 1.0f)
                    return;

                const float nx = dx / length;
                const float ny = dy / length;
                const ImVec2 base1(endPoint.x - nx * kGizmoArrowSize - ny * kGizmoArrowSize * 0.4f,
                                   endPoint.y - ny * kGizmoArrowSize + nx * kGizmoArrowSize * 0.4f);
                const ImVec2 base2(endPoint.x - nx * kGizmoArrowSize + ny * kGizmoArrowSize * 0.4f,
                                   endPoint.y - ny * kGizmoArrowSize - nx * kGizmoArrowSize * 0.4f);
                drawList->AddTriangleFilled(endPoint, base1, base2, AxisColor(axis, activeAxis));
            };

            for (int axis = 0; axis < 3; ++axis)
            {
                if (!axisVisible[axis] || axisScreenLengths[axis] <= 1.0f)
                    continue;

                drawList->AddLine(originScreen, axisEnds[axis], AxisColor(axis, activeAxis), 2.5f);
                drawArrowHead(axisEnds[axis], axis);
            }

            const float planeOffset = kGizmoPlaneHandleSize;
            const ImVec2 planeCorner(originScreen.x + planeOffset, originScreen.y - planeOffset);
            drawList->AddRectFilled(
                ImVec2(originScreen.x + planeOffset * 0.3f, originScreen.y - planeOffset * 0.3f),
                planeCorner,
                activeAxis == 3 ? kGizmoColorActive : kGizmoColorXY);

            int hoveredAxis = -1;
            if (canEdit && mouseInViewport && !gizmoState.DragActive)
            {
                const ImVec2 planeMin(originScreen.x + planeOffset * 0.3f, originScreen.y - planeOffset);
                const ImVec2 planeMax(originScreen.x + planeOffset, originScreen.y - planeOffset * 0.3f);
                if (mousePos.x >= planeMin.x && mousePos.x <= planeMax.x &&
                    mousePos.y >= planeMin.y && mousePos.y <= planeMax.y)
                {
                    hoveredAxis = 3;
                }

                if (hoveredAxis < 0)
                {
                    float bestDistance = kGizmoHitRadius + 1.0f;
                    for (int axis = 0; axis < 3; ++axis)
                    {
                        if (!axisVisible[axis] || axisScreenLengths[axis] <= 8.0f)
                            continue;

                        const float distance = DistanceToLineSegment(mousePos, originScreen, axisEnds[axis]);
                        if (distance <= kGizmoHitRadius && distance < bestDistance)
                        {
                            bestDistance = distance;
                            hoveredAxis = axis;
                        }
                    }
                }
            }

            if (hoveredAxis >= 0)
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

            if (hoveredAxis >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                if (undoService)
                    undoService->BeginInteractiveSceneMutation();
                gizmoState.DragActive = true;
                gizmoState.DragAxis = hoveredAxis;
                gizmoState.DragEntity = selectedEntity;
                gizmoState.DragStartEntityPosition = transform->Position;
                gizmoState.DragStartGizmoOrigin = entityWorldPos;
                gizmoState.DragStartMousePosition = glm::vec2(mousePos.x, mousePos.y);
                gizmoState.DragReferenceValue = worldAxisLength;

                if (hoveredAxis == 3)
                {
                    gizmoState.DragPlaneNormal = glm::vec3(0.0f, 0.0f, 1.0f);
                }
                else
                {
                    gizmoState.DragPlaneNormal = ComputeAxisDragPlaneNormal(camera,
                                                                           GetGizmoAxisDirection(hoveredAxis),
                                                                           entityWorldPos);
                }

                glm::vec3 mouseWorld{};
                if (!TryIntersectMouseWithPlane(camera,
                                                viewportMin,
                                                viewportMax,
                                                mousePos,
                                                gizmoState.DragStartGizmoOrigin,
                                                gizmoState.DragPlaneNormal,
                                                mouseWorld))
                {
                    mouseWorld = gizmoState.DragStartGizmoOrigin;
                }
                gizmoState.DragStartWorldPosition = mouseWorld;

                gizmoState.DragEntities.clear();
                gizmoState.DragStartPositions.clear();
                for (entt::entity e : multiSelectedEntities)
                {
                    if (e != selectedEntity && scene.IsValid(e))
                    {
                        auto* t = registry.try_get<TransformComponent>(e);
                        if (t)
                        {
                            gizmoState.DragEntities.push_back(e);
                            gizmoState.DragStartPositions.push_back(t->Position);
                        }
                    }
                }
            }

            if (gizmoState.DragActive && gizmoState.DragEntity == selectedEntity &&
                gizmoState.Mode == TransformGizmoMode::Translate)
            {
                glm::vec3 currentMouseWorld{};
                if (TryIntersectMouseWithPlane(camera,
                                               viewportMin,
                                               viewportMax,
                                               mousePos,
                                               gizmoState.DragStartGizmoOrigin,
                                               gizmoState.DragPlaneNormal,
                                               currentMouseWorld))
                {
                    glm::vec3 delta = currentMouseWorld - gizmoState.DragStartWorldPosition;
                    if (gizmoState.DragAxis >= 0 && gizmoState.DragAxis < 3)
                    {
                        const glm::vec3 axisDirection = GetGizmoAxisDirection(gizmoState.DragAxis);
                        delta = axisDirection * glm::dot(delta, axisDirection);
                    }

                    transform->Position = gizmoState.DragStartEntityPosition + delta;
                    scene.MarkTransformDirty(selectedEntity);

                    for (size_t i = 0; i < gizmoState.DragEntities.size(); ++i)
                    {
                        entt::entity e = gizmoState.DragEntities[i];
                        if (!scene.IsValid(e))
                            continue;
                        auto* t = registry.try_get<TransformComponent>(e);
                        if (t)
                        {
                            t->Position = gizmoState.DragStartPositions[i] + delta;
                            scene.MarkTransformDirty(e);
                        }
                    }
                }

                if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                {
                    if (undoService)
                        (void)undoService->CommitInteractiveSceneMutation("Move Entity");
                    gizmoState.DragActive = false;
                }
            }
        }
        else if (gizmoState.Mode == TransformGizmoMode::Rotate)
        {
            constexpr int kRingSegments = 64;
            const float worldRadius = kGizmoRotateRadius / pixelsPerUnit;

            auto projectAndDrawRing = [&](int axis, ImU32 color, float thickness,
                                          std::array<ImVec2, kRingSegments>& outScreenPoints,
                                          int& outValidCount)
            {
                outValidCount = 0;
                ImVec2 prevScreen{};
                bool prevValid = false;
                for (int seg = 0; seg <= kRingSegments; ++seg)
                {
                    const float angle = (static_cast<float>(seg % kRingSegments) / static_cast<float>(kRingSegments)) * 6.2831853f;
                    const float ca = std::cos(angle);
                    const float sa = std::sin(angle);
                    glm::vec3 worldPt = entityWorldPos;
                    if (axis == 0) { worldPt.y += ca * worldRadius; worldPt.z += sa * worldRadius; }
                    else if (axis == 1) { worldPt.x += ca * worldRadius; worldPt.z += sa * worldRadius; }
                    else { worldPt.x += ca * worldRadius; worldPt.y += sa * worldRadius; }

                    ImVec2 screenPt{};
                    const bool valid = WorldToViewportPoint(camera, viewportMin, viewportWidth, viewportHeight, worldPt, screenPt);
                    if (valid && seg < kRingSegments)
                    {
                        outScreenPoints[outValidCount] = screenPt;
                        outValidCount++;
                    }
                    if (valid && prevValid && seg > 0)
                        drawList->AddLine(prevScreen, screenPt, color, thickness);
                    prevScreen = screenPt;
                    prevValid = valid;
                }
            };

            auto ringDistanceToMouse = [&](const std::array<ImVec2, kRingSegments>& pts, int count) -> float {
                float minDist = std::numeric_limits<float>::max();
                for (int i = 0; i < count; ++i)
                {
                    const int j = (i + 1) % count;
                    const ImVec2& a = pts[i];
                    const ImVec2& b = pts[j];
                    const float abx = b.x - a.x;
                    const float aby = b.y - a.y;
                    const float lenSq = abx * abx + aby * aby;
                    float t = 0.0f;
                    if (lenSq > 0.001f)
                        t = std::clamp(((mousePos.x - a.x) * abx + (mousePos.y - a.y) * aby) / lenSq, 0.0f, 1.0f);
                    const float cx = a.x + t * abx;
                    const float cy = a.y + t * aby;
                    const float dist = std::sqrt((mousePos.x - cx) * (mousePos.x - cx) + (mousePos.y - cy) * (mousePos.y - cy));
                    minDist = std::min(minDist, dist);
                }
                return minDist;
            };

            constexpr ImU32 kRingColors[3] = { kGizmoColorX, kGizmoColorY, kGizmoColorZ };
            const char* kAxisLabels[3] = { "X", "Y", "Z" };
            std::array<ImVec2, kRingSegments> ringPts[3]{};
            int ringCounts[3] = {};

            for (int axis = 0; axis < 3; ++axis)
            {
                const ImU32 color = (activeAxis == axis) ? kGizmoColorActive : kRingColors[axis];
                const float thickness = (activeAxis == axis) ? 3.0f : 2.0f;
                projectAndDrawRing(axis, color, thickness, ringPts[axis], ringCounts[axis]);
            }

            for (int axis = 0; axis < 3; ++axis)
            {
                const float angleRad = glm::radians(transform->Rotation[axis]);
                const float ca = std::cos(angleRad);
                const float sa = std::sin(angleRad);
                glm::vec3 indicatorWorld = entityWorldPos;
                if (axis == 0) { indicatorWorld.y += ca * worldRadius; indicatorWorld.z += sa * worldRadius; }
                else if (axis == 1) { indicatorWorld.x += ca * worldRadius; indicatorWorld.z += sa * worldRadius; }
                else { indicatorWorld.x += ca * worldRadius; indicatorWorld.y += sa * worldRadius; }

                ImVec2 indicatorScreen{};
                if (WorldToViewportPoint(camera, viewportMin, viewportWidth, viewportHeight, indicatorWorld, indicatorScreen))
                {
                    drawList->AddLine(originScreen, indicatorScreen,
                        (activeAxis == axis) ? kGizmoColorActive : kRingColors[axis], 1.2f);
                    drawList->AddCircleFilled(indicatorScreen, 3.0f, kRingColors[axis]);
                }
            }

            drawList->AddCircleFilled(originScreen, 3.0f, IM_COL32(200, 200, 200, 200));

            int hoveredAxis = -1;
            if (canEdit && mouseInViewport && !gizmoState.DragActive)
            {
                float bestDist = kGizmoHitRadius + 1.0f;
                for (int axis = 0; axis < 3; ++axis)
                {
                    if (ringCounts[axis] < 3)
                        continue;
                    const float dist = ringDistanceToMouse(ringPts[axis], ringCounts[axis]);
                    if (dist <= kGizmoHitRadius && dist < bestDist)
                    {
                        bestDist = dist;
                        hoveredAxis = axis;
                    }
                }
            }

            if (hoveredAxis >= 0)
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

            if (hoveredAxis >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                if (undoService)
                    undoService->BeginInteractiveSceneMutation();
                gizmoState.DragActive = true;
                gizmoState.DragAxis = hoveredAxis;
                gizmoState.DragEntity = selectedEntity;
                gizmoState.DragStartEntityRotation = transform->Rotation;
                const float dx = mousePos.x - originScreen.x;
                const float dy = -(mousePos.y - originScreen.y);
                gizmoState.DragStartAngle = std::atan2(dy, dx);

                gizmoState.DragEntities.clear();
                gizmoState.DragStartRotations.clear();
                for (entt::entity e : multiSelectedEntities)
                {
                    if (e != selectedEntity && scene.IsValid(e))
                    {
                        auto* t = registry.try_get<TransformComponent>(e);
                        if (t)
                        {
                            gizmoState.DragEntities.push_back(e);
                            gizmoState.DragStartRotations.push_back(t->Rotation);
                        }
                    }
                }
            }

            if (gizmoState.DragActive && gizmoState.DragEntity == selectedEntity &&
                gizmoState.Mode == TransformGizmoMode::Rotate)
            {
                const float dx = mousePos.x - originScreen.x;
                const float dy = -(mousePos.y - originScreen.y);
                const float currentAngle = std::atan2(dy, dx);
                const float angleDelta = glm::degrees(currentAngle - gizmoState.DragStartAngle);

                const int dragAxis = gizmoState.DragAxis;
                transform->Rotation[dragAxis] = gizmoState.DragStartEntityRotation[dragAxis] + angleDelta;
                scene.MarkTransformDirty(selectedEntity);

                for (size_t i = 0; i < gizmoState.DragEntities.size(); ++i)
                {
                    entt::entity e = gizmoState.DragEntities[i];
                    if (!scene.IsValid(e))
                        continue;
                    auto* t = registry.try_get<TransformComponent>(e);
                    if (t)
                    {
                        t->Rotation[dragAxis] = gizmoState.DragStartRotations[i][dragAxis] + angleDelta;
                        scene.MarkTransformDirty(e);
                    }
                }

                char angleBuf[48]{};
                std::snprintf(angleBuf, sizeof(angleBuf), "%s: %.1f deg", kAxisLabels[dragAxis], angleDelta);
                drawList->AddText(ImVec2(originScreen.x + kGizmoRotateRadius + 12.0f, originScreen.y - 10.0f),
                                 kRingColors[dragAxis], angleBuf);

                if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                {
                    if (undoService)
                        (void)undoService->CommitInteractiveSceneMutation("Rotate Entity");
                    gizmoState.DragActive = false;
                }
            }
        }
        else if (gizmoState.Mode == TransformGizmoMode::Scale)
        {
            for (int axis = 0; axis < 3; ++axis)
            {
                if (!axisVisible[axis] || axisScreenLengths[axis] <= 1.0f)
                    continue;

                drawList->AddLine(originScreen, axisEnds[axis], AxisColor(axis, activeAxis), 2.5f);
            }

            for (int axis = 0; axis < 3; ++axis)
            {
                if (!axisVisible[axis] || axisScreenLengths[axis] <= 8.0f)
                    continue;

                drawList->AddRectFilled(
                    ImVec2(axisEnds[axis].x - kGizmoScaleBoxSize, axisEnds[axis].y - kGizmoScaleBoxSize),
                    ImVec2(axisEnds[axis].x + kGizmoScaleBoxSize, axisEnds[axis].y + kGizmoScaleBoxSize),
                    AxisColor(axis, activeAxis));
            }

            drawList->AddRectFilled(
                ImVec2(originScreen.x - kGizmoScaleBoxSize, originScreen.y - kGizmoScaleBoxSize),
                ImVec2(originScreen.x + kGizmoScaleBoxSize, originScreen.y + kGizmoScaleBoxSize),
                activeAxis == 3 ? kGizmoColorActive : IM_COL32(200, 200, 200, 220));

            int hoveredAxis = -1;
            if (canEdit && mouseInViewport && !gizmoState.DragActive)
            {
                if (IsMouseNearPoint(mousePos, originScreen, kGizmoScaleBoxSize + 4.0f))
                    hoveredAxis = 3;

                if (hoveredAxis < 0)
                {
                    for (int axis = 0; axis < 3; ++axis)
                    {
                        if (!axisVisible[axis] || axisScreenLengths[axis] <= 8.0f)
                            continue;
                        if (!IsMouseNearPoint(mousePos, axisEnds[axis], kGizmoScaleBoxSize + 4.0f))
                            continue;

                        hoveredAxis = axis;
                        break;
                    }
                }
            }

            if (hoveredAxis >= 0)
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

            if (hoveredAxis >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                if (undoService)
                    undoService->BeginInteractiveSceneMutation();
                gizmoState.DragActive = true;
                gizmoState.DragAxis = hoveredAxis;
                gizmoState.DragEntity = selectedEntity;
                gizmoState.DragStartEntityScale = transform->Scale;
                gizmoState.DragStartGizmoOrigin = entityWorldPos;
                gizmoState.DragStartMousePosition = glm::vec2(mousePos.x, mousePos.y);

                if (hoveredAxis == 3)
                {
                    gizmoState.DragPlaneNormal = GetCameraForwardDirection(camera);
                    gizmoState.DragStartWorldPosition = gizmoState.DragStartGizmoOrigin;
                    const float dx = mousePos.x - originScreen.x;
                    const float dy = mousePos.y - originScreen.y;
                    gizmoState.DragReferenceValue = std::sqrt(dx * dx + dy * dy);
                }
                else
                {
                    const glm::vec3 axisDirection = GetGizmoAxisDirection(hoveredAxis);
                    gizmoState.DragPlaneNormal = ComputeAxisDragPlaneNormal(camera, axisDirection, entityWorldPos);
                    gizmoState.DragStartWorldPosition = gizmoState.DragStartGizmoOrigin + axisDirection * worldAxisLength;
                    gizmoState.DragReferenceValue = worldAxisLength;
                }

                gizmoState.DragEntities.clear();
                gizmoState.DragStartScales.clear();
                for (entt::entity e : multiSelectedEntities)
                {
                    if (e != selectedEntity && scene.IsValid(e))
                    {
                        auto* t = registry.try_get<TransformComponent>(e);
                        if (t)
                        {
                            gizmoState.DragEntities.push_back(e);
                            gizmoState.DragStartScales.push_back(t->Scale);
                        }
                    }
                }
            }

            if (gizmoState.DragActive && gizmoState.DragEntity == selectedEntity &&
                gizmoState.Mode == TransformGizmoMode::Scale)
            {
                glm::vec3 scaleFactor(1.0f);
                if (gizmoState.DragAxis == 3)
                {
                    const float startDistance = gizmoState.DragReferenceValue;
                    float uniformFactor = 1.0f;
                    if (startDistance > 8.0f)
                    {
                        const float dx = mousePos.x - originScreen.x;
                        const float dy = mousePos.y - originScreen.y;
                        const float currentDistance = std::sqrt(dx * dx + dy * dy);
                        uniformFactor = currentDistance / startDistance;
                    }
                    else
                    {
                        const float deltaPixels = (mousePos.x - gizmoState.DragStartMousePosition.x) -
                                                  (mousePos.y - gizmoState.DragStartMousePosition.y);
                        uniformFactor = 1.0f + deltaPixels * 0.01f;
                    }
                    scaleFactor = glm::vec3(std::max(uniformFactor, 0.001f));
                }
                else
                {
                    glm::vec3 currentMouseWorld{};
                    if (TryIntersectMouseWithPlane(camera,
                                                   viewportMin,
                                                   viewportMax,
                                                   mousePos,
                                                   gizmoState.DragStartGizmoOrigin,
                                                   gizmoState.DragPlaneNormal,
                                                   currentMouseWorld))
                    {
                        const glm::vec3 axisDirection = GetGizmoAxisDirection(gizmoState.DragAxis);
                        const float referenceDistance = std::abs(gizmoState.DragReferenceValue) > 0.001f
                            ? gizmoState.DragReferenceValue
                            : 1.0f;
                        const float currentAxisDistance = glm::dot(currentMouseWorld - gizmoState.DragStartGizmoOrigin,
                                                                   axisDirection);
                        scaleFactor[gizmoState.DragAxis] = currentAxisDistance / referenceDistance;
                    }
                }

                transform->Scale = gizmoState.DragStartEntityScale * scaleFactor;
                scene.MarkTransformDirty(selectedEntity);

                for (size_t i = 0; i < gizmoState.DragEntities.size(); ++i)
                {
                    entt::entity e = gizmoState.DragEntities[i];
                    if (!scene.IsValid(e))
                        continue;
                    auto* t = registry.try_get<TransformComponent>(e);
                    if (t)
                    {
                        t->Scale = gizmoState.DragStartScales[i] * scaleFactor;
                        scene.MarkTransformDirty(e);
                    }
                }

                if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                {
                    if (undoService)
                        (void)undoService->CommitInteractiveSceneMutation("Scale Entity");
                    gizmoState.DragActive = false;
                }
            }
        }

        if (gizmoState.DragActive && ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            if (undoService)
                undoService->CancelInteractiveSceneMutation();
            gizmoState.DragActive = false;
        }

        if (gizmoState.DragActive && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            if (undoService)
                (void)undoService->CommitInteractiveSceneMutation("Transform Entity");
            gizmoState.DragActive = false;
        }

        return gizmoState.DragActive;
    }

    void HandleGizmoKeyboardShortcuts(TransformGizmoState& gizmoState, bool viewportFocused)
    {
        if (!viewportFocused)
            return;
        if (gizmoState.DragActive)
            return;
        if (ImGui::GetIO().WantTextInput)
            return;

        if (ImGui::IsKeyPressed(ImGuiKey_Q, false))
            gizmoState.Mode = TransformGizmoMode::None;
        else if (ImGui::IsKeyPressed(ImGuiKey_W, false))
            gizmoState.Mode = TransformGizmoMode::Translate;
        else if (ImGui::IsKeyPressed(ImGuiKey_E, false))
            gizmoState.Mode = TransformGizmoMode::Rotate;
        else if (ImGui::IsKeyPressed(ImGuiKey_R, false))
            gizmoState.Mode = TransformGizmoMode::Scale;
    }

    void DrawGizmoToolbar(ImDrawList* drawList,
                          const ImVec2& viewportMin,
                          const ImVec2& viewportMax,
                          TransformGizmoState& gizmoState)
    {
        (void)viewportMax;
        const float buttonSize = 28.0f;
        const float padding = 4.0f;
        const float toolbarWidth = buttonSize * 4 + padding * 5;
        const float toolbarHeight = buttonSize + padding * 2;
        const ImVec2 toolbarMin(viewportMin.x + 10.0f, viewportMin.y + 10.0f);
        const ImVec2 toolbarMax(toolbarMin.x + toolbarWidth, toolbarMin.y + toolbarHeight);

        drawList->AddRectFilled(toolbarMin, toolbarMax, IM_COL32(30, 30, 35, 210), 5.0f);
        drawList->AddRect(toolbarMin, toolbarMax, IM_COL32(255, 255, 255, 30), 5.0f);

        struct ToolButton
        {
            const char* Label;
            const char* Shortcut;
            TransformGizmoMode Mode;
        };

        const ToolButton buttons[] = {
            {"Q", "Q", TransformGizmoMode::None},
            {"W", "W", TransformGizmoMode::Translate},
            {"E", "E", TransformGizmoMode::Rotate},
            {"R", "R", TransformGizmoMode::Scale},
        };

        for (int i = 0; i < 4; ++i)
        {
            const float x = toolbarMin.x + padding + static_cast<float>(i) * (buttonSize + padding);
            const float y = toolbarMin.y + padding;
            const ImVec2 btnMin(x, y);
            const ImVec2 btnMax(x + buttonSize, y + buttonSize);

            const bool isActive = gizmoState.Mode == buttons[i].Mode;
            const ImU32 btnColor = isActive ? IM_COL32(80, 140, 220, 220) : IM_COL32(55, 55, 60, 180);
            const ImU32 btnHoverColor = isActive ? IM_COL32(90, 155, 240, 240) : IM_COL32(70, 70, 78, 200);

            const ImVec2 mousePos = ImGui::GetMousePos();
            const bool hovered = mousePos.x >= btnMin.x && mousePos.x <= btnMax.x &&
                                 mousePos.y >= btnMin.y && mousePos.y <= btnMax.y;

            drawList->AddRectFilled(btnMin, btnMax, hovered ? btnHoverColor : btnColor, 3.0f);
            if (isActive)
                drawList->AddRect(btnMin, btnMax, IM_COL32(130, 190, 255, 255), 3.0f, 0, 1.5f);

            const ImVec2 textSize = ImGui::CalcTextSize(buttons[i].Label);
            drawList->AddText(
                ImVec2(btnMin.x + (buttonSize - textSize.x) * 0.5f,
                       btnMin.y + (buttonSize - textSize.y) * 0.5f),
                IM_COL32(235, 240, 250, 255),
                buttons[i].Label);

            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                gizmoState.Mode = buttons[i].Mode;
        }
    }
}
