#include "Physics/Physics2DWorld.h"

#include "Scene/Scene.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <unordered_set>
#include <vector>
#include <glm/gtc/constants.hpp>

namespace Limitless
{
    namespace
    {
        constexpr float kMinimumColliderExtent = 0.001f;
        constexpr float kMinimumCircleRadius = 0.001f;
        constexpr float kMinimumStepDelta = 0.000001f;
        constexpr float kTransformSnapEpsilon = 0.0001f;

        entt::entity ToEntityHandle(void* userData)
        {
            return static_cast<entt::entity>(reinterpret_cast<uintptr_t>(userData));
        }

        void* ToUserData(entt::entity entity)
        {
            return reinterpret_cast<void*>(static_cast<uintptr_t>(entity));
        }

        b2BodyType ToBox2DBodyType(Rigidbody2DComponent::BodyType type)
        {
            switch (type)
            {
                case Rigidbody2DComponent::BodyType::Static: return b2_staticBody;
                case Rigidbody2DComponent::BodyType::Kinematic: return b2_kinematicBody;
                case Rigidbody2DComponent::BodyType::Dynamic:
                default: return b2_dynamicBody;
            }
        }

        float WrapAngleRadians(float angleRadians)
        {
            while (angleRadians > glm::pi<float>())
                angleRadians -= glm::two_pi<float>();
            while (angleRadians < -glm::pi<float>())
                angleRadians += glm::two_pi<float>();
            return angleRadians;
        }

        uint64_t HashCombine64(uint64_t seed, uint64_t value)
        {
            // 64-bit hash-combine variant suitable for incremental content hashes.
            seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
            return seed;
        }

        uint64_t HashFloat(float value)
        {
            return static_cast<uint64_t>(std::bit_cast<uint32_t>(value));
        }

        bool IsTileSolidForCollider(const TilemapComponent& tilemap,
                                    const TilemapCollider2DComponent& collider,
                                    int32_t cellX,
                                    int32_t cellY)
        {
            if (!IsTilemapCellInBounds(tilemap, cellX, cellY))
                return false;

            const size_t tileIndex = TilemapCellToIndex(tilemap, cellX, cellY);
            for (size_t layerIndex = 0; layerIndex < tilemap.Layers.size(); ++layerIndex)
            {
                const auto& layer = tilemap.Layers[layerIndex];
                if (collider.UseCollisionEnabledLayers)
                {
                    if (!layer.CollisionEnabled)
                        continue;
                }
                else if (static_cast<int32_t>(layerIndex) != collider.LayerIndex)
                {
                    continue;
                }

                if (tileIndex < layer.Tiles.size() && layer.Tiles[tileIndex] != 0u)
                    return true;
            }
            return false;
        }

        uint64_t ComputeTilemapColliderHash(const glm::mat4& worldTransform,
                                            const TilemapComponent& tilemap,
                                            const TilemapCollider2DComponent& collider)
        {
            const glm::vec3 worldPosition = glm::vec3(worldTransform[3]);
            const glm::vec3 basisX = glm::vec3(worldTransform[0]);
            const glm::vec3 basisY = glm::vec3(worldTransform[1]);
            const float worldScaleX = std::max(kMinimumColliderExtent, glm::length(basisX));
            const float worldScaleY = std::max(kMinimumColliderExtent, glm::length(basisY));
            const float worldAngleRadians = std::atan2(basisX.y, basisX.x);

            uint64_t hash = 1469598103934665603ull;
            hash = HashCombine64(hash, static_cast<uint64_t>(collider.Enabled));
            hash = HashCombine64(hash, static_cast<uint64_t>(collider.MergeAdjacentTiles));
            hash = HashCombine64(hash, static_cast<uint64_t>(collider.UseCollisionEnabledLayers));
            hash = HashCombine64(hash, static_cast<uint64_t>(std::max(0, collider.LayerIndex)));
            hash = HashCombine64(hash, HashFloat(worldPosition.x));
            hash = HashCombine64(hash, HashFloat(worldPosition.y));
            hash = HashCombine64(hash, HashFloat(worldScaleX));
            hash = HashCombine64(hash, HashFloat(worldScaleY));
            hash = HashCombine64(hash, HashFloat(worldAngleRadians));
            hash = HashCombine64(hash, static_cast<uint64_t>(std::max(1, tilemap.GridSize.x)));
            hash = HashCombine64(hash, static_cast<uint64_t>(std::max(1, tilemap.GridSize.y)));
            hash = HashCombine64(hash, HashFloat(tilemap.CellSize.x));
            hash = HashCombine64(hash, HashFloat(tilemap.CellSize.y));

            const int32_t width = std::max(1, tilemap.GridSize.x);
            const int32_t height = std::max(1, tilemap.GridSize.y);
            for (int32_t y = 0; y < height; ++y)
            {
                for (int32_t x = 0; x < width; ++x)
                {
                    hash = HashCombine64(hash, static_cast<uint64_t>(IsTileSolidForCollider(tilemap, collider, x, y)));
                }
            }
            return hash;
        }

        struct MergedTileRect
        {
            int32_t X = 0;
            int32_t Y = 0;
            int32_t Width = 1;
            int32_t Height = 1;
        };

        std::vector<MergedTileRect> BuildMergedTileRectangles(const TilemapComponent& tilemap,
                                                              const TilemapCollider2DComponent& collider)
        {
            const int32_t width = std::max(1, tilemap.GridSize.x);
            const int32_t height = std::max(1, tilemap.GridSize.y);
            std::vector<uint8_t> occupied(static_cast<size_t>(width * height), 0u);
            std::vector<uint8_t> visited(static_cast<size_t>(width * height), 0u);
            for (int32_t y = 0; y < height; ++y)
            {
                for (int32_t x = 0; x < width; ++x)
                {
                    if (IsTileSolidForCollider(tilemap, collider, x, y))
                        occupied[static_cast<size_t>(y * width + x)] = 1u;
                }
            }

            std::vector<MergedTileRect> rectangles;
            rectangles.reserve(static_cast<size_t>(width * height) / 2);
            for (int32_t y = 0; y < height; ++y)
            {
                for (int32_t x = 0; x < width; ++x)
                {
                    const size_t startIndex = static_cast<size_t>(y * width + x);
                    if (occupied[startIndex] == 0u || visited[startIndex] != 0u)
                        continue;

                    int32_t rectWidth = 1;
                    while (x + rectWidth < width)
                    {
                        const size_t index = static_cast<size_t>(y * width + (x + rectWidth));
                        if (occupied[index] == 0u || visited[index] != 0u)
                            break;
                        ++rectWidth;
                    }

                    int32_t rectHeight = 1;
                    bool canGrow = true;
                    while (canGrow && y + rectHeight < height)
                    {
                        for (int32_t checkX = 0; checkX < rectWidth; ++checkX)
                        {
                            const size_t index = static_cast<size_t>((y + rectHeight) * width + (x + checkX));
                            if (occupied[index] == 0u || visited[index] != 0u)
                            {
                                canGrow = false;
                                break;
                            }
                        }
                        if (canGrow)
                            ++rectHeight;
                    }

                    for (int32_t fillY = 0; fillY < rectHeight; ++fillY)
                    {
                        for (int32_t fillX = 0; fillX < rectWidth; ++fillX)
                            visited[static_cast<size_t>((y + fillY) * width + (x + fillX))] = 1u;
                    }

                    rectangles.push_back(MergedTileRect{ x, y, rectWidth, rectHeight });
                }
            }
            return rectangles;
        }
    }

    Physics2DWorld::~Physics2DWorld() = default;

    void Physics2DWorld::Initialize(const Physics2DWorldSettings& settings)
    {
#ifdef LT_ENABLE_PHYSICS2D
        m_Settings = settings;

        if (b2World_IsValid(m_WorldId))
            b2DestroyWorld(m_WorldId);

        b2WorldDef worldDefinition = b2DefaultWorldDef();
        worldDefinition.gravity = { m_Settings.Gravity.x, m_Settings.Gravity.y };
        worldDefinition.enableSleep = m_Settings.EnableSleep;
        worldDefinition.enableContinuous = m_Settings.EnableContinuousCollision;
        m_WorldId = b2CreateWorld(&worldDefinition);
        b2World_SetContactTuning(
            m_WorldId,
            std::max(0.0f, m_Settings.ContactHertz),
            std::max(0.0f, m_Settings.ContactDampingRatio),
            std::max(0.0f, m_Settings.ContactPushSpeed));
#else
        (void)settings;
#endif
        m_RuntimeBuilt = false;
        m_ContactListener.Clear();
        m_Diagnostics = Physics2DDiagnostics{};
        m_BodyDiagnostics.clear();
    }

    void Physics2DWorld::Shutdown(Scene& scene)
    {
        DestroyRuntimeState(scene);

#ifdef LT_ENABLE_PHYSICS2D
        if (b2World_IsValid(m_WorldId))
            b2DestroyWorld(m_WorldId);
        m_WorldId = b2_nullWorldId;
#endif
        m_RuntimeBuilt = false;
    }

    bool Physics2DWorld::IsInitialized() const
    {
#ifdef LT_ENABLE_PHYSICS2D
        return b2World_IsValid(m_WorldId);
#else
        return false;
#endif
    }

    void Physics2DWorld::SetSettings(const Physics2DWorldSettings& settings)
    {
        m_Settings = settings;
#ifdef LT_ENABLE_PHYSICS2D
        if (b2World_IsValid(m_WorldId))
        {
            b2World_SetGravity(m_WorldId, { settings.Gravity.x, settings.Gravity.y });
            b2World_EnableSleeping(m_WorldId, settings.EnableSleep);
            b2World_EnableContinuous(m_WorldId, settings.EnableContinuousCollision);
            b2World_SetContactTuning(
                m_WorldId,
                std::max(0.0f, settings.ContactHertz),
                std::max(0.0f, settings.ContactDampingRatio),
                std::max(0.0f, settings.ContactPushSpeed));
        }
#endif
    }

    void Physics2DWorld::RebuildScene(Scene& scene)
    {
        DestroyRuntimeState(scene);
        BuildBodiesAndShapes(scene);
        BuildJoints(scene);
        m_RuntimeBuilt = true;
    }

    void Physics2DWorld::Step(Scene& scene, float fixedDeltaTime)
    {
#ifdef LT_ENABLE_PHYSICS2D
        if (!b2World_IsValid(m_WorldId))
            Initialize(m_Settings);

        if (!m_RuntimeBuilt || RequiresRuntimeRebuild(scene))
            RebuildScene(scene);

        const float step = std::max(fixedDeltaTime, kMinimumStepDelta);
        SyncAuthoringTransformsToBodies(scene, step);

        const int subSteps = ComputeEffectiveSubSteps(scene);
        b2World_Step(m_WorldId, step, subSteps);

        SyncMovedBodiesToTransforms(scene);
        SyncBodyContactCounts(scene);
        CollectContactEvents();
        CollectDiagnostics(scene);
#else
        (void)scene;
        (void)fixedDeltaTime;
#endif
    }

    void Physics2DWorld::DestroyRuntimeState(Scene& scene)
    {
        auto& registry = scene.GetRegistry();

        auto jointView = registry.view<Joint2DComponent>();
        for (entt::entity entity : jointView)
        {
            auto& joint = jointView.get<Joint2DComponent>(entity);
#ifdef LT_ENABLE_PHYSICS2D
            if (joint.RuntimeJointCreated && b2Joint_IsValid(joint.RuntimeJointId))
                b2DestroyJoint(joint.RuntimeJointId);
            joint.RuntimeJointId = b2_nullJointId;
#endif
            joint.RuntimeJointCreated = false;
        }

        auto boxColliderView = registry.view<BoxCollider2DComponent>();
        for (entt::entity entity : boxColliderView)
        {
            auto& collider = boxColliderView.get<BoxCollider2DComponent>(entity);
#ifdef LT_ENABLE_PHYSICS2D
            collider.RuntimeShapeId = b2_nullShapeId;
#endif
            collider.RuntimeShapeCreated = false;
        }

        auto circleColliderView = registry.view<CircleCollider2DComponent>();
        for (entt::entity entity : circleColliderView)
        {
            auto& collider = circleColliderView.get<CircleCollider2DComponent>(entity);
#ifdef LT_ENABLE_PHYSICS2D
            collider.RuntimeShapeId = b2_nullShapeId;
#endif
            collider.RuntimeShapeCreated = false;
        }

        auto tilemapColliderView = registry.view<TilemapCollider2DComponent>();
        for (entt::entity entity : tilemapColliderView)
        {
            auto& tilemapCollider = tilemapColliderView.get<TilemapCollider2DComponent>(entity);
#ifdef LT_ENABLE_PHYSICS2D
            if (tilemapCollider.RuntimeBodyCreated && b2Body_IsValid(tilemapCollider.RuntimeBodyId))
                b2DestroyBody(tilemapCollider.RuntimeBodyId);
            tilemapCollider.RuntimeBodyId = b2_nullBodyId;
            tilemapCollider.RuntimeShapeIds.clear();
#endif
            tilemapCollider.RuntimeBodyCreated = false;
            tilemapCollider.RuntimeBuiltHash = 0ull;
        }

        auto bodyView = registry.view<Rigidbody2DComponent>();
        for (entt::entity entity : bodyView)
        {
            auto& rigidbody = bodyView.get<Rigidbody2DComponent>(entity);
#ifdef LT_ENABLE_PHYSICS2D
            if (rigidbody.RuntimeBodyCreated && b2Body_IsValid(rigidbody.RuntimeBodyId))
                b2DestroyBody(rigidbody.RuntimeBodyId);
            rigidbody.RuntimeBodyId = b2_nullBodyId;
#endif
            rigidbody.RuntimeBodyCreated = false;
            rigidbody.RuntimePreviousPosition = glm::vec2(0.0f);
            rigidbody.RuntimePreviousAngleRadians = 0.0f;
            rigidbody.RuntimeRenderPreviousPosition = glm::vec2(0.0f);
            rigidbody.RuntimeRenderPreviousAngleRadians = 0.0f;
            rigidbody.RuntimeRenderCurrentPosition = glm::vec2(0.0f);
            rigidbody.RuntimeRenderCurrentAngleRadians = 0.0f;
            rigidbody.RuntimeLinearVelocity = glm::vec2(0.0f);
            rigidbody.RuntimePendingLinearVelocity = glm::vec2(0.0f);
            rigidbody.RuntimeHasPendingLinearVelocity = false;
            rigidbody.RuntimePendingLinearVelocityX = 0.0f;
            rigidbody.RuntimeHasPendingLinearVelocityX = false;
            rigidbody.RuntimePendingLinearVelocityY = 0.0f;
            rigidbody.RuntimeHasPendingLinearVelocityY = false;
            rigidbody.RuntimeContactCount = 0;
            rigidbody.RuntimeContactCountExcludingSensors = 0;
            rigidbody.RuntimeContactCount = 0;
            rigidbody.RuntimeContactCountExcludingSensors = 0;
        }
        m_Diagnostics = Physics2DDiagnostics{};
        m_BodyDiagnostics.clear();
    }

    void Physics2DWorld::BuildBodiesAndShapes(Scene& scene)
    {
#ifdef LT_ENABLE_PHYSICS2D
        auto& registry = scene.GetRegistry();
        auto bodyView = registry.view<Rigidbody2DComponent, TransformComponent>();
        for (entt::entity entity : bodyView)
        {
            auto& rigidbody = bodyView.get<Rigidbody2DComponent>(entity);
            auto& transform = bodyView.get<TransformComponent>(entity);

            b2BodyDef bodyDefinition = b2DefaultBodyDef();
            bodyDefinition.type = ToBox2DBodyType(rigidbody.Type);
            bodyDefinition.position = { transform.Position.x, transform.Position.y };
            bodyDefinition.rotation = b2MakeRot(glm::radians(transform.Rotation.z));
            bodyDefinition.linearDamping = rigidbody.LinearDamping;
            bodyDefinition.angularDamping = rigidbody.AngularDamping;
            bodyDefinition.gravityScale = rigidbody.GravityScale;
            bodyDefinition.fixedRotation = rigidbody.IsRotationLocked();
            bodyDefinition.enableSleep = rigidbody.EnableSleep;
            bodyDefinition.isAwake = rigidbody.StartAwake;
            bodyDefinition.isBullet = rigidbody.UseCCD;
            bodyDefinition.userData = ToUserData(entity);

            rigidbody.RuntimeBodyId = b2CreateBody(m_WorldId, &bodyDefinition);
            rigidbody.RuntimeBodyCreated = b2Body_IsValid(rigidbody.RuntimeBodyId);
            rigidbody.RuntimePreviousPosition = glm::vec2(transform.Position.x, transform.Position.y);
            rigidbody.RuntimePreviousAngleRadians = glm::radians(transform.Rotation.z);
            rigidbody.RuntimeRenderPreviousPosition = rigidbody.RuntimePreviousPosition;
            rigidbody.RuntimeRenderPreviousAngleRadians = rigidbody.RuntimePreviousAngleRadians;
            rigidbody.RuntimeRenderCurrentPosition = rigidbody.RuntimePreviousPosition;
            rigidbody.RuntimeRenderCurrentAngleRadians = rigidbody.RuntimePreviousAngleRadians;
            rigidbody.RuntimeLinearVelocity = glm::vec2(0.0f);
            rigidbody.RuntimePendingLinearVelocity = glm::vec2(0.0f);
            rigidbody.RuntimeHasPendingLinearVelocity = false;
            rigidbody.RuntimePendingLinearVelocityX = 0.0f;
            rigidbody.RuntimeHasPendingLinearVelocityX = false;
            rigidbody.RuntimePendingLinearVelocityY = 0.0f;
            rigidbody.RuntimeHasPendingLinearVelocityY = false;

            if (!rigidbody.RuntimeBodyCreated)
                continue;

            if (auto* boxCollider = registry.try_get<BoxCollider2DComponent>(entity))
            {
                b2ShapeDef shapeDefinition = b2DefaultShapeDef();
                shapeDefinition.density = boxCollider->Density;
                shapeDefinition.friction = boxCollider->Friction;
                shapeDefinition.restitution = boxCollider->Restitution;
                shapeDefinition.isSensor = boxCollider->IsSensor;
                shapeDefinition.enableContactEvents = true;
                shapeDefinition.filter.categoryBits = boxCollider->CollisionLayer;
                shapeDefinition.filter.maskBits = boxCollider->CollisionMask;

                const float halfWidth = std::max(kMinimumColliderExtent, boxCollider->Size.x * 0.5f * std::abs(transform.Scale.x));
                const float halfHeight = std::max(kMinimumColliderExtent, boxCollider->Size.y * 0.5f * std::abs(transform.Scale.y));
                b2Polygon boxPolygon = b2MakeOffsetBox(
                    halfWidth,
                    halfHeight,
                    { boxCollider->Offset.x * transform.Scale.x, boxCollider->Offset.y * transform.Scale.y },
                    b2Rot_identity);

                boxCollider->RuntimeShapeId = b2CreatePolygonShape(rigidbody.RuntimeBodyId, &shapeDefinition, &boxPolygon);
                boxCollider->RuntimeShapeCreated = b2Shape_IsValid(boxCollider->RuntimeShapeId);
            }

            if (auto* circleCollider = registry.try_get<CircleCollider2DComponent>(entity))
            {
                b2ShapeDef shapeDefinition = b2DefaultShapeDef();
                shapeDefinition.density = circleCollider->Density;
                shapeDefinition.friction = circleCollider->Friction;
                shapeDefinition.restitution = circleCollider->Restitution;
                shapeDefinition.isSensor = circleCollider->IsSensor;
                shapeDefinition.enableContactEvents = true;
                shapeDefinition.filter.categoryBits = circleCollider->CollisionLayer;
                shapeDefinition.filter.maskBits = circleCollider->CollisionMask;

                const float maxScale = std::max(std::abs(transform.Scale.x), std::abs(transform.Scale.y));
                b2Circle circleShape{};
                circleShape.center = { circleCollider->Offset.x * transform.Scale.x, circleCollider->Offset.y * transform.Scale.y };
                circleShape.radius = std::max(kMinimumCircleRadius, circleCollider->Radius * maxScale);

                circleCollider->RuntimeShapeId = b2CreateCircleShape(rigidbody.RuntimeBodyId, &shapeDefinition, &circleShape);
                circleCollider->RuntimeShapeCreated = b2Shape_IsValid(circleCollider->RuntimeShapeId);
            }
        }

        auto tilemapColliderView = registry.view<TilemapComponent, TilemapCollider2DComponent>();
        for (entt::entity entity : tilemapColliderView)
        {
            auto& tilemap = tilemapColliderView.get<TilemapComponent>(entity);
            auto& tilemapCollider = tilemapColliderView.get<TilemapCollider2DComponent>(entity);
            tilemap.EnsureLayerStorage();

            if (!tilemapCollider.Enabled)
                continue;

            const glm::mat4 worldTransform = scene.GetWorldTransformMatrix(entity);
            const glm::vec3 worldPosition = glm::vec3(worldTransform[3]);
            const glm::vec3 basisX = glm::vec3(worldTransform[0]);
            const glm::vec3 basisY = glm::vec3(worldTransform[1]);
            const float worldScaleX = std::max(kMinimumColliderExtent, glm::length(basisX));
            const float worldScaleY = std::max(kMinimumColliderExtent, glm::length(basisY));
            const float worldAngleRadians = std::atan2(basisX.y, basisX.x);

            b2BodyDef bodyDefinition = b2DefaultBodyDef();
            bodyDefinition.type = b2_staticBody;
            bodyDefinition.position = { worldPosition.x, worldPosition.y };
            bodyDefinition.rotation = b2MakeRot(worldAngleRadians);
            bodyDefinition.userData = ToUserData(entity);
            tilemapCollider.RuntimeBodyId = b2CreateBody(m_WorldId, &bodyDefinition);
            tilemapCollider.RuntimeBodyCreated = b2Body_IsValid(tilemapCollider.RuntimeBodyId);
            if (!tilemapCollider.RuntimeBodyCreated)
                continue;

            b2ShapeDef shapeDefinition = b2DefaultShapeDef();
            shapeDefinition.density = 0.0f;
            shapeDefinition.friction = tilemapCollider.Friction;
            shapeDefinition.restitution = tilemapCollider.Restitution;
            shapeDefinition.isSensor = tilemapCollider.IsSensor;
            shapeDefinition.enableContactEvents = true;
            shapeDefinition.filter.categoryBits = tilemapCollider.CollisionLayer;
            shapeDefinition.filter.maskBits = tilemapCollider.CollisionMask;

            const int32_t gridWidth = std::max(1, tilemap.GridSize.x);
            const int32_t gridHeight = std::max(1, tilemap.GridSize.y);
            const glm::vec2 safeCellSize(std::max(0.001f, tilemap.CellSize.x), std::max(0.001f, tilemap.CellSize.y));
            const glm::vec2 mapCenterOffset = -0.5f * glm::vec2(gridWidth - 1, gridHeight - 1) * safeCellSize;
            const glm::vec2 scaledCellSize = glm::vec2(
                safeCellSize.x * worldScaleX,
                safeCellSize.y * worldScaleY);

#ifdef LT_ENABLE_PHYSICS2D
            tilemapCollider.RuntimeShapeIds.clear();
#endif
            if (tilemapCollider.MergeAdjacentTiles)
            {
                const auto mergedRects = BuildMergedTileRectangles(tilemap, tilemapCollider);
                for (const auto& rect : mergedRects)
                {
                    const float centerX = mapCenterOffset.x + (static_cast<float>(rect.X) + (static_cast<float>(rect.Width) - 1.0f) * 0.5f) * safeCellSize.x;
                    const float centerY = mapCenterOffset.y + (static_cast<float>(rect.Y) + (static_cast<float>(rect.Height) - 1.0f) * 0.5f) * safeCellSize.y;
                    const float halfWidth = std::max(kMinimumColliderExtent, 0.5f * static_cast<float>(rect.Width) * scaledCellSize.x);
                    const float halfHeight = std::max(kMinimumColliderExtent, 0.5f * static_cast<float>(rect.Height) * scaledCellSize.y);
                    b2Polygon boxPolygon = b2MakeOffsetBox(
                        halfWidth,
                        halfHeight,
                        { centerX * worldScaleX, centerY * worldScaleY },
                        b2Rot_identity);
                    const b2ShapeId shapeId = b2CreatePolygonShape(tilemapCollider.RuntimeBodyId, &shapeDefinition, &boxPolygon);
#ifdef LT_ENABLE_PHYSICS2D
                    if (b2Shape_IsValid(shapeId))
                        tilemapCollider.RuntimeShapeIds.push_back(shapeId);
#endif
                }
            }
            else
            {
                for (int32_t cellY = 0; cellY < gridHeight; ++cellY)
                {
                    for (int32_t cellX = 0; cellX < gridWidth; ++cellX)
                    {
                        if (!IsTileSolidForCollider(tilemap, tilemapCollider, cellX, cellY))
                            continue;

                        const float centerX = mapCenterOffset.x + static_cast<float>(cellX) * safeCellSize.x;
                        const float centerY = mapCenterOffset.y + static_cast<float>(cellY) * safeCellSize.y;
                        const float halfWidth = std::max(kMinimumColliderExtent, 0.5f * scaledCellSize.x);
                        const float halfHeight = std::max(kMinimumColliderExtent, 0.5f * scaledCellSize.y);
                        b2Polygon boxPolygon = b2MakeOffsetBox(
                            halfWidth,
                            halfHeight,
                            { centerX * worldScaleX, centerY * worldScaleY },
                            b2Rot_identity);
                        const b2ShapeId shapeId = b2CreatePolygonShape(tilemapCollider.RuntimeBodyId, &shapeDefinition, &boxPolygon);
#ifdef LT_ENABLE_PHYSICS2D
                        if (b2Shape_IsValid(shapeId))
                            tilemapCollider.RuntimeShapeIds.push_back(shapeId);
#endif
                    }
                }
            }

            tilemapCollider.RuntimeBuiltHash = ComputeTilemapColliderHash(worldTransform, tilemap, tilemapCollider);
        }
#else
        (void)scene;
#endif
    }

    void Physics2DWorld::SyncAuthoringTransformsToBodies(Scene& scene, float fixedDeltaTime)
    {
#ifdef LT_ENABLE_PHYSICS2D
        auto& registry = scene.GetRegistry();
        auto bodyView = registry.view<Rigidbody2DComponent, TransformComponent>();
        for (entt::entity entity : bodyView)
        {
            auto& rigidbody = bodyView.get<Rigidbody2DComponent>(entity);
            auto& transform = bodyView.get<TransformComponent>(entity);

            if (!rigidbody.RuntimeBodyCreated || !b2Body_IsValid(rigidbody.RuntimeBodyId))
                continue;

            const b2BodyType expectedType = ToBox2DBodyType(rigidbody.Type);
            if (b2Body_GetType(rigidbody.RuntimeBodyId) != expectedType)
                b2Body_SetType(rigidbody.RuntimeBodyId, expectedType);

            const bool freezePositionX = rigidbody.FreezePositionX;
            const bool freezePositionY = rigidbody.FreezePositionY;
            const bool freezeRotation = rigidbody.IsRotationLocked();
            rigidbody.FixedRotation = freezeRotation;

            b2Body_SetLinearDamping(rigidbody.RuntimeBodyId, rigidbody.LinearDamping);
            b2Body_SetAngularDamping(rigidbody.RuntimeBodyId, rigidbody.AngularDamping);
            b2Body_SetGravityScale(rigidbody.RuntimeBodyId, rigidbody.GravityScale);
            b2Body_SetFixedRotation(rigidbody.RuntimeBodyId, freezeRotation);
            b2Body_EnableSleep(rigidbody.RuntimeBodyId, rigidbody.EnableSleep);
            b2Body_SetBullet(rigidbody.RuntimeBodyId, rigidbody.UseCCD);

            const b2Transform runtimeTransform = b2Body_GetTransform(rigidbody.RuntimeBodyId);
            const glm::vec2 runtimePosition(runtimeTransform.p.x, runtimeTransform.p.y);
            const float runtimeAngleRadians = b2Rot_GetAngle(runtimeTransform.q);
            glm::vec2 authoringPosition(transform.Position.x, transform.Position.y);
            float authoringAngleRadians = glm::radians(transform.Rotation.z);

            // Constraints are authoritative during simulation.
            // If scripts/editor mutate constrained axes directly, snap back to the body.
            if (freezePositionX)
            {
                authoringPosition.x = runtimePosition.x;
                transform.Position.x = runtimePosition.x;
            }
            if (freezePositionY)
            {
                authoringPosition.y = runtimePosition.y;
                transform.Position.y = runtimePosition.y;
            }
            if (freezeRotation)
            {
                authoringAngleRadians = runtimeAngleRadians;
                transform.Rotation.z = glm::degrees(runtimeAngleRadians);
            }

            if (expectedType == b2_kinematicBody)
            {
                // For authored kinematic bodies, drive velocity from authored transform deltas.
                // This avoids solver-feedback jitter when a dynamic body is in contact.
                const glm::vec2 authoredPositionDelta = authoringPosition - rigidbody.RuntimePreviousPosition;
                const float authoredAngleDelta = WrapAngleRadians(authoringAngleRadians - rigidbody.RuntimePreviousAngleRadians);
                const bool authoringChanged = glm::length(authoredPositionDelta) > kTransformSnapEpsilon ||
                                              std::abs(authoredAngleDelta) > kTransformSnapEpsilon;

                if (!authoringChanged)
                {
                    b2Body_SetLinearVelocity(rigidbody.RuntimeBodyId, { 0.0f, 0.0f });
                    b2Body_SetAngularVelocity(rigidbody.RuntimeBodyId, 0.0f);
                    rigidbody.RuntimePreviousPosition = authoringPosition;
                    rigidbody.RuntimePreviousAngleRadians = authoringAngleRadians;
                    continue;
                }

                const float inverseStep = 1.0f / std::max(fixedDeltaTime, kMinimumStepDelta);
                glm::vec2 targetLinearVelocity = authoredPositionDelta * inverseStep;
                float targetAngularVelocity = authoredAngleDelta * inverseStep;
                if (freezePositionX)
                    targetLinearVelocity.x = 0.0f;
                if (freezePositionY)
                    targetLinearVelocity.y = 0.0f;
                if (freezeRotation)
                    targetAngularVelocity = 0.0f;
                b2Body_SetLinearVelocity(rigidbody.RuntimeBodyId, { targetLinearVelocity.x, targetLinearVelocity.y });
                b2Body_SetAngularVelocity(rigidbody.RuntimeBodyId, targetAngularVelocity);

                rigidbody.RuntimePreviousPosition = authoringPosition;
                rigidbody.RuntimePreviousAngleRadians = authoringAngleRadians;
                continue;
            }

            if (expectedType == b2_dynamicBody &&
                (rigidbody.RuntimeHasPendingLinearVelocity ||
                 rigidbody.RuntimeHasPendingLinearVelocityX ||
                 rigidbody.RuntimeHasPendingLinearVelocityY))
            {
                b2Vec2 runtimeVelocity = b2Body_GetLinearVelocity(rigidbody.RuntimeBodyId);
                if (rigidbody.RuntimeHasPendingLinearVelocity)
                {
                    runtimeVelocity.x = rigidbody.RuntimePendingLinearVelocity.x;
                    runtimeVelocity.y = rigidbody.RuntimePendingLinearVelocity.y;
                }
                else
                {
                    if (rigidbody.RuntimeHasPendingLinearVelocityX)
                        runtimeVelocity.x = rigidbody.RuntimePendingLinearVelocityX;
                    if (rigidbody.RuntimeHasPendingLinearVelocityY)
                        runtimeVelocity.y = rigidbody.RuntimePendingLinearVelocityY;
                }
                if (freezePositionX)
                    runtimeVelocity.x = 0.0f;
                if (freezePositionY)
                    runtimeVelocity.y = 0.0f;
                b2Body_SetLinearVelocity(rigidbody.RuntimeBodyId, runtimeVelocity);
                rigidbody.RuntimeLinearVelocity = glm::vec2(runtimeVelocity.x, runtimeVelocity.y);
                rigidbody.RuntimeHasPendingLinearVelocity = false;
                rigidbody.RuntimeHasPendingLinearVelocityX = false;
                rigidbody.RuntimeHasPendingLinearVelocityY = false;
            }

            if ((expectedType == b2_dynamicBody || expectedType == b2_kinematicBody) && (freezePositionX || freezePositionY || freezeRotation))
            {
                b2Vec2 runtimeVelocity = b2Body_GetLinearVelocity(rigidbody.RuntimeBodyId);
                bool velocityChanged = false;
                if (freezePositionX && std::abs(runtimeVelocity.x) > kTransformSnapEpsilon)
                {
                    runtimeVelocity.x = 0.0f;
                    velocityChanged = true;
                }
                if (freezePositionY && std::abs(runtimeVelocity.y) > kTransformSnapEpsilon)
                {
                    runtimeVelocity.y = 0.0f;
                    velocityChanged = true;
                }
                if (velocityChanged)
                    b2Body_SetLinearVelocity(rigidbody.RuntimeBodyId, runtimeVelocity);

                if (freezeRotation)
                    b2Body_SetAngularVelocity(rigidbody.RuntimeBodyId, 0.0f);
            }

            const glm::vec2 positionDelta = authoringPosition - runtimePosition;
            const float angleDelta = WrapAngleRadians(authoringAngleRadians - runtimeAngleRadians);
            const bool transformChangedByAuthoring = glm::length(positionDelta) > kTransformSnapEpsilon ||
                                                     std::abs(angleDelta) > kTransformSnapEpsilon;
            if (transformChangedByAuthoring)
            {
                b2Body_SetTransform(
                    rigidbody.RuntimeBodyId,
                    { authoringPosition.x, authoringPosition.y },
                    b2MakeRot(authoringAngleRadians));
            }
        }

        auto tilemapColliderView = registry.view<TilemapCollider2DComponent>();
        for (entt::entity entity : tilemapColliderView)
        {
            auto& tilemapCollider = tilemapColliderView.get<TilemapCollider2DComponent>(entity);
            if (!tilemapCollider.RuntimeBodyCreated || !b2Body_IsValid(tilemapCollider.RuntimeBodyId))
                continue;

            const glm::mat4 worldTransform = scene.GetWorldTransformMatrix(entity);
            const glm::vec3 worldPosition = glm::vec3(worldTransform[3]);
            const glm::vec3 basisX = glm::vec3(worldTransform[0]);
            const float worldAngleRadians = std::atan2(basisX.y, basisX.x);

            b2Body_SetTransform(
                tilemapCollider.RuntimeBodyId,
                { worldPosition.x, worldPosition.y },
                b2MakeRot(worldAngleRadians));
        }
#else
        (void)scene;
        (void)fixedDeltaTime;
#endif
    }

    bool Physics2DWorld::RequiresRuntimeRebuild(Scene& scene) const
    {
#ifdef LT_ENABLE_PHYSICS2D
        auto& registry = scene.GetRegistry();

        auto rigidbodyView = registry.view<Rigidbody2DComponent>();
        for (entt::entity entity : rigidbodyView)
        {
            const auto& rigidbody = rigidbodyView.get<Rigidbody2DComponent>(entity);
            if (!rigidbody.RuntimeBodyCreated || !b2Body_IsValid(rigidbody.RuntimeBodyId))
                return true;
        }

        auto boxColliderView = registry.view<BoxCollider2DComponent, Rigidbody2DComponent>();
        for (entt::entity entity : boxColliderView)
        {
            const auto& boxCollider = boxColliderView.get<BoxCollider2DComponent>(entity);
            if (!boxCollider.RuntimeShapeCreated || !b2Shape_IsValid(boxCollider.RuntimeShapeId))
                return true;
        }

        auto circleColliderView = registry.view<CircleCollider2DComponent, Rigidbody2DComponent>();
        for (entt::entity entity : circleColliderView)
        {
            const auto& circleCollider = circleColliderView.get<CircleCollider2DComponent>(entity);
            if (!circleCollider.RuntimeShapeCreated || !b2Shape_IsValid(circleCollider.RuntimeShapeId))
                return true;
        }

        auto jointView = registry.view<Joint2DComponent, Rigidbody2DComponent>();
        for (entt::entity entity : jointView)
        {
            const auto& joint = jointView.get<Joint2DComponent>(entity);
            if (joint.ConnectedEntity == entt::null)
                continue;
            if (!joint.RuntimeJointCreated || !b2Joint_IsValid(joint.RuntimeJointId))
                return true;
        }

        auto tilemapColliderView = registry.view<TilemapComponent, TilemapCollider2DComponent>();
        for (entt::entity entity : tilemapColliderView)
        {
            const auto& tilemap = tilemapColliderView.get<TilemapComponent>(entity);
            const auto& tilemapCollider = tilemapColliderView.get<TilemapCollider2DComponent>(entity);
            if (!tilemapCollider.Enabled)
                continue;

            if (!tilemapCollider.RuntimeBodyCreated || !b2Body_IsValid(tilemapCollider.RuntimeBodyId))
                return true;

            const glm::mat4 worldTransform = scene.GetWorldTransformMatrix(entity);
            const uint64_t currentHash = ComputeTilemapColliderHash(worldTransform, tilemap, tilemapCollider);
            if (tilemapCollider.RuntimeBuiltHash != currentHash)
                return true;

#ifdef LT_ENABLE_PHYSICS2D
            for (const b2ShapeId shapeId : tilemapCollider.RuntimeShapeIds)
            {
                if (!b2Shape_IsValid(shapeId))
                    return true;
            }
#endif
        }
#else
        (void)scene;
#endif
        return false;
    }

    void Physics2DWorld::BuildJoints(Scene& scene)
    {
#ifdef LT_ENABLE_PHYSICS2D
        auto& registry = scene.GetRegistry();
        auto jointView = registry.view<Joint2DComponent, Rigidbody2DComponent>();
        for (entt::entity entity : jointView)
        {
            auto& joint = jointView.get<Joint2DComponent>(entity);
            auto& bodyAComponent = jointView.get<Rigidbody2DComponent>(entity);
            if (!bodyAComponent.RuntimeBodyCreated || !b2Body_IsValid(bodyAComponent.RuntimeBodyId))
                continue;

            if (joint.ConnectedEntity == entt::null || !scene.IsValid(joint.ConnectedEntity))
                continue;

            auto* bodyBComponent = registry.try_get<Rigidbody2DComponent>(joint.ConnectedEntity);
            if (!bodyBComponent || !bodyBComponent->RuntimeBodyCreated || !b2Body_IsValid(bodyBComponent->RuntimeBodyId))
                continue;

            if (joint.Type == Joint2DComponent::JointType::Distance)
            {
                b2DistanceJointDef definition = b2DefaultDistanceJointDef();
                definition.bodyIdA = bodyAComponent.RuntimeBodyId;
                definition.bodyIdB = bodyBComponent->RuntimeBodyId;
                definition.localAnchorA = { joint.AnchorA.x, joint.AnchorA.y };
                definition.localAnchorB = { joint.AnchorB.x, joint.AnchorB.y };
                definition.collideConnected = joint.CollideConnected;
                definition.enableLimit = joint.EnableLimit;
                definition.minLength = std::min(joint.Limits.x, joint.Limits.y);
                definition.maxLength = std::max(joint.Limits.x, joint.Limits.y);
                definition.enableMotor = joint.EnableMotor;
                definition.motorSpeed = joint.MotorSpeed;
                definition.maxMotorForce = std::max(0.0f, joint.MaxMotorForceOrTorque);
                definition.enableSpring = joint.EnableSpring;
                definition.hertz = std::max(0.0f, joint.Hertz);
                definition.dampingRatio = std::max(0.0f, joint.DampingRatio);
                joint.RuntimeJointId = b2CreateDistanceJoint(m_WorldId, &definition);
            }
            else if (joint.Type == Joint2DComponent::JointType::Revolute)
            {
                b2RevoluteJointDef definition = b2DefaultRevoluteJointDef();
                definition.bodyIdA = bodyAComponent.RuntimeBodyId;
                definition.bodyIdB = bodyBComponent->RuntimeBodyId;
                definition.localAnchorA = { joint.AnchorA.x, joint.AnchorA.y };
                definition.localAnchorB = { joint.AnchorB.x, joint.AnchorB.y };
                definition.collideConnected = joint.CollideConnected;
                definition.enableLimit = joint.EnableLimit;
                definition.lowerAngle = glm::radians(std::min(joint.Limits.x, joint.Limits.y));
                definition.upperAngle = glm::radians(std::max(joint.Limits.x, joint.Limits.y));
                definition.enableMotor = joint.EnableMotor;
                definition.motorSpeed = glm::radians(joint.MotorSpeed);
                definition.maxMotorTorque = std::max(0.0f, joint.MaxMotorForceOrTorque);
                definition.enableSpring = joint.EnableSpring;
                definition.hertz = std::max(0.0f, joint.Hertz);
                definition.dampingRatio = std::max(0.0f, joint.DampingRatio);
                joint.RuntimeJointId = b2CreateRevoluteJoint(m_WorldId, &definition);
            }
            else
            {
                b2PrismaticJointDef definition = b2DefaultPrismaticJointDef();
                definition.bodyIdA = bodyAComponent.RuntimeBodyId;
                definition.bodyIdB = bodyBComponent->RuntimeBodyId;
                definition.localAnchorA = { joint.AnchorA.x, joint.AnchorA.y };
                definition.localAnchorB = { joint.AnchorB.x, joint.AnchorB.y };
                definition.localAxisA = { joint.Axis.x, joint.Axis.y };
                definition.collideConnected = joint.CollideConnected;
                definition.enableLimit = joint.EnableLimit;
                definition.lowerTranslation = std::min(joint.Limits.x, joint.Limits.y);
                definition.upperTranslation = std::max(joint.Limits.x, joint.Limits.y);
                definition.enableMotor = joint.EnableMotor;
                definition.motorSpeed = joint.MotorSpeed;
                definition.maxMotorForce = std::max(0.0f, joint.MaxMotorForceOrTorque);
                definition.enableSpring = joint.EnableSpring;
                definition.hertz = std::max(0.0f, joint.Hertz);
                definition.dampingRatio = std::max(0.0f, joint.DampingRatio);
                joint.RuntimeJointId = b2CreatePrismaticJoint(m_WorldId, &definition);
            }

            joint.RuntimeJointCreated = b2Joint_IsValid(joint.RuntimeJointId);
        }
#else
        (void)scene;
#endif
    }

    void Physics2DWorld::SyncMovedBodiesToTransforms(Scene& scene)
    {
#ifdef LT_ENABLE_PHYSICS2D
        auto& registry = scene.GetRegistry();
        b2BodyEvents bodyEvents = b2World_GetBodyEvents(m_WorldId);
        for (int eventIndex = 0; eventIndex < bodyEvents.moveCount; ++eventIndex)
        {
            const b2BodyMoveEvent& moveEvent = bodyEvents.moveEvents[eventIndex];
            const entt::entity entity = ToEntityHandle(moveEvent.userData);
            if (!scene.IsValid(entity))
                continue;

            auto* transform = registry.try_get<TransformComponent>(entity);
            auto* rigidbody = registry.try_get<Rigidbody2DComponent>(entity);
            if (!transform || !rigidbody)
                continue;

            const bool freezePositionX = rigidbody->FreezePositionX;
            const bool freezePositionY = rigidbody->FreezePositionY;
            const bool freezeRotation = rigidbody->IsRotationLocked();
            rigidbody->FixedRotation = freezeRotation;

            glm::vec2 bodyPosition(moveEvent.transform.p.x, moveEvent.transform.p.y);
            float bodyAngleRadians = b2Rot_GetAngle(moveEvent.transform.q);
            b2Vec2 bodyVelocity = b2Body_GetLinearVelocity(rigidbody->RuntimeBodyId);

            if (freezePositionX || freezePositionY || freezeRotation)
            {
                glm::vec2 constrainedPosition = bodyPosition;
                float constrainedAngleRadians = bodyAngleRadians;
                if (freezePositionX)
                    constrainedPosition.x = transform->Position.x;
                if (freezePositionY)
                    constrainedPosition.y = transform->Position.y;
                if (freezeRotation)
                    constrainedAngleRadians = glm::radians(transform->Rotation.z);

                if (glm::distance(constrainedPosition, bodyPosition) > kTransformSnapEpsilon ||
                    std::abs(WrapAngleRadians(constrainedAngleRadians - bodyAngleRadians)) > kTransformSnapEpsilon)
                {
                    b2Body_SetTransform(
                        rigidbody->RuntimeBodyId,
                        { constrainedPosition.x, constrainedPosition.y },
                        b2MakeRot(constrainedAngleRadians));
                    bodyPosition = constrainedPosition;
                    bodyAngleRadians = constrainedAngleRadians;
                }

                if (freezePositionX)
                    bodyVelocity.x = 0.0f;
                if (freezePositionY)
                    bodyVelocity.y = 0.0f;
                b2Body_SetLinearVelocity(rigidbody->RuntimeBodyId, bodyVelocity);
                if (freezeRotation)
                    b2Body_SetAngularVelocity(rigidbody->RuntimeBodyId, 0.0f);
            }

            rigidbody->RuntimeRenderPreviousPosition = rigidbody->RuntimeRenderCurrentPosition;
            rigidbody->RuntimeRenderPreviousAngleRadians = rigidbody->RuntimeRenderCurrentAngleRadians;
            rigidbody->RuntimeRenderCurrentPosition = bodyPosition;
            rigidbody->RuntimeRenderCurrentAngleRadians = bodyAngleRadians;
            rigidbody->RuntimeLinearVelocity = glm::vec2(bodyVelocity.x, bodyVelocity.y);

            if (rigidbody->Type == Rigidbody2DComponent::BodyType::Kinematic)
                continue;

            rigidbody->RuntimePreviousPosition = glm::vec2(transform->Position.x, transform->Position.y);
            rigidbody->RuntimePreviousAngleRadians = glm::radians(transform->Rotation.z);

            transform->Position.x = bodyPosition.x;
            transform->Position.y = bodyPosition.y;
            transform->Rotation.z = glm::degrees(bodyAngleRadians);
        }
#else
        (void)scene;
#endif
    }

    void Physics2DWorld::SyncBodyContactCounts(Scene& scene)
    {
#ifdef LT_ENABLE_PHYSICS2D
        auto& registry = scene.GetRegistry();
        auto bodyView = registry.view<Rigidbody2DComponent>();
        std::vector<b2ContactData> contactBuffer;
        for (entt::entity entity : bodyView)
        {
            auto& rigidbody = bodyView.get<Rigidbody2DComponent>(entity);
            rigidbody.RuntimeContactCount = 0;
            rigidbody.RuntimeContactCountExcludingSensors = 0;

            if (!rigidbody.RuntimeBodyCreated || !b2Body_IsValid(rigidbody.RuntimeBodyId))
                continue;

            const int contactCapacity = std::max(0, b2Body_GetContactCapacity(rigidbody.RuntimeBodyId));
            if (contactCapacity <= 0)
                continue;

            contactBuffer.resize(static_cast<size_t>(contactCapacity));
            const int contactCount = b2Body_GetContactData(rigidbody.RuntimeBodyId, contactBuffer.data(), contactCapacity);
            rigidbody.RuntimeContactCount = std::max(0, contactCount);

            int nonSensorContactCount = 0;
            for (int contactIndex = 0; contactIndex < contactCount; ++contactIndex)
            {
                const b2ContactData& contact = contactBuffer[static_cast<size_t>(contactIndex)];
                const bool isSensorContact = b2Shape_IsSensor(contact.shapeIdA) || b2Shape_IsSensor(contact.shapeIdB);
                if (!isSensorContact)
                    ++nonSensorContactCount;
            }
            rigidbody.RuntimeContactCountExcludingSensors = nonSensorContactCount;
        }
#else
        (void)scene;
#endif
    }

    void Physics2DWorld::CollectContactEvents()
    {
#ifdef LT_ENABLE_PHYSICS2D
        m_ContactListener.Clear();

        const b2ContactEvents contactEvents = b2World_GetContactEvents(m_WorldId);
        for (int eventIndex = 0; eventIndex < contactEvents.beginCount; ++eventIndex)
        {
            const auto& eventData = contactEvents.beginEvents[eventIndex];
            if (!b2Shape_IsValid(eventData.shapeIdA) || !b2Shape_IsValid(eventData.shapeIdB))
                continue;

            const b2BodyId bodyA = b2Shape_GetBody(eventData.shapeIdA);
            const b2BodyId bodyB = b2Shape_GetBody(eventData.shapeIdB);
            if (!b2Body_IsValid(bodyA) || !b2Body_IsValid(bodyB))
                continue;

            const entt::entity entityA = ToEntityHandle(b2Body_GetUserData(bodyA));
            const entt::entity entityB = ToEntityHandle(b2Body_GetUserData(bodyB));
            const bool isSensor = b2Shape_IsSensor(eventData.shapeIdA) || b2Shape_IsSensor(eventData.shapeIdB);
            m_ContactListener.PushBegin(entityA, entityB, isSensor);
        }

        for (int eventIndex = 0; eventIndex < contactEvents.endCount; ++eventIndex)
        {
            const auto& eventData = contactEvents.endEvents[eventIndex];
            if (!b2Shape_IsValid(eventData.shapeIdA) || !b2Shape_IsValid(eventData.shapeIdB))
                continue;

            const b2BodyId bodyA = b2Shape_GetBody(eventData.shapeIdA);
            const b2BodyId bodyB = b2Shape_GetBody(eventData.shapeIdB);
            if (!b2Body_IsValid(bodyA) || !b2Body_IsValid(bodyB))
                continue;

            const entt::entity entityA = ToEntityHandle(b2Body_GetUserData(bodyA));
            const entt::entity entityB = ToEntityHandle(b2Body_GetUserData(bodyB));
            const bool isSensor = b2Shape_IsSensor(eventData.shapeIdA) || b2Shape_IsSensor(eventData.shapeIdB);
            m_ContactListener.PushEnd(entityA, entityB, isSensor);
        }
#endif
    }

    int Physics2DWorld::ComputeEffectiveSubSteps(Scene& scene) const
    {
        int effectiveSubSteps = std::max(1, m_Settings.VelocitySubSteps);
        if (m_Settings.HighContactQualityMode)
            effectiveSubSteps += std::max(0, m_Settings.HighContactQualityExtraSubSteps);

        auto& registry = scene.GetRegistry();
        auto bodyView = registry.view<Rigidbody2DComponent>();
        int maxBodyExtraSubSteps = 0;
        for (entt::entity entity : bodyView)
        {
            const auto& rigidbody = bodyView.get<Rigidbody2DComponent>(entity);
            if (!rigidbody.HighContactQuality)
                continue;
            maxBodyExtraSubSteps = std::max(maxBodyExtraSubSteps, std::max(0, rigidbody.ExtraSolverSubSteps));
        }

        // Box2D sub-steps are world-wide, so we apply the strongest requested body override.
        effectiveSubSteps += maxBodyExtraSubSteps;
        return std::max(1, effectiveSubSteps);
    }

    void Physics2DWorld::CollectDiagnostics(Scene& scene)
    {
        m_Diagnostics = Physics2DDiagnostics{};
        m_BodyDiagnostics.clear();
#ifdef LT_ENABLE_PHYSICS2D
        if (!b2World_IsValid(m_WorldId))
            return;

        auto& registry = scene.GetRegistry();
        auto bodyView = registry.view<Rigidbody2DComponent>();
        for (entt::entity entity : bodyView)
        {
            const auto& rigidbody = bodyView.get<Rigidbody2DComponent>(entity);
            if (!rigidbody.RuntimeBodyCreated || !b2Body_IsValid(rigidbody.RuntimeBodyId))
                continue;

            ++m_Diagnostics.BodyCount;
            auto& bodyDiagnostics = m_BodyDiagnostics[entity];
            bodyDiagnostics.IsValid = true;
            if (b2Body_IsAwake(rigidbody.RuntimeBodyId))
            {
                ++m_Diagnostics.AwakeBodyCount;
                bodyDiagnostics.IsAwake = true;
            }
            else
            {
                ++m_Diagnostics.SleepingBodyCount;
                bodyDiagnostics.IsAwake = false;
            }
        }

        struct ShapePairKey
        {
            uint64_t ShapeA = 0;
            uint64_t ShapeB = 0;

            bool operator==(const ShapePairKey& other) const
            {
                return ShapeA == other.ShapeA && ShapeB == other.ShapeB;
            }
        };

        struct ShapePairKeyHash
        {
            size_t operator()(const ShapePairKey& key) const
            {
                const uint64_t mixed = key.ShapeA ^ (key.ShapeB + 0x9e3779b97f4a7c15ull + (key.ShapeA << 6) + (key.ShapeA >> 2));
                return static_cast<size_t>(mixed);
            }
        };

        std::unordered_set<ShapePairKey, ShapePairKeyHash> uniqueContactPairs;
        std::vector<b2ContactData> contactBuffer;
        for (entt::entity entity : bodyView)
        {
            const auto& rigidbody = bodyView.get<Rigidbody2DComponent>(entity);
            if (!rigidbody.RuntimeBodyCreated || !b2Body_IsValid(rigidbody.RuntimeBodyId))
                continue;

            const int contactCapacity = std::max(0, b2Body_GetContactCapacity(rigidbody.RuntimeBodyId));
            if (contactCapacity <= 0)
                continue;

            contactBuffer.resize(static_cast<size_t>(contactCapacity));
            const int contactCount = b2Body_GetContactData(rigidbody.RuntimeBodyId, contactBuffer.data(), contactCapacity);
            auto bodyDiagnosticsIt = m_BodyDiagnostics.find(entity);
            if (bodyDiagnosticsIt != m_BodyDiagnostics.end())
                bodyDiagnosticsIt->second.ContactPairCount = std::max(0, contactCount);
            for (int contactIndex = 0; contactIndex < contactCount; ++contactIndex)
            {
                const b2ContactData& contact = contactBuffer[static_cast<size_t>(contactIndex)];

                const uint64_t shapeAId = b2StoreShapeId(contact.shapeIdA);
                const uint64_t shapeBId = b2StoreShapeId(contact.shapeIdB);
                ShapePairKey pairKey{};
                if (shapeAId < shapeBId)
                {
                    pairKey.ShapeA = shapeAId;
                    pairKey.ShapeB = shapeBId;
                }
                else
                {
                    pairKey.ShapeA = shapeBId;
                    pairKey.ShapeB = shapeAId;
                }

                const bool firstTimeSeen = uniqueContactPairs.insert(pairKey).second;
                if (firstTimeSeen)
                    ++m_Diagnostics.ContactPairCount;

                for (int pointIndex = 0; pointIndex < contact.manifold.pointCount; ++pointIndex)
                {
                    const float separation = contact.manifold.points[pointIndex].separation;
                    if (separation < 0.0f)
                    {
                        if (firstTimeSeen)
                        {
                            ++m_Diagnostics.PenetratingContactPointCount;
                            m_Diagnostics.MaxPenetrationDepth = std::max(m_Diagnostics.MaxPenetrationDepth, -separation);
                        }
                        if (bodyDiagnosticsIt != m_BodyDiagnostics.end())
                        {
                            ++bodyDiagnosticsIt->second.PenetratingContactPointCount;
                            bodyDiagnosticsIt->second.MaxPenetrationDepth =
                                std::max(bodyDiagnosticsIt->second.MaxPenetrationDepth, -separation);
                        }
                    }
                }
            }
        }
#else
        (void)scene;
#endif
    }

    bool Physics2DWorld::TryGetBodyDiagnostics(entt::entity entity, Physics2DBodyDiagnostics& outDiagnostics) const
    {
        outDiagnostics = Physics2DBodyDiagnostics{};
        const auto diagnosticsIt = m_BodyDiagnostics.find(entity);
        if (diagnosticsIt == m_BodyDiagnostics.end())
            return false;

        outDiagnostics = diagnosticsIt->second;
        return outDiagnostics.IsValid;
    }

    Physics2DRaycastHit Physics2DWorld::RaycastClosest(const glm::vec2& origin, const glm::vec2& direction, float maxDistance, uint64_t collisionMask) const
    {
        Physics2DRaycastHit result{};
#ifdef LT_ENABLE_PHYSICS2D
        if (!b2World_IsValid(m_WorldId))
            return result;

        const float castDistance = std::max(0.0f, maxDistance);
        if (castDistance <= 0.0f)
            return result;

        const glm::vec2 safeDirection = glm::length(direction) > 0.00001f
            ? glm::normalize(direction)
            : glm::vec2(1.0f, 0.0f);

        b2QueryFilter filter = b2DefaultQueryFilter();
        filter.categoryBits = ~0ull;
        filter.maskBits = collisionMask;

        const b2RayResult hitResult = b2World_CastRayClosest(
            m_WorldId,
            { origin.x, origin.y },
            { safeDirection.x * castDistance, safeDirection.y * castDistance },
            filter);

        if (!hitResult.hit || !b2Shape_IsValid(hitResult.shapeId))
            return result;

        const b2BodyId bodyId = b2Shape_GetBody(hitResult.shapeId);
        if (!b2Body_IsValid(bodyId))
            return result;

        result.HasHit = true;
        result.Entity = ToEntityHandle(b2Body_GetUserData(bodyId));
        result.Point = glm::vec2(hitResult.point.x, hitResult.point.y);
        result.Normal = glm::vec2(hitResult.normal.x, hitResult.normal.y);
        result.Fraction = hitResult.fraction;
#else
        (void)origin;
        (void)direction;
        (void)maxDistance;
        (void)collisionMask;
#endif
        return result;
    }
}
