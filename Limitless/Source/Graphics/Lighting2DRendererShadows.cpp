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

    std::vector<NormalPassSpriteDraw> BuildNormalPassDrawList(Scene& scene, float interpolationAlpha, float pixelsPerUnit, uint32_t cullingMask)
    {
        std::vector<NormalPassSpriteDraw> drawList;
        auto sortedEntities = BuildSortedSpriteRenderList(scene, interpolationAlpha, cullingMask);
        drawList.reserve(sortedEntities.size());

        auto& registry = scene.GetRegistry();
        for (entt::entity entity : sortedEntities)
        {
            auto& sprite = registry.get<SpriteComponent>(entity);
            auto* animator = registry.try_get<AnimatorComponent>(entity);

            NormalPassSpriteDraw draw{};
            draw.Model = scene.GetWorldTransformMatrixForRendering(entity, interpolationAlpha);
            draw.Color = sprite.Color;
            draw.AlbedoTexture = g_State->WhiteTexture;
            draw.NormalTexture = g_State->FlatNormalTexture;
            const glm::vec2 safeTilingFactor(
                std::max(0.001f, sprite.TilingFactor.x),
                std::max(0.001f, sprite.TilingFactor.y));
            draw.UvMin = glm::vec2(0.0f, 0.0f);
            draw.UvMax = safeTilingFactor;
            draw.NormalStrength = 1.0f;
            draw.ReceiveShadows = sprite.ReceiveShadows;
            draw.CasterHeightPixels = 0.0f;
            draw.CasterEntityId = glm::vec4(0.0f);

            const bool hasAnimatorSubRect =
                animator && animator->Enabled && animator->ApplyToSprite && animator->RuntimeHasSpriteSubRect;
            const bool hasSpriteSubRect = sprite.SubSpriteIndex >= 0;
            if (hasAnimatorSubRect)
            {
                draw.UvMin = animator->RuntimeSpriteUvMin;
                glm::vec2 frameSpan = animator->RuntimeSpriteUvMax - animator->RuntimeSpriteUvMin;
                if (frameSpan.x <= 0.0001f || frameSpan.y <= 0.0001f)
                    frameSpan = glm::vec2(1.0f, 1.0f);
                draw.UvMax = animator->RuntimeSpriteUvMin + frameSpan * safeTilingFactor;
            }
            else if (hasSpriteSubRect)
            {
                draw.UvMin = sprite.UvMin;
                glm::vec2 subSpan = sprite.UvMax - sprite.UvMin;
                if (subSpan.x <= 0.0001f || subSpan.y <= 0.0001f)
                    subSpan = glm::vec2(1.0f, 1.0f);
                draw.UvMax = sprite.UvMin + subSpan * safeTilingFactor;
            }

            bool hasMaterialButFailed = false;
            bool hasAnimatedTextureOverride = false;
            if (animator && animator->Enabled && animator->ApplyToSprite && !animator->RuntimeSpriteTextureOverrideKey.empty())
            {
                if (!animator->RuntimeCachedSpriteTextureOverride)
                {
                    animator->RuntimeCachedSpriteTextureOverride = std::dynamic_pointer_cast<Assets::TextureAsset>(
                        Assets::AssetManager::GetCachedByKey(animator->RuntimeSpriteTextureOverrideKey));
                }

                if (animator->RuntimeCachedSpriteTextureOverride &&
                    animator->RuntimeCachedSpriteTextureOverride->GetTexture())
                {
                    draw.AlbedoTexture = animator->RuntimeCachedSpriteTextureOverride->GetTexture();
                    hasAnimatedTextureOverride = true;
                }
            }

            if (auto* material = registry.try_get<MaterialComponent>(entity))
            {
                RefreshSpriteMaterialCache(*material);
                if (material->CachedMaterial)
                {
                    if (!hasAnimatedTextureOverride)
                    {
                        if (auto mainTexture = material->CachedMaterial->GetMainTexture())
                        {
                            draw.AlbedoTexture = mainTexture;
                        }
                    }

                    if (!hasAnimatorSubRect && !hasSpriteSubRect && !hasAnimatedTextureOverride &&
                        material->CachedMaterial->HasMainTextureSubRect())
                    {
                        draw.UvMin = material->CachedMaterial->GetMainTextureUvMin();
                        glm::vec2 subSpan = material->CachedMaterial->GetMainTextureUvMax() - material->CachedMaterial->GetMainTextureUvMin();
                        if (subSpan.x <= 0.0001f || subSpan.y <= 0.0001f)
                            subSpan = glm::vec2(1.0f, 1.0f);
                        draw.UvMax = material->CachedMaterial->GetMainTextureUvMin() + subSpan * safeTilingFactor;
                    }

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
                {
                    draw.AlbedoTexture = sprite.CachedTexture->GetTexture();
                }
            }

            if (hasMaterialButFailed)
                draw.Color = glm::vec4(1.0f, 0.0f, 1.0f, sprite.Color.a);

            const float spriteWorldWidth = glm::length(glm::vec3(draw.Model[0]));
            const float spriteWorldHeight = glm::length(glm::vec3(draw.Model[1]));
            const float spriteScreenExtentPixels = std::max(spriteWorldWidth, spriteWorldHeight) * pixelsPerUnit;
            draw.CasterHeightPixels = std::max(spriteScreenExtentPixels * 0.35f, 2.0f);

            const bool hasExplicitShadowOccluder = registry.any_of<ShadowOccluder2DComponent>(entity);
            if (sprite.CastShadows &&
                draw.Color.a > 0.01f &&
                (draw.AlbedoTexture != g_State->WhiteTexture || hasExplicitShadowOccluder))
                draw.CasterEntityId = EncodeEntityIdToUnitVec4(entity);

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
        float maxDirectionalShadowDistanceWorld = 0.0f;
        glm::vec2 directionalShadowCullPaddingMinWorld(0.0f);
        glm::vec2 directionalShadowCullPaddingMaxWorld(0.0f);
        bool hasShadowCastingLights = false;
        float maxPointShadowRadiusWorld = 0.0f;
        const bool useGameplayShadowWarmMargin = camera.GetUsage() == CameraUsage::Gameplay;
        const bool useEditorPerspectiveShadowWarmMargin =
            camera.GetUsage() == CameraUsage::Editor && camera.GetType() == CameraType::Perspective3D;
        if (!g_State->Settings.EnableShadows)
            return segments;
        auto directionalLightView = registry.view<DirectionalLight2DComponent>();
        for (entt::entity entity : directionalLightView)
        {
            if (!scene.IsEntityEnabledInHierarchy(entity))
                continue;
            if (!IsEntityVisibleToCameraCullingMask(registry, entity, cullingMask))
                continue;

            const auto& directional = directionalLightView.get<DirectionalLight2DComponent>(entity);
            if (!directional.Enabled || !directional.CastShadows || directional.Intensity <= 0.0f)
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

            hasShadowCastingLights = true;
            const float shadowDistanceWorld = std::max(0.0f, directional.ShadowDistance);
            maxDirectionalShadowDistanceWorld = std::max(maxDirectionalShadowDistanceWorld, shadowDistanceWorld);
            directionalShadowCullPaddingMinWorld.x = std::max(directionalShadowCullPaddingMinWorld.x, std::max(0.0f, worldDirection.x) * shadowDistanceWorld);
            directionalShadowCullPaddingMinWorld.y = std::max(directionalShadowCullPaddingMinWorld.y, std::max(0.0f, worldDirection.y) * shadowDistanceWorld);
            directionalShadowCullPaddingMaxWorld.x = std::max(directionalShadowCullPaddingMaxWorld.x, std::max(0.0f, -worldDirection.x) * shadowDistanceWorld);
            directionalShadowCullPaddingMaxWorld.y = std::max(directionalShadowCullPaddingMaxWorld.y, std::max(0.0f, -worldDirection.y) * shadowDistanceWorld);
        }
        const float baseShadowCullPaddingWorld =
            camera.GetType() == CameraType::Perspective3D
                ? maxDirectionalShadowDistanceWorld
                : maxDirectionalShadowDistanceWorld * 0.2f;
        directionalShadowCullPaddingMinWorld = glm::max(directionalShadowCullPaddingMinWorld, glm::vec2(baseShadowCullPaddingWorld));
        directionalShadowCullPaddingMaxWorld = glm::max(directionalShadowCullPaddingMaxWorld, glm::vec2(baseShadowCullPaddingWorld));

        auto pointLightView = registry.view<PointLight2DComponent, TransformComponent>();
        for (entt::entity entity : pointLightView)
        {
            if (!scene.IsEntityEnabledInHierarchy(entity))
                continue;
            if (!IsEntityVisibleToCameraCullingMask(registry, entity, cullingMask))
                continue;

            const auto& pointLight = pointLightView.get<PointLight2DComponent>(entity);
            if (!pointLight.Enabled || !pointLight.CastShadows || pointLight.Intensity <= 0.0f || pointLight.Radius <= 0.01f)
                continue;

            hasShadowCastingLights = true;
            const glm::mat4 worldTransform = scene.GetWorldTransformMatrixForRendering(entity, interpolationAlpha);
            const float worldRadiusScaleX = glm::length(glm::vec3(worldTransform[0]));
            const float worldRadiusScaleY = glm::length(glm::vec3(worldTransform[1]));
            const float lightRadiusWorld = std::max(pointLight.Radius * std::max(worldRadiusScaleX, worldRadiusScaleY), pointLight.Radius);
            maxPointShadowRadiusWorld = std::max(maxPointShadowRadiusWorld, lightRadiusWorld);
        }

        if (!hasShadowCastingLights)
            return segments;

        const SceneRenderCullingFrustum shadowFrustum = BuildSceneRenderCullingFrustum(camera);
        const glm::vec3 shadowCullPaddingMinBase(
            directionalShadowCullPaddingMinWorld.x + maxPointShadowRadiusWorld,
            directionalShadowCullPaddingMinWorld.y + maxPointShadowRadiusWorld,
            maxDirectionalShadowDistanceWorld + maxPointShadowRadiusWorld);
        const glm::vec3 shadowCullPaddingMaxBase(
            directionalShadowCullPaddingMaxWorld.x + maxPointShadowRadiusWorld,
            directionalShadowCullPaddingMaxWorld.y + maxPointShadowRadiusWorld,
            maxDirectionalShadowDistanceWorld + maxPointShadowRadiusWorld);
        const auto computeShadowWarmPadding = [&](const glm::vec3& boundsExtents) {
            if (useGameplayShadowWarmMargin)
                return glm::max(boundsExtents * 0.2f, glm::vec3(1.5f, 1.5f, 0.5f));
            if (useEditorPerspectiveShadowWarmMargin)
                return glm::max(boundsExtents * 0.08f, glm::vec3(0.75f, 0.75f, 0.25f));
            return glm::vec3(0.0f);
        };
        auto isWorldBoundsShadowRelevant = [&](const glm::mat4& worldTransform, const glm::vec2& localMinimum, const glm::vec2& localMaximum) {
            const glm::vec3 worldCorners[4] = {
                glm::vec3(worldTransform * glm::vec4(localMinimum.x, localMinimum.y, 0.0f, 1.0f)),
                glm::vec3(worldTransform * glm::vec4(localMaximum.x, localMinimum.y, 0.0f, 1.0f)),
                glm::vec3(worldTransform * glm::vec4(localMaximum.x, localMaximum.y, 0.0f, 1.0f)),
                glm::vec3(worldTransform * glm::vec4(localMinimum.x, localMaximum.y, 0.0f, 1.0f))
            };
            glm::vec3 aabbMin(0.0f), aabbMax(0.0f);
            if (!ComputeSceneRenderAabbFromPoints(worldCorners, 4, aabbMin, aabbMax))
                return true;

            glm::vec3 shadowCullPaddingMin = shadowCullPaddingMinBase;
            glm::vec3 shadowCullPaddingMax = shadowCullPaddingMaxBase;
            const glm::vec3 boundsExtents = glm::max(aabbMax - aabbMin, glm::vec3(0.0f));
            const glm::vec3 shadowWarmPadding = computeShadowWarmPadding(boundsExtents);
            shadowCullPaddingMin += shadowWarmPadding;
            shadowCullPaddingMax += shadowWarmPadding;

            return IsSceneRenderAabbVisible(shadowFrustum, aabbMin - shadowCullPaddingMin, aabbMax + shadowCullPaddingMax);
        };
        auto isWorldPolygonShadowRelevant = [&](const glm::mat4& worldTransform, const std::vector<glm::vec2>& localPoints) {
            if (localPoints.empty())
                return true;

            glm::vec2 localMinimum = localPoints.front();
            glm::vec2 localMaximum = localPoints.front();
            for (size_t index = 1; index < localPoints.size(); ++index)
            {
                localMinimum = glm::min(localMinimum, localPoints[index]);
                localMaximum = glm::max(localMaximum, localPoints[index]);
            }

            return isWorldBoundsShadowRelevant(worldTransform, localMinimum, localMaximum);
        };

        auto appendOccluderSegments = [&](entt::entity entity, const glm::mat4& worldTransform, const std::vector<glm::vec2>& localPoints, bool closed, int32_t segmentFlags) {
            if (segments.size() >= maxSegments || localPoints.size() < 2)
                return;

            const glm::vec4 casterEntityId = EncodeEntityIdToUnitVec4(entity);

            // Work in clip space so we can clip edges at the near plane
            // instead of clamping vertices to extreme off-screen positions.
            // Clamping caused discontinuous segment jumps when clip.w
            // crossed the threshold, which was the main source of shadow
            // flicker during camera rotation.
            // Compute world points once and reuse for both clip projection
            // and signed area to avoid redundant matrix-vector multiplies.
            std::vector<glm::vec3> worldPoints;
            std::vector<glm::vec4> clipPoints;
            worldPoints.reserve(localPoints.size());
            clipPoints.reserve(localPoints.size());
            for (const glm::vec2& localPoint : localPoints)
            {
                const glm::vec3 wp(worldTransform * glm::vec4(localPoint, 0.0f, 1.0f));
                worldPoints.push_back(wp);
                clipPoints.push_back(viewProjection * glm::vec4(wp, 1.0f));
            }

            if (closed && worldPoints.size() >= 3)
            {
                // Compute signed area from world-space XY so the winding
                // stays stable regardless of perspective camera orientation.
                float worldSignedArea = 0.0f;
                for (size_t index = 0; index < worldPoints.size(); ++index)
                {
                    const glm::vec3& wa = worldPoints[index];
                    const glm::vec3& wb = worldPoints[(index + 1) % worldPoints.size()];
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
            // Coordinates are clamped to a moderate off-screen range to
            // keep the shader's ray-segment intersection numerically stable
            // and prevent degenerate near-plane projections from creating
            // huge segments that cause edge-of-viewport shadow artifacts.
            const float coordLimit = std::max(fWidth, fHeight) * 2.0f;
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

                const float maxSegmentScreenLength = std::max(fWidth, fHeight) * 1.5f;
                if (edgeLength > maxSegmentScreenLength)
                    return;

                segments.push_back(ShadowSegment{ glm::vec4(screenA.x, screenA.y, screenB.x, screenB.y), casterEntityId, segmentFlags });
            };

            for (size_t index = 0; index + 1 < clipPoints.size(); ++index)
                addClippedEdge(clipPoints[index], clipPoints[index + 1]);

            if (closed && clipPoints.size() >= 3 && segments.size() < maxSegments)
                addClippedEdge(clipPoints.back(), clipPoints.front());
        };

        auto appendWorldEdge = [&](const glm::vec3& worldStart, const glm::vec3& worldEnd, const glm::vec4& casterEntityId, int32_t segmentFlags) {
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
            const float coordLimit = std::max(fWidth, fHeight) * 2.0f;
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
            const float edgeLen = glm::length(screenB - screenA);
            if (edgeLen <= kEpsilon)
                return;

            const float maxSegLen = std::max(fWidth, fHeight) * 1.5f;
            if (edgeLen > maxSegLen)
                return;

            segments.push_back(ShadowSegment{ glm::vec4(screenA.x, screenA.y, screenB.x, screenB.y), casterEntityId });
        };

        auto resolveSpriteShadowTextureKey = [&](entt::entity entity, const SpriteComponent& sprite) -> std::string {
            std::string textureKey = sprite.TextureKey;

            const auto* animator = registry.try_get<AnimatorComponent>(entity);
            const bool animatorActive = animator && animator->Enabled && animator->ApplyToSprite;
            if (animatorActive && !animator->RuntimeSpriteTextureOverrideKey.empty())
                textureKey = animator->RuntimeSpriteTextureOverrideKey;

            if (textureKey.empty())
            {
                const auto* material = registry.try_get<MaterialComponent>(entity);
                if (material && material->CachedMaterial)
                {
                    auto mainTex = material->CachedMaterial->GetMainTextureHandle().Lock();
                    if (mainTex)
                        textureKey = mainTex->GetKey();
                }
            }

            return textureKey;
        };

        auto shouldAutoCastFromSprite = [&](entt::entity entity, const SpriteComponent& sprite) -> bool {
            if (!sprite.CastShadows || sprite.Color.a <= 0.01f)
                return false;
            return !resolveSpriteShadowTextureKey(entity, sprite).empty();
        };

        auto shouldUseAlphaRaymarchOnly = [&](entt::entity /*entity*/, const SpriteComponent& /*sprite*/) -> bool {
            return false;
        };

        // Try to resolve an alpha-based convex hull for a sprite entity.
        // Returns a non-empty vector (in local [-0.5,0.5] space) when the
        // sprite texture has a non-trivial silhouette; empty on failure/cache-miss
        // so callers fall back to the collider or quad shape.
        auto tryGetSpriteAlphaHull = [&](entt::entity entity, const SpriteComponent& sprite) -> std::vector<glm::vec2> {
            std::string textureKey = resolveSpriteShadowTextureKey(entity, sprite);
            int32_t subIndex = sprite.SubSpriteIndex;

            const auto* animator = registry.try_get<AnimatorComponent>(entity);
            const bool animatorActive = animator && animator->Enabled && animator->ApplyToSprite;

            if (textureKey.empty())
                return {};

            // When the animator provides a sub-rect (sprite sheet frame), pass the
            // UV rect directly.  The hull function decodes the image from disk and
            // resolves pixel coordinates internally, so it works even when the GPU
            // texture hasn't been cached yet.
            if (animatorActive && animator->RuntimeHasSpriteSubRect)
            {
                return GetOrBuildSpriteAlphaHullForUvRect(
                    textureKey,
                    animator->RuntimeSpriteUvMin,
                    animator->RuntimeSpriteUvMax);
            }

            // Non-animated sprite or sprite with a known sub-sprite index.
            return GetOrBuildSpriteAlphaHull(textureKey, subIndex);
        };

        auto isDefaultUnitQuadOccluder = [&](const ShadowOccluder2DComponent& occluder) -> bool {
            if (occluder.Source != ShadowOccluder2DComponent::SourceMode::ManualPolygon ||
                occluder.PolygonPoints.size() != 4)
            {
                return false;
            }

            static const glm::vec2 kDefaultPoints[4] = {
                glm::vec2(-0.5f, -0.5f),
                glm::vec2(0.5f, -0.5f),
                glm::vec2(0.5f, 0.5f),
                glm::vec2(-0.5f, 0.5f)
            };

            for (size_t i = 0; i < 4; ++i)
            {
                if (glm::length(occluder.PolygonPoints[i] - kDefaultPoints[i]) > kEpsilon)
                    return false;
            }

            return true;
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
            const auto* sprite = registry.try_get<SpriteComponent>(entity);
            if (sprite && !sprite->CastShadows)
                continue;

            std::vector<glm::vec2> localPoints;
            if (sprite &&
                (occluder.Source == ShadowOccluder2DComponent::SourceMode::PhysicsCollider ||
                 isDefaultUnitQuadOccluder(occluder)))
            {
                localPoints = tryGetSpriteAlphaHull(entity, *sprite);
            }

            if (localPoints.empty())
                localPoints = ResolveOccluderLocalPolygon(registry, entity, occluder);
            else if (occluder.Extrusion > 0.0f && localPoints.size() >= 3)
            {
                // Apply extrusion to hull points (same logic as ResolveOccluderLocalPolygon).
                glm::vec2 centroid(0.0f);
                for (const glm::vec2& point : localPoints)
                    centroid += point;
                centroid /= static_cast<float>(localPoints.size());
                for (glm::vec2& point : localPoints)
                {
                    const glm::vec2 direction = point - centroid;
                    const float length = glm::length(direction);
                    if (length > kEpsilon)
                        point += (direction / length) * occluder.Extrusion;
                }
            }

            const glm::mat4 occluderWorldTransform = scene.GetWorldTransformMatrixForRendering(entity, interpolationAlpha);
            if (!isWorldPolygonShadowRelevant(occluderWorldTransform, localPoints))
                continue;

            if (sprite)
            {
                if (!localPoints.empty() &&
                    (occluder.Source == ShadowOccluder2DComponent::SourceMode::PhysicsCollider ||
                     isDefaultUnitQuadOccluder(occluder)))
                {
                    if (shouldUseAlphaRaymarchOnly(entity, *sprite))
                        continue;
                }
            }

            const int32_t segmentFlags = (sprite &&
                (occluder.Source == ShadowOccluder2DComponent::SourceMode::PhysicsCollider ||
                 isDefaultUnitQuadOccluder(occluder)))
                ? kShadowSegmentFlagOffscreenOnly
                : 0;
            appendOccluderSegments(entity, occluderWorldTransform, localPoints, occluder.Closed, segmentFlags);
        }

        // Hybrid fallback: collider-backed occluders for entities without an explicit ShadowOccluder2D.
        // This keeps scene authoring fast while preserving explicit component control when present.
        // When the sprite has an alpha hull available, prefer that over the box collider shape
        // so shadows match the visible silhouette instead of the physics bounds.
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
            if (!shouldAutoCastFromSprite(entity, *sprite))
                continue;

            const auto& boxCollider = boxColliderView.get<BoxCollider2DComponent>(entity);
            const glm::vec2 boxHalfSize = boxCollider.Size * 0.5f;
            const glm::mat4 boxWorldTransform = scene.GetWorldTransformMatrixForRendering(entity, interpolationAlpha);
            if (!isWorldBoundsShadowRelevant(boxWorldTransform, boxCollider.Offset - boxHalfSize, boxCollider.Offset + boxHalfSize))
                continue;

            std::vector<glm::vec2> localPoints = tryGetSpriteAlphaHull(entity, *sprite);
            if (localPoints.empty())
            {
                if (shouldUseAlphaRaymarchOnly(entity, *sprite))
                    continue;
                LT_CORE_TRACE("SpriteAlphaHull: no hull for entity {} (textureKey='{}', sub={}), using box collider",
                    static_cast<uint32_t>(entity), sprite->TextureKey, sprite->SubSpriteIndex);
                BuildPhysicsOccluderPolygon(registry, entity, localPoints);
                appendOccluderSegments(entity, boxWorldTransform, localPoints, true, kShadowSegmentFlagOffscreenOnly);
            }
            else
            {
                if (!shouldUseAlphaRaymarchOnly(entity, *sprite))
                    appendOccluderSegments(entity, boxWorldTransform, localPoints, true, kShadowSegmentFlagOffscreenOnly);
            }
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
            if (!shouldAutoCastFromSprite(entity, *sprite))
                continue;

            const auto& circleCollider = circleColliderView.get<CircleCollider2DComponent>(entity);
            const float circleRadius = std::max(0.01f, circleCollider.Radius);
            const glm::mat4 circleWorldTransform = scene.GetWorldTransformMatrixForRendering(entity, interpolationAlpha);
            if (!isWorldBoundsShadowRelevant(circleWorldTransform, circleCollider.Offset - glm::vec2(circleRadius), circleCollider.Offset + glm::vec2(circleRadius)))
                continue;

            std::vector<glm::vec2> localPoints = tryGetSpriteAlphaHull(entity, *sprite);
            if (localPoints.empty())
            {
                if (shouldUseAlphaRaymarchOnly(entity, *sprite))
                    continue;
                BuildPhysicsOccluderPolygon(registry, entity, localPoints);
                appendOccluderSegments(entity, circleWorldTransform, localPoints, true, kShadowSegmentFlagOffscreenOnly);
            }
            else
            {
                if (!shouldUseAlphaRaymarchOnly(entity, *sprite))
                    appendOccluderSegments(entity, circleWorldTransform, localPoints, true, kShadowSegmentFlagOffscreenOnly);
            }
        }

        // Sprite fallback: allow regular sprite entities to cast shadows even
        // without explicit ShadowOccluder2D/physics collider components.
        // When an alpha hull is available, use it for accurate silhouette shadows.
        // Otherwise fall back to a unit quad (clamped to a max world extent).
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
            if (!shouldAutoCastFromSprite(entity, sprite))
                continue;
            const glm::mat4 spriteWorldTransform = scene.GetWorldTransformMatrixForRendering(entity, interpolationAlpha);
            if (!isWorldBoundsShadowRelevant(spriteWorldTransform, glm::vec2(-0.5f, -0.5f), glm::vec2(0.5f, 0.5f)))
                continue;

            std::vector<glm::vec2> alphaHull = tryGetSpriteAlphaHull(entity, sprite);
            if (!alphaHull.empty())
            {
                if (!shouldUseAlphaRaymarchOnly(entity, sprite))
                    appendOccluderSegments(entity, spriteWorldTransform, alphaHull, true, kShadowSegmentFlagOffscreenOnly);
                continue;
            }

            // Sprites that rely on alpha raymarching should not fall back
            // to rectangular quad segments when the alpha hull is unavailable.
            if (shouldUseAlphaRaymarchOnly(entity, sprite))
                continue;

            // Automatic sprite fallback should not produce map-sized caster
            // quads when users scale sprites up for visual reasons (common
            // with pixel-art imports). Clamp fallback caster extents in
            // world space; explicit colliders/occluders remain exact.
            const float worldScaleX = std::max(glm::length(glm::vec2(spriteWorldTransform[0].x, spriteWorldTransform[0].y)), kEpsilon);
            const float worldScaleY = std::max(glm::length(glm::vec2(spriteWorldTransform[1].x, spriteWorldTransform[1].y)), kEpsilon);

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
            appendOccluderSegments(entity, spriteWorldTransform, localQuad, true, kShadowSegmentFlagOffscreenOnly);
        }

        // Tilemap fallback: emit edge segments for occupied cells in Grid2D
        // layers so tilemaps can cast directional/point shadows without
        // requiring explicit collider/occluder authoring.
        const auto mergeTileOccluderEdges = [](std::vector<glm::vec4>& edges) {
            if (edges.size() < 2)
                return;

            struct EdgeRun
            {
                bool Horizontal = false;
                bool Forward = false;
                float Line = 0.0f;
                float Start = 0.0f;
                float End = 0.0f;
            };

            std::vector<EdgeRun> runs;
            std::vector<glm::vec4> passthrough;
            runs.reserve(edges.size());
            passthrough.reserve(edges.size());
            for (const glm::vec4& edge : edges)
            {
                const bool horizontal = std::abs(edge.y - edge.w) <= kEpsilon;
                const bool vertical = std::abs(edge.x - edge.z) <= kEpsilon;
                if (!horizontal && !vertical)
                {
                    passthrough.push_back(edge);
                    continue;
                }

                EdgeRun run{};
                run.Horizontal = horizontal;
                if (horizontal)
                {
                    run.Forward = edge.z >= edge.x;
                    run.Line = (edge.y + edge.w) * 0.5f;
                    run.Start = std::min(edge.x, edge.z);
                    run.End = std::max(edge.x, edge.z);
                }
                else
                {
                    run.Forward = edge.w >= edge.y;
                    run.Line = (edge.x + edge.z) * 0.5f;
                    run.Start = std::min(edge.y, edge.w);
                    run.End = std::max(edge.y, edge.w);
                }
                runs.push_back(run);
            }

            std::sort(runs.begin(), runs.end(), [](const EdgeRun& left, const EdgeRun& right) {
                if (left.Horizontal != right.Horizontal)
                    return left.Horizontal < right.Horizontal;
                if (left.Forward != right.Forward)
                    return left.Forward < right.Forward;
                if (std::abs(left.Line - right.Line) > kEpsilon)
                    return left.Line < right.Line;
                if (std::abs(left.Start - right.Start) > kEpsilon)
                    return left.Start < right.Start;
                return left.End < right.End;
            });

            std::vector<EdgeRun> mergedRuns;
            mergedRuns.reserve(runs.size());
            for (const EdgeRun& run : runs)
            {
                if (!mergedRuns.empty())
                {
                    EdgeRun& merged = mergedRuns.back();
                    if (merged.Horizontal == run.Horizontal &&
                        merged.Forward == run.Forward &&
                        std::abs(merged.Line - run.Line) <= kEpsilon &&
                        run.Start <= merged.End + kEpsilon)
                    {
                        merged.End = std::max(merged.End, run.End);
                        continue;
                    }
                }

                mergedRuns.push_back(run);
            }

            edges.clear();
            edges.reserve(mergedRuns.size() + passthrough.size());
            for (const EdgeRun& run : mergedRuns)
            {
                if (run.Horizontal)
                {
                    if (run.Forward)
                        edges.emplace_back(run.Start, run.Line, run.End, run.Line);
                    else
                        edges.emplace_back(run.End, run.Line, run.Start, run.Line);
                }
                else
                {
                    if (run.Forward)
                        edges.emplace_back(run.Line, run.Start, run.Line, run.End);
                    else
                        edges.emplace_back(run.Line, run.End, run.Line, run.Start);
                }
            }
            edges.insert(edges.end(), passthrough.begin(), passthrough.end());
        };

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

                        mergeTileOccluderEdges(lc.OccluderEdges);
                        lc.Empty = lc.OccluderEdges.empty();
                        lc.Dirty = false;
                    }
                }

                // Frustum-cull chunks and append cached edges for visible chunks.
                bool emittedAnySegment = false;
                const glm::vec4 casterEntityId = EncodeEntityIdToUnitVec4(layerEntity);
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

                        // Frustum-cull using persistent chunk bounds, but pad by
                        // the active directional shadow distance so offscreen
                        // tile occluders just outside the view can still cast
                        // into the viewport instead of popping at the edge.
                        const glm::vec3 worldCorners[4] = {
                            glm::vec3(gridWorldTransform * glm::vec4(lc.LocalMin.x, lc.LocalMin.y, 0.0f, 1.0f)),
                            glm::vec3(gridWorldTransform * glm::vec4(lc.LocalMax.x, lc.LocalMin.y, 0.0f, 1.0f)),
                            glm::vec3(gridWorldTransform * glm::vec4(lc.LocalMax.x, lc.LocalMax.y, 0.0f, 1.0f)),
                            glm::vec3(gridWorldTransform * glm::vec4(lc.LocalMin.x, lc.LocalMax.y, 0.0f, 1.0f))
                        };
                        glm::vec3 aabbMin(0.0f), aabbMax(0.0f);
                        if (ComputeSceneRenderAabbFromPoints(worldCorners, 4, aabbMin, aabbMax))
                        {
                            glm::vec3 shadowCullPaddingMin = shadowCullPaddingMinBase;
                            glm::vec3 shadowCullPaddingMax = shadowCullPaddingMaxBase;
                            const glm::vec3 chunkExtents = glm::max(aabbMax - aabbMin, glm::vec3(0.0f));
                            const glm::vec3 shadowWarmPadding = computeShadowWarmPadding(chunkExtents);
                            shadowCullPaddingMin += shadowWarmPadding;
                            shadowCullPaddingMax += shadowWarmPadding;
                            if (!IsSceneRenderAabbVisible(shadowFrustum, aabbMin - shadowCullPaddingMin, aabbMax + shadowCullPaddingMax))
                                continue;
                        }

                        for (const glm::vec4& edge : lc.OccluderEdges)
                        {
                            if (segments.size() >= maxSegments)
                                break;
                            const glm::vec3 worldA = glm::vec3(gridWorldTransform * glm::vec4(edge.x, edge.y, 0.0f, 1.0f));
                            const glm::vec3 worldB = glm::vec3(gridWorldTransform * glm::vec4(edge.z, edge.w, 0.0f, 1.0f));
                            const size_t beforeCount = segments.size();
                            appendWorldEdge(worldA, worldB, casterEntityId, 0);
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
        glm::vec3 screenDirectionReferencePoint = glm::vec3(0.0f);
        {
            const glm::mat4 cameraWorld = glm::inverse(camera.GetViewMatrix());
            const glm::vec3 cameraPosition = glm::vec3(cameraWorld[3]);
            const glm::vec3 cameraForward = glm::normalize(-glm::vec3(cameraWorld[2]));
            if (std::abs(cameraForward.z) > kEpsilon)
            {
                const float t = (0.0f - cameraPosition.z) / cameraForward.z;
                if (t > 0.0f && std::isfinite(t))
                    screenDirectionReferencePoint = cameraPosition + cameraForward * t;
            }
        }
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

            const glm::vec2 referenceScreen = ProjectWorldToScreenClamped(
                viewProjection,
                screenDirectionReferencePoint,
                width,
                height);
            const glm::vec2 offsetScreen = ProjectWorldToScreenClamped(
                viewProjection,
                screenDirectionReferencePoint + glm::vec3(worldDirection, 0.0f),
                width,
                height);
            glm::vec2 screenDirection = offsetScreen - referenceScreen;
            if (glm::length(screenDirection) <= kEpsilon)
            {
                glm::vec4 viewDirection4 = camera.GetViewMatrix() * glm::vec4(worldDirection, 0.0f, 0.0f);
                screenDirection = glm::vec2(viewDirection4.x, viewDirection4.y);
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
            screenLight.ShadowSoftnessPixels = std::max(0.0f, directional.ShadowSoftness * g_State->Settings.ShadowSoftnessScale * std::sqrt(std::max(1.0f, pixelsPerUnit)));
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
                                                    const Camera& camera,
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
        const SceneRenderCullingFrustum lightFrustum = BuildSceneRenderCullingFrustum(camera);
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
            const float worldRadiusScaleX = glm::length(glm::vec3(worldTransform[0]));
            const float worldRadiusScaleY = glm::length(glm::vec3(worldTransform[1]));
            const float lightRadiusWorld = std::max(pointLight.Radius * std::max(worldRadiusScaleX, worldRadiusScaleY), pointLight.Radius);
            const glm::vec3 lightBoundsExtent(lightRadiusWorld);
            if (!IsSceneRenderAabbVisible(lightFrustum, worldPosition - lightBoundsExtent, worldPosition + lightBoundsExtent))
            {
                pointLight.RuntimeViewportPosition = glm::vec2(0.0f);
                pointLight.RuntimeViewportRadius = 0.0f;
                continue;
            }

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
            screenLight.ShadowSoftnessPixels = std::max(0.0f, pointLight.ShadowSoftness * g_State->Settings.ShadowSoftnessScale * std::sqrt(std::max(1.0f, pixelsPerUnit)));
            screenLight.ShadowSamples = ClampShadowSamplesByQuality(g_State->Settings, pointLight.ShadowSamples);
            screenLight.ShadowBiasPixels = std::max(0.0f, pointLight.ShadowBias * pixelsPerUnit);
            pointLight.RuntimeViewportPosition = screenPosition;
            pointLight.RuntimeViewportRadius = screenLight.RadiusPixels;
            lights.push_back(screenLight);
        }

        return lights;
    }
}
