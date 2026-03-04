#pragma once

#include "EnTT/entt.hpp"

#ifdef LT_ENABLE_PHYSICS2D
    #include <box2d/box2d.h>
#endif

#include <cstdint>
#include <glm/glm.hpp>

namespace Limitless
{
    // -------------------------------------------------------------------------
    // Physics runtime handle types.
    //
    // These ensure that component structs have an identical binary layout
    // regardless of whether LT_ENABLE_PHYSICS2D is defined. This is critical
    // because ScriptCore.dll and the engine share the same EnTT registry but
    // may compile components with different preprocessor settings.
    //
    // When physics is enabled the handles are direct typedefs to Box2D's ID
    // types. When disabled they are layout-compatible opaque POD structs.
    // -------------------------------------------------------------------------

#ifdef LT_ENABLE_PHYSICS2D
    using Physics2DBodyHandle = b2BodyId;
    using Physics2DShapeHandle = b2ShapeId;
    using Physics2DJointHandle = b2JointId;

    inline constexpr Physics2DBodyHandle kNullPhysics2DBody = { 0, 0, 0 };
    inline constexpr Physics2DShapeHandle kNullPhysics2DShape = { 0, 0, 0 };
    inline constexpr Physics2DJointHandle kNullPhysics2DJoint = { 0, 0, 0 };
#else
    /// Opaque stand-in for b2BodyId (8 bytes: int32_t + uint16_t + uint16_t).
    struct Physics2DBodyHandle
    {
        int32_t Index = 0;
        uint16_t World = 0;
        uint16_t Generation = 0;
    };

    /// Opaque stand-in for b2ShapeId (same layout).
    struct Physics2DShapeHandle
    {
        int32_t Index = 0;
        uint16_t World = 0;
        uint16_t Generation = 0;
    };

    /// Opaque stand-in for b2JointId (same layout).
    struct Physics2DJointHandle
    {
        int32_t Index = 0;
        uint16_t World = 0;
        uint16_t Generation = 0;
    };

    inline constexpr Physics2DBodyHandle kNullPhysics2DBody{};
    inline constexpr Physics2DShapeHandle kNullPhysics2DShape{};
    inline constexpr Physics2DJointHandle kNullPhysics2DJoint{};
#endif

    struct Rigidbody2DComponent
    {
        enum class BodyType
        {
            Static = 0,
            Dynamic = 1,
            Kinematic = 2
        };

        BodyType Type = BodyType::Dynamic;
        bool FreezePositionX = false;
        bool FreezePositionY = false;
        // Legacy compatibility field (older scenes/scripts may still set this).
        // Inspector labels this as "Freeze Rotation".
        bool FixedRotation = false;
        bool UseCCD = false;
        bool EnableSleep = true;
        bool StartAwake = true;
        bool Interpolate = true;
        bool HighContactQuality = false;
        int ExtraSolverSubSteps = 0;
        float GravityScale = 1.0f;
        float LinearDamping = 0.0f;
        float AngularDamping = 0.01f;
        // Unity-style script API. Scripts call Set*/Get* helpers on this component.
        // Physics2DWorld consumes pending writes each fixed step.
        glm::vec2 RuntimePendingLinearVelocity = glm::vec2(0.0f);
        bool RuntimeHasPendingLinearVelocity = false;
        float RuntimePendingLinearVelocityX = 0.0f;
        bool RuntimeHasPendingLinearVelocityX = false;
        float RuntimePendingLinearVelocityY = 0.0f;
        bool RuntimeHasPendingLinearVelocityY = false;
        glm::vec2 RuntimeLinearVelocity = glm::vec2(0.0f);
        int32_t RuntimeContactCount = 0;
        int32_t RuntimeContactCountExcludingSensors = 0;
        bool RuntimeWarnedInvalidBodyParameters = false;

        glm::vec2 GetLinearVelocity() const
        {
            return RuntimeLinearVelocity;
        }

        void SetLinearVelocity(const glm::vec2& velocity)
        {
            RuntimePendingLinearVelocity = velocity;
            RuntimeHasPendingLinearVelocity = true;
            RuntimeHasPendingLinearVelocityX = false;
            RuntimeHasPendingLinearVelocityY = false;
        }

        void SetLinearVelocityX(float velocityX)
        {
            RuntimePendingLinearVelocityX = velocityX;
            RuntimeHasPendingLinearVelocityX = true;
        }

        void SetLinearVelocityY(float velocityY)
        {
            RuntimePendingLinearVelocityY = velocityY;
            RuntimeHasPendingLinearVelocityY = true;
        }

        void AddLinearVelocity(const glm::vec2& deltaVelocity)
        {
            SetLinearVelocity(RuntimeLinearVelocity + deltaVelocity);
        }

        int32_t GetContactCount(bool includeSensorContacts = true) const
        {
            return includeSensorContacts ? RuntimeContactCount : RuntimeContactCountExcludingSensors;
        }

        bool IsRotationLocked() const
        {
            return FixedRotation;
        }

        Physics2DBodyHandle RuntimeBodyId = kNullPhysics2DBody;
        bool RuntimeBodyCreated = false;
        glm::vec2 RuntimePreviousPosition = glm::vec2(0.0f);
        float RuntimePreviousAngleRadians = 0.0f;
        glm::vec2 RuntimeRenderPreviousPosition = glm::vec2(0.0f);
        float RuntimeRenderPreviousAngleRadians = 0.0f;
        glm::vec2 RuntimeRenderCurrentPosition = glm::vec2(0.0f);
        float RuntimeRenderCurrentAngleRadians = 0.0f;
        // Authoring/runtime world-slot fields are appended to preserve offsets
        // for existing ScriptCore builds that still reference older layouts.
        uint16_t PhysicsWorldSlot = 0;
        uint16_t RuntimeWorldSlot = 0;
        uint64_t RuntimeBodyAndShapeSignature = 0;
    };

    struct BoxCollider2DComponent
    {
        glm::vec2 Offset = glm::vec2(0.0f);
        glm::vec2 Size = glm::vec2(1.0f, 1.0f);
        float Density = 1.0f;
        float Friction = 0.5f;
        float Restitution = 0.0f;
        bool IsSensor = false;
        uint64_t CollisionLayer = 1ull;
        uint64_t CollisionMask = ~0ull;

        Physics2DShapeHandle RuntimeShapeId = kNullPhysics2DShape;
        bool RuntimeShapeCreated = false;
    };

    struct CircleCollider2DComponent
    {
        glm::vec2 Offset = glm::vec2(0.0f);
        float Radius = 0.5f;
        float Density = 1.0f;
        float Friction = 0.5f;
        float Restitution = 0.0f;
        bool IsSensor = false;
        uint64_t CollisionLayer = 1ull;
        uint64_t CollisionMask = ~0ull;

        Physics2DShapeHandle RuntimeShapeId = kNullPhysics2DShape;
        bool RuntimeShapeCreated = false;
    };

    struct Joint2DComponent
    {
        enum class JointType
        {
            Distance = 0,
            Revolute = 1,
            Prismatic = 2
        };

        JointType Type = JointType::Distance;
        entt::entity ConnectedEntity = entt::null;
        bool CollideConnected = false;
        glm::vec2 AnchorA = glm::vec2(0.0f);
        glm::vec2 AnchorB = glm::vec2(0.0f);
        glm::vec2 Axis = glm::vec2(1.0f, 0.0f);
        bool EnableLimit = false;
        glm::vec2 Limits = glm::vec2(-1.0f, 1.0f);
        bool EnableMotor = false;
        float MotorSpeed = 0.0f;
        float MaxMotorForceOrTorque = 10.0f;
        bool EnableSpring = false;
        float Hertz = 5.0f;
        float DampingRatio = 0.7f;

        Physics2DJointHandle RuntimeJointId = kNullPhysics2DJoint;
        bool RuntimeJointCreated = false;
        uint16_t RuntimeWorldSlot = 0;
        uint64_t RuntimeJointSignature = 0;
    };
}
