#include "Scene/Scene.h"
#include "Scene/Components/AudioComponents.h"
#include "Scene/Components/PhysicsComponents.h"
#include "Scene/Components/RenderingComponents.h"
#include "Scene/Components/ScriptingComponents.h"
#include "Scene/Components/TilemapComponents.h"

#include "Scripting/Coroutine.h"

#include <algorithm>
#include <unordered_map>

namespace Limitless
{
    namespace
    {
        void ResetAnimatorRuntimeOutput(AnimatorComponent& animator, bool clearSpriteTextureOverrideCache)
        {
            animator.RuntimeHasSpriteSubRect = false;
            animator.RuntimeSpriteUvMin = glm::vec2(0.0f, 0.0f);
            animator.RuntimeSpriteUvMax = glm::vec2(1.0f, 1.0f);
            animator.RuntimeSpriteTextureOverrideKey.clear();
            if (clearSpriteTextureOverrideCache)
            {
                animator.RuntimeCachedSpriteTextureOverride.reset();
                animator.RuntimeSpriteTextureOverrideLoadAttempted = false;
                animator.RuntimeAppliedPositionOffset = glm::vec3(0.0f);
                animator.RuntimeAppliedScaleOffset = glm::vec3(0.0f);
                animator.RuntimeAppliedRotationOffset = glm::vec3(0.0f);
            }
            animator.RuntimeHasPosition = false;
            animator.RuntimeHasScale = false;
            animator.RuntimeHasRotation = false;
            animator.RuntimePosition = glm::vec3(0.0f);
            animator.RuntimeScale = glm::vec3(0.0f);
            animator.RuntimeRotation = glm::vec3(0.0f);
        }

        // Keep clone runtime cleanup centralized so newly added components
        // only need one update point to stay Play Mode-safe.
        void ResetRuntimeStateForClonedEntity(entt::registry& registry, entt::entity entity)
        {
            if (auto* sprite = registry.try_get<SpriteComponent>(entity))
            {
                sprite->CachedTexture.reset();
                sprite->TextureLoadAttempted = false;
            }

            if (auto* material = registry.try_get<MaterialComponent>(entity))
            {
                material->CachedMaterial.reset();
                material->MaterialLoadAttempted = false;
            }

            if (auto* uiText = registry.try_get<UITextComponent>(entity))
            {
                uiText->CachedFont.reset();
                uiText->FontLoadAttempted = false;
            }

            if (auto* uiButton = registry.try_get<UIButtonComponent>(entity))
            {
                uiButton->IsHovered = false;
                uiButton->IsPressed = false;
                uiButton->RuntimeHoverEnteredThisFrame = false;
                uiButton->RuntimeHoverExitedThisFrame = false;
                uiButton->RuntimePressedThisFrame = false;
                uiButton->RuntimeClickedThisFrame = false;
                uiButton->OnClicked.Clear();
                uiButton->OnHoverEnter.Clear();
                uiButton->OnHoverExit.Clear();
                uiButton->OnPressed.Clear();
            }

            if (auto* uiSlider = registry.try_get<UISliderComponent>(entity))
            {
                uiSlider->RuntimeDragging = false;
                uiSlider->RuntimeValueChangedThisFrame = false;
                uiSlider->OnValueChanged.Clear();
            }

            if (auto* directionalLight = registry.try_get<DirectionalLight2DComponent>(entity))
                directionalLight->RuntimeResolvedDirection = glm::vec2(0.0f, -1.0f);

            if (auto* pointLight = registry.try_get<PointLight2DComponent>(entity))
            {
                pointLight->RuntimeViewportPosition = glm::vec2(0.0f);
                pointLight->RuntimeViewportRadius = 0.0f;
            }

            if (auto* shadowOccluder = registry.try_get<ShadowOccluder2DComponent>(entity))
            {
                shadowOccluder->RuntimeResolvedPolygonPoints.clear();
                shadowOccluder->RuntimeGeometryRevision = 0;
            }

            if (auto* audioListener3D = registry.try_get<AudioListener3DComponent>(entity))
            {
                audioListener3D->RuntimeHasPreviousWorldPosition = false;
                audioListener3D->RuntimePreviousWorldPosition = glm::vec3(0.0f);
            }

            if (auto* audioSource = registry.try_get<AudioSourceComponent>(entity))
            {
                audioSource->RuntimeVoiceId = 0;
                audioSource->RuntimePlaybackStarted = false;
                audioSource->RuntimePlayOnStartConsumed = false;
                audioSource->RuntimeHasPreviousWorldPosition = false;
                audioSource->RuntimePreviousWorldPosition = glm::vec3(0.0f);
            }

            if (auto* rigidbody2D = registry.try_get<Rigidbody2DComponent>(entity))
            {
                rigidbody2D->RuntimeBodyId = kNullPhysics2DBody;
                rigidbody2D->RuntimeBodyCreated = false;
                rigidbody2D->RuntimePreviousPosition = glm::vec2(0.0f);
                rigidbody2D->RuntimePreviousAngleRadians = 0.0f;
                rigidbody2D->RuntimeRenderPreviousPosition = glm::vec2(0.0f);
                rigidbody2D->RuntimeRenderPreviousAngleRadians = 0.0f;
                rigidbody2D->RuntimeRenderCurrentPosition = glm::vec2(0.0f);
                rigidbody2D->RuntimeRenderCurrentAngleRadians = 0.0f;
                rigidbody2D->RuntimeLinearVelocity = glm::vec2(0.0f);
                rigidbody2D->RuntimePendingLinearVelocity = glm::vec2(0.0f);
                rigidbody2D->RuntimeHasPendingLinearVelocity = false;
                rigidbody2D->RuntimePendingLinearVelocityX = 0.0f;
                rigidbody2D->RuntimeHasPendingLinearVelocityX = false;
                rigidbody2D->RuntimePendingLinearVelocityY = 0.0f;
                rigidbody2D->RuntimeHasPendingLinearVelocityY = false;
                rigidbody2D->RuntimeContactCount = 0;
                rigidbody2D->RuntimeContactCountExcludingSensors = 0;
            }

            if (auto* boxCollider2D = registry.try_get<BoxCollider2DComponent>(entity))
            {
                boxCollider2D->RuntimeShapeId = kNullPhysics2DShape;
                boxCollider2D->RuntimeShapeCreated = false;
            }

            if (auto* circleCollider2D = registry.try_get<CircleCollider2DComponent>(entity))
            {
                circleCollider2D->RuntimeShapeId = kNullPhysics2DShape;
                circleCollider2D->RuntimeShapeCreated = false;
            }

            if (auto* polygonCollider2D = registry.try_get<PolygonCollider2DComponent>(entity))
            {
                polygonCollider2D->RuntimeShapeId = kNullPhysics2DShape;
                polygonCollider2D->RuntimeShapeCreated = false;
            }

            if (auto* edgeCollider2D = registry.try_get<EdgeCollider2DComponent>(entity))
            {
                edgeCollider2D->RuntimeShapeId = kNullPhysics2DShape;
                edgeCollider2D->RuntimeShapeCreated = false;
            }

            if (auto* capsuleCollider2D = registry.try_get<CapsuleCollider2DComponent>(entity))
            {
                capsuleCollider2D->RuntimeShapeId = kNullPhysics2DShape;
                capsuleCollider2D->RuntimeShapeCreated = false;
            }

            if (auto* joint2D = registry.try_get<Joint2DComponent>(entity))
            {
                joint2D->RuntimeJointId = kNullPhysics2DJoint;
                joint2D->RuntimeJointCreated = false;
            }

            if (auto* animator = registry.try_get<AnimatorComponent>(entity))
            {
                animator->CachedController.reset();
                animator->ControllerLoadAttempted = false;
                animator->CachedDefaultClip.reset();
                animator->DefaultClipLoadAttempted = false;
                animator->RuntimeInitialized = false;
                animator->RuntimeCurrentStateName.clear();
                animator->RuntimeCurrentClipKey.clear();
                animator->RuntimePreviousStateTimeSeconds = 0.0f;
                animator->RuntimeStateTimeSeconds = 0.0f;
                animator->RuntimeCurrentStateDurationSeconds = 1.0f;
                animator->RuntimeStateSpeedMultiplier = 1.0f;
                animator->ResetAllTriggers();
                ResetAnimatorRuntimeOutput(*animator, true);
            }

            if (auto* eventReceiver = registry.try_get<AnimationEventReceiverComponent>(entity))
            {
                eventReceiver->RuntimeDispatchedEvents.clear();
                eventReceiver->RuntimeDispatchFrame = 0;
            }

            if (auto* particleEmitter = registry.try_get<ParticleEmitterComponent>(entity))
            {
                particleEmitter->CachedTexture.reset();
                particleEmitter->TextureLoadAttempted = false;
                particleEmitter->RuntimeState.reset();
                particleEmitter->Playing = false;
                particleEmitter->Paused = false;
            }

            auto scriptView = registry.view<ScriptComponent>();
            for (entt::entity scriptEntity : scriptView)
            {
                auto& scriptComponent = scriptView.get<ScriptComponent>(scriptEntity);
                if (scriptComponent.OwnerEntity != entity)
                    continue;
                if (NativeScriptEntry* scriptEntry = scriptComponent.TryGetNativeEntry())
                {
                    scriptEntry->RuntimeInitialized = false;
                    if (scriptEntry->RuntimeInstance)
                        Coroutine::StopAll(*scriptEntry->RuntimeInstance);
                    scriptEntry->RuntimeInstance.reset();
                    scriptEntry->RuntimeUpdateCount = 0;
                    scriptEntry->RuntimeWarnedOnUpdateTransformMutation = false;
                    scriptEntry->RuntimeWarnedMissingAccessDeclaration = false;
                    scriptEntry->RuntimeWarnedAccessMaskMismatch = false;
                }
                else if (ManagedScriptEntry* scriptEntry = scriptComponent.TryGetManagedEntry())
                {
                    scriptEntry->RuntimeInstanceId = 0;
                    scriptEntry->RuntimeInitialized = false;
                    scriptEntry->RuntimeUpdateCount = 0;
                    scriptEntry->RuntimeWarnedOnUpdateTransformMutation = false;
                    scriptEntry->RuntimeWarnedMissingHost = false;
                    scriptEntry->RuntimeWarnedMissingClass = false;
                }
            }
        }
    }

    std::unique_ptr<Scene> Scene::Clone() const
    {
        auto clone = std::make_unique<Scene>();
        const auto& sourceRegistry = GetRegistry();
        auto& destinationRegistry = clone->GetRegistry();
        std::unordered_map<entt::entity, entt::entity> entityMap;

        auto view = sourceRegistry.view<TagComponent, TransformComponent>();
        for (entt::entity sourceEntity : view)
        {
            const auto& tag = view.get<TagComponent>(sourceEntity);
            const auto& transform = view.get<TransformComponent>(sourceEntity);

            // CreateEntity ensures default baseline components are initialized first.
            entt::entity destinationEntity = clone->CreateEntity(tag.Tag);
            entityMap.emplace(sourceEntity, destinationEntity);
            if (auto* destinationTag = destinationRegistry.try_get<TagComponent>(destinationEntity))
                destinationTag->Enabled = tag.Enabled;
            destinationRegistry.replace<TransformComponent>(destinationEntity, transform);

            if (const auto* canvas = sourceRegistry.try_get<CanvasComponent>(sourceEntity))
            {
                destinationRegistry.emplace<CanvasComponent>(destinationEntity, *canvas);
            }

            if (const auto* rectTransform = sourceRegistry.try_get<RectTransformComponent>(sourceEntity))
            {
                destinationRegistry.emplace<RectTransformComponent>(destinationEntity, *rectTransform);
            }

            if (const auto* sprite = sourceRegistry.try_get<SpriteComponent>(sourceEntity))
            {
                auto& destinationSprite = destinationRegistry.emplace<SpriteComponent>(destinationEntity);
                destinationSprite.TextureKey = sprite->TextureKey;
                destinationSprite.CachedTexture.reset();
                destinationSprite.TextureLoadAttempted = false;
                destinationSprite.Color = sprite->Color;
                destinationSprite.TilingFactor = sprite->TilingFactor;
                destinationSprite.SubSpriteIndex = sprite->SubSpriteIndex;
                destinationSprite.UvMin = sprite->UvMin;
                destinationSprite.UvMax = sprite->UvMax;
                destinationSprite.RenderOrder = sprite->RenderOrder;
                destinationSprite.CastShadows = sprite->CastShadows;
                destinationSprite.ReceiveShadows = sprite->ReceiveShadows;
            }

            if (const auto* animator = sourceRegistry.try_get<AnimatorComponent>(sourceEntity))
            {
                destinationRegistry.emplace<AnimatorComponent>(destinationEntity, *animator);
            }

            if (const auto* animationEventReceiver = sourceRegistry.try_get<AnimationEventReceiverComponent>(sourceEntity))
            {
                auto& destinationReceiver = destinationRegistry.emplace<AnimationEventReceiverComponent>(destinationEntity, *animationEventReceiver);
                destinationReceiver.RuntimeDispatchedEvents.clear();
                destinationReceiver.RuntimeDispatchFrame = 0;
            }

            if (const auto* material = sourceRegistry.try_get<MaterialComponent>(sourceEntity))
            {
                auto& destinationMaterial = destinationRegistry.emplace<MaterialComponent>(destinationEntity);
                destinationMaterial.MaterialKey = material->MaterialKey;
                destinationMaterial.CachedMaterial.reset();
                destinationMaterial.MaterialLoadAttempted = false;
            }

            if (const auto* directionalLight = sourceRegistry.try_get<DirectionalLight2DComponent>(sourceEntity))
            {
                auto& destinationDirectionalLight = destinationRegistry.emplace<DirectionalLight2DComponent>(destinationEntity, *directionalLight);
                destinationDirectionalLight.RuntimeResolvedDirection = glm::vec2(0.0f, -1.0f);
            }

            if (const auto* pointLight = sourceRegistry.try_get<PointLight2DComponent>(sourceEntity))
            {
                auto& destinationPointLight = destinationRegistry.emplace<PointLight2DComponent>(destinationEntity, *pointLight);
                destinationPointLight.RuntimeViewportPosition = glm::vec2(0.0f);
                destinationPointLight.RuntimeViewportRadius = 0.0f;
            }

            if (const auto* shadowOccluder = sourceRegistry.try_get<ShadowOccluder2DComponent>(sourceEntity))
            {
                auto& destinationShadowOccluder = destinationRegistry.emplace<ShadowOccluder2DComponent>(destinationEntity, *shadowOccluder);
                destinationShadowOccluder.RuntimeResolvedPolygonPoints.clear();
                destinationShadowOccluder.RuntimeGeometryRevision = 0;
            }

            if (const auto* uiImage = sourceRegistry.try_get<UIImageComponent>(sourceEntity))
            {
                destinationRegistry.emplace<UIImageComponent>(destinationEntity, *uiImage);
            }

            if (const auto* uiPanel = sourceRegistry.try_get<UIPanelComponent>(sourceEntity))
            {
                destinationRegistry.emplace<UIPanelComponent>(destinationEntity, *uiPanel);
            }

            if (const auto* uiText = sourceRegistry.try_get<UITextComponent>(sourceEntity))
            {
                auto& destinationUIText = destinationRegistry.emplace<UITextComponent>(destinationEntity);
                destinationUIText.Text = uiText->Text;
                destinationUIText.FontFilePath = uiText->FontFilePath;
                destinationUIText.CachedFont.reset();
                destinationUIText.FontLoadAttempted = false;
                destinationUIText.FontSize = uiText->FontSize;
                destinationUIText.Color = uiText->Color;
                destinationUIText.RaycastTarget = uiText->RaycastTarget;
            }

            if (const auto* uiButton = sourceRegistry.try_get<UIButtonComponent>(sourceEntity))
            {
                auto& destinationButton = destinationRegistry.emplace<UIButtonComponent>(destinationEntity, *uiButton);
                destinationButton.IsHovered = false;
                destinationButton.IsPressed = false;
                destinationButton.RuntimeHoverEnteredThisFrame = false;
                destinationButton.RuntimeHoverExitedThisFrame = false;
                destinationButton.RuntimePressedThisFrame = false;
                destinationButton.RuntimeClickedThisFrame = false;
                destinationButton.OnClicked.Clear();
                destinationButton.OnHoverEnter.Clear();
                destinationButton.OnHoverExit.Clear();
                destinationButton.OnPressed.Clear();
            }

            if (const auto* uiSlider = sourceRegistry.try_get<UISliderComponent>(sourceEntity))
            {
                auto& destinationSlider = destinationRegistry.emplace<UISliderComponent>(destinationEntity, *uiSlider);
                destinationSlider.Value = std::clamp(destinationSlider.Value, destinationSlider.MinValue, destinationSlider.MaxValue);
                destinationSlider.RuntimeDragging = false;
                destinationSlider.RuntimeValueChangedThisFrame = false;
                destinationSlider.OnValueChanged.Clear();
            }

            if (const auto* grid2D = sourceRegistry.try_get<Grid2DComponent>(sourceEntity))
            {
                destinationRegistry.emplace<Grid2DComponent>(destinationEntity, *grid2D);
            }

            if (const auto* tilemapLayer = sourceRegistry.try_get<TilemapLayerComponent>(sourceEntity))
            {
                auto& destinationLayer = destinationRegistry.emplace<TilemapLayerComponent>(destinationEntity, *tilemapLayer);
                destinationLayer.CachedTileRender.clear();
                destinationLayer.RenderCacheDirty = true;
            }

            if (const auto* camera = sourceRegistry.try_get<CameraComponent>(sourceEntity))
            {
                destinationRegistry.emplace<CameraComponent>(destinationEntity, *camera);
            }

            if (const auto* audioListener = sourceRegistry.try_get<AudioListener2DComponent>(sourceEntity))
            {
                destinationRegistry.emplace<AudioListener2DComponent>(destinationEntity, *audioListener);
            }

            if (const auto* audioListener3D = sourceRegistry.try_get<AudioListener3DComponent>(sourceEntity))
            {
                auto& destinationAudioListener3D = destinationRegistry.emplace<AudioListener3DComponent>(destinationEntity, *audioListener3D);
                destinationAudioListener3D.RuntimeHasPreviousWorldPosition = false;
                destinationAudioListener3D.RuntimePreviousWorldPosition = glm::vec3(0.0f);
            }

            if (const auto* rigidbody2D = sourceRegistry.try_get<Rigidbody2DComponent>(sourceEntity))
            {
                auto& destinationRigidbody2D = destinationRegistry.emplace<Rigidbody2DComponent>(destinationEntity, *rigidbody2D);
                destinationRigidbody2D.RuntimeBodyId = kNullPhysics2DBody;
                destinationRigidbody2D.RuntimeBodyCreated = false;
                destinationRigidbody2D.RuntimeWorldSlot = 0;
                destinationRigidbody2D.RuntimePreviousPosition = glm::vec2(0.0f);
                destinationRigidbody2D.RuntimePreviousAngleRadians = 0.0f;
                destinationRigidbody2D.RuntimeRenderPreviousPosition = glm::vec2(0.0f);
                destinationRigidbody2D.RuntimeRenderPreviousAngleRadians = 0.0f;
                destinationRigidbody2D.RuntimeRenderCurrentPosition = glm::vec2(0.0f);
                destinationRigidbody2D.RuntimeRenderCurrentAngleRadians = 0.0f;
                destinationRigidbody2D.RuntimeLinearVelocity = glm::vec2(0.0f);
                destinationRigidbody2D.RuntimePendingLinearVelocity = glm::vec2(0.0f);
                destinationRigidbody2D.RuntimeHasPendingLinearVelocity = false;
                destinationRigidbody2D.RuntimePendingLinearVelocityX = 0.0f;
                destinationRigidbody2D.RuntimeHasPendingLinearVelocityX = false;
                destinationRigidbody2D.RuntimePendingLinearVelocityY = 0.0f;
                destinationRigidbody2D.RuntimeHasPendingLinearVelocityY = false;
                destinationRigidbody2D.RuntimeContactCount = 0;
                destinationRigidbody2D.RuntimeContactCountExcludingSensors = 0;
            }

            if (const auto* boxCollider2D = sourceRegistry.try_get<BoxCollider2DComponent>(sourceEntity))
            {
                auto& destinationBoxCollider2D = destinationRegistry.emplace<BoxCollider2DComponent>(destinationEntity, *boxCollider2D);
                destinationBoxCollider2D.RuntimeShapeId = kNullPhysics2DShape;
                destinationBoxCollider2D.RuntimeShapeCreated = false;
            }

            if (const auto* circleCollider2D = sourceRegistry.try_get<CircleCollider2DComponent>(sourceEntity))
            {
                auto& destinationCircleCollider2D = destinationRegistry.emplace<CircleCollider2DComponent>(destinationEntity, *circleCollider2D);
                destinationCircleCollider2D.RuntimeShapeId = kNullPhysics2DShape;
                destinationCircleCollider2D.RuntimeShapeCreated = false;
            }

            if (const auto* polygonCollider2D = sourceRegistry.try_get<PolygonCollider2DComponent>(sourceEntity))
            {
                auto& destinationPolygonCollider2D = destinationRegistry.emplace<PolygonCollider2DComponent>(destinationEntity, *polygonCollider2D);
                destinationPolygonCollider2D.RuntimeShapeId = kNullPhysics2DShape;
                destinationPolygonCollider2D.RuntimeShapeCreated = false;
            }

            if (const auto* edgeCollider2D = sourceRegistry.try_get<EdgeCollider2DComponent>(sourceEntity))
            {
                auto& destinationEdgeCollider2D = destinationRegistry.emplace<EdgeCollider2DComponent>(destinationEntity, *edgeCollider2D);
                destinationEdgeCollider2D.RuntimeShapeId = kNullPhysics2DShape;
                destinationEdgeCollider2D.RuntimeShapeCreated = false;
            }

            if (const auto* capsuleCollider2D = sourceRegistry.try_get<CapsuleCollider2DComponent>(sourceEntity))
            {
                auto& destinationCapsuleCollider2D = destinationRegistry.emplace<CapsuleCollider2DComponent>(destinationEntity, *capsuleCollider2D);
                destinationCapsuleCollider2D.RuntimeShapeId = kNullPhysics2DShape;
                destinationCapsuleCollider2D.RuntimeShapeCreated = false;
            }

            if (const auto* joint2D = sourceRegistry.try_get<Joint2DComponent>(sourceEntity))
            {
                auto& destinationJoint2D = destinationRegistry.emplace<Joint2DComponent>(destinationEntity, *joint2D);
                destinationJoint2D.RuntimeJointId = kNullPhysics2DJoint;
                destinationJoint2D.RuntimeJointCreated = false;
                destinationJoint2D.RuntimeWorldSlot = 0;
            }

            if (const auto* audioSource = sourceRegistry.try_get<AudioSourceComponent>(sourceEntity))
            {
                auto& destinationAudioSource = destinationRegistry.emplace<AudioSourceComponent>(destinationEntity);
                destinationAudioSource.AudioClipKey = audioSource->AudioClipKey;
                destinationAudioSource.Volume = audioSource->Volume;
                destinationAudioSource.Pitch = audioSource->Pitch;
                destinationAudioSource.PlayOnStart = audioSource->PlayOnStart;
                destinationAudioSource.Loop = audioSource->Loop;
                destinationAudioSource.Muted = audioSource->Muted;
                destinationAudioSource.Space = audioSource->Space;
                destinationAudioSource.MixerGroup = audioSource->MixerGroup;
                destinationAudioSource.SpatialMinDistance = audioSource->SpatialMinDistance;
                destinationAudioSource.SpatialMaxDistance = audioSource->SpatialMaxDistance;
                destinationAudioSource.SpatialRolloffExponent = audioSource->SpatialRolloffExponent;
                destinationAudioSource.StereoPanStrength = audioSource->StereoPanStrength;
                destinationAudioSource.SpatialRolloffMode = audioSource->SpatialRolloffMode;
                destinationAudioSource.DopplerFactor = audioSource->DopplerFactor;
                destinationAudioSource.EnableDirectionalAttenuation = audioSource->EnableDirectionalAttenuation;
                destinationAudioSource.DirectionalInnerAngleDegrees = audioSource->DirectionalInnerAngleDegrees;
                destinationAudioSource.DirectionalOuterAngleDegrees = audioSource->DirectionalOuterAngleDegrees;
                destinationAudioSource.DirectionalOuterVolume = audioSource->DirectionalOuterVolume;
                destinationAudioSource.AttenuationCurveKey = audioSource->AttenuationCurveKey;
                destinationAudioSource.RuntimeVoiceId = 0;
                destinationAudioSource.RuntimePlaybackStarted = false;
                destinationAudioSource.RuntimePlayOnStartConsumed = false;
                destinationAudioSource.RuntimeHasPreviousWorldPosition = false;
                destinationAudioSource.RuntimePreviousWorldPosition = glm::vec3(0.0f);
            }

            auto sourceScriptView = sourceRegistry.view<ScriptComponent>();
            for (entt::entity sourceScriptEntity : sourceScriptView)
            {
                const auto& sourceScriptComponent = sourceScriptView.get<ScriptComponent>(sourceScriptEntity);
                if (sourceScriptComponent.OwnerEntity != sourceEntity)
                    continue;

                entt::entity destinationScriptEntity = entt::null;
                if (const NativeScriptEntry* sourceScriptEntry = sourceScriptComponent.TryGetNativeEntry())
                {
                    NativeScriptEntry destinationScriptEntry{};
                    destinationScriptEntry.ScriptClassName = sourceScriptEntry->ScriptClassName;
                    destinationScriptEntry.ScriptAssetRelativePath = sourceScriptEntry->ScriptAssetRelativePath;
                    destinationScriptEntry.Enabled = sourceScriptEntry->Enabled;
                    destinationScriptEntry.ExposedProperties = sourceScriptEntry->ExposedProperties;
                    destinationScriptEntry.ExecutionPolicy = sourceScriptEntry->ExecutionPolicy;
                    destinationScriptEntry.DeclaredReadAccessMask = sourceScriptEntry->DeclaredReadAccessMask;
                    destinationScriptEntry.DeclaredWriteAccessMask = sourceScriptEntry->DeclaredWriteAccessMask;
                    destinationScriptEntry.RuntimeExposedPropertiesRevision = 1;
                    destinationScriptEntity = clone->AttachScriptComponent(destinationEntity, std::move(destinationScriptEntry));
                }
                else if (const ManagedScriptEntry* sourceScriptEntry = sourceScriptComponent.TryGetManagedEntry())
                {
                    ManagedScriptEntry destinationScriptEntry{};
                    destinationScriptEntry.ScriptClassName = sourceScriptEntry->ScriptClassName;
                    destinationScriptEntry.ScriptAssetRelativePath = sourceScriptEntry->ScriptAssetRelativePath;
                    destinationScriptEntry.Enabled = sourceScriptEntry->Enabled;
                    destinationScriptEntry.ExposedProperties = sourceScriptEntry->ExposedProperties;
                    destinationScriptEntry.RuntimeExposedPropertiesRevision = 1;
                    destinationScriptEntity = clone->AttachManagedScriptComponent(destinationEntity, std::move(destinationScriptEntry));
                }
                if (auto* attachedScriptComponent = destinationRegistry.try_get<ScriptComponent>(destinationScriptEntity))
                    attachedScriptComponent->ComponentOrder = sourceScriptComponent.ComponentOrder;
            }

            if (const auto* particleEmitter = sourceRegistry.try_get<ParticleEmitterComponent>(sourceEntity))
            {
                // Copy constructor copies authoring fields and resets runtime state
                destinationRegistry.emplace<ParticleEmitterComponent>(destinationEntity, *particleEmitter);
            }

            if (const auto* prefabInstance = sourceRegistry.try_get<PrefabInstanceComponent>(sourceEntity))
            {
                destinationRegistry.emplace<PrefabInstanceComponent>(destinationEntity, *prefabInstance);
            }

            ResetRuntimeStateForClonedEntity(destinationRegistry, destinationEntity);
        }

        for (const auto& [sourceEntity, destinationEntity] : entityMap)
        {
            const auto* sourceHierarchy = sourceRegistry.try_get<HierarchyComponent>(sourceEntity);
            if (!sourceHierarchy)
                continue;

            auto* destinationHierarchy = destinationRegistry.try_get<HierarchyComponent>(destinationEntity);
            if (!destinationHierarchy)
                destinationHierarchy = &destinationRegistry.emplace<HierarchyComponent>(destinationEntity);

            // Preserve exact local transform values from edit scene.
            // Using SetParent() would preserve world transform and rewrite local transform,
            // which causes children to shift when entering Play Mode.
            destinationHierarchy->Parent = entt::null;
            if (sourceHierarchy->Parent != entt::null)
            {
                auto foundParent = entityMap.find(sourceHierarchy->Parent);
                if (foundParent != entityMap.end())
                    destinationHierarchy->Parent = foundParent->second;
            }
            destinationHierarchy->SiblingOrder = sourceHierarchy->SiblingOrder;
        }

        for (const auto& [sourceEntity, destinationEntity] : entityMap)
        {
            const auto* sourceJoint = sourceRegistry.try_get<Joint2DComponent>(sourceEntity);
            auto* destinationJoint = destinationRegistry.try_get<Joint2DComponent>(destinationEntity);
            if (!sourceJoint || !destinationJoint)
                continue;
            if (sourceJoint->ConnectedEntity == entt::null)
            {
                destinationJoint->ConnectedEntity = entt::null;
                continue;
            }
            const auto mappedConnectedEntity = entityMap.find(sourceJoint->ConnectedEntity);
            destinationJoint->ConnectedEntity = (mappedConnectedEntity != entityMap.end())
                ? mappedConnectedEntity->second
                : entt::null;
        }

        clone->m_TransformsDirty = true;
        clone->m_HierarchyDepthDirty = true;

        clone->m_EditorCameraBookmark = m_EditorCameraBookmark;
        clone->m_Physics2DSettings = m_Physics2DSettings;
        return clone;
    }
}
