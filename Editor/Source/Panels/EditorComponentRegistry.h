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
        Joint2D = 5,
        Grid2D = 6,
        DirectionalLight2D = 7,
        PointLight2D = 8,
        ShadowOccluder2D = 9,
        Camera = 10,
        Animator = 11,
        AnimationEventReceiver = 12,
        Sprite = 13,
        AudioSource = 14,
        NativeScript = 15,
        TilemapLayer = 16,
        Canvas = 17,
        RectTransform = 18,
        UIImage = 19,
        UIPanel = 20,
        UIText = 21,
        UIButton = 22,
        AudioListener3D = 23
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
