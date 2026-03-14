#include "EditorViewportPanelShared.h"

#include "Graphics/Camera/Camera.h"
#include "Scene/Scene.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace Limitless::EditorViewportPanel::Internal
{
    namespace
    {
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
            return PointInTriangle(point, quad[0], quad[1], quad[2]) || PointInTriangle(point, quad[2], quad[3], quad[0]);
        }
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

    bool IsEntityUnderCanvas(Scene& scene, entt::entity entity)
    {
        auto& registry = scene.GetRegistry();
        entt::entity current = entity;
        while (current != entt::null)
        {
            if (registry.any_of<CanvasComponent>(current))
                return true;
            current = scene.GetParent(current);
        }
        return false;
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
            if (IsEntityUnderCanvas(scene, entity))
                continue;

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
                const float pixelX = (ndc.x * 0.5f + 0.5f) * viewportWidth;
                const float pixelY = (1.0f - (ndc.y * 0.5f + 0.5f)) * viewportHeight;
                projected[i] = ImVec2(viewportMin.x + pixelX, viewportMin.y + pixelY);
            }

            if (anyBehindCamera)
                continue;

            if (!PointInProjectedQuad(mouseScreenPosition, projected))
                continue;

            const float worldZ = model[3].z;
            const auto* hierarchy = registry.try_get<HierarchyComponent>(entity);
            const int32_t siblingOrder = hierarchy ? hierarchy->SiblingOrder : 0;

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
        if (camera.GetType() == CameraType::Perspective3D && clip.w <= 0.0f)
            return false;

        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        const float pixelX = (ndc.x * 0.5f + 0.5f) * viewportWidth;
        const float pixelY = (1.0f - (ndc.y * 0.5f + 0.5f)) * viewportHeight;
        outPoint = ImVec2(viewportMin.x + pixelX, viewportMin.y + pixelY);
        return true;
    }

    bool IsMouseNearPoint(const ImVec2& mousePosition, const ImVec2& point, float radiusPixels)
    {
        const float dx = mousePosition.x - point.x;
        const float dy = mousePosition.y - point.y;
        return (dx * dx + dy * dy) <= radiusPixels * radiusPixels;
    }

    float DistanceToLineSegment(const ImVec2& point, const ImVec2& a, const ImVec2& b)
    {
        const float abx = b.x - a.x;
        const float aby = b.y - a.y;
        const float lengthSquared = abx * abx + aby * aby;
        if (lengthSquared <= 0.000001f)
            return std::sqrt((point.x - a.x) * (point.x - a.x) + (point.y - a.y) * (point.y - a.y));

        const float t = std::clamp(((point.x - a.x) * abx + (point.y - a.y) * aby) / lengthSquared, 0.0f, 1.0f);
        const float closestX = a.x + t * abx;
        const float closestY = a.y + t * aby;
        return std::sqrt((point.x - closestX) * (point.x - closestX) + (point.y - closestY) * (point.y - closestY));
    }
}
