#pragma once

#include "Scene/Scene.h"
#include "Scene/Components/AudioComponents.h"
#include "Scene/Components/PhysicsComponents.h"
#include "Scene/Components/RenderingComponents.h"
#include "Scene/Components/ScriptingComponents.h"
#include "Scene/Components/TilemapComponents.h"

#include "Core/Concurrency/JobSystem.h"

#include <atomic>
#include <string>
#include <string_view>
#include <vector>

namespace Limitless
{
    std::string ResolveRegisteredScriptClassNameForSceneRuntime(const std::string& requestedClassName,
                                                                const std::string& scriptAssetRelativePath);
    void ProcessUiInteractionSystemForSceneRuntime(Scene& scene, uint32_t windowWidth, uint32_t windowHeight);
    void UpdateAnimation2DSystemForSceneRuntime(Scene& scene, float deltaTime, uint64_t dispatchFrame);

    namespace SceneScriptRuntimeInternal
    {
        extern std::atomic<uint64_t> s_ParallelScriptAccessMaskMismatchCount;

        size_t ResolveParallelScriptMinSlots(const Concurrency::JobSystem& jobSystem);
        size_t ResolveParallelScriptMinBatchSize();

        std::vector<entt::entity> CollectOrderedScriptComponentEntities(const entt::registry& registry);

        NativeScriptEntry* TryGetScriptEntry(entt::registry& registry, entt::entity scriptEntity);
        const NativeScriptEntry* TryGetScriptEntry(const entt::registry& registry, entt::entity scriptEntity);

        ManagedScriptEntry* TryGetManagedScriptEntry(entt::registry& registry, entt::entity scriptEntity);
        const ManagedScriptEntry* TryGetManagedScriptEntry(const entt::registry& registry, entt::entity scriptEntity);

        entt::entity TryGetScriptOwnerEntity(const entt::registry& registry, entt::entity scriptEntity);

        bool HasTransformChangedForAccessValidation(const TransformComponent& before, const TransformComponent& after);
        bool HasHierarchyChangedForAccessValidation(const HierarchyComponent& before, const HierarchyComponent& after);
        bool HasRigidbodyChangedForAccessValidation(const Rigidbody2DComponent& before, const Rigidbody2DComponent& after);
        bool HasBoxColliderChangedForAccessValidation(const BoxCollider2DComponent& before, const BoxCollider2DComponent& after);
        bool HasCircleColliderChangedForAccessValidation(const CircleCollider2DComponent& before, const CircleCollider2DComponent& after);
        bool HasPointListChangedForAccessValidation(const std::vector<glm::vec2>& before, const std::vector<glm::vec2>& after);
        bool HasPolygonColliderChangedForAccessValidation(const PolygonCollider2DComponent& before, const PolygonCollider2DComponent& after);
        bool HasEdgeColliderChangedForAccessValidation(const EdgeCollider2DComponent& before, const EdgeCollider2DComponent& after);
        bool HasCapsuleColliderChangedForAccessValidation(const CapsuleCollider2DComponent& before, const CapsuleCollider2DComponent& after);
        bool HasJoint2DChangedForAccessValidation(const Joint2DComponent& before, const Joint2DComponent& after);
        bool HasTagChangedForAccessValidation(const TagComponent& before, const TagComponent& after);
        bool HasSpriteChangedForAccessValidation(const SpriteComponent& before, const SpriteComponent& after);
        bool HasMaterialChangedForAccessValidation(const MaterialComponent& before, const MaterialComponent& after);
        bool HasCanvasChangedForAccessValidation(const CanvasComponent& before, const CanvasComponent& after);
        bool HasRectTransformChangedForAccessValidation(const RectTransformComponent& before, const RectTransformComponent& after);
        bool HasUIImageChangedForAccessValidation(const UIImageComponent& before, const UIImageComponent& after);
        bool HasUIPanelChangedForAccessValidation(const UIPanelComponent& before, const UIPanelComponent& after);
        bool HasUITextChangedForAccessValidation(const UITextComponent& before, const UITextComponent& after);
        bool HasUIButtonChangedForAccessValidation(const UIButtonComponent& before, const UIButtonComponent& after);
        bool HasUISliderChangedForAccessValidation(const UISliderComponent& before, const UISliderComponent& after);
        bool HasDirectionalLight2DChangedForAccessValidation(const DirectionalLight2DComponent& before, const DirectionalLight2DComponent& after);
        bool HasPointLight2DChangedForAccessValidation(const PointLight2DComponent& before, const PointLight2DComponent& after);
        bool HasShadowOccluder2DChangedForAccessValidation(const ShadowOccluder2DComponent& before, const ShadowOccluder2DComponent& after);
        bool HasAudioListener2DChangedForAccessValidation(const AudioListener2DComponent& before, const AudioListener2DComponent& after);
        bool HasAudioListener3DChangedForAccessValidation(const AudioListener3DComponent& before, const AudioListener3DComponent& after);
        bool HasAudioSourceChangedForAccessValidation(const AudioSourceComponent& before, const AudioSourceComponent& after);
        bool HasCameraChangedForAccessValidation(const CameraComponent& before, const CameraComponent& after);
        bool HasPrefabInstanceChangedForAccessValidation(const PrefabInstanceComponent& before, const PrefabInstanceComponent& after);
        bool HasGrid2DChangedForAccessValidation(const Grid2DComponent& before, const Grid2DComponent& after);
        bool HasTilemapLayerChangedForAccessValidation(const TilemapLayerComponent& before, const TilemapLayerComponent& after);

        /// Lightweight snapshot for tilemap change detection that avoids
        /// deep-copying Tiles/CachedTileRender/CachedPaintedCells vectors.
        /// Uses MutationRevision as fast path + bounded content hash as
        /// fallback for detecting direct Tiles[] writes from native scripts.
        struct TilemapLayerValidationSnapshot
        {
            uint64_t MutationRevision = 0;
            int32_t RenderOrder = 0;
            bool CollisionEnabled = false;
            bool CastShadows = false;
            size_t TilesCount = 0;
            size_t TileTableCount = 0;
            uint64_t ContentHash = 0;
        };

        uint64_t HashTilemapLayerContentForValidation(const TilemapLayerComponent& layer);
        TilemapLayerValidationSnapshot SnapshotTilemapLayerForValidation(const TilemapLayerComponent& layer);
        bool HasTilemapLayerChangedFromSnapshot(const TilemapLayerValidationSnapshot& before, const TilemapLayerComponent& after);
        bool HasAnimatorChangedForAccessValidation(const AnimatorComponent& before, const AnimatorComponent& after);
        bool HasAnimationEventReceiverChangedForAccessValidation(const AnimationEventReceiverComponent& before, const AnimationEventReceiverComponent& after);
        bool HasParticleEmitterChangedForAccessValidation(const ParticleEmitterComponent& before, const ParticleEmitterComponent& after);

        std::string DescribeAccessMask(uint64_t mask);
    }
}
