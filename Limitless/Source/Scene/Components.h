#pragma once

#include "Assets/MaterialAsset.h"
#include "Assets/AudioClipAsset.h"
#include "Assets/TextureAsset.h"
#include "Graphics/Font.h"
#include "Scripting/ScriptableEntity.h"
#include "Scripting/ScriptProperty.h"
#include "EnTT/entt.hpp"

#include <glm/glm.hpp>
#ifdef LT_ENABLE_PHYSICS2D
    #include <box2d/box2d.h>
#endif
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Limitless
{
    // -----------------------------------------------------------------------------
    // Core ECS components for the scene system.
    // Unity-style: each component is a plain struct of data.
    // -----------------------------------------------------------------------------

    /// Display name in the hierarchy. Required for all entities.
    struct TagComponent
    {
        std::string Tag = "Entity";
    };

    /// Position, rotation (euler degrees), and scale. Used for rendering and hierarchy.
    struct TransformComponent
    {
        glm::vec3 Position = glm::vec3(0.0f);
        glm::vec3 Rotation = glm::vec3(0.0f); ///< Euler angles in degrees (X=pitch, Y=yaw, Z=roll)
        glm::vec3 Scale = glm::vec3(1.0f);

        glm::mat4 GetLocalMatrix() const;
    };

    /// Optional parent for hierarchy. Entities without this are root-level.
    struct HierarchyComponent
    {
        /// Parent entity. Use entt::null for no parent (root).
        entt::entity Parent = entt::null;

        /// Relative sibling order under the current parent (lower renders/appears first).
        int32_t SiblingOrder = 0;
    };

    /// Renders a 2D sprite (quad). Size comes from TransformComponent::Scale.
    /// TextureKey is empty for color-only; non-empty for textured sprites (e.g. "Assets/Textures/sissy.jpg").
    /// CachedTexture holds a reference to keep the asset alive (avoids per-frame reload / GC).
    /// Used with Renderer2D for the viewport.
    struct SpriteComponent
    {
        std::string TextureKey;  ///< Asset key for texture; empty = color-only
        Assets::TextureAsset::Ptr CachedTexture;  ///< Runtime cache; keeps asset alive
        bool TextureLoadAttempted = false; ///< Prevents per-frame retry/log spam when texture is missing
        glm::vec4 Color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    };

    /// Renders runtime text using an MSDF atlas generated from the font file path.
    struct TextComponent
    {
        enum class RenderSpace
        {
            World = 0,
            Screen = 1
        };
        enum class ScreenAnchor
        {
            Center = 0,
            TopLeft = 1,
            TopCenter = 2,
            TopRight = 3,
            MiddleLeft = 4,
            MiddleRight = 5,
            BottomLeft = 6,
            BottomCenter = 7,
            BottomRight = 8
        };

        std::string Text = "Text";
        std::string FontFilePath; ///< Relative or absolute font file path.
        Font::Ptr CachedFont;
        bool FontLoadAttempted = false;
        float FontSize = 32.0f;
        glm::vec4 Color = glm::vec4(1.0f);
        RenderSpace Space = RenderSpace::World;
        ScreenAnchor Anchor = ScreenAnchor::Center;
    };

    /// Optional material reference (Unity-style).
    /// When set, rendering should prefer the material's bound resources over Sprite defaults.
    struct MaterialComponent
    {
        std::string MaterialKey; ///< Asset key for material (example: "Assets/Materials/MyMaterial.material.json")
        Assets::MaterialAsset::Ptr CachedMaterial; ///< Runtime cache; keeps asset alive
        bool MaterialLoadAttempted = false; ///< Prevents per-frame retry/log spam when material is missing
    };

    struct DirectionalLight2DComponent
    {
        bool Enabled = true;
        glm::vec3 Color = glm::vec3(1.0f);
        float Intensity = 1.0f;
        bool UseEntityRotation = true;
        glm::vec2 Direction = glm::vec2(0.0f, -1.0f);
        bool CastShadows = true;
        float ShadowStrength = 1.0f;
        float ShadowSoftness = 1.0f;
        int ShadowSamples = 8;
        float ShadowDistance = 25.0f;
        float ShadowBias = 0.02f;

        // Runtime-only state (not serialized).
        glm::vec2 RuntimeResolvedDirection = glm::vec2(0.0f, -1.0f);
    };

    struct PointLight2DComponent
    {
        bool Enabled = true;
        glm::vec3 Color = glm::vec3(1.0f);
        float Intensity = 1.0f;
        float Radius = 5.0f;
        float Falloff = 2.0f;
        bool CastShadows = true;
        float ShadowStrength = 1.0f;
        float ShadowSoftness = 1.0f;
        int ShadowSamples = 8;
        float ShadowBias = 0.0015f;

        // Runtime-only state (not serialized).
        glm::vec2 RuntimeViewportPosition = glm::vec2(0.0f);
        float RuntimeViewportRadius = 0.0f;
    };

    struct ShadowOccluder2DComponent
    {
        enum class SourceMode
        {
            ManualPolygon = 0,
            PhysicsCollider = 1
        };

        bool Enabled = true;
        SourceMode Source = SourceMode::ManualPolygon;
        bool Closed = true;
        std::vector<glm::vec2> PolygonPoints = {
            glm::vec2(-0.5f, -0.5f),
            glm::vec2(0.5f, -0.5f),
            glm::vec2(0.5f, 0.5f),
            glm::vec2(-0.5f, 0.5f)
        };
        float Extrusion = 0.0f;

        // Runtime-only state (not serialized).
        std::vector<glm::vec2> RuntimeResolvedPolygonPoints;
        uint64_t RuntimeGeometryRevision = 0;
    };

    /// Optional audio source reference (Unity-style).
    /// Supports simple non-spatial clip playback with Play On Start and loop.
    struct AudioSourceComponent
    {
        std::string AudioClipKey; ///< Asset key for audio clip (example: "Assets/Audio/MyClip.wav")
        float Volume = 1.0f;
        bool PlayOnStart = true;
        bool Loop = false;
        bool Muted = false;

        // Runtime-only state (not serialized).
        uint32_t RuntimeVoiceId = 0;
        bool RuntimePlaybackStarted = false;
    };

    /// Entity camera settings used to build the active gameplay camera in Play Mode.
    /// Unity-style: attach to any entity and mark one as Primary.
    struct CameraComponent
    {
        enum class ProjectionType
        {
            Orthographic2D = 0,
            Perspective3D = 1
        };

        ProjectionType Projection = ProjectionType::Orthographic2D;
        bool IsPrimary = true;

        // Orthographic settings.
        float Zoom = 1.0f;
        float NearPlane = -1.0f;
        float FarPlane = 1.0f;

        // Perspective settings.
        float FieldOfViewYDegrees = 60.0f;
    };

    struct Rigidbody2DComponent
    {
        enum class BodyType
        {
            Static = 0,
            Dynamic = 1,
            Kinematic = 2
        };

        BodyType Type = BodyType::Dynamic;
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

#ifdef LT_ENABLE_PHYSICS2D
        b2BodyId RuntimeBodyId = b2_nullBodyId;
#endif
        bool RuntimeBodyCreated = false;
        glm::vec2 RuntimePreviousPosition = glm::vec2(0.0f);
        float RuntimePreviousAngleRadians = 0.0f;
        glm::vec2 RuntimeRenderPreviousPosition = glm::vec2(0.0f);
        float RuntimeRenderPreviousAngleRadians = 0.0f;
        glm::vec2 RuntimeRenderCurrentPosition = glm::vec2(0.0f);
        float RuntimeRenderCurrentAngleRadians = 0.0f;
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

#ifdef LT_ENABLE_PHYSICS2D
        b2ShapeId RuntimeShapeId = b2_nullShapeId;
#endif
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

#ifdef LT_ENABLE_PHYSICS2D
        b2ShapeId RuntimeShapeId = b2_nullShapeId;
#endif
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

#ifdef LT_ENABLE_PHYSICS2D
        b2JointId RuntimeJointId = b2_nullJointId;
#endif
        bool RuntimeJointCreated = false;
    };

    /// Single native C++ behavior script entry attached to an entity.
    struct NativeScriptEntry
    {
        std::string ScriptClassName;
        std::string ScriptAssetRelativePath; ///< Relative to project Assets root without extension (example: "Gameplay/Player/PlayerController")
        bool Enabled = true;
        std::unordered_map<std::string, ScriptPropertyValue> ExposedProperties;

        // Runtime-only state (not serialized).
        std::unique_ptr<ScriptableEntity> RuntimeInstance;
        bool RuntimeInitialized = false;
        uint64_t RuntimeUpdateCount = 0;
        bool RuntimeWarnedOnUpdateTransformMutation = false;

        NativeScriptEntry() = default;

        NativeScriptEntry(const NativeScriptEntry& other)
            : ScriptClassName(other.ScriptClassName),
              ScriptAssetRelativePath(other.ScriptAssetRelativePath),
              Enabled(other.Enabled),
              ExposedProperties(other.ExposedProperties),
              RuntimeInstance(nullptr),
              RuntimeInitialized(false),
              RuntimeUpdateCount(0),
              RuntimeWarnedOnUpdateTransformMutation(false)
        {
        }

        NativeScriptEntry& operator=(const NativeScriptEntry& other)
        {
            if (this == &other)
                return *this;
            ScriptClassName = other.ScriptClassName;
            ScriptAssetRelativePath = other.ScriptAssetRelativePath;
            Enabled = other.Enabled;
            ExposedProperties = other.ExposedProperties;
            RuntimeInstance.reset();
            RuntimeInitialized = false;
            RuntimeUpdateCount = 0;
            RuntimeWarnedOnUpdateTransformMutation = false;
            return *this;
        }

        NativeScriptEntry(NativeScriptEntry&&) noexcept = default;
        NativeScriptEntry& operator=(NativeScriptEntry&&) noexcept = default;
    };

    /// Native C++ behavior scripts attached to an entity (Unity-style list).
    struct NativeScriptComponent
    {
        std::vector<NativeScriptEntry> Scripts;
    };

    /// Marks an entity hierarchy root as an instance of a prefab asset.
    struct PrefabInstanceComponent
    {
        std::string PrefabAssetKey; ///< Asset key for prefab (example: "Assets/Prefabs/Player.prefab.json")
    };
}
