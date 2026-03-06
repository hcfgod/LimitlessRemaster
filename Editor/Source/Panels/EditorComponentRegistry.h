#pragma once

#include "EditorInspectorPanelEntityComponents.h"

#include <cstdint>

namespace Limitless::EditorInspectorPanel
{
    enum class ComponentRegistryKey : uint8_t
    {
        AudioListener2D = 0,
        Rigidbody2D = 1,
        BoxCollider2D = 2,
        ParticleEmitter = 3,
        CircleCollider2D = 4,
        PolygonCollider2D = 5,
        EdgeCollider2D = 6,
        CapsuleCollider2D = 7,
        Joint2D = 8,
        Grid2D = 9,
        DirectionalLight2D = 10,
        PointLight2D = 11,
        ShadowOccluder2D = 12,
        Camera = 13,
        Animator = 14,
        AnimationEventReceiver = 15,
        Sprite = 16,
        AudioSource = 17,
        NativeScript = 18,
        TilemapLayer = 19,
        Canvas = 20,
        RectTransform = 21,
        UIImage = 22,
        UIPanel = 23,
        UIText = 24,
        UIButton = 25,
        AudioListener3D = 26
    };

    struct ComponentRegistryEntry
    {
        const char* MenuItemLabel = "";
        const char* AddMutationLabel = "";
        const char* RemoveMutationLabel = "";
        bool (*HasComponent)(const entt::registry&, entt::entity) = nullptr;
        void (*AddComponent)(entt::registry&, entt::entity) = nullptr;
        void (*RemoveComponent)(entt::registry&, entt::entity) = nullptr;
    };

    const ComponentRegistryEntry* FindComponentRegistryEntry(ComponentRegistryKey key);
}
