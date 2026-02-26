#include "Scene/Scene.h"

#include "Assets/AssetPaths.h"
#include "Scripting/Coroutine.h"

#include <unordered_map>
#include <vector>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

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
            }
            animator.RuntimeHasPosition = false;
            animator.RuntimeHasScale = false;
            animator.RuntimeHasRotationZ = false;
            animator.RuntimePosition = glm::vec3(0.0f);
            animator.RuntimeScale = glm::vec3(1.0f);
            animator.RuntimeRotationZDegrees = 0.0f;
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
            }

            if (auto* uiSlider = registry.try_get<UISliderComponent>(entity))
            {
                uiSlider->RuntimeDragging = false;
                uiSlider->RuntimeValueChangedThisFrame = false;
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

            if (auto* audioSource = registry.try_get<AudioSourceComponent>(entity))
            {
                audioSource->RuntimeVoiceId = 0;
                audioSource->RuntimePlaybackStarted = false;
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

            if (auto* nativeScript = registry.try_get<NativeScriptComponent>(entity))
            {
                for (auto& scriptEntry : nativeScript->Scripts)
                {
                    scriptEntry.RuntimeInitialized = false;
                    if (scriptEntry.RuntimeInstance)
                        Coroutine::StopAll(*scriptEntry.RuntimeInstance);
                    scriptEntry.RuntimeInstance.reset();
                    scriptEntry.RuntimeUpdateCount = 0;
                    scriptEntry.RuntimeWarnedOnUpdateTransformMutation = false;
                    scriptEntry.RuntimeWarnedMissingAccessDeclaration = false;
                }
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
                    destinationAudio.AttenuationCurveKey = sourceAudio->AttenuationCurveKey;
                    destinationAudio.RuntimeVoiceId = 0;
                    destinationAudio.RuntimePlaybackStarted = false;
                }

                if (const auto* sourceRigidbody2D = sourceRegistry.try_get<Rigidbody2DComponent>(sourceEntity))
                    destinationRegistry.emplace<Rigidbody2DComponent>(destinationEntity, *sourceRigidbody2D);

                if (const auto* sourceBoxCollider2D = sourceRegistry.try_get<BoxCollider2DComponent>(sourceEntity))
                    destinationRegistry.emplace<BoxCollider2DComponent>(destinationEntity, *sourceBoxCollider2D);

                if (const auto* sourceCircleCollider2D = sourceRegistry.try_get<CircleCollider2DComponent>(sourceEntity))
                    destinationRegistry.emplace<CircleCollider2DComponent>(destinationEntity, *sourceCircleCollider2D);

                if (const auto* sourceJoint2D = sourceRegistry.try_get<Joint2DComponent>(sourceEntity))
                    destinationRegistry.emplace<Joint2DComponent>(destinationEntity, *sourceJoint2D);

                if (const auto* sourceScripts = sourceRegistry.try_get<NativeScriptComponent>(sourceEntity))
                {
                    auto& destinationScripts = destinationRegistry.emplace<NativeScriptComponent>(destinationEntity);
                    destinationScripts.Scripts.reserve(sourceScripts->Scripts.size());
                    for (const auto& sourceScriptEntry : sourceScripts->Scripts)
                    {
                        auto& destinationScriptEntry = destinationScripts.Scripts.emplace_back();
                        destinationScriptEntry.ScriptClassName = sourceScriptEntry.ScriptClassName;
                        destinationScriptEntry.ScriptAssetRelativePath = sourceScriptEntry.ScriptAssetRelativePath;
                        destinationScriptEntry.Enabled = sourceScriptEntry.Enabled;
                        destinationScriptEntry.ExecutionPolicy = sourceScriptEntry.ExecutionPolicy;
                        destinationScriptEntry.DeclaredReadAccessMask = sourceScriptEntry.DeclaredReadAccessMask;
                        destinationScriptEntry.DeclaredWriteAccessMask = sourceScriptEntry.DeclaredWriteAccessMask;
                        destinationScriptEntry.ExposedProperties = sourceScriptEntry.ExposedProperties;
                        destinationScriptEntry.RuntimeInitialized = false;
                        destinationScriptEntry.RuntimeInstance.reset();
                        destinationScriptEntry.RuntimeWarnedMissingAccessDeclaration = false;
                    }
                }

                if (const auto* sourceParticleEmitter = sourceRegistry.try_get<ParticleEmitterComponent>(sourceEntity))
                    destinationRegistry.emplace<ParticleEmitterComponent>(destinationEntity, *sourceParticleEmitter);

                if (const auto* sourcePrefabInstance = sourceRegistry.try_get<PrefabInstanceComponent>(sourceEntity))
                    destinationRegistry.emplace<PrefabInstanceComponent>(destinationEntity, *sourcePrefabInstance);

                ResetRuntimeStateForEntity(destinationRegistry, destinationEntity);
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
                        const glm::mat4 childLocal = glm::inverse(parentWorld) * sourceRootWorld;

                        glm::vec3 skew(0.0f);
                        glm::vec4 perspective(0.0f);
                        glm::quat orientation(1.0f, 0.0f, 0.0f, 0.0f);
                        glm::vec3 translation(0.0f);
                        glm::vec3 scale(1.0f);
                        if (glm::decompose(childLocal, scale, orientation, translation, skew, perspective))
                        {
                            destinationTransform->Position = translation;
                            destinationTransform->Rotation = glm::degrees(glm::eulerAngles(orientation));
                            destinationTransform->Scale = scale;
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
        if (prefabAssetKey.empty())
            return entt::null;

        if (ShouldDeferStructuralMutations())
        {
            const std::string deferredPrefabAssetKey = prefabAssetKey;
            EnqueueDeferredStructuralMutation([deferredPrefabAssetKey, parentEntity](Scene& scene) {
                (void)scene.InstantiatePrefab(deferredPrefabAssetKey, parentEntity);
            }, "InstantiatePrefab");
            return entt::null;
        }

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
