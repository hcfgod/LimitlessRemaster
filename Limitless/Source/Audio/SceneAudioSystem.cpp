#include "Audio/SceneAudioSystem.h"

#include "Assets/AudioClipAsset.h"
#include "Audio/AudioEngine.h"
#include "Scene/Components/AudioComponents.h"
#include "Scene/Components/CoreComponents.h"
#include "Scene/Scene.h"

#include <algorithm>
#include <cmath>

namespace Limitless::Audio
{
    namespace
    {
        glm::vec2 ComputeEntityWorldPosition2D(const Scene& scene, entt::entity entity)
        {
            const glm::mat4 worldTransform = scene.GetWorldTransformMatrix(entity);
            return glm::vec2(worldTransform[3][0], worldTransform[3][1]);
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

    AudioSpatialMix2D ComputeAudioSpatialMix2D(const AudioSourceComponent& audioSource,
                                                const glm::vec2& sourcePosition,
                                                const AudioListenerPositions2D& listeners)
    {
        AudioSpatialMix2D result{};
        if (audioSource.Space != AudioSourceComponent::PlaybackSpace::Spatial2D || listeners.empty())
            return result;

        const AudioListener2DRuntimeState listenerState = ResolveNearestListener(listeners, sourcePosition);

        const float minDistance = std::max(0.001f, audioSource.SpatialMinDistance);
        const float maxDistance = std::max(minDistance, audioSource.SpatialMaxDistance);
        const float rolloffExponent = std::max(0.01f, audioSource.SpatialRolloffExponent);
        const float panStrength = std::clamp(audioSource.StereoPanStrength, 0.0f, 1.0f);

        const float distanceToListener = glm::length(sourcePosition - listenerState.Position);
        if (distanceToListener <= minDistance)
        {
            result.Gain = 1.0f;
        }
        else if (distanceToListener >= maxDistance)
        {
            result.Gain = 0.0f;
        }
        else
        {
            const float normalized = (distanceToListener - minDistance) / (maxDistance - minDistance);
            result.Gain = std::pow(std::max(0.0f, 1.0f - normalized), rolloffExponent);
        }

        const float panNormalizationDistance = std::max(maxDistance, 0.001f);
        const float signedPan = std::clamp((sourcePosition.x - listenerState.Position.x) / panNormalizationDistance, -1.0f, 1.0f);
        result.Pan = signedPan * panStrength;
        return result;
    }

    void UpdateSceneAudioSources(Scene* scene, bool runtimePlaybackAllowed)
    {
        if (!scene)
            return;

        auto& registry = scene->GetRegistry();
        const AudioListenerPositions2D listenerPositions = CollectAudioListenerPositions2D(*scene);
        auto audioView = registry.view<AudioSourceComponent>();
        for (entt::entity entity : audioView)
        {
            auto& audioSource = audioView.get<AudioSourceComponent>(entity);
            const bool entityEnabled = scene->IsEntityEnabledInHierarchy(entity);
            if (audioSource.RuntimeVoiceId != 0 &&
                !AudioEngine::GetInstance().IsVoiceActive(audioSource.RuntimeVoiceId))
            {
                audioSource.RuntimeVoiceId = 0;
            }

            if (!entityEnabled)
            {
                if (audioSource.RuntimeVoiceId != 0)
                    AudioEngine::GetInstance().Stop(audioSource.RuntimeVoiceId);
                audioSource.RuntimeVoiceId = 0;
                audioSource.RuntimePlaybackStarted = false;
                continue;
            }

            if (!runtimePlaybackAllowed)
                continue;

            const glm::vec2 sourcePosition = ComputeEntityWorldPosition2D(*scene, entity);
            const AudioSpatialMix2D spatialMix = ComputeAudioSpatialMix2D(audioSource, sourcePosition, listenerPositions);
            const float authoredVolume = audioSource.Muted ? 0.0f : std::max(0.0f, audioSource.Volume);
            const float runtimeVolume = authoredVolume * spatialMix.Gain;
            const float runtimePan = spatialMix.Pan;
            const float runtimePitch = std::max(0.01f, audioSource.Pitch);

            const bool shouldPlayOnStart =
                audioSource.PlayOnStart &&
                !audioSource.AudioClipKey.empty();

            if (shouldPlayOnStart && !audioSource.RuntimePlaybackStarted)
            {
                auto clipAsset = Assets::AudioClipAsset::LoadBlocking(audioSource.AudioClipKey);
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
                }
            }
            else if (shouldPlayOnStart && audioSource.RuntimeVoiceId != 0)
            {
                (void)AudioEngine::GetInstance().SetVoiceMixParameters(
                    audioSource.RuntimeVoiceId,
                    runtimeVolume,
                    runtimePan,
                    audioSource.MixerGroup,
                    runtimePitch);
            }
            else if (!shouldPlayOnStart && audioSource.RuntimeVoiceId != 0)
            {
                AudioEngine::GetInstance().Stop(audioSource.RuntimeVoiceId);
                audioSource.RuntimeVoiceId = 0;
                audioSource.RuntimePlaybackStarted = false;
            }
        }
    }

    void StopAudioSourcesInScene(Scene* scene)
    {
        if (!scene)
            return;

        auto& registry = scene->GetRegistry();
        auto audioView = registry.view<AudioSourceComponent>();
        for (entt::entity entity : audioView)
        {
            auto& audioSource = audioView.get<AudioSourceComponent>(entity);
            if (audioSource.RuntimeVoiceId != 0)
                AudioEngine::GetInstance().Stop(audioSource.RuntimeVoiceId);
            audioSource.RuntimeVoiceId = 0;
            audioSource.RuntimePlaybackStarted = false;
        }
    }
}
