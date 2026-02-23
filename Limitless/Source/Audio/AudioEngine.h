#pragma once

#include "Audio/AudioClip.h"
#include "Core/Concurrency/LockFreeQueue.h"

#include <SDL3/SDL_audio.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Limitless::Audio
{
    struct VoiceActivityState
    {
        std::atomic<bool> Active{ false };
    };

    enum class AudioCommandType : uint8_t
    {
        PlayVoice = 0,
        StopVoice,
        SetVoiceMixParameters,
        SetMixerGroupVolume,
        SetMixerGroupReverbSend,
        StopAll
    };

    struct AudioCommand
    {
        AudioCommandType Type = AudioCommandType::StopVoice;
        uint32_t VoiceId = 0;
        std::shared_ptr<const AudioClip> Clip;
        std::shared_ptr<VoiceActivityState> ActivityState;
        float Volume = 1.0f;
        float Pan = 0.0f;
        float Pitch = 1.0f;
        bool Loop = false;
        std::string MixerGroup = "Master";
    };

    // -----------------------------------------------------------------------------
    // AudioEngine
    // Unity-style runtime mixer:
    // - Owns the SDL audio device stream
    // - Mixes multiple "voices" (playing AudioClip instances) into one output
    //
    // Notes:
    // - The audio callback must do no allocations and avoid blocking.
    // - Public API threads push commands into a lock-free queue.
    // - The callback thread owns voice/mixer runtime state and drains commands.
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
        float GetMasterVolume() const { return m_MasterVolume.load(std::memory_order_acquire); }

        // Play a clip as a new voice. Returns a voice id that can be stopped later.
        // `mixerGroup` routes the voice through a named group fader (Master/SFX/Music/UI/custom).
        // `pan` is stereo pan in the range [-1, 1], where -1 = left and 1 = right.
        uint32_t PlayClip(std::shared_ptr<const AudioClip> clip,
                          float volume = 1.0f,
                          bool loop = false,
                          const std::string& mixerGroup = "Master",
                          float pan = 0.0f,
                          float pitch = 1.0f);

        // Backward-compatible alias for non-looping playback.
        uint32_t PlayOneShot(std::shared_ptr<const AudioClip> clip, float volume = 1.0f);

        // Stop a voice by id. Safe to call even if the voice already finished.
        void Stop(uint32_t voiceId);

        // Returns true when the voice id currently maps to an active voice.
        bool IsVoiceActive(uint32_t voiceId) const;

        // Update per-voice volume/pan/group at runtime (used by spatial audio updates).
        bool SetVoiceMixParameters(uint32_t voiceId, float volume, float pan, const std::string& mixerGroup, float pitch = 1.0f);

        // Update/read mixer group faders at runtime.
        void SetMixerGroupVolume(const std::string& mixerGroup, float volume);
        float GetMixerGroupVolume(const std::string& mixerGroup) const;
        void SetMixerGroupReverbSend(const std::string& mixerGroup, float sendAmount);
        float GetMixerGroupReverbSend(const std::string& mixerGroup) const;

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
            std::shared_ptr<VoiceActivityState> ActivityState;
            double FrameCursor = 0.0;
            float Volume = 1.0f;
            float Pan = 0.0f;
            float Pitch = 1.0f;
            std::string MixerGroup = "Master";
            bool Loop = false;
            bool Active = false;
        };

        static void SDLCALL StreamCallback(void* userdata, SDL_AudioStream* stream, int additionalAmount, int totalAmount);
        void ProduceAudio(SDL_AudioStream* stream, int additionalAmount, int totalAmount);

        bool EnqueueCriticalCommand(AudioCommand&& command, const char* commandName);
        bool EnqueueBestEffortCommand(AudioCommand&& command, const char* commandName);
        void DrainPendingCommands();
        void ApplyPlayVoiceCommand(AudioCommand&& command);
        void ApplyStopVoiceCommand(uint32_t voiceId);
        bool ApplySetVoiceMixParametersCommand(uint32_t voiceId, float volume, float pan, const std::string& mixerGroup, float pitch);

        // Mix into `outInterleavedStereoF32`, which contains `frameCount` frames.
        void Mix(float* outInterleavedStereoF32, uint32_t frameCount);
        float ResolveMixerGroupVolume(const std::string& mixerGroup) const;
        float ResolveMixerGroupReverbSend(const std::string& mixerGroup) const;

    private:
        SDL_AudioStream* m_Stream = nullptr;
        SDL_AudioSpec m_MixerSpec{};

        std::atomic<float> m_MasterVolume{ 1.0f };
        std::atomic<uint32_t> m_NextVoiceId{ 1u };

        Concurrency::LockFreeMPMCQueue<AudioCommand, 4096> m_CommandQueue;
        std::vector<Voice> m_Voices;
        std::unordered_map<std::string, float> m_MixerGroupVolumes;
        std::unordered_map<std::string, float> m_MixerGroupReverbSends;
        mutable std::mutex m_PublicStateMutex;
        std::unordered_map<uint32_t, std::shared_ptr<VoiceActivityState>> m_VoiceActivityById;
        std::unordered_map<std::string, float> m_MixerGroupVolumeSnapshot;
        std::unordered_map<std::string, float> m_MixerGroupReverbSendSnapshot;

        // Scratch buffer used by the audio callback (interleaved stereo float32).
        // This is preallocated to avoid per-callback allocations.
        std::vector<float> m_Scratch;
        std::vector<float> m_ReverbSendScratch;
        std::vector<float> m_ReverbDelayLeft;
        std::vector<float> m_ReverbDelayRight;
        uint32_t m_ReverbDelayCursor = 0;
        float m_ReverbFeedback = 0.62f;
        float m_ReverbWetMix = 0.18f;
    };
}

