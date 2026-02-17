#pragma once

#include "Audio/AudioSource.h"

#include <memory>
#include <string>

namespace Limitless
{
    // -----------------------------------------------------------------------------
    // AudioDemo
    // Minimal Runtime-side validation for the Unity-style audio stack:
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

        // NOTE(macOS/Clang):
        // Avoid default arguments like `const Settings& settings = Settings{}` for nested types that
        // rely on default member initializers (like ClipKey above). Some Clang versions reject this
        // while the enclosing class is still being defined. Overloads are portable.
        void Initialize();
        void Initialize(const Settings& settings);
        void Shutdown();

        void Play();
        void Stop();

    private:
        Settings m_Settings{};
        Audio::AudioSource m_Source;
    };
}

