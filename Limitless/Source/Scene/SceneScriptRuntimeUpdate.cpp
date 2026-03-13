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
    void Scene::Update(float deltaTime)
    {
        SetRuntimePhase(RuntimePhase::Structural);
        FlushDeferredStructuralMutations();

        Physics2DQueries::SetActiveSceneForScriptQueries(this);
        if (Application::HasInstance())
        {
            auto& window = Application::GetInstance().GetWindow();
            ProcessUiInteractionSystemForSceneRuntime(*this, window.GetWidth(), window.GetHeight());
        }
        else
        {
            ProcessUiInteractionSystemForSceneRuntime(*this, 0, 0);
        }

        auto runScheduledSimulationSystems = [&]() {
            SetRuntimePhase(RuntimePhase::Simulation);
            const bool enableSystemScheduler = ConfigManager::GetInstance().GetValue<bool>("ecs.mt.enable_system_scheduler", true);
            if (!enableSystemScheduler)
            {
                UpdateAnimation2DSystemForSceneRuntime(*this, deltaTime, ++m_AnimationDispatchFrameCounter);
                UpdateParticleEmitterSystem(m_Registry, deltaTime);
                return;
            }

            std::vector<ScheduledSceneSystem> scheduledSystems;
            ScheduledSceneSystem animationSystem{};
            animationSystem.Name = "Animation2D";
            animationSystem.Access.Reads = ToAccessMask(SceneSystemAccessComponent::Animator);
            animationSystem.Access.Writes = ToAccessMask(SceneSystemAccessComponent::Animator) |
                                           ToAccessMask(SceneSystemAccessComponent::Transform);
            animationSystem.Execute = [this, deltaTime]() {
                UpdateAnimation2DSystemForSceneRuntime(*this, deltaTime, ++m_AnimationDispatchFrameCounter);
            };
            animationSystem.AllowParallel = true;
            scheduledSystems.push_back(std::move(animationSystem));

            ScheduledSceneSystem particleSystem{};
            particleSystem.Name = "ParticleEmitter";
            particleSystem.Access.Reads = ToAccessMask(SceneSystemAccessComponent::Transform);
            particleSystem.Access.Writes = ToAccessMask(SceneSystemAccessComponent::ParticleEmitter);
            particleSystem.Execute = [this, deltaTime]() {
                UpdateParticleEmitterSystem(m_Registry, deltaTime);
            };
            particleSystem.AllowParallel = true;
            scheduledSystems.push_back(std::move(particleSystem));
            SceneSystemScheduler::Run(Concurrency::GetJobSystem(), scheduledSystems);
        };

        if (NativeScriptRegistry::IsExecutionBlocked())
        {
            SetRuntimePhase(RuntimePhase::ScriptMainThread);
            for (entt::entity scriptEntity : CollectOrderedScriptComponentEntities(m_Registry))
            {
                auto* scriptComponent = m_Registry.try_get<ScriptComponent>(scriptEntity);
                if (!scriptComponent)
                    continue;

                const entt::entity entity = scriptComponent->OwnerEntity;
                if (NativeScriptEntry* scriptEntry = scriptComponent->TryGetNativeEntry())
                {
                    if (!scriptEntry->RuntimeInstance)
                        continue;

                    if (scriptEntry->RuntimeInstance)
                    {
                        if (scriptEntry->RuntimeInitialized)
                        {
                            try
                            {
                                scriptEntry->RuntimeInstance->UnsubscribeAllScriptEvents();
                                scriptEntry->RuntimeInstance->OnDestroy();
                            }
                            catch (const std::exception& exception)
                            {
                                const auto* tag = m_Registry.try_get<TagComponent>(entity);
                                LT_ERROR("Script '{}' on entity '{}' threw during OnDestroy while script execution is blocked: {}",
                                         scriptEntry->ScriptClassName,
                                         tag ? tag->Tag : "Entity",
                                         exception.what());
                            }
                            catch (...)
                            {
                                const auto* tag = m_Registry.try_get<TagComponent>(entity);
                                LT_ERROR("Script '{}' on entity '{}' threw a non-standard exception during OnDestroy while script execution is blocked",
                                         scriptEntry->ScriptClassName,
                                         tag ? tag->Tag : "Entity");
                            }
                        }

                        try
                        {
                            Coroutine::StopAll(*scriptEntry->RuntimeInstance);
                        }
                        catch (const std::exception& exception)
                        {
                            const auto* tag = m_Registry.try_get<TagComponent>(entity);
                            LT_WARN("Script '{}' on entity '{}' threw during coroutine cleanup while script execution is blocked: {}",
                                    scriptEntry->ScriptClassName,
                                    tag ? tag->Tag : "Entity",
                                    exception.what());
                        }
                        catch (...)
                        {
                            const auto* tag = m_Registry.try_get<TagComponent>(entity);
                            LT_WARN("Script '{}' on entity '{}' threw a non-standard exception during coroutine cleanup while script execution is blocked",
                                    scriptEntry->ScriptClassName,
                                    tag ? tag->Tag : "Entity");
                        }
                        scriptEntry->RuntimeInstance.reset();
                    }

                    scriptEntry->RuntimeInitialized = false;
                    scriptEntry->RuntimeUpdateCount = 0;
                    scriptEntry->RuntimeWarnedOnUpdateTransformMutation = false;
                    scriptEntry->RuntimeWarnedMissingAccessDeclaration = false;
                    scriptEntry->RuntimeWarnedAccessMaskMismatch = false;
                    continue;
                }

                ManagedScriptEntry* managedScriptEntry = scriptComponent->TryGetManagedEntry();
                if (!managedScriptEntry || managedScriptEntry->RuntimeInstanceId == 0)
                    continue;

                if (managedScriptEntry->RuntimeInitialized)
                {
                    std::string managedError;
                    if (!ManagedScriptHost::InvokeScriptOnDestroy(managedScriptEntry->RuntimeInstanceId, this, &managedError))
                    {
                        const auto* tag = m_Registry.try_get<TagComponent>(entity);
                        LT_ERROR("Managed script '{}' on entity '{}' failed during OnDestroy while script execution is blocked: {}",
                                 managedScriptEntry->ScriptClassName,
                                 tag ? tag->Tag : "Entity",
                                 managedError.empty() ? "unknown error" : managedError.c_str());
                    }
                }

                ManagedScriptHost::DestroyScriptInstance(managedScriptEntry->RuntimeInstanceId);
                managedScriptEntry->RuntimeInstanceId = 0;
                managedScriptEntry->RuntimeInitialized = false;
                managedScriptEntry->RuntimeUpdateCount = 0;
                managedScriptEntry->RuntimeWarnedOnUpdateTransformMutation = false;
                managedScriptEntry->RuntimeWarnedMissingHost = false;
                managedScriptEntry->RuntimeWarnedMissingClass = false;
            }
            SetRuntimePhase(RuntimePhase::Transform);
            UpdateTransforms();
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

        auto executeScriptUpdateSlot = [&](entt::entity entity, size_t scriptIndex) {
            NativeScriptEntry* scriptEntry = tryGetScriptEntry(entity, scriptIndex);
            if (!scriptEntry || !scriptEntry->RuntimeInstance)
                return;

            TransformComponent transformBeforeUpdate{};
            bool hadTransformBeforeUpdate = false;
            HierarchyComponent hierarchyBeforeUpdate{};
            bool hadHierarchyBeforeUpdate = false;
            Rigidbody2DComponent rigidbodyBeforeUpdate{};
            bool hadRigidbodyBeforeUpdate = false;
            BoxCollider2DComponent boxColliderBeforeUpdate{};
            bool hadBoxColliderBeforeUpdate = false;
            CircleCollider2DComponent circleColliderBeforeUpdate{};
            bool hadCircleColliderBeforeUpdate = false;
            PolygonCollider2DComponent polygonColliderBeforeUpdate{};
            bool hadPolygonColliderBeforeUpdate = false;
            EdgeCollider2DComponent edgeColliderBeforeUpdate{};
            bool hadEdgeColliderBeforeUpdate = false;
            CapsuleCollider2DComponent capsuleColliderBeforeUpdate{};
            bool hadCapsuleColliderBeforeUpdate = false;
            Joint2DComponent joint2DBeforeUpdate{};
            bool hadJoint2DBeforeUpdate = false;
            TagComponent tagBeforeUpdate{};
            bool hadTagBeforeUpdate = false;
            SpriteComponent spriteBeforeUpdate{};
            bool hadSpriteBeforeUpdate = false;
            MaterialComponent materialBeforeUpdate{};
            bool hadMaterialBeforeUpdate = false;
            CanvasComponent canvasBeforeUpdate{};
            bool hadCanvasBeforeUpdate = false;
            RectTransformComponent rectTransformBeforeUpdate{};
            bool hadRectTransformBeforeUpdate = false;
            UIImageComponent uiImageBeforeUpdate{};
            bool hadUIImageBeforeUpdate = false;
            UIPanelComponent uiPanelBeforeUpdate{};
            bool hadUIPanelBeforeUpdate = false;
            UITextComponent uiTextBeforeUpdate{};
            bool hadUITextBeforeUpdate = false;
            UIButtonComponent uiButtonBeforeUpdate{};
            bool hadUIButtonBeforeUpdate = false;
            UISliderComponent uiSliderBeforeUpdate{};
            bool hadUISliderBeforeUpdate = false;
            DirectionalLight2DComponent directionalLightBeforeUpdate{};
            bool hadDirectionalLightBeforeUpdate = false;
            PointLight2DComponent pointLightBeforeUpdate{};
            bool hadPointLightBeforeUpdate = false;
            ShadowOccluder2DComponent shadowOccluderBeforeUpdate{};
            bool hadShadowOccluderBeforeUpdate = false;
            AudioListener2DComponent audioListenerBeforeUpdate{};
            bool hadAudioListenerBeforeUpdate = false;
            AudioListener3DComponent audioListener3DBeforeUpdate{};
            bool hadAudioListener3DBeforeUpdate = false;
            AudioSourceComponent audioSourceBeforeUpdate{};
            bool hadAudioSourceBeforeUpdate = false;
            CameraComponent cameraBeforeUpdate{};
            bool hadCameraBeforeUpdate = false;
            PrefabInstanceComponent prefabInstanceBeforeUpdate{};
            bool hadPrefabInstanceBeforeUpdate = false;
            Grid2DComponent grid2DBeforeUpdate{};
            bool hadGrid2DBeforeUpdate = false;
            TilemapLayerValidationSnapshot tilemapLayerSnapshotBeforeUpdate{};
            bool hadTilemapLayerBeforeUpdate = false;
            AnimatorComponent animatorBeforeUpdate{};
            bool hadAnimatorBeforeUpdate = false;
            AnimationEventReceiverComponent animationEventReceiverBeforeUpdate{};
            bool hadAnimationEventReceiverBeforeUpdate = false;
            ParticleEmitterComponent particleEmitterBeforeUpdate{};
            bool hadParticleEmitterBeforeUpdate = false;
            bool trackTransformMutation = false;
            if (auto* transform = m_Registry.try_get<TransformComponent>(entity))
            {
                transformBeforeUpdate = *transform;
                hadTransformBeforeUpdate = true;
                if (const auto* rigidbody2D = m_Registry.try_get<Rigidbody2DComponent>(entity))
                {
                    trackTransformMutation = rigidbody2D->Type == Rigidbody2DComponent::BodyType::Dynamic ||
                                            rigidbody2D->Type == Rigidbody2DComponent::BodyType::Kinematic;
                }
            }
            const bool trackParallelAccessValidation =
                validateParallelScriptAccessMasks &&
                scriptEntry->ExecutionPolicy == ScriptExecutionPolicy::ParallelSafe;
            if (trackParallelAccessValidation)
            {
                if (auto* hierarchy = m_Registry.try_get<HierarchyComponent>(entity))
                {
                    hierarchyBeforeUpdate = *hierarchy;
                    hadHierarchyBeforeUpdate = true;
                }
                if (auto* rigidbody2D = m_Registry.try_get<Rigidbody2DComponent>(entity))
                {
                    rigidbodyBeforeUpdate = *rigidbody2D;
                    hadRigidbodyBeforeUpdate = true;
                }
                if (auto* boxCollider2D = m_Registry.try_get<BoxCollider2DComponent>(entity))
                {
                    boxColliderBeforeUpdate = *boxCollider2D;
                    hadBoxColliderBeforeUpdate = true;
                }
                if (auto* circleCollider2D = m_Registry.try_get<CircleCollider2DComponent>(entity))
                {
                    circleColliderBeforeUpdate = *circleCollider2D;
                    hadCircleColliderBeforeUpdate = true;
                }
                if (auto* polygonCollider2D = m_Registry.try_get<PolygonCollider2DComponent>(entity))
                {
                    polygonColliderBeforeUpdate = *polygonCollider2D;
                    hadPolygonColliderBeforeUpdate = true;
                }
                if (auto* edgeCollider2D = m_Registry.try_get<EdgeCollider2DComponent>(entity))
                {
                    edgeColliderBeforeUpdate = *edgeCollider2D;
                    hadEdgeColliderBeforeUpdate = true;
                }
                if (auto* capsuleCollider2D = m_Registry.try_get<CapsuleCollider2DComponent>(entity))
                {
                    capsuleColliderBeforeUpdate = *capsuleCollider2D;
                    hadCapsuleColliderBeforeUpdate = true;
                }
                if (auto* joint2D = m_Registry.try_get<Joint2DComponent>(entity))
                {
                    joint2DBeforeUpdate = *joint2D;
                    hadJoint2DBeforeUpdate = true;
                }
                if (auto* tag = m_Registry.try_get<TagComponent>(entity))
                {
                    tagBeforeUpdate = *tag;
                    hadTagBeforeUpdate = true;
                }
                if (auto* sprite = m_Registry.try_get<SpriteComponent>(entity))
                {
                    spriteBeforeUpdate = *sprite;
                    hadSpriteBeforeUpdate = true;
                }
                if (auto* material = m_Registry.try_get<MaterialComponent>(entity))
                {
                    materialBeforeUpdate = *material;
                    hadMaterialBeforeUpdate = true;
                }
                if (auto* canvas = m_Registry.try_get<CanvasComponent>(entity))
                {
                    canvasBeforeUpdate = *canvas;
                    hadCanvasBeforeUpdate = true;
                }
                if (auto* rectTransform = m_Registry.try_get<RectTransformComponent>(entity))
                {
                    rectTransformBeforeUpdate = *rectTransform;
                    hadRectTransformBeforeUpdate = true;
                }
                if (auto* uiImage = m_Registry.try_get<UIImageComponent>(entity))
                {
                    uiImageBeforeUpdate = *uiImage;
                    hadUIImageBeforeUpdate = true;
                }
                if (auto* uiPanel = m_Registry.try_get<UIPanelComponent>(entity))
                {
                    uiPanelBeforeUpdate = *uiPanel;
                    hadUIPanelBeforeUpdate = true;
                }
                if (auto* uiText = m_Registry.try_get<UITextComponent>(entity))
                {
                    uiTextBeforeUpdate = *uiText;
                    hadUITextBeforeUpdate = true;
                }
                if (auto* uiButton = m_Registry.try_get<UIButtonComponent>(entity))
                {
                    uiButtonBeforeUpdate = *uiButton;
                    hadUIButtonBeforeUpdate = true;
                }
                if (auto* uiSlider = m_Registry.try_get<UISliderComponent>(entity))
                {
                    uiSliderBeforeUpdate = *uiSlider;
                    hadUISliderBeforeUpdate = true;
                }
                if (auto* directionalLight = m_Registry.try_get<DirectionalLight2DComponent>(entity))
                {
                    directionalLightBeforeUpdate = *directionalLight;
                    hadDirectionalLightBeforeUpdate = true;
                }
                if (auto* pointLight = m_Registry.try_get<PointLight2DComponent>(entity))
                {
                    pointLightBeforeUpdate = *pointLight;
                    hadPointLightBeforeUpdate = true;
                }
                if (auto* shadowOccluder = m_Registry.try_get<ShadowOccluder2DComponent>(entity))
                {
                    shadowOccluderBeforeUpdate = *shadowOccluder;
                    hadShadowOccluderBeforeUpdate = true;
                }
                if (auto* audioListener = m_Registry.try_get<AudioListener2DComponent>(entity))
                {
                    audioListenerBeforeUpdate = *audioListener;
                    hadAudioListenerBeforeUpdate = true;
                }
                if (auto* audioListener3D = m_Registry.try_get<AudioListener3DComponent>(entity))
                {
                    audioListener3DBeforeUpdate = *audioListener3D;
                    hadAudioListener3DBeforeUpdate = true;
                }
                if (auto* audioSource = m_Registry.try_get<AudioSourceComponent>(entity))
                {
                    audioSourceBeforeUpdate = *audioSource;
                    hadAudioSourceBeforeUpdate = true;
                }
                if (auto* camera = m_Registry.try_get<CameraComponent>(entity))
                {
                    cameraBeforeUpdate = *camera;
                    hadCameraBeforeUpdate = true;
                }
                if (auto* prefabInstance = m_Registry.try_get<PrefabInstanceComponent>(entity))
                {
                    prefabInstanceBeforeUpdate = *prefabInstance;
                    hadPrefabInstanceBeforeUpdate = true;
                }
                if (auto* grid2D = m_Registry.try_get<Grid2DComponent>(entity))
                {
                    grid2DBeforeUpdate = *grid2D;
                    hadGrid2DBeforeUpdate = true;
                }
                if (auto* tilemapLayer = m_Registry.try_get<TilemapLayerComponent>(entity))
                {
                    tilemapLayerSnapshotBeforeUpdate = SnapshotTilemapLayerForValidation(*tilemapLayer);
                    hadTilemapLayerBeforeUpdate = true;
                }
                if (auto* animator = m_Registry.try_get<AnimatorComponent>(entity))
                {
                    animatorBeforeUpdate = *animator;
                    hadAnimatorBeforeUpdate = true;
                }
                if (auto* animationEventReceiver = m_Registry.try_get<AnimationEventReceiverComponent>(entity))
                {
                    animationEventReceiverBeforeUpdate = *animationEventReceiver;
                    hadAnimationEventReceiverBeforeUpdate = true;
                }
                if (auto* particleEmitter = m_Registry.try_get<ParticleEmitterComponent>(entity))
                {
                    particleEmitterBeforeUpdate = *particleEmitter;
                    hadParticleEmitterBeforeUpdate = true;
                }
            }

            try
            {
                executeWithDeferredEntityDestroy([&]() {
                    scriptEntry->RuntimeInstance->OnSynchronizeExposedFields();
                    scriptEntry->RuntimeInstance->MarkExposedPropertySyncComplete();
                    scriptEntry->RuntimeInstance->OnUpdate(deltaTime);
                    Coroutine::TickOwner(*scriptEntry->RuntimeInstance, deltaTime);
                    scriptEntry->RuntimeInstance->OnWriteBackExposedFields();
                    scriptEntry->RuntimeInstance->MarkExposedPropertySyncComplete();
                });
            }
            catch (const std::exception& exception)
            {
                handleScriptCallbackFailure(entity, scriptIndex, "OnUpdate", exception.what());
                return;
            }
            catch (...)
            {
                handleScriptCallbackFailure(entity, scriptIndex, "OnUpdate", "non-standard exception");
                return;
            }

            scriptEntry = tryGetScriptEntry(entity, scriptIndex);
            if (!scriptEntry || !scriptEntry->RuntimeInstance)
                return;
            ++scriptEntry->RuntimeUpdateCount;

            const auto* transformAfterUpdate = m_Registry.try_get<TransformComponent>(entity);
            bool transformChanged = false;
            if (hadTransformBeforeUpdate && transformAfterUpdate)
            {
                transformChanged = HasTransformChangedForAccessValidation(transformBeforeUpdate, *transformAfterUpdate);
                if (transformChanged)
                    MarkTransformDirty(entity);
            }
            else if (!hadTransformBeforeUpdate && transformAfterUpdate)
            {
                transformChanged = true;
                MarkTransformDirty(entity);
            }

            uint64_t observedWriteMask = 0;
            if (transformChanged)
                observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Transform);
            if (trackParallelAccessValidation)
            {
                const auto* hierarchyAfterUpdate = m_Registry.try_get<HierarchyComponent>(entity);
                const bool hasHierarchyAfterUpdate = hierarchyAfterUpdate != nullptr;
                if (hadHierarchyBeforeUpdate != hasHierarchyAfterUpdate ||
                    (hadHierarchyBeforeUpdate && hierarchyAfterUpdate &&
                     HasHierarchyChangedForAccessValidation(hierarchyBeforeUpdate, *hierarchyAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Hierarchy);
                }

                const auto* rigidbodyAfterUpdate = m_Registry.try_get<Rigidbody2DComponent>(entity);
                const bool hasRigidbodyAfterUpdate = rigidbodyAfterUpdate != nullptr;
                if (hadRigidbodyBeforeUpdate != hasRigidbodyAfterUpdate ||
                    (hadRigidbodyBeforeUpdate && rigidbodyAfterUpdate &&
                     HasRigidbodyChangedForAccessValidation(rigidbodyBeforeUpdate, *rigidbodyAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Rigidbody2D);
                }

                const auto* boxColliderAfterUpdate = m_Registry.try_get<BoxCollider2DComponent>(entity);
                const bool hasBoxColliderAfterUpdate = boxColliderAfterUpdate != nullptr;
                if (hadBoxColliderBeforeUpdate != hasBoxColliderAfterUpdate ||
                    (hadBoxColliderBeforeUpdate && boxColliderAfterUpdate &&
                     HasBoxColliderChangedForAccessValidation(boxColliderBeforeUpdate, *boxColliderAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::BoxCollider2D);
                }

                const auto* circleColliderAfterUpdate = m_Registry.try_get<CircleCollider2DComponent>(entity);
                const bool hasCircleColliderAfterUpdate = circleColliderAfterUpdate != nullptr;
                if (hadCircleColliderBeforeUpdate != hasCircleColliderAfterUpdate ||
                    (hadCircleColliderBeforeUpdate && circleColliderAfterUpdate &&
                     HasCircleColliderChangedForAccessValidation(circleColliderBeforeUpdate, *circleColliderAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::CircleCollider2D);
                }

                const auto* polygonColliderAfterUpdate = m_Registry.try_get<PolygonCollider2DComponent>(entity);
                const bool hasPolygonColliderAfterUpdate = polygonColliderAfterUpdate != nullptr;
                if (hadPolygonColliderBeforeUpdate != hasPolygonColliderAfterUpdate ||
                    (hadPolygonColliderBeforeUpdate && polygonColliderAfterUpdate &&
                     HasPolygonColliderChangedForAccessValidation(polygonColliderBeforeUpdate, *polygonColliderAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::PolygonCollider2D);
                }

                const auto* edgeColliderAfterUpdate = m_Registry.try_get<EdgeCollider2DComponent>(entity);
                const bool hasEdgeColliderAfterUpdate = edgeColliderAfterUpdate != nullptr;
                if (hadEdgeColliderBeforeUpdate != hasEdgeColliderAfterUpdate ||
                    (hadEdgeColliderBeforeUpdate && edgeColliderAfterUpdate &&
                     HasEdgeColliderChangedForAccessValidation(edgeColliderBeforeUpdate, *edgeColliderAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::EdgeCollider2D);
                }

                const auto* capsuleColliderAfterUpdate = m_Registry.try_get<CapsuleCollider2DComponent>(entity);
                const bool hasCapsuleColliderAfterUpdate = capsuleColliderAfterUpdate != nullptr;
                if (hadCapsuleColliderBeforeUpdate != hasCapsuleColliderAfterUpdate ||
                    (hadCapsuleColliderBeforeUpdate && capsuleColliderAfterUpdate &&
                     HasCapsuleColliderChangedForAccessValidation(capsuleColliderBeforeUpdate, *capsuleColliderAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::CapsuleCollider2D);
                }

                const auto* joint2DAfterUpdate = m_Registry.try_get<Joint2DComponent>(entity);
                const bool hasJoint2DAfterUpdate = joint2DAfterUpdate != nullptr;
                if (hadJoint2DBeforeUpdate != hasJoint2DAfterUpdate ||
                    (hadJoint2DBeforeUpdate && joint2DAfterUpdate &&
                     HasJoint2DChangedForAccessValidation(joint2DBeforeUpdate, *joint2DAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Joint2D);
                }

                const auto* tagAfterUpdate = m_Registry.try_get<TagComponent>(entity);
                const bool hasTagAfterUpdate = tagAfterUpdate != nullptr;
                if (hadTagBeforeUpdate != hasTagAfterUpdate ||
                    (hadTagBeforeUpdate && tagAfterUpdate &&
                     HasTagChangedForAccessValidation(tagBeforeUpdate, *tagAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Metadata);
                }

                const auto* spriteAfterUpdate = m_Registry.try_get<SpriteComponent>(entity);
                const bool hasSpriteAfterUpdate = spriteAfterUpdate != nullptr;
                if (hadSpriteBeforeUpdate != hasSpriteAfterUpdate ||
                    (hadSpriteBeforeUpdate && spriteAfterUpdate &&
                     HasSpriteChangedForAccessValidation(spriteBeforeUpdate, *spriteAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Rendering2D);
                }

                const auto* materialAfterUpdate = m_Registry.try_get<MaterialComponent>(entity);
                const bool hasMaterialAfterUpdate = materialAfterUpdate != nullptr;
                if (hadMaterialBeforeUpdate != hasMaterialAfterUpdate ||
                    (hadMaterialBeforeUpdate && materialAfterUpdate &&
                     HasMaterialChangedForAccessValidation(materialBeforeUpdate, *materialAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Rendering2D);
                }

                const auto* canvasAfterUpdate = m_Registry.try_get<CanvasComponent>(entity);
                const bool hasCanvasAfterUpdate = canvasAfterUpdate != nullptr;
                if (hadCanvasBeforeUpdate != hasCanvasAfterUpdate ||
                    (hadCanvasBeforeUpdate && canvasAfterUpdate &&
                     HasCanvasChangedForAccessValidation(canvasBeforeUpdate, *canvasAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::UI);
                }

                const auto* rectTransformAfterUpdate = m_Registry.try_get<RectTransformComponent>(entity);
                const bool hasRectTransformAfterUpdate = rectTransformAfterUpdate != nullptr;
                if (hadRectTransformBeforeUpdate != hasRectTransformAfterUpdate ||
                    (hadRectTransformBeforeUpdate && rectTransformAfterUpdate &&
                     HasRectTransformChangedForAccessValidation(rectTransformBeforeUpdate, *rectTransformAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::UI);
                }

                const auto* uiImageAfterUpdate = m_Registry.try_get<UIImageComponent>(entity);
                const bool hasUIImageAfterUpdate = uiImageAfterUpdate != nullptr;
                if (hadUIImageBeforeUpdate != hasUIImageAfterUpdate ||
                    (hadUIImageBeforeUpdate && uiImageAfterUpdate &&
                     HasUIImageChangedForAccessValidation(uiImageBeforeUpdate, *uiImageAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::UI);
                }

                const auto* uiPanelAfterUpdate = m_Registry.try_get<UIPanelComponent>(entity);
                const bool hasUIPanelAfterUpdate = uiPanelAfterUpdate != nullptr;
                if (hadUIPanelBeforeUpdate != hasUIPanelAfterUpdate ||
                    (hadUIPanelBeforeUpdate && uiPanelAfterUpdate &&
                     HasUIPanelChangedForAccessValidation(uiPanelBeforeUpdate, *uiPanelAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::UI);
                }

                const auto* uiTextAfterUpdate = m_Registry.try_get<UITextComponent>(entity);
                const bool hasUITextAfterUpdate = uiTextAfterUpdate != nullptr;
                if (hadUITextBeforeUpdate != hasUITextAfterUpdate ||
                    (hadUITextBeforeUpdate && uiTextAfterUpdate &&
                     HasUITextChangedForAccessValidation(uiTextBeforeUpdate, *uiTextAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::UI);
                }

                const auto* uiButtonAfterUpdate = m_Registry.try_get<UIButtonComponent>(entity);
                const bool hasUIButtonAfterUpdate = uiButtonAfterUpdate != nullptr;
                if (hadUIButtonBeforeUpdate != hasUIButtonAfterUpdate ||
                    (hadUIButtonBeforeUpdate && uiButtonAfterUpdate &&
                     HasUIButtonChangedForAccessValidation(uiButtonBeforeUpdate, *uiButtonAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::UI);
                }

                const auto* uiSliderAfterUpdate = m_Registry.try_get<UISliderComponent>(entity);
                const bool hasUISliderAfterUpdate = uiSliderAfterUpdate != nullptr;
                if (hadUISliderBeforeUpdate != hasUISliderAfterUpdate ||
                    (hadUISliderBeforeUpdate && uiSliderAfterUpdate &&
                     HasUISliderChangedForAccessValidation(uiSliderBeforeUpdate, *uiSliderAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::UI);
                }

                const auto* directionalLightAfterUpdate = m_Registry.try_get<DirectionalLight2DComponent>(entity);
                const bool hasDirectionalLightAfterUpdate = directionalLightAfterUpdate != nullptr;
                if (hadDirectionalLightBeforeUpdate != hasDirectionalLightAfterUpdate ||
                    (hadDirectionalLightBeforeUpdate && directionalLightAfterUpdate &&
                     HasDirectionalLight2DChangedForAccessValidation(directionalLightBeforeUpdate, *directionalLightAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Lighting2D);
                }

                const auto* pointLightAfterUpdate = m_Registry.try_get<PointLight2DComponent>(entity);
                const bool hasPointLightAfterUpdate = pointLightAfterUpdate != nullptr;
                if (hadPointLightBeforeUpdate != hasPointLightAfterUpdate ||
                    (hadPointLightBeforeUpdate && pointLightAfterUpdate &&
                     HasPointLight2DChangedForAccessValidation(pointLightBeforeUpdate, *pointLightAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Lighting2D);
                }

                const auto* shadowOccluderAfterUpdate = m_Registry.try_get<ShadowOccluder2DComponent>(entity);
                const bool hasShadowOccluderAfterUpdate = shadowOccluderAfterUpdate != nullptr;
                if (hadShadowOccluderBeforeUpdate != hasShadowOccluderAfterUpdate ||
                    (hadShadowOccluderBeforeUpdate && shadowOccluderAfterUpdate &&
                     HasShadowOccluder2DChangedForAccessValidation(shadowOccluderBeforeUpdate, *shadowOccluderAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Lighting2D);
                }

                const auto* audioListenerAfterUpdate = m_Registry.try_get<AudioListener2DComponent>(entity);
                const bool hasAudioListenerAfterUpdate = audioListenerAfterUpdate != nullptr;
                if (hadAudioListenerBeforeUpdate != hasAudioListenerAfterUpdate ||
                    (hadAudioListenerBeforeUpdate && audioListenerAfterUpdate &&
                     HasAudioListener2DChangedForAccessValidation(audioListenerBeforeUpdate, *audioListenerAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Audio);
                }

                const auto* audioListener3DAfterUpdate = m_Registry.try_get<AudioListener3DComponent>(entity);
                const bool hasAudioListener3DAfterUpdate = audioListener3DAfterUpdate != nullptr;
                if (hadAudioListener3DBeforeUpdate != hasAudioListener3DAfterUpdate ||
                    (hadAudioListener3DBeforeUpdate && audioListener3DAfterUpdate &&
                     HasAudioListener3DChangedForAccessValidation(audioListener3DBeforeUpdate, *audioListener3DAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Audio);
                }

                const auto* audioSourceAfterUpdate = m_Registry.try_get<AudioSourceComponent>(entity);
                const bool hasAudioSourceAfterUpdate = audioSourceAfterUpdate != nullptr;
                if (hadAudioSourceBeforeUpdate != hasAudioSourceAfterUpdate ||
                    (hadAudioSourceBeforeUpdate && audioSourceAfterUpdate &&
                     HasAudioSourceChangedForAccessValidation(audioSourceBeforeUpdate, *audioSourceAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Audio);
                }

                const auto* cameraAfterUpdate = m_Registry.try_get<CameraComponent>(entity);
                const bool hasCameraAfterUpdate = cameraAfterUpdate != nullptr;
                if (hadCameraBeforeUpdate != hasCameraAfterUpdate ||
                    (hadCameraBeforeUpdate && cameraAfterUpdate &&
                     HasCameraChangedForAccessValidation(cameraBeforeUpdate, *cameraAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Camera);
                }

                const auto* prefabInstanceAfterUpdate = m_Registry.try_get<PrefabInstanceComponent>(entity);
                const bool hasPrefabInstanceAfterUpdate = prefabInstanceAfterUpdate != nullptr;
                if (hadPrefabInstanceBeforeUpdate != hasPrefabInstanceAfterUpdate ||
                    (hadPrefabInstanceBeforeUpdate && prefabInstanceAfterUpdate &&
                     HasPrefabInstanceChangedForAccessValidation(prefabInstanceBeforeUpdate, *prefabInstanceAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Metadata);
                }

                const auto* grid2DAfterUpdate = m_Registry.try_get<Grid2DComponent>(entity);
                const bool hasGrid2DAfterUpdate = grid2DAfterUpdate != nullptr;
                if (hadGrid2DBeforeUpdate != hasGrid2DAfterUpdate ||
                    (hadGrid2DBeforeUpdate && grid2DAfterUpdate &&
                     HasGrid2DChangedForAccessValidation(grid2DBeforeUpdate, *grid2DAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Tilemap);
                }

                const auto* tilemapLayerAfterUpdate = m_Registry.try_get<TilemapLayerComponent>(entity);
                const bool hasTilemapLayerAfterUpdate = tilemapLayerAfterUpdate != nullptr;
                if (hadTilemapLayerBeforeUpdate != hasTilemapLayerAfterUpdate ||
                    (hadTilemapLayerBeforeUpdate && tilemapLayerAfterUpdate &&
                     HasTilemapLayerChangedFromSnapshot(tilemapLayerSnapshotBeforeUpdate, *tilemapLayerAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Tilemap);
                }

                const auto* animatorAfterUpdate = m_Registry.try_get<AnimatorComponent>(entity);
                const bool hasAnimatorAfterUpdate = animatorAfterUpdate != nullptr;
                if (hadAnimatorBeforeUpdate != hasAnimatorAfterUpdate ||
                    (hadAnimatorBeforeUpdate && animatorAfterUpdate &&
                     HasAnimatorChangedForAccessValidation(animatorBeforeUpdate, *animatorAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Animator);
                }

                const auto* animationEventReceiverAfterUpdate = m_Registry.try_get<AnimationEventReceiverComponent>(entity);
                const bool hasAnimationEventReceiverAfterUpdate = animationEventReceiverAfterUpdate != nullptr;
                if (hadAnimationEventReceiverBeforeUpdate != hasAnimationEventReceiverAfterUpdate ||
                    (hadAnimationEventReceiverBeforeUpdate && animationEventReceiverAfterUpdate &&
                     HasAnimationEventReceiverChangedForAccessValidation(animationEventReceiverBeforeUpdate, *animationEventReceiverAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Animator);
                }

                const auto* particleEmitterAfterUpdate = m_Registry.try_get<ParticleEmitterComponent>(entity);
                const bool hasParticleEmitterAfterUpdate = particleEmitterAfterUpdate != nullptr;
                if (hadParticleEmitterBeforeUpdate != hasParticleEmitterAfterUpdate ||
                    (hadParticleEmitterBeforeUpdate && particleEmitterAfterUpdate &&
                     HasParticleEmitterChangedForAccessValidation(particleEmitterBeforeUpdate, *particleEmitterAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::ParticleEmitter);
                }
            }

            validateParallelScriptAccessMask(entity, *scriptEntry, observedWriteMask, "OnUpdate");

            if (trackTransformMutation && !scriptEntry->RuntimeWarnedOnUpdateTransformMutation)
            {
                if (transformAfterUpdate && transformChanged)
                {
                    const auto* tag = m_Registry.try_get<TagComponent>(entity);
                    LT_WARN("Script '{}' on entity '{}' is mutating Transform in OnUpdate while Rigidbody2D is Dynamic/Kinematic. Use OnFixedUpdate and prefer Rigidbody2D velocity/force APIs (or move in FixedUpdate) to keep physics contacts stable.",
                            scriptEntry->ScriptClassName,
                            tag ? tag->Tag : "Entity");
                    scriptEntry->RuntimeWarnedOnUpdateTransformMutation = true;
                }
            }
        };

        // Two-phase runtime bootstrapping:
        // 1) Create all script instances for currently active slots.
        // 2) Invoke OnCreate/OnUpdate in slot order.
        // This lets scripts safely reference sibling scripts during OnCreate,
        // even when the referenced script appears later in the list.
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

            TransformComponent transformBeforeUpdate{};
            bool hadTransformBeforeUpdate = false;
            bool trackTransformMutation = false;
            if (auto* transform = m_Registry.try_get<TransformComponent>(entity))
            {
                transformBeforeUpdate = *transform;
                hadTransformBeforeUpdate = true;
                if (const auto* rigidbody2D = m_Registry.try_get<Rigidbody2DComponent>(entity))
                {
                    trackTransformMutation = rigidbody2D->Type == Rigidbody2DComponent::BodyType::Dynamic ||
                                            rigidbody2D->Type == Rigidbody2DComponent::BodyType::Kinematic;
                }
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
                if (!ManagedScriptHost::InvokeScriptOnUpdate(scriptEntry->RuntimeInstanceId, this, deltaTime, &managedError))
                {
                    handleManagedScriptCallbackFailure(entity, scriptIndex, "OnUpdate", managedError.empty() ? "unknown error" : managedError.c_str());
                }
            });

            scriptEntry = tryGetManagedEntry(entity, scriptIndex);
            if (!scriptEntry || scriptEntry->RuntimeInstanceId == 0)
                continue;

            if (!managedError.empty())
                continue;

            if (!readBackManagedScriptExposedProperties(entity, scriptIndex, "OnUpdate"))
                continue;

            scriptEntry = tryGetManagedEntry(entity, scriptIndex);
            if (!scriptEntry || scriptEntry->RuntimeInstanceId == 0)
                continue;

            const auto* transformAfterUpdate = m_Registry.try_get<TransformComponent>(entity);
            bool transformChanged = false;
            if (hadTransformBeforeUpdate && transformAfterUpdate)
            {
                transformChanged = HasTransformChangedForAccessValidation(transformBeforeUpdate, *transformAfterUpdate);
                if (transformChanged)
                    MarkTransformDirty(entity);
            }
            else if (!hadTransformBeforeUpdate && transformAfterUpdate)
            {
                transformChanged = true;
                MarkTransformDirty(entity);
            }

            if (trackTransformMutation && transformChanged && !scriptEntry->RuntimeWarnedOnUpdateTransformMutation)
            {
                const auto* tag = m_Registry.try_get<TagComponent>(entity);
                LT_WARN("Managed script '{}' on entity '{}' is mutating Transform in OnUpdate while Rigidbody2D is Dynamic/Kinematic. Prefer Rigidbody2D velocity/force APIs instead of Transform writes during physics-driven motion to keep physics contacts stable.",
                        scriptEntry->ScriptClassName,
                        tag ? tag->Tag : "Entity");
                scriptEntry->RuntimeWarnedOnUpdateTransformMutation = true;
            }

            ++scriptEntry->RuntimeUpdateCount;
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

            // Rebind runtime context every frame. NativeScriptEntry objects can move in memory
            // when the scripts vector grows/reorders, so cached pointers must be refreshed.
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

            executeScriptUpdateSlot(entity, scriptIndex);
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
                executeScriptUpdateSlot(slot.first, slot.second);
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

        // Dispatch UI component delegate events (ScriptEvent).
        {
            auto buttonView = m_Registry.view<UIButtonComponent>();
            for (entt::entity buttonEntity : buttonView)
            {
                auto& button = buttonView.get<UIButtonComponent>(buttonEntity);
                if (!IsEntityEnabledInHierarchy(buttonEntity))
                    continue;

                if (button.RuntimeClickedThisFrame && button.OnClicked.HasSubscribers())
                {
                    try
                    {
                        executeWithDeferredEntityDestroy([&]() { button.OnClicked.Invoke(); });
                    }
                    catch (const std::exception& exception)
                    {
                        const auto* tag = m_Registry.try_get<TagComponent>(buttonEntity);
                        LT_ERROR("UIButtonComponent.OnClicked on entity '{}' threw: {}", tag ? tag->Tag : "Entity", exception.what());
                    }
                    catch (...) {}
                }
                if (button.RuntimeHoverEnteredThisFrame && button.OnHoverEnter.HasSubscribers())
                {
                    try
                    {
                        executeWithDeferredEntityDestroy([&]() { button.OnHoverEnter.Invoke(); });
                    }
                    catch (const std::exception& exception)
                    {
                        const auto* tag = m_Registry.try_get<TagComponent>(buttonEntity);
                        LT_ERROR("UIButtonComponent.OnHoverEnter on entity '{}' threw: {}", tag ? tag->Tag : "Entity", exception.what());
                    }
                    catch (...) {}
                }
                if (button.RuntimeHoverExitedThisFrame && button.OnHoverExit.HasSubscribers())
                {
                    try
                    {
                        executeWithDeferredEntityDestroy([&]() { button.OnHoverExit.Invoke(); });
                    }
                    catch (const std::exception& exception)
                    {
                        const auto* tag = m_Registry.try_get<TagComponent>(buttonEntity);
                        LT_ERROR("UIButtonComponent.OnHoverExit on entity '{}' threw: {}", tag ? tag->Tag : "Entity", exception.what());
                    }
                    catch (...) {}
                }
                if (button.RuntimePressedThisFrame && button.OnPressed.HasSubscribers())
                {
                    try
                    {
                        executeWithDeferredEntityDestroy([&]() { button.OnPressed.Invoke(); });
                    }
                    catch (const std::exception& exception)
                    {
                        const auto* tag = m_Registry.try_get<TagComponent>(buttonEntity);
                        LT_ERROR("UIButtonComponent.OnPressed on entity '{}' threw: {}", tag ? tag->Tag : "Entity", exception.what());
                    }
                    catch (...) {}
                }

                /// Deprecated: Legacy shim: also broadcast OnUIButtonClicked to all scripts (deprecated).
                if (button.RuntimeClickedThisFrame)
                {
                    const Entity buttonWrapper(&m_Registry, buttonEntity);
                    for (const auto& scriptSlot : scriptSlots)
                    {
                        const entt::entity scriptEntity = scriptSlot.first;
                        const size_t scriptIndex = scriptSlot.second;
                        NativeScriptEntry* scriptEntry = tryGetScriptEntry(scriptEntity, scriptIndex);
                        if (!scriptEntry || !scriptEntry->RuntimeInstance || !scriptEntry->RuntimeInitialized)
                            continue;
                        if (!scriptEntry->Enabled || scriptEntry->ScriptClassName.empty() || !IsEntityEnabledInHierarchy(scriptEntity))
                            continue;

                        try
                        {
                            executeWithDeferredEntityDestroy([&]() {
                                scriptEntry->RuntimeInstance->DispatchUIButtonClicked(buttonWrapper);
                            });
                        }
                        catch (const std::exception& exception)
                        {
                            handleScriptCallbackFailure(scriptEntity, scriptIndex, "OnUIButtonClicked", exception.what());
                        }
                        catch (...)
                        {
                            handleScriptCallbackFailure(scriptEntity, scriptIndex, "OnUIButtonClicked", "non-standard exception");
                        }
                    }
                }
            }

            auto sliderView = m_Registry.view<UISliderComponent>();
            for (entt::entity sliderEntity : sliderView)
            {
                auto& slider = sliderView.get<UISliderComponent>(sliderEntity);
                if (!IsEntityEnabledInHierarchy(sliderEntity))
                    continue;

                if (slider.RuntimeValueChangedThisFrame && slider.OnValueChanged.HasSubscribers())
                {
                    try
                    {
                        executeWithDeferredEntityDestroy([&]() { slider.OnValueChanged.Invoke(slider.Value); });
                    }
                    catch (const std::exception& exception)
                    {
                        const auto* tag = m_Registry.try_get<TagComponent>(sliderEntity);
                        LT_ERROR("UISliderComponent.OnValueChanged on entity '{}' threw: {}", tag ? tag->Tag : "Entity", exception.what());
                    }
                    catch (...) {}
                }
            }
        }

        // Final frame writeback pass:
        // scripts can mutate fields on other scripts outside those scripts' own
        // callbacks (for example via direct method calls). Persist those field
        // values back to exposed properties before the next frame sync.
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

        runScheduledSimulationSystems();
        SetRuntimePhase(RuntimePhase::Transform);
        UpdateTransforms();
        SetRuntimePhase(RuntimePhase::Idle);
    }

}
