#pragma once

#include "Assets/MaterialAsset.h"
#include "Assets/AudioClipAsset.h"
#include "Assets/TextureAsset.h"
#include "EnTT/entt.hpp"

#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <string>

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

    /// Optional material reference (Unity-style).
    /// When set, rendering should prefer the material's bound resources over Sprite defaults.
    struct MaterialComponent
    {
        std::string MaterialKey; ///< Asset key for material (example: "Assets/Materials/MyMaterial.material.json")
        Assets::MaterialAsset::Ptr CachedMaterial; ///< Runtime cache; keeps asset alive
        bool MaterialLoadAttempted = false; ///< Prevents per-frame retry/log spam when material is missing
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
}
