#include "Physics/Physics2DWorld.h"

#include "Scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
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

        const int subSteps = std::max(1, m_Settings.VelocitySubSteps);
        b2World_Step(m_WorldId, step, subSteps);

        SyncMovedBodiesToTransforms(scene);
        CollectContactEvents();
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
        }
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
            bodyDefinition.fixedRotation = rigidbody.FixedRotation;
            bodyDefinition.enableSleep = rigidbody.EnableSleep;
            bodyDefinition.isAwake = rigidbody.StartAwake;
            bodyDefinition.isBullet = rigidbody.IsBullet;
            bodyDefinition.userData = ToUserData(entity);

            rigidbody.RuntimeBodyId = b2CreateBody(m_WorldId, &bodyDefinition);
            rigidbody.RuntimeBodyCreated = b2Body_IsValid(rigidbody.RuntimeBodyId);
            rigidbody.RuntimePreviousPosition = glm::vec2(transform.Position.x, transform.Position.y);
            rigidbody.RuntimePreviousAngleRadians = glm::radians(transform.Rotation.z);

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

            b2Body_SetLinearDamping(rigidbody.RuntimeBodyId, rigidbody.LinearDamping);
            b2Body_SetAngularDamping(rigidbody.RuntimeBodyId, rigidbody.AngularDamping);
            b2Body_SetGravityScale(rigidbody.RuntimeBodyId, rigidbody.GravityScale);
            b2Body_SetFixedRotation(rigidbody.RuntimeBodyId, rigidbody.FixedRotation);
            b2Body_EnableSleep(rigidbody.RuntimeBodyId, rigidbody.EnableSleep);
            b2Body_SetBullet(rigidbody.RuntimeBodyId, rigidbody.IsBullet);

            const b2Transform runtimeTransform = b2Body_GetTransform(rigidbody.RuntimeBodyId);
            const glm::vec2 authoringPosition(transform.Position.x, transform.Position.y);
            const float authoringAngleRadians = glm::radians(transform.Rotation.z);
            const glm::vec2 runtimePosition(runtimeTransform.p.x, runtimeTransform.p.y);
            const float runtimeAngleRadians = b2Rot_GetAngle(runtimeTransform.q);

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
                    continue;
                }

                const float inverseStep = 1.0f / std::max(fixedDeltaTime, kMinimumStepDelta);
                const glm::vec2 targetLinearVelocity = authoredPositionDelta * inverseStep;
                const float targetAngularVelocity = authoredAngleDelta * inverseStep;
                b2Body_SetLinearVelocity(rigidbody.RuntimeBodyId, { targetLinearVelocity.x, targetLinearVelocity.y });
                b2Body_SetAngularVelocity(rigidbody.RuntimeBodyId, targetAngularVelocity);

                rigidbody.RuntimePreviousPosition = authoringPosition;
                rigidbody.RuntimePreviousAngleRadians = authoringAngleRadians;
                continue;
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

            if (rigidbody->Type == Rigidbody2DComponent::BodyType::Kinematic)
                continue;

            rigidbody->RuntimePreviousPosition = glm::vec2(transform->Position.x, transform->Position.y);
            rigidbody->RuntimePreviousAngleRadians = glm::radians(transform->Rotation.z);

            transform->Position.x = moveEvent.transform.p.x;
            transform->Position.y = moveEvent.transform.p.y;
            transform->Rotation.z = glm::degrees(b2Rot_GetAngle(moveEvent.transform.q));
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
