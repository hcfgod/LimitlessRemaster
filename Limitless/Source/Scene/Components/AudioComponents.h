#pragma once

#include <glm/vec3.hpp>

#include <cstdint>
#include <string>

namespace Limitless
{
    /// Selects a 2D audio listener location for spatialized sources.
    /// Author one listener per scene (or one on the active camera entity).
    struct AudioListener2DComponent
    {
        bool Enabled = true;
        bool UsePrimaryCameraPosition = true;
    };

    struct AudioListener3DComponent
    {
        bool Enabled = true;
        bool UsePrimaryCameraTransform = true;

        bool RuntimeHasPreviousWorldPosition = false;
        glm::vec3 RuntimePreviousWorldPosition = glm::vec3(0.0f);
    };

    /// Optional audio source reference (Unity-style).
    /// Supports global playback and 2D spatial playback with attenuation/pan.
    struct AudioSourceComponent
    {
        enum class PlaybackSpace
        {
            Global = 0,
            Spatial2D = 1,
            Spatial3D = 2
        };

        enum class RolloffMode
        {
            SmoothStep = 0,
            Linear = 1,
            Inverse = 2
        };

        std::string AudioClipKey; ///< Asset key for audio clip (example: "Assets/Audio/MyClip.wav")
        float Volume = 1.0f;
        float Pitch = 1.0f;
        bool PlayOnStart = true;
        bool Loop = false;
        bool Muted = false;
        PlaybackSpace Space = PlaybackSpace::Global;
        std::string MixerGroup = "SFX";

        // Spatial 2D settings.
        float SpatialMinDistance = 1.0f;
        float SpatialMaxDistance = 20.0f;
        float SpatialRolloffExponent = 1.0f;
        float StereoPanStrength = 1.0f;
        RolloffMode SpatialRolloffMode = RolloffMode::Linear;
        float DopplerFactor = 1.0f;
        bool EnableDirectionalAttenuation = false;
        float DirectionalInnerAngleDegrees = 360.0f;
        float DirectionalOuterAngleDegrees = 360.0f;
        float DirectionalOuterVolume = 1.0f;
        std::string AttenuationCurveKey; ///< Optional curve reference for future attenuation assets.

        // Runtime-only state (not serialized).
        uint32_t RuntimeVoiceId = 0;
        bool RuntimePlaybackStarted = false;
        bool RuntimeHasPreviousWorldPosition = false;
        glm::vec3 RuntimePreviousWorldPosition = glm::vec3(0.0f);
    };
}
