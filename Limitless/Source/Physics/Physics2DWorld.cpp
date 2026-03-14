#include "Physics/Physics2DWorldInternal.h"
#include "Core/Debug/Log.h"

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
    using namespace Physics2DInternal;

    // Anonymous namespace helpers have been moved to Physics2DWorldInternal.h.
    // BuildBodiesAndShapes lives in Physics2DShapeBuilders.cpp.
    // Sync methods live in Physics2DSync.cpp.

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

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
        if (auto* polygonCollider = registry.try_get<PolygonCollider2DComponent>(entity))
        {
            polygonCollider->RuntimeShapeId = kNullPhysics2DShape;
            polygonCollider->RuntimeShapeCreated = false;
        }
        if (auto* edgeCollider = registry.try_get<EdgeCollider2DComponent>(entity))
        {
            edgeCollider->RuntimeShapeId = kNullPhysics2DShape;
            edgeCollider->RuntimeShapeCreated = false;
        }
        if (auto* capsuleCollider = registry.try_get<CapsuleCollider2DComponent>(entity))
        {
            capsuleCollider->RuntimeShapeId = kNullPhysics2DShape;
            capsuleCollider->RuntimeShapeCreated = false;
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

    // -------------------------------------------------------------------------
    // Step
    // -------------------------------------------------------------------------

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

        auto polygonColliderView = registry.view<PolygonCollider2DComponent>();
        for (auto [entity, collider] : polygonColliderView.each())
        {
            const auto* rigidbody = registry.try_get<Rigidbody2DComponent>(entity);
            if (!rigidbody || rigidbody->RuntimeWorldSlot != m_SceneWorldSlot)
                continue;
#ifdef LT_ENABLE_PHYSICS2D
            collider.RuntimeShapeId = kNullPhysics2DShape;
#endif
            collider.RuntimeShapeCreated = false;
        }

        auto edgeColliderView = registry.view<EdgeCollider2DComponent>();
        for (auto [entity, collider] : edgeColliderView.each())
        {
            const auto* rigidbody = registry.try_get<Rigidbody2DComponent>(entity);
            if (!rigidbody || rigidbody->RuntimeWorldSlot != m_SceneWorldSlot)
                continue;
#ifdef LT_ENABLE_PHYSICS2D
            collider.RuntimeShapeId = kNullPhysics2DShape;
#endif
            collider.RuntimeShapeCreated = false;
        }

        auto capsuleColliderView = registry.view<CapsuleCollider2DComponent>();
        for (auto [entity, collider] : capsuleColliderView.each())
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
            rigidbody.RuntimeContactCount = 0;
            rigidbody.RuntimeContactCountExcludingSensors = 0;
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
        }
        m_Diagnostics = Physics2DDiagnostics{};
        m_BodyDiagnostics.clear();
        m_SubStepsCacheDirty = true;
    }

    // -------------------------------------------------------------------------
    // Joints
    // -------------------------------------------------------------------------

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

    // -------------------------------------------------------------------------
    // Diagnostics & Queries
    // -------------------------------------------------------------------------

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
