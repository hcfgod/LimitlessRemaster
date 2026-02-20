#pragma once

#include "Assets/MaterialAsset.h"
#include "Assets/AudioClipAsset.h"
#include "Assets/AnimationClipAsset.h"
#include "Assets/AnimatorControllerAsset.h"
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
    // -------------------------------------------------------------------------
    // Physics runtime handle types.
    //
    // These ensure that component structs have an identical binary layout
    // regardless of whether LT_ENABLE_PHYSICS2D is defined. This is critical
    // because ScriptCore.dll and the engine share the same EnTT registry but
    // may compile Components.h with different preprocessor settings.
    //
    // When physics is enabled the handles are direct typedefs to Box2D's ID
    // types. When disabled they are layout-compatible opaque POD structs.
    // -------------------------------------------------------------------------

#ifdef LT_ENABLE_PHYSICS2D
    using Physics2DBodyHandle  = b2BodyId;
    using Physics2DShapeHandle = b2ShapeId;
    using Physics2DJointHandle = b2JointId;

    inline constexpr Physics2DBodyHandle  kNullPhysics2DBody  = { 0, 0, 0 };
    inline constexpr Physics2DShapeHandle kNullPhysics2DShape = { 0, 0, 0 };
    inline constexpr Physics2DJointHandle kNullPhysics2DJoint = { 0, 0, 0 };
#else
    /// Opaque stand-in for b2BodyId  (8 bytes: int32_t + uint16_t + uint16_t).
    struct Physics2DBodyHandle  { int32_t Index = 0; uint16_t World = 0; uint16_t Generation = 0; };
    /// Opaque stand-in for b2ShapeId (same layout).
    struct Physics2DShapeHandle { int32_t Index = 0; uint16_t World = 0; uint16_t Generation = 0; };
    /// Opaque stand-in for b2JointId (same layout).
    struct Physics2DJointHandle { int32_t Index = 0; uint16_t World = 0; uint16_t Generation = 0; };

    inline constexpr Physics2DBodyHandle  kNullPhysics2DBody{};
    inline constexpr Physics2DShapeHandle kNullPhysics2DShape{};
    inline constexpr Physics2DJointHandle kNullPhysics2DJoint{};
#endif

    // -----------------------------------------------------------------------------
    // Core ECS components for the scene system.
    // Unity-style: each component is a plain struct of data.
    // -----------------------------------------------------------------------------

    /// Display name in the hierarchy. Required for all entities.
    struct TagComponent
    {
        std::string Tag = "Entity";
        bool Enabled = true; ///< Unity-style active state. Disabled entities do not update or render.
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

    /// Marks an entity as a UI canvas root.
    /// Child entities under the canvas are rendered in UI space.
    struct CanvasComponent
    {
        enum class RenderMode
        {
            ScreenSpace = 0,
            WorldSpace = 1
        };

        RenderMode Mode = RenderMode::ScreenSpace;
        int32_t SortOrder = 0;
        glm::vec2 ReferenceResolution = glm::vec2(1920.0f, 1080.0f);
    };

    /// Unity-style UI transform model.
    /// Layout is resolved in a canvas-aware pass instead of normal world transforms.
    struct RectTransformComponent
    {
        glm::vec2 AnchorMin = glm::vec2(0.5f, 0.5f);
        glm::vec2 AnchorMax = glm::vec2(0.5f, 0.5f);
        glm::vec2 Pivot = glm::vec2(0.5f, 0.5f);
        glm::vec2 SizeDelta = glm::vec2(100.0f, 40.0f);
        glm::vec2 AnchoredPosition = glm::vec2(0.0f, 0.0f);
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
        glm::vec2 TilingFactor = glm::vec2(1.0f, 1.0f);
        int32_t RenderOrder = 0; ///< Draw order relative to tile layers/sprites. Lower draws first.
        bool CastShadows = true;
        bool ReceiveShadows = true;

        // Optional sub-sprite authoring data.
        // -1 means render full texture; >= 0 selects a sub-sprite entry.
        int32_t SubSpriteIndex = -1;
        glm::vec2 UvMin = glm::vec2(0.0f, 0.0f);
        glm::vec2 UvMax = glm::vec2(1.0f, 1.0f);
    };

    struct AnimationEventMessage
    {
        std::string Name;
        std::string StringPayload;
        float FloatPayload = 0.0f;
        int32_t IntegerPayload = 0;
        bool BooleanPayload = false;
        float TimeSeconds = 0.0f;
        float NormalizedTime = 0.0f;
    };

    struct AnimationEventReceiverComponent
    {
        bool Enabled = true;

        // Runtime-only events dispatched in the most recent animation update.
        std::vector<AnimationEventMessage> RuntimeDispatchedEvents;
        uint64_t RuntimeDispatchFrame = 0;
    };

    struct AnimatorComponent
    {
        // Authoring state (serialized).
        std::string ControllerKey;
        std::string DefaultClipKey;
        float PlaybackSpeed = 1.0f;
        bool Enabled = true;
        bool ApplyToSprite = true;
        bool ApplyToTransform = true;
        bool AutoPlay = true;
        std::unordered_map<std::string, bool> BoolParameters;
        std::unordered_map<std::string, float> FloatParameters;
        std::unordered_map<std::string, int32_t> IntegerParameters;
        std::unordered_map<std::string, bool> TriggerParameters;

        // Runtime caches.
        Assets::AnimatorControllerAsset::Ptr CachedController;
        bool ControllerLoadAttempted = false;
        Assets::AnimationClipAsset::Ptr CachedDefaultClip;
        bool DefaultClipLoadAttempted = false;

        // Runtime playback state.
        bool RuntimeInitialized = false;
        std::string RuntimeCurrentStateName;
        std::string RuntimeCurrentClipKey;
        float RuntimePreviousStateTimeSeconds = 0.0f;
        float RuntimeStateTimeSeconds = 0.0f;
        float RuntimeCurrentStateDurationSeconds = 1.0f;
        float RuntimeStateSpeedMultiplier = 1.0f;

        // Runtime sampled sprite output.
        bool RuntimeHasSpriteSubRect = false;
        glm::vec2 RuntimeSpriteUvMin = glm::vec2(0.0f, 0.0f);
        glm::vec2 RuntimeSpriteUvMax = glm::vec2(1.0f, 1.0f);
        std::string RuntimeSpriteTextureOverrideKey;
        Assets::TextureAsset::Ptr RuntimeCachedSpriteTextureOverride;
        bool RuntimeSpriteTextureOverrideLoadAttempted = false;

        // Runtime sampled transform output.
        bool RuntimeHasPosition = false;
        bool RuntimeHasScale = false;
        bool RuntimeHasRotationZ = false;
        glm::vec3 RuntimePosition = glm::vec3(0.0f);
        glm::vec3 RuntimeScale = glm::vec3(1.0f);
        float RuntimeRotationZDegrees = 0.0f;

        void SetBoolParameter(const std::string& name, bool value)
        {
            BoolParameters[name] = value;
        }

        bool GetBoolParameter(const std::string& name, bool fallback = false) const
        {
            const auto found = BoolParameters.find(name);
            if (found == BoolParameters.end())
                return fallback;
            return found->second;
        }

        void SetFloatParameter(const std::string& name, float value)
        {
            FloatParameters[name] = value;
        }

        float GetFloatParameter(const std::string& name, float fallback = 0.0f) const
        {
            const auto found = FloatParameters.find(name);
            if (found == FloatParameters.end())
                return fallback;
            return found->second;
        }

        void SetIntegerParameter(const std::string& name, int32_t value)
        {
            IntegerParameters[name] = value;
        }

        int32_t GetIntegerParameter(const std::string& name, int32_t fallback = 0) const
        {
            const auto found = IntegerParameters.find(name);
            if (found == IntegerParameters.end())
                return fallback;
            return found->second;
        }

        void SetTrigger(const std::string& name)
        {
            TriggerParameters[name] = true;
        }

        void ResetTrigger(const std::string& name)
        {
            TriggerParameters[name] = false;
        }

        void ResetAllTriggers()
        {
            for (auto& [name, value] : TriggerParameters)
            {
                (void)name;
                value = false;
            }
        }
    };

    /// Marks a SpriteComponent as a UI image for UI-specific tooling and scripts.
    struct UIImageComponent
    {
        bool RaycastTarget = true;
    };
 
    /// Unity-style UI panel for colored backgrounds and optional sprite-backed fills.
    struct UIPanelComponent
    {
        glm::vec4 BackgroundColor = glm::vec4(0.12f, 0.12f, 0.12f, 0.9f);
        bool UseSpriteTexture = false;
        bool RaycastTarget = false;
    };

    /// Runtime UI text payload and interaction metadata for Canvas-based UI.
    struct UITextComponent
    {
        std::string Text = "Text";
        std::string FontFilePath; ///< Relative or absolute font file path.
        Font::Ptr CachedFont;
        bool FontLoadAttempted = false;
        float FontSize = 32.0f;
        glm::vec4 Color = glm::vec4(1.0f);
        bool RaycastTarget = false;
    };

    /// Minimal button state and script event hooks.
    struct UIButtonComponent
    {
        bool Interactable = true;
        bool UseStateColors = true;
        glm::vec4 NormalColor = glm::vec4(0.82f, 0.82f, 0.82f, 1.0f);
        glm::vec4 HoveredColor = glm::vec4(0.92f, 0.92f, 0.92f, 1.0f);
        glm::vec4 PressedColor = glm::vec4(0.72f, 0.72f, 0.72f, 1.0f);
        glm::vec4 DisabledColor = glm::vec4(0.45f, 0.45f, 0.45f, 1.0f);
        bool IsHovered = false;
        bool IsPressed = false;
        bool RuntimeHoverEnteredThisFrame = false;
        bool RuntimeHoverExitedThisFrame = false;
        bool RuntimePressedThisFrame = false;
        bool RuntimeClickedThisFrame = false;

        // Event names are consumed by scripting bridges.
        std::string OnClickEvent;
        std::string OnHoverEnterEvent;
        std::string OnHoverExitEvent;
        std::string OnPressedEvent;
    };

    /// Minimal slider state and value range.
    struct UISliderComponent
    {
        bool Interactable = true;
        float MinValue = 0.0f;
        float MaxValue = 1.0f;
        float Value = 0.0f;
        glm::vec4 BackgroundColor = glm::vec4(0.22f, 0.22f, 0.22f, 1.0f);
        glm::vec4 FillColor = glm::vec4(0.22f, 0.72f, 1.0f, 0.95f);
        glm::vec4 HandleColor = glm::vec4(0.92f, 0.92f, 0.92f, 1.0f);
        float HandleWidth = 16.0f;
        float HandleHeightMultiplier = 1.25f;
        bool ShowHandle = true;
        bool RuntimeDragging = false;
        bool RuntimeValueChangedThisFrame = false;
        std::string OnValueChangedEvent;
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

    /// Selects a 2D audio listener location for spatialized sources.
    /// Author one listener per scene (or one on the active camera entity).
    struct AudioListener2DComponent
    {
        bool Enabled = true;
        bool UsePrimaryCameraPosition = true;
    };

    /// Optional audio source reference (Unity-style).
    /// Supports global playback and 2D spatial playback with attenuation/pan.
    struct AudioSourceComponent
    {
        enum class PlaybackSpace
        {
            Global = 0,
            Spatial2D = 1
        };

        std::string AudioClipKey; ///< Asset key for audio clip (example: "Assets/Audio/MyClip.wav")
        float Volume = 1.0f;
        float Pitch = 1.0f;
        bool PlayOnStart = true;
        bool Loop = false;
        bool Muted = false;
        PlaybackSpace Space = PlaybackSpace::Global;
        std::string MixerGroup = "SFX";

        // Spatial 2D settings.
        float SpatialMinDistance = 1.0f;
        float SpatialMaxDistance = 20.0f;
        float SpatialRolloffExponent = 1.0f;
        float StereoPanStrength = 1.0f;
        std::string AttenuationCurveKey; ///< Optional curve reference for future attenuation assets.

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
        bool RuntimeWarnedMissingCompiledScript = false;

        NativeScriptEntry() = default;

        NativeScriptEntry(const NativeScriptEntry& other)
            : ScriptClassName(other.ScriptClassName),
              ScriptAssetRelativePath(other.ScriptAssetRelativePath),
              Enabled(other.Enabled),
              ExposedProperties(other.ExposedProperties),
              RuntimeInstance(nullptr),
              RuntimeInitialized(false),
              RuntimeUpdateCount(0),
              RuntimeWarnedOnUpdateTransformMutation(false),
              RuntimeWarnedMissingCompiledScript(false)
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
            RuntimeWarnedMissingCompiledScript = false;
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

    // -------------------------------------------------------------------------
    // Grid2D + TilemapLayer system (Unity-style Tile Palette architecture)
    //
    // Grid2DComponent lives on a parent entity and defines the grid layout.
    // TilemapLayerComponent lives on child entities — one per layer.
    // Each layer stores a tile table mapping compact IDs to TileAsset keys,
    // and a per-cell array of tile IDs.
    // -------------------------------------------------------------------------

    /// Defines the grid layout for a tilemap hierarchy. Place on a parent entity
    /// whose children carry TilemapLayerComponent for per-layer tile data.
    struct Grid2DComponent
    {
        glm::vec2 CellSize = glm::vec2(1.0f, 1.0f);
        glm::vec2 CellGap  = glm::vec2(0.0f, 0.0f);
    };

    /// A single tilemap layer within a Grid2D hierarchy. Each cell stores an
    /// index into the TileTable; index 0 is always "empty". The TileTable maps
    /// compact IDs to TileAsset keys for rendering and collision.
    struct TilemapLayerComponent
    {
        glm::ivec2 GridSize = glm::ivec2(64, 64);
        int32_t RenderOrder = 0;
        bool CollisionEnabled = false;

        /// Maps compact tile IDs to TileAsset keys. Index 0 is reserved (empty).
        std::vector<std::string> TileTable;

        /// Per-cell tile ID (index into TileTable). 0 = empty.
        std::vector<uint32_t> Tiles;

        /// Transient render cache — not serialized. Rebuilt lazily when dirty.
        struct CachedTileRenderData
        {
            Assets::TextureAsset::Ptr Texture;
            glm::vec2 UvMin = glm::vec2(0.0f);
            glm::vec2 UvMax = glm::vec2(1.0f);
            glm::vec4 Color = glm::vec4(1.0f);
        };
        std::vector<CachedTileRenderData> CachedTileRender;
        bool RenderCacheDirty = true;

        int32_t GetCellCount() const
        {
            return std::max(1, GridSize.x) * std::max(1, GridSize.y);
        }

        void EnsureStorage()
        {
            const size_t cellCount = static_cast<size_t>(GetCellCount());
            if (Tiles.size() != cellCount)
                Tiles.resize(cellCount, 0u);
        }

        /// Returns the existing tile table index for the given key, or appends
        /// a new entry and returns the new index. Index 0 is reserved for "no tile".
        uint32_t GetOrAddTileTableEntry(const std::string& tileAssetKey)
        {
            if (tileAssetKey.empty())
                return 0;

            // Ensure index 0 is the empty sentinel so tileId==0 always means "no tile".
            if (TileTable.empty())
                TileTable.emplace_back();

            for (size_t i = 1; i < TileTable.size(); ++i)
            {
                if (TileTable[i] == tileAssetKey)
                    return static_cast<uint32_t>(i);
            }
            TileTable.push_back(tileAssetKey);
            RenderCacheDirty = true;
            return static_cast<uint32_t>(TileTable.size() - 1);
        }

        void ResizeGrid(const glm::ivec2& requestedGridSize)
        {
            const glm::ivec2 previousGridSize = GridSize;
            GridSize = glm::ivec2(std::max(1, requestedGridSize.x), std::max(1, requestedGridSize.y));

            const int32_t oldWidth  = std::max(1, previousGridSize.x);
            const int32_t newWidth  = std::max(1, GridSize.x);
            const int32_t newHeight = std::max(1, GridSize.y);
            const int32_t copyWidth  = std::min(oldWidth, newWidth);
            const int32_t copyHeight = std::min(std::max(1, previousGridSize.y), newHeight);

            const std::vector<uint32_t> oldTiles = Tiles;
            Tiles.assign(static_cast<size_t>(newWidth * newHeight), 0u);

            for (int32_t y = 0; y < copyHeight; ++y)
            {
                for (int32_t x = 0; x < copyWidth; ++x)
                {
                    const size_t oldIndex = static_cast<size_t>(y * oldWidth + x);
                    const size_t newIndex = static_cast<size_t>(y * newWidth + x);
                    if (oldIndex < oldTiles.size())
                        Tiles[newIndex] = oldTiles[oldIndex];
                }
            }
        }
    };

    inline bool IsLayerCellInBounds(const TilemapLayerComponent& layer, int32_t cellX, int32_t cellY)
    {
        return cellX >= 0 && cellY >= 0 &&
               cellX < std::max(1, layer.GridSize.x) &&
               cellY < std::max(1, layer.GridSize.y);
    }

    inline size_t LayerCellToIndex(const TilemapLayerComponent& layer, int32_t cellX, int32_t cellY)
    {
        const int32_t width = std::max(1, layer.GridSize.x);
        return static_cast<size_t>(cellY * width + cellX);
    }

    // -------------------------------------------------------------------------
    // 2D Particle System
    //
    // Unity-style particle emitter component. Authoring fields are serialized;
    // runtime state (the particle pool) is transient and rebuilt on play.
    // -------------------------------------------------------------------------

    /// Internal particle pool using a Struct-of-Arrays layout for cache-friendly
    /// iteration. Allocated once when the emitter starts playing; never grows.
    struct ParticleEmitterRuntime
    {
        std::vector<glm::vec2> Positions;
        std::vector<glm::vec2> Velocities;
        std::vector<float>     Lifetimes;
        std::vector<float>     MaxLifetimes;
        std::vector<float>     StartSizes;
        std::vector<float>     EndSizes;
        std::vector<glm::vec4> StartColors;
        std::vector<glm::vec4> EndColors;
        std::vector<float>     Rotations;
        std::vector<float>     RotationSpeeds;

        uint32_t AliveCount      = 0;
        float    SpawnAccumulator = 0.0f;
        float    ElapsedTime     = 0.0f;
        bool     BurstFired      = false;

        /// Pre-allocate all SoA arrays to the given capacity. Called once.
        void Allocate(uint32_t maxParticles)
        {
            Positions.resize(maxParticles);
            Velocities.resize(maxParticles);
            Lifetimes.resize(maxParticles);
            MaxLifetimes.resize(maxParticles);
            StartSizes.resize(maxParticles);
            EndSizes.resize(maxParticles);
            StartColors.resize(maxParticles, glm::vec4(1.0f));
            EndColors.resize(maxParticles, glm::vec4(1.0f));
            Rotations.resize(maxParticles, 0.0f);
            RotationSpeeds.resize(maxParticles, 0.0f);
            AliveCount = 0;
            SpawnAccumulator = 0.0f;
            ElapsedTime = 0.0f;
            BurstFired = false;
        }

        void Reset()
        {
            AliveCount = 0;
            SpawnAccumulator = 0.0f;
            ElapsedTime = 0.0f;
            BurstFired = false;
        }
    };

    /// 2D particle emitter component. Attach to any entity with a TransformComponent.
    struct ParticleEmitterComponent
    {
        static constexpr uint32_t kMaxParticlesCap = 32768;

        // --- Emission ---
        float    SpawnRate   = 10.0f;   ///< Particles emitted per second (continuous mode).
        float    LifetimeMin = 1.0f;    ///< Minimum particle lifetime in seconds.
        float    LifetimeMax = 2.0f;    ///< Maximum particle lifetime in seconds.
        bool     Looping     = true;
        float    Duration    = 5.0f;    ///< Emitter duration before stopping (if not looping).
        bool     PlayOnStart = true;    ///< Automatically begin emitting when the scene starts.

        // Burst emission (fires once at start of each cycle).
        bool     BurstEnabled = false;
        uint32_t BurstCount   = 10;

        // Spawn position relative to emitter origin.
        // Box mode: random offset in [SpawnOffsetMin, SpawnOffsetMax].
        // Radial mode: random offset from radius range around emitter origin.
        glm::vec2 SpawnOffsetMin = glm::vec2(0.0f);
        glm::vec2 SpawnOffsetMax = glm::vec2(0.0f);
        bool UseRadialSpawn = false;
        float SpawnRadiusMin = 0.0f;
        float SpawnRadiusMax = 0.0f;

        // --- Velocity ---
        float SpeedMin = 50.0f;   ///< Minimum initial particle speed (world units/sec).
        float SpeedMax = 100.0f;
        float AngleMin = 0.0f;    ///< Minimum emission angle in degrees (0 = right, 90 = up).
        float AngleMax = 360.0f;
        bool RadialVelocity = false; ///< When true, initial velocity points away from emitter center.

        // --- Physics ---
        float GravityModifier = 0.0f;  ///< Multiplied by 9.81 and applied to Y velocity each frame.

        // --- Appearance ---
        float    StartSizeMin = 1.0f;
        float    StartSizeMax = 1.0f;
        float    EndSize      = 0.0f;
        glm::vec4 StartColor  = glm::vec4(1.0f);
        glm::vec4 EndColor    = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);

        // --- Rotation ---
        float StartRotationMin = 0.0f;   ///< Degrees.
        float StartRotationMax = 0.0f;
        float RotationSpeedMin = 0.0f;   ///< Degrees per second.
        float RotationSpeedMax = 0.0f;

        // --- Texture ---
        std::string TextureKey;
        Assets::TextureAsset::Ptr CachedTexture;
        bool TextureLoadAttempted = false;

        // --- Limits ---
        uint32_t MaxParticles = 1024;

        // --- Runtime (not serialized, transient) ---
        std::unique_ptr<ParticleEmitterRuntime> RuntimeState;
        bool Playing = false;
        bool Paused  = false;

        // -- Copy semantics: copy authoring data, reset runtime --

        ParticleEmitterComponent() = default;

        ParticleEmitterComponent(const ParticleEmitterComponent& other)
            : SpawnRate(other.SpawnRate), LifetimeMin(other.LifetimeMin), LifetimeMax(other.LifetimeMax),
              Looping(other.Looping), Duration(other.Duration), PlayOnStart(other.PlayOnStart),
              BurstEnabled(other.BurstEnabled), BurstCount(other.BurstCount),
              SpawnOffsetMin(other.SpawnOffsetMin), SpawnOffsetMax(other.SpawnOffsetMax),
              UseRadialSpawn(other.UseRadialSpawn), SpawnRadiusMin(other.SpawnRadiusMin), SpawnRadiusMax(other.SpawnRadiusMax),
              SpeedMin(other.SpeedMin), SpeedMax(other.SpeedMax),
              AngleMin(other.AngleMin), AngleMax(other.AngleMax), RadialVelocity(other.RadialVelocity),
              GravityModifier(other.GravityModifier),
              StartSizeMin(other.StartSizeMin), StartSizeMax(other.StartSizeMax), EndSize(other.EndSize),
              StartColor(other.StartColor), EndColor(other.EndColor),
              StartRotationMin(other.StartRotationMin), StartRotationMax(other.StartRotationMax),
              RotationSpeedMin(other.RotationSpeedMin), RotationSpeedMax(other.RotationSpeedMax),
              TextureKey(other.TextureKey),
              CachedTexture(nullptr), TextureLoadAttempted(false),
              MaxParticles(other.MaxParticles),
              RuntimeState(nullptr), Playing(false), Paused(false)
        {
        }

        ParticleEmitterComponent& operator=(const ParticleEmitterComponent& other)
        {
            if (this == &other) return *this;
            SpawnRate = other.SpawnRate; LifetimeMin = other.LifetimeMin; LifetimeMax = other.LifetimeMax;
            Looping = other.Looping; Duration = other.Duration; PlayOnStart = other.PlayOnStart;
            BurstEnabled = other.BurstEnabled; BurstCount = other.BurstCount;
            SpawnOffsetMin = other.SpawnOffsetMin; SpawnOffsetMax = other.SpawnOffsetMax;
            UseRadialSpawn = other.UseRadialSpawn; SpawnRadiusMin = other.SpawnRadiusMin; SpawnRadiusMax = other.SpawnRadiusMax;
            SpeedMin = other.SpeedMin; SpeedMax = other.SpeedMax;
            AngleMin = other.AngleMin; AngleMax = other.AngleMax; RadialVelocity = other.RadialVelocity;
            GravityModifier = other.GravityModifier;
            StartSizeMin = other.StartSizeMin; StartSizeMax = other.StartSizeMax; EndSize = other.EndSize;
            StartColor = other.StartColor; EndColor = other.EndColor;
            StartRotationMin = other.StartRotationMin; StartRotationMax = other.StartRotationMax;
            RotationSpeedMin = other.RotationSpeedMin; RotationSpeedMax = other.RotationSpeedMax;
            TextureKey = other.TextureKey;
            CachedTexture = nullptr; TextureLoadAttempted = false;
            MaxParticles = other.MaxParticles;
            RuntimeState.reset(); Playing = false; Paused = false;
            return *this;
        }

        ParticleEmitterComponent(ParticleEmitterComponent&&) noexcept = default;
        ParticleEmitterComponent& operator=(ParticleEmitterComponent&&) noexcept = default;
    };
}
