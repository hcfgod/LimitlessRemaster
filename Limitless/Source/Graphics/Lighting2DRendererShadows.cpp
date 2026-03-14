#include "Graphics/Lighting2DRendererInternal.h"

namespace Limitless::Lighting2DInternal
{
    std::vector<entt::entity> BuildSortedSpriteRenderList(Scene& scene, float interpolationAlpha, uint32_t cullingMask)
    {
        auto& registry = scene.GetRegistry();
        auto view = registry.view<TransformComponent, SpriteComponent>();

        std::vector<entt::entity> entities;
        entities.reserve(view.size_hint());
        for (entt::entity entity : view)
        {
            if (!scene.IsEntityEnabledInHierarchy(entity))
                continue;
            if (!IsEntityVisibleToCameraCullingMask(registry, entity, cullingMask))
                continue;
            entities.push_back(entity);
        }

        std::sort(entities.begin(), entities.end(), [&scene, &registry, interpolationAlpha](entt::entity left, entt::entity right) {
            const auto& leftSprite = registry.get<SpriteComponent>(left);
            const auto& rightSprite = registry.get<SpriteComponent>(right);
            if (leftSprite.RenderOrder != rightSprite.RenderOrder)
                return leftSprite.RenderOrder < rightSprite.RenderOrder;

            const glm::mat4 leftWorld = scene.GetWorldTransformMatrixForRendering(left, interpolationAlpha);
            const glm::mat4 rightWorld = scene.GetWorldTransformMatrixForRendering(right, interpolationAlpha);
            // Sort by world-space Z rather than view-space Z so the order stays
            // stable regardless of the editor camera orientation.
            const float leftWorldZ = leftWorld[3][2];
            const float rightWorldZ = rightWorld[3][2];
            constexpr float kDepthSortEpsilon = 0.005f;
            if (std::abs(leftWorldZ - rightWorldZ) > kDepthSortEpsilon)
                return leftWorldZ < rightWorldZ;

            const auto* leftHierarchy = registry.try_get<HierarchyComponent>(left);
            const auto* rightHierarchy = registry.try_get<HierarchyComponent>(right);
            const int32_t leftOrder = leftHierarchy ? leftHierarchy->SiblingOrder : 0;
            const int32_t rightOrder = rightHierarchy ? rightHierarchy->SiblingOrder : 0;
            if (leftOrder != rightOrder)
                return leftOrder < rightOrder;
            return static_cast<uint32_t>(left) < static_cast<uint32_t>(right);
        });

        return entities;
    }

    std::vector<NormalPassSpriteDraw> BuildNormalPassDrawList(Scene& scene, float interpolationAlpha, uint32_t cullingMask)
    {
        std::vector<NormalPassSpriteDraw> drawList;
        auto sortedEntities = BuildSortedSpriteRenderList(scene, interpolationAlpha, cullingMask);
        drawList.reserve(sortedEntities.size());

        auto& registry = scene.GetRegistry();
        for (entt::entity entity : sortedEntities)
        {
            auto& sprite = registry.get<SpriteComponent>(entity);

            NormalPassSpriteDraw draw{};
            draw.Model = scene.GetWorldTransformMatrixForRendering(entity, interpolationAlpha);
            draw.Color = sprite.Color;
            draw.AlbedoTexture = g_State->WhiteTexture;
            draw.NormalTexture = g_State->FlatNormalTexture;
            draw.NormalStrength = 1.0f;
            draw.ReceiveShadows = sprite.ReceiveShadows;
            draw.CasterEntityId = EncodeEntityIdToUnitVec2(entity);

            bool hasMaterialButFailed = false;
            if (auto* material = registry.try_get<MaterialComponent>(entity))
            {
                RefreshSpriteMaterialCache(*material);
                if (material->CachedMaterial)
                {
                    if (auto mainTexture = material->CachedMaterial->GetMainTexture())
                        draw.AlbedoTexture = mainTexture;

                    if (g_State->Settings.EnableNormalMaps)
                    {
                        if (auto normalTexture = material->CachedMaterial->GetNormalTexture())
                            draw.NormalTexture = normalTexture;
                        draw.NormalStrength = material->CachedMaterial->GetNormalStrength();
                    }
                }
                else if (!material->MaterialKey.empty())
                {
                    hasMaterialButFailed = true;
                }
            }

            if (!hasMaterialButFailed && draw.AlbedoTexture == g_State->WhiteTexture && !sprite.TextureKey.empty())
            {
                RefreshSpriteTextureCache(sprite);
                if (sprite.CachedTexture && sprite.CachedTexture->GetTexture())
                    draw.AlbedoTexture = sprite.CachedTexture->GetTexture();
            }

            if (hasMaterialButFailed)
                draw.Color = glm::vec4(1.0f, 0.0f, 1.0f, sprite.Color.a);

            drawList.push_back(std::move(draw));
        }

        return drawList;
    }

    void BuildPhysicsOccluderPolygon(const entt::registry& registry, entt::entity entity, std::vector<glm::vec2>& outPoints)
    {
        outPoints.clear();
        if (const auto* boxCollider = registry.try_get<BoxCollider2DComponent>(entity))
        {
            const glm::vec2 halfSize = boxCollider->Size * 0.5f;
            outPoints.push_back(boxCollider->Offset + glm::vec2(-halfSize.x, -halfSize.y));
            outPoints.push_back(boxCollider->Offset + glm::vec2(halfSize.x, -halfSize.y));
            outPoints.push_back(boxCollider->Offset + glm::vec2(halfSize.x, halfSize.y));
            outPoints.push_back(boxCollider->Offset + glm::vec2(-halfSize.x, halfSize.y));
            return;
        }

        if (const auto* circleCollider = registry.try_get<CircleCollider2DComponent>(entity))
        {
            const float radius = std::max(0.01f, circleCollider->Radius);
            outPoints.reserve(kPhysicsCircleSegmentApproximation);
            for (uint32_t index = 0; index < kPhysicsCircleSegmentApproximation; ++index)
            {
                const float phase = static_cast<float>(index) / static_cast<float>(kPhysicsCircleSegmentApproximation);
                const float angle = phase * glm::two_pi<float>();
                const glm::vec2 direction(std::cos(angle), std::sin(angle));
                outPoints.push_back(circleCollider->Offset + direction * radius);
            }
        }
    }

    std::vector<glm::vec2> ResolveOccluderLocalPolygon(const entt::registry& registry, entt::entity entity, const ShadowOccluder2DComponent& occluder)
    {
        std::vector<glm::vec2> points;
        if (occluder.Source == ShadowOccluder2DComponent::SourceMode::PhysicsCollider)
        {
            BuildPhysicsOccluderPolygon(registry, entity, points);
        }
        else
        {
            points = occluder.PolygonPoints;
        }

        if (points.empty())
            return points;

        if (occluder.Extrusion > 0.0f)
        {
            glm::vec2 centroid(0.0f);
            for (const glm::vec2& point : points)
                centroid += point;
            centroid /= static_cast<float>(points.size());
            for (glm::vec2& point : points)
            {
                const glm::vec2 direction = point - centroid;
                const float length = glm::length(direction);
                if (length > kEpsilon)
                    point += (direction / length) * occluder.Extrusion;
            }
        }

        return points;
    }

    std::vector<ShadowSegment> BuildShadowSegments(Scene& scene,
                                                    float interpolationAlpha,
                                                    const Camera& camera,
                                                    const glm::mat4& viewProjection,
                                                    uint32_t width,
                                                    uint32_t height,
                                                    uint32_t maxSegments,
                                                    uint32_t cullingMask,
                                                    uint32_t& outOccluderCount,
                                                    float shadowSegmentSnapPixels)
    {
        std::vector<ShadowSegment> segments;
        segments.reserve(maxSegments);
        outOccluderCount = 0;
        const float snappedPixels = std::max(0.0f, shadowSegmentSnapPixels);

        auto& registry = scene.GetRegistry();
        std::unordered_set<entt::entity> explicitOccluderEntities;
        explicitOccluderEntities.reserve(64);

        auto appendOccluderSegments = [&](entt::entity entity, const std::vector<glm::vec2>& localPoints, bool closed) {
            if (segments.size() >= maxSegments || localPoints.size() < 2)
                return;

            const glm::mat4 worldTransform = scene.GetWorldTransformMatrixForRendering(entity, interpolationAlpha);
            const glm::vec2 casterEntityId = EncodeEntityIdToUnitVec2(entity);

            // Work in clip space so we can clip edges at the near plane
            // instead of clamping vertices to extreme off-screen positions.
            // Clamping caused discontinuous segment jumps when clip.w
            // crossed the threshold, which was the main source of shadow
            // flicker during camera rotation.
            std::vector<glm::vec4> clipPoints;
            clipPoints.reserve(localPoints.size());
            for (const glm::vec2& localPoint : localPoints)
            {
                const glm::vec4 worldPoint = worldTransform * glm::vec4(localPoint, 0.0f, 1.0f);
                clipPoints.push_back(viewProjection * worldPoint);
            }

            if (closed && clipPoints.size() >= 3)
            {
                // Compute signed area from world-space XY so the winding
                // stays stable regardless of perspective camera orientation.
                float worldSignedArea = 0.0f;
                for (size_t index = 0; index < localPoints.size(); ++index)
                {
                    const glm::vec4 wa = worldTransform * glm::vec4(localPoints[index], 0.0f, 1.0f);
                    const glm::vec4 wb = worldTransform * glm::vec4(localPoints[(index + 1) % localPoints.size()], 0.0f, 1.0f);
                    worldSignedArea += (wa.x * wb.y) - (wb.x * wa.y);
                }

                if (worldSignedArea < 0.0f)
                    std::reverse(clipPoints.begin(), clipPoints.end());
            }

            ++outOccluderCount;

            constexpr float kNearW = 0.01f;
            const float fWidth = static_cast<float>(width);
            const float fHeight = static_cast<float>(height);

            // Project a clip-space point (with w >= kNearW) to screen space.
            // Coordinates are clamped to a generous off-screen range to
            // keep the shader's ray-segment intersection numerically stable.
            // For visible pixels the intersection point is always near the
            // on-screen segment endpoint, so this clamp does not affect
            // shadow correctness on screen.
            const float coordLimit = std::max(fWidth, fHeight) * 4.0f;
            auto projectToScreen = [fWidth, fHeight, snappedPixels, coordLimit](const glm::vec4& clip) -> glm::vec2 {
                const float ndcX = clip.x / clip.w;
                const float ndcY = clip.y / clip.w;
                float sx = (ndcX * 0.5f + 0.5f) * fWidth;
                float sy = (ndcY * 0.5f + 0.5f) * fHeight;
                sx = std::clamp(sx, -coordLimit, coordLimit);
                sy = std::clamp(sy, -coordLimit, coordLimit);
                if (snappedPixels > kEpsilon)
                {
                    sx = std::round(sx / snappedPixels) * snappedPixels;
                    sy = std::round(sy / snappedPixels) * snappedPixels;
                }
                return { sx, sy };
            };

            // Clip a single edge to the camera near plane and emit as a
            // screen-space segment.  When one vertex is behind the camera
            // the edge is smoothly clipped at the near plane boundary
            // (interpolating along the edge) instead of clamping the vertex
            // to an extreme coordinate.
            auto addClippedEdge = [&](const glm::vec4& clipA, const glm::vec4& clipB) {
                if (segments.size() >= maxSegments)
                    return;

                const bool behindA = clipA.w < kNearW;
                const bool behindB = clipB.w < kNearW;

                if (behindA && behindB)
                    return;

                glm::vec4 ca = clipA;
                glm::vec4 cb = clipB;

                if (behindA)
                {
                    const float t = (kNearW - ca.w) / (cb.w - ca.w);
                    ca = glm::mix(ca, cb, t);
                }
                else if (behindB)
                {
                    const float t = (kNearW - cb.w) / (ca.w - cb.w);
                    cb = glm::mix(cb, ca, t);
                }

                const glm::vec2 screenA = projectToScreen(ca);
                const glm::vec2 screenB = projectToScreen(cb);
                const float edgeLength = glm::length(screenB - screenA);
                if (edgeLength <= kEpsilon)
                    return;

                segments.push_back(ShadowSegment{ glm::vec4(screenA.x, screenA.y, screenB.x, screenB.y), casterEntityId });
            };

            for (size_t index = 0; index + 1 < clipPoints.size(); ++index)
                addClippedEdge(clipPoints[index], clipPoints[index + 1]);

            if (closed && clipPoints.size() >= 3 && segments.size() < maxSegments)
                addClippedEdge(clipPoints.back(), clipPoints.front());
        };

        auto appendWorldEdge = [&](const glm::vec3& worldStart, const glm::vec3& worldEnd, const glm::vec2& casterEntityId) {
            if (segments.size() >= maxSegments)
                return;

            constexpr float kNearW = 0.01f;
            const glm::vec4 clipA = viewProjection * glm::vec4(worldStart, 1.0f);
            const glm::vec4 clipB = viewProjection * glm::vec4(worldEnd, 1.0f);

            const bool behindA = clipA.w < kNearW;
            const bool behindB = clipB.w < kNearW;
            if (behindA && behindB)
                return;

            glm::vec4 ca = clipA;
            glm::vec4 cb = clipB;
            if (behindA)
            {
                const float t = (kNearW - ca.w) / (cb.w - ca.w);
                ca = glm::mix(ca, cb, t);
            }
            else if (behindB)
            {
                const float t = (kNearW - cb.w) / (ca.w - cb.w);
                cb = glm::mix(cb, ca, t);
            }

            const float fWidth = static_cast<float>(width);
            const float fHeight = static_cast<float>(height);
            const float coordLimit = std::max(fWidth, fHeight) * 4.0f;
            auto projectToScreen = [fWidth, fHeight, snappedPixels, coordLimit](const glm::vec4& clip) -> glm::vec2 {
                const float ndcX = clip.x / clip.w;
                const float ndcY = clip.y / clip.w;
                float sx = (ndcX * 0.5f + 0.5f) * fWidth;
                float sy = (ndcY * 0.5f + 0.5f) * fHeight;
                sx = std::clamp(sx, -coordLimit, coordLimit);
                sy = std::clamp(sy, -coordLimit, coordLimit);
                if (snappedPixels > kEpsilon)
                {
                    sx = std::round(sx / snappedPixels) * snappedPixels;
                    sy = std::round(sy / snappedPixels) * snappedPixels;
                }
                return { sx, sy };
            };

            const glm::vec2 screenA = projectToScreen(ca);
            const glm::vec2 screenB = projectToScreen(cb);
            if (glm::length(screenB - screenA) <= kEpsilon)
                return;

            segments.push_back(ShadowSegment{ glm::vec4(screenA.x, screenA.y, screenB.x, screenB.y), casterEntityId });
        };

        auto occluderView = registry.view<ShadowOccluder2DComponent>();
        for (entt::entity entity : occluderView)
        {
            if (segments.size() >= maxSegments)
                break;
            if (!scene.IsEntityEnabledInHierarchy(entity))
                continue;
            if (!IsEntityVisibleToCameraCullingMask(registry, entity, cullingMask))
                continue;

            auto& occluder = occluderView.get<ShadowOccluder2DComponent>(entity);
            explicitOccluderEntities.insert(entity);
            if (!occluder.Enabled)
                continue;
            if (const auto* sprite = registry.try_get<SpriteComponent>(entity); sprite && !sprite->CastShadows)
                continue;

            std::vector<glm::vec2> localPoints = ResolveOccluderLocalPolygon(registry, entity, occluder);
            appendOccluderSegments(entity, localPoints, occluder.Closed);
        }

        auto shouldAutoCastFromSprite = [&](const SpriteComponent& sprite) -> bool {
            if (!sprite.CastShadows || sprite.Color.a <= 0.01f)
                return false;
            return true;
        };

        // Hybrid fallback: collider-backed occluders for entities without an explicit ShadowOccluder2D.
        // This keeps scene authoring fast while preserving explicit component control when present.
        auto boxColliderView = registry.view<BoxCollider2DComponent>();
        for (entt::entity entity : boxColliderView)
        {
            if (segments.size() >= maxSegments)
                break;
            if (explicitOccluderEntities.contains(entity))
                continue;
            if (IsEntityInLightHierarchy(registry, entity))
                continue;
            if (!scene.IsEntityEnabledInHierarchy(entity))
                continue;
            if (!IsEntityVisibleToCameraCullingMask(registry, entity, cullingMask))
                continue;
            const auto* sprite = registry.try_get<SpriteComponent>(entity);
            if (!sprite)
                continue;
            if (!shouldAutoCastFromSprite(*sprite))
                continue;

            std::vector<glm::vec2> localPoints;
            BuildPhysicsOccluderPolygon(registry, entity, localPoints);
            appendOccluderSegments(entity, localPoints, true);
        }

        auto circleColliderView = registry.view<CircleCollider2DComponent>();
        for (entt::entity entity : circleColliderView)
        {
            if (segments.size() >= maxSegments)
                break;
            if (explicitOccluderEntities.contains(entity) || registry.all_of<BoxCollider2DComponent>(entity))
                continue;
            if (IsEntityInLightHierarchy(registry, entity))
                continue;
            if (!scene.IsEntityEnabledInHierarchy(entity))
                continue;
            if (!IsEntityVisibleToCameraCullingMask(registry, entity, cullingMask))
                continue;
            const auto* sprite = registry.try_get<SpriteComponent>(entity);
            if (!sprite)
                continue;
            if (!shouldAutoCastFromSprite(*sprite))
                continue;

            std::vector<glm::vec2> localPoints;
            BuildPhysicsOccluderPolygon(registry, entity, localPoints);
            appendOccluderSegments(entity, localPoints, true);
        }

        // Sprite fallback: allow regular sprite entities to cast shadows even
        // without explicit ShadowOccluder2D/physics collider components.
        // The shadow shape is a unit quad scaled by the entity transform, so
        // its size matches the rendered sprite.  Shadow *length* is governed
        // by the per-light ShadowDistance setting -- keep that value reasonable
        // (e.g. 2-10 world units) for top-down games.
        auto spriteView = registry.view<TransformComponent, SpriteComponent>();
        for (entt::entity entity : spriteView)
        {
            if (segments.size() >= maxSegments)
                break;
            if (explicitOccluderEntities.contains(entity))
                continue;
            if (IsEntityInLightHierarchy(registry, entity))
                continue;
            if (registry.any_of<BoxCollider2DComponent, CircleCollider2DComponent>(entity))
                continue;
            if (!scene.IsEntityEnabledInHierarchy(entity))
                continue;
            if (!IsEntityVisibleToCameraCullingMask(registry, entity, cullingMask))
                continue;
            if (IsEntityInCanvasUiHierarchy(registry, entity))
                continue;

            const auto& sprite = spriteView.get<SpriteComponent>(entity);
            if (!shouldAutoCastFromSprite(sprite))
                continue;

            // Automatic sprite fallback should not produce map-sized caster
            // quads when users scale sprites up for visual reasons (common
            // with pixel-art imports). Clamp fallback caster extents in
            // world space; explicit colliders/occluders remain exact.
            const glm::mat4 worldTransform = scene.GetWorldTransformMatrixForRendering(entity, interpolationAlpha);
            const float worldScaleX = std::max(glm::length(glm::vec2(worldTransform[0].x, worldTransform[0].y)), kEpsilon);
            const float worldScaleY = std::max(glm::length(glm::vec2(worldTransform[1].x, worldTransform[1].y)), kEpsilon);

            constexpr float kAutoSpriteCasterMaxWorldExtent = 1.5f;
            const float clampedWorldWidth = std::min(worldScaleX, kAutoSpriteCasterMaxWorldExtent);
            const float clampedWorldHeight = std::min(worldScaleY, kAutoSpriteCasterMaxWorldExtent);

            const glm::vec2 localHalfExtents(
                0.5f * clampedWorldWidth / worldScaleX,
                0.5f * clampedWorldHeight / worldScaleY);
            const std::vector<glm::vec2> localQuad = {
                glm::vec2(-localHalfExtents.x, -localHalfExtents.y),
                glm::vec2( localHalfExtents.x, -localHalfExtents.y),
                glm::vec2( localHalfExtents.x,  localHalfExtents.y),
                glm::vec2(-localHalfExtents.x,  localHalfExtents.y)
            };
            appendOccluderSegments(entity, localQuad, true);
        }

        // Tilemap fallback: emit edge segments for occupied cells in Grid2D
        // layers so tilemaps can cast directional/point shadows without
        // requiring explicit collider/occluder authoring.
        auto gridView = registry.view<TransformComponent, Grid2DComponent>();
        for (entt::entity gridEntity : gridView)
        {
            if (segments.size() >= maxSegments)
                break;
            if (!scene.IsEntityEnabledInHierarchy(gridEntity))
                continue;

            const auto& grid = gridView.get<Grid2DComponent>(gridEntity);
            const glm::vec2 cellSize(
                std::max(0.001f, grid.CellSize.x),
                std::max(0.001f, grid.CellSize.y));
            const glm::mat4 gridWorldTransform = scene.GetWorldTransformMatrixForRendering(gridEntity, interpolationAlpha);

            const auto children = scene.GetChildren(gridEntity);
            for (entt::entity layerEntity : children)
            {
                if (segments.size() >= maxSegments)
                    break;
                if (!registry.all_of<TilemapLayerComponent>(layerEntity) ||
                    !scene.IsEntityEnabledInHierarchy(layerEntity))
                {
                    continue;
                }
                if (!IsEntityVisibleToCameraCullingMask(registry, layerEntity, cullingMask))
                    continue;

                auto& layer = registry.get<TilemapLayerComponent>(layerEntity);
                // Tilemap layers must opt in to shadow casting. Keeping
                // this separate from collision avoids giant map-perimeter
                // shadows when a gameplay collision layer is broadly filled.
                if (!layer.CollisionEnabled || !layer.CastShadows)
                    continue;
                EnsureTilemapLayerStorage(grid, layer);
                if (layer.PaintedCellCacheDirty)
                    RebuildPaintedCellCache(layer);

                const int32_t widthCells = std::max(1, grid.GridSize.x);
                const int32_t heightCells = std::max(1, grid.GridSize.y);
                const glm::vec2 firstCellCenter = GetTilemapLayerFirstCellCenter(grid, layer);
                if (layer.PaintedCellRowOffsetsDirty)
                    RebuildPaintedCellRowOffsets(grid, layer);
                if (layer.PaintedCellChunkCacheDirty)
                    RebuildPaintedCellChunkCache(grid, layer);
                if (layer.ChunkTopologyDirty)
                    RebuildChunkTopology(grid, layer);

                const SceneRenderCullingFrustum shadowFrustum = BuildSceneRenderCullingFrustum(camera);
                const int32_t chunkCountX = std::max(1, layer.ChunkGridSize.x);
                const int32_t chunkCountY = std::max(1, layer.ChunkGridSize.y);

                const auto hasTileAt = [&](int32_t cellX, int32_t cellY) -> bool {
                    if (cellX < 0 || cellY < 0 || cellX >= widthCells || cellY >= heightCells)
                        return false;
                    const size_t index = static_cast<size_t>(cellY * widthCells + cellX);
                    if (index >= layer.Tiles.size())
                        return false;
                    return layer.Tiles[index] != 0u;
                };

                // Rebuild dirty chunk lighting caches.
                for (int32_t cy = 0; cy < chunkCountY; ++cy)
                {
                    for (int32_t cx = 0; cx < chunkCountX; ++cx)
                    {
                        const size_t ci = static_cast<size_t>(cy) * static_cast<size_t>(chunkCountX) + static_cast<size_t>(cx);
                        if (ci >= layer.ChunkLightingCaches.size())
                            continue;
                        auto& lc = layer.ChunkLightingCaches[ci];
                        if (!lc.Dirty)
                            continue;

                        lc.OccluderEdges.clear();
                        lc.Empty = true;

                        if (ci < layer.CachedPaintedCellChunkOffsets.size() &&
                            ci + 1u < layer.CachedPaintedCellChunkOffsets.size())
                        {
                            const uint32_t chunkStart = layer.CachedPaintedCellChunkOffsets[ci];
                            const uint32_t chunkEnd = layer.CachedPaintedCellChunkOffsets[ci + 1u];
                            for (uint32_t pcIdx = chunkStart; pcIdx < chunkEnd; ++pcIdx)
                            {
                                if (pcIdx >= layer.CachedPaintedCellChunkIndices.size())
                                    continue;
                                const uint32_t paintedIdx = layer.CachedPaintedCellChunkIndices[pcIdx];
                                if (paintedIdx >= layer.CachedPaintedCells.size())
                                    continue;
                                const auto& pc = layer.CachedPaintedCells[paintedIdx];
                                if (pc.CellIndex >= layer.Tiles.size())
                                    continue;
                                if (layer.Tiles[pc.CellIndex] == 0u)
                                    continue;

                                const int32_t cellX = static_cast<int32_t>(pc.CellIndex % static_cast<uint32_t>(widthCells));
                                const int32_t cellY = static_cast<int32_t>(pc.CellIndex / static_cast<uint32_t>(widthCells));
                                const glm::vec2 localCenter = firstCellCenter + glm::vec2(
                                    static_cast<float>(cellX) * cellSize.x,
                                    static_cast<float>(cellY) * cellSize.y);
                                const glm::vec2 localMin = localCenter - cellSize * 0.5f;
                                const glm::vec2 localMax = localCenter + cellSize * 0.5f;

                                if (!hasTileAt(cellX, cellY - 1))
                                    lc.OccluderEdges.push_back(glm::vec4(localMin.x, localMin.y, localMax.x, localMin.y));
                                if (!hasTileAt(cellX + 1, cellY))
                                    lc.OccluderEdges.push_back(glm::vec4(localMax.x, localMin.y, localMax.x, localMax.y));
                                if (!hasTileAt(cellX, cellY + 1))
                                    lc.OccluderEdges.push_back(glm::vec4(localMax.x, localMax.y, localMin.x, localMax.y));
                                if (!hasTileAt(cellX - 1, cellY))
                                    lc.OccluderEdges.push_back(glm::vec4(localMin.x, localMax.y, localMin.x, localMin.y));
                            }
                        }

                        lc.Empty = lc.OccluderEdges.empty();
                        lc.Dirty = false;
                    }
                }

                // Frustum-cull chunks and append cached edges for visible chunks.
                bool emittedAnySegment = false;
                const glm::vec2 casterEntityId = EncodeEntityIdToUnitVec2(layerEntity);
                for (int32_t cy = 0; cy < chunkCountY && segments.size() < maxSegments; ++cy)
                {
                    for (int32_t cx = 0; cx < chunkCountX && segments.size() < maxSegments; ++cx)
                    {
                        const size_t ci = static_cast<size_t>(cy) * static_cast<size_t>(chunkCountX) + static_cast<size_t>(cx);
                        if (ci >= layer.ChunkLightingCaches.size())
                            continue;
                        const auto& lc = layer.ChunkLightingCaches[ci];
                        if (lc.Empty)
                            continue;

                        // Frustum-cull using persistent chunk bounds.
                        const glm::vec3 worldCorners[4] = {
                            glm::vec3(gridWorldTransform * glm::vec4(lc.LocalMin.x, lc.LocalMin.y, 0.0f, 1.0f)),
                            glm::vec3(gridWorldTransform * glm::vec4(lc.LocalMax.x, lc.LocalMin.y, 0.0f, 1.0f)),
                            glm::vec3(gridWorldTransform * glm::vec4(lc.LocalMax.x, lc.LocalMax.y, 0.0f, 1.0f)),
                            glm::vec3(gridWorldTransform * glm::vec4(lc.LocalMin.x, lc.LocalMax.y, 0.0f, 1.0f))
                        };
                        glm::vec3 aabbMin(0.0f), aabbMax(0.0f);
                        if (ComputeSceneRenderAabbFromPoints(worldCorners, 4, aabbMin, aabbMax) &&
                            !IsSceneRenderAabbVisible(shadowFrustum, aabbMin, aabbMax))
                            continue;

                        for (const glm::vec4& edge : lc.OccluderEdges)
                        {
                            if (segments.size() >= maxSegments)
                                break;
                            const glm::vec3 worldA = glm::vec3(gridWorldTransform * glm::vec4(edge.x, edge.y, 0.0f, 1.0f));
                            const glm::vec3 worldB = glm::vec3(gridWorldTransform * glm::vec4(edge.z, edge.w, 0.0f, 1.0f));
                            const size_t beforeCount = segments.size();
                            appendWorldEdge(worldA, worldB, casterEntityId);
                            if (segments.size() > beforeCount)
                                emittedAnySegment = true;
                        }
                    }
                }

                if (emittedAnySegment)
                    ++outOccluderCount;
            }
        }

        return segments;
    }

    std::vector<glm::vec4> FilterDirectionalShadowSegmentsByFacing(const std::vector<glm::vec4>& shadowSegments, const glm::vec2& lightDirection)
    {
        if (shadowSegments.empty() || glm::length(lightDirection) <= kEpsilon)
            return shadowSegments;

        const glm::vec2 rayDirection = -glm::normalize(lightDirection);
        std::vector<glm::vec4> filteredSegments;
        filteredSegments.reserve(shadowSegments.size());
        for (const glm::vec4& segment : shadowSegments)
        {
            const glm::vec2 start(segment.x, segment.y);
            const glm::vec2 end(segment.z, segment.w);
            const glm::vec2 edge = end - start;
            if (glm::length(edge) <= kEpsilon)
                continue;

            const glm::vec2 outwardNormal = glm::normalize(glm::vec2(edge.y, -edge.x));
            if (glm::dot(outwardNormal, rayDirection) > 0.0f)
                filteredSegments.push_back(segment);
        }

        // Fallback to unfiltered segments if winding input is inconsistent.
        if (filteredSegments.empty())
            return shadowSegments;
        return filteredSegments;
    }

    std::vector<ScreenDirectionalLight> BuildDirectionalLights(Scene& scene,
                                                                float interpolationAlpha,
                                                                const Camera& camera,
                                                                const glm::mat4& viewProjection,
                                                                uint32_t width,
                                                                uint32_t height,
                                                                float pixelsPerUnit,
                                                                uint32_t cullingMask)
    {
        std::vector<ScreenDirectionalLight> lights;
        lights.reserve(std::max(0, g_State->Settings.MaxDirectionalLights));

        const uint32_t maxDirectionalLights = ClampDirectionalLightsByQuality(g_State->Settings);
        if (maxDirectionalLights == 0)
            return lights;

        auto& registry = scene.GetRegistry();
        auto directionalView = registry.view<DirectionalLight2DComponent>();
        for (entt::entity entity : directionalView)
        {
            if (lights.size() >= maxDirectionalLights)
                break;
            if (!scene.IsEntityEnabledInHierarchy(entity))
                continue;
            if (!IsEntityVisibleToCameraCullingMask(registry, entity, cullingMask))
                continue;

            auto& directional = directionalView.get<DirectionalLight2DComponent>(entity);
            if (!directional.Enabled || directional.Intensity <= 0.0f)
                continue;

            glm::vec2 worldDirection = directional.Direction;
            if (directional.UseEntityRotation)
            {
                const glm::mat4 worldTransform = scene.GetWorldTransformMatrixForRendering(entity, interpolationAlpha);
                worldDirection = glm::vec2(worldTransform[0].x, worldTransform[0].y);
            }
            if (glm::length(worldDirection) <= kEpsilon)
                worldDirection = glm::vec2(0.0f, -1.0f);
            worldDirection = glm::normalize(worldDirection);

            glm::vec4 viewDirection4 = camera.GetViewMatrix() * glm::vec4(worldDirection, 0.0f, 0.0f);
            glm::vec2 screenDirection(viewDirection4.x, viewDirection4.y);
            if (glm::length(screenDirection) <= kEpsilon)
            {
                const glm::vec2 originScreen = ProjectWorldToScreenClamped(viewProjection, glm::vec3(0.0f, 0.0f, 0.0f), width, height);
                const glm::vec2 endScreen = ProjectWorldToScreenClamped(viewProjection, glm::vec3(worldDirection, 0.0f), width, height);
                screenDirection = endScreen - originScreen;
            }
            if (glm::length(screenDirection) <= kEpsilon)
                screenDirection = glm::vec2(0.0f, -1.0f);
            screenDirection = glm::normalize(screenDirection);

            directional.RuntimeResolvedDirection = worldDirection;

            ScreenDirectionalLight screenLight{};
            screenLight.Color = glm::max(directional.Color, glm::vec3(0.0f));
            screenLight.Intensity = directional.Intensity;
            screenLight.ShadowDirection = screenDirection;
            screenLight.ShadingDirection = worldDirection;
            screenLight.CastShadows = g_State->Settings.EnableShadows && directional.CastShadows;
            screenLight.ShadowStrength = std::clamp(directional.ShadowStrength, 0.0f, 1.0f);
            screenLight.ShadowSoftnessPixels = std::max(0.0f, directional.ShadowSoftness * g_State->Settings.ShadowSoftnessScale * pixelsPerUnit);
            screenLight.ShadowSamples = ClampShadowSamplesByQuality(g_State->Settings, directional.ShadowSamples);
            screenLight.ShadowDistancePixels = std::max(1.0f, directional.ShadowDistance * pixelsPerUnit);
            // Screen-space directional ray tests need a bias to avoid self-occlusion on receiver quads.
            const float biasScale = std::max(0.0f, g_State->Settings.DirectionalShadowBiasScale);
            screenLight.ShadowBiasPixels = std::max(0.0f, directional.ShadowBias * biasScale * pixelsPerUnit);
            lights.push_back(screenLight);
        }

        return lights;
    }

    std::vector<ScreenPointLight> BuildPointLights(Scene& scene,
                                                    float interpolationAlpha,
                                                    const glm::mat4& viewProjection,
                                                    uint32_t width,
                                                    uint32_t height,
                                                    float pixelsPerUnit,
                                                    uint32_t cullingMask)
    {
        std::vector<ScreenPointLight> lights;
        lights.reserve(std::max(0, g_State->Settings.MaxPointLights));

        const uint32_t maxPointLights = ClampPointLightsByQuality(g_State->Settings);
        if (maxPointLights == 0)
            return lights;

        auto& registry = scene.GetRegistry();
        auto pointView = registry.view<PointLight2DComponent, TransformComponent>();
        for (entt::entity entity : pointView)
        {
            if (lights.size() >= maxPointLights)
                break;
            if (!scene.IsEntityEnabledInHierarchy(entity))
                continue;
            if (!IsEntityVisibleToCameraCullingMask(registry, entity, cullingMask))
                continue;

            auto& pointLight = pointView.get<PointLight2DComponent>(entity);
            if (!pointLight.Enabled || pointLight.Intensity <= 0.0f || pointLight.Radius <= 0.01f)
                continue;

            const glm::mat4 worldTransform = scene.GetWorldTransformMatrixForRendering(entity, interpolationAlpha);
            const glm::vec3 worldPosition = glm::vec3(worldTransform[3]);

            // Use the clamped projection so lights near the camera's near
            // plane are pushed off-screen instead of being dropped entirely.
            // A dropped light causes the lit area to flash to ambient-only
            // for one frame, which is the primary cause of flicker during
            // combined strafe + rotation.
            const glm::vec2 screenPosition = ProjectWorldToScreenClamped(viewProjection, worldPosition, width, height);

            const glm::vec3 worldPositionRadius = glm::vec3(worldTransform * glm::vec4(pointLight.Radius, 0.0f, 0.0f, 1.0f));
            const glm::vec2 screenRadiusPosition = ProjectWorldToScreenClamped(viewProjection, worldPositionRadius, width, height);
            float radiusPixels = glm::length(screenRadiusPosition - screenPosition);
            if (radiusPixels < 1.0f)
                radiusPixels = pointLight.Radius * pixelsPerUnit;

            ScreenPointLight screenLight{};
            screenLight.Color = glm::max(pointLight.Color, glm::vec3(0.0f));
            screenLight.Intensity = pointLight.Intensity;
            screenLight.Position = screenPosition;
            screenLight.RadiusPixels = std::max(1.0f, radiusPixels);
            screenLight.Falloff = std::max(0.1f, pointLight.Falloff);
            screenLight.CastShadows = g_State->Settings.EnableShadows && pointLight.CastShadows;
            screenLight.ShadowStrength = std::clamp(pointLight.ShadowStrength, 0.0f, 1.0f);
            screenLight.ShadowSoftnessPixels = std::max(0.0f, pointLight.ShadowSoftness * g_State->Settings.ShadowSoftnessScale * pixelsPerUnit);
            screenLight.ShadowSamples = ClampShadowSamplesByQuality(g_State->Settings, pointLight.ShadowSamples);
            screenLight.ShadowBiasPixels = std::max(0.0f, pointLight.ShadowBias * pixelsPerUnit);
            lights.push_back(screenLight);
        }

        return lights;
    }
}
