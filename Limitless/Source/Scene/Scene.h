#pragma once

#include "Scene/Components.h"

#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace Limitless
{
    class Camera;
    class Framebuffer;

    // -----------------------------------------------------------------------------
    // Scene
    // Unity-style scene: owns a registry of entities and components.
    // One scene per level or editor context. Create entities, add components, iterate.
    // Uses EnTT for the ECS runtime.
    // -----------------------------------------------------------------------------
    class Scene
    {
    public:
        Scene();
        ~Scene() = default;

        Scene(const Scene&) = delete;
        Scene& operator=(const Scene&) = delete;

        /// Create a new entity with TagComponent. Returns entity handle.
        entt::entity CreateEntity(const std::string& name = "Entity");

        /// Destroy an entity and all its components.
        void DestroyEntity(entt::entity entity);

        /// Check if entity exists.
        bool IsValid(entt::entity entity) const;

        /// Set parent-child relation. Use entt::null parent to make entity root-level.
        bool SetParent(entt::entity child, entt::entity parent);

        /// Reorder entity to appear before target sibling in hierarchy.
        bool SetSiblingOrderBefore(entt::entity entity, entt::entity targetSibling);

        /// Reorder entity to appear after target sibling in hierarchy.
        bool SetSiblingOrderAfter(entt::entity entity, entt::entity targetSibling);

        /// Get entity parent or entt::null when root-level.
        entt::entity GetParent(entt::entity entity) const;

        /// Returns true when entity is a descendant of potentialAncestor.
        bool IsDescendantOf(entt::entity entity, entt::entity potentialAncestor) const;

        /// Get all direct children of parent. Use entt::null for root-level entities.
        std::vector<entt::entity> GetChildren(entt::entity parent) const;

        /// Get world transform matrix with hierarchy applied.
        glm::mat4 GetWorldTransformMatrix(entt::entity entity) const;

        /// Get the EnTT registry for custom queries (views, groups, etc.).
        entt::registry& GetRegistry() { return m_Registry; }
        const entt::registry& GetRegistry() const { return m_Registry; }

        /// Create a deep copy of this scene for runtime simulation.
        /// Used by editor Play Mode to keep edit-time data isolated.
        std::unique_ptr<Scene> Clone() const;

    private:
        entt::registry m_Registry;
    };

    // -----------------------------------------------------------------------------
    // SceneRenderer
    // Renders scene entities to the given camera. Uses Renderer2D for SpriteComponent.
    // -----------------------------------------------------------------------------
    class SceneRenderer
    {
    public:
        /// Render scene to the given camera (draws to whatever framebuffer is currently bound).
        static void Render(Scene& scene, const Camera& camera);

        /// Render scene to a viewport framebuffer (binds, clears, draws, unbinds).
        /// Use this for editor viewports or off-screen rendering.
        static void RenderToViewport(Scene& scene, const Camera& camera,
            const std::shared_ptr<Framebuffer>& framebuffer, uint32_t width, uint32_t height);
    };
}
