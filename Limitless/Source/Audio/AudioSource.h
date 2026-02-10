#pragma once

#include "Audio/AudioEngine.h"
#include "Assets/AudioClipAsset.h"
#include "Assets/AssetHandle.h"

#include <memory>

namespace Limitless::Audio
{
    // -----------------------------------------------------------------------------
    // AudioSource
    // Unity-style lightweight component-like object:
    // - Holds a reference to an AudioClip asset
    // - Plays/stops via AudioEngine
    //
    // This is intentionally minimal (no spatialization yet).
    // -----------------------------------------------------------------------------
    class AudioSource final
    {
    public:
        // Sets the referenced clip (Unity-style). The AudioSource will keep the asset alive
        // while referenced so weak-cache GC does not break playback.
        void SetClip(const Assets::AssetHandle<Assets::AudioClipAsset>& clip);
        const Assets::AssetHandle<Assets::AudioClipAsset>& GetClip() const { return m_Clip; }

        void SetVolume(float volume) { m_Volume = (volume < 0.0f) ? 0.0f : volume; }
        float GetVolume() const { return m_Volume; }

        // Starts playback (one-shot for now).
        // Returns true if a voice started.
        bool Play();

        void Stop();

        bool IsPlaying() const { return m_VoiceId != 0; }

    private:
        Assets::AssetHandle<Assets::AudioClipAsset> m_Clip;
        std::shared_ptr<Assets::AudioClipAsset> m_ClipAssetPinned;
        float m_Volume = 1.0f;
        uint32_t m_VoiceId = 0;
    };
}

