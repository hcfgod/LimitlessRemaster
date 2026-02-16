#include "Physics/Physics2DWorld.h"

#include "Core/Debug/Log.h"
#include "Scene/Scene.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <unordered_set>
#include <vector>
#include <glm/gtc/constants.hpp>

// Verify at compile time that our handle typedefs match Box2D's actual types.
// If Box2D ever changes its ID layout, these asserts will catch it immediately.
#ifdef LT_ENABLE_PHYSICS2D
static_assert(sizeof(Limitless::Physics2DBodyHandle)  == sizeof(b2BodyId),  "Physics2DBodyHandle size mismatch with b2BodyId");
static_assert(sizeof(Limitless::Physics2DShapeHandle) == sizeof(b2ShapeId), "Physics2DShapeHandle size mismatch with b2ShapeId");
static_assert(sizeof(Limitless::Physics2DJointHandle) == sizeof(b2JointId), "Physics2DJointHandle size mismatch with b2JointId");
static_assert(alignof(Limitless::Physics2DBodyHandle)  == alignof(b2BodyId),  "Physics2DBodyHandle alignment mismatch");
static_assert(alignof(Limitless::Physics2DShapeHandle) == alignof(b2ShapeId), "Physics2DShapeHandle alignment mismatch");
static_assert(alignof(Limitless::Physics2DJointHandle) == alignof(b2JointId), "Physics2DJointHandle alignment mismatch");
#endif

namespace Limitless
{
    namespace
    {
        constexpr float kMinimumColliderExtent = 0.001f;
        constexpr float kMinimumCircleRadius = 0.001f;
        constexpr float kMinimumDynamicShapeDensity = 0.0001f;
        constexpr float kMaximumShapeDensity = 100.0f;
        // Keep authored values in a conservative range to avoid Box2D broadphase overflow/asserts.
        constexpr float kMaximumWorldPosition = 10000.0f;
        constexpr float kMaximumColliderExtent = 1000.0f;
        constexpr float kMaximumColliderOffset = 1000.0f;
        constexpr float kMinimumStepDelta = 0.000001f;
        constexpr float kTransformSnapEpsilon = 0.0001f;

        // Maximum number of new physics bodies to create per Step() call.
        // Prevents Box2D allocator pressure when scripts instantiate many
        // entities at once. Remaining entities are deferred to subsequent frames.
        constexpr int kMaxNewBodiesPerStep = 256;

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

        float SanitizeFiniteNonNegative(float value, float fallbackValue)
        {
            if (!std::isfinite(value) || value < 0.0f)
                return fallbackValue;
            return value;
        }

        float SanitizeFinite(float value, float fallbackValue)
        {
            if (!std::isfinite(value))
                return fallbackValue;
            return value;
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
        // Invalidate cached substeps when settings change.
        if (m_Settings.VelocitySubSteps != settings.VelocitySubSteps ||
            m_Settings.HighContactQualityMode != settings.HighContactQualityMode ||
            m_Settings.HighContactQualityExtraSubSteps != settings.HighContactQualityExtraSubSteps)
        {
            m_SubStepsCacheDirty = true;
        }

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

        if (!m_RuntimeBuilt)
        {
            // First time: full destroy-and-rebuild to ensure a clean slate.
            RebuildScene(scene);
            m_SubStepsCacheDirty = true;
        }
        else
        {
            // Incremental path: only create bodies/shapes/joints for entities
            // that don't have them yet. BuildBodiesAndShapes skips already-built
            // entities with a cheap bool check, so calling it every step is
            // lightweight when no new entities exist.
            const int created = BuildBodiesAndShapes(scene);
            if (created > 0)
            {
                BuildJoints(scene);
                m_SubStepsCacheDirty = true;
            }
        }

        const float step = std::max(fixedDeltaTime, kMinimumStepDelta);
        SyncAuthoringTransformsToBodies(scene, step);

        // Use cached substep count to avoid iterating all bodies every step.
        if (m_SubStepsCacheDirty)
        {
            m_CachedEffectiveSubSteps = ComputeEffectiveSubSteps(scene);
            m_SubStepsCacheDirty = false;
        }
        b2World_Step(m_WorldId, step, m_CachedEffectiveSubSteps);

        SyncMovedBodiesToTransforms(scene);
        SyncBodyContactCounts(scene);
        CollectContactEvents();

        // Only collect expensive per-body diagnostics when the diagnostics
        // panel is actually visible. This avoids O(N*C) contact queries
        // per step when nobody is observing them.
        if (m_DiagnosticsEnabled)
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
        for (auto [entity, joint] : jointView.each())
        {
            (void)entity;
#ifdef LT_ENABLE_PHYSICS2D
            if (joint.RuntimeJointCreated && b2Joint_IsValid(joint.RuntimeJointId))
                b2DestroyJoint(joint.RuntimeJointId);
            joint.RuntimeJointId = kNullPhysics2DJoint;
#endif
            joint.RuntimeJointCreated = false;
        }

        auto boxColliderView = registry.view<BoxCollider2DComponent>();
        for (auto [entity, collider] : boxColliderView.each())
        {
            (void)entity;
#ifdef LT_ENABLE_PHYSICS2D
            collider.RuntimeShapeId = kNullPhysics2DShape;
#endif
            collider.RuntimeShapeCreated = false;
        }

        auto circleColliderView = registry.view<CircleCollider2DComponent>();
        for (auto [entity, collider] : circleColliderView.each())
        {
            (void)entity;
#ifdef LT_ENABLE_PHYSICS2D
            collider.RuntimeShapeId = kNullPhysics2DShape;
#endif
            collider.RuntimeShapeCreated = false;
        }

        auto tilemapColliderView = registry.view<TilemapCollider2DComponent>();
        for (auto [entity, tilemapCollider] : tilemapColliderView.each())
        {
            (void)entity;
#ifdef LT_ENABLE_PHYSICS2D
            if (tilemapCollider.RuntimeBodyCreated && b2Body_IsValid(tilemapCollider.RuntimeBodyId))
                b2DestroyBody(tilemapCollider.RuntimeBodyId);
            tilemapCollider.RuntimeBodyId = kNullPhysics2DBody;
            tilemapCollider.RuntimeShapeIds.clear();
#endif
            tilemapCollider.RuntimeBodyCreated = false;
            tilemapCollider.RuntimeBuiltHash = 0ull;
        }

        auto bodyView = registry.view<Rigidbody2DComponent>();
        for (auto [entity, rigidbody] : bodyView.each())
        {
            (void)entity;
#ifdef LT_ENABLE_PHYSICS2D
            if (rigidbody.RuntimeBodyCreated && b2Body_IsValid(rigidbody.RuntimeBodyId))
                b2DestroyBody(rigidbody.RuntimeBodyId);
            rigidbody.RuntimeBodyId = kNullPhysics2DBody;
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
        }
        m_Diagnostics = Physics2DDiagnostics{};
        m_BodyDiagnostics.clear();
        m_SubStepsCacheDirty = true;
    }

    int Physics2DWorld::BuildBodiesAndShapes(Scene& scene)
    {
        int newBodiesCreatedThisStep = 0;

#ifdef LT_ENABLE_PHYSICS2D
        auto& registry = scene.GetRegistry();

        auto bodyView = registry.view<Rigidbody2DComponent, TransformComponent>();
        for (entt::entity entity : bodyView)
        {
            auto& rigidbody = bodyView.get<Rigidbody2DComponent>(entity);

            // Incremental guard: skip entities that already have a valid runtime body.
            if (rigidbody.RuntimeBodyCreated && b2Body_IsValid(rigidbody.RuntimeBodyId))
                continue;

            // Engine-side batching: cap the number of new bodies per step to
            // prevent overwhelming Box2D's allocator. Deferred entities will
            // be created on subsequent physics steps automatically.
            if (newBodiesCreatedThisStep >= kMaxNewBodiesPerStep)
                break;

            auto& transform = bodyView.get<TransformComponent>(entity);

            const float safePositionX = glm::clamp(SanitizeFinite(transform.Position.x, 0.0f), -kMaximumWorldPosition, kMaximumWorldPosition);
            const float safePositionY = glm::clamp(SanitizeFinite(transform.Position.y, 0.0f), -kMaximumWorldPosition, kMaximumWorldPosition);
            const float safeRotationDegrees = SanitizeFinite(transform.Rotation.z, 0.0f);
            const float safeLinearDamping = SanitizeFiniteNonNegative(rigidbody.LinearDamping, 0.0f);
            const float safeAngularDamping = SanitizeFiniteNonNegative(rigidbody.AngularDamping, 0.01f);
            const float safeGravityScale = SanitizeFinite(rigidbody.GravityScale, 1.0f);

            const bool hadInvalidBodyParameters =
                (safePositionX != transform.Position.x) ||
                (safePositionY != transform.Position.y) ||
                (safeRotationDegrees != transform.Rotation.z) ||
                (safeLinearDamping != rigidbody.LinearDamping) ||
                (safeAngularDamping != rigidbody.AngularDamping) ||
                (safeGravityScale != rigidbody.GravityScale);

            if (hadInvalidBodyParameters)
            {
                if (!rigidbody.RuntimeWarnedInvalidBodyParameters)
                {
                    const auto* tag = registry.try_get<TagComponent>(entity);
                    LT_WARN("Physics2D: sanitized invalid body parameters for entity '{}' (linearDamping={}, angularDamping={}, gravityScale={}).",
                            tag ? tag->Tag : "Entity",
                            rigidbody.LinearDamping,
                            rigidbody.AngularDamping,
                            rigidbody.GravityScale);
                    rigidbody.RuntimeWarnedInvalidBodyParameters = true;
                }
            }
            else
            {
                rigidbody.RuntimeWarnedInvalidBodyParameters = false;
            }

            b2BodyDef bodyDefinition = b2DefaultBodyDef();
            bodyDefinition.type = ToBox2DBodyType(rigidbody.Type);
            bodyDefinition.position = { safePositionX, safePositionY };
            bodyDefinition.rotation = b2MakeRot(glm::radians(safeRotationDegrees));
            bodyDefinition.linearDamping = safeLinearDamping;
            bodyDefinition.angularDamping = safeAngularDamping;
            bodyDefinition.gravityScale = safeGravityScale;
            bodyDefinition.fixedRotation = rigidbody.IsRotationLocked();
            bodyDefinition.enableSleep = rigidbody.EnableSleep;
            bodyDefinition.isAwake = rigidbody.StartAwake;
            bodyDefinition.isBullet = rigidbody.UseCCD;
            bodyDefinition.userData = ToUserData(entity);

            rigidbody.RuntimeBodyId = b2CreateBody(m_WorldId, &bodyDefinition);
            rigidbody.RuntimeBodyCreated = b2Body_IsValid(rigidbody.RuntimeBodyId);
            ++newBodiesCreatedThisStep;
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
                const float safeScaleX = SanitizeFinite(transform.Scale.x, 1.0f);
                const float safeScaleY = SanitizeFinite(transform.Scale.y, 1.0f);
                const float safeColliderSizeX = SanitizeFiniteNonNegative(boxCollider->Size.x, 1.0f);
                const float safeColliderSizeY = SanitizeFiniteNonNegative(boxCollider->Size.y, 1.0f);
                const float safeColliderOffsetX = SanitizeFinite(boxCollider->Offset.x, 0.0f);
                const float safeColliderOffsetY = SanitizeFinite(boxCollider->Offset.y, 0.0f);
                const float scaledOffsetX = glm::clamp(safeColliderOffsetX * safeScaleX, -kMaximumColliderOffset, kMaximumColliderOffset);
                const float scaledOffsetY = glm::clamp(safeColliderOffsetY * safeScaleY, -kMaximumColliderOffset, kMaximumColliderOffset);
                const float halfWidth = glm::clamp(
                    std::max(kMinimumColliderExtent, safeColliderSizeX * 0.5f * std::abs(safeScaleX)),
                    kMinimumColliderExtent,
                    kMaximumColliderExtent);
                const float halfHeight = glm::clamp(
                    std::max(kMinimumColliderExtent, safeColliderSizeY * 0.5f * std::abs(safeScaleY)),
                    kMinimumColliderExtent,
                    kMaximumColliderExtent);

                const bool hadInvalidBoxParameters =
                    (safeScaleX != transform.Scale.x) ||
                    (safeScaleY != transform.Scale.y) ||
                    (safeColliderSizeX != boxCollider->Size.x) ||
                    (safeColliderSizeY != boxCollider->Size.y) ||
                    (safeColliderOffsetX != boxCollider->Offset.x) ||
                    (safeColliderOffsetY != boxCollider->Offset.y);
                if (hadInvalidBoxParameters && !rigidbody.RuntimeWarnedInvalidBodyParameters)
                {
                    const auto* tag = registry.try_get<TagComponent>(entity);
                    LT_WARN("Physics2D: sanitized invalid box collider parameters for entity '{}' (size=({}, {}), offset=({}, {}), scale=({}, {})).",
                            tag ? tag->Tag : "Entity",
                            boxCollider->Size.x,
                            boxCollider->Size.y,
                            boxCollider->Offset.x,
                            boxCollider->Offset.y,
                            transform.Scale.x,
                            transform.Scale.y);
                    rigidbody.RuntimeWarnedInvalidBodyParameters = true;
                }

                b2ShapeDef shapeDefinition = b2DefaultShapeDef();
                shapeDefinition.density = glm::clamp(
                    SanitizeFiniteNonNegative(boxCollider->Density, 1.0f),
                    0.0f,
                    kMaximumShapeDensity);
                shapeDefinition.friction = SanitizeFiniteNonNegative(boxCollider->Friction, 0.5f);
                shapeDefinition.restitution = glm::clamp(SanitizeFiniteNonNegative(boxCollider->Restitution, 0.0f), 0.0f, 1.0f);
                shapeDefinition.isSensor = boxCollider->IsSensor;
                shapeDefinition.enableContactEvents = true;
                shapeDefinition.filter.categoryBits = boxCollider->CollisionLayer;
                shapeDefinition.filter.maskBits = boxCollider->CollisionMask;
                shapeDefinition.updateBodyMass = !shapeDefinition.isSensor;
                if (rigidbody.Type == Rigidbody2DComponent::BodyType::Dynamic && !shapeDefinition.isSensor)
                {
                    shapeDefinition.density = glm::clamp(
                        shapeDefinition.density,
                        kMinimumDynamicShapeDensity,
                        kMaximumShapeDensity);
                }
                b2Polygon boxPolygon = b2MakeOffsetBox(
                    halfWidth,
                    halfHeight,
                    { scaledOffsetX, scaledOffsetY },
                    b2Rot_identity);

                boxCollider->RuntimeShapeId = b2CreatePolygonShape(rigidbody.RuntimeBodyId, &shapeDefinition, &boxPolygon);
                boxCollider->RuntimeShapeCreated = b2Shape_IsValid(boxCollider->RuntimeShapeId);
            }

            if (auto* circleCollider = registry.try_get<CircleCollider2DComponent>(entity))
            {
                const float safeScaleX = SanitizeFinite(transform.Scale.x, 1.0f);
                const float safeScaleY = SanitizeFinite(transform.Scale.y, 1.0f);
                const float safeCircleRadius = SanitizeFiniteNonNegative(circleCollider->Radius, 0.5f);
                const float safeCircleOffsetX = SanitizeFinite(circleCollider->Offset.x, 0.0f);
                const float safeCircleOffsetY = SanitizeFinite(circleCollider->Offset.y, 0.0f);
                const float maxScale = std::max(std::abs(safeScaleX), std::abs(safeScaleY));
                const float safeRadius = glm::clamp(
                    std::max(kMinimumCircleRadius, safeCircleRadius * maxScale),
                    kMinimumCircleRadius,
                    kMaximumColliderExtent);

                const bool hadInvalidCircleParameters =
                    (safeScaleX != transform.Scale.x) ||
                    (safeScaleY != transform.Scale.y) ||
                    (safeCircleRadius != circleCollider->Radius) ||
                    (safeCircleOffsetX != circleCollider->Offset.x) ||
                    (safeCircleOffsetY != circleCollider->Offset.y);
                if (hadInvalidCircleParameters && !rigidbody.RuntimeWarnedInvalidBodyParameters)
                {
                    const auto* tag = registry.try_get<TagComponent>(entity);
                    LT_WARN("Physics2D: sanitized invalid circle collider parameters for entity '{}' (radius={}, offset=({}, {}), scale=({}, {})).",
                            tag ? tag->Tag : "Entity",
                            circleCollider->Radius,
                            circleCollider->Offset.x,
                            circleCollider->Offset.y,
                            transform.Scale.x,
                            transform.Scale.y);
                    rigidbody.RuntimeWarnedInvalidBodyParameters = true;
                }

                b2ShapeDef shapeDefinition = b2DefaultShapeDef();
                shapeDefinition.density = glm::clamp(
                    SanitizeFiniteNonNegative(circleCollider->Density, 1.0f),
                    0.0f,
                    kMaximumShapeDensity);
                shapeDefinition.friction = SanitizeFiniteNonNegative(circleCollider->Friction, 0.5f);
                shapeDefinition.restitution = glm::clamp(SanitizeFiniteNonNegative(circleCollider->Restitution, 0.0f), 0.0f, 1.0f);
                shapeDefinition.isSensor = circleCollider->IsSensor;
                shapeDefinition.enableContactEvents = true;
                shapeDefinition.filter.categoryBits = circleCollider->CollisionLayer;
                shapeDefinition.filter.maskBits = circleCollider->CollisionMask;
                shapeDefinition.updateBodyMass = !shapeDefinition.isSensor;
                if (rigidbody.Type == Rigidbody2DComponent::BodyType::Dynamic && !shapeDefinition.isSensor)
                {
                    shapeDefinition.density = glm::clamp(
                        shapeDefinition.density,
                        kMinimumDynamicShapeDensity,
                        kMaximumShapeDensity);
                }
                b2Circle circleShape{};
                circleShape.center = {
                    glm::clamp(safeCircleOffsetX * safeScaleX, -kMaximumColliderOffset, kMaximumColliderOffset),
                    glm::clamp(safeCircleOffsetY * safeScaleY, -kMaximumColliderOffset, kMaximumColliderOffset)
                };
                circleShape.radius = safeRadius;

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

            // Incremental guard: skip tilemap colliders that already have a valid runtime body.
            if (tilemapCollider.RuntimeBodyCreated && b2Body_IsValid(tilemapCollider.RuntimeBodyId))
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
            shapeDefinition.updateBodyMass = false;

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
        return newBodiesCreatedThisStep;
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

            // -----------------------------------------------------------
            // Fast path: skip sleeping dynamic bodies with no script writes.
            // Keep kinematic/static bodies on the full path so transform-
            // authored motion (e.g. rotating kinematic platforms) still
            // drives collision updates even when Box2D marks them as sleeping.
            // -----------------------------------------------------------
            const bool hasPendingVelocityWrites =
                rigidbody.RuntimeHasPendingLinearVelocity ||
                rigidbody.RuntimeHasPendingLinearVelocityX ||
                rigidbody.RuntimeHasPendingLinearVelocityY;

            if (expectedType == b2_dynamicBody &&
                !b2Body_IsAwake(rigidbody.RuntimeBodyId) &&
                !hasPendingVelocityWrites)
                continue;
            if (b2Body_GetType(rigidbody.RuntimeBodyId) != expectedType)
                b2Body_SetType(rigidbody.RuntimeBodyId, expectedType);

            const bool freezePositionX = rigidbody.FreezePositionX;
            const bool freezePositionY = rigidbody.FreezePositionY;
            const bool freezeRotation = rigidbody.IsRotationLocked();
            rigidbody.FixedRotation = freezeRotation;

            // -----------------------------------------------------------
            // Property sync: damping, gravity scale, CCD, sleep, rotation
            // lock. These values rarely change after body creation, so we
            // only run the comparison for bodies that might need it:
            //   - Kinematic bodies (editor/script driven transforms)
            //   - Bodies with axis constraints (inspector-tuned)
            //   - Bodies with pending velocity writes (active scripts)
            // Pure dynamic bodies with no scripts touching them skip the
            // 6 Box2D getter calls entirely.
            // -----------------------------------------------------------
            const bool hasConstraints = freezePositionX || freezePositionY || freezeRotation;
            const bool needsPropertySync = (expectedType == b2_kinematicBody) ||
                                           hasConstraints ||
                                           hasPendingVelocityWrites;

            if (needsPropertySync)
            {
                const float safeLinearDamping = SanitizeFiniteNonNegative(rigidbody.LinearDamping, 0.0f);
                const float safeAngularDamping = SanitizeFiniteNonNegative(rigidbody.AngularDamping, 0.01f);
                const float safeGravityScale = SanitizeFinite(rigidbody.GravityScale, 1.0f);
                if ((safeLinearDamping != rigidbody.LinearDamping ||
                     safeAngularDamping != rigidbody.AngularDamping ||
                     safeGravityScale != rigidbody.GravityScale) &&
                    !rigidbody.RuntimeWarnedInvalidBodyParameters)
                {
                    const auto* tag = registry.try_get<TagComponent>(entity);
                    LT_WARN("Physics2D: sanitized runtime body properties for entity '{}' (linearDamping={}, angularDamping={}, gravityScale={}).",
                            tag ? tag->Tag : "Entity",
                            rigidbody.LinearDamping,
                            rigidbody.AngularDamping,
                            rigidbody.GravityScale);
                    rigidbody.RuntimeWarnedInvalidBodyParameters = true;
                }

                // Only call Box2D setters when values actually differ from the
                // current Box2D state. The getters are O(1) array lookups while
                // setters perform validation, wake-up logic, and solver bookkeeping.
                if (b2Body_GetLinearDamping(rigidbody.RuntimeBodyId) != safeLinearDamping)
                    b2Body_SetLinearDamping(rigidbody.RuntimeBodyId, safeLinearDamping);
                if (b2Body_GetAngularDamping(rigidbody.RuntimeBodyId) != safeAngularDamping)
                    b2Body_SetAngularDamping(rigidbody.RuntimeBodyId, safeAngularDamping);
                if (b2Body_GetGravityScale(rigidbody.RuntimeBodyId) != safeGravityScale)
                    b2Body_SetGravityScale(rigidbody.RuntimeBodyId, safeGravityScale);
                if (b2Body_IsFixedRotation(rigidbody.RuntimeBodyId) != freezeRotation)
                    b2Body_SetFixedRotation(rigidbody.RuntimeBodyId, freezeRotation);
                if (b2Body_IsSleepEnabled(rigidbody.RuntimeBodyId) != rigidbody.EnableSleep)
                    b2Body_EnableSleep(rigidbody.RuntimeBodyId, rigidbody.EnableSleep);
                if (b2Body_IsBullet(rigidbody.RuntimeBodyId) != rigidbody.UseCCD)
                    b2Body_SetBullet(rigidbody.RuntimeBodyId, rigidbody.UseCCD);
            }

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

            if (hasPendingVelocityWrites)
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

            if ((expectedType == b2_dynamicBody || expectedType == b2_kinematicBody) && hasConstraints)
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

    void Physics2DWorld::BuildJoints(Scene& scene)
    {
#ifdef LT_ENABLE_PHYSICS2D
        auto& registry = scene.GetRegistry();
        auto jointView = registry.view<Joint2DComponent, Rigidbody2DComponent>();
        for (entt::entity entity : jointView)
        {
            auto& joint = jointView.get<Joint2DComponent>(entity);

            // Incremental guard: skip joints that are already built and valid.
            if (joint.RuntimeJointCreated && b2Joint_IsValid(joint.RuntimeJointId))
                continue;

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

            if (!rigidbody.RuntimeBodyCreated || !b2Body_IsValid(rigidbody.RuntimeBodyId))
            {
                rigidbody.RuntimeContactCount = 0;
                rigidbody.RuntimeContactCountExcludingSensors = 0;
                continue;
            }

            // Sleeping bodies have frozen contacts -- their last-known counts
            // remain valid. Skip the expensive per-body contact query for them.
            if (!b2Body_IsAwake(rigidbody.RuntimeBodyId))
                continue;

            rigidbody.RuntimeContactCount = 0;
            rigidbody.RuntimeContactCountExcludingSensors = 0;

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
