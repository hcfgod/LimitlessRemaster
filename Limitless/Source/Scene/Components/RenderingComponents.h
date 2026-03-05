#pragma once

#include "Assets/AnimationClipAsset.h"
#include "Assets/AnimatorControllerAsset.h"
#include "Assets/MaterialAsset.h"
#include "Assets/TextureAsset.h"
#include "Graphics/Font.h"

#include <algorithm>
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Limitless
{
    /// Renders a 2D sprite (quad). Size comes from TransformComponent::Scale.
    /// TextureKey is empty for color-only; non-empty for textured sprites.
    /// CachedTexture holds a reference to keep the asset alive.
    struct SpriteComponent
    {
        std::string TextureKey; ///< Asset key for texture; empty = color-only
        Assets::TextureAsset::Ptr CachedTexture; ///< Runtime cache; keeps asset alive
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

        // Runtime sampled transform output (additive offsets applied on top of base transform).
        bool RuntimeHasPosition = false;
        bool RuntimeHasScale = false;
        bool RuntimeHasRotation = false;
        glm::vec3 RuntimePosition = glm::vec3(0.0f);
        glm::vec3 RuntimeScale = glm::vec3(0.0f);
        glm::vec3 RuntimeRotation = glm::vec3(0.0f);

        // Tracks the offsets currently baked into the TransformComponent so they
        // can be undone before applying the next frame's sampled values.
        glm::vec3 RuntimeAppliedPositionOffset = glm::vec3(0.0f);
        glm::vec3 RuntimeAppliedScaleOffset = glm::vec3(0.0f);
        glm::vec3 RuntimeAppliedRotationOffset = glm::vec3(0.0f);

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
        std::string MaterialKey; ///< Asset key for material.
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
        std::vector<float> Lifetimes;
        std::vector<float> MaxLifetimes;
        std::vector<float> StartSizes;
        std::vector<float> EndSizes;
        std::vector<glm::vec4> StartColors;
        std::vector<glm::vec4> EndColors;
        std::vector<float> Rotations;
        std::vector<float> RotationSpeeds;

        uint32_t AliveCount = 0;
        float SpawnAccumulator = 0.0f;
        float ElapsedTime = 0.0f;
        bool BurstFired = false;

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
        float SpawnRate = 10.0f;   ///< Particles emitted per second (continuous mode).
        float LifetimeMin = 1.0f;  ///< Minimum particle lifetime in seconds.
        float LifetimeMax = 2.0f;  ///< Maximum particle lifetime in seconds.
        bool Looping = true;
        float Duration = 5.0f;     ///< Emitter duration before stopping (if not looping).
        bool PlayOnStart = true;   ///< Automatically begin emitting when the scene starts.

        // Burst emission (fires once at start of each cycle).
        bool BurstEnabled = false;
        uint32_t BurstCount = 10;

        // Spawn position relative to emitter origin.
        // Box mode: random offset in [SpawnOffsetMin, SpawnOffsetMax].
        // Radial mode: random offset from radius range around emitter origin.
        glm::vec2 SpawnOffsetMin = glm::vec2(0.0f);
        glm::vec2 SpawnOffsetMax = glm::vec2(0.0f);
        bool UseRadialSpawn = false;
        float SpawnRadiusMin = 0.0f;
        float SpawnRadiusMax = 0.0f;

        // --- Velocity ---
        float SpeedMin = 50.0f; ///< Minimum initial particle speed (world units/sec).
        float SpeedMax = 100.0f;
        float AngleMin = 0.0f;  ///< Minimum emission angle in degrees (0 = right, 90 = up).
        float AngleMax = 360.0f;
        bool RadialVelocity = false; ///< When true, initial velocity points away from emitter center.

        // --- Physics ---
        float GravityModifier = 0.0f; ///< Multiplied by 9.81 and applied to Y velocity each frame.

        // --- Appearance ---
        float StartSizeMin = 1.0f;
        float StartSizeMax = 1.0f;
        float EndSize = 0.0f;
        glm::vec4 StartColor = glm::vec4(1.0f);
        glm::vec4 EndColor = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);

        // --- Rotation ---
        float StartRotationMin = 0.0f; ///< Degrees.
        float StartRotationMax = 0.0f;
        float RotationSpeedMin = 0.0f; ///< Degrees per second.
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
        bool Paused = false;

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
            if (this == &other)
                return *this;
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
