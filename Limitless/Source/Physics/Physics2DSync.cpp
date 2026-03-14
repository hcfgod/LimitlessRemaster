#include "Physics/Physics2DWorldInternal.h"
#include "Core/Debug/Log.h"

#include <unordered_map>

namespace Limitless
{
    using namespace Physics2DInternal;

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
}
