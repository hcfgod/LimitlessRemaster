#include "Audio/AudioSource.h"

#include "Assets/AudioClipAsset.h"
#include "Core/Debug/Log.h"

namespace Limitless::Audio
{
    void AudioSource::SetClip(const Assets::AssetHandle<Assets::AudioClipAsset>& clip)
    {
        m_Clip = clip;
        m_ClipAssetPinned = m_Clip.Lock();
    }

    bool AudioSource::Play()
    {
        auto clipAsset = m_ClipAssetPinned ? m_ClipAssetPinned : m_Clip.Lock();
        if (!clipAsset)
        {
            LT_CORE_WARN("AudioSource::Play: no AudioClipAsset set/loaded");
            return false;
        }

        // If we resolved through the handle, pin the asset so it stays alive for future calls.
        if (!m_ClipAssetPinned)
        {
            m_ClipAssetPinned = clipAsset;
        }

        const auto clip = clipAsset->GetClip();
        if (!clip)
        {
            LT_CORE_WARN("AudioSource::Play: AudioClipAsset has no decoded clip data");
            return false;
        }

        auto& engine = AudioEngine::GetInstance();
        if (!engine.IsInitialized())
        {
            LT_CORE_WARN("AudioSource::Play: AudioEngine not initialized");
            return false;
        }

        m_VoiceId = engine.PlayOneShot(clip, m_Volume);
        return m_VoiceId != 0;
    }

    void AudioSource::Stop()
    {
        if (m_VoiceId == 0)
        {
            return;
        }

        AudioEngine::GetInstance().Stop(m_VoiceId);
        m_VoiceId = 0;
    }
}

