#pragma once

#include "EnTT/entt.hpp"

#include <glm/glm.hpp>
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
    };

    /// Renders a 2D quad. Used with Renderer2D for the viewport.
    struct QuadRendererComponent
    {
        glm::vec2 Size = glm::vec2(1.0f, 1.0f);
        glm::vec4 Color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    };
}
