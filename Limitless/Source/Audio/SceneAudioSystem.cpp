#include "Audio/SceneAudioSystem.h"

#include "Assets/AudioClipAsset.h"
#include "Audio/AudioEngine.h"
#include "Scene/Components/AudioComponents.h"
#include "Scene/Components/CoreComponents.h"
#include "Scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace Limitless::Audio
{
    namespace
    {
        constexpr float kAudioMinimumDistance = 0.001f;
        constexpr float kAudioMinimumPitch = 0.01f;
        constexpr float kMinimumDeltaTime = 0.000001f;
        constexpr float kSpeedOfSoundMetersPerSecond = 343.0f;

        std::unordered_map<std::string, Async::Task<Assets::AudioClipAsset::Ptr>>& GetPendingAudioClipLoads()
        {
            static std::unordered_map<std::string, Async::Task<Assets::AudioClipAsset::Ptr>> pendingAudioClipLoads;
            return pendingAudioClipLoads;
        }

        glm::vec3 ComputeEntityWorldPosition3D(const Scene& scene, entt::entity entity)
        {
            const glm::mat4 worldTransform = scene.GetWorldTransformMatrix(entity);
            return glm::vec3(worldTransform[3][0], worldTransform[3][1], worldTransform[3][2]);
        }

        glm::vec2 ComputeEntityWorldPosition2D(const Scene& scene, entt::entity entity)
        {
            const glm::vec3 worldPosition = ComputeEntityWorldPosition3D(scene, entity);
            return glm::vec2(worldPosition.x, worldPosition.y);
        }

        glm::vec3 SafeNormalize(const glm::vec3& value, const glm::vec3& fallback)
        {
            const float lengthSquared = glm::dot(value, value);
            if (lengthSquared <= 0.000001f)
                return fallback;
            return value / std::sqrt(lengthSquared);
        }

        glm::vec3 ComputeEntityWorldForward(const Scene& scene, entt::entity entity)
        {
            const glm::mat4 worldTransform = scene.GetWorldTransformMatrix(entity);
            const glm::vec3 forward = -glm::vec3(worldTransform[2][0], worldTransform[2][1], worldTransform[2][2]);
            return SafeNormalize(forward, glm::vec3(0.0f, 0.0f, -1.0f));
        }

        glm::vec3 ComputeListenerRight(const glm::vec3& listenerForward)
        {
            constexpr glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
            glm::vec3 right = glm::cross(listenerForward, worldUp);
            if (glm::dot(right, right) <= 0.000001f)
                right = glm::vec3(1.0f, 0.0f, 0.0f);
            return SafeNormalize(right, glm::vec3(1.0f, 0.0f, 0.0f));
        }

        bool TryFindPrimaryCameraEntity(const Scene& scene, entt::entity& outEntity)
        {
            const auto& registry = scene.GetRegistry();
            auto cameraView = registry.view<CameraComponent>();
            entt::entity fallbackEntity = entt::null;
            for (entt::entity entity : cameraView)
            {
                if (fallbackEntity == entt::null)
                    fallbackEntity = entity;

                const auto& camera = cameraView.get<CameraComponent>(entity);
                if (camera.IsPrimary)
                {
                    outEntity = entity;
                    return true;
                }
            }

            if (fallbackEntity != entt::null)
            {
                outEntity = fallbackEntity;
                return true;
            }

            return false;
        }

        struct AudioListener2DRuntimeState
        {
            bool HasListener = false;
            glm::vec2 Position = glm::vec2(0.0f);
        };

        AudioListener2DRuntimeState ResolveNearestListener(const AudioListenerPositions2D& listeners,
                                                           const glm::vec2& sourcePosition)
        {
            AudioListener2DRuntimeState state{};
            if (listeners.empty())
                return state;

            state.HasListener = true;
            state.Position = listeners.front();
            float bestDistanceSq = glm::dot(sourcePosition - state.Position, sourcePosition - state.Position);
            for (size_t i = 1; i < listeners.size(); ++i)
            {
                const glm::vec2& pos = listeners[i];
                const float distanceSq = glm::dot(sourcePosition - pos, sourcePosition - pos);
                if (distanceSq < bestDistanceSq)
                {
                    bestDistanceSq = distanceSq;
                    state.Position = pos;
                }
            }

            return state;
        }

        AudioListenerState3D ResolveNearestListener(const AudioListenerStates3D& listeners,
                                                    const glm::vec3& sourcePosition)
        {
            AudioListenerState3D state{};
            if (listeners.empty())
                return state;

            state = listeners.front();
            float bestDistanceSq = glm::dot(sourcePosition - state.Position, sourcePosition - state.Position);
            for (size_t i = 1; i < listeners.size(); ++i)
            {
                const AudioListenerState3D& candidate = listeners[i];
                const float distanceSq = glm::dot(sourcePosition - candidate.Position, sourcePosition - candidate.Position);
                if (distanceSq < bestDistanceSq)
                {
                    bestDistanceSq = distanceSq;
                    state = candidate;
                }
            }

            return state;
        }

        float ComputeDistanceAttenuation(const AudioSourceComponent& audioSource, float distanceToListener)
        {
            const float minDistance = std::max(kAudioMinimumDistance, audioSource.SpatialMinDistance);
            const float maxDistance = std::max(minDistance, audioSource.SpatialMaxDistance);
            const float rolloffExponent = std::max(0.01f, audioSource.SpatialRolloffExponent);

            if (distanceToListener <= minDistance)
                return 1.0f;

            switch (audioSource.SpatialRolloffMode)
            {
                case AudioSourceComponent::RolloffMode::Linear:
                {
                    if (distanceToListener >= maxDistance)
                        return 0.0f;
                    const float normalized = (distanceToListener - minDistance) / std::max(kAudioMinimumDistance, maxDistance - minDistance);
                    return std::pow(std::clamp(1.0f - normalized, 0.0f, 1.0f), rolloffExponent);
                }
                case AudioSourceComponent::RolloffMode::Inverse:
                {
                    if (distanceToListener >= maxDistance)
                        return 0.0f;
                    const float clampedDistance = std::clamp(distanceToListener, minDistance, maxDistance);
                    return std::pow(minDistance / clampedDistance, rolloffExponent);
                }
                case AudioSourceComponent::RolloffMode::SmoothStep:
                default:
                {
                    if (distanceToListener >= maxDistance)
                        return 0.0f;
                    const float normalized = std::clamp((distanceToListener - minDistance) / std::max(kAudioMinimumDistance, maxDistance - minDistance), 0.0f, 1.0f);
                    const float smooth = 1.0f - (normalized * normalized * (3.0f - 2.0f * normalized));
                    return std::pow(std::max(0.0f, smooth), rolloffExponent);
                }
            }
        }

        float ComputeDirectionalAttenuation(const AudioSourceComponent& audioSource,
                                            const glm::vec3& sourcePosition,
                                            const glm::vec3& sourceForward,
                                            const glm::vec3& listenerPosition)
        {
            if (!audioSource.EnableDirectionalAttenuation)
                return 1.0f;

            const glm::vec3 toListener = listenerPosition - sourcePosition;
            if (glm::dot(toListener, toListener) <= 0.000001f)
                return 1.0f;

            const glm::vec3 sourceFacing = SafeNormalize(sourceForward, glm::vec3(0.0f, 0.0f, -1.0f));
            const glm::vec3 listenerDirection = SafeNormalize(toListener, glm::vec3(0.0f, 0.0f, -1.0f));
            const float cosine = std::clamp(glm::dot(sourceFacing, listenerDirection), -1.0f, 1.0f);
            const float angleDegrees = glm::degrees(std::acos(cosine));

            const float innerHalfAngle = std::clamp(audioSource.DirectionalInnerAngleDegrees * 0.5f, 0.0f, 180.0f);
            const float outerHalfAngle = std::clamp(std::max(audioSource.DirectionalInnerAngleDegrees, audioSource.DirectionalOuterAngleDegrees) * 0.5f, innerHalfAngle, 180.0f);
            const float outerVolume = std::clamp(audioSource.DirectionalOuterVolume, 0.0f, 1.0f);
            if (angleDegrees <= innerHalfAngle)
                return 1.0f;
            if (angleDegrees >= outerHalfAngle)
                return outerVolume;

            const float t = (angleDegrees - innerHalfAngle) / std::max(0.001f, outerHalfAngle - innerHalfAngle);
            return std::clamp(1.0f + (outerVolume - 1.0f) * t, outerVolume, 1.0f);
        }

        float ComputeDopplerPitchMultiplier(const AudioSourceComponent& audioSource,
                                            const glm::vec3& sourcePosition,
                                            const glm::vec3& sourceVelocity,
                                            const AudioListenerState3D& listenerState)
        {
            const float dopplerFactor = std::max(0.0f, audioSource.DopplerFactor);
            if (dopplerFactor <= 0.0f)
                return 1.0f;

            const glm::vec3 listenerToSource = sourcePosition - listenerState.Position;
            if (glm::dot(listenerToSource, listenerToSource) <= 0.000001f)
                return 1.0f;

            const glm::vec3 direction = SafeNormalize(listenerToSource, glm::vec3(0.0f, 0.0f, -1.0f));
            const float listenerTowardSource = glm::dot(listenerState.Velocity, direction) * dopplerFactor;
            const float sourceTowardListener = glm::dot(sourceVelocity, -direction) * dopplerFactor;
            const float numerator = std::max(1.0f, kSpeedOfSoundMetersPerSecond + listenerTowardSource);
            const float denominator = std::max(1.0f, kSpeedOfSoundMetersPerSecond - sourceTowardListener);
            return std::clamp(numerator / denominator, 0.5f, 2.0f);
        }
    }

    AudioListenerPositions2D CollectAudioListenerPositions2D(const Scene& scene)
    {
        AudioListenerPositions2D listenerPositions;
        const auto& registry = scene.GetRegistry();

        auto listenerView = registry.view<AudioListener2DComponent>();
        for (entt::entity entity : listenerView)
        {
            if (!scene.IsEntityEnabledInHierarchy(entity))
                continue;

            const auto& listener = listenerView.get<AudioListener2DComponent>(entity);
            if (!listener.Enabled)
                continue;

            if (listener.UsePrimaryCameraPosition)
            {
                entt::entity cameraEntity = entt::null;
                if (TryFindPrimaryCameraEntity(scene, cameraEntity))
                {
                    listenerPositions.push_back(ComputeEntityWorldPosition2D(scene, cameraEntity));
                    continue;
                }
            }

            listenerPositions.push_back(ComputeEntityWorldPosition2D(scene, entity));
        }

        if (listenerPositions.empty())
        {
            entt::entity fallbackCameraEntity = entt::null;
            if (TryFindPrimaryCameraEntity(scene, fallbackCameraEntity))
                listenerPositions.push_back(ComputeEntityWorldPosition2D(scene, fallbackCameraEntity));
        }

        return listenerPositions;
    }

    AudioListenerStates3D CollectAudioListenerStates3D(Scene& scene, float deltaTime)
    {
        AudioListenerStates3D listenerStates;
        auto& registry = scene.GetRegistry();

        auto listenerView = registry.view<AudioListener3DComponent>();
        for (entt::entity entity : listenerView)
        {
            if (!scene.IsEntityEnabledInHierarchy(entity))
                continue;

            auto& listener = listenerView.get<AudioListener3DComponent>(entity);
            if (!listener.Enabled)
                continue;

            entt::entity transformEntity = entity;
            if (listener.UsePrimaryCameraTransform)
            {
                entt::entity cameraEntity = entt::null;
                if (TryFindPrimaryCameraEntity(scene, cameraEntity))
                    transformEntity = cameraEntity;
            }

            AudioListenerState3D state{};
            state.HasListener = true;
            state.Position = ComputeEntityWorldPosition3D(scene, transformEntity);
            state.Forward = ComputeEntityWorldForward(scene, transformEntity);
            if (deltaTime > kMinimumDeltaTime && listener.RuntimeHasPreviousWorldPosition)
                state.Velocity = (state.Position - listener.RuntimePreviousWorldPosition) / deltaTime;
            listener.RuntimePreviousWorldPosition = state.Position;
            listener.RuntimeHasPreviousWorldPosition = true;
            listenerStates.push_back(state);
        }

        if (listenerStates.empty())
        {
            entt::entity fallbackCameraEntity = entt::null;
            if (TryFindPrimaryCameraEntity(scene, fallbackCameraEntity))
            {
                AudioListenerState3D fallbackState{};
                fallbackState.HasListener = true;
                fallbackState.Position = ComputeEntityWorldPosition3D(scene, fallbackCameraEntity);
                fallbackState.Forward = ComputeEntityWorldForward(scene, fallbackCameraEntity);
                listenerStates.push_back(fallbackState);
            }
        }

        return listenerStates;
    }

    AudioSpatialMix2D ComputeAudioSpatialMix2D(const AudioSourceComponent& audioSource,
                                                const glm::vec2& sourcePosition,
                                                const AudioListenerPositions2D& listeners)
    {
        AudioSpatialMix2D result{};
        if (audioSource.Space != AudioSourceComponent::PlaybackSpace::Spatial2D || listeners.empty())
            return result;

        const AudioListener2DRuntimeState listenerState = ResolveNearestListener(listeners, sourcePosition);

        const float panStrength = std::clamp(audioSource.StereoPanStrength, 0.0f, 1.0f);

        const float distanceToListener = glm::length(sourcePosition - listenerState.Position);
        const float maxDistance = std::max(std::max(kAudioMinimumDistance, audioSource.SpatialMinDistance), audioSource.SpatialMaxDistance);
        result.Gain = ComputeDistanceAttenuation(audioSource, distanceToListener);
        const float panNormalizationDistance = std::max(maxDistance, 0.001f);
        const float signedPan = std::clamp((sourcePosition.x - listenerState.Position.x) / panNormalizationDistance, -1.0f, 1.0f);
        result.Pan = signedPan * panStrength;
        return result;
    }

    AudioSpatialMix3D ComputeAudioSpatialMix3D(const AudioSourceComponent& audioSource,
                                                const glm::vec3& sourcePosition,
                                                const glm::vec3& sourceForward,
                                                const glm::vec3& sourceVelocity,
                                                const AudioListenerStates3D& listeners)
    {
        AudioSpatialMix3D result{};
        if (audioSource.Space != AudioSourceComponent::PlaybackSpace::Spatial3D || listeners.empty())
            return result;

        const AudioListenerState3D listenerState = ResolveNearestListener(listeners, sourcePosition);
        const glm::vec3 listenerToSource = sourcePosition - listenerState.Position;
        const float distanceToListener = std::sqrt(glm::dot(listenerToSource, listenerToSource));
        result.Gain = ComputeDistanceAttenuation(audioSource, distanceToListener);
        result.Gain *= ComputeDirectionalAttenuation(audioSource, sourcePosition, sourceForward, listenerState.Position);

        if (distanceToListener > 0.000001f)
        {
            const glm::vec3 relativeDirection = listenerToSource / distanceToListener;
            const glm::vec3 listenerRight = ComputeListenerRight(listenerState.Forward);
            const float rawPan = glm::dot(relativeDirection, listenerRight);
            result.Pan = std::clamp(rawPan * std::clamp(audioSource.StereoPanStrength, 0.0f, 1.0f), -1.0f, 1.0f);
        }

        result.PitchMultiplier = ComputeDopplerPitchMultiplier(audioSource, sourcePosition, sourceVelocity, listenerState);
        return result;
    }

    void UpdateSceneAudioSources(Scene* scene, float deltaTime, bool runtimePlaybackAllowed)
    {
        if (!scene)
            return;

        auto& registry = scene->GetRegistry();
        const AudioListenerPositions2D listenerPositions = CollectAudioListenerPositions2D(*scene);
        const AudioListenerStates3D listenerStates3D = CollectAudioListenerStates3D(*scene, std::max(0.0f, deltaTime));
        auto audioView = registry.view<AudioSourceComponent>();
        for (entt::entity entity : audioView)
        {
            auto& audioSource = audioView.get<AudioSourceComponent>(entity);
            const bool entityEnabled = scene->IsEntityEnabledInHierarchy(entity);
            if (audioSource.RuntimeVoiceId != 0 &&
                !AudioEngine::GetInstance().IsVoiceActive(audioSource.RuntimeVoiceId))
            {
                audioSource.RuntimeVoiceId = 0;
                audioSource.RuntimePlaybackStarted = false;
                audioSource.RuntimePlayRequested = false;
            }

            if (!entityEnabled)
            {
                if (audioSource.RuntimeVoiceId != 0)
                    AudioEngine::GetInstance().Stop(audioSource.RuntimeVoiceId);
                audioSource.RuntimeVoiceId = 0;
                audioSource.RuntimePlaybackStarted = false;
                audioSource.RuntimePlayRequested = false;
                audioSource.RuntimePlayOnStartConsumed = false;
                audioSource.RuntimeHasPreviousWorldPosition = false;
                audioSource.RuntimePreviousWorldPosition = glm::vec3(0.0f);
                continue;
            }

            const glm::vec3 sourcePosition3D = ComputeEntityWorldPosition3D(*scene, entity);
            glm::vec3 sourceVelocity(0.0f);
            if (deltaTime > kMinimumDeltaTime && audioSource.RuntimeHasPreviousWorldPosition)
                sourceVelocity = (sourcePosition3D - audioSource.RuntimePreviousWorldPosition) / deltaTime;
            audioSource.RuntimePreviousWorldPosition = sourcePosition3D;
            audioSource.RuntimeHasPreviousWorldPosition = true;

            if (!runtimePlaybackAllowed)
                continue;

            const glm::vec2 sourcePosition2D(sourcePosition3D.x, sourcePosition3D.y);
            const float authoredVolume = audioSource.Muted ? 0.0f : std::max(0.0f, audioSource.Volume);
            float runtimeVolume = authoredVolume;
            float runtimePan = 0.0f;
            float runtimePitch = std::max(kAudioMinimumPitch, audioSource.Pitch);

            if (audioSource.Space == AudioSourceComponent::PlaybackSpace::Spatial2D)
            {
                const AudioSpatialMix2D spatialMix2D = ComputeAudioSpatialMix2D(audioSource, sourcePosition2D, listenerPositions);
                runtimeVolume *= spatialMix2D.Gain;
                runtimePan = spatialMix2D.Pan;
            }
            else if (audioSource.Space == AudioSourceComponent::PlaybackSpace::Spatial3D)
            {
                const AudioSpatialMix3D spatialMix3D = ComputeAudioSpatialMix3D(
                    audioSource,
                    sourcePosition3D,
                    ComputeEntityWorldForward(*scene, entity),
                    sourceVelocity,
                    listenerStates3D);
                runtimeVolume *= spatialMix3D.Gain;
                runtimePan = spatialMix3D.Pan;
                runtimePitch *= spatialMix3D.PitchMultiplier;
            }

            runtimePan = std::clamp(runtimePan, -1.0f, 1.0f);
            runtimePitch = std::max(kAudioMinimumPitch, runtimePitch);

            const bool shouldPlayOnStart =
                audioSource.PlayOnStart &&
                !audioSource.RuntimePlayOnStartConsumed &&
                !audioSource.AudioClipKey.empty();
            const bool shouldPlayRequested =
                audioSource.RuntimePlayRequested &&
                !audioSource.AudioClipKey.empty();
            const bool shouldBePlaying = shouldPlayOnStart || shouldPlayRequested;

            if (!audioSource.PlayOnStart || audioSource.AudioClipKey.empty())
                audioSource.RuntimePlayOnStartConsumed = false;

            if (shouldBePlaying && audioSource.RuntimeVoiceId == 0)
            {
                auto& pendingAudioClipLoads = GetPendingAudioClipLoads();
                auto clipAsset = std::dynamic_pointer_cast<Assets::AudioClipAsset>(
                    Assets::AssetManager::GetCachedByKey(audioSource.AudioClipKey));
                if (!clipAsset)
                {
                    auto pendingIt = pendingAudioClipLoads.find(audioSource.AudioClipKey);
                    if (pendingIt == pendingAudioClipLoads.end())
                    {
                        pendingAudioClipLoads.emplace(audioSource.AudioClipKey, Assets::AudioClipAsset::LoadAsync(audioSource.AudioClipKey));
                        continue;
                    }

                    if (!pendingIt->second.IsDone())
                        continue;

                    clipAsset = pendingIt->second.Get();
                    pendingAudioClipLoads.erase(pendingIt);
                }
                else
                {
                    pendingAudioClipLoads.erase(audioSource.AudioClipKey);
                }

                if (clipAsset && clipAsset->GetClip())
                {
                    audioSource.RuntimeVoiceId = AudioEngine::GetInstance().PlayClip(
                        clipAsset->GetClip(),
                        runtimeVolume,
                        audioSource.Loop,
                        audioSource.MixerGroup,
                        runtimePan,
                        runtimePitch);
                    audioSource.RuntimePlaybackStarted = (audioSource.RuntimeVoiceId != 0);
                    if (shouldPlayOnStart)
                        audioSource.RuntimePlayOnStartConsumed = (audioSource.RuntimeVoiceId != 0);
                    if (audioSource.RuntimeVoiceId != 0)
                        audioSource.RuntimePlayRequested = false;
                }
            }
            else if (shouldBePlaying && audioSource.RuntimeVoiceId != 0)
            {
                (void)AudioEngine::GetInstance().SetVoiceMixParameters(
                    audioSource.RuntimeVoiceId,
                    runtimeVolume,
                    runtimePan,
                    audioSource.MixerGroup,
                    runtimePitch);
            }
        }
    }

    void StopAudioSourcesInScene(Scene* scene)
    {
        if (!scene)
            return;

        auto& registry = scene->GetRegistry();
        auto listenerView = registry.view<AudioListener3DComponent>();
        for (entt::entity entity : listenerView)
        {
            auto& listener = listenerView.get<AudioListener3DComponent>(entity);
            listener.RuntimeHasPreviousWorldPosition = false;
            listener.RuntimePreviousWorldPosition = glm::vec3(0.0f);
        }

        auto audioView = registry.view<AudioSourceComponent>();
        for (entt::entity entity : audioView)
        {
            auto& audioSource = audioView.get<AudioSourceComponent>(entity);
            if (audioSource.RuntimeVoiceId != 0)
                AudioEngine::GetInstance().Stop(audioSource.RuntimeVoiceId);
            audioSource.RuntimeVoiceId = 0;
            audioSource.RuntimePlaybackStarted = false;
            audioSource.RuntimePlayRequested = false;
            audioSource.RuntimePlayOnStartConsumed = false;
            audioSource.RuntimeHasPreviousWorldPosition = false;
            audioSource.RuntimePreviousWorldPosition = glm::vec3(0.0f);
        }
    }
}
