#pragma once

#include <cstdint>
#include <cstddef>
#include <array>

#include <glm/glm.hpp>

namespace Limitless
{
    class Camera;

    struct SceneRenderCullingPlane
    {
        glm::vec3 Normal = glm::vec3(0.0f);
        float Distance = 0.0f;
    };

    struct SceneRenderCullingFrustum
    {
        std::array<SceneRenderCullingPlane, 6> Planes{};
        bool Valid = false;
    };

    SceneRenderCullingFrustum BuildSceneRenderCullingFrustum(const Camera& camera);
    bool ComputeSceneRenderAabbFromPoints(const glm::vec3* points, std::size_t pointCount, glm::vec3& outMinimum, glm::vec3& outMaximum);
    void ExpandSceneRenderAabb(glm::vec3& minimum, glm::vec3& maximum, const glm::vec3& point);
    bool IsSceneRenderAabbVisible(const SceneRenderCullingFrustum& frustum, const glm::vec3& minimum, const glm::vec3& maximum);
    bool IsSceneRenderQuadVisible(const SceneRenderCullingFrustum& frustum, const glm::mat4& transform);
    bool TryGetSceneRenderVisibleGridCellRange(const SceneRenderCullingFrustum& frustum,
                                               const glm::mat4& gridWorldTransform,
                                               const glm::vec2& firstCellCenter,
                                               const glm::vec2& cellSize,
                                               const glm::ivec2& gridSize,
                                               int32_t& outMinCellX,
                                               int32_t& outMinCellY,
                                               int32_t& outMaxCellX,
                                               int32_t& outMaxCellY);
}
