#include "Audio/AudioEngine.h"

#include "Core/Debug/Log.h"

#include <algorithm>
#include <cmath>

namespace Limitless::Audio
{
    static constexpr uint32_t kMixerSampleRateHz = 48000;
    static constexpr uint32_t kMixerChannels = 2;

    AudioEngine& AudioEngine::GetInstance()
    {
        static AudioEngine s_Instance;
        return s_Instance;
    }

    bool AudioEngine::Initialize()
    {
        if (IsInitialized())
        {
            return true;
        }

        SDL_AudioSpec spec{};
        spec.freq = static_cast<int>(kMixerSampleRateHz);
        spec.format = SDL_AUDIO_F32;
        spec.channels = static_cast<int>(kMixerChannels);

        m_Stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &AudioEngine::StreamCallback, this);
        if (!m_Stream)
        {
            LT_CORE_ERROR("AudioEngine: SDL_OpenAudioDeviceStream failed: {}", SDL_GetError());
            return false;
        }

        // Cache the actual stream format (SDL may adjust based on hardware).
        SDL_AudioSpec src{};
        SDL_AudioSpec dst{};
        if (!SDL_GetAudioStreamFormat(m_Stream, &src, &dst))
        {
            LT_CORE_WARN("AudioEngine: SDL_GetAudioStreamFormat failed: {}", SDL_GetError());
        }

        // We provide input data, so src is the app-side format.
        m_MixerSpec = src;

        // Preallocate a conservative scratch buffer (frames).
        // SDL can request different sizes; we chunk as needed.
        const uint32_t initialFrames = 4096;
        m_Scratch.resize(static_cast<size_t>(initialFrames) * kMixerChannels);

        {
            std::lock_guard<std::mutex> lock(m_VoiceMutex);
            if (m_MixerGroupVolumes.empty())
            {
                m_MixerGroupVolumes.emplace("Master", 1.0f);
                m_MixerGroupVolumes.emplace("SFX", 1.0f);
                m_MixerGroupVolumes.emplace("Music", 1.0f);
                m_MixerGroupVolumes.emplace("UI", 1.0f);
            }
        }

        if (!SDL_ResumeAudioStreamDevice(m_Stream))
        {
            LT_CORE_ERROR("AudioEngine: SDL_ResumeAudioStreamDevice failed: {}", SDL_GetError());
            SDL_DestroyAudioStream(m_Stream);
            m_Stream = nullptr;
            return false;
        }

        LT_CORE_INFO("AudioEngine initialized (Mixer={} Hz, {} channels, format=SDL_AUDIO_F32)", kMixerSampleRateHz, kMixerChannels);
        return true;
    }

    void AudioEngine::Shutdown()
    {
        if (!IsInitialized())
        {
            return;
        }

        StopAll();

        SDL_DestroyAudioStream(m_Stream);
        m_Stream = nullptr;

        m_Voices.clear();
        m_Scratch.clear();

        LT_CORE_INFO("AudioEngine shutdown complete");
    }

    void AudioEngine::SetMasterVolume(float volume)
    {
        m_MasterVolume = std::max(0.0f, volume);
    }

    uint32_t AudioEngine::PlayClip(std::shared_ptr<const AudioClip> clip, float volume, bool loop, const std::string& mixerGroup, float pan)
    {
        if (!clip || clip->Samples.empty())
        {
            return 0;
        }

        if (clip->SampleRateHz != kMixerSampleRateHz || clip->ChannelCount != kMixerChannels)
        {
            LT_CORE_WARN("AudioEngine::PlayClip: clip format mismatch ({} Hz, {} ch). Expected {} Hz, {} ch. Decoder should resample/remix.",
                         clip->SampleRateHz, clip->ChannelCount, kMixerSampleRateHz, kMixerChannels);
        }

        const uint32_t id = m_NextVoiceId++;

        // Lock-free version is a future optimization. This is correct and simple.
        std::lock_guard<std::mutex> lock(m_VoiceMutex);

        // Reuse an inactive slot if possible to avoid vector growth.
        for (auto& v : m_Voices)
        {
            if (!v.Active && !v.Clip)
            {
                v.Id = id;
                v.Clip = std::move(clip);
                v.FrameCursor = 0;
                v.Volume = std::max(0.0f, volume);
                v.Pan = std::clamp(pan, -1.0f, 1.0f);
                v.MixerGroup = mixerGroup.empty() ? "Master" : mixerGroup;
                v.Loop = loop;
                v.Active = true;
                return id;
            }
        }

        Voice v{};
        v.Id = id;
        v.Clip = std::move(clip);
        v.FrameCursor = 0;
        v.Volume = std::max(0.0f, volume);
        v.Pan = std::clamp(pan, -1.0f, 1.0f);
        v.MixerGroup = mixerGroup.empty() ? "Master" : mixerGroup;
        v.Loop = loop;
        v.Active = true;
        m_Voices.emplace_back(std::move(v));

        return id;
    }

    uint32_t AudioEngine::PlayOneShot(std::shared_ptr<const AudioClip> clip, float volume)
    {
        return PlayClip(std::move(clip), volume, false);
    }

    void AudioEngine::Stop(uint32_t voiceId)
    {
        if (voiceId == 0)
        {
            return;
        }

        std::lock_guard<std::mutex> lock(m_VoiceMutex);
        for (auto& v : m_Voices)
        {
            if (v.Id == voiceId)
            {
                v.Active = false;
                v.Clip.reset();
                v.FrameCursor = 0;
                return;
            }
        }
    }

    bool AudioEngine::IsVoiceActive(uint32_t voiceId) const
    {
        if (voiceId == 0)
            return false;

        std::lock_guard<std::mutex> lock(m_VoiceMutex);
        for (const auto& v : m_Voices)
        {
            if (v.Id == voiceId)
                return v.Active && v.Clip != nullptr;
        }

        return false;
    }

    bool AudioEngine::SetVoiceMixParameters(uint32_t voiceId, float volume, float pan, const std::string& mixerGroup)
    {
        if (voiceId == 0)
            return false;

        std::lock_guard<std::mutex> lock(m_VoiceMutex);
        for (auto& v : m_Voices)
        {
            if (v.Id == voiceId && v.Active && v.Clip)
            {
                v.Volume = std::max(0.0f, volume);
                v.Pan = std::clamp(pan, -1.0f, 1.0f);
                v.MixerGroup = mixerGroup.empty() ? "Master" : mixerGroup;
                return true;
            }
        }

        return false;
    }

    void AudioEngine::SetMixerGroupVolume(const std::string& mixerGroup, float volume)
    {
        if (mixerGroup.empty())
            return;

        std::lock_guard<std::mutex> lock(m_VoiceMutex);
        m_MixerGroupVolumes[mixerGroup] = std::max(0.0f, volume);
    }

    float AudioEngine::GetMixerGroupVolume(const std::string& mixerGroup) const
    {
        if (mixerGroup.empty())
            return 1.0f;

        std::lock_guard<std::mutex> lock(m_VoiceMutex);
        const auto it = m_MixerGroupVolumes.find(mixerGroup);
        if (it == m_MixerGroupVolumes.end())
            return 1.0f;
        return std::max(0.0f, it->second);
    }

    void AudioEngine::StopAll()
    {
        std::lock_guard<std::mutex> lock(m_VoiceMutex);
        for (auto& v : m_Voices)
        {
            v.Active = false;
            v.Clip.reset();
            v.FrameCursor = 0;
        }
        m_Voices.clear();
    }

    void SDLCALL AudioEngine::StreamCallback(void* userdata, SDL_AudioStream* stream, int additionalAmount, int totalAmount)
    {
        auto* self = static_cast<AudioEngine*>(userdata);
        if (!self)
        {
            return;
        }

        // If the device format changes, SDL may call this with different sizes.
        // Our job: ensure at least `additionalAmount` bytes are queued.
        self->ProduceAudio(stream, additionalAmount, totalAmount);
    }

    void AudioEngine::ProduceAudio(SDL_AudioStream* stream, int additionalAmount, int /*totalAmount*/)
    {
        if (!stream || additionalAmount <= 0)
        {
            return;
        }

        // We always provide float32 stereo input.
        const int bytesPerFrame = static_cast<int>(sizeof(float) * kMixerChannels);
        const uint32_t requestedFrames = static_cast<uint32_t>(additionalAmount / bytesPerFrame);
        if (requestedFrames == 0)
        {
            return;
        }

        // Chunk production to fit our scratch buffer.
        uint32_t remainingFrames = requestedFrames;
        while (remainingFrames > 0)
        {
            const uint32_t scratchFrames = static_cast<uint32_t>(m_Scratch.size() / kMixerChannels);
            const uint32_t framesThisChunk = std::min(remainingFrames, scratchFrames);

            Mix(m_Scratch.data(), framesThisChunk);

            const int bytesThisChunk = static_cast<int>(framesThisChunk * bytesPerFrame);
            if (!SDL_PutAudioStreamData(stream, m_Scratch.data(), bytesThisChunk))
            {
                // Avoid logging every callback; this is time sensitive.
                // Log once per failure kind would be better; keep it simple for now.
                return;
            }

            remainingFrames -= framesThisChunk;
        }
    }

    void AudioEngine::Mix(float* outInterleavedStereoF32, uint32_t frameCount)
    {
        // Clear output to silence.
        const uint32_t sampleCount = frameCount * kMixerChannels;
        for (uint32_t i = 0; i < sampleCount; ++i)
        {
            outInterleavedStereoF32[i] = 0.0f;
        }

        // We keep mixing simple: sum voices, then apply master volume.
        std::lock_guard<std::mutex> lock(m_VoiceMutex);

        for (auto& v : m_Voices)
        {
            if (!v.Active || !v.Clip || v.Clip->Samples.empty())
            {
                continue;
            }

            const auto& samples = v.Clip->Samples;
            const uint64_t clipFrames = v.Clip->GetFrameCount();
            if (clipFrames == 0)
            {
                v.Active = false;
                v.Clip.reset();
                continue;
            }

            uint64_t cursor = v.FrameCursor;
            const float mixerGroupVolume = ResolveMixerGroupVolumeLocked(v.MixerGroup);
            const float voiceVolume = v.Volume * mixerGroupVolume;
            const float clampedPan = std::clamp(v.Pan, -1.0f, 1.0f);
            constexpr float kHalfPi = 1.57079632679f;
            const float panAngle = (clampedPan + 1.0f) * 0.5f * kHalfPi;
            const float panLeftGain = std::cos(panAngle);
            const float panRightGain = std::sin(panAngle);

            for (uint32_t f = 0; f < frameCount; ++f)
            {
                if (cursor >= clipFrames)
                {
                    if (v.Loop)
                    {
                        cursor = 0;
                    }
                    else
                    {
                        v.Active = false;
                        v.Clip.reset();
                        break;
                    }
                }

                const uint64_t base = cursor * kMixerChannels;
                const float inL = samples[static_cast<size_t>(base + 0)];
                const float inR = samples[static_cast<size_t>(base + 1)];

                const uint32_t outBase = f * kMixerChannels;
                outInterleavedStereoF32[outBase + 0] += inL * voiceVolume * panLeftGain;
                outInterleavedStereoF32[outBase + 1] += inR * voiceVolume * panRightGain;

                ++cursor;
            }

            v.FrameCursor = cursor;
        }

        // Apply master volume and clamp (cheap hard clip).
        const float master = m_MasterVolume;
        for (uint32_t i = 0; i < sampleCount; ++i)
        {
            float x = outInterleavedStereoF32[i] * master;
            x = std::max(-1.0f, std::min(1.0f, x));
            outInterleavedStereoF32[i] = x;
        }
    }

    float AudioEngine::ResolveMixerGroupVolumeLocked(const std::string& mixerGroup) const
    {
        auto resolveSingleGroup = [this](const std::string& groupName) {
            if (groupName.empty())
                return 1.0f;
            const auto it = m_MixerGroupVolumes.find(groupName);
            if (it == m_MixerGroupVolumes.end())
                return 1.0f;
            return std::max(0.0f, it->second);
        };

        // Mixer "Master" is a true top-level fader:
        // it applies to every routed group, not just voices explicitly assigned to "Master".
        const float masterGroupVolume = resolveSingleGroup("Master");
        if (mixerGroup.empty() || mixerGroup == "Master")
            return masterGroupVolume;

        const float routedGroupVolume = resolveSingleGroup(mixerGroup);
        return masterGroupVolume * routedGroupVolume;
    }
}

