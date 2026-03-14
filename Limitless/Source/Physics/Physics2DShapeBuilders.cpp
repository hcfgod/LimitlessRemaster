#include "Physics/Physics2DWorldInternal.h"
#include "Core/Debug/Log.h"

namespace Limitless
{
    using namespace Physics2DInternal;

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
            auto* polygonCollider = registry.try_get<PolygonCollider2DComponent>(entity);
            auto* edgeCollider = registry.try_get<EdgeCollider2DComponent>(entity);
            auto* capsuleCollider = registry.try_get<CapsuleCollider2DComponent>(entity);
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
                    if (polygonCollider)
                    {
                        polygonCollider->RuntimeShapeId = kNullPhysics2DShape;
                        polygonCollider->RuntimeShapeCreated = false;
                    }
                    if (edgeCollider)
                    {
                        edgeCollider->RuntimeShapeId = kNullPhysics2DShape;
                        edgeCollider->RuntimeShapeCreated = false;
                    }
                    if (capsuleCollider)
                    {
                        capsuleCollider->RuntimeShapeId = kNullPhysics2DShape;
                        capsuleCollider->RuntimeShapeCreated = false;
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
                if (polygonCollider)
                {
                    polygonCollider->RuntimeShapeId = kNullPhysics2DShape;
                    polygonCollider->RuntimeShapeCreated = false;
                }
                if (edgeCollider)
                {
                    edgeCollider->RuntimeShapeId = kNullPhysics2DShape;
                    edgeCollider->RuntimeShapeCreated = false;
                }
                if (capsuleCollider)
                {
                    capsuleCollider->RuntimeShapeId = kNullPhysics2DShape;
                    capsuleCollider->RuntimeShapeCreated = false;
                }
                continue;
            }

            auto& transform = bodyView.get<TransformComponent>(entity);
            const auto* tagComponent = registry.try_get<TagComponent>(entity);
            const uint8_t entityLayer = tagComponent ? tagComponent->Layer : static_cast<uint8_t>(0);
            const uint64_t desiredBodyAndShapeSignature =
                BuildBodyAndShapeSignature(rigidbody,
                                            transform,
                                            boxCollider,
                                            circleCollider,
                                            polygonCollider,
                                            edgeCollider,
                                            capsuleCollider,
                                            entityLayer,
                                            &m_CollisionMatrix,
                                            m_SceneWorldSlot);

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
                if (polygonCollider)
                {
                    polygonCollider->RuntimeShapeId = kNullPhysics2DShape;
                    polygonCollider->RuntimeShapeCreated = false;
                }
                if (edgeCollider)
                {
                    edgeCollider->RuntimeShapeId = kNullPhysics2DShape;
                    edgeCollider->RuntimeShapeCreated = false;
                }
                if (capsuleCollider)
                {
                    capsuleCollider->RuntimeShapeId = kNullPhysics2DShape;
                    capsuleCollider->RuntimeShapeCreated = false;
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

                b2ShapeDef shapeDefinition = MakeShapeDefinition(boxCollider->Density,
                                                                  boxCollider->IsSensor,
                                                                  boxCollider->CollisionLayer,
                                                                  boxCollider->CollisionMask,
                                                                  rigidbody.Type,
                                                                  true,
                                                                  entityLayer,
                                                                  &m_CollisionMatrix);
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

                b2ShapeDef shapeDefinition = MakeShapeDefinition(circleCollider->Density,
                                                                  circleCollider->IsSensor,
                                                                  circleCollider->CollisionLayer,
                                                                  circleCollider->CollisionMask,
                                                                  rigidbody.Type,
                                                                  true,
                                                                  entityLayer,
                                                                  &m_CollisionMatrix);
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

            if (polygonCollider)
            {
                const float safeScaleX = SanitizeFinite(transform.Scale.x, 1.0f);
                const float safeScaleY = SanitizeFinite(transform.Scale.y, 1.0f);
                const float safeOffsetX = SanitizeFinite(polygonCollider->Offset.x, 0.0f);
                const float safeOffsetY = SanitizeFinite(polygonCollider->Offset.y, 0.0f);
                const size_t pointCount = std::min<size_t>(polygonCollider->Points.size(), kPhysics2DPolygonMaxPoints);
                std::array<b2Vec2, kPhysics2DPolygonMaxPoints> polygonPoints{};
                bool hadInvalidPolygonParameters = (safeScaleX != transform.Scale.x) ||
                                                   (safeScaleY != transform.Scale.y) ||
                                                   (safeOffsetX != polygonCollider->Offset.x) ||
                                                   (safeOffsetY != polygonCollider->Offset.y) ||
                                                   (pointCount != polygonCollider->Points.size());
                for (size_t pointIndex = 0; pointIndex < pointCount; ++pointIndex)
                {
                    const glm::vec2& authoredPoint = polygonCollider->Points[pointIndex];
                    const float safePointX = SanitizeFinite(authoredPoint.x, 0.0f);
                    const float safePointY = SanitizeFinite(authoredPoint.y, 0.0f);
                    hadInvalidPolygonParameters = hadInvalidPolygonParameters ||
                                                  (safePointX != authoredPoint.x) ||
                                                  (safePointY != authoredPoint.y);
                    polygonPoints[pointIndex] = {
                        glm::clamp((safeOffsetX + safePointX) * safeScaleX, -kMaximumColliderOffset, kMaximumColliderOffset),
                        glm::clamp((safeOffsetY + safePointY) * safeScaleY, -kMaximumColliderOffset, kMaximumColliderOffset)
                    };
                }

                if (hadInvalidPolygonParameters && !rigidbody.RuntimeWarnedInvalidBodyParameters)
                {
                    const auto* tag = registry.try_get<TagComponent>(entity);
                    LT_WARN("Physics2D: sanitized invalid polygon collider parameters for entity '{}' (pointCount={}, offset=({}, {}), scale=({}, {})).",
                            tag ? tag->Tag : "Entity",
                            polygonCollider->Points.size(),
                            polygonCollider->Offset.x,
                            polygonCollider->Offset.y,
                            transform.Scale.x,
                            transform.Scale.y);
                    rigidbody.RuntimeWarnedInvalidBodyParameters = true;
                }

                polygonCollider->RuntimeShapeId = kNullPhysics2DShape;
                polygonCollider->RuntimeShapeCreated = false;
                if (pointCount >= 3)
                {
                    const b2Hull hull = b2ComputeHull(polygonPoints.data(), static_cast<int>(pointCount));
                    if (hull.count >= 3 && b2ValidateHull(&hull))
                    {
                        b2ShapeDef shapeDefinition = MakeShapeDefinition(polygonCollider->Density,
                                                                          polygonCollider->IsSensor,
                                                                          polygonCollider->CollisionLayer,
                                                                          polygonCollider->CollisionMask,
                                                                          rigidbody.Type,
                                                                          true,
                                                                          entityLayer,
                                                                          &m_CollisionMatrix);
                        const b2Polygon polygonShape = b2MakePolygon(&hull, 0.0f);
                        polygonCollider->RuntimeShapeId = b2CreatePolygonShape(rigidbody.RuntimeBodyId, &shapeDefinition, &polygonShape);
                        polygonCollider->RuntimeShapeCreated = b2Shape_IsValid(polygonCollider->RuntimeShapeId);
                        if (polygonCollider->RuntimeShapeCreated)
                        {
                            b2Shape_SetFriction(polygonCollider->RuntimeShapeId, SanitizeFiniteNonNegative(polygonCollider->Friction, 0.5f));
                            b2Shape_SetRestitution(
                                polygonCollider->RuntimeShapeId,
                                glm::clamp(SanitizeFiniteNonNegative(polygonCollider->Restitution, 0.0f), 0.0f, 1.0f));
                        }
                    }
                }
            }

            if (edgeCollider)
            {
                const float safeScaleX = SanitizeFinite(transform.Scale.x, 1.0f);
                const float safeScaleY = SanitizeFinite(transform.Scale.y, 1.0f);
                const float safeOffsetX = SanitizeFinite(edgeCollider->Offset.x, 0.0f);
                const float safeOffsetY = SanitizeFinite(edgeCollider->Offset.y, 0.0f);
                const float safePointAX = SanitizeFinite(edgeCollider->PointA.x, -0.5f);
                const float safePointAY = SanitizeFinite(edgeCollider->PointA.y, 0.0f);
                const float safePointBX = SanitizeFinite(edgeCollider->PointB.x, 0.5f);
                const float safePointBY = SanitizeFinite(edgeCollider->PointB.y, 0.0f);
                const b2Vec2 pointA = {
                    glm::clamp((safeOffsetX + safePointAX) * safeScaleX, -kMaximumColliderOffset, kMaximumColliderOffset),
                    glm::clamp((safeOffsetY + safePointAY) * safeScaleY, -kMaximumColliderOffset, kMaximumColliderOffset)
                };
                const b2Vec2 pointB = {
                    glm::clamp((safeOffsetX + safePointBX) * safeScaleX, -kMaximumColliderOffset, kMaximumColliderOffset),
                    glm::clamp((safeOffsetY + safePointBY) * safeScaleY, -kMaximumColliderOffset, kMaximumColliderOffset)
                };
                const bool hadInvalidEdgeParameters = (safeScaleX != transform.Scale.x) ||
                                                      (safeScaleY != transform.Scale.y) ||
                                                      (safeOffsetX != edgeCollider->Offset.x) ||
                                                      (safeOffsetY != edgeCollider->Offset.y) ||
                                                      (safePointAX != edgeCollider->PointA.x) ||
                                                      (safePointAY != edgeCollider->PointA.y) ||
                                                      (safePointBX != edgeCollider->PointB.x) ||
                                                      (safePointBY != edgeCollider->PointB.y);
                if (hadInvalidEdgeParameters && !rigidbody.RuntimeWarnedInvalidBodyParameters)
                {
                    const auto* tag = registry.try_get<TagComponent>(entity);
                    LT_WARN("Physics2D: sanitized invalid edge collider parameters for entity '{}' (offset=({}, {}), pointA=({}, {}), pointB=({}, {}), scale=({}, {})).",
                            tag ? tag->Tag : "Entity",
                            edgeCollider->Offset.x,
                            edgeCollider->Offset.y,
                            edgeCollider->PointA.x,
                            edgeCollider->PointA.y,
                            edgeCollider->PointB.x,
                            edgeCollider->PointB.y,
                            transform.Scale.x,
                            transform.Scale.y);
                    rigidbody.RuntimeWarnedInvalidBodyParameters = true;
                }

                edgeCollider->RuntimeShapeId = kNullPhysics2DShape;
                edgeCollider->RuntimeShapeCreated = false;
                if (std::abs(pointA.x - pointB.x) > kTransformSnapEpsilon || std::abs(pointA.y - pointB.y) > kTransformSnapEpsilon)
                {
                    b2ShapeDef shapeDefinition = MakeShapeDefinition(0.0f,
                                                                      edgeCollider->IsSensor,
                                                                      edgeCollider->CollisionLayer,
                                                                      edgeCollider->CollisionMask,
                                                                      rigidbody.Type,
                                                                      false,
                                                                      entityLayer,
                                                                      &m_CollisionMatrix);
                    b2Segment segmentShape{};
                    segmentShape.point1 = pointA;
                    segmentShape.point2 = pointB;
                    edgeCollider->RuntimeShapeId = b2CreateSegmentShape(rigidbody.RuntimeBodyId, &shapeDefinition, &segmentShape);
                    edgeCollider->RuntimeShapeCreated = b2Shape_IsValid(edgeCollider->RuntimeShapeId);
                    if (edgeCollider->RuntimeShapeCreated)
                    {
                        b2Shape_SetFriction(edgeCollider->RuntimeShapeId, SanitizeFiniteNonNegative(edgeCollider->Friction, 0.5f));
                        b2Shape_SetRestitution(
                            edgeCollider->RuntimeShapeId,
                            glm::clamp(SanitizeFiniteNonNegative(edgeCollider->Restitution, 0.0f), 0.0f, 1.0f));
                    }
                }
            }

            if (capsuleCollider)
            {
                const float safeScaleX = SanitizeFinite(transform.Scale.x, 1.0f);
                const float safeScaleY = SanitizeFinite(transform.Scale.y, 1.0f);
                const float safeOffsetX = SanitizeFinite(capsuleCollider->Offset.x, 0.0f);
                const float safeOffsetY = SanitizeFinite(capsuleCollider->Offset.y, 0.0f);
                const float safeSizeX = SanitizeFiniteNonNegative(capsuleCollider->Size.x, 1.0f);
                const float safeSizeY = SanitizeFiniteNonNegative(capsuleCollider->Size.y, 2.0f);
                const float scaledOffsetX = glm::clamp(safeOffsetX * safeScaleX, -kMaximumColliderOffset, kMaximumColliderOffset);
                const float scaledOffsetY = glm::clamp(safeOffsetY * safeScaleY, -kMaximumColliderOffset, kMaximumColliderOffset);
                const float scaledSizeX = glm::clamp(
                    std::max(kMinimumColliderExtent, safeSizeX * std::abs(safeScaleX)),
                    kMinimumColliderExtent,
                    kMaximumColliderExtent * 2.0f);
                const float scaledSizeY = glm::clamp(
                    std::max(kMinimumColliderExtent, safeSizeY * std::abs(safeScaleY)),
                    kMinimumColliderExtent,
                    kMaximumColliderExtent * 2.0f);
                const bool hadInvalidCapsuleParameters = (safeScaleX != transform.Scale.x) ||
                                                         (safeScaleY != transform.Scale.y) ||
                                                         (safeOffsetX != capsuleCollider->Offset.x) ||
                                                         (safeOffsetY != capsuleCollider->Offset.y) ||
                                                         (safeSizeX != capsuleCollider->Size.x) ||
                                                         (safeSizeY != capsuleCollider->Size.y);
                if (hadInvalidCapsuleParameters && !rigidbody.RuntimeWarnedInvalidBodyParameters)
                {
                    const auto* tag = registry.try_get<TagComponent>(entity);
                    LT_WARN("Physics2D: sanitized invalid capsule collider parameters for entity '{}' (size=({}, {}), offset=({}, {}), scale=({}, {})).",
                            tag ? tag->Tag : "Entity",
                            capsuleCollider->Size.x,
                            capsuleCollider->Size.y,
                            capsuleCollider->Offset.x,
                            capsuleCollider->Offset.y,
                            transform.Scale.x,
                            transform.Scale.y);
                    rigidbody.RuntimeWarnedInvalidBodyParameters = true;
                }

                const bool horizontal = capsuleCollider->Direction == CapsuleCollider2DComponent::Orientation::Horizontal;
                const float radius = glm::clamp(
                    horizontal ? scaledSizeY * 0.5f : scaledSizeX * 0.5f,
                    kMinimumCircleRadius,
                    kMaximumColliderExtent);
                const float centerDistance = std::max(0.0f, (horizontal ? scaledSizeX : scaledSizeY) - radius * 2.0f);
                b2Capsule capsuleShape{};
                if (horizontal)
                {
                    capsuleShape.center1 = { scaledOffsetX - centerDistance * 0.5f, scaledOffsetY };
                    capsuleShape.center2 = { scaledOffsetX + centerDistance * 0.5f, scaledOffsetY };
                }
                else
                {
                    capsuleShape.center1 = { scaledOffsetX, scaledOffsetY - centerDistance * 0.5f };
                    capsuleShape.center2 = { scaledOffsetX, scaledOffsetY + centerDistance * 0.5f };
                }
                capsuleShape.radius = radius;

                b2ShapeDef shapeDefinition = MakeShapeDefinition(capsuleCollider->Density,
                                                                  capsuleCollider->IsSensor,
                                                                  capsuleCollider->CollisionLayer,
                                                                  capsuleCollider->CollisionMask,
                                                                  rigidbody.Type,
                                                                  true,
                                                                  entityLayer,
                                                                  &m_CollisionMatrix);
                capsuleCollider->RuntimeShapeId = b2CreateCapsuleShape(rigidbody.RuntimeBodyId, &shapeDefinition, &capsuleShape);
                capsuleCollider->RuntimeShapeCreated = b2Shape_IsValid(capsuleCollider->RuntimeShapeId);
                if (capsuleCollider->RuntimeShapeCreated)
                {
                    b2Shape_SetFriction(capsuleCollider->RuntimeShapeId, SanitizeFiniteNonNegative(capsuleCollider->Friction, 0.5f));
                    b2Shape_SetRestitution(
                        capsuleCollider->RuntimeShapeId,
                        glm::clamp(SanitizeFiniteNonNegative(capsuleCollider->Restitution, 0.0f), 0.0f, 1.0f));
                }
            }

            rigidbody.RuntimeBodyAndShapeSignature = desiredBodyAndShapeSignature;
        }

#else
        (void)scene;
#endif
        return newBodiesCreatedThisStep;
    }
}
