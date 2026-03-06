#include "EditorComponentRegistry.h"
#include "Audio/AudioEngine.h"

namespace Limitless::EditorInspectorPanel
{
    namespace
    {
        template<typename ComponentType>
        ComponentType& EnsureComponent(entt::registry& registry, entt::entity entity)
        {
            if (auto* existing = registry.try_get<ComponentType>(entity))
                return *existing;
            return registry.emplace<ComponentType>(entity);
        }

        void ClearPrimaryFlagFromOtherCameras(entt::registry& registry, entt::entity currentEntity)
        {
            auto view = registry.view<CameraComponent>();
            for (entt::entity entity : view)
            {
                if (entity == currentEntity)
                    continue;

                auto& otherCamera = view.get<CameraComponent>(entity);
                otherCamera.IsPrimary = false;
            }
        }

        template<typename ComponentType>
        bool HasComponent(const entt::registry& registry, entt::entity entity)
        {
            return registry.all_of<ComponentType>(entity);
        }

        template<typename ComponentType>
        void AddComponent(entt::registry& registry, entt::entity entity)
        {
            registry.emplace<ComponentType>(entity);
        }

        template<typename ComponentType>
        void RemoveComponent(entt::registry& registry, entt::entity entity)
        {
            registry.remove<ComponentType>(entity);
        }

        void AddCameraComponent(entt::registry& registry, entt::entity entity)
        {
            auto& camera = registry.emplace<CameraComponent>(entity);
            camera.IsPrimary = true;
            ClearPrimaryFlagFromOtherCameras(registry, entity);
        }

        void RemoveSpriteComponent(entt::registry& registry, entt::entity entity)
        {
            registry.remove<SpriteComponent>(entity);
            registry.remove<MaterialComponent>(entity);
        }

        void RemoveAudioSourceComponent(entt::registry& registry, entt::entity entity)
        {
            if (auto* audioSource = registry.try_get<AudioSourceComponent>(entity))
            {
                if (audioSource->RuntimeVoiceId != 0)
                    Audio::AudioEngine::GetInstance().Stop(audioSource->RuntimeVoiceId);
            }
            registry.remove<AudioSourceComponent>(entity);
        }

        void AddTilemapLayerComponent(entt::registry& registry, entt::entity entity)
        {
            auto& layer = registry.emplace<TilemapLayerComponent>(entity);
            layer.EnsureStorage();
        }

        void AddCanvasComponent(entt::registry& registry, entt::entity entity)
        {
            registry.emplace<CanvasComponent>(entity);
            (void)EnsureComponent<RectTransformComponent>(registry, entity);
        }

        void AddUIImageComponent(entt::registry& registry, entt::entity entity)
        {
            registry.emplace<UIImageComponent>(entity);
            (void)EnsureComponent<RectTransformComponent>(registry, entity);
            (void)EnsureComponent<SpriteComponent>(registry, entity);
        }

        void AddUIPanelComponent(entt::registry& registry, entt::entity entity)
        {
            auto& panel = registry.emplace<UIPanelComponent>(entity);
            (void)EnsureComponent<RectTransformComponent>(registry, entity);
            if (!registry.all_of<SpriteComponent>(entity))
            {
                auto& sprite = registry.emplace<SpriteComponent>(entity);
                sprite.Color = panel.BackgroundColor;
            }
        }

        void AddUITextComponent(entt::registry& registry, entt::entity entity)
        {
            auto& uiText = registry.emplace<UITextComponent>(entity);
            uiText.FontFilePath = "Assets/Fonts/Default.ttf";
            (void)EnsureComponent<RectTransformComponent>(registry, entity);
        }

        void AddUIButtonComponent(entt::registry& registry, entt::entity entity)
        {
            auto& button = registry.emplace<UIButtonComponent>(entity);
            (void)EnsureComponent<UIImageComponent>(registry, entity);
            (void)EnsureComponent<RectTransformComponent>(registry, entity);
            if (!registry.all_of<SpriteComponent>(entity))
            {
                auto& sprite = registry.emplace<SpriteComponent>(entity);
                sprite.Color = glm::vec4(0.82f, 0.82f, 0.82f, 1.0f);
            }
            if (auto* sprite = registry.try_get<SpriteComponent>(entity))
            {
                button.NormalColor = sprite->Color;
                button.HoveredColor = glm::clamp(sprite->Color * glm::vec4(1.12f, 1.12f, 1.12f, 1.0f), glm::vec4(0.0f), glm::vec4(1.0f));
                button.PressedColor = glm::clamp(sprite->Color * glm::vec4(0.85f, 0.85f, 0.85f, 1.0f), glm::vec4(0.0f), glm::vec4(1.0f));
                button.DisabledColor = glm::clamp(sprite->Color * glm::vec4(0.55f, 0.55f, 0.55f, 1.0f), glm::vec4(0.0f), glm::vec4(1.0f));
            }
        }

        void RemoveNativeScriptComponent(entt::registry& registry, entt::entity entity)
        {
            if (auto* nativeScript = registry.try_get<NativeScriptComponent>(entity))
            {
                for (auto& scriptEntry : nativeScript->Scripts)
                {
                    scriptEntry.RuntimeInitialized = false;
                    scriptEntry.RuntimeInstance.reset();
                }
            }
            registry.remove<NativeScriptComponent>(entity);
        }

        const ComponentRegistryEntry kAudioListener2DEntry{
            "Audio Listener 2D",
            "Add Audio Listener 2D Component",
            "Remove Audio Listener 2D Component",
            &HasComponent<AudioListener2DComponent>,
            &AddComponent<AudioListener2DComponent>,
            &RemoveComponent<AudioListener2DComponent>
        };

        const ComponentRegistryEntry kAudioListener3DEntry{
            "Audio Listener 3D",
            "Add Audio Listener 3D Component",
            "Remove Audio Listener 3D Component",
            &HasComponent<AudioListener3DComponent>,
            &AddComponent<AudioListener3DComponent>,
            &RemoveComponent<AudioListener3DComponent>
        };

        const ComponentRegistryEntry kRigidbody2DEntry{
            "Rigidbody 2D",
            "Add Rigidbody2D Component",
            "Remove Rigidbody2D Component",
            &HasComponent<Rigidbody2DComponent>,
            &AddComponent<Rigidbody2DComponent>,
            &RemoveComponent<Rigidbody2DComponent>
        };

        const ComponentRegistryEntry kBoxCollider2DEntry{
            "Box Collider 2D",
            "Add BoxCollider2D Component",
            "Remove BoxCollider2D Component",
            &HasComponent<BoxCollider2DComponent>,
            &AddComponent<BoxCollider2DComponent>,
            &RemoveComponent<BoxCollider2DComponent>
        };

        const ComponentRegistryEntry kParticleEmitterEntry{
            "Particle Emitter",
            "Add Particle Emitter Component",
            "Remove Particle Emitter Component",
            &HasComponent<ParticleEmitterComponent>,
            &AddComponent<ParticleEmitterComponent>,
            &RemoveComponent<ParticleEmitterComponent>
        };

        const ComponentRegistryEntry kCircleCollider2DEntry{
            "Circle Collider 2D",
            "Add CircleCollider2D Component",
            "Remove CircleCollider2D Component",
            &HasComponent<CircleCollider2DComponent>,
            &AddComponent<CircleCollider2DComponent>,
            &RemoveComponent<CircleCollider2DComponent>
        };

        const ComponentRegistryEntry kPolygonCollider2DEntry{
            "Polygon Collider 2D",
            "Add PolygonCollider2D Component",
            "Remove PolygonCollider2D Component",
            &HasComponent<PolygonCollider2DComponent>,
            &AddComponent<PolygonCollider2DComponent>,
            &RemoveComponent<PolygonCollider2DComponent>
        };

        const ComponentRegistryEntry kEdgeCollider2DEntry{
            "Edge Collider 2D",
            "Add EdgeCollider2D Component",
            "Remove EdgeCollider2D Component",
            &HasComponent<EdgeCollider2DComponent>,
            &AddComponent<EdgeCollider2DComponent>,
            &RemoveComponent<EdgeCollider2DComponent>
        };

        const ComponentRegistryEntry kCapsuleCollider2DEntry{
            "Capsule Collider 2D",
            "Add CapsuleCollider2D Component",
            "Remove CapsuleCollider2D Component",
            &HasComponent<CapsuleCollider2DComponent>,
            &AddComponent<CapsuleCollider2DComponent>,
            &RemoveComponent<CapsuleCollider2DComponent>
        };

        const ComponentRegistryEntry kJoint2DEntry{
            "Joint 2D",
            "Add Joint2D Component",
            "Remove Joint2D Component",
            &HasComponent<Joint2DComponent>,
            &AddComponent<Joint2DComponent>,
            &RemoveComponent<Joint2DComponent>
        };

        const ComponentRegistryEntry kGrid2DEntry{
            "Grid 2D",
            "Add Grid2D Component",
            "Remove Grid2D Component",
            &HasComponent<Grid2DComponent>,
            &AddComponent<Grid2DComponent>,
            &RemoveComponent<Grid2DComponent>
        };

        const ComponentRegistryEntry kDirectionalLight2DEntry{
            "Directional Light 2D",
            "Add DirectionalLight2D Component",
            "Remove DirectionalLight2D Component",
            &HasComponent<DirectionalLight2DComponent>,
            &AddComponent<DirectionalLight2DComponent>,
            &RemoveComponent<DirectionalLight2DComponent>
        };

        const ComponentRegistryEntry kPointLight2DEntry{
            "Point Light 2D",
            "Add PointLight2D Component",
            "Remove PointLight2D Component",
            &HasComponent<PointLight2DComponent>,
            &AddComponent<PointLight2DComponent>,
            &RemoveComponent<PointLight2DComponent>
        };

        const ComponentRegistryEntry kShadowOccluder2DEntry{
            "Shadow Occluder 2D",
            "Add ShadowOccluder2D Component",
            "Remove ShadowOccluder2D Component",
            &HasComponent<ShadowOccluder2DComponent>,
            &AddComponent<ShadowOccluder2DComponent>,
            &RemoveComponent<ShadowOccluder2DComponent>
        };

        const ComponentRegistryEntry kCameraEntry{
            "Camera Component",
            "Add Camera Component",
            "Remove Camera Component",
            &HasComponent<CameraComponent>,
            &AddCameraComponent,
            &RemoveComponent<CameraComponent>
        };

        const ComponentRegistryEntry kAnimatorEntry{
            "Animator",
            "Add Animator Component",
            "Remove Animator Component",
            &HasComponent<AnimatorComponent>,
            &AddComponent<AnimatorComponent>,
            &RemoveComponent<AnimatorComponent>
        };

        const ComponentRegistryEntry kAnimationEventReceiverEntry{
            "Animation Event Receiver",
            "Add Animation Event Receiver Component",
            "Remove Animation Event Receiver Component",
            &HasComponent<AnimationEventReceiverComponent>,
            &AddComponent<AnimationEventReceiverComponent>,
            &RemoveComponent<AnimationEventReceiverComponent>
        };

        const ComponentRegistryEntry kSpriteEntry{
            "Sprite Component",
            "Add Sprite Component",
            "Remove Sprite Component",
            &HasComponent<SpriteComponent>,
            &AddComponent<SpriteComponent>,
            &RemoveSpriteComponent
        };

        const ComponentRegistryEntry kAudioSourceEntry{
            "Audio Source",
            "Add Audio Source Component",
            "Remove Audio Source Component",
            &HasComponent<AudioSourceComponent>,
            &AddComponent<AudioSourceComponent>,
            &RemoveAudioSourceComponent
        };

        const ComponentRegistryEntry kNativeScriptEntry{
            "Native Script",
            "Add Native Script Component",
            "Remove Native Script Component",
            &HasComponent<NativeScriptComponent>,
            &AddComponent<NativeScriptComponent>,
            &RemoveNativeScriptComponent
        };

        const ComponentRegistryEntry kTilemapLayerEntry{
            "Tilemap Layer",
            "Add TilemapLayer Component",
            "Remove TilemapLayer Component",
            &HasComponent<TilemapLayerComponent>,
            &AddTilemapLayerComponent,
            &RemoveComponent<TilemapLayerComponent>
        };

        const ComponentRegistryEntry kCanvasEntry{
            "Canvas",
            "Add Canvas Component",
            "Remove Canvas Component",
            &HasComponent<CanvasComponent>,
            &AddCanvasComponent,
            &RemoveComponent<CanvasComponent>
        };

        const ComponentRegistryEntry kRectTransformEntry{
            "RectTransform",
            "Add RectTransform Component",
            "Remove RectTransform Component",
            &HasComponent<RectTransformComponent>,
            &AddComponent<RectTransformComponent>,
            &RemoveComponent<RectTransformComponent>
        };

        const ComponentRegistryEntry kUIImageEntry{
            "UI Image",
            "Add UIImage Component",
            "Remove UIImage Component",
            &HasComponent<UIImageComponent>,
            &AddUIImageComponent,
            &RemoveComponent<UIImageComponent>
        };

        const ComponentRegistryEntry kUIPanelEntry{
            "UI Panel",
            "Add UIPanel Component",
            "Remove UIPanel Component",
            &HasComponent<UIPanelComponent>,
            &AddUIPanelComponent,
            &RemoveComponent<UIPanelComponent>
        };

        const ComponentRegistryEntry kUITextEntry{
            "UI Text",
            "Add UIText Component",
            "Remove UIText Component",
            &HasComponent<UITextComponent>,
            &AddUITextComponent,
            &RemoveComponent<UITextComponent>
        };

        const ComponentRegistryEntry kUIButtonEntry{
            "UI Button",
            "Add UIButton Component",
            "Remove UIButton Component",
            &HasComponent<UIButtonComponent>,
            &AddUIButtonComponent,
            &RemoveComponent<UIButtonComponent>
        };
    }

    const ComponentRegistryEntry* FindComponentRegistryEntry(ComponentRegistryKey key)
    {
        switch (key)
        {
            case ComponentRegistryKey::AudioListener2D:
                return &kAudioListener2DEntry;
            case ComponentRegistryKey::AudioListener3D:
                return &kAudioListener3DEntry;
            case ComponentRegistryKey::Rigidbody2D:
                return &kRigidbody2DEntry;
            case ComponentRegistryKey::BoxCollider2D:
                return &kBoxCollider2DEntry;
            case ComponentRegistryKey::ParticleEmitter:
                return &kParticleEmitterEntry;
            case ComponentRegistryKey::CircleCollider2D:
                return &kCircleCollider2DEntry;
            case ComponentRegistryKey::PolygonCollider2D:
                return &kPolygonCollider2DEntry;
            case ComponentRegistryKey::EdgeCollider2D:
                return &kEdgeCollider2DEntry;
            case ComponentRegistryKey::CapsuleCollider2D:
                return &kCapsuleCollider2DEntry;
            case ComponentRegistryKey::Joint2D:
                return &kJoint2DEntry;
            case ComponentRegistryKey::Grid2D:
                return &kGrid2DEntry;
            case ComponentRegistryKey::DirectionalLight2D:
                return &kDirectionalLight2DEntry;
            case ComponentRegistryKey::PointLight2D:
                return &kPointLight2DEntry;
            case ComponentRegistryKey::ShadowOccluder2D:
                return &kShadowOccluder2DEntry;
            case ComponentRegistryKey::Camera:
                return &kCameraEntry;
            case ComponentRegistryKey::Animator:
                return &kAnimatorEntry;
            case ComponentRegistryKey::AnimationEventReceiver:
                return &kAnimationEventReceiverEntry;
            case ComponentRegistryKey::Sprite:
                return &kSpriteEntry;
            case ComponentRegistryKey::AudioSource:
                return &kAudioSourceEntry;
            case ComponentRegistryKey::NativeScript:
                return &kNativeScriptEntry;
            case ComponentRegistryKey::TilemapLayer:
                return &kTilemapLayerEntry;
            case ComponentRegistryKey::Canvas:
                return &kCanvasEntry;
            case ComponentRegistryKey::RectTransform:
                return &kRectTransformEntry;
            case ComponentRegistryKey::UIImage:
                return &kUIImageEntry;
            case ComponentRegistryKey::UIPanel:
                return &kUIPanelEntry;
            case ComponentRegistryKey::UIText:
                return &kUITextEntry;
            case ComponentRegistryKey::UIButton:
                return &kUIButtonEntry;
            default:
                break;
        }

        return nullptr;
    }
}
