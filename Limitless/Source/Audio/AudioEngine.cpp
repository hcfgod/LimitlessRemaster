#include "Audio/AudioEngine.h"

#include "Core/Debug/Log.h"

#include <algorithm>
#include <cmath>
#include <thread>

namespace Limitless::Audio
{
    static constexpr uint32_t kMixerSampleRateHz = 48000;
    static constexpr uint32_t kMixerChannels = 2;
    static constexpr int kCriticalCommandEnqueueRetries = 1024;
    static constexpr uint32_t kReverbDelayFrames = 12000;

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
        m_ReverbSendScratch.resize(static_cast<size_t>(initialFrames) * kMixerChannels);
        m_ReverbDelayLeft.assign(kReverbDelayFrames, 0.0f);
        m_ReverbDelayRight.assign(kReverbDelayFrames, 0.0f);
        m_ReverbDelayCursor = 0;

        m_CommandQueue.Clear();
        m_Voices.clear();
        m_MixerGroupVolumes.clear();
        m_MixerGroupReverbSends.clear();
        m_MixerGroupVolumes.emplace("Master", 1.0f);
        m_MixerGroupVolumes.emplace("SFX", 1.0f);
        m_MixerGroupVolumes.emplace("Music", 1.0f);
        m_MixerGroupVolumes.emplace("UI", 1.0f);
        m_MixerGroupReverbSends.emplace("Master", 0.0f);
        m_MixerGroupReverbSends.emplace("SFX", 0.0f);
        m_MixerGroupReverbSends.emplace("Music", 0.0f);
        m_MixerGroupReverbSends.emplace("UI", 0.0f);
        m_MasterVolume.store(1.0f, std::memory_order_release);
        m_NextVoiceId.store(1u, std::memory_order_release);

        {
            std::lock_guard<std::mutex> lock(m_PublicStateMutex);
            m_VoiceActivityById.clear();
            m_MixerGroupVolumeSnapshot = m_MixerGroupVolumes;
            m_MixerGroupReverbSendSnapshot = m_MixerGroupReverbSends;
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

        m_CommandQueue.Clear();
        m_Voices.clear();
        m_MixerGroupVolumes.clear();
        m_MixerGroupReverbSends.clear();
        m_Scratch.clear();
        m_ReverbSendScratch.clear();
        m_ReverbDelayLeft.clear();
        m_ReverbDelayRight.clear();
        {
            std::lock_guard<std::mutex> lock(m_PublicStateMutex);
            m_VoiceActivityById.clear();
            m_MixerGroupVolumeSnapshot.clear();
            m_MixerGroupReverbSendSnapshot.clear();
        }

        LT_CORE_INFO("AudioEngine shutdown complete");
    }

    void AudioEngine::SetMasterVolume(float volume)
    {
        m_MasterVolume.store(std::max(0.0f, volume), std::memory_order_release);
    }

    bool AudioEngine::EnqueueCriticalCommand(AudioCommand&& command, const char* commandName)
    {
        for (int attempt = 0; attempt < kCriticalCommandEnqueueRetries; ++attempt)
        {
            if (m_CommandQueue.TryPush(std::move(command)))
                return true;
            std::this_thread::yield();
        }

        LT_CORE_ERROR("AudioEngine: command queue overflow while enqueuing '{}'.", commandName ? commandName : "<unknown>");
        return false;
    }

    bool AudioEngine::EnqueueBestEffortCommand(AudioCommand&& command, const char* commandName)
    {
        if (m_CommandQueue.TryPush(std::move(command)))
            return true;

        LT_CORE_WARN("AudioEngine: dropped best-effort command '{}' because queue is full.", commandName ? commandName : "<unknown>");
        return false;
    }

    uint32_t AudioEngine::PlayClip(std::shared_ptr<const AudioClip> clip, float volume, bool loop, const std::string& mixerGroup, float pan, float pitch)
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

        const uint32_t id = m_NextVoiceId.fetch_add(1u, std::memory_order_relaxed);
        auto activityState = std::make_shared<VoiceActivityState>();
        activityState->Active.store(true, std::memory_order_release);

        {
            std::lock_guard<std::mutex> lock(m_PublicStateMutex);
            m_VoiceActivityById[id] = activityState;
        }

        AudioCommand command{};
        command.Type = AudioCommandType::PlayVoice;
        command.VoiceId = id;
        command.Clip = std::move(clip);
        command.ActivityState = std::move(activityState);
        command.Volume = std::max(0.0f, volume);
        command.Pan = std::clamp(pan, -1.0f, 1.0f);
        command.Pitch = std::max(0.01f, pitch);
        command.Loop = loop;
        command.MixerGroup = mixerGroup.empty() ? "Master" : mixerGroup;

        if (!EnqueueCriticalCommand(std::move(command), "PlayVoice"))
        {
            std::lock_guard<std::mutex> lock(m_PublicStateMutex);
            m_VoiceActivityById.erase(id);
            return 0;
        }

        return id;
    }

    uint32_t AudioEngine::PlayOneShot(std::shared_ptr<const AudioClip> clip, float volume)
    {
        return PlayClip(std::move(clip), volume, false);
    }

    void AudioEngine::Stop(uint32_t voiceId)
    {
        if (voiceId == 0)
            return;

        {
            std::lock_guard<std::mutex> lock(m_PublicStateMutex);
            const auto it = m_VoiceActivityById.find(voiceId);
            if (it != m_VoiceActivityById.end())
            {
                it->second->Active.store(false, std::memory_order_release);
                m_VoiceActivityById.erase(it);
            }
        }

        AudioCommand command{};
        command.Type = AudioCommandType::StopVoice;
        command.VoiceId = voiceId;
        (void)EnqueueCriticalCommand(std::move(command), "StopVoice");
    }

    bool AudioEngine::IsVoiceActive(uint32_t voiceId) const
    {
        if (voiceId == 0)
            return false;

        std::lock_guard<std::mutex> lock(m_PublicStateMutex);
        const auto it = m_VoiceActivityById.find(voiceId);
        if (it == m_VoiceActivityById.end())
            return false;

        return it->second->Active.load(std::memory_order_acquire);
    }

    bool AudioEngine::SetVoiceMixParameters(uint32_t voiceId, float volume, float pan, const std::string& mixerGroup, float pitch)
    {
        if (voiceId == 0)
            return false;

        {
            std::lock_guard<std::mutex> lock(m_PublicStateMutex);
            const auto it = m_VoiceActivityById.find(voiceId);
            if (it == m_VoiceActivityById.end())
                return false;
            if (!it->second->Active.load(std::memory_order_acquire))
            {
                m_VoiceActivityById.erase(it);
                return false;
            }
        }

        AudioCommand command{};
        command.Type = AudioCommandType::SetVoiceMixParameters;
        command.VoiceId = voiceId;
        command.Volume = std::max(0.0f, volume);
        command.Pan = std::clamp(pan, -1.0f, 1.0f);
        command.Pitch = std::max(0.01f, pitch);
        command.MixerGroup = mixerGroup.empty() ? "Master" : mixerGroup;
        return EnqueueBestEffortCommand(std::move(command), "SetVoiceMixParameters");
    }

    void AudioEngine::SetMixerGroupVolume(const std::string& mixerGroup, float volume)
    {
        if (mixerGroup.empty())
            return;

        const float clampedVolume = std::max(0.0f, volume);
        {
            std::lock_guard<std::mutex> lock(m_PublicStateMutex);
            m_MixerGroupVolumeSnapshot[mixerGroup] = clampedVolume;
        }

        AudioCommand command{};
        command.Type = AudioCommandType::SetMixerGroupVolume;
        command.MixerGroup = mixerGroup;
        command.Volume = clampedVolume;
        (void)EnqueueCriticalCommand(std::move(command), "SetMixerGroupVolume");
    }

    float AudioEngine::GetMixerGroupVolume(const std::string& mixerGroup) const
    {
        if (mixerGroup.empty())
            return 1.0f;

        std::lock_guard<std::mutex> lock(m_PublicStateMutex);
        const auto it = m_MixerGroupVolumeSnapshot.find(mixerGroup);
        if (it == m_MixerGroupVolumeSnapshot.end())
            return 1.0f;
        return std::max(0.0f, it->second);
    }

    void AudioEngine::SetMixerGroupReverbSend(const std::string& mixerGroup, float sendAmount)
    {
        if (mixerGroup.empty())
            return;

        const float clampedSendAmount = std::clamp(sendAmount, 0.0f, 1.0f);
        {
            std::lock_guard<std::mutex> lock(m_PublicStateMutex);
            m_MixerGroupReverbSendSnapshot[mixerGroup] = clampedSendAmount;
        }

        AudioCommand command{};
        command.Type = AudioCommandType::SetMixerGroupReverbSend;
        command.MixerGroup = mixerGroup;
        command.Volume = clampedSendAmount;
        (void)EnqueueCriticalCommand(std::move(command), "SetMixerGroupReverbSend");
    }

    float AudioEngine::GetMixerGroupReverbSend(const std::string& mixerGroup) const
    {
        if (mixerGroup.empty())
            return 0.0f;

        std::lock_guard<std::mutex> lock(m_PublicStateMutex);
        const auto it = m_MixerGroupReverbSendSnapshot.find(mixerGroup);
        if (it == m_MixerGroupReverbSendSnapshot.end())
            return 0.0f;
        return std::clamp(it->second, 0.0f, 1.0f);
    }

    void AudioEngine::StopAll()
    {
        {
            std::lock_guard<std::mutex> lock(m_PublicStateMutex);
            for (auto& [_, state] : m_VoiceActivityById)
            {
                if (state)
                    state->Active.store(false, std::memory_order_release);
            }
            m_VoiceActivityById.clear();
        }

        AudioCommand command{};
        command.Type = AudioCommandType::StopAll;
        (void)EnqueueCriticalCommand(std::move(command), "StopAll");
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
            if (scratchFrames == 0)
                return;
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

    void AudioEngine::DrainPendingCommands()
    {
        while (true)
        {
            std::optional<AudioCommand> commandOpt = m_CommandQueue.TryPop();
            if (!commandOpt.has_value())
                return;

            AudioCommand command = std::move(commandOpt.value());
            switch (command.Type)
            {
                case AudioCommandType::PlayVoice:
                    ApplyPlayVoiceCommand(std::move(command));
                    break;
                case AudioCommandType::StopVoice:
                    ApplyStopVoiceCommand(command.VoiceId);
                    break;
                case AudioCommandType::SetVoiceMixParameters:
                    (void)ApplySetVoiceMixParametersCommand(
                        command.VoiceId,
                        command.Volume,
                        command.Pan,
                        command.MixerGroup,
                        command.Pitch);
                    break;
                case AudioCommandType::SetMixerGroupVolume:
                    if (!command.MixerGroup.empty())
                        m_MixerGroupVolumes[command.MixerGroup] = std::max(0.0f, command.Volume);
                    break;
                case AudioCommandType::SetMixerGroupReverbSend:
                    if (!command.MixerGroup.empty())
                        m_MixerGroupReverbSends[command.MixerGroup] = std::clamp(command.Volume, 0.0f, 1.0f);
                    break;
                case AudioCommandType::StopAll:
                    for (auto& v : m_Voices)
                    {
                        v.Active = false;
                        if (v.ActivityState)
                            v.ActivityState->Active.store(false, std::memory_order_release);
                        v.Clip.reset();
                        v.FrameCursor = 0.0;
                    }
                    m_Voices.clear();
                    break;
            }
        }
    }

    void AudioEngine::ApplyPlayVoiceCommand(AudioCommand&& command)
    {
        if (!command.Clip || command.Clip->Samples.empty())
        {
            if (command.ActivityState)
                command.ActivityState->Active.store(false, std::memory_order_release);
            return;
        }

        if (command.ActivityState)
            command.ActivityState->Active.store(true, std::memory_order_release);

        for (auto& v : m_Voices)
        {
            if (!v.Active && !v.Clip)
            {
                v.Id = command.VoiceId;
                v.Clip = std::move(command.Clip);
                v.ActivityState = std::move(command.ActivityState);
                v.FrameCursor = 0.0;
                v.Volume = std::max(0.0f, command.Volume);
                v.Pan = std::clamp(command.Pan, -1.0f, 1.0f);
                v.Pitch = std::max(0.01f, command.Pitch);
                v.MixerGroup = command.MixerGroup.empty() ? "Master" : command.MixerGroup;
                v.Loop = command.Loop;
                v.Active = true;
                return;
            }
        }

        Voice v{};
        v.Id = command.VoiceId;
        v.Clip = std::move(command.Clip);
        v.ActivityState = std::move(command.ActivityState);
        v.FrameCursor = 0.0;
        v.Volume = std::max(0.0f, command.Volume);
        v.Pan = std::clamp(command.Pan, -1.0f, 1.0f);
        v.Pitch = std::max(0.01f, command.Pitch);
        v.MixerGroup = command.MixerGroup.empty() ? "Master" : command.MixerGroup;
        v.Loop = command.Loop;
        v.Active = true;
        m_Voices.emplace_back(std::move(v));
    }

    void AudioEngine::ApplyStopVoiceCommand(uint32_t voiceId)
    {
        if (voiceId == 0)
            return;

        for (auto& v : m_Voices)
        {
            if (v.Id == voiceId)
            {
                v.Active = false;
                if (v.ActivityState)
                    v.ActivityState->Active.store(false, std::memory_order_release);
                v.Clip.reset();
                v.FrameCursor = 0.0;
                return;
            }
        }
    }

    bool AudioEngine::ApplySetVoiceMixParametersCommand(uint32_t voiceId, float volume, float pan, const std::string& mixerGroup, float pitch)
    {
        if (voiceId == 0)
            return false;

        for (auto& v : m_Voices)
        {
            if (v.Id == voiceId && v.Active && v.Clip)
            {
                v.Volume = std::max(0.0f, volume);
                v.Pan = std::clamp(pan, -1.0f, 1.0f);
                v.Pitch = std::max(0.01f, pitch);
                v.MixerGroup = mixerGroup.empty() ? "Master" : mixerGroup;
                return true;
            }
        }

        return false;
    }

    void AudioEngine::Mix(float* outInterleavedStereoF32, uint32_t frameCount)
    {
        // Clear output to silence.
        const uint32_t sampleCount = frameCount * kMixerChannels;
        for (uint32_t i = 0; i < sampleCount; ++i)
        {
            outInterleavedStereoF32[i] = 0.0f;
            if (i < m_ReverbSendScratch.size())
                m_ReverbSendScratch[i] = 0.0f;
        }

        // Callback thread owns voice runtime state and drains commands before mixing.
        DrainPendingCommands();

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
                if (v.ActivityState)
                    v.ActivityState->Active.store(false, std::memory_order_release);
                v.Clip.reset();
                continue;
            }

            double cursor = std::max(0.0, v.FrameCursor);
            const float mixerGroupVolume = ResolveMixerGroupVolume(v.MixerGroup);
            const float reverbSendAmount = ResolveMixerGroupReverbSend(v.MixerGroup);
            const float voiceVolume = v.Volume * mixerGroupVolume;
            const float clampedPan = std::clamp(v.Pan, -1.0f, 1.0f);
            const float clampedPitch = std::max(0.01f, v.Pitch);
            constexpr float kHalfPi = 1.57079632679f;
            const float panAngle = (clampedPan + 1.0f) * 0.5f * kHalfPi;
            const float panLeftGain = std::cos(panAngle);
            const float panRightGain = std::sin(panAngle);
            const double clipFrameCount = static_cast<double>(clipFrames);

            for (uint32_t f = 0; f < frameCount; ++f)
            {
                if (cursor >= clipFrameCount)
                {
                    if (v.Loop)
                    {
                        cursor = std::fmod(cursor, clipFrameCount);
                        if (cursor < 0.0)
                            cursor += clipFrameCount;
                    }
                    else
                    {
                        v.Active = false;
                        if (v.ActivityState)
                            v.ActivityState->Active.store(false, std::memory_order_release);
                        v.Clip.reset();
                        break;
                    }
                }

                const uint64_t frameIndex0 = static_cast<uint64_t>(cursor);
                uint64_t frameIndex1 = frameIndex0 + 1ull;
                if (frameIndex1 >= clipFrames)
                    frameIndex1 = v.Loop ? 0ull : frameIndex0;

                const float sampleAlpha = static_cast<float>(cursor - static_cast<double>(frameIndex0));
                const uint64_t base0 = frameIndex0 * kMixerChannels;
                const uint64_t base1 = frameIndex1 * kMixerChannels;
                const float sample0L = samples[static_cast<size_t>(base0 + 0)];
                const float sample0R = samples[static_cast<size_t>(base0 + 1)];
                const float sample1L = samples[static_cast<size_t>(base1 + 0)];
                const float sample1R = samples[static_cast<size_t>(base1 + 1)];
                const float inL = sample0L + (sample1L - sample0L) * sampleAlpha;
                const float inR = sample0R + (sample1R - sample0R) * sampleAlpha;

                const uint32_t outBase = f * kMixerChannels;
                const float mixedL = inL * voiceVolume * panLeftGain;
                const float mixedR = inR * voiceVolume * panRightGain;
                outInterleavedStereoF32[outBase + 0] += mixedL;
                outInterleavedStereoF32[outBase + 1] += mixedR;
                if (outBase + 1 < m_ReverbSendScratch.size())
                {
                    m_ReverbSendScratch[outBase + 0] += mixedL * reverbSendAmount;
                    m_ReverbSendScratch[outBase + 1] += mixedR * reverbSendAmount;
                }

                cursor += static_cast<double>(clampedPitch);
            }

            v.FrameCursor = cursor;
        }

        // Minimal built-in reverb: one feedback delay return fed by per-group sends.
        if (!m_ReverbDelayLeft.empty() && !m_ReverbDelayRight.empty() && sampleCount <= m_ReverbSendScratch.size())
        {
            const size_t delayBufferSize = m_ReverbDelayLeft.size();
            if (delayBufferSize == m_ReverbDelayRight.size())
            {
                size_t delayCursor = static_cast<size_t>(m_ReverbDelayCursor) % delayBufferSize;
                for (uint32_t f = 0; f < frameCount; ++f)
                {
                    const uint32_t base = f * kMixerChannels;
                    const float feedbackL = m_ReverbDelayLeft[delayCursor];
                    const float feedbackR = m_ReverbDelayRight[delayCursor];
                    const float reverbInL = m_ReverbSendScratch[base + 0] + feedbackL * m_ReverbFeedback;
                    const float reverbInR = m_ReverbSendScratch[base + 1] + feedbackR * m_ReverbFeedback;
                    m_ReverbDelayLeft[delayCursor] = reverbInL;
                    m_ReverbDelayRight[delayCursor] = reverbInR;
                    outInterleavedStereoF32[base + 0] += feedbackL * m_ReverbWetMix;
                    outInterleavedStereoF32[base + 1] += feedbackR * m_ReverbWetMix;
                    delayCursor = (delayCursor + 1) % delayBufferSize;
                }
                m_ReverbDelayCursor = static_cast<uint32_t>(delayCursor);
            }
        }

        // Apply master volume and clamp (cheap hard clip).
        const float master = m_MasterVolume.load(std::memory_order_acquire);
        for (uint32_t i = 0; i < sampleCount; ++i)
        {
            float x = outInterleavedStereoF32[i] * master;
            x = std::max(-1.0f, std::min(1.0f, x));
            outInterleavedStereoF32[i] = x;
        }
    }

    float AudioEngine::ResolveMixerGroupVolume(const std::string& mixerGroup) const
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

    float AudioEngine::ResolveMixerGroupReverbSend(const std::string& mixerGroup) const
    {
        if (mixerGroup.empty())
            return 0.0f;

        const auto it = m_MixerGroupReverbSends.find(mixerGroup);
        if (it == m_MixerGroupReverbSends.end())
            return 0.0f;
        return std::clamp(it->second, 0.0f, 1.0f);
    }
}

