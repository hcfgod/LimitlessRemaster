#include "Physics/Physics2DWorld.h"

#include "Core/Debug/Log.h"
#include "Scene/Scene.h"
#include "Scene/Components/RenderingComponents.h"

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

        float ExtractZRotationRadians(const glm::mat4& transformMatrix)
        {
            return std::atan2(transformMatrix[1][0], transformMatrix[0][0]);
        }

        struct Pose2D
        {
            glm::vec2 Position = glm::vec2(0.0f);
            float AngleRadians = 0.0f;
        };

        Pose2D GetEntityWorldPose2D(const Scene& scene, entt::entity entity, const TransformComponent& transform)
        {
            Pose2D pose{};
            pose.Position = glm::vec2(transform.Position.x, transform.Position.y);
            pose.AngleRadians = glm::radians(transform.Rotation.z);

            const entt::entity parent = scene.GetParent(entity);
            if (parent == entt::null || !scene.IsValid(parent))
                return pose;

            const glm::mat4 worldTransform = scene.GetWorldTransformMatrix(entity);
            pose.Position = glm::vec2(worldTransform[3].x, worldTransform[3].y);
            pose.AngleRadians = ExtractZRotationRadians(worldTransform);
            return pose;
        }

        void SetEntityLocalPoseFromWorld2D(const Scene& scene,
                                           entt::entity entity,
                                           const glm::vec2& worldPosition,
                                           float worldAngleRadians,
                                           TransformComponent& transform)
        {
            const entt::entity parent = scene.GetParent(entity);
            if (parent == entt::null || !scene.IsValid(parent))
            {
                transform.Position.x = worldPosition.x;
                transform.Position.y = worldPosition.y;
                transform.Rotation.z = glm::degrees(worldAngleRadians);
                return;
            }

            const glm::mat4 parentWorld = scene.GetWorldTransformMatrix(parent);
            const glm::mat4 inverseParentWorld = glm::inverse(parentWorld);
            const glm::vec4 localPosition4 = inverseParentWorld * glm::vec4(worldPosition.x, worldPosition.y, transform.Position.z, 1.0f);
            const float parentWorldAngleRadians = ExtractZRotationRadians(parentWorld);
            const float localAngleRadians = WrapAngleRadians(worldAngleRadians - parentWorldAngleRadians);

            transform.Position.x = localPosition4.x;
            transform.Position.y = localPosition4.y;
            transform.Rotation.z = glm::degrees(localAngleRadians);
        }

        bool IsEntityAssignedToWorld(const Scene& scene,
                                     entt::entity entity,
                                     uint16_t worldSlot,
                                     const Rigidbody2DComponent* cachedRigidbody = nullptr)
        {
            const auto& registry = scene.GetRegistry();
            const Rigidbody2DComponent* rigidbody = cachedRigidbody;
            if (!rigidbody)
                rigidbody = registry.try_get<Rigidbody2DComponent>(entity);
            if (!rigidbody)
                return worldSlot == 0;

            const uint16_t worldCount = std::max<uint16_t>(1, scene.GetPhysics2DWorldCount());
            const uint16_t clampedSlot = std::min<uint16_t>(rigidbody->PhysicsWorldSlot, static_cast<uint16_t>(worldCount - 1));
            return clampedSlot == worldSlot;
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

        uint64_t BuildBodyAndShapeSignature(const Rigidbody2DComponent& rigidbody,
                                            const TransformComponent& transform,
                                            const BoxCollider2DComponent* boxCollider,
                                            const CircleCollider2DComponent* circleCollider,
                                            uint16_t worldSlot)
        {
            uint64_t signature = 0;
            signature = HashCombine64(signature, static_cast<uint64_t>(worldSlot));
            signature = HashCombine64(signature, static_cast<uint64_t>(rigidbody.Type));
            signature = HashCombine64(signature, HashFloat(transform.Scale.x));
            signature = HashCombine64(signature, HashFloat(transform.Scale.y));

            signature = HashCombine64(signature, boxCollider ? 1ull : 0ull);
            if (boxCollider)
            {
                signature = HashCombine64(signature, HashFloat(boxCollider->Offset.x));
                signature = HashCombine64(signature, HashFloat(boxCollider->Offset.y));
                signature = HashCombine64(signature, HashFloat(boxCollider->Size.x));
                signature = HashCombine64(signature, HashFloat(boxCollider->Size.y));
                signature = HashCombine64(signature, HashFloat(boxCollider->Density));
                signature = HashCombine64(signature, HashFloat(boxCollider->Friction));
                signature = HashCombine64(signature, HashFloat(boxCollider->Restitution));
                signature = HashCombine64(signature, boxCollider->IsSensor ? 1ull : 0ull);
                signature = HashCombine64(signature, boxCollider->CollisionLayer);
                signature = HashCombine64(signature, boxCollider->CollisionMask);
            }

            signature = HashCombine64(signature, circleCollider ? 1ull : 0ull);
            if (circleCollider)
            {
                signature = HashCombine64(signature, HashFloat(circleCollider->Offset.x));
                signature = HashCombine64(signature, HashFloat(circleCollider->Offset.y));
                signature = HashCombine64(signature, HashFloat(circleCollider->Radius));
                signature = HashCombine64(signature, HashFloat(circleCollider->Density));
                signature = HashCombine64(signature, HashFloat(circleCollider->Friction));
                signature = HashCombine64(signature, HashFloat(circleCollider->Restitution));
                signature = HashCombine64(signature, circleCollider->IsSensor ? 1ull : 0ull);
                signature = HashCombine64(signature, circleCollider->CollisionLayer);
                signature = HashCombine64(signature, circleCollider->CollisionMask);
            }

            return signature;
        }

        uint64_t BuildJointSignature(const Joint2DComponent& joint,
                                     const Rigidbody2DComponent& bodyA,
                                     const Rigidbody2DComponent& bodyB,
                                     uint16_t worldSlot)
        {
            uint64_t signature = 0;
            signature = HashCombine64(signature, static_cast<uint64_t>(worldSlot));
            signature = HashCombine64(signature, static_cast<uint64_t>(joint.Type));
            signature = HashCombine64(signature, static_cast<uint64_t>(joint.ConnectedEntity));
            signature = HashCombine64(signature, joint.CollideConnected ? 1ull : 0ull);
            signature = HashCombine64(signature, HashFloat(joint.AnchorA.x));
            signature = HashCombine64(signature, HashFloat(joint.AnchorA.y));
            signature = HashCombine64(signature, HashFloat(joint.AnchorB.x));
            signature = HashCombine64(signature, HashFloat(joint.AnchorB.y));
            signature = HashCombine64(signature, HashFloat(joint.Axis.x));
            signature = HashCombine64(signature, HashFloat(joint.Axis.y));
            signature = HashCombine64(signature, joint.EnableLimit ? 1ull : 0ull);
            signature = HashCombine64(signature, HashFloat(joint.Limits.x));
            signature = HashCombine64(signature, HashFloat(joint.Limits.y));
            signature = HashCombine64(signature, joint.EnableMotor ? 1ull : 0ull);
            signature = HashCombine64(signature, HashFloat(joint.MotorSpeed));
            signature = HashCombine64(signature, HashFloat(joint.MaxMotorForceOrTorque));
            signature = HashCombine64(signature, joint.EnableSpring ? 1ull : 0ull);
            signature = HashCombine64(signature, HashFloat(joint.Hertz));
            signature = HashCombine64(signature, HashFloat(joint.DampingRatio));
            signature = HashCombine64(signature, bodyA.RuntimeBodyAndShapeSignature);
            signature = HashCombine64(signature, bodyB.RuntimeBodyAndShapeSignature);
            return signature;
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

    void Physics2DWorld::TeardownRuntimeBodyForRemovedComponent(entt::registry& registry,
                                                                entt::entity entity,
                                                                const Rigidbody2DComponent& removedComponent)
    {
#ifdef LT_ENABLE_PHYSICS2D
        if (removedComponent.RuntimeBodyCreated &&
            removedComponent.RuntimeWorldSlot == m_SceneWorldSlot &&
            b2Body_IsValid(removedComponent.RuntimeBodyId))
        {
            b2DestroyBody(removedComponent.RuntimeBodyId);
            m_SubStepsCacheDirty = true;
        }
#else
        (void)entity;
        (void)removedComponent;
#endif

        if (auto* boxCollider = registry.try_get<BoxCollider2DComponent>(entity))
        {
            boxCollider->RuntimeShapeId = kNullPhysics2DShape;
            boxCollider->RuntimeShapeCreated = false;
        }
        if (auto* circleCollider = registry.try_get<CircleCollider2DComponent>(entity))
        {
            circleCollider->RuntimeShapeId = kNullPhysics2DShape;
            circleCollider->RuntimeShapeCreated = false;
        }
        if (auto* joint = registry.try_get<Joint2DComponent>(entity))
        {
            joint->RuntimeJointId = kNullPhysics2DJoint;
            joint->RuntimeJointCreated = false;
            joint->RuntimeWorldSlot = 0;
            joint->RuntimeJointSignature = 0;
        }
    }

    void Physics2DWorld::TeardownRuntimeJointForRemovedComponent(entt::registry& registry,
                                                                 entt::entity entity,
                                                                 const Joint2DComponent& removedComponent)
    {
#ifdef LT_ENABLE_PHYSICS2D
        if (removedComponent.RuntimeJointCreated &&
            removedComponent.RuntimeWorldSlot == m_SceneWorldSlot &&
            b2Joint_IsValid(removedComponent.RuntimeJointId))
        {
            b2DestroyJoint(removedComponent.RuntimeJointId);
        }
#else
        (void)entity;
        (void)removedComponent;
#endif
        (void)registry;
        (void)entity;
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

    void Physics2DWorld::PrepareForStep(Scene& scene, float fixedDeltaTime)
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
            BuildJoints(scene);
            if (created > 0)
                m_SubStepsCacheDirty = true;
        }

        const float step = std::max(fixedDeltaTime, kMinimumStepDelta);
        SyncAuthoringTransformsToBodies(scene, step);

        // Use cached substep count to avoid iterating all bodies every step.
        if (m_SubStepsCacheDirty)
        {
            m_CachedEffectiveSubSteps = ComputeEffectiveSubSteps(scene);
            m_SubStepsCacheDirty = false;
        }
#else
        (void)scene;
        (void)fixedDeltaTime;
#endif
    }

    void Physics2DWorld::StepWorldOnly(float fixedDeltaTime)
    {
#ifdef LT_ENABLE_PHYSICS2D
        if (!b2World_IsValid(m_WorldId))
            return;

        const float step = std::max(fixedDeltaTime, kMinimumStepDelta);
        const int effectiveSubSteps = (m_CachedEffectiveSubSteps > 0)
            ? m_CachedEffectiveSubSteps
            : std::max(1, m_Settings.VelocitySubSteps);
        b2World_Step(m_WorldId, step, effectiveSubSteps);
#else
        (void)fixedDeltaTime;
#endif
    }

    void Physics2DWorld::SyncAfterStep(Scene& scene)
    {
#ifdef LT_ENABLE_PHYSICS2D

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
#endif
    }

    void Physics2DWorld::Step(Scene& scene, float fixedDeltaTime)
    {
        PrepareForStep(scene, fixedDeltaTime);
        StepWorldOnly(fixedDeltaTime);
        SyncAfterStep(scene);
    }

    void Physics2DWorld::DestroyRuntimeState(Scene& scene)
    {
        auto& registry = scene.GetRegistry();

        auto jointView = registry.view<Joint2DComponent>();
        for (auto [entity, joint] : jointView.each())
        {
            (void)entity;
            if (joint.RuntimeWorldSlot != m_SceneWorldSlot)
                continue;
#ifdef LT_ENABLE_PHYSICS2D
            if (joint.RuntimeJointCreated && b2Joint_IsValid(joint.RuntimeJointId))
                b2DestroyJoint(joint.RuntimeJointId);
            joint.RuntimeJointId = kNullPhysics2DJoint;
#endif
            joint.RuntimeJointCreated = false;
            joint.RuntimeWorldSlot = 0;
            joint.RuntimeJointSignature = 0;
        }

        auto boxColliderView = registry.view<BoxCollider2DComponent>();
        for (auto [entity, collider] : boxColliderView.each())
        {
            const auto* rigidbody = registry.try_get<Rigidbody2DComponent>(entity);
            if (!rigidbody || rigidbody->RuntimeWorldSlot != m_SceneWorldSlot)
                continue;
#ifdef LT_ENABLE_PHYSICS2D
            collider.RuntimeShapeId = kNullPhysics2DShape;
#endif
            collider.RuntimeShapeCreated = false;
        }

        auto circleColliderView = registry.view<CircleCollider2DComponent>();
        for (auto [entity, collider] : circleColliderView.each())
        {
            const auto* rigidbody = registry.try_get<Rigidbody2DComponent>(entity);
            if (!rigidbody || rigidbody->RuntimeWorldSlot != m_SceneWorldSlot)
                continue;
#ifdef LT_ENABLE_PHYSICS2D
            collider.RuntimeShapeId = kNullPhysics2DShape;
#endif
            collider.RuntimeShapeCreated = false;
        }

        auto bodyView = registry.view<Rigidbody2DComponent>();
        for (auto [entity, rigidbody] : bodyView.each())
        {
            (void)entity;
            if (rigidbody.RuntimeWorldSlot != m_SceneWorldSlot)
                continue;
#ifdef LT_ENABLE_PHYSICS2D
            if (rigidbody.RuntimeBodyCreated && b2Body_IsValid(rigidbody.RuntimeBodyId))
                b2DestroyBody(rigidbody.RuntimeBodyId);
            rigidbody.RuntimeBodyId = kNullPhysics2DBody;
#endif
            rigidbody.RuntimeBodyCreated = false;
            rigidbody.RuntimeWorldSlot = 0;
            rigidbody.RuntimeBodyAndShapeSignature = 0;
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
            auto* boxCollider = registry.try_get<BoxCollider2DComponent>(entity);
            auto* circleCollider = registry.try_get<CircleCollider2DComponent>(entity);
            const bool assignedToThisWorld = IsEntityAssignedToWorld(scene, entity, m_SceneWorldSlot, &rigidbody);
            if (!assignedToThisWorld)
            {
                if (rigidbody.RuntimeWorldSlot == m_SceneWorldSlot)
                {
                    if (rigidbody.RuntimeBodyCreated && b2Body_IsValid(rigidbody.RuntimeBodyId))
                        b2DestroyBody(rigidbody.RuntimeBodyId);
                    rigidbody.RuntimeBodyId = kNullPhysics2DBody;
                    rigidbody.RuntimeBodyCreated = false;
                    rigidbody.RuntimeWorldSlot = 0;
                    rigidbody.RuntimeContactCount = 0;
                    rigidbody.RuntimeContactCountExcludingSensors = 0;
                    rigidbody.RuntimeBodyAndShapeSignature = 0;
                    if (boxCollider)
                    {
                        boxCollider->RuntimeShapeId = kNullPhysics2DShape;
                        boxCollider->RuntimeShapeCreated = false;
                    }
                    if (circleCollider)
                    {
                        circleCollider->RuntimeShapeId = kNullPhysics2DShape;
                        circleCollider->RuntimeShapeCreated = false;
                    }
                }
                continue;
            }

            if (!scene.IsEntityEnabledInHierarchy(entity))
            {
                if (rigidbody.RuntimeWorldSlot == m_SceneWorldSlot &&
                    rigidbody.RuntimeBodyCreated &&
                    b2Body_IsValid(rigidbody.RuntimeBodyId))
                {
                    b2DestroyBody(rigidbody.RuntimeBodyId);
                }
                rigidbody.RuntimeBodyId = kNullPhysics2DBody;
                rigidbody.RuntimeBodyCreated = false;
                rigidbody.RuntimeWorldSlot = 0;
                rigidbody.RuntimeContactCount = 0;
                rigidbody.RuntimeContactCountExcludingSensors = 0;
                rigidbody.RuntimeBodyAndShapeSignature = 0;
                if (boxCollider)
                {
                    boxCollider->RuntimeShapeId = kNullPhysics2DShape;
                    boxCollider->RuntimeShapeCreated = false;
                }
                if (circleCollider)
                {
                    circleCollider->RuntimeShapeId = kNullPhysics2DShape;
                    circleCollider->RuntimeShapeCreated = false;
                }
                continue;
            }

            auto& transform = bodyView.get<TransformComponent>(entity);
            const uint64_t desiredBodyAndShapeSignature =
                BuildBodyAndShapeSignature(rigidbody, transform, boxCollider, circleCollider, m_SceneWorldSlot);

            const bool hasValidRuntimeBody = rigidbody.RuntimeBodyCreated && b2Body_IsValid(rigidbody.RuntimeBodyId);
            bool shouldRebuildExistingRuntimeBody = false;
            if (hasValidRuntimeBody)
            {
                if (rigidbody.RuntimeWorldSlot != m_SceneWorldSlot)
                    continue;
                if (rigidbody.RuntimeBodyAndShapeSignature == desiredBodyAndShapeSignature)
                    continue;
                shouldRebuildExistingRuntimeBody = true;
            }

            // Engine-side batching: cap the number of new bodies per step to
            // prevent overwhelming Box2D's allocator. Deferred entities will
            // be created on subsequent physics steps automatically.
            if (newBodiesCreatedThisStep >= kMaxNewBodiesPerStep)
            {
                if (shouldRebuildExistingRuntimeBody)
                    continue;
                break;
            }

            if (shouldRebuildExistingRuntimeBody)
            {
                b2DestroyBody(rigidbody.RuntimeBodyId);
                rigidbody.RuntimeBodyId = kNullPhysics2DBody;
                rigidbody.RuntimeBodyCreated = false;
                rigidbody.RuntimeWorldSlot = 0;
                rigidbody.RuntimeContactCount = 0;
                rigidbody.RuntimeContactCountExcludingSensors = 0;
                rigidbody.RuntimeBodyAndShapeSignature = 0;
                if (boxCollider)
                {
                    boxCollider->RuntimeShapeId = kNullPhysics2DShape;
                    boxCollider->RuntimeShapeCreated = false;
                }
                if (circleCollider)
                {
                    circleCollider->RuntimeShapeId = kNullPhysics2DShape;
                    circleCollider->RuntimeShapeCreated = false;
                }
            }

            const Pose2D authoredWorldPose = GetEntityWorldPose2D(scene, entity, transform);
            const float safePositionX = glm::clamp(SanitizeFinite(authoredWorldPose.Position.x, 0.0f), -kMaximumWorldPosition, kMaximumWorldPosition);
            const float safePositionY = glm::clamp(SanitizeFinite(authoredWorldPose.Position.y, 0.0f), -kMaximumWorldPosition, kMaximumWorldPosition);
            const float safeRotationDegrees = glm::degrees(SanitizeFinite(authoredWorldPose.AngleRadians, 0.0f));
            const float safeLinearDamping = SanitizeFiniteNonNegative(rigidbody.LinearDamping, 0.0f);
            const float safeAngularDamping = SanitizeFiniteNonNegative(rigidbody.AngularDamping, 0.01f);
            const float safeGravityScale = SanitizeFinite(rigidbody.GravityScale, 1.0f);

            const bool hadInvalidBodyParameters =
                (safePositionX != authoredWorldPose.Position.x) ||
                (safePositionY != authoredWorldPose.Position.y) ||
                (safeRotationDegrees != glm::degrees(authoredWorldPose.AngleRadians)) ||
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
            const bool isKinematicBody = rigidbody.Type == Rigidbody2DComponent::BodyType::Kinematic;
            bodyDefinition.enableSleep = !isKinematicBody && rigidbody.EnableSleep;
            bodyDefinition.isAwake = isKinematicBody || rigidbody.StartAwake;
            bodyDefinition.isBullet = rigidbody.UseCCD;
            bodyDefinition.userData = ToUserData(entity);

            rigidbody.RuntimeBodyId = b2CreateBody(m_WorldId, &bodyDefinition);
            rigidbody.RuntimeBodyCreated = b2Body_IsValid(rigidbody.RuntimeBodyId);
            rigidbody.RuntimeWorldSlot = rigidbody.RuntimeBodyCreated ? m_SceneWorldSlot : 0;
            ++newBodiesCreatedThisStep;
            rigidbody.RuntimePreviousPosition = glm::vec2(safePositionX, safePositionY);
            rigidbody.RuntimePreviousAngleRadians = glm::radians(safeRotationDegrees);
            rigidbody.RuntimeRenderPreviousPosition = rigidbody.RuntimePreviousPosition;
            rigidbody.RuntimeRenderPreviousAngleRadians = rigidbody.RuntimePreviousAngleRadians;
            rigidbody.RuntimeRenderCurrentPosition = rigidbody.RuntimePreviousPosition;
            rigidbody.RuntimeRenderCurrentAngleRadians = rigidbody.RuntimePreviousAngleRadians;
            rigidbody.RuntimeLinearVelocity = glm::vec2(0.0f);

            // Preserve script-authored velocity writes made in the same frame as
            // body creation (for example, freshly instantiated projectiles).
            // Previously this was reset here, causing bullets to spawn and remain
            // stationary until another script write occurred.
            if (rigidbody.RuntimeBodyCreated)
            {
                b2Vec2 initialVelocity{ 0.0f, 0.0f };
                if (rigidbody.RuntimeHasPendingLinearVelocity)
                {
                    initialVelocity.x = rigidbody.RuntimePendingLinearVelocity.x;
                    initialVelocity.y = rigidbody.RuntimePendingLinearVelocity.y;
                }
                else
                {
                    if (rigidbody.RuntimeHasPendingLinearVelocityX)
                        initialVelocity.x = rigidbody.RuntimePendingLinearVelocityX;
                    if (rigidbody.RuntimeHasPendingLinearVelocityY)
                        initialVelocity.y = rigidbody.RuntimePendingLinearVelocityY;
                }

                if (rigidbody.FreezePositionX)
                    initialVelocity.x = 0.0f;
                if (rigidbody.FreezePositionY)
                    initialVelocity.y = 0.0f;

                if (rigidbody.Type != Rigidbody2DComponent::BodyType::Static)
                    b2Body_SetLinearVelocity(rigidbody.RuntimeBodyId, initialVelocity);
                rigidbody.RuntimeLinearVelocity = glm::vec2(initialVelocity.x, initialVelocity.y);
            }

            rigidbody.RuntimePendingLinearVelocity = glm::vec2(0.0f);
            rigidbody.RuntimeHasPendingLinearVelocity = false;
            rigidbody.RuntimePendingLinearVelocityX = 0.0f;
            rigidbody.RuntimeHasPendingLinearVelocityX = false;
            rigidbody.RuntimePendingLinearVelocityY = 0.0f;
            rigidbody.RuntimeHasPendingLinearVelocityY = false;

            if (!rigidbody.RuntimeBodyCreated)
            {
                rigidbody.RuntimeBodyAndShapeSignature = 0;
                continue;
            }

            if (boxCollider)
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
                if (boxCollider->RuntimeShapeCreated)
                {
                    b2Shape_SetFriction(boxCollider->RuntimeShapeId, SanitizeFiniteNonNegative(boxCollider->Friction, 0.5f));
                    b2Shape_SetRestitution(
                        boxCollider->RuntimeShapeId,
                        glm::clamp(SanitizeFiniteNonNegative(boxCollider->Restitution, 0.0f), 0.0f, 1.0f));
                }
            }

            if (circleCollider)
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
                if (circleCollider->RuntimeShapeCreated)
                {
                    b2Shape_SetFriction(circleCollider->RuntimeShapeId, SanitizeFiniteNonNegative(circleCollider->Friction, 0.5f));
                    b2Shape_SetRestitution(
                        circleCollider->RuntimeShapeId,
                        glm::clamp(SanitizeFiniteNonNegative(circleCollider->Restitution, 0.0f), 0.0f, 1.0f));
                }
            }

            rigidbody.RuntimeBodyAndShapeSignature = desiredBodyAndShapeSignature;
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
        static std::unordered_map<entt::entity, uint32_t> s_AstroidMissingRuntimeBodyFrames;
        static std::unordered_map<entt::entity, uint32_t> s_AstroidUnexpectedBodyTypeFrames;
        static std::unordered_map<entt::entity, uint32_t> s_AstroidOriginMismatchFrames;
        for (entt::entity entity : bodyView)
        {
            if (!scene.IsEntityEnabledInHierarchy(entity))
                continue;
            auto& rigidbody = bodyView.get<Rigidbody2DComponent>(entity);
            auto& transform = bodyView.get<TransformComponent>(entity);
            const bool assignedToThisWorld = IsEntityAssignedToWorld(scene, entity, m_SceneWorldSlot, &rigidbody);
            if (!assignedToThisWorld)
                continue;
            if (rigidbody.RuntimeWorldSlot != m_SceneWorldSlot)
                continue;

            if (!rigidbody.RuntimeBodyCreated || !b2Body_IsValid(rigidbody.RuntimeBodyId))
                continue;

            const b2BodyType expectedType = ToBox2DBodyType(rigidbody.Type);
            const bool hasPendingVelocityWrites =
                rigidbody.RuntimeHasPendingLinearVelocity ||
                rigidbody.RuntimeHasPendingLinearVelocityX ||
                rigidbody.RuntimeHasPendingLinearVelocityY;
            if (b2Body_GetType(rigidbody.RuntimeBodyId) != expectedType)
                b2Body_SetType(rigidbody.RuntimeBodyId, expectedType);

            const bool freezePositionX = rigidbody.FreezePositionX;
            const bool freezePositionY = rigidbody.FreezePositionY;
            const bool freezeRotation = rigidbody.IsRotationLocked();
            rigidbody.FixedRotation = freezeRotation;

            b2Transform runtimeTransform = b2Body_GetTransform(rigidbody.RuntimeBodyId);
            glm::vec2 runtimePosition(runtimeTransform.p.x, runtimeTransform.p.y);
            float runtimeAngleRadians = b2Rot_GetAngle(runtimeTransform.q);
            const Pose2D authoredWorldPose = GetEntityWorldPose2D(scene, entity, transform);
            glm::vec2 authoringPosition = authoredWorldPose.Position;
            float authoringAngleRadians = authoredWorldPose.AngleRadians;
            if (const auto* animator = registry.try_get<AnimatorComponent>(entity);
                animator && animator->ApplyToTransform)
            {
                if (animator->RuntimeHasPosition)
                {
                    authoringPosition.x += animator->RuntimePosition.x;
                    authoringPosition.y += animator->RuntimePosition.y;
                }
                if (animator->RuntimeHasRotation)
                    authoringAngleRadians = WrapAngleRadians(authoringAngleRadians + glm::radians(animator->RuntimeRotation.z));
            }
            glm::vec2 snappedAuthoringPosition = authoringPosition;
            float snappedAuthoringAngleRadians = authoringAngleRadians;
            bool snappedTransformToRuntime = false;

            // Constraints are authoritative during simulation.
            // If scripts/editor mutate constrained axes directly, snap world pose back to the body.
            if (freezePositionX)
                snappedAuthoringPosition.x = runtimePosition.x;
            if (freezePositionY)
                snappedAuthoringPosition.y = runtimePosition.y;
            if (freezeRotation)
                snappedAuthoringAngleRadians = runtimeAngleRadians;

            if (glm::distance(snappedAuthoringPosition, authoringPosition) > kTransformSnapEpsilon ||
                std::abs(WrapAngleRadians(snappedAuthoringAngleRadians - authoringAngleRadians)) > kTransformSnapEpsilon)
            {
                SetEntityLocalPoseFromWorld2D(scene, entity, snappedAuthoringPosition, snappedAuthoringAngleRadians, transform);
                authoringPosition = snappedAuthoringPosition;
                authoringAngleRadians = snappedAuthoringAngleRadians;
                snappedTransformToRuntime = true;
            }

            if (snappedTransformToRuntime)
                scene.MarkTransformDirty(entity);

            const glm::vec2 positionDelta = authoringPosition - runtimePosition;
            const float angleDelta = WrapAngleRadians(authoringAngleRadians - runtimeAngleRadians);
            bool transformChangedByAuthoring = glm::length(positionDelta) > kTransformSnapEpsilon ||
                                               std::abs(angleDelta) > kTransformSnapEpsilon;
            // -----------------------------------------------------------
            // Fast path: skip sleeping dynamic bodies only when they have
            // no pending velocity writes and no authored transform delta.
            // This keeps transform-driven dynamic motion in sync with Box2D.
            // -----------------------------------------------------------
            if (expectedType == b2_dynamicBody &&
                !b2Body_IsAwake(rigidbody.RuntimeBodyId) &&
                !hasPendingVelocityWrites)
            {
                if (!transformChangedByAuthoring)
                    continue;

                b2Body_SetTransform(
                    rigidbody.RuntimeBodyId,
                    { authoringPosition.x, authoringPosition.y },
                    b2MakeRot(authoringAngleRadians));
                b2Body_SetAwake(rigidbody.RuntimeBodyId, true);
                transformChangedByAuthoring = false;

#if !defined(NDEBUG)
                static uint64_t s_TransformWakeEventCount = 0;
                ++s_TransformWakeEventCount;
                if (s_TransformWakeEventCount <= 4 || (s_TransformWakeEventCount % 64ull) == 0ull)
                {
                    const auto* tag = registry.try_get<TagComponent>(entity);
                    LT_WARN("Physics2D: woke sleeping dynamic body '{}' after transform-authored pose update (event #{})",
                            tag ? tag->Tag : "Entity",
                            s_TransformWakeEventCount);
                }
#endif
            }

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
                const bool effectiveEnableSleep = (expectedType != b2_kinematicBody) && rigidbody.EnableSleep;
                if (b2Body_IsSleepEnabled(rigidbody.RuntimeBodyId) != effectiveEnableSleep)
                    b2Body_EnableSleep(rigidbody.RuntimeBodyId, effectiveEnableSleep);
                if (b2Body_IsBullet(rigidbody.RuntimeBodyId) != rigidbody.UseCCD)
                    b2Body_SetBullet(rigidbody.RuntimeBodyId, rigidbody.UseCCD);
            }

            if (expectedType == b2_kinematicBody)
            {
                // Drive kinematic bodies from authored transform deltas unless a script
                // explicitly wrote linear velocity. This keeps pooled enemy movement
                // deterministic in debug and avoids stale-body snapback.
                b2Vec2 runtimeVelocity = b2Body_GetLinearVelocity(rigidbody.RuntimeBodyId);
                const glm::vec2 authoredPositionDelta = authoringPosition - rigidbody.RuntimePreviousPosition;
                const float authoredAngleDelta = WrapAngleRadians(authoringAngleRadians - rigidbody.RuntimePreviousAngleRadians);
                const bool authoringChanged = glm::length(authoredPositionDelta) > kTransformSnapEpsilon ||
                                              std::abs(authoredAngleDelta) > kTransformSnapEpsilon;

                float targetAngularVelocity = 0.0f;
                if (hasPendingVelocityWrites)
                {
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

                    rigidbody.RuntimeHasPendingLinearVelocity = false;
                    rigidbody.RuntimeHasPendingLinearVelocityX = false;
                    rigidbody.RuntimeHasPendingLinearVelocityY = false;
                    targetAngularVelocity = authoredAngleDelta / std::max(fixedDeltaTime, kMinimumStepDelta);
                }
                else
                {
                    if (!authoringChanged)
                    {
                        const bool hasRuntimeLinearVelocity =
                            std::abs(runtimeVelocity.x) > kTransformSnapEpsilon ||
                            std::abs(runtimeVelocity.y) > kTransformSnapEpsilon;

                        // Preserve non-zero runtime velocity when scripts already set kinematic velocity.
                        // This avoids zeroing the body between multiple fixed substeps in one frame.
                        if (hasRuntimeLinearVelocity)
                        {
                            rigidbody.RuntimeLinearVelocity = glm::vec2(runtimeVelocity.x, runtimeVelocity.y);
                            rigidbody.RuntimePreviousPosition = authoringPosition;
                            rigidbody.RuntimePreviousAngleRadians = authoringAngleRadians;
                            continue;
                        }

                        runtimeVelocity = { 0.0f, 0.0f };
                        b2Body_SetLinearVelocity(rigidbody.RuntimeBodyId, runtimeVelocity);
                        b2Body_SetAngularVelocity(rigidbody.RuntimeBodyId, 0.0f);
                        rigidbody.RuntimeLinearVelocity = glm::vec2(0.0f);
                        rigidbody.RuntimePreviousPosition = authoringPosition;
                        rigidbody.RuntimePreviousAngleRadians = authoringAngleRadians;
                        continue;
                    }

                    const float inverseStep = 1.0f / std::max(fixedDeltaTime, kMinimumStepDelta);
                    runtimeVelocity.x = authoredPositionDelta.x * inverseStep;
                    runtimeVelocity.y = authoredPositionDelta.y * inverseStep;
                    targetAngularVelocity = authoredAngleDelta * inverseStep;
                }

                if (freezePositionX)
                    runtimeVelocity.x = 0.0f;
                if (freezePositionY)
                    runtimeVelocity.y = 0.0f;
                if (freezeRotation)
                    targetAngularVelocity = 0.0f;

                // Authoring transform writes (spawn/reposition) must be applied even
                // when scripts also drive velocity this frame.
                if (transformChangedByAuthoring)
                {
                    b2Body_SetTransform(
                        rigidbody.RuntimeBodyId,
                        { authoringPosition.x, authoringPosition.y },
                        b2MakeRot(authoringAngleRadians));
                    b2Body_SetAwake(rigidbody.RuntimeBodyId, true);
                }

                b2Body_SetLinearVelocity(rigidbody.RuntimeBodyId, runtimeVelocity);
                b2Body_SetAngularVelocity(rigidbody.RuntimeBodyId, targetAngularVelocity);

                rigidbody.RuntimeLinearVelocity = glm::vec2(runtimeVelocity.x, runtimeVelocity.y);
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

            if (transformChangedByAuthoring)
            {
                b2Body_SetTransform(
                    rigidbody.RuntimeBodyId,
                    { authoringPosition.x, authoringPosition.y },
                    b2MakeRot(authoringAngleRadians));
            }
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
        auto destroyRuntimeJoint = [this](Joint2DComponent& joint) {
            if (joint.RuntimeJointCreated &&
                joint.RuntimeWorldSlot == m_SceneWorldSlot &&
                b2Joint_IsValid(joint.RuntimeJointId))
            {
                b2DestroyJoint(joint.RuntimeJointId);
            }
            joint.RuntimeJointId = kNullPhysics2DJoint;
            joint.RuntimeJointCreated = false;
            joint.RuntimeWorldSlot = 0;
            joint.RuntimeJointSignature = 0;
        };
        for (entt::entity entity : jointView)
        {
            auto& joint = jointView.get<Joint2DComponent>(entity);
            auto& bodyAComponent = jointView.get<Rigidbody2DComponent>(entity);
            if (!IsEntityAssignedToWorld(scene, entity, m_SceneWorldSlot, &bodyAComponent))
            {
                destroyRuntimeJoint(joint);
                continue;
            }

            if (!scene.IsEntityEnabledInHierarchy(entity))
            {
                destroyRuntimeJoint(joint);
                continue;
            }

            if (!bodyAComponent.RuntimeBodyCreated ||
                bodyAComponent.RuntimeWorldSlot != m_SceneWorldSlot ||
                !b2Body_IsValid(bodyAComponent.RuntimeBodyId))
            {
                destroyRuntimeJoint(joint);
                continue;
            }

            if (joint.ConnectedEntity == entt::null || !scene.IsValid(joint.ConnectedEntity))
            {
                destroyRuntimeJoint(joint);
                continue;
            }
            if (!scene.IsEntityEnabledInHierarchy(joint.ConnectedEntity))
            {
                destroyRuntimeJoint(joint);
                continue;
            }

            auto* bodyBComponent = registry.try_get<Rigidbody2DComponent>(joint.ConnectedEntity);
            if (!bodyBComponent ||
                !IsEntityAssignedToWorld(scene, joint.ConnectedEntity, m_SceneWorldSlot, bodyBComponent) ||
                !bodyBComponent->RuntimeBodyCreated ||
                bodyBComponent->RuntimeWorldSlot != m_SceneWorldSlot ||
                !b2Body_IsValid(bodyBComponent->RuntimeBodyId))
            {
                destroyRuntimeJoint(joint);
                continue;
            }

            const uint64_t desiredJointSignature =
                BuildJointSignature(joint, bodyAComponent, *bodyBComponent, m_SceneWorldSlot);

            if (joint.RuntimeJointCreated &&
                joint.RuntimeWorldSlot == m_SceneWorldSlot &&
                b2Joint_IsValid(joint.RuntimeJointId))
            {
                if (joint.RuntimeJointSignature == desiredJointSignature)
                    continue;

                destroyRuntimeJoint(joint);
            }

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
            joint.RuntimeWorldSlot = joint.RuntimeJointCreated ? m_SceneWorldSlot : 0;
            joint.RuntimeJointSignature = joint.RuntimeJointCreated ? desiredJointSignature : 0;
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
            if (!scene.IsEntityEnabledInHierarchy(entity))
                continue;

            auto* transform = registry.try_get<TransformComponent>(entity);
            auto* rigidbody = registry.try_get<Rigidbody2DComponent>(entity);
            if (!transform || !rigidbody)
                continue;
            if (!IsEntityAssignedToWorld(scene, entity, m_SceneWorldSlot, rigidbody))
                continue;
            if (rigidbody->RuntimeWorldSlot != m_SceneWorldSlot)
                continue;

            const bool freezePositionX = rigidbody->FreezePositionX;
            const bool freezePositionY = rigidbody->FreezePositionY;
            const bool freezeRotation = rigidbody->IsRotationLocked();
            rigidbody->FixedRotation = freezeRotation;
            const Pose2D authoredWorldPose = GetEntityWorldPose2D(scene, entity, *transform);

            glm::vec2 bodyPosition(moveEvent.transform.p.x, moveEvent.transform.p.y);
            float bodyAngleRadians = b2Rot_GetAngle(moveEvent.transform.q);
            b2Vec2 bodyVelocity = b2Body_GetLinearVelocity(rigidbody->RuntimeBodyId);

            // Defensive runtime guard: on some platforms/driver/library combos,
            // upstream physics can occasionally report non-finite values.
            // Never propagate NaN/Inf into authored transforms.
            if (!std::isfinite(bodyPosition.x) ||
                !std::isfinite(bodyPosition.y) ||
                !std::isfinite(bodyAngleRadians) ||
                !std::isfinite(bodyVelocity.x) ||
                !std::isfinite(bodyVelocity.y))
            {
                const float safeX = glm::clamp(SanitizeFinite(transform->Position.x, 0.0f), -kMaximumWorldPosition, kMaximumWorldPosition);
                const float safeY = glm::clamp(SanitizeFinite(transform->Position.y, 0.0f), -kMaximumWorldPosition, kMaximumWorldPosition);
                const float safeAngleRadians = SanitizeFinite(glm::radians(transform->Rotation.z), 0.0f);

                b2Body_SetTransform(rigidbody->RuntimeBodyId, { safeX, safeY }, b2MakeRot(safeAngleRadians));
                b2Body_SetLinearVelocity(rigidbody->RuntimeBodyId, { 0.0f, 0.0f });
                b2Body_SetAngularVelocity(rigidbody->RuntimeBodyId, 0.0f);

                bodyPosition = glm::vec2(safeX, safeY);
                bodyAngleRadians = safeAngleRadians;
                bodyVelocity = { 0.0f, 0.0f };
            }

            if (freezePositionX || freezePositionY || freezeRotation)
            {
                glm::vec2 constrainedPosition = bodyPosition;
                float constrainedAngleRadians = bodyAngleRadians;
                if (freezePositionX)
                    constrainedPosition.x = authoredWorldPose.Position.x;
                if (freezePositionY)
                    constrainedPosition.y = authoredWorldPose.Position.y;
                if (freezeRotation)
                    constrainedAngleRadians = authoredWorldPose.AngleRadians;

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

            const bool isKinematicBody = rigidbody->Type == Rigidbody2DComponent::BodyType::Kinematic;
            const bool kinematicHasRuntimeVelocity =
                std::abs(bodyVelocity.x) > kTransformSnapEpsilon ||
                std::abs(bodyVelocity.y) > kTransformSnapEpsilon;
            if (isKinematicBody && !kinematicHasRuntimeVelocity)
                continue;

            rigidbody->RuntimePreviousPosition = authoredWorldPose.Position;
            rigidbody->RuntimePreviousAngleRadians = authoredWorldPose.AngleRadians;

            const bool transformChanged =
                std::abs(authoredWorldPose.Position.x - bodyPosition.x) > kTransformSnapEpsilon ||
                std::abs(authoredWorldPose.Position.y - bodyPosition.y) > kTransformSnapEpsilon ||
                std::abs(WrapAngleRadians(authoredWorldPose.AngleRadians - bodyAngleRadians)) > kTransformSnapEpsilon;

            SetEntityLocalPoseFromWorld2D(scene, entity, bodyPosition, bodyAngleRadians, *transform);

            if (transformChanged)
                scene.MarkTransformDirty(entity);
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
            if (!IsEntityAssignedToWorld(scene, entity, m_SceneWorldSlot, &rigidbody) ||
                rigidbody.RuntimeWorldSlot != m_SceneWorldSlot)
            {
                continue;
            }
            if (!scene.IsEntityEnabledInHierarchy(entity))
            {
                rigidbody.RuntimeContactCount = 0;
                rigidbody.RuntimeContactCountExcludingSensors = 0;
                continue;
            }

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

        auto resolveShapeEntity = [](b2ShapeId shapeId) -> entt::entity
        {
            if (!b2Shape_IsValid(shapeId))
                return entt::null;

            const b2BodyId bodyId = b2Shape_GetBody(shapeId);
            if (!b2Body_IsValid(bodyId))
                return entt::null;

            return ToEntityHandle(b2Body_GetUserData(bodyId));
        };

        const b2SensorEvents sensorEvents = b2World_GetSensorEvents(m_WorldId);
        for (int eventIndex = 0; eventIndex < sensorEvents.beginCount; ++eventIndex)
        {
            const auto& eventData = sensorEvents.beginEvents[eventIndex];
            const entt::entity sensorEntity = resolveShapeEntity(eventData.sensorShapeId);
            const entt::entity visitorEntity = resolveShapeEntity(eventData.visitorShapeId);
            if (sensorEntity == entt::null || visitorEntity == entt::null)
                continue;

            m_ContactListener.PushBegin(sensorEntity, visitorEntity, true);
        }

        for (int eventIndex = 0; eventIndex < sensorEvents.endCount; ++eventIndex)
        {
            const auto& eventData = sensorEvents.endEvents[eventIndex];
            const entt::entity sensorEntity = resolveShapeEntity(eventData.sensorShapeId);
            const entt::entity visitorEntity = resolveShapeEntity(eventData.visitorShapeId);
            if (sensorEntity == entt::null || visitorEntity == entt::null)
                continue;

            m_ContactListener.PushEnd(sensorEntity, visitorEntity, true);
        }

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
            if (!IsEntityAssignedToWorld(scene, entity, m_SceneWorldSlot, &rigidbody))
                continue;
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
            if (!IsEntityAssignedToWorld(scene, entity, m_SceneWorldSlot, &rigidbody))
                continue;
            if (!rigidbody.RuntimeBodyCreated ||
                rigidbody.RuntimeWorldSlot != m_SceneWorldSlot ||
                !b2Body_IsValid(rigidbody.RuntimeBodyId))
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
            if (!IsEntityAssignedToWorld(scene, entity, m_SceneWorldSlot, &rigidbody))
                continue;
            if (!rigidbody.RuntimeBodyCreated ||
                rigidbody.RuntimeWorldSlot != m_SceneWorldSlot ||
                !b2Body_IsValid(rigidbody.RuntimeBodyId))
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
