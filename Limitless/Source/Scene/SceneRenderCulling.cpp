#include "Scene/SceneRenderCulling.h"

#include "Graphics/Camera/Camera.h"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>

namespace Limitless
{
    namespace
    {
        glm::vec4 GetMatrixRow(const glm::mat4& matrix, int rowIndex)
        {
            return glm::vec4(
                matrix[0][rowIndex],
                matrix[1][rowIndex],
                matrix[2][rowIndex],
                matrix[3][rowIndex]);
        }

        SceneRenderCullingPlane BuildNormalizedPlane(const glm::vec4& equation)
        {
            const glm::vec3 normal(equation.x, equation.y, equation.z);
            const float normalLength = glm::length(normal);
            if (normalLength <= 0.000001f)
                return {};

            const float inverseLength = 1.0f / normalLength;
            SceneRenderCullingPlane plane{};
            plane.Normal = normal * inverseLength;
            plane.Distance = equation.w * inverseLength;
            return plane;
        }

    }

    SceneRenderCullingFrustum BuildSceneRenderCullingFrustum(const Camera& camera)
    {
        const glm::mat4 viewProjection = camera.GetViewProjectionMatrix();
        const glm::vec4 row0 = GetMatrixRow(viewProjection, 0);
        const glm::vec4 row1 = GetMatrixRow(viewProjection, 1);
        const glm::vec4 row2 = GetMatrixRow(viewProjection, 2);
        const glm::vec4 row3 = GetMatrixRow(viewProjection, 3);

        SceneRenderCullingFrustum frustum{};
        frustum.Planes[0] = BuildNormalizedPlane(row3 + row0);
        frustum.Planes[1] = BuildNormalizedPlane(row3 - row0);
        frustum.Planes[2] = BuildNormalizedPlane(row3 + row1);
        frustum.Planes[3] = BuildNormalizedPlane(row3 - row1);
        frustum.Planes[4] = BuildNormalizedPlane(row3 + row2);
        frustum.Planes[5] = BuildNormalizedPlane(row3 - row2);

        frustum.Valid = true;
        for (const SceneRenderCullingPlane& plane : frustum.Planes)
        {
            if (glm::length(plane.Normal) <= 0.000001f)
            {
                frustum.Valid = false;
                break;
            }
        }

        return frustum;
    }

    bool ComputeSceneRenderAabbFromPoints(const glm::vec3* points, std::size_t pointCount, glm::vec3& outMinimum, glm::vec3& outMaximum)
    {
        if (points == nullptr || pointCount == 0)
            return false;

        outMinimum = points[0];
        outMaximum = points[0];
        for (std::size_t index = 1; index < pointCount; ++index)
            ExpandSceneRenderAabb(outMinimum, outMaximum, points[index]);

        return true;
    }

    void ExpandSceneRenderAabb(glm::vec3& minimum, glm::vec3& maximum, const glm::vec3& point)
    {
        minimum = glm::min(minimum, point);
        maximum = glm::max(maximum, point);
    }

    bool IsSceneRenderAabbVisible(const SceneRenderCullingFrustum& frustum, const glm::vec3& minimum, const glm::vec3& maximum)
    {
        if (!frustum.Valid)
            return true;

        for (const SceneRenderCullingPlane& plane : frustum.Planes)
        {
            const glm::vec3 positiveVertex(
                plane.Normal.x >= 0.0f ? maximum.x : minimum.x,
                plane.Normal.y >= 0.0f ? maximum.y : minimum.y,
                plane.Normal.z >= 0.0f ? maximum.z : minimum.z);
            if (glm::dot(plane.Normal, positiveVertex) + plane.Distance < 0.0f)
                return false;
        }

        return true;
    }

    bool IsSceneRenderQuadVisible(const SceneRenderCullingFrustum& frustum, const glm::mat4& transform)
    {
        const glm::vec4 localCorners[4] = {
            glm::vec4(-0.5f, -0.5f, 0.0f, 1.0f),
            glm::vec4(0.5f, -0.5f, 0.0f, 1.0f),
            glm::vec4(0.5f, 0.5f, 0.0f, 1.0f),
            glm::vec4(-0.5f, 0.5f, 0.0f, 1.0f)
        };

        glm::vec3 worldCorners[4]{};
        for (std::size_t index = 0; index < 4; ++index)
        {
            const glm::vec4 worldCorner = transform * localCorners[index];
            worldCorners[index] = glm::vec3(worldCorner);
        }

        glm::vec3 minimum(0.0f);
        glm::vec3 maximum(0.0f);
        if (!ComputeSceneRenderAabbFromPoints(worldCorners, 4, minimum, maximum))
            return true;

        return IsSceneRenderAabbVisible(frustum, minimum, maximum);
    }

    bool TryGetSceneRenderVisibleGridCellRange(const SceneRenderCullingFrustum& frustum,
                                               const glm::mat4& gridWorldTransform,
                                               const glm::vec2& firstCellCenter,
                                               const glm::vec2& cellSize,
                                               const glm::ivec2& gridSize,
                                               int32_t& outMinCellX,
                                               int32_t& outMinCellY,
                                               int32_t& outMaxCellX,
                                               int32_t& outMaxCellY)
    {
        if (!frustum.Valid)
            return false;
        if (cellSize.x <= 0.000001f || cellSize.y <= 0.000001f)
            return false;

        const int32_t maxCellX = gridSize.x - 1;
        const int32_t maxCellY = gridSize.y - 1;
        if (maxCellX < 0 || maxCellY < 0)
            return false;

        // Transform frustum planes from world space to grid-local space.
        // For a plane π (as vec4) and local-to-world M: π_local = Mᵀ · π_world.
        // At z_local=0 the plane reduces to: a*x + b*y + d ≥ 0.
        const glm::mat4 transposeM = glm::transpose(gridWorldTransform);

        struct LocalPlane2D { float a; float b; float d; };
        LocalPlane2D localPlanes[6]{};
        for (int i = 0; i < 6; ++i)
        {
            const glm::vec4 worldPlane(frustum.Planes[i].Normal.x,
                                       frustum.Planes[i].Normal.y,
                                       frustum.Planes[i].Normal.z,
                                       frustum.Planes[i].Distance);
            const glm::vec4 lp = transposeM * worldPlane;
            localPlanes[i] = { lp.x, lp.y, lp.w };
        }

        // Grid rectangle in local space (covers all cells including half-cell borders).
        const glm::vec2 gridMin(
            firstCellCenter.x - cellSize.x * 0.5f,
            firstCellCenter.y - cellSize.y * 0.5f);
        const glm::vec2 gridMax(
            firstCellCenter.x + static_cast<float>(maxCellX) * cellSize.x + cellSize.x * 0.5f,
            firstCellCenter.y + static_cast<float>(maxCellY) * cellSize.y + cellSize.y * 0.5f);

        // Sutherland-Hodgman polygon clipping: clip the grid rectangle against
        // each frustum half-plane to get the exact visible region on the tile plane.
        constexpr int kMaxPolyVerts = 18;
        glm::vec2 polyA[kMaxPolyVerts];
        glm::vec2 polyB[kMaxPolyVerts];
        int polyCount = 4;
        polyA[0] = glm::vec2(gridMin.x, gridMin.y);
        polyA[1] = glm::vec2(gridMax.x, gridMin.y);
        polyA[2] = glm::vec2(gridMax.x, gridMax.y);
        polyA[3] = glm::vec2(gridMin.x, gridMax.y);

        glm::vec2* input = polyA;
        glm::vec2* output = polyB;

        for (int planeIdx = 0; planeIdx < 6; ++planeIdx)
        {
            if (polyCount == 0)
                return false;

            const LocalPlane2D& plane = localPlanes[planeIdx];
            int outputCount = 0;

            for (int i = 0; i < polyCount; ++i)
            {
                const glm::vec2& current = input[i];
                const glm::vec2& next = input[(i + 1) % polyCount];
                const float dCurrent = plane.a * current.x + plane.b * current.y + plane.d;
                const float dNext = plane.a * next.x + plane.b * next.y + plane.d;

                if (dCurrent >= 0.0f)
                {
                    if (outputCount < kMaxPolyVerts)
                        output[outputCount++] = current;
                }

                if ((dCurrent >= 0.0f) != (dNext >= 0.0f))
                {
                    const float denom = dCurrent - dNext;
                    if (std::abs(denom) > 0.000001f)
                    {
                        const float t = dCurrent / denom;
                        const glm::vec2 intersection = current + t * (next - current);
                        if (outputCount < kMaxPolyVerts)
                            output[outputCount++] = intersection;
                    }
                }
            }

            polyCount = outputCount;
            std::swap(input, output);
        }

        if (polyCount == 0)
            return false;

        // AABB of the clipped polygon in grid-local space.
        glm::vec2 clippedMin = input[0];
        glm::vec2 clippedMax = input[0];
        for (int i = 1; i < polyCount; ++i)
        {
            clippedMin = glm::min(clippedMin, input[i]);
            clippedMax = glm::max(clippedMax, input[i]);
        }

        // Convert local-space AABB to cell indices (conservative).
        outMinCellX = static_cast<int32_t>(std::floor((clippedMin.x - firstCellCenter.x) / cellSize.x));
        outMinCellY = static_cast<int32_t>(std::floor((clippedMin.y - firstCellCenter.y) / cellSize.y));
        outMaxCellX = static_cast<int32_t>(std::ceil((clippedMax.x - firstCellCenter.x) / cellSize.x));
        outMaxCellY = static_cast<int32_t>(std::ceil((clippedMax.y - firstCellCenter.y) / cellSize.y));

        outMinCellX = std::clamp(outMinCellX, 0, maxCellX);
        outMinCellY = std::clamp(outMinCellY, 0, maxCellY);
        outMaxCellX = std::clamp(outMaxCellX, 0, maxCellX);
        outMaxCellY = std::clamp(outMaxCellY, 0, maxCellY);

        return outMinCellX <= outMaxCellX && outMinCellY <= outMaxCellY;
    }
}
