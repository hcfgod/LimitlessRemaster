#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <vector>

namespace Limitless
{
    struct AudioSourceComponent;
    class Scene;
}

namespace Limitless::Audio
{
    using AudioListenerPositions2D = std::vector<glm::vec2>;

    struct AudioListenerState3D
    {
        bool HasListener = false;
        glm::vec3 Position = glm::vec3(0.0f);
        glm::vec3 Forward = glm::vec3(0.0f, 0.0f, -1.0f);
        glm::vec3 Velocity = glm::vec3(0.0f);
    };

    using AudioListenerStates3D = std::vector<AudioListenerState3D>;

    struct AudioSpatialMix2D
    {
        float Gain = 1.0f;
        float Pan = 0.0f;
    };

    struct AudioSpatialMix3D
    {
        float Gain = 1.0f;
        float Pan = 0.0f;
        float PitchMultiplier = 1.0f;
    };

    /// Collects world-space positions of all enabled AudioListener2DComponents in the scene.
    /// Falls back to the primary camera position when no explicit listener exists.
    AudioListenerPositions2D CollectAudioListenerPositions2D(const Scene& scene);

    AudioListenerStates3D CollectAudioListenerStates3D(Scene& scene, float deltaTime);

    /// Computes distance-based gain attenuation and stereo pan for a single spatial source
    /// relative to the nearest listener.
    AudioSpatialMix2D ComputeAudioSpatialMix2D(const AudioSourceComponent& audioSource,
                                                const glm::vec2& sourcePosition,
                                                const AudioListenerPositions2D& listeners);

    AudioSpatialMix3D ComputeAudioSpatialMix3D(const AudioSourceComponent& audioSource,
                                                const glm::vec3& sourcePosition,
                                                const glm::vec3& sourceForward,
                                                const glm::vec3& sourceVelocity,
                                                const AudioListenerStates3D& listeners);

    /// Drives per-frame audio source lifecycle: starts PlayOnStart voices, updates spatial
    /// mix parameters on active voices, and stops voices for disabled entities.
    /// Set @p runtimePlaybackAllowed to false to skip starting/updating runtime voices
    /// (used by the editor in Edit mode to leave manual preview playback alone).
    void UpdateSceneAudioSources(Scene* scene, float deltaTime, bool runtimePlaybackAllowed = true);

    /// Immediately stops all active audio voices in the scene and resets their runtime state.
    void StopAudioSourcesInScene(Scene* scene);
}
