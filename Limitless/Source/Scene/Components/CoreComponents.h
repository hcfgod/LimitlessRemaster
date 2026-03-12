#pragma once

#include "EnTT/entt.hpp"

#include <cstdint>
#include <glm/glm.hpp>
#include <string>

namespace Limitless
{
    /// Display name in the hierarchy. Required for all entities.
    struct TagComponent
    {
        std::string Tag = "Entity";
        bool Enabled = true; ///< Unity-style active state. Disabled entities do not update or render.
        uint8_t Layer = 0; ///< Unity-style layer index (0-31). Used for physics collision filtering, camera culling, and raycasting.
    };

    struct SceneEntityIdComponent
    {
        std::string Id;
    };

    /// Position, rotation (euler degrees), and scale. Used for rendering and hierarchy.
    struct TransformComponent
    {
        glm::vec3 Position = glm::vec3(0.0f);
        glm::vec3 Rotation = glm::vec3(0.0f); ///< Euler angles in degrees (X=pitch, Y=yaw, Z=roll)
        glm::vec3 Scale = glm::vec3(1.0f);
        glm::mat4 LocalTransform = glm::mat4(1.0f);
        glm::mat4 WorldTransform = glm::mat4(1.0f);
        bool LocalDirty = true;
        bool Dirty = true;
        bool RuntimeWorldUpdatedThisFrame = false;
        glm::vec3 CachedLocalPosition = glm::vec3(0.0f);
        glm::vec3 CachedLocalRotation = glm::vec3(0.0f);
        glm::vec3 CachedLocalScale = glm::vec3(1.0f);

        glm::mat4 GetLocalMatrix() const;
    };

    /// Optional parent for hierarchy. Entities without this are root-level.
    struct HierarchyComponent
    {
        /// Parent entity. Use entt::null for no parent (root).
        entt::entity Parent = entt::null;

        /// Relative sibling order under the current parent (lower renders/appears first).
        int32_t SiblingOrder = 0;

        /// Cached hierarchy depth used by depth-batched transform updates.
        uint16_t HierarchyDepth = 0;
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

        // Layer culling mask. Each bit corresponds to a layer index (0-31).
        // Only entities whose layer bit is set in this mask will be rendered by this camera.
        uint32_t CullingMask = ~0u; ///< Default: render all layers.
    };

    /// Marks an entity hierarchy root as an instance of a prefab asset.
    struct PrefabInstanceComponent
    {
        std::string PrefabAssetKey; ///< Asset key for prefab (example: "Assets/Prefabs/Player.prefab.json")
    };
}
