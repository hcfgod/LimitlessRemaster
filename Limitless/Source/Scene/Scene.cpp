#include "Scene/Scene.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetBundle.h"
#include "Assets/AssetManager.h"
#include "Assets/AssetPaths.h"
#include "Assets/AssetTypes.h"
#include "Assets/AssetUtils.h"
#include "Assets/AnimationClipAsset.h"
#include "Assets/AnimationClipAssetImporter.h"
#include "Assets/AnimatorControllerAsset.h"
#include "Assets/AnimatorControllerAssetImporter.h"
#include "Assets/MaterialAsset.h"
#include "Assets/MaterialAssetImporter.h"
#include "Assets/TileAsset.h"
#include "Assets/SpriteImportSettings.h"
#include "Assets/TextureAsset.h"
#include "Graphics/Camera/Camera.h"
#include "Graphics/Framebuffer.h"
#include "Graphics/Lighting2DRenderer.h"
#include "Graphics/RenderCommand.h"
#include "Graphics/Renderer.h"
#include "Graphics/Renderer2D.h"
#include "Core/Application.h"
#include "Core/Input/InputSystem.h"
#include "Core/Concurrency/AsyncIO.h"
#include "Core/Time.h"
#include "Platform/Window.h"
#include "Physics/Physics2DQueries.h"
#include "Physics/Physics2DWorld.h"
#include "Scene/ParticleEmitterSystem.h"
#include "Scripting/Coroutine.h"
#include "Scripting/NativeScriptRegistry.h"

#include <nlohmann/json.hpp>

#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>
#include <SDL3/SDL_mouse.h>
#include <algorithm>
#include <cmath>
#include <exception>
#include <fstream>
#include <limits>
#include <mutex>
#include <set>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace Limitless
{
    namespace
    {
        std::string GetUnqualifiedScriptClassName(std::string_view className)
        {
            const size_t separator = className.rfind("::");
            if (separator == std::string_view::npos)
                return std::string(className);
            return std::string(className.substr(separator + 2));
        }

        std::string ResolveRegisteredScriptClassName(const std::string& requestedClassName,
                                                     const std::string& scriptAssetRelativePath)
        {
            if (requestedClassName.empty())
                return {};
            if (NativeScriptRegistry::HasScript(requestedClassName))
                return requestedClassName;

            const auto registeredScriptNames = NativeScriptRegistry::GetRegisteredScriptNames();
            auto resolveByToken = [&](const std::string& classToken) -> std::string {
                if (classToken.empty())
                    return {};
                if (NativeScriptRegistry::HasScript(classToken))
                    return classToken;

                std::string matchedClassName;
                for (const std::string& candidate : registeredScriptNames)
                {
                    if (candidate == classToken || GetUnqualifiedScriptClassName(candidate) == classToken)
                    {
                        if (!matchedClassName.empty())
                            return {};
                        matchedClassName = candidate;
                    }
                }
                return matchedClassName;
            };

            if (const std::string fromRequested = resolveByToken(requestedClassName); !fromRequested.empty())
                return fromRequested;

            if (!scriptAssetRelativePath.empty())
            {
                const std::string stem = std::filesystem::path(scriptAssetRelativePath).stem().string();
                if (const std::string fromAssetPath = resolveByToken(stem); !fromAssetPath.empty())
                    return fromAssetPath;
            }

            if (registeredScriptNames.size() == 1)
                return registeredScriptNames.front();

            return {};
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
            }
            animator.RuntimeHasPosition = false;
            animator.RuntimeHasScale = false;
            animator.RuntimeHasRotationZ = false;
            animator.RuntimePosition = glm::vec3(0.0f);
            animator.RuntimeScale = glm::vec3(1.0f);
            animator.RuntimeRotationZDegrees = 0.0f;
        }

        bool TryResolveAnimatorControllerAsset(AnimatorComponent& animator)
        {
            if (animator.ControllerKey.empty())
            {
                animator.CachedController.reset();
                animator.ControllerLoadAttempted = false;
                return false;
            }

            if (animator.CachedController && animator.CachedController->GetKey() == animator.ControllerKey)
                return true;

            static std::mutex s_PendingAnimatorControllerLoadsMutex;
            static std::unordered_map<std::string, Async::Task<Assets::AnimatorControllerAsset::Ptr>> s_PendingAnimatorControllerLoads;

            animator.CachedController = std::dynamic_pointer_cast<Assets::AnimatorControllerAsset>(
                Assets::AssetManager::GetCachedByKey(animator.ControllerKey));
            if (!animator.CachedController)
            {
                std::lock_guard<std::mutex> lock(s_PendingAnimatorControllerLoadsMutex);
                auto pendingLoad = s_PendingAnimatorControllerLoads.find(animator.ControllerKey);
                if (pendingLoad == s_PendingAnimatorControllerLoads.end())
                {
                    s_PendingAnimatorControllerLoads.emplace(
                        animator.ControllerKey,
                        Assets::AssetManager::LoadAsync<Assets::AnimatorControllerAsset>(animator.ControllerKey));
                }
                else if (pendingLoad->second.IsDone())
                {
                    try
                    {
                        animator.CachedController = pendingLoad->second.Get();
                    }
                    catch (const std::exception& exception)
                    {
                        LT_WARN("Animator controller async load failed for '{}': {}",
                                animator.ControllerKey,
                                exception.what());
                        animator.CachedController.reset();
                    }
                    catch (...)
                    {
                        LT_WARN("Animator controller async load failed for '{}' with unknown error",
                                animator.ControllerKey);
                        animator.CachedController.reset();
                    }

                    s_PendingAnimatorControllerLoads.erase(pendingLoad);
                }
            }

            animator.ControllerLoadAttempted = true;
            return (animator.CachedController != nullptr);
        }

        bool TryResolveAnimationClipAssetByKey(const std::string& clipKey, Assets::AnimationClipAsset::Ptr& outClip)
        {
            outClip.reset();
            if (clipKey.empty())
                return false;

            outClip = std::dynamic_pointer_cast<Assets::AnimationClipAsset>(Assets::AssetManager::GetCachedByKey(clipKey));
            if (!outClip)
            {
                static std::mutex s_PendingAnimationClipLoadsMutex;
                static std::unordered_map<std::string, Async::Task<Assets::AnimationClipAsset::Ptr>> s_PendingAnimationClipLoads;

                std::lock_guard<std::mutex> lock(s_PendingAnimationClipLoadsMutex);
                auto pendingLoad = s_PendingAnimationClipLoads.find(clipKey);
                if (pendingLoad == s_PendingAnimationClipLoads.end())
                {
                    s_PendingAnimationClipLoads.emplace(
                        clipKey,
                        Assets::AssetManager::LoadAsync<Assets::AnimationClipAsset>(clipKey));
                }
                else if (pendingLoad->second.IsDone())
                {
                    try
                    {
                        outClip = pendingLoad->second.Get();
                    }
                    catch (const std::exception& exception)
                    {
                        LT_WARN("Animation clip async load failed for '{}': {}", clipKey, exception.what());
                        outClip.reset();
                    }
                    catch (...)
                    {
                        LT_WARN("Animation clip async load failed for '{}' with unknown error", clipKey);
                        outClip.reset();
                    }

                    s_PendingAnimationClipLoads.erase(pendingLoad);
                }
            }
            return (outClip != nullptr);
        }

        bool TryResolveDefaultClipAsset(AnimatorComponent& animator)
        {
            if (animator.DefaultClipKey.empty())
            {
                animator.CachedDefaultClip.reset();
                animator.DefaultClipLoadAttempted = false;
                return false;
            }

            if (animator.CachedDefaultClip && animator.CachedDefaultClip->GetKey() == animator.DefaultClipKey)
                return true;

            animator.CachedDefaultClip.reset();
            (void)TryResolveAnimationClipAssetByKey(animator.DefaultClipKey, animator.CachedDefaultClip);

            animator.DefaultClipLoadAttempted = true;
            return (animator.CachedDefaultClip != nullptr);
        }

        const Assets::AnimatorControllerAsset::StateDefinition* FindControllerState(
            const Assets::AnimatorControllerAsset::Data& controllerData,
            const std::string& stateName)
        {
            for (const auto& state : controllerData.States)
            {
                if (state.Name == stateName)
                    return &state;
            }
            return nullptr;
        }

        void EnsureAnimatorParametersFromControllerDefaults(
            AnimatorComponent& animator,
            const Assets::AnimatorControllerAsset::Data& controllerData)
        {
            for (const auto& parameter : controllerData.Parameters)
            {
                switch (parameter.Type)
                {
                    case Assets::AnimatorControllerAsset::ParameterType::Bool:
                    {
                        if (!animator.BoolParameters.contains(parameter.Name))
                            animator.BoolParameters[parameter.Name] = parameter.DefaultBool;
                        break;
                    }
                    case Assets::AnimatorControllerAsset::ParameterType::Float:
                    {
                        if (!animator.FloatParameters.contains(parameter.Name))
                            animator.FloatParameters[parameter.Name] = parameter.DefaultFloat;
                        break;
                    }
                    case Assets::AnimatorControllerAsset::ParameterType::Integer:
                    {
                        if (!animator.IntegerParameters.contains(parameter.Name))
                            animator.IntegerParameters[parameter.Name] = parameter.DefaultInteger;
                        break;
                    }
                    case Assets::AnimatorControllerAsset::ParameterType::Trigger:
                    {
                        if (!animator.TriggerParameters.contains(parameter.Name))
                            animator.TriggerParameters[parameter.Name] = false;
                        break;
                    }
                }
            }
        }

        bool EvaluateAnimatorTransitionConditions(
            const Assets::AnimatorControllerAsset::TransitionDefinition& transition,
            AnimatorComponent& animator,
            std::vector<std::string>& outTriggersToConsume)
        {
            outTriggersToConsume.clear();
            for (const auto& condition : transition.Conditions)
            {
                switch (condition.Mode)
                {
                    case Assets::AnimatorControllerAsset::ConditionMode::If:
                    {
                        const bool value = animator.GetBoolParameter(condition.ParameterName, false);
                        if (!value)
                            return false;
                        break;
                    }
                    case Assets::AnimatorControllerAsset::ConditionMode::IfNot:
                    {
                        const bool value = animator.GetBoolParameter(condition.ParameterName, false);
                        if (value)
                            return false;
                        break;
                    }
                    case Assets::AnimatorControllerAsset::ConditionMode::Greater:
                    {
                        const float value = animator.GetFloatParameter(condition.ParameterName, 0.0f);
                        if (!(value > condition.FloatThreshold))
                            return false;
                        break;
                    }
                    case Assets::AnimatorControllerAsset::ConditionMode::Less:
                    {
                        const float value = animator.GetFloatParameter(condition.ParameterName, 0.0f);
                        if (!(value < condition.FloatThreshold))
                            return false;
                        break;
                    }
                    case Assets::AnimatorControllerAsset::ConditionMode::Equals:
                    {
                        const int32_t value = animator.GetIntegerParameter(condition.ParameterName, 0);
                        if (value != condition.IntegerThreshold)
                            return false;
                        break;
                    }
                    case Assets::AnimatorControllerAsset::ConditionMode::NotEquals:
                    {
                        const int32_t value = animator.GetIntegerParameter(condition.ParameterName, 0);
                        if (value == condition.IntegerThreshold)
                            return false;
                        break;
                    }
                    case Assets::AnimatorControllerAsset::ConditionMode::Triggered:
                    {
                        const auto found = animator.TriggerParameters.find(condition.ParameterName);
                        if (found == animator.TriggerParameters.end() || !found->second)
                            return false;
                        outTriggersToConsume.push_back(condition.ParameterName);
                        break;
                    }
                }
            }

            return true;
        }

        template<typename TTrackKeyframe>
        const TTrackKeyframe* SampleStepTrackKeyframe(const std::vector<TTrackKeyframe>& track, float timeSeconds)
        {
            if (track.empty())
                return nullptr;
            const TTrackKeyframe* sampled = &track.front();
            for (const auto& keyframe : track)
            {
                if (keyframe.TimeSeconds <= timeSeconds)
                {
                    sampled = &keyframe;
                    continue;
                }
                break;
            }
            return sampled;
        }

        glm::vec3 SampleVector3Track(const std::vector<Assets::AnimationClipAsset::Vector3Keyframe>& track,
                                     float timeSeconds,
                                     bool& outHasSample)
        {
            outHasSample = false;
            if (track.empty())
                return glm::vec3(0.0f);

            outHasSample = true;
            if (track.size() == 1 || timeSeconds <= track.front().TimeSeconds)
                return track.front().Value;
            if (timeSeconds >= track.back().TimeSeconds)
                return track.back().Value;

            for (size_t index = 1; index < track.size(); ++index)
            {
                const auto& right = track[index];
                if (timeSeconds > right.TimeSeconds)
                    continue;

                const auto& left = track[index - 1];
                const float segmentLength = std::max(0.0001f, right.TimeSeconds - left.TimeSeconds);
                const float interpolation = std::clamp((timeSeconds - left.TimeSeconds) / segmentLength, 0.0f, 1.0f);
                if (left.Interpolation == Assets::AnimationClipAsset::InterpolationMode::Step)
                    return left.Value;
                return glm::mix(left.Value, right.Value, interpolation);
            }

            return track.back().Value;
        }

        float SampleFloatTrack(const std::vector<Assets::AnimationClipAsset::FloatKeyframe>& track,
                               float timeSeconds,
                               bool& outHasSample)
        {
            outHasSample = false;
            if (track.empty())
                return 0.0f;

            outHasSample = true;
            if (track.size() == 1 || timeSeconds <= track.front().TimeSeconds)
                return track.front().Value;
            if (timeSeconds >= track.back().TimeSeconds)
                return track.back().Value;

            for (size_t index = 1; index < track.size(); ++index)
            {
                const auto& right = track[index];
                if (timeSeconds > right.TimeSeconds)
                    continue;

                const auto& left = track[index - 1];
                const float segmentLength = std::max(0.0001f, right.TimeSeconds - left.TimeSeconds);
                const float interpolation = std::clamp((timeSeconds - left.TimeSeconds) / segmentLength, 0.0f, 1.0f);
                if (left.Interpolation == Assets::AnimationClipAsset::InterpolationMode::Step)
                    return left.Value;
                return glm::mix(left.Value, right.Value, interpolation);
            }

            return track.back().Value;
        }

        bool ShouldDispatchAnimationEventAtTime(float eventTimeSeconds,
                                                float previousTimeSeconds,
                                                float currentTimeSeconds,
                                                bool loopedThisFrame,
                                                float durationSeconds)
        {
            if (durationSeconds <= 0.0001f)
                return false;

            if (!loopedThisFrame)
            {
                if (currentTimeSeconds < previousTimeSeconds)
                    return false;
                if (previousTimeSeconds <= 0.0001f)
                    return eventTimeSeconds >= previousTimeSeconds && eventTimeSeconds <= currentTimeSeconds;
                return eventTimeSeconds > previousTimeSeconds && eventTimeSeconds <= currentTimeSeconds;
            }

            // Wrapped around clip end this frame.
            const bool inTail = eventTimeSeconds > previousTimeSeconds && eventTimeSeconds <= durationSeconds;
            const bool inHead = eventTimeSeconds >= 0.0f && eventTimeSeconds <= currentTimeSeconds;
            return inTail || inHead;
        }

        void UpdateAnimatorComponentForEntity(Scene& scene, entt::entity entity, float deltaTime, uint64_t dispatchFrame)
        {
            auto& registry = scene.GetRegistry();
            auto* animator = registry.try_get<AnimatorComponent>(entity);
            if (!animator)
                return;

            auto* eventReceiver = registry.try_get<AnimationEventReceiverComponent>(entity);
            if (eventReceiver)
            {
                eventReceiver->RuntimeDispatchedEvents.clear();
                eventReceiver->RuntimeDispatchFrame = dispatchFrame;
            }

            const std::string previousSpriteTextureOverrideKey = animator->RuntimeSpriteTextureOverrideKey;
            ResetAnimatorRuntimeOutput(*animator, false);

            if (!animator->Enabled || !scene.IsEntityEnabledInHierarchy(entity))
                return;

            const bool hasController = TryResolveAnimatorControllerAsset(*animator);
            const bool hasDefaultClip = TryResolveDefaultClipAsset(*animator);

            const Assets::AnimatorControllerAsset::Data* controllerData = nullptr;
            const Assets::AnimatorControllerAsset::StateDefinition* activeState = nullptr;
            if (hasController && animator->CachedController)
            {
                controllerData = &animator->CachedController->GetData();
                EnsureAnimatorParametersFromControllerDefaults(*animator, *controllerData);

                if (!animator->RuntimeInitialized)
                {
                    if (!controllerData->DefaultStateName.empty())
                        animator->RuntimeCurrentStateName = controllerData->DefaultStateName;
                    else if (!controllerData->States.empty())
                        animator->RuntimeCurrentStateName = controllerData->States.front().Name;
                    animator->RuntimeStateTimeSeconds = 0.0f;
                    animator->RuntimePreviousStateTimeSeconds = 0.0f;
                    animator->RuntimeInitialized = true;
                }

                activeState = FindControllerState(*controllerData, animator->RuntimeCurrentStateName);
                if (!activeState && !controllerData->States.empty())
                {
                    activeState = &controllerData->States.front();
                    animator->RuntimeCurrentStateName = activeState->Name;
                    animator->RuntimeStateTimeSeconds = 0.0f;
                    animator->RuntimePreviousStateTimeSeconds = 0.0f;
                }

                if (activeState)
                {
                    for (const auto& transition : activeState->Transitions)
                    {
                        if (!transition.CanTransitionToSelf && transition.ToState == activeState->Name)
                            continue;

                        if (transition.HasExitTime)
                        {
                            const float duration = std::max(0.0001f, animator->RuntimeCurrentStateDurationSeconds);
                            float normalizedTime = animator->RuntimeStateTimeSeconds / duration;
                            normalizedTime = std::clamp(normalizedTime, 0.0f, 1.0f);
                            if (normalizedTime < transition.ExitTimeNormalized)
                                continue;
                        }

                        std::vector<std::string> triggersToConsume;
                        if (!EvaluateAnimatorTransitionConditions(transition, *animator, triggersToConsume))
                            continue;

                        const auto* nextState = FindControllerState(*controllerData, transition.ToState);
                        if (!nextState)
                            continue;

                        for (const auto& triggerName : triggersToConsume)
                            animator->ResetTrigger(triggerName);

                        animator->RuntimeCurrentStateName = nextState->Name;
                        animator->RuntimeStateTimeSeconds = 0.0f;
                        animator->RuntimePreviousStateTimeSeconds = 0.0f;
                        activeState = nextState;
                        break;
                    }
                }
            }

            std::string resolvedClipKey;
            bool loopOverrideEnabled = false;
            bool loopOverrideValue = true;
            animator->RuntimeStateSpeedMultiplier = 1.0f;

            if (activeState)
            {
                resolvedClipKey = activeState->ClipKey;
                loopOverrideEnabled = activeState->LoopOverrideEnabled;
                loopOverrideValue = activeState->LoopOverride;
                animator->RuntimeStateSpeedMultiplier = std::max(0.0f, activeState->SpeedMultiplier);
            }

            if (resolvedClipKey.empty())
                resolvedClipKey = animator->DefaultClipKey;

            Assets::AnimationClipAsset::Ptr clipAsset;
            if (!resolvedClipKey.empty())
                (void)TryResolveAnimationClipAssetByKey(resolvedClipKey, clipAsset);
            if (!clipAsset && hasDefaultClip && animator->CachedDefaultClip)
                clipAsset = animator->CachedDefaultClip;

            if (!clipAsset)
            {
                animator->RuntimeCurrentClipKey.clear();
                animator->RuntimeCurrentStateDurationSeconds = 1.0f;
                return;
            }

            animator->RuntimeCurrentClipKey = clipAsset->GetKey();
            const auto& clipData = clipAsset->GetData();
            const float durationSeconds = std::max(0.0001f, clipData.DurationSeconds);
            animator->RuntimeCurrentStateDurationSeconds = durationSeconds;
            const bool clipLoops = loopOverrideEnabled ? loopOverrideValue : clipData.Loop;

            const float safeDeltaSeconds = std::max(0.0f, deltaTime);
            const float speed = std::max(0.0f, animator->PlaybackSpeed) * std::max(0.0f, animator->RuntimeStateSpeedMultiplier);

            const float previousTime = animator->RuntimeStateTimeSeconds;
            float currentTime = previousTime + safeDeltaSeconds * speed;
            bool loopedThisFrame = false;
            if (clipLoops)
            {
                if (currentTime >= durationSeconds)
                {
                    currentTime = std::fmod(currentTime, durationSeconds);
                    loopedThisFrame = true;
                }
            }
            else
            {
                currentTime = std::clamp(currentTime, 0.0f, durationSeconds);
            }

            animator->RuntimePreviousStateTimeSeconds = previousTime;
            animator->RuntimeStateTimeSeconds = currentTime;

            if (const auto* spriteSubRect = SampleStepTrackKeyframe(clipData.SpriteSubRectTrack, currentTime))
            {
                animator->RuntimeHasSpriteSubRect = true;
                animator->RuntimeSpriteUvMin = spriteSubRect->UvMin;
                animator->RuntimeSpriteUvMax = spriteSubRect->UvMax;
            }

            if (const auto* spriteTexture = SampleStepTrackKeyframe(clipData.SpriteTextureTrack, currentTime))
            {
                animator->RuntimeSpriteTextureOverrideKey = spriteTexture->TextureKey;
            }

            if (animator->RuntimeSpriteTextureOverrideKey != previousSpriteTextureOverrideKey)
            {
                animator->RuntimeCachedSpriteTextureOverride.reset();
                animator->RuntimeSpriteTextureOverrideLoadAttempted = false;
            }

            bool hasPositionSample = false;
            const glm::vec3 sampledPosition = SampleVector3Track(clipData.PositionTrack, currentTime, hasPositionSample);
            animator->RuntimeHasPosition = hasPositionSample;
            animator->RuntimePosition = sampledPosition;

            bool hasScaleSample = false;
            const glm::vec3 sampledScale = SampleVector3Track(clipData.ScaleTrack, currentTime, hasScaleSample);
            animator->RuntimeHasScale = hasScaleSample;
            animator->RuntimeScale = sampledScale;

            bool hasRotationSample = false;
            const float sampledRotation = SampleFloatTrack(clipData.RotationZTrack, currentTime, hasRotationSample);
            animator->RuntimeHasRotationZ = hasRotationSample;
            animator->RuntimeRotationZDegrees = sampledRotation;

            if (animator->ApplyToTransform)
            {
                if (auto* transform = registry.try_get<TransformComponent>(entity))
                {
                    bool transformChanged = false;
                    if (animator->RuntimeHasPosition)
                    {
                        transform->Position = animator->RuntimePosition;
                        transformChanged = true;
                    }
                    if (animator->RuntimeHasScale)
                    {
                        transform->Scale = animator->RuntimeScale;
                        transformChanged = true;
                    }
                    if (animator->RuntimeHasRotationZ)
                    {
                        transform->Rotation.z = animator->RuntimeRotationZDegrees;
                        transformChanged = true;
                    }
                    if (transformChanged)
                        scene.MarkTransformDirty(entity);
                }
            }

            if (eventReceiver && eventReceiver->Enabled && !clipData.EventTrack.empty())
            {
                const float normalizedTime = std::clamp(currentTime / durationSeconds, 0.0f, 1.0f);
                for (const auto& eventKeyframe : clipData.EventTrack)
                {
                    if (!ShouldDispatchAnimationEventAtTime(
                            eventKeyframe.TimeSeconds,
                            previousTime,
                            currentTime,
                            loopedThisFrame,
                            durationSeconds))
                    {
                        continue;
                    }

                    AnimationEventMessage message{};
                    message.Name = eventKeyframe.Name;
                    message.StringPayload = eventKeyframe.StringPayload;
                    message.FloatPayload = eventKeyframe.FloatPayload;
                    message.IntegerPayload = eventKeyframe.IntegerPayload;
                    message.BooleanPayload = eventKeyframe.BooleanPayload;
                    message.TimeSeconds = eventKeyframe.TimeSeconds;
                    message.NormalizedTime = normalizedTime;
                    eventReceiver->RuntimeDispatchedEvents.push_back(std::move(message));
                }
            }
        }

        void UpdateAnimation2DSystem(Scene& scene, float deltaTime, uint64_t dispatchFrame)
        {
            auto& registry = scene.GetRegistry();
            auto view = registry.view<AnimatorComponent>();
            for (entt::entity entity : view)
            {
                UpdateAnimatorComponentForEntity(scene, entity, deltaTime, dispatchFrame);
            }
        }

    }

    Scene::Scene()
        : m_DeferredStructuralMutationQueue(std::make_unique<Concurrency::LockFreeMPMCQueue<DeferredStructuralMutation, kDeferredStructuralMutationQueueSize>>())
        , m_DeferredStructuralMutationOverflowQueue(std::make_unique<Concurrency::LockFreeMPMCQueue<DeferredStructuralMutation, kDeferredStructuralMutationOverflowQueueSize>>())
    {
    }

    Scene::~Scene()
    {
        for (auto& physicsWorld : m_Physics2DWorlds)
        {
            if (physicsWorld)
                physicsWorld->Shutdown(*this);
        }

        auto view = m_Registry.view<NativeScriptComponent>();
        for (entt::entity entity : view)
        {
            (void)entity;
            auto& nativeScript = view.get<NativeScriptComponent>(entity);
            for (auto& scriptEntry : nativeScript.Scripts)
            {
                if (scriptEntry.RuntimeInstance)
                {
                    if (scriptEntry.RuntimeInitialized)
                        scriptEntry.RuntimeInstance->OnDestroy();
                    Coroutine::StopAll(*scriptEntry.RuntimeInstance);
                    scriptEntry.RuntimeInstance.reset();
                }
                scriptEntry.RuntimeInitialized = false;
                scriptEntry.RuntimeUpdateCount = 0;
                scriptEntry.RuntimeWarnedOnUpdateTransformMutation = false;
                scriptEntry.RuntimeWarnedMissingAccessDeclaration = false;
                scriptEntry.RuntimeWarnedAccessMaskMismatch = false;
            }
        }
    }

    std::string ResolveRegisteredScriptClassNameForSceneRuntime(const std::string& requestedClassName,
                                                                const std::string& scriptAssetRelativePath)
    {
        return ResolveRegisteredScriptClassName(requestedClassName, scriptAssetRelativePath);
    }

    void ProcessUiInteractionSystemForSceneRuntimeBridge(Scene& scene, uint32_t windowWidth, uint32_t windowHeight);

    void ProcessUiInteractionSystemForSceneRuntime(Scene& scene, uint32_t windowWidth, uint32_t windowHeight)
    {
        ProcessUiInteractionSystemForSceneRuntimeBridge(scene, windowWidth, windowHeight);
    }

    void UpdateAnimation2DSystemForSceneRuntime(Scene& scene, float deltaTime, uint64_t dispatchFrame)
    {
        UpdateAnimation2DSystem(scene, deltaTime, dispatchFrame);
    }

}