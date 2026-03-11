#include "Scene/Scene.h"
#include "Scene/Components/AudioComponents.h"
#include "Scene/Components/PhysicsComponents.h"
#include "Scene/Components/RenderingComponents.h"
#include "Scene/Components/ScriptingComponents.h"

#include "Assets/AssetPaths.h"
#include "Scripting/Coroutine.h"

#include <cmath>
#include <unordered_map>
#include <vector>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

namespace Limitless
{
    namespace
    {
        constexpr float kParentInverseDeterminantEpsilon = 1e-6f;

        entt::entity FindFirstEntityByTag(const Scene& scene, const std::string& tag)
        {
            if (tag.empty())
                return entt::null;

            const auto& registry = scene.GetRegistry();
            auto view = registry.view<TagComponent>();
            for (entt::entity entity : view)
            {
                const auto& tagComponent = view.get<TagComponent>(entity);
                if (tagComponent.Tag == tag)
                    return entity;
            }

            return entt::null;
        }

        bool IsFiniteMatrix(const glm::mat4& matrix)
        {
            for (int column = 0; column < 4; ++column)
            {
                for (int row = 0; row < 4; ++row)
                {
                    if (!std::isfinite(matrix[column][row]))
                        return false;
                }
            }
            return true;
        }

        bool IsFiniteVec3(const glm::vec3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool IsFiniteQuat(const glm::quat& value)
        {
            return std::isfinite(value.w) && std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool TryAssignLocalTransformFromWorld(const glm::mat4& parentWorld,
                                              const glm::mat4& childWorld,
                                              TransformComponent& destinationTransform)
        {
            if (!IsFiniteMatrix(parentWorld) || !IsFiniteMatrix(childWorld))
                return false;

            const float determinant = glm::determinant(parentWorld);
            if (!std::isfinite(determinant) || std::abs(determinant) <= kParentInverseDeterminantEpsilon)
                return false;

            const glm::mat4 childLocal = glm::inverse(parentWorld) * childWorld;
            if (!IsFiniteMatrix(childLocal))
                return false;

            glm::vec3 skew(0.0f);
            glm::vec4 perspective(0.0f);
            glm::quat orientation(1.0f, 0.0f, 0.0f, 0.0f);
            glm::vec3 translation(0.0f);
            glm::vec3 scale(1.0f);
            if (!glm::decompose(childLocal, scale, orientation, translation, skew, perspective))
                return false;
            if (!IsFiniteVec3(translation) || !IsFiniteVec3(scale) || !IsFiniteQuat(orientation))
                return false;

            const float orientationLengthSquared =
                orientation.w * orientation.w +
                orientation.x * orientation.x +
                orientation.y * orientation.y +
                orientation.z * orientation.z;
            if (!std::isfinite(orientationLengthSquared) || orientationLengthSquared <= 0.0f)
                return false;
            orientation = glm::normalize(orientation);

            const glm::vec3 eulerDegrees = glm::degrees(glm::eulerAngles(orientation));
            if (!IsFiniteVec3(eulerDegrees))
                return false;

            destinationTransform.Position = translation;
            destinationTransform.Rotation = eulerDegrees;
            destinationTransform.Scale = scale;
            return true;
        }

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

        // Keep clone/load runtime cleanup centralized so newly added components
        // only need one update point to stay Play Mode-safe.
        void ResetRuntimeStateForEntity(entt::registry& registry, entt::entity entity)
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
                    scriptEntry->RuntimeWarnedMissingHost = false;
                    scriptEntry->RuntimeWarnedMissingClass = false;
                }
            }
        }

        entt::entity ResolveCopiedReferenceSourceEntity(const Scene& sourceScene, const ScriptEntityReference& reference)
        {
            if (!reference.SceneEntityId.empty())
            {
                const entt::entity resolvedEntity = sourceScene.FindEntityByPersistentId(reference.SceneEntityId);
                if (resolvedEntity != entt::null && sourceScene.IsValid(resolvedEntity))
                    return resolvedEntity;
            }

            return FindFirstEntityByTag(sourceScene, reference.Tag);
        }

        void RemapCopiedScriptEntityReferences(const Scene& sourceScene,
                                              Scene& destinationScene,
                                              const std::unordered_map<entt::entity, entt::entity>& entityMap,
                                              std::unordered_map<std::string, ScriptPropertyValue>& exposedProperties)
        {
            for (auto& [propertyName, propertyValue] : exposedProperties)
            {
                auto* entityReference = std::get_if<ScriptEntityReference>(&propertyValue);
                if (!entityReference || !entityReference->PrefabAssetKey.empty())
                    continue;

                const entt::entity sourceReferencedEntity = ResolveCopiedReferenceSourceEntity(sourceScene, *entityReference);
                const auto mappedReferencedEntity = entityMap.find(sourceReferencedEntity);
                if (mappedReferencedEntity == entityMap.end())
                {
                    if (sourceReferencedEntity != entt::null)
                        entityReference->SceneEntityId.clear();
                    continue;
                }

                entityReference->SceneEntityId = destinationScene.GetEntityPersistentId(mappedReferencedEntity->second);
                if (const auto* destinationTag = destinationScene.GetRegistry().try_get<TagComponent>(mappedReferencedEntity->second))
                    entityReference->Tag = destinationTag->Tag;
            }
        }

        bool CopyEntitySubtreeToScene(const Scene& sourceScene,
                                      Scene& destinationScene,
                                      entt::entity sourceRootEntity,
                                      entt::entity destinationParentEntity,
                                      entt::entity* outDestinationRootEntity)
        {
            if (!sourceScene.IsValid(sourceRootEntity))
                return false;

            const auto& sourceRegistry = sourceScene.GetRegistry();
            auto& destinationRegistry = destinationScene.GetRegistry();
            const entt::entity resolvedDestinationParentEntity =
                (destinationParentEntity != entt::null && destinationScene.IsValid(destinationParentEntity))
                ? destinationParentEntity
                : entt::null;

            std::vector<entt::entity> sourceEntities;
            sourceEntities.push_back(sourceRootEntity);
            for (size_t index = 0; index < sourceEntities.size(); ++index)
            {
                const auto children = sourceScene.GetChildren(sourceEntities[index]);
                sourceEntities.insert(sourceEntities.end(), children.begin(), children.end());
            }

            std::unordered_map<entt::entity, entt::entity> entityMap;
            entityMap.reserve(sourceEntities.size());
            std::unordered_map<entt::entity, entt::entity> scriptComponentMap;

            for (entt::entity sourceEntity : sourceEntities)
            {
                const auto* sourceTag = sourceRegistry.try_get<TagComponent>(sourceEntity);
                const auto* sourceTransform = sourceRegistry.try_get<TransformComponent>(sourceEntity);
                if (!sourceTag || !sourceTransform)
                    return false;

                const entt::entity destinationEntity = destinationScene.CreateEntity(sourceTag->Tag);
                entityMap.emplace(sourceEntity, destinationEntity);
                if (auto* destinationTag = destinationRegistry.try_get<TagComponent>(destinationEntity))
                    destinationTag->Enabled = sourceTag->Enabled;
                destinationRegistry.replace<TransformComponent>(destinationEntity, *sourceTransform);

                if (const auto* sourceCanvas = sourceRegistry.try_get<CanvasComponent>(sourceEntity))
                    destinationRegistry.emplace<CanvasComponent>(destinationEntity, *sourceCanvas);

                if (const auto* sourceRectTransform = sourceRegistry.try_get<RectTransformComponent>(sourceEntity))
                    destinationRegistry.emplace<RectTransformComponent>(destinationEntity, *sourceRectTransform);

                if (const auto* sourceSprite = sourceRegistry.try_get<SpriteComponent>(sourceEntity))
                {
                    auto& destinationSprite = destinationRegistry.emplace<SpriteComponent>(destinationEntity);
                    destinationSprite.TextureKey = sourceSprite->TextureKey;
                    destinationSprite.CachedTexture.reset();
                    destinationSprite.TextureLoadAttempted = false;
                    destinationSprite.Color = sourceSprite->Color;
                    destinationSprite.TilingFactor = sourceSprite->TilingFactor;
                    destinationSprite.SubSpriteIndex = sourceSprite->SubSpriteIndex;
                    destinationSprite.UvMin = sourceSprite->UvMin;
                    destinationSprite.UvMax = sourceSprite->UvMax;
                    destinationSprite.RenderOrder = sourceSprite->RenderOrder;
                    destinationSprite.CastShadows = sourceSprite->CastShadows;
                    destinationSprite.ReceiveShadows = sourceSprite->ReceiveShadows;
                }

                if (const auto* sourceAnimator = sourceRegistry.try_get<AnimatorComponent>(sourceEntity))
                    destinationRegistry.emplace<AnimatorComponent>(destinationEntity, *sourceAnimator);

                if (const auto* sourceAnimationEventReceiver = sourceRegistry.try_get<AnimationEventReceiverComponent>(sourceEntity))
                    destinationRegistry.emplace<AnimationEventReceiverComponent>(destinationEntity, *sourceAnimationEventReceiver);

                if (const auto* sourceMaterial = sourceRegistry.try_get<MaterialComponent>(sourceEntity))
                {
                    auto& destinationMaterial = destinationRegistry.emplace<MaterialComponent>(destinationEntity);
                    destinationMaterial.MaterialKey = sourceMaterial->MaterialKey;
                    destinationMaterial.CachedMaterial.reset();
                    destinationMaterial.MaterialLoadAttempted = false;
                }

                if (const auto* sourceDirectionalLight = sourceRegistry.try_get<DirectionalLight2DComponent>(sourceEntity))
                    destinationRegistry.emplace<DirectionalLight2DComponent>(destinationEntity, *sourceDirectionalLight);

                if (const auto* sourcePointLight = sourceRegistry.try_get<PointLight2DComponent>(sourceEntity))
                    destinationRegistry.emplace<PointLight2DComponent>(destinationEntity, *sourcePointLight);

                if (const auto* sourceShadowOccluder = sourceRegistry.try_get<ShadowOccluder2DComponent>(sourceEntity))
                    destinationRegistry.emplace<ShadowOccluder2DComponent>(destinationEntity, *sourceShadowOccluder);

                if (const auto* sourceUIImage = sourceRegistry.try_get<UIImageComponent>(sourceEntity))
                    destinationRegistry.emplace<UIImageComponent>(destinationEntity, *sourceUIImage);

                if (const auto* sourceUIPanel = sourceRegistry.try_get<UIPanelComponent>(sourceEntity))
                    destinationRegistry.emplace<UIPanelComponent>(destinationEntity, *sourceUIPanel);

                if (const auto* sourceUIText = sourceRegistry.try_get<UITextComponent>(sourceEntity))
                {
                    auto& destinationUIText = destinationRegistry.emplace<UITextComponent>(destinationEntity);
                    destinationUIText.Text = sourceUIText->Text;
                    destinationUIText.FontFilePath = sourceUIText->FontFilePath;
                    destinationUIText.CachedFont.reset();
                    destinationUIText.FontLoadAttempted = false;
                    destinationUIText.FontSize = sourceUIText->FontSize;
                    destinationUIText.Color = sourceUIText->Color;
                    destinationUIText.RaycastTarget = sourceUIText->RaycastTarget;
                }

                if (const auto* sourceUIButton = sourceRegistry.try_get<UIButtonComponent>(sourceEntity))
                    destinationRegistry.emplace<UIButtonComponent>(destinationEntity, *sourceUIButton);

                if (const auto* sourceUISlider = sourceRegistry.try_get<UISliderComponent>(sourceEntity))
                    destinationRegistry.emplace<UISliderComponent>(destinationEntity, *sourceUISlider);

                if (const auto* sourceCamera = sourceRegistry.try_get<CameraComponent>(sourceEntity))
                    destinationRegistry.emplace<CameraComponent>(destinationEntity, *sourceCamera);

                if (const auto* sourceAudioListener = sourceRegistry.try_get<AudioListener2DComponent>(sourceEntity))
                    destinationRegistry.emplace<AudioListener2DComponent>(destinationEntity, *sourceAudioListener);

                if (const auto* sourceAudioListener3D = sourceRegistry.try_get<AudioListener3DComponent>(sourceEntity))
                {
                    auto& destinationAudioListener3D = destinationRegistry.emplace<AudioListener3DComponent>(destinationEntity, *sourceAudioListener3D);
                    destinationAudioListener3D.RuntimeHasPreviousWorldPosition = false;
                    destinationAudioListener3D.RuntimePreviousWorldPosition = glm::vec3(0.0f);
                }

                if (const auto* sourceAudio = sourceRegistry.try_get<AudioSourceComponent>(sourceEntity))
                {
                    auto& destinationAudio = destinationRegistry.emplace<AudioSourceComponent>(destinationEntity);
                    destinationAudio.AudioClipKey = sourceAudio->AudioClipKey;
                    destinationAudio.Volume = sourceAudio->Volume;
                    destinationAudio.Pitch = sourceAudio->Pitch;
                    destinationAudio.PlayOnStart = sourceAudio->PlayOnStart;
                    destinationAudio.Loop = sourceAudio->Loop;
                    destinationAudio.Muted = sourceAudio->Muted;
                    destinationAudio.Space = sourceAudio->Space;
                    destinationAudio.MixerGroup = sourceAudio->MixerGroup;
                    destinationAudio.SpatialMinDistance = sourceAudio->SpatialMinDistance;
                    destinationAudio.SpatialMaxDistance = sourceAudio->SpatialMaxDistance;
                    destinationAudio.SpatialRolloffExponent = sourceAudio->SpatialRolloffExponent;
                    destinationAudio.StereoPanStrength = sourceAudio->StereoPanStrength;
                    destinationAudio.SpatialRolloffMode = sourceAudio->SpatialRolloffMode;
                    destinationAudio.DopplerFactor = sourceAudio->DopplerFactor;
                    destinationAudio.EnableDirectionalAttenuation = sourceAudio->EnableDirectionalAttenuation;
                    destinationAudio.DirectionalInnerAngleDegrees = sourceAudio->DirectionalInnerAngleDegrees;
                    destinationAudio.DirectionalOuterAngleDegrees = sourceAudio->DirectionalOuterAngleDegrees;
                    destinationAudio.DirectionalOuterVolume = sourceAudio->DirectionalOuterVolume;
                    destinationAudio.AttenuationCurveKey = sourceAudio->AttenuationCurveKey;
                    destinationAudio.RuntimeVoiceId = 0;
                    destinationAudio.RuntimePlaybackStarted = false;
                    destinationAudio.RuntimePlayOnStartConsumed = false;
                    destinationAudio.RuntimeHasPreviousWorldPosition = false;
                    destinationAudio.RuntimePreviousWorldPosition = glm::vec3(0.0f);
                }

                if (const auto* sourceRigidbody2D = sourceRegistry.try_get<Rigidbody2DComponent>(sourceEntity))
                    destinationRegistry.emplace<Rigidbody2DComponent>(destinationEntity, *sourceRigidbody2D);

                if (const auto* sourceBoxCollider2D = sourceRegistry.try_get<BoxCollider2DComponent>(sourceEntity))
                    destinationRegistry.emplace<BoxCollider2DComponent>(destinationEntity, *sourceBoxCollider2D);

                if (const auto* sourceCircleCollider2D = sourceRegistry.try_get<CircleCollider2DComponent>(sourceEntity))
                    destinationRegistry.emplace<CircleCollider2DComponent>(destinationEntity, *sourceCircleCollider2D);

                if (const auto* sourcePolygonCollider2D = sourceRegistry.try_get<PolygonCollider2DComponent>(sourceEntity))
                    destinationRegistry.emplace<PolygonCollider2DComponent>(destinationEntity, *sourcePolygonCollider2D);

                if (const auto* sourceEdgeCollider2D = sourceRegistry.try_get<EdgeCollider2DComponent>(sourceEntity))
                    destinationRegistry.emplace<EdgeCollider2DComponent>(destinationEntity, *sourceEdgeCollider2D);

                if (const auto* sourceCapsuleCollider2D = sourceRegistry.try_get<CapsuleCollider2DComponent>(sourceEntity))
                    destinationRegistry.emplace<CapsuleCollider2DComponent>(destinationEntity, *sourceCapsuleCollider2D);

                if (const auto* sourceJoint2D = sourceRegistry.try_get<Joint2DComponent>(sourceEntity))
                    destinationRegistry.emplace<Joint2DComponent>(destinationEntity, *sourceJoint2D);

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
                        destinationScriptEntry.ExecutionPolicy = sourceScriptEntry->ExecutionPolicy;
                        destinationScriptEntry.DeclaredReadAccessMask = sourceScriptEntry->DeclaredReadAccessMask;
                        destinationScriptEntry.DeclaredWriteAccessMask = sourceScriptEntry->DeclaredWriteAccessMask;
                        destinationScriptEntry.ExposedProperties = sourceScriptEntry->ExposedProperties;
                        destinationScriptEntry.RuntimeExposedPropertiesRevision = 1;
                        destinationScriptEntity = destinationScene.AttachScriptComponent(destinationEntity, std::move(destinationScriptEntry));
                    }
                    else if (const ManagedScriptEntry* sourceScriptEntry = sourceScriptComponent.TryGetManagedEntry())
                    {
                        ManagedScriptEntry destinationScriptEntry{};
                        destinationScriptEntry.ScriptClassName = sourceScriptEntry->ScriptClassName;
                        destinationScriptEntry.ScriptAssetRelativePath = sourceScriptEntry->ScriptAssetRelativePath;
                        destinationScriptEntry.Enabled = sourceScriptEntry->Enabled;
                        destinationScriptEntry.ExposedProperties = sourceScriptEntry->ExposedProperties;
                        destinationScriptEntry.RuntimeExposedPropertiesRevision = 1;
                        destinationScriptEntity = destinationScene.AttachManagedScriptComponent(destinationEntity, std::move(destinationScriptEntry));
                    }
                    if (destinationScriptEntity != entt::null)
                        scriptComponentMap.emplace(sourceScriptEntity, destinationScriptEntity);
                    if (auto* attachedScriptComponent = destinationRegistry.try_get<ScriptComponent>(destinationScriptEntity))
                        attachedScriptComponent->ComponentOrder = sourceScriptComponent.ComponentOrder;
                }

                if (const auto* sourceParticleEmitter = sourceRegistry.try_get<ParticleEmitterComponent>(sourceEntity))
                    destinationRegistry.emplace<ParticleEmitterComponent>(destinationEntity, *sourceParticleEmitter);

                if (const auto* sourcePrefabInstance = sourceRegistry.try_get<PrefabInstanceComponent>(sourceEntity))
                    destinationRegistry.emplace<PrefabInstanceComponent>(destinationEntity, *sourcePrefabInstance);

                ResetRuntimeStateForEntity(destinationRegistry, destinationEntity);
            }

            for (const auto& [sourceScriptEntity, destinationScriptEntity] : scriptComponentMap)
            {
                auto* destinationScriptComponent = destinationScene.GetScriptComponent(destinationScriptEntity);
                if (!destinationScriptComponent)
                    continue;

                if (NativeScriptEntry* destinationScriptEntry = destinationScriptComponent->TryGetNativeEntry())
                    RemapCopiedScriptEntityReferences(sourceScene, destinationScene, entityMap, destinationScriptEntry->ExposedProperties);
                else if (ManagedScriptEntry* destinationScriptEntry = destinationScriptComponent->TryGetManagedEntry())
                    RemapCopiedScriptEntityReferences(sourceScene, destinationScene, entityMap, destinationScriptEntry->ExposedProperties);
            }

            for (entt::entity sourceEntity : sourceEntities)
            {
                const auto mappedEntity = entityMap.find(sourceEntity);
                if (mappedEntity == entityMap.end())
                    continue;
                const entt::entity destinationEntity = mappedEntity->second;

                auto* destinationHierarchy = destinationRegistry.try_get<HierarchyComponent>(destinationEntity);
                if (!destinationHierarchy)
                    destinationHierarchy = &destinationRegistry.emplace<HierarchyComponent>(destinationEntity);

                const entt::entity sourceParent = sourceScene.GetParent(sourceEntity);
                if (sourceParent != entt::null)
                {
                    const auto mappedParent = entityMap.find(sourceParent);
                    destinationHierarchy->Parent = (mappedParent != entityMap.end()) ? mappedParent->second : resolvedDestinationParentEntity;
                }
                else
                {
                    destinationHierarchy->Parent = resolvedDestinationParentEntity;
                }

                if (const auto* sourceHierarchy = sourceRegistry.try_get<HierarchyComponent>(sourceEntity))
                    destinationHierarchy->SiblingOrder = sourceHierarchy->SiblingOrder;
                else
                    destinationHierarchy->SiblingOrder = 0;

                // Preserve prefab-authored root world transform even when instantiated under
                // a non-null parent. Without this, direct parent assignment can skew scale/size
                // and offset position due to inherited parent transforms.
                if (sourceEntity == sourceRootEntity && resolvedDestinationParentEntity != entt::null)
                {
                    if (auto* destinationTransform = destinationRegistry.try_get<TransformComponent>(destinationEntity))
                    {
                        const glm::mat4 sourceRootWorld = sourceScene.GetWorldTransformMatrix(sourceEntity);
                        const glm::mat4 parentWorld = destinationScene.GetWorldTransformMatrix(resolvedDestinationParentEntity);
                        if (TryAssignLocalTransformFromWorld(parentWorld, sourceRootWorld, *destinationTransform))
                        {
                            destinationScene.MarkTransformDirty(destinationEntity);
                        }
                    }
                }
            }

            for (entt::entity sourceEntity : sourceEntities)
            {
                const auto* sourceJoint = sourceRegistry.try_get<Joint2DComponent>(sourceEntity);
                if (!sourceJoint)
                    continue;

                const auto mappedEntity = entityMap.find(sourceEntity);
                if (mappedEntity == entityMap.end())
                    continue;

                auto* destinationJoint = destinationRegistry.try_get<Joint2DComponent>(mappedEntity->second);
                if (!destinationJoint)
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

            const auto mappedRoot = entityMap.find(sourceRootEntity);
            if (mappedRoot == entityMap.end())
                return false;

            destinationScene.MarkTransformDirty(mappedRoot->second);

            if (outDestinationRootEntity)
                *outDestinationRootEntity = mappedRoot->second;
            return true;
        }
    }

    entt::entity Scene::InstantiatePrefab(const std::string& prefabAssetKey, entt::entity parentEntity)
    {
        if (m_IsShuttingDown)
            return entt::null;

        if (prefabAssetKey.empty())
            return entt::null;

        if (ShouldDeferStructuralMutations())
        {
            const std::string deferredPrefabAssetKey = prefabAssetKey;
            const entt::entity deferredEntity = AllocateDeferredEntityReference();
            const bool enqueued = EnqueueDeferredStructuralMutation([deferredPrefabAssetKey, parentEntity, deferredEntity](Scene& scene) {
                const entt::entity createdEntity = scene.InstantiatePrefab(deferredPrefabAssetKey, parentEntity);
                scene.BindDeferredEntityReference(deferredEntity, createdEntity);
            }, "InstantiatePrefab");
            if (!enqueued)
            {
                ForgetDeferredEntityReference(deferredEntity);
                return entt::null;
            }
            return deferredEntity;
        }

        parentEntity = ResolveEntityReference(parentEntity);
        if (parentEntity != entt::null && !IsValid(parentEntity))
            parentEntity = entt::null;

        auto loadedPrefabSceneResult = Scene::LoadFromFile(prefabAssetKey);
        if (const auto resolvedPath = Assets::ResolveAssetKeyToPath(prefabAssetKey); resolvedPath.IsSuccess())
        {
            auto resolvedLoadResult = Scene::LoadFromFile(resolvedPath.GetValue());
            if (!resolvedLoadResult.IsFailure())
                loadedPrefabSceneResult = std::move(resolvedLoadResult);
        }

        if (loadedPrefabSceneResult.IsFailure())
        {
            LT_WARN("Scene::InstantiatePrefab failed for '{}': {}",
                    prefabAssetKey,
                    loadedPrefabSceneResult.GetError().GetErrorMessage());
            return entt::null;
        }

        auto& loadedPrefabScene = *loadedPrefabSceneResult.GetValue();
        const auto prefabRoots = loadedPrefabScene.GetChildren(entt::null);
        if (prefabRoots.empty())
            return entt::null;

        entt::entity createdRoot = entt::null;
        if (!CopyEntitySubtreeToScene(loadedPrefabScene, *this, prefabRoots.front(), parentEntity, &createdRoot))
            return entt::null;

        if (createdRoot == entt::null || !IsValid(createdRoot))
            return entt::null;

        auto& prefabInstance = m_Registry.emplace_or_replace<PrefabInstanceComponent>(createdRoot);
        prefabInstance.PrefabAssetKey = prefabAssetKey;
        return createdRoot;
    }
}
