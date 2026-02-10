#pragma once

#include "Audio/AudioClip.h"

#include <SDL3/SDL_audio.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace Limitless::Audio
{
    // -----------------------------------------------------------------------------
    // AudioEngine
    // Unity-style runtime mixer:
    // - Owns the SDL audio device stream
    // - Mixes multiple "voices" (playing AudioClip instances) into one output
    //
    // Notes:
    // - The audio callback must do no allocations and avoid blocking.
    // - We keep synchronization simple initially (device lock), but the API is
    //   designed so we can move to a lock-free command queue later.
    // -----------------------------------------------------------------------------
    class AudioEngine final
    {
    public:
        static AudioEngine& GetInstance();

        bool Initialize();
        void Shutdown();

        bool IsInitialized() const { return m_Stream != nullptr; }

        // Global gain applied after all voice mixing.
        void SetMasterVolume(float volume);
        float GetMasterVolume() const { return m_MasterVolume; }

        // Play a clip as a new voice. Returns a voice id that can be stopped later.
        uint32_t PlayOneShot(std::shared_ptr<const AudioClip> clip, float volume = 1.0f);

        // Stop a voice by id. Safe to call even if the voice already finished.
        void Stop(uint32_t voiceId);

        // Stop everything immediately.
        void StopAll();

    private:
        AudioEngine() = default;
        ~AudioEngine() = default;

        AudioEngine(const AudioEngine&) = delete;
        AudioEngine& operator=(const AudioEngine&) = delete;

        struct Voice
        {
            uint32_t Id = 0;
            std::shared_ptr<const AudioClip> Clip;
            uint64_t FrameCursor = 0;
            float Volume = 1.0f;
            bool Loop = false;
            bool Active = false;
        };

        static void SDLCALL StreamCallback(void* userdata, SDL_AudioStream* stream, int additionalAmount, int totalAmount);
        void ProduceAudio(SDL_AudioStream* stream, int additionalAmount, int totalAmount);

        // Mix into `outInterleavedStereoF32`, which contains `frameCount` frames.
        void Mix(float* outInterleavedStereoF32, uint32_t frameCount);

    private:
        SDL_AudioStream* m_Stream = nullptr;
        SDL_AudioSpec m_MixerSpec{};

        float m_MasterVolume = 1.0f;
        uint32_t m_NextVoiceId = 1;

        std::mutex m_VoiceMutex;
        std::vector<Voice> m_Voices;

        // Scratch buffer used by the audio callback (interleaved stereo float32).
        // This is preallocated to avoid per-callback allocations.
        std::vector<float> m_Scratch;
    };
}

