#include "AudioDemo.h"

#include "Assets/AssetManager.h"
#include "Assets/AudioClipAssetImporter.h"

#include "Core/Debug/Log.h"

namespace Limitless
{
    void AudioDemo::Initialize(const Settings& settings)
    {
        m_Settings = settings;

        auto clip = Assets::AssetManager::LoadBlocking<Assets::AudioClipAsset>(m_Settings.ClipKey);
        if (!clip)
        {
            LT_WARN("AudioDemo: failed to load clip '{}'. Drop an audio file at that path (and rebuild AssetBundle if enabled).", m_Settings.ClipKey);
            return;
        }

        m_Source.SetClip(Assets::AssetHandle<Assets::AudioClipAsset>(clip));
        m_Source.SetVolume(1.0f);

        LT_INFO("AudioDemo: ready (clipGuid={})", clip->GetGuid());
    }

    void AudioDemo::Shutdown()
    {
        Stop();
    }

    void AudioDemo::Play()
    {
        if (!m_Source.Play())
        {
            LT_WARN("AudioDemo: Play failed (clipKey='{}')", m_Settings.ClipKey);
            return;
        }

        LT_INFO("AudioDemo: Play started (clipKey='{}')", m_Settings.ClipKey);
    }

    void AudioDemo::Stop()
    {
        m_Source.Stop();
    }
}

