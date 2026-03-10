#include "Scene/Scene.h"
#include "Scene/SceneScriptRuntimeInternal.h"
#include "Scene/Components/PhysicsComponents.h"
#include "Scene/Components/RenderingComponents.h"

#include "Assets/AssetManager.h"
#include "Assets/AnimationClipAsset.h"
#include "Assets/AnimationClipAssetImporter.h"
#include "Assets/AnimatorControllerAsset.h"
#include "Assets/AnimatorControllerAssetImporter.h"
#include "Core/Concurrency/AsyncIO.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <mutex>
#include <unordered_map>
#include <vector>

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

            const bool inTail = eventTimeSeconds > previousTimeSeconds && eventTimeSeconds <= durationSeconds;
            const bool inHead = eventTimeSeconds >= 0.0f && eventTimeSeconds <= currentTimeSeconds;
            return inTail || inHead;
        }

        bool UndoAnimatorAppliedTransform(Scene& scene, entt::entity entity, AnimatorComponent& animator)
        {
            bool hadOffset = false;
            if (auto* transform = scene.GetRegistry().try_get<TransformComponent>(entity))
            {
                transform->Position -= animator.RuntimeAppliedPositionOffset;
                transform->Scale -= animator.RuntimeAppliedScaleOffset;
                transform->Rotation -= animator.RuntimeAppliedRotationOffset;
                hadOffset = animator.RuntimeAppliedPositionOffset != glm::vec3(0.0f) ||
                            animator.RuntimeAppliedScaleOffset != glm::vec3(0.0f) ||
                            animator.RuntimeAppliedRotationOffset != glm::vec3(0.0f);
            }

            animator.RuntimeAppliedPositionOffset = glm::vec3(0.0f);
            animator.RuntimeAppliedScaleOffset = glm::vec3(0.0f);
            animator.RuntimeAppliedRotationOffset = glm::vec3(0.0f);
            return hadOffset;
        }

        bool ApplyAnimatorTransformOffsets(Scene& scene,
                                           entt::entity entity,
                                           AnimatorComponent& animator,
                                           bool suppressPhysicsOwned2DChannels)
        {
            auto& registry = scene.GetRegistry();
            auto* transform = registry.try_get<TransformComponent>(entity);
            if (!transform)
                return false;

            glm::vec3 posOffset = animator.RuntimeHasPosition ? animator.RuntimePosition : glm::vec3(0.0f);
            const glm::vec3 scaleOffset = animator.RuntimeHasScale ? animator.RuntimeScale : glm::vec3(0.0f);
            glm::vec3 rotOffset = animator.RuntimeHasRotation ? animator.RuntimeRotation : glm::vec3(0.0f);

            if (suppressPhysicsOwned2DChannels)
            {
                const auto* rigidbody = registry.try_get<Rigidbody2DComponent>(entity);
                const bool physicsOwns2DPose = rigidbody && rigidbody->RuntimeBodyCreated;
                if (physicsOwns2DPose)
                {
                    posOffset.x = 0.0f;
                    posOffset.y = 0.0f;
                    rotOffset.z = 0.0f;
                }
            }

            transform->Position += posOffset;
            transform->Scale += scaleOffset;
            transform->Rotation += rotOffset;

            animator.RuntimeAppliedPositionOffset = posOffset;
            animator.RuntimeAppliedScaleOffset = scaleOffset;
            animator.RuntimeAppliedRotationOffset = rotOffset;

            return posOffset != glm::vec3(0.0f) || scaleOffset != glm::vec3(0.0f) || rotOffset != glm::vec3(0.0f);
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

            (void)UndoAnimatorAppliedTransform(scene, entity, *animator);

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
                animator->RuntimeSpriteTextureOverrideKey = spriteTexture->TextureKey;

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
            const glm::vec3 sampledRotation = SampleVector3Track(clipData.RotationTrack, currentTime, hasRotationSample);
            animator->RuntimeHasRotation = hasRotationSample;
            animator->RuntimeRotation = sampledRotation;

            if (animator->ApplyToTransform && ApplyAnimatorTransformOffsets(scene, entity, *animator, true))
                scene.MarkTransformDirty(entity);

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
    }

    void UpdateAnimation2DSystemForSceneRuntime(Scene& scene, float deltaTime, uint64_t dispatchFrame)
    {
        auto view = scene.GetRegistry().view<AnimatorComponent>();
        for (entt::entity entity : view)
            UpdateAnimatorComponentForEntity(scene, entity, deltaTime, dispatchFrame);
    }

    bool Scene::PreviewAnimationClipOnEntity(entt::entity entity, const std::string& clipAssetKey, float previewTimeSeconds)
    {
        if (!IsValid(entity) || clipAssetKey.empty())
            return false;

        auto& registry = GetRegistry();
        auto* animator = registry.try_get<AnimatorComponent>(entity);
        if (!animator)
            return false;

        const bool hadOffset = UndoAnimatorAppliedTransform(*this, entity, *animator);

        auto clipAsset = std::dynamic_pointer_cast<Assets::AnimationClipAsset>(
            Assets::AssetManager::GetCachedByKey(clipAssetKey));
        if (!clipAsset)
            clipAsset = Assets::AnimationClipAsset::LoadBlocking(clipAssetKey);
        if (!clipAsset)
            return false;

        const auto& clipData = clipAsset->GetData();
        const float durationSeconds = std::max(0.0001f, clipData.DurationSeconds);
        const float currentTime = std::clamp(previewTimeSeconds, 0.0f, durationSeconds);

        const std::string previousSpriteTextureOverrideKey = animator->RuntimeSpriteTextureOverrideKey;
        ResetAnimatorRuntimeOutput(*animator, false);

        animator->RuntimeCurrentClipKey = clipAssetKey;
        animator->RuntimeCurrentStateDurationSeconds = durationSeconds;
        animator->RuntimePreviousStateTimeSeconds = currentTime;
        animator->RuntimeStateTimeSeconds = currentTime;
        animator->RuntimeStateSpeedMultiplier = 1.0f;

        if (const auto* spriteSubRect = SampleStepTrackKeyframe(clipData.SpriteSubRectTrack, currentTime))
        {
            animator->RuntimeHasSpriteSubRect = true;
            animator->RuntimeSpriteUvMin = spriteSubRect->UvMin;
            animator->RuntimeSpriteUvMax = spriteSubRect->UvMax;
        }

        if (const auto* spriteTexture = SampleStepTrackKeyframe(clipData.SpriteTextureTrack, currentTime))
            animator->RuntimeSpriteTextureOverrideKey = spriteTexture->TextureKey;

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
        const glm::vec3 sampledRotation = SampleVector3Track(clipData.RotationTrack, currentTime, hasRotationSample);
        animator->RuntimeHasRotation = hasRotationSample;
        animator->RuntimeRotation = sampledRotation;

        const bool appliedOffset = animator->ApplyToTransform && ApplyAnimatorTransformOffsets(*this, entity, *animator, false);
        if (hadOffset || appliedOffset)
            MarkTransformDirty(entity);

        return true;
    }

    void Scene::ClearAnimationPreviewOnAllEntities()
    {
        auto& registry = GetRegistry();
        auto view = registry.view<AnimatorComponent>();
        for (entt::entity entity : view)
        {
            auto* animator = registry.try_get<AnimatorComponent>(entity);
            if (!animator)
                continue;

            const bool hadOffset = UndoAnimatorAppliedTransform(*this, entity, *animator);
            ResetAnimatorRuntimeOutput(*animator, false);
            animator->RuntimeCurrentClipKey.clear();
            animator->RuntimePreviousStateTimeSeconds = 0.0f;
            animator->RuntimeStateTimeSeconds = 0.0f;
            animator->RuntimeCurrentStateDurationSeconds = 1.0f;
            animator->RuntimeStateSpeedMultiplier = 1.0f;

            if (hadOffset)
                MarkTransformDirty(entity);
        }
    }
}
