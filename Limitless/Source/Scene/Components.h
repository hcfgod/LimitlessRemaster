#pragma once

#include "Assets/TextureAsset.h"
#include "EnTT/entt.hpp"

#include <glm/glm.hpp>
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
        glm::vec4 Color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    };
}
