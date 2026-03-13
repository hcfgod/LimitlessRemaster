#include "Scene/SceneScriptRuntimeInternal.h"

#include "Core/Application.h"
#include "Core/ConfigManager.h"
#include "Physics/Physics2DQueries.h"
#include "Platform/Window.h"
#include "Scene/ParticleEmitterSystem.h"
#include "Scene/SceneSystemScheduler.h"
#include "Scripting/Coroutine.h"
#include "Scripting/ManagedScriptHost.h"
#include "Scripting/NativeScriptRegistry.h"

#include <algorithm>
#include <exception>
#include <string_view>
#include <utility>
#include <vector>

namespace Limitless
{
    using namespace SceneScriptRuntimeInternal;
    void Scene::FixedUpdate(float fixedDeltaTime)
    {
        SetRuntimePhase(RuntimePhase::Structural);
        FlushDeferredStructuralMutations();

        Physics2DQueries::SetActiveSceneForScriptQueries(this);
        if (NativeScriptRegistry::IsExecutionBlocked())
        {
            SetRuntimePhase(RuntimePhase::Idle);
            return;
        }

        std::vector<std::pair<entt::entity, size_t>> scriptSlots;
        std::vector<std::pair<entt::entity, size_t>> managedScriptSlots;
        for (entt::entity scriptEntity : CollectOrderedScriptComponentEntities(m_Registry))
        {
            const entt::entity ownerEntity = TryGetScriptOwnerEntity(m_Registry, scriptEntity);
            if (ownerEntity == entt::null)
                continue;
            const size_t scriptIndex = static_cast<size_t>(static_cast<uint32_t>(scriptEntity));
            const auto* scriptComponent = m_Registry.try_get<ScriptComponent>(scriptEntity);
            if (scriptComponent && scriptComponent->IsManagedBackend())
                managedScriptSlots.emplace_back(ownerEntity, scriptIndex);
            else
                scriptSlots.emplace_back(ownerEntity, scriptIndex);
        }

        auto tryGetScriptEntry = [&](entt::entity scriptEntity, size_t scriptIndex) -> NativeScriptEntry* {
            (void)scriptEntity;
            return TryGetScriptEntry(m_Registry, static_cast<entt::entity>(static_cast<uint32_t>(scriptIndex)));
        };

        auto tryGetManagedEntry = [&](entt::entity scriptEntity, size_t scriptIndex) -> ManagedScriptEntry* {
            (void)scriptEntity;
            return TryGetManagedScriptEntry(m_Registry, static_cast<entt::entity>(static_cast<uint32_t>(scriptIndex)));
        };

        auto applyScriptDeclaredAccessDefaults = [&](NativeScriptEntry& scriptEntry) {
            if (!scriptEntry.RuntimeInstance)
                return;

            const uint64_t defaultReadMask = scriptEntry.RuntimeInstance->GetDeclaredReadAccessMask();
            const uint64_t defaultWriteMask = scriptEntry.RuntimeInstance->GetDeclaredWriteAccessMask();
            if (defaultReadMask != 0 || defaultWriteMask != 0)
            {
                // Merge script-authored defaults with serialized entry masks so
                // existing scene data cannot keep stale/missing access bits.
                scriptEntry.DeclaredReadAccessMask |= defaultReadMask;
                scriptEntry.DeclaredWriteAccessMask |= defaultWriteMask;
            }
        };

        auto handleScriptCallbackFailure = [&](entt::entity scriptEntity,
                                               size_t scriptIndex,
                                               std::string_view callbackName,
                                               const char* message) {
            NativeScriptEntry* scriptEntry = tryGetScriptEntry(scriptEntity, scriptIndex);
            const auto* tag = m_Registry.try_get<TagComponent>(scriptEntity);
            LT_ERROR("Script '{}' on entity '{}' failed during {}: {}",
                     scriptEntry ? scriptEntry->ScriptClassName : "<unknown>",
                     tag ? tag->Tag : "Entity",
                     callbackName,
                     message ? message : "unknown error");

            if (!scriptEntry)
                return;

            if (scriptEntry->RuntimeInstance)
            {
                try
                {
                    Coroutine::StopAll(*scriptEntry->RuntimeInstance);
                }
                catch (const std::exception& exception)
                {
                    LT_WARN("Script '{}' on entity '{}' threw during coroutine cleanup after {} failure: {}",
                            scriptEntry->ScriptClassName,
                            tag ? tag->Tag : "Entity",
                            callbackName,
                            exception.what());
                }
                catch (...)
                {
                    LT_WARN("Script '{}' on entity '{}' threw a non-standard exception during coroutine cleanup after {} failure",
                            scriptEntry->ScriptClassName,
                            tag ? tag->Tag : "Entity",
                            callbackName);
                }
            }
            scriptEntry->RuntimeInstance.reset();
            scriptEntry->RuntimeInitialized = false;
            scriptEntry->RuntimeUpdateCount = 0;
            scriptEntry->RuntimeWarnedOnUpdateTransformMutation = false;
            scriptEntry->RuntimeWarnedAccessMaskMismatch = false;
        };

        auto destroyManagedScriptRuntime = [&](ManagedScriptEntry& scriptEntry) {
            if (scriptEntry.RuntimeInstanceId != 0)
                ManagedScriptHost::DestroyScriptInstance(scriptEntry.RuntimeInstanceId);
            scriptEntry.RuntimeInstanceId = 0;
            scriptEntry.RuntimeInitialized = false;
            scriptEntry.RuntimeUpdateCount = 0;
            scriptEntry.RuntimeWarnedOnUpdateTransformMutation = false;
        };

        auto handleManagedScriptCallbackFailure = [&](entt::entity scriptEntity,
                                                      size_t scriptIndex,
                                                      std::string_view callbackName,
                                                      const char* message) {
            ManagedScriptEntry* scriptEntry = tryGetManagedEntry(scriptEntity, scriptIndex);
            const auto* tag = m_Registry.try_get<TagComponent>(scriptEntity);
            LT_ERROR("Managed script '{}' on entity '{}' failed during {}: {}",
                     scriptEntry ? scriptEntry->ScriptClassName : "<unknown>",
                     tag ? tag->Tag : "Entity",
                     callbackName,
                     message ? message : "unknown error");

            if (!scriptEntry)
                return;

            destroyManagedScriptRuntime(*scriptEntry);
            scriptEntry->RuntimeWarnedMissingHost = false;
            scriptEntry->RuntimeWarnedMissingClass = false;
        };

        auto readBackManagedScriptExposedProperties = [&](entt::entity scriptEntity,
                                                          size_t scriptIndex,
                                                          std::string_view callbackName) {
            ManagedScriptEntry* scriptEntry = tryGetManagedEntry(scriptEntity, scriptIndex);
            if (!scriptEntry || scriptEntry->RuntimeInstanceId == 0)
                return false;

            std::string managedError;
            if (!ManagedScriptHost::ReadBackScriptExposedProperties(scriptEntry->RuntimeInstanceId,
                                                                    this,
                                                                    scriptEntry->ExposedProperties,
                                                                    &scriptEntry->RuntimeExposedPropertiesRevision,
                                                                    &managedError))
            {
                std::string readBackLabel = std::string("ReadBackExposedProperties after ") + std::string(callbackName);
                handleManagedScriptCallbackFailure(scriptEntity,
                                                  scriptIndex,
                                                  readBackLabel,
                                                  managedError.empty() ? "unknown error" : managedError.c_str());
                return false;
            }

            return true;
        };

        const bool validateParallelScriptAccessMasks =
            ConfigManager::GetInstance().GetValue<bool>("ecs.mt.validate_parallel_script_access_masks", true);
        const bool warnOnParallelScriptAccessMaskMismatch =
            ConfigManager::GetInstance().GetValue<bool>("ecs.mt.warn_parallel_script_access_mismatch", true);
        auto validateParallelScriptAccessMask = [&](entt::entity entity,
                                                    NativeScriptEntry& scriptEntry,
                                                    uint64_t observedWriteMask,
                                                    std::string_view callbackName) {
            if (!validateParallelScriptAccessMasks || observedWriteMask == 0)
                return;
            if (!Scene::IsCurrentThreadParallelScriptExecution())
                return;
            if (scriptEntry.ExecutionPolicy != ScriptExecutionPolicy::ParallelSafe)
                return;

            const uint64_t missingWriteMask = observedWriteMask & ~scriptEntry.DeclaredWriteAccessMask;
            if (missingWriteMask == 0)
                return;

            const uint64_t mismatchCount = s_ParallelScriptAccessMaskMismatchCount.fetch_add(1, std::memory_order_relaxed) + 1;
            if (warnOnParallelScriptAccessMaskMismatch && !scriptEntry.RuntimeWarnedAccessMaskMismatch)
            {
                const auto* tag = m_Registry.try_get<TagComponent>(entity);
                LT_WARN("ParallelSafe script '{}' on entity '{}' mutated [{}] during {} without declaring write access (declared={} observed={} missing={}). Add required bits to DeclaredWriteAccessMask. total_mismatches={}",
                        scriptEntry.ScriptClassName,
                        tag ? tag->Tag : "Entity",
                        DescribeAccessMask(observedWriteMask),
                        callbackName,
                        DescribeAccessMask(scriptEntry.DeclaredWriteAccessMask),
                        DescribeAccessMask(observedWriteMask),
                        DescribeAccessMask(missingWriteMask),
                        mismatchCount);
                scriptEntry.RuntimeWarnedAccessMaskMismatch = true;
            }
        };

        auto executeWithDeferredEntityDestroy = [this](auto&& callback) {
            [[maybe_unused]] auto forcedDeferredDestroyScope = MakeForcedDeferredEntityDestructionScope();
            callback();
        };

        auto executeScriptFixedUpdateSlot = [&](entt::entity entity, size_t scriptIndex) {
            NativeScriptEntry* scriptEntry = tryGetScriptEntry(entity, scriptIndex);
            if (!scriptEntry || !scriptEntry->RuntimeInstance)
                return;

            TransformComponent transformBeforeFixedUpdate{};
            bool hadTransformBeforeFixedUpdate = false;
            HierarchyComponent hierarchyBeforeFixedUpdate{};
            bool hadHierarchyBeforeFixedUpdate = false;
            Rigidbody2DComponent rigidbodyBeforeFixedUpdate{};
            bool hadRigidbodyBeforeFixedUpdate = false;
            BoxCollider2DComponent boxColliderBeforeFixedUpdate{};
            bool hadBoxColliderBeforeFixedUpdate = false;
            CircleCollider2DComponent circleColliderBeforeFixedUpdate{};
            bool hadCircleColliderBeforeFixedUpdate = false;
            PolygonCollider2DComponent polygonColliderBeforeFixedUpdate{};
            bool hadPolygonColliderBeforeFixedUpdate = false;
            EdgeCollider2DComponent edgeColliderBeforeFixedUpdate{};
            bool hadEdgeColliderBeforeFixedUpdate = false;
            CapsuleCollider2DComponent capsuleColliderBeforeFixedUpdate{};
            bool hadCapsuleColliderBeforeFixedUpdate = false;
            Joint2DComponent joint2DBeforeFixedUpdate{};
            bool hadJoint2DBeforeFixedUpdate = false;
            TagComponent tagBeforeFixedUpdate{};
            bool hadTagBeforeFixedUpdate = false;
            SpriteComponent spriteBeforeFixedUpdate{};
            bool hadSpriteBeforeFixedUpdate = false;
            MaterialComponent materialBeforeFixedUpdate{};
            bool hadMaterialBeforeFixedUpdate = false;
            CanvasComponent canvasBeforeFixedUpdate{};
            bool hadCanvasBeforeFixedUpdate = false;
            RectTransformComponent rectTransformBeforeFixedUpdate{};
            bool hadRectTransformBeforeFixedUpdate = false;
            UIImageComponent uiImageBeforeFixedUpdate{};
            bool hadUIImageBeforeFixedUpdate = false;
            UIPanelComponent uiPanelBeforeFixedUpdate{};
            bool hadUIPanelBeforeFixedUpdate = false;
            UITextComponent uiTextBeforeFixedUpdate{};
            bool hadUITextBeforeFixedUpdate = false;
            UIButtonComponent uiButtonBeforeFixedUpdate{};
            bool hadUIButtonBeforeFixedUpdate = false;
            UISliderComponent uiSliderBeforeFixedUpdate{};
            bool hadUISliderBeforeFixedUpdate = false;
            DirectionalLight2DComponent directionalLightBeforeFixedUpdate{};
            bool hadDirectionalLightBeforeFixedUpdate = false;
            PointLight2DComponent pointLightBeforeFixedUpdate{};
            bool hadPointLightBeforeFixedUpdate = false;
            ShadowOccluder2DComponent shadowOccluderBeforeFixedUpdate{};
            bool hadShadowOccluderBeforeFixedUpdate = false;
            AudioListener2DComponent audioListenerBeforeFixedUpdate{};
            bool hadAudioListenerBeforeFixedUpdate = false;
            AudioListener3DComponent audioListener3DBeforeFixedUpdate{};
            bool hadAudioListener3DBeforeFixedUpdate = false;
            AudioSourceComponent audioSourceBeforeFixedUpdate{};
            bool hadAudioSourceBeforeFixedUpdate = false;
            CameraComponent cameraBeforeFixedUpdate{};
            bool hadCameraBeforeFixedUpdate = false;
            PrefabInstanceComponent prefabInstanceBeforeFixedUpdate{};
            bool hadPrefabInstanceBeforeFixedUpdate = false;
            Grid2DComponent grid2DBeforeFixedUpdate{};
            bool hadGrid2DBeforeFixedUpdate = false;
            TilemapLayerValidationSnapshot tilemapLayerSnapshotBeforeFixedUpdate{};
            bool hadTilemapLayerBeforeFixedUpdate = false;
            AnimatorComponent animatorBeforeFixedUpdate{};
            bool hadAnimatorBeforeFixedUpdate = false;
            AnimationEventReceiverComponent animationEventReceiverBeforeFixedUpdate{};
            bool hadAnimationEventReceiverBeforeFixedUpdate = false;
            ParticleEmitterComponent particleEmitterBeforeFixedUpdate{};
            bool hadParticleEmitterBeforeFixedUpdate = false;
            if (auto* transform = m_Registry.try_get<TransformComponent>(entity))
            {
                transformBeforeFixedUpdate = *transform;
                hadTransformBeforeFixedUpdate = true;
            }
            const bool trackParallelAccessValidation =
                validateParallelScriptAccessMasks &&
                scriptEntry->ExecutionPolicy == ScriptExecutionPolicy::ParallelSafe;
            if (trackParallelAccessValidation)
            {
                if (auto* hierarchy = m_Registry.try_get<HierarchyComponent>(entity))
                {
                    hierarchyBeforeFixedUpdate = *hierarchy;
                    hadHierarchyBeforeFixedUpdate = true;
                }
                if (auto* rigidbody2D = m_Registry.try_get<Rigidbody2DComponent>(entity))
                {
                    rigidbodyBeforeFixedUpdate = *rigidbody2D;
                    hadRigidbodyBeforeFixedUpdate = true;
                }
                if (auto* boxCollider2D = m_Registry.try_get<BoxCollider2DComponent>(entity))
                {
                    boxColliderBeforeFixedUpdate = *boxCollider2D;
                    hadBoxColliderBeforeFixedUpdate = true;
                }
                if (auto* circleCollider2D = m_Registry.try_get<CircleCollider2DComponent>(entity))
                {
                    circleColliderBeforeFixedUpdate = *circleCollider2D;
                    hadCircleColliderBeforeFixedUpdate = true;
                }
                if (auto* polygonCollider2D = m_Registry.try_get<PolygonCollider2DComponent>(entity))
                {
                    polygonColliderBeforeFixedUpdate = *polygonCollider2D;
                    hadPolygonColliderBeforeFixedUpdate = true;
                }
                if (auto* edgeCollider2D = m_Registry.try_get<EdgeCollider2DComponent>(entity))
                {
                    edgeColliderBeforeFixedUpdate = *edgeCollider2D;
                    hadEdgeColliderBeforeFixedUpdate = true;
                }
                if (auto* capsuleCollider2D = m_Registry.try_get<CapsuleCollider2DComponent>(entity))
                {
                    capsuleColliderBeforeFixedUpdate = *capsuleCollider2D;
                    hadCapsuleColliderBeforeFixedUpdate = true;
                }
                if (auto* joint2D = m_Registry.try_get<Joint2DComponent>(entity))
                {
                    joint2DBeforeFixedUpdate = *joint2D;
                    hadJoint2DBeforeFixedUpdate = true;
                }
                if (auto* tag = m_Registry.try_get<TagComponent>(entity))
                {
                    tagBeforeFixedUpdate = *tag;
                    hadTagBeforeFixedUpdate = true;
                }
                if (auto* sprite = m_Registry.try_get<SpriteComponent>(entity))
                {
                    spriteBeforeFixedUpdate = *sprite;
                    hadSpriteBeforeFixedUpdate = true;
                }
                if (auto* material = m_Registry.try_get<MaterialComponent>(entity))
                {
                    materialBeforeFixedUpdate = *material;
                    hadMaterialBeforeFixedUpdate = true;
                }
                if (auto* canvas = m_Registry.try_get<CanvasComponent>(entity))
                {
                    canvasBeforeFixedUpdate = *canvas;
                    hadCanvasBeforeFixedUpdate = true;
                }
                if (auto* rectTransform = m_Registry.try_get<RectTransformComponent>(entity))
                {
                    rectTransformBeforeFixedUpdate = *rectTransform;
                    hadRectTransformBeforeFixedUpdate = true;
                }
                if (auto* uiImage = m_Registry.try_get<UIImageComponent>(entity))
                {
                    uiImageBeforeFixedUpdate = *uiImage;
                    hadUIImageBeforeFixedUpdate = true;
                }
                if (auto* uiPanel = m_Registry.try_get<UIPanelComponent>(entity))
                {
                    uiPanelBeforeFixedUpdate = *uiPanel;
                    hadUIPanelBeforeFixedUpdate = true;
                }
                if (auto* uiText = m_Registry.try_get<UITextComponent>(entity))
                {
                    uiTextBeforeFixedUpdate = *uiText;
                    hadUITextBeforeFixedUpdate = true;
                }
                if (auto* uiButton = m_Registry.try_get<UIButtonComponent>(entity))
                {
                    uiButtonBeforeFixedUpdate = *uiButton;
                    hadUIButtonBeforeFixedUpdate = true;
                }
                if (auto* uiSlider = m_Registry.try_get<UISliderComponent>(entity))
                {
                    uiSliderBeforeFixedUpdate = *uiSlider;
                    hadUISliderBeforeFixedUpdate = true;
                }
                if (auto* directionalLight = m_Registry.try_get<DirectionalLight2DComponent>(entity))
                {
                    directionalLightBeforeFixedUpdate = *directionalLight;
                    hadDirectionalLightBeforeFixedUpdate = true;
                }
                if (auto* pointLight = m_Registry.try_get<PointLight2DComponent>(entity))
                {
                    pointLightBeforeFixedUpdate = *pointLight;
                    hadPointLightBeforeFixedUpdate = true;
                }
                if (auto* shadowOccluder = m_Registry.try_get<ShadowOccluder2DComponent>(entity))
                {
                    shadowOccluderBeforeFixedUpdate = *shadowOccluder;
                    hadShadowOccluderBeforeFixedUpdate = true;
                }
                if (auto* audioListener = m_Registry.try_get<AudioListener2DComponent>(entity))
                {
                    audioListenerBeforeFixedUpdate = *audioListener;
                    hadAudioListenerBeforeFixedUpdate = true;
                }
                if (auto* audioListener3D = m_Registry.try_get<AudioListener3DComponent>(entity))
                {
                    audioListener3DBeforeFixedUpdate = *audioListener3D;
                    hadAudioListener3DBeforeFixedUpdate = true;
                }
                if (auto* audioSource = m_Registry.try_get<AudioSourceComponent>(entity))
                {
                    audioSourceBeforeFixedUpdate = *audioSource;
                    hadAudioSourceBeforeFixedUpdate = true;
                }
                if (auto* camera = m_Registry.try_get<CameraComponent>(entity))
                {
                    cameraBeforeFixedUpdate = *camera;
                    hadCameraBeforeFixedUpdate = true;
                }
                if (auto* prefabInstance = m_Registry.try_get<PrefabInstanceComponent>(entity))
                {
                    prefabInstanceBeforeFixedUpdate = *prefabInstance;
                    hadPrefabInstanceBeforeFixedUpdate = true;
                }
                if (auto* grid2D = m_Registry.try_get<Grid2DComponent>(entity))
                {
                    grid2DBeforeFixedUpdate = *grid2D;
                    hadGrid2DBeforeFixedUpdate = true;
                }
                if (auto* tilemapLayer = m_Registry.try_get<TilemapLayerComponent>(entity))
                {
                    tilemapLayerSnapshotBeforeFixedUpdate = SnapshotTilemapLayerForValidation(*tilemapLayer);
                    hadTilemapLayerBeforeFixedUpdate = true;
                }
                if (auto* animator = m_Registry.try_get<AnimatorComponent>(entity))
                {
                    animatorBeforeFixedUpdate = *animator;
                    hadAnimatorBeforeFixedUpdate = true;
                }
                if (auto* animationEventReceiver = m_Registry.try_get<AnimationEventReceiverComponent>(entity))
                {
                    animationEventReceiverBeforeFixedUpdate = *animationEventReceiver;
                    hadAnimationEventReceiverBeforeFixedUpdate = true;
                }
                if (auto* particleEmitter = m_Registry.try_get<ParticleEmitterComponent>(entity))
                {
                    particleEmitterBeforeFixedUpdate = *particleEmitter;
                    hadParticleEmitterBeforeFixedUpdate = true;
                }
            }

            try
            {
                executeWithDeferredEntityDestroy([&]() {
                    scriptEntry->RuntimeInstance->OnSynchronizeExposedFields();
                    scriptEntry->RuntimeInstance->MarkExposedPropertySyncComplete();
                    scriptEntry->RuntimeInstance->OnFixedUpdate(fixedDeltaTime);
                    scriptEntry->RuntimeInstance->OnWriteBackExposedFields();
                    scriptEntry->RuntimeInstance->MarkExposedPropertySyncComplete();
                });
            }
            catch (const std::exception& exception)
            {
                handleScriptCallbackFailure(entity, scriptIndex, "OnFixedUpdate", exception.what());
                return;
            }
            catch (...)
            {
                handleScriptCallbackFailure(entity, scriptIndex, "OnFixedUpdate", "non-standard exception");
                return;
            }

            const auto* transformAfterFixedUpdate = m_Registry.try_get<TransformComponent>(entity);
            bool transformChanged = false;
            if (hadTransformBeforeFixedUpdate && transformAfterFixedUpdate)
            {
                transformChanged = HasTransformChangedForAccessValidation(transformBeforeFixedUpdate, *transformAfterFixedUpdate);
                if (transformChanged)
                    MarkTransformDirty(entity);
            }
            else if (!hadTransformBeforeFixedUpdate && transformAfterFixedUpdate)
            {
                transformChanged = true;
                MarkTransformDirty(entity);
            }

            uint64_t observedWriteMask = 0;
            if (transformChanged)
                observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Transform);
            if (trackParallelAccessValidation)
            {
                const auto* hierarchyAfterFixedUpdate = m_Registry.try_get<HierarchyComponent>(entity);
                const bool hasHierarchyAfterFixedUpdate = hierarchyAfterFixedUpdate != nullptr;
                if (hadHierarchyBeforeFixedUpdate != hasHierarchyAfterFixedUpdate ||
                    (hadHierarchyBeforeFixedUpdate && hierarchyAfterFixedUpdate &&
                     HasHierarchyChangedForAccessValidation(hierarchyBeforeFixedUpdate, *hierarchyAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Hierarchy);
                }

                const auto* rigidbodyAfterFixedUpdate = m_Registry.try_get<Rigidbody2DComponent>(entity);
                const bool hasRigidbodyAfterFixedUpdate = rigidbodyAfterFixedUpdate != nullptr;
                if (hadRigidbodyBeforeFixedUpdate != hasRigidbodyAfterFixedUpdate ||
                    (hadRigidbodyBeforeFixedUpdate && rigidbodyAfterFixedUpdate &&
                     HasRigidbodyChangedForAccessValidation(rigidbodyBeforeFixedUpdate, *rigidbodyAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Rigidbody2D);
                }

                const auto* boxColliderAfterFixedUpdate = m_Registry.try_get<BoxCollider2DComponent>(entity);
                const bool hasBoxColliderAfterFixedUpdate = boxColliderAfterFixedUpdate != nullptr;
                if (hadBoxColliderBeforeFixedUpdate != hasBoxColliderAfterFixedUpdate ||
                    (hadBoxColliderBeforeFixedUpdate && boxColliderAfterFixedUpdate &&
                     HasBoxColliderChangedForAccessValidation(boxColliderBeforeFixedUpdate, *boxColliderAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::BoxCollider2D);
                }

                const auto* circleColliderAfterFixedUpdate = m_Registry.try_get<CircleCollider2DComponent>(entity);
                const bool hasCircleColliderAfterFixedUpdate = circleColliderAfterFixedUpdate != nullptr;
                if (hadCircleColliderBeforeFixedUpdate != hasCircleColliderAfterFixedUpdate ||
                    (hadCircleColliderBeforeFixedUpdate && circleColliderAfterFixedUpdate &&
                     HasCircleColliderChangedForAccessValidation(circleColliderBeforeFixedUpdate, *circleColliderAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::CircleCollider2D);
                }

                const auto* polygonColliderAfterFixedUpdate = m_Registry.try_get<PolygonCollider2DComponent>(entity);
                const bool hasPolygonColliderAfterFixedUpdate = polygonColliderAfterFixedUpdate != nullptr;
                if (hadPolygonColliderBeforeFixedUpdate != hasPolygonColliderAfterFixedUpdate ||
                    (hadPolygonColliderBeforeFixedUpdate && polygonColliderAfterFixedUpdate &&
                     HasPolygonColliderChangedForAccessValidation(polygonColliderBeforeFixedUpdate, *polygonColliderAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::PolygonCollider2D);
                }

                const auto* edgeColliderAfterFixedUpdate = m_Registry.try_get<EdgeCollider2DComponent>(entity);
                const bool hasEdgeColliderAfterFixedUpdate = edgeColliderAfterFixedUpdate != nullptr;
                if (hadEdgeColliderBeforeFixedUpdate != hasEdgeColliderAfterFixedUpdate ||
                    (hadEdgeColliderBeforeFixedUpdate && edgeColliderAfterFixedUpdate &&
                     HasEdgeColliderChangedForAccessValidation(edgeColliderBeforeFixedUpdate, *edgeColliderAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::EdgeCollider2D);
                }

                const auto* capsuleColliderAfterFixedUpdate = m_Registry.try_get<CapsuleCollider2DComponent>(entity);
                const bool hasCapsuleColliderAfterFixedUpdate = capsuleColliderAfterFixedUpdate != nullptr;
                if (hadCapsuleColliderBeforeFixedUpdate != hasCapsuleColliderAfterFixedUpdate ||
                    (hadCapsuleColliderBeforeFixedUpdate && capsuleColliderAfterFixedUpdate &&
                     HasCapsuleColliderChangedForAccessValidation(capsuleColliderBeforeFixedUpdate, *capsuleColliderAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::CapsuleCollider2D);
                }

                const auto* joint2DAfterFixedUpdate = m_Registry.try_get<Joint2DComponent>(entity);
                const bool hasJoint2DAfterFixedUpdate = joint2DAfterFixedUpdate != nullptr;
                if (hadJoint2DBeforeFixedUpdate != hasJoint2DAfterFixedUpdate ||
                    (hadJoint2DBeforeFixedUpdate && joint2DAfterFixedUpdate &&
                     HasJoint2DChangedForAccessValidation(joint2DBeforeFixedUpdate, *joint2DAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Joint2D);
                }

                const auto* tagAfterFixedUpdate = m_Registry.try_get<TagComponent>(entity);
                const bool hasTagAfterFixedUpdate = tagAfterFixedUpdate != nullptr;
                if (hadTagBeforeFixedUpdate != hasTagAfterFixedUpdate ||
                    (hadTagBeforeFixedUpdate && tagAfterFixedUpdate &&
                     HasTagChangedForAccessValidation(tagBeforeFixedUpdate, *tagAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Metadata);
                }

                const auto* spriteAfterFixedUpdate = m_Registry.try_get<SpriteComponent>(entity);
                const bool hasSpriteAfterFixedUpdate = spriteAfterFixedUpdate != nullptr;
                if (hadSpriteBeforeFixedUpdate != hasSpriteAfterFixedUpdate ||
                    (hadSpriteBeforeFixedUpdate && spriteAfterFixedUpdate &&
                     HasSpriteChangedForAccessValidation(spriteBeforeFixedUpdate, *spriteAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Rendering2D);
                }

                const auto* materialAfterFixedUpdate = m_Registry.try_get<MaterialComponent>(entity);
                const bool hasMaterialAfterFixedUpdate = materialAfterFixedUpdate != nullptr;
                if (hadMaterialBeforeFixedUpdate != hasMaterialAfterFixedUpdate ||
                    (hadMaterialBeforeFixedUpdate && materialAfterFixedUpdate &&
                     HasMaterialChangedForAccessValidation(materialBeforeFixedUpdate, *materialAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Rendering2D);
                }

                const auto* canvasAfterFixedUpdate = m_Registry.try_get<CanvasComponent>(entity);
                const bool hasCanvasAfterFixedUpdate = canvasAfterFixedUpdate != nullptr;
                if (hadCanvasBeforeFixedUpdate != hasCanvasAfterFixedUpdate ||
                    (hadCanvasBeforeFixedUpdate && canvasAfterFixedUpdate &&
                     HasCanvasChangedForAccessValidation(canvasBeforeFixedUpdate, *canvasAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::UI);
                }

                const auto* rectTransformAfterFixedUpdate = m_Registry.try_get<RectTransformComponent>(entity);
                const bool hasRectTransformAfterFixedUpdate = rectTransformAfterFixedUpdate != nullptr;
                if (hadRectTransformBeforeFixedUpdate != hasRectTransformAfterFixedUpdate ||
                    (hadRectTransformBeforeFixedUpdate && rectTransformAfterFixedUpdate &&
                     HasRectTransformChangedForAccessValidation(rectTransformBeforeFixedUpdate, *rectTransformAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::UI);
                }

                const auto* uiImageAfterFixedUpdate = m_Registry.try_get<UIImageComponent>(entity);
                const bool hasUIImageAfterFixedUpdate = uiImageAfterFixedUpdate != nullptr;
                if (hadUIImageBeforeFixedUpdate != hasUIImageAfterFixedUpdate ||
                    (hadUIImageBeforeFixedUpdate && uiImageAfterFixedUpdate &&
                     HasUIImageChangedForAccessValidation(uiImageBeforeFixedUpdate, *uiImageAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::UI);
                }

                const auto* uiPanelAfterFixedUpdate = m_Registry.try_get<UIPanelComponent>(entity);
                const bool hasUIPanelAfterFixedUpdate = uiPanelAfterFixedUpdate != nullptr;
                if (hadUIPanelBeforeFixedUpdate != hasUIPanelAfterFixedUpdate ||
                    (hadUIPanelBeforeFixedUpdate && uiPanelAfterFixedUpdate &&
                     HasUIPanelChangedForAccessValidation(uiPanelBeforeFixedUpdate, *uiPanelAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::UI);
                }

                const auto* uiTextAfterFixedUpdate = m_Registry.try_get<UITextComponent>(entity);
                const bool hasUITextAfterFixedUpdate = uiTextAfterFixedUpdate != nullptr;
                if (hadUITextBeforeFixedUpdate != hasUITextAfterFixedUpdate ||
                    (hadUITextBeforeFixedUpdate && uiTextAfterFixedUpdate &&
                     HasUITextChangedForAccessValidation(uiTextBeforeFixedUpdate, *uiTextAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::UI);
                }

                const auto* uiButtonAfterFixedUpdate = m_Registry.try_get<UIButtonComponent>(entity);
                const bool hasUIButtonAfterFixedUpdate = uiButtonAfterFixedUpdate != nullptr;
                if (hadUIButtonBeforeFixedUpdate != hasUIButtonAfterFixedUpdate ||
                    (hadUIButtonBeforeFixedUpdate && uiButtonAfterFixedUpdate &&
                     HasUIButtonChangedForAccessValidation(uiButtonBeforeFixedUpdate, *uiButtonAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::UI);
                }

                const auto* uiSliderAfterFixedUpdate = m_Registry.try_get<UISliderComponent>(entity);
                const bool hasUISliderAfterFixedUpdate = uiSliderAfterFixedUpdate != nullptr;
                if (hadUISliderBeforeFixedUpdate != hasUISliderAfterFixedUpdate ||
                    (hadUISliderBeforeFixedUpdate && uiSliderAfterFixedUpdate &&
                     HasUISliderChangedForAccessValidation(uiSliderBeforeFixedUpdate, *uiSliderAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::UI);
                }

                const auto* directionalLightAfterFixedUpdate = m_Registry.try_get<DirectionalLight2DComponent>(entity);
                const bool hasDirectionalLightAfterFixedUpdate = directionalLightAfterFixedUpdate != nullptr;
                if (hadDirectionalLightBeforeFixedUpdate != hasDirectionalLightAfterFixedUpdate ||
                    (hadDirectionalLightBeforeFixedUpdate && directionalLightAfterFixedUpdate &&
                     HasDirectionalLight2DChangedForAccessValidation(directionalLightBeforeFixedUpdate, *directionalLightAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Lighting2D);
                }

                const auto* pointLightAfterFixedUpdate = m_Registry.try_get<PointLight2DComponent>(entity);
                const bool hasPointLightAfterFixedUpdate = pointLightAfterFixedUpdate != nullptr;
                if (hadPointLightBeforeFixedUpdate != hasPointLightAfterFixedUpdate ||
                    (hadPointLightBeforeFixedUpdate && pointLightAfterFixedUpdate &&
                     HasPointLight2DChangedForAccessValidation(pointLightBeforeFixedUpdate, *pointLightAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Lighting2D);
                }

                const auto* shadowOccluderAfterFixedUpdate = m_Registry.try_get<ShadowOccluder2DComponent>(entity);
                const bool hasShadowOccluderAfterFixedUpdate = shadowOccluderAfterFixedUpdate != nullptr;
                if (hadShadowOccluderBeforeFixedUpdate != hasShadowOccluderAfterFixedUpdate ||
                    (hadShadowOccluderBeforeFixedUpdate && shadowOccluderAfterFixedUpdate &&
                     HasShadowOccluder2DChangedForAccessValidation(shadowOccluderBeforeFixedUpdate, *shadowOccluderAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Lighting2D);
                }

                const auto* audioListenerAfterFixedUpdate = m_Registry.try_get<AudioListener2DComponent>(entity);
                const bool hasAudioListenerAfterFixedUpdate = audioListenerAfterFixedUpdate != nullptr;
                if (hadAudioListenerBeforeFixedUpdate != hasAudioListenerAfterFixedUpdate ||
                    (hadAudioListenerBeforeFixedUpdate && audioListenerAfterFixedUpdate &&
                     HasAudioListener2DChangedForAccessValidation(audioListenerBeforeFixedUpdate, *audioListenerAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Audio);
                }

                const auto* audioListener3DAfterFixedUpdate = m_Registry.try_get<AudioListener3DComponent>(entity);
                const bool hasAudioListener3DAfterFixedUpdate = audioListener3DAfterFixedUpdate != nullptr;
                if (hadAudioListener3DBeforeFixedUpdate != hasAudioListener3DAfterFixedUpdate ||
                    (hadAudioListener3DBeforeFixedUpdate && audioListener3DAfterFixedUpdate &&
                     HasAudioListener3DChangedForAccessValidation(audioListener3DBeforeFixedUpdate, *audioListener3DAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Audio);
                }

                const auto* audioSourceAfterFixedUpdate = m_Registry.try_get<AudioSourceComponent>(entity);
                const bool hasAudioSourceAfterFixedUpdate = audioSourceAfterFixedUpdate != nullptr;
                if (hadAudioSourceBeforeFixedUpdate != hasAudioSourceAfterFixedUpdate ||
                    (hadAudioSourceBeforeFixedUpdate && audioSourceAfterFixedUpdate &&
                     HasAudioSourceChangedForAccessValidation(audioSourceBeforeFixedUpdate, *audioSourceAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Audio);
                }

                const auto* cameraAfterFixedUpdate = m_Registry.try_get<CameraComponent>(entity);
                const bool hasCameraAfterFixedUpdate = cameraAfterFixedUpdate != nullptr;
                if (hadCameraBeforeFixedUpdate != hasCameraAfterFixedUpdate ||
                    (hadCameraBeforeFixedUpdate && cameraAfterFixedUpdate &&
                     HasCameraChangedForAccessValidation(cameraBeforeFixedUpdate, *cameraAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Camera);
                }

                const auto* prefabInstanceAfterFixedUpdate = m_Registry.try_get<PrefabInstanceComponent>(entity);
                const bool hasPrefabInstanceAfterFixedUpdate = prefabInstanceAfterFixedUpdate != nullptr;
                if (hadPrefabInstanceBeforeFixedUpdate != hasPrefabInstanceAfterFixedUpdate ||
                    (hadPrefabInstanceBeforeFixedUpdate && prefabInstanceAfterFixedUpdate &&
                     HasPrefabInstanceChangedForAccessValidation(prefabInstanceBeforeFixedUpdate, *prefabInstanceAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Metadata);
                }

                const auto* grid2DAfterFixedUpdate = m_Registry.try_get<Grid2DComponent>(entity);
                const bool hasGrid2DAfterFixedUpdate = grid2DAfterFixedUpdate != nullptr;
                if (hadGrid2DBeforeFixedUpdate != hasGrid2DAfterFixedUpdate ||
                    (hadGrid2DBeforeFixedUpdate && grid2DAfterFixedUpdate &&
                     HasGrid2DChangedForAccessValidation(grid2DBeforeFixedUpdate, *grid2DAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Tilemap);
                }

                const auto* tilemapLayerAfterFixedUpdate = m_Registry.try_get<TilemapLayerComponent>(entity);
                const bool hasTilemapLayerAfterFixedUpdate = tilemapLayerAfterFixedUpdate != nullptr;
                if (hadTilemapLayerBeforeFixedUpdate != hasTilemapLayerAfterFixedUpdate ||
                    (hadTilemapLayerBeforeFixedUpdate && tilemapLayerAfterFixedUpdate &&
                     HasTilemapLayerChangedFromSnapshot(tilemapLayerSnapshotBeforeFixedUpdate, *tilemapLayerAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Tilemap);
                }

                const auto* animatorAfterFixedUpdate = m_Registry.try_get<AnimatorComponent>(entity);
                const bool hasAnimatorAfterFixedUpdate = animatorAfterFixedUpdate != nullptr;
                if (hadAnimatorBeforeFixedUpdate != hasAnimatorAfterFixedUpdate ||
                    (hadAnimatorBeforeFixedUpdate && animatorAfterFixedUpdate &&
                     HasAnimatorChangedForAccessValidation(animatorBeforeFixedUpdate, *animatorAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Animator);
                }

                const auto* animationEventReceiverAfterFixedUpdate = m_Registry.try_get<AnimationEventReceiverComponent>(entity);
                const bool hasAnimationEventReceiverAfterFixedUpdate = animationEventReceiverAfterFixedUpdate != nullptr;
                if (hadAnimationEventReceiverBeforeFixedUpdate != hasAnimationEventReceiverAfterFixedUpdate ||
                    (hadAnimationEventReceiverBeforeFixedUpdate && animationEventReceiverAfterFixedUpdate &&
                     HasAnimationEventReceiverChangedForAccessValidation(animationEventReceiverBeforeFixedUpdate, *animationEventReceiverAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Animator);
                }

                const auto* particleEmitterAfterFixedUpdate = m_Registry.try_get<ParticleEmitterComponent>(entity);
                const bool hasParticleEmitterAfterFixedUpdate = particleEmitterAfterFixedUpdate != nullptr;
                if (hadParticleEmitterBeforeFixedUpdate != hasParticleEmitterAfterFixedUpdate ||
                    (hadParticleEmitterBeforeFixedUpdate && particleEmitterAfterFixedUpdate &&
                     HasParticleEmitterChangedForAccessValidation(particleEmitterBeforeFixedUpdate, *particleEmitterAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::ParticleEmitter);
                }
            }

            validateParallelScriptAccessMask(entity, *scriptEntry, observedWriteMask, "OnFixedUpdate");
        };

        // Mirror Update() bootstrap so FixedUpdate callbacks can also resolve
        // other scripts during OnCreate regardless of declaration order.
        std::vector<std::pair<entt::entity, size_t>> parallelScriptSlots;
        parallelScriptSlots.reserve(scriptSlots.size());
        const bool enableParallelScripts = ConfigManager::GetInstance().GetValue<bool>("ecs.mt.enable_parallel_scripts", true);
        const bool requireParallelScriptAccessDeclarations =
            ConfigManager::GetInstance().GetValue<bool>("ecs.mt.require_parallel_script_access_declarations", true);
        const bool warnOnImplicitParallelScriptAccess =
            ConfigManager::GetInstance().GetValue<bool>("ecs.mt.warn_implicit_parallel_script_access", true);
        auto tryQueueParallelScriptSlot = [&](entt::entity entity, size_t scriptIndex, NativeScriptEntry& scriptEntry) {
            if (!(enableParallelScripts && scriptEntry.ExecutionPolicy == ScriptExecutionPolicy::ParallelSafe))
                return false;

            const bool hasAccessDeclaration = scriptEntry.DeclaredReadAccessMask != 0 || scriptEntry.DeclaredWriteAccessMask != 0;
            if (!hasAccessDeclaration)
            {
                if (warnOnImplicitParallelScriptAccess && !scriptEntry.RuntimeWarnedMissingAccessDeclaration)
                {
                    const auto* tag = m_Registry.try_get<TagComponent>(entity);
                    LT_WARN("ParallelSafe script '{}' on entity '{}' has no declared component access mask and will {}. Author DeclaredReadAccessMask/DeclaredWriteAccessMask for deterministic scheduling.",
                            scriptEntry.ScriptClassName,
                            tag ? tag->Tag : "Entity",
                            requireParallelScriptAccessDeclarations ? "run on the main thread this frame" : "use conservative scheduler barriers");
                    scriptEntry.RuntimeWarnedMissingAccessDeclaration = true;
                }

                if (requireParallelScriptAccessDeclarations)
                    return false;
            }

            parallelScriptSlots.emplace_back(entity, scriptIndex);
            return true;
        };
        SetRuntimePhase(RuntimePhase::ScriptMainThread);
        for (const auto& managedScriptSlot : managedScriptSlots)
        {
            const entt::entity entity = managedScriptSlot.first;
            const size_t scriptIndex = managedScriptSlot.second;
            ManagedScriptEntry* scriptEntry = tryGetManagedEntry(entity, scriptIndex);
            if (!scriptEntry)
                continue;

            if (!scriptEntry->Enabled || scriptEntry->ScriptClassName.empty() || !IsEntityEnabledInHierarchy(entity))
            {
                if (scriptEntry->RuntimeInitialized && scriptEntry->RuntimeInstanceId != 0)
                {
                    std::string managedError;
                    executeWithDeferredEntityDestroy([&]() {
                        if (!ManagedScriptHost::InvokeScriptOnDestroy(scriptEntry->RuntimeInstanceId, this, &managedError))
                        {
                            handleManagedScriptCallbackFailure(entity, scriptIndex, "OnDestroy", managedError.empty() ? "unknown error" : managedError.c_str());
                        }
                    });
                }

                scriptEntry = tryGetManagedEntry(entity, scriptIndex);
                if (!scriptEntry)
                    continue;

                destroyManagedScriptRuntime(*scriptEntry);
                continue;
            }

            if (!ManagedScriptHost::IsInitialized())
            {
                if (!scriptEntry->RuntimeWarnedMissingHost)
                {
                    const auto* tag = m_Registry.try_get<TagComponent>(entity);
                    LT_WARN("Managed script '{}' on entity '{}' cannot execute because the managed host is unavailable.",
                            scriptEntry->ScriptClassName,
                            tag ? tag->Tag : "Entity");
                    scriptEntry->RuntimeWarnedMissingHost = true;
                }
                destroyManagedScriptRuntime(*scriptEntry);
                continue;
            }
            scriptEntry->RuntimeWarnedMissingHost = false;

            if (!ManagedScriptHost::HasDiscoveredClass(scriptEntry->ScriptClassName))
            {
                if (!scriptEntry->RuntimeWarnedMissingClass)
                {
                    const auto* tag = m_Registry.try_get<TagComponent>(entity);
                    LT_WARN("Managed script '{}' on entity '{}' was not discovered in the active managed assemblies.",
                            scriptEntry->ScriptClassName,
                            tag ? tag->Tag : "Entity");
                    scriptEntry->RuntimeWarnedMissingClass = true;
                }

                destroyManagedScriptRuntime(*scriptEntry);
                continue;
            }
            scriptEntry->RuntimeWarnedMissingClass = false;

            TransformComponent transformBeforeFixedUpdate{};
            bool hadTransformBeforeFixedUpdate = false;
            if (auto* transform = m_Registry.try_get<TransformComponent>(entity))
            {
                transformBeforeFixedUpdate = *transform;
                hadTransformBeforeFixedUpdate = true;
            }

            if (scriptEntry->RuntimeInstanceId == 0)
            {
                std::string managedError;
                scriptEntry->RuntimeInstanceId = ManagedScriptHost::CreateScriptInstance(scriptEntry->ScriptClassName,
                                                                                         static_cast<uint32_t>(entity),
                                                                                         &managedError);
                if (scriptEntry->RuntimeInstanceId == 0)
                {
                    handleManagedScriptCallbackFailure(entity, scriptIndex, "CreateInstance", managedError.empty() ? "unknown error" : managedError.c_str());
                    continue;
                }

                scriptEntry->RuntimeInitialized = false;
                scriptEntry->RuntimeUpdateCount = 0;
                scriptEntry->RuntimeWarnedOnUpdateTransformMutation = false;
            }

            {
                std::string managedError;
                if (!ManagedScriptHost::SynchronizeScriptExposedProperties(scriptEntry->RuntimeInstanceId,
                                                                           this,
                                                                           scriptEntry->ExposedProperties,
                                                                           scriptEntry->RuntimeExposedPropertiesRevision,
                                                                           &managedError))
                {
                    handleManagedScriptCallbackFailure(entity,
                                                       scriptIndex,
                                                       "SyncExposedProperties",
                                                       managedError.empty() ? "unknown error" : managedError.c_str());
                    continue;
                }
            }

            if (!scriptEntry->RuntimeInitialized)
            {
                std::string managedError;
                executeWithDeferredEntityDestroy([&]() {
                    if (!ManagedScriptHost::InvokeScriptOnCreate(scriptEntry->RuntimeInstanceId, this, &managedError))
                    {
                        handleManagedScriptCallbackFailure(entity, scriptIndex, "OnCreate", managedError.empty() ? "unknown error" : managedError.c_str());
                    }
                });

                scriptEntry = tryGetManagedEntry(entity, scriptIndex);
                if (!scriptEntry || scriptEntry->RuntimeInstanceId == 0)
                    continue;

                if (!managedError.empty())
                    continue;

                if (!readBackManagedScriptExposedProperties(entity, scriptIndex, "OnCreate"))
                    continue;

                scriptEntry = tryGetManagedEntry(entity, scriptIndex);
                if (!scriptEntry || scriptEntry->RuntimeInstanceId == 0)
                    continue;

                scriptEntry->RuntimeInitialized = true;
            }

            std::string managedError;
            if (!ManagedScriptHost::SynchronizeScriptExposedProperties(scriptEntry->RuntimeInstanceId,
                                                                       this,
                                                                       scriptEntry->ExposedProperties,
                                                                       scriptEntry->RuntimeExposedPropertiesRevision,
                                                                       &managedError))
            {
                handleManagedScriptCallbackFailure(entity,
                                                   scriptIndex,
                                                   "SyncExposedProperties",
                                                   managedError.empty() ? "unknown error" : managedError.c_str());
                continue;
            }

            managedError.clear();
            executeWithDeferredEntityDestroy([&]() {
                if (!ManagedScriptHost::InvokeScriptOnFixedUpdate(scriptEntry->RuntimeInstanceId, this, fixedDeltaTime, &managedError))
                {
                    handleManagedScriptCallbackFailure(entity, scriptIndex, "OnFixedUpdate", managedError.empty() ? "unknown error" : managedError.c_str());
                }
            });

            scriptEntry = tryGetManagedEntry(entity, scriptIndex);
            if (!scriptEntry || scriptEntry->RuntimeInstanceId == 0)
                continue;

            if (!managedError.empty())
                continue;

            if (!readBackManagedScriptExposedProperties(entity, scriptIndex, "OnFixedUpdate"))
                continue;

            scriptEntry = tryGetManagedEntry(entity, scriptIndex);
            if (!scriptEntry || scriptEntry->RuntimeInstanceId == 0)
                continue;

            const auto* transformAfterFixedUpdate = m_Registry.try_get<TransformComponent>(entity);
            bool transformChanged = false;
            if (hadTransformBeforeFixedUpdate && transformAfterFixedUpdate)
            {
                transformChanged = HasTransformChangedForAccessValidation(transformBeforeFixedUpdate, *transformAfterFixedUpdate);
                if (transformChanged)
                    MarkTransformDirty(entity);
            }
            else if (!hadTransformBeforeFixedUpdate && transformAfterFixedUpdate)
            {
                MarkTransformDirty(entity);
            }
        }

        for (const auto& scriptSlot : scriptSlots)
        {
            const entt::entity entity = scriptSlot.first;
            const size_t scriptIndex = scriptSlot.second;
            NativeScriptEntry* scriptEntry = tryGetScriptEntry(entity, scriptIndex);
            if (!scriptEntry)
                continue;

            if (!scriptEntry->Enabled || scriptEntry->ScriptClassName.empty() || !IsEntityEnabledInHierarchy(entity))
                continue;

            if (!scriptEntry->RuntimeInstance)
            {
                scriptEntry->RuntimeInstance = NativeScriptRegistry::CreateScript(scriptEntry->ScriptClassName);
                if (!scriptEntry->RuntimeInstance)
                {
                    const std::string resolvedClassName = ResolveRegisteredScriptClassNameForSceneRuntime(scriptEntry->ScriptClassName,
                                                                                                           scriptEntry->ScriptAssetRelativePath);
                    if (!resolvedClassName.empty())
                    {
                        scriptEntry->ScriptClassName = resolvedClassName;
                        scriptEntry->RuntimeInstance = NativeScriptRegistry::CreateScript(scriptEntry->ScriptClassName);
                    }
                }
                if (scriptEntry->RuntimeInstance)
                {
                    scriptEntry->RuntimeInstance->m_Scene = this;
                    scriptEntry->RuntimeInstance->m_Registry = &m_Registry;
                    scriptEntry->RuntimeInstance->m_EntityHandle = entity;
                    scriptEntry->RuntimeInstance->m_ExposedProperties = &scriptEntry->ExposedProperties;
                    scriptEntry->RuntimeInstance->m_ExposedPropertiesRevision = &scriptEntry->RuntimeExposedPropertiesRevision;
                    scriptEntry->RuntimeInitialized = false;
                    scriptEntry->RuntimeUpdateCount = 0;
                    scriptEntry->RuntimeWarnedOnUpdateTransformMutation = false;
                    scriptEntry->RuntimeWarnedMissingCompiledScript = false;
                    scriptEntry->RuntimeWarnedMissingAccessDeclaration = false;
                    scriptEntry->RuntimeWarnedAccessMaskMismatch = false;
                }
            }

            if (scriptEntry->RuntimeInstance)
            {
                scriptEntry->RuntimeInstance->m_Scene = this;
                scriptEntry->RuntimeInstance->m_Registry = &m_Registry;
                scriptEntry->RuntimeInstance->m_EntityHandle = entity;
                scriptEntry->RuntimeInstance->m_ExposedProperties = &scriptEntry->ExposedProperties;
                scriptEntry->RuntimeInstance->m_ExposedPropertiesRevision = &scriptEntry->RuntimeExposedPropertiesRevision;
            }
        }

        for (const auto& scriptSlot : scriptSlots)
        {
            const entt::entity entity = scriptSlot.first;
            const size_t scriptIndex = scriptSlot.second;
            NativeScriptEntry* scriptEntry = tryGetScriptEntry(entity, scriptIndex);
            if (!scriptEntry)
                continue;

            if (!scriptEntry->Enabled || scriptEntry->ScriptClassName.empty() || !IsEntityEnabledInHierarchy(entity))
            {
                if (scriptEntry->RuntimeInstance)
                {
                    if (scriptEntry->RuntimeInitialized)
                    {
                        try
                        {
                            executeWithDeferredEntityDestroy([&]() {
                                scriptEntry->RuntimeInstance->UnsubscribeAllScriptEvents();
                                scriptEntry->RuntimeInstance->OnDestroy();
                            });
                        }
                        catch (const std::exception& exception)
                        {
                            handleScriptCallbackFailure(entity, scriptIndex, "OnDestroy", exception.what());
                        }
                        catch (...)
                        {
                            handleScriptCallbackFailure(entity, scriptIndex, "OnDestroy", "non-standard exception");
                        }
                    }
                }

                scriptEntry = tryGetScriptEntry(entity, scriptIndex);
                if (!scriptEntry)
                    continue;

                if (scriptEntry->RuntimeInstance)
                {
                    Coroutine::StopAll(*scriptEntry->RuntimeInstance);
                    scriptEntry->RuntimeInstance.reset();
                }
                scriptEntry->RuntimeInitialized = false;
                scriptEntry->RuntimeUpdateCount = 0;
                scriptEntry->RuntimeWarnedOnUpdateTransformMutation = false;
                scriptEntry->RuntimeWarnedMissingCompiledScript = false;
                scriptEntry->RuntimeWarnedMissingAccessDeclaration = false;
                scriptEntry->RuntimeWarnedAccessMaskMismatch = false;
                continue;
            }

            if (!scriptEntry->RuntimeInstance)
            {
                scriptEntry->RuntimeInstance = NativeScriptRegistry::CreateScript(scriptEntry->ScriptClassName);
                if (!scriptEntry->RuntimeInstance)
                {
                    const std::string resolvedClassName = ResolveRegisteredScriptClassNameForSceneRuntime(scriptEntry->ScriptClassName,
                                                                                                           scriptEntry->ScriptAssetRelativePath);
                    if (!resolvedClassName.empty())
                    {
                        scriptEntry->ScriptClassName = resolvedClassName;
                        scriptEntry->RuntimeInstance = NativeScriptRegistry::CreateScript(scriptEntry->ScriptClassName);
                    }
                }
                if (scriptEntry->RuntimeInstance)
                {
                    scriptEntry->RuntimeInstance->m_Scene = this;
                    scriptEntry->RuntimeInstance->m_Registry = &m_Registry;
                    scriptEntry->RuntimeInstance->m_EntityHandle = entity;
                    scriptEntry->RuntimeInstance->m_ExposedProperties = &scriptEntry->ExposedProperties;
                    scriptEntry->RuntimeInstance->m_ExposedPropertiesRevision = &scriptEntry->RuntimeExposedPropertiesRevision;
                    scriptEntry->RuntimeInitialized = false;
                    scriptEntry->RuntimeUpdateCount = 0;
                    scriptEntry->RuntimeWarnedOnUpdateTransformMutation = false;
                    scriptEntry->RuntimeWarnedMissingCompiledScript = false;
                    scriptEntry->RuntimeWarnedMissingAccessDeclaration = false;
                    scriptEntry->RuntimeWarnedAccessMaskMismatch = false;
                }
            }

            if (!scriptEntry->RuntimeInstance)
            {
                if (!scriptEntry->RuntimeWarnedMissingCompiledScript)
                {
                    const auto* tag = m_Registry.try_get<TagComponent>(entity);
                    LT_WARN("Script '{}' on entity '{}' is not compiled/registered in ScriptCore. Build project scripts before entering Play Mode.",
                            scriptEntry->ScriptClassName,
                            tag ? tag->Tag : "Entity");
                    scriptEntry->RuntimeWarnedMissingCompiledScript = true;
                }
                continue;
            }

            scriptEntry->RuntimeInstance->m_Scene = this;
            scriptEntry->RuntimeInstance->m_Registry = &m_Registry;
            scriptEntry->RuntimeInstance->m_EntityHandle = entity;
            scriptEntry->RuntimeInstance->m_ExposedProperties = &scriptEntry->ExposedProperties;
            scriptEntry->RuntimeInstance->m_ExposedPropertiesRevision = &scriptEntry->RuntimeExposedPropertiesRevision;

            if (!scriptEntry->RuntimeInitialized)
            {
                try
                {
                    executeWithDeferredEntityDestroy([&]() {
                        scriptEntry->RuntimeInstance->OnSynchronizeExposedFields();
                        scriptEntry->RuntimeInstance->MarkExposedPropertySyncComplete();
                        scriptEntry->RuntimeInstance->OnCreate();
                        scriptEntry->RuntimeInstance->OnWriteBackExposedFields();
                        scriptEntry->RuntimeInstance->MarkExposedPropertySyncComplete();
                    });
                }
                catch (const std::exception& exception)
                {
                    handleScriptCallbackFailure(entity, scriptIndex, "OnCreate", exception.what());
                    continue;
                }
                catch (...)
                {
                    handleScriptCallbackFailure(entity, scriptIndex, "OnCreate", "non-standard exception");
                    continue;
                }

                scriptEntry = tryGetScriptEntry(entity, scriptIndex);
                if (!scriptEntry || !scriptEntry->RuntimeInstance)
                    continue;
                scriptEntry->RuntimeInitialized = true;
            }

            applyScriptDeclaredAccessDefaults(*scriptEntry);
            if (tryQueueParallelScriptSlot(entity, scriptIndex, *scriptEntry))
            {
                continue;
            }

            executeScriptFixedUpdateSlot(entity, scriptIndex);
        }

        if (!parallelScriptSlots.empty())
        {
            auto& jobSystem = Concurrency::GetJobSystem();
            const size_t parallelScriptMinSlots = ResolveParallelScriptMinSlots(jobSystem);
            const size_t parallelScriptMinBatchSize = ResolveParallelScriptMinBatchSize();
            const bool shouldUseParallelJobs = jobSystem.IsInitialized() &&
                                               parallelScriptSlots.size() >= parallelScriptMinSlots;
            auto runParallelSlotAtIndex = [&](size_t slotIndex) {
                struct ScopedParallelScriptThreadFlag final
                {
                    ScopedParallelScriptThreadFlag() { Scene::SetCurrentThreadParallelScriptExecution(true); }
                    ~ScopedParallelScriptThreadFlag() { Scene::SetCurrentThreadParallelScriptExecution(false); }
                } scopedThreadFlag;

                const auto& slot = parallelScriptSlots[slotIndex];
                executeScriptFixedUpdateSlot(slot.first, slot.second);
            };
            if (shouldUseParallelJobs)
                SetRuntimePhase(RuntimePhase::ScriptParallel);

            auto hasAccessHazard = [](const SceneSystemAccess& left, const SceneSystemAccess& right) {
                return (left.Writes & right.Writes) != 0 ||
                       (left.Writes & right.Reads) != 0 ||
                       (left.Reads & right.Writes) != 0;
            };
            auto getScriptAccess = [&](size_t slotIndex) {
                SceneSystemAccess access{};
                const auto& slot = parallelScriptSlots[slotIndex];
                const NativeScriptEntry* scriptEntry = tryGetScriptEntry(slot.first, slot.second);
                if (!scriptEntry)
                {
                    access.Writes = ~0ull;
                    return access;
                }
                access.Reads = scriptEntry->DeclaredReadAccessMask;
                access.Writes = scriptEntry->DeclaredWriteAccessMask;
                if (access.Reads == 0 && access.Writes == 0)
                    access.Writes = ~0ull;
                return access;
            };

            std::vector<size_t> pendingIndices(parallelScriptSlots.size());
            for (size_t index = 0; index < pendingIndices.size(); ++index)
                pendingIndices[index] = index;

            while (!pendingIndices.empty())
            {
                std::vector<size_t> batchIndices;
                std::vector<size_t> nextPendingIndices;
                batchIndices.reserve(pendingIndices.size());
                nextPendingIndices.reserve(pendingIndices.size());

                for (size_t pendingIndex : pendingIndices)
                {
                    const SceneSystemAccess candidateAccess = getScriptAccess(pendingIndex);
                    bool conflicts = false;
                    for (size_t batchIndex : batchIndices)
                    {
                        if (hasAccessHazard(candidateAccess, getScriptAccess(batchIndex)))
                        {
                            conflicts = true;
                            break;
                        }
                    }

                    if (!conflicts)
                        batchIndices.push_back(pendingIndex);
                    else
                        nextPendingIndices.push_back(pendingIndex);
                }

                if (batchIndices.empty())
                {
                    batchIndices.push_back(pendingIndices.front());
                    nextPendingIndices.erase(std::remove(nextPendingIndices.begin(), nextPendingIndices.end(), pendingIndices.front()), nextPendingIndices.end());
                }

                if (shouldUseParallelJobs && batchIndices.size() >= parallelScriptMinBatchSize)
                {
                    Concurrency::WaitGroup waitGroup;
                    for (size_t batchIndex : batchIndices)
                    {
                        waitGroup.Add(1);
                        const bool submitted = jobSystem.Submit([&runParallelSlotAtIndex, &waitGroup, batchIndex]() {
                            struct WaitGroupDoneGuard final
                            {
                                Concurrency::WaitGroup& Group;
                                ~WaitGroupDoneGuard() { Group.Done(); }
                            } doneGuard{ waitGroup };

                            runParallelSlotAtIndex(batchIndex);
                        });
                        if (!submitted)
                        {
                            runParallelSlotAtIndex(batchIndex);
                            waitGroup.Done();
                        }
                    }
                    waitGroup.Wait();
                }
                else
                {
                    for (size_t batchIndex : batchIndices)
                        runParallelSlotAtIndex(batchIndex);
                }

                pendingIndices = std::move(nextPendingIndices);
            }
        }

        // Final frame writeback pass mirrors Update(). This preserves cross-script
        // field mutations made outside a script's own fixed-update callback.
        for (const auto& scriptSlot : scriptSlots)
        {
            const entt::entity entity = scriptSlot.first;
            const size_t scriptIndex = scriptSlot.second;
            NativeScriptEntry* scriptEntry = tryGetScriptEntry(entity, scriptIndex);
            if (!scriptEntry || !scriptEntry->RuntimeInstance || !scriptEntry->RuntimeInitialized)
                continue;
            if (!scriptEntry->Enabled || scriptEntry->ScriptClassName.empty() || !IsEntityEnabledInHierarchy(entity))
                continue;

            scriptEntry->RuntimeInstance->m_Scene = this;
            scriptEntry->RuntimeInstance->m_Registry = &m_Registry;
            scriptEntry->RuntimeInstance->m_EntityHandle = entity;
            scriptEntry->RuntimeInstance->m_ExposedProperties = &scriptEntry->ExposedProperties;
            scriptEntry->RuntimeInstance->m_ExposedPropertiesRevision = &scriptEntry->RuntimeExposedPropertiesRevision;

            try
            {
                executeWithDeferredEntityDestroy([&]() {
                    scriptEntry->RuntimeInstance->OnWriteBackExposedFields();
                    scriptEntry->RuntimeInstance->MarkExposedPropertySyncComplete();
                });
            }
            catch (const std::exception& exception)
            {
                handleScriptCallbackFailure(entity, scriptIndex, "OnWriteBackExposedFields", exception.what());
            }
            catch (...)
            {
                handleScriptCallbackFailure(entity, scriptIndex, "OnWriteBackExposedFields", "non-standard exception");
            }
        }

        SetRuntimePhase(RuntimePhase::Structural);
        FlushDeferredStructuralMutations();
        SetRuntimePhase(RuntimePhase::Transform);
        UpdateTransforms();
        SetRuntimePhase(RuntimePhase::Idle);
    }
}
