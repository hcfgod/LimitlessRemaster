#pragma once

// Internal header shared across the Physics2DWorld split translation units.
// NOT part of the public engine API.

#include "Physics/Physics2DWorld.h"
#include "Scene/Scene.h"
#include "Scene/Components/RenderingComponents.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <unordered_set>
#include <vector>
#include <glm/gtc/constants.hpp>

#include "Core/MathSanitize.h"

namespace Limitless::Physics2DInternal
{
    // -------------------------------------------------------------------------
    // Constants
    // -------------------------------------------------------------------------

    constexpr float kMinimumColliderExtent = 0.001f;
    constexpr float kMinimumCircleRadius = 0.001f;
    constexpr float kMinimumDynamicShapeDensity = 0.0001f;
    constexpr float kMaximumShapeDensity = 100.0f;
    constexpr float kMaximumWorldPosition = 10000.0f;
    constexpr float kMaximumColliderExtent = 1000.0f;
    constexpr float kMaximumColliderOffset = 1000.0f;
    constexpr float kMinimumStepDelta = 0.000001f;
    constexpr float kTransformSnapEpsilon = 0.0001f;

    // Maximum number of new physics bodies to create per Step() call.
    // Prevents Box2D allocator pressure when scripts instantiate many
    // entities at once. Remaining entities are deferred to subsequent frames.
    constexpr int kMaxNewBodiesPerStep = 256;

    // -------------------------------------------------------------------------
    // Utility helpers
    // -------------------------------------------------------------------------

    inline bool IsDefaultColliderCollisionLayer(uint64_t collisionLayer)
    {
        return collisionLayer == 1ull;
    }

    inline bool IsDefaultColliderCollisionMask(uint64_t collisionMask)
    {
        return collisionMask == ~0ull || collisionMask == static_cast<uint64_t>(0xFFFFFFFFu);
    }

    inline entt::entity ToEntityHandle(void* userData)
    {
        return static_cast<entt::entity>(reinterpret_cast<uintptr_t>(userData));
    }

    inline void* ToUserData(entt::entity entity)
    {
        return reinterpret_cast<void*>(static_cast<uintptr_t>(entity));
    }

#ifdef LT_ENABLE_PHYSICS2D
    inline b2BodyType ToBox2DBodyType(Rigidbody2DComponent::BodyType type)
    {
        switch (type)
        {
            case Rigidbody2DComponent::BodyType::Static: return b2_staticBody;
            case Rigidbody2DComponent::BodyType::Kinematic: return b2_kinematicBody;
            case Rigidbody2DComponent::BodyType::Dynamic:
            default: return b2_dynamicBody;
        }
    }
#endif

    inline float ExtractZRotationRadians(const glm::mat4& transformMatrix)
    {
        return std::atan2(transformMatrix[1][0], transformMatrix[0][0]);
    }

    struct Pose2D
    {
        glm::vec2 Position = glm::vec2(0.0f);
        float AngleRadians = 0.0f;
    };

    inline Pose2D GetEntityWorldPose2D(const Scene& scene, entt::entity entity, const TransformComponent& transform)
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

    inline void SetEntityLocalPoseFromWorld2D(const Scene& scene,
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

    inline bool IsEntityAssignedToWorld(const Scene& scene,
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

    // -------------------------------------------------------------------------
    // Hashing helpers
    // -------------------------------------------------------------------------

    inline uint64_t HashCombine64(uint64_t seed, uint64_t value)
    {
        // 64-bit hash-combine variant suitable for incremental content hashes.
        seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
        return seed;
    }

    inline uint64_t HashFloat(float value)
    {
        return static_cast<uint64_t>(std::bit_cast<uint32_t>(value));
    }

    inline uint64_t HashPoints(uint64_t seed, const std::vector<glm::vec2>& points)
    {
        seed = HashCombine64(seed, static_cast<uint64_t>(points.size()));
        for (const glm::vec2& point : points)
        {
            seed = HashCombine64(seed, HashFloat(point.x));
            seed = HashCombine64(seed, HashFloat(point.y));
        }
        return seed;
    }

#ifdef LT_ENABLE_PHYSICS2D
    inline b2ShapeDef MakeShapeDefinition(float density,
                                           bool isSensor,
                                           uint64_t collisionLayer,
                                           uint64_t collisionMask,
                                           Rigidbody2DComponent::BodyType bodyType,
                                           bool updateBodyMass,
                                           uint8_t entityLayer = 0,
                                           const std::array<uint32_t, 32>* collisionMatrix = nullptr)
    {
        b2ShapeDef shapeDefinition = b2DefaultShapeDef();
        shapeDefinition.density = glm::clamp(
            SanitizeFiniteNonNegative(density, 1.0f),
            0.0f,
            kMaximumShapeDensity);
        shapeDefinition.isSensor = isSensor;
        shapeDefinition.enableContactEvents = true;

        const bool colliderHasDefaultLayer = IsDefaultColliderCollisionLayer(collisionLayer);
        const bool colliderHasDefaultMask = IsDefaultColliderCollisionMask(collisionMask);
        if (colliderHasDefaultLayer && colliderHasDefaultMask && collisionMatrix != nullptr && entityLayer < 32)
        {
            shapeDefinition.filter.categoryBits = (1ull << entityLayer);
            shapeDefinition.filter.maskBits = static_cast<uint64_t>((*collisionMatrix)[entityLayer]);
        }
        else
        {
            shapeDefinition.filter.categoryBits = collisionLayer;
            shapeDefinition.filter.maskBits = collisionMask;
        }

        shapeDefinition.updateBodyMass = updateBodyMass && !shapeDefinition.isSensor;
        if (bodyType == Rigidbody2DComponent::BodyType::Dynamic && shapeDefinition.updateBodyMass)
        {
            shapeDefinition.density = glm::clamp(
                shapeDefinition.density,
                kMinimumDynamicShapeDensity,
                kMaximumShapeDensity);
        }
        return shapeDefinition;
    }
#endif

    inline uint64_t BuildBodyAndShapeSignature(const Rigidbody2DComponent& rigidbody,
                                                const TransformComponent& transform,
                                                const BoxCollider2DComponent* boxCollider,
                                                const CircleCollider2DComponent* circleCollider,
                                                const PolygonCollider2DComponent* polygonCollider,
                                                const EdgeCollider2DComponent* edgeCollider,
                                                const CapsuleCollider2DComponent* capsuleCollider,
                                                uint8_t entityLayer,
                                                const std::array<uint32_t, 32>* collisionMatrix,
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
            const bool useEntityLayerFiltering =
                IsDefaultColliderCollisionLayer(boxCollider->CollisionLayer) &&
                IsDefaultColliderCollisionMask(boxCollider->CollisionMask) &&
                collisionMatrix != nullptr &&
                entityLayer < 32;
            const uint64_t resolvedCollisionLayer = useEntityLayerFiltering ? (1ull << entityLayer) : boxCollider->CollisionLayer;
            const uint64_t resolvedCollisionMask =
                useEntityLayerFiltering ? static_cast<uint64_t>((*collisionMatrix)[entityLayer]) : boxCollider->CollisionMask;
            signature = HashCombine64(signature, HashFloat(boxCollider->Offset.x));
            signature = HashCombine64(signature, HashFloat(boxCollider->Offset.y));
            signature = HashCombine64(signature, HashFloat(boxCollider->Size.x));
            signature = HashCombine64(signature, HashFloat(boxCollider->Size.y));
            signature = HashCombine64(signature, HashFloat(boxCollider->Density));
            signature = HashCombine64(signature, HashFloat(boxCollider->Friction));
            signature = HashCombine64(signature, HashFloat(boxCollider->Restitution));
            signature = HashCombine64(signature, boxCollider->IsSensor ? 1ull : 0ull);
            signature = HashCombine64(signature, resolvedCollisionLayer);
            signature = HashCombine64(signature, resolvedCollisionMask);
        }

        signature = HashCombine64(signature, circleCollider ? 1ull : 0ull);
        if (circleCollider)
        {
            const bool useEntityLayerFiltering =
                IsDefaultColliderCollisionLayer(circleCollider->CollisionLayer) &&
                IsDefaultColliderCollisionMask(circleCollider->CollisionMask) &&
                collisionMatrix != nullptr &&
                entityLayer < 32;
            const uint64_t resolvedCollisionLayer = useEntityLayerFiltering ? (1ull << entityLayer) : circleCollider->CollisionLayer;
            const uint64_t resolvedCollisionMask =
                useEntityLayerFiltering ? static_cast<uint64_t>((*collisionMatrix)[entityLayer]) : circleCollider->CollisionMask;
            signature = HashCombine64(signature, HashFloat(circleCollider->Offset.x));
            signature = HashCombine64(signature, HashFloat(circleCollider->Offset.y));
            signature = HashCombine64(signature, HashFloat(circleCollider->Radius));
            signature = HashCombine64(signature, HashFloat(circleCollider->Density));
            signature = HashCombine64(signature, HashFloat(circleCollider->Friction));
            signature = HashCombine64(signature, HashFloat(circleCollider->Restitution));
            signature = HashCombine64(signature, circleCollider->IsSensor ? 1ull : 0ull);
            signature = HashCombine64(signature, resolvedCollisionLayer);
            signature = HashCombine64(signature, resolvedCollisionMask);
        }

        signature = HashCombine64(signature, polygonCollider ? 1ull : 0ull);
        if (polygonCollider)
        {
            const bool useEntityLayerFiltering =
                IsDefaultColliderCollisionLayer(polygonCollider->CollisionLayer) &&
                IsDefaultColliderCollisionMask(polygonCollider->CollisionMask) &&
                collisionMatrix != nullptr &&
                entityLayer < 32;
            const uint64_t resolvedCollisionLayer = useEntityLayerFiltering ? (1ull << entityLayer) : polygonCollider->CollisionLayer;
            const uint64_t resolvedCollisionMask =
                useEntityLayerFiltering ? static_cast<uint64_t>((*collisionMatrix)[entityLayer]) : polygonCollider->CollisionMask;
            signature = HashCombine64(signature, HashFloat(polygonCollider->Offset.x));
            signature = HashCombine64(signature, HashFloat(polygonCollider->Offset.y));
            signature = HashPoints(signature, polygonCollider->Points);
            signature = HashCombine64(signature, HashFloat(polygonCollider->Density));
            signature = HashCombine64(signature, HashFloat(polygonCollider->Friction));
            signature = HashCombine64(signature, HashFloat(polygonCollider->Restitution));
            signature = HashCombine64(signature, polygonCollider->IsSensor ? 1ull : 0ull);
            signature = HashCombine64(signature, resolvedCollisionLayer);
            signature = HashCombine64(signature, resolvedCollisionMask);
        }

        signature = HashCombine64(signature, edgeCollider ? 1ull : 0ull);
        if (edgeCollider)
        {
            const bool useEntityLayerFiltering =
                IsDefaultColliderCollisionLayer(edgeCollider->CollisionLayer) &&
                IsDefaultColliderCollisionMask(edgeCollider->CollisionMask) &&
                collisionMatrix != nullptr &&
                entityLayer < 32;
            const uint64_t resolvedCollisionLayer = useEntityLayerFiltering ? (1ull << entityLayer) : edgeCollider->CollisionLayer;
            const uint64_t resolvedCollisionMask =
                useEntityLayerFiltering ? static_cast<uint64_t>((*collisionMatrix)[entityLayer]) : edgeCollider->CollisionMask;
            signature = HashCombine64(signature, HashFloat(edgeCollider->Offset.x));
            signature = HashCombine64(signature, HashFloat(edgeCollider->Offset.y));
            signature = HashCombine64(signature, HashFloat(edgeCollider->PointA.x));
            signature = HashCombine64(signature, HashFloat(edgeCollider->PointA.y));
            signature = HashCombine64(signature, HashFloat(edgeCollider->PointB.x));
            signature = HashCombine64(signature, HashFloat(edgeCollider->PointB.y));
            signature = HashCombine64(signature, HashFloat(edgeCollider->Friction));
            signature = HashCombine64(signature, HashFloat(edgeCollider->Restitution));
            signature = HashCombine64(signature, edgeCollider->IsSensor ? 1ull : 0ull);
            signature = HashCombine64(signature, resolvedCollisionLayer);
            signature = HashCombine64(signature, resolvedCollisionMask);
        }

        signature = HashCombine64(signature, capsuleCollider ? 1ull : 0ull);
        if (capsuleCollider)
        {
            const bool useEntityLayerFiltering =
                IsDefaultColliderCollisionLayer(capsuleCollider->CollisionLayer) &&
                IsDefaultColliderCollisionMask(capsuleCollider->CollisionMask) &&
                collisionMatrix != nullptr &&
                entityLayer < 32;
            const uint64_t resolvedCollisionLayer = useEntityLayerFiltering ? (1ull << entityLayer) : capsuleCollider->CollisionLayer;
            const uint64_t resolvedCollisionMask =
                useEntityLayerFiltering ? static_cast<uint64_t>((*collisionMatrix)[entityLayer]) : capsuleCollider->CollisionMask;
            signature = HashCombine64(signature, HashFloat(capsuleCollider->Offset.x));
            signature = HashCombine64(signature, HashFloat(capsuleCollider->Offset.y));
            signature = HashCombine64(signature, HashFloat(capsuleCollider->Size.x));
            signature = HashCombine64(signature, HashFloat(capsuleCollider->Size.y));
            signature = HashCombine64(signature, static_cast<uint64_t>(capsuleCollider->Direction));
            signature = HashCombine64(signature, HashFloat(capsuleCollider->Density));
            signature = HashCombine64(signature, HashFloat(capsuleCollider->Friction));
            signature = HashCombine64(signature, HashFloat(capsuleCollider->Restitution));
            signature = HashCombine64(signature, capsuleCollider->IsSensor ? 1ull : 0ull);
            signature = HashCombine64(signature, resolvedCollisionLayer);
            signature = HashCombine64(signature, resolvedCollisionMask);
        }

        return signature;
    }

    inline uint64_t BuildJointSignature(const Joint2DComponent& joint,
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
