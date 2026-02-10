#pragma once

#include "Audio/AudioSource.h"

#include <memory>
#include <string>

namespace Limitless
{
    // -----------------------------------------------------------------------------
    // AudioDemo
    // Minimal Sandbox-side validation for the Unity-style audio stack:
    // - Loads an AudioClip asset via AssetManager (FFmpeg decode)
    // - Plays it through AudioEngine when triggered
    // -----------------------------------------------------------------------------
    class AudioDemo final
    {
    public:
        struct Settings
        {
            // User can drop a file here, e.g.:
            // - Assets/Audio/Example.wav
            // - Assets/Audio/Example.mp3
            std::string ClipKey = "Assets/Audio/test.wav";
        };

        void Initialize(const Settings& settings = Settings{});
        void Shutdown();

        void Play();
        void Stop();

    private:
        Settings m_Settings{};
        Audio::AudioSource m_Source;
    };
}

