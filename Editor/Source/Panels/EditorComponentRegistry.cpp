#include "EditorComponentRegistry.h"
#include "Audio/AudioEngine.h"

namespace Limitless::EditorInspectorPanel
{
    namespace
    {
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

        const ComponentRegistryEntry kAudioListener2DEntry{
            "Audio Listener 2D",
            "Add Audio Listener 2D Component",
            "Remove Audio Listener 2D Component",
            &HasComponent<AudioListener2DComponent>,
            &AddComponent<AudioListener2DComponent>,
            &RemoveComponent<AudioListener2DComponent>
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
    }

    const ComponentRegistryEntry* FindComponentRegistryEntry(ComponentRegistryKey key)
    {
        switch (key)
        {
            case ComponentRegistryKey::AudioListener2D:
                return &kAudioListener2DEntry;
            case ComponentRegistryKey::Rigidbody2D:
                return &kRigidbody2DEntry;
            case ComponentRegistryKey::BoxCollider2D:
                return &kBoxCollider2DEntry;
            case ComponentRegistryKey::ParticleEmitter:
                return &kParticleEmitterEntry;
            case ComponentRegistryKey::CircleCollider2D:
                return &kCircleCollider2DEntry;
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
            default:
                break;
        }

        return nullptr;
    }
}
