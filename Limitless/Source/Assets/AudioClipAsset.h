#pragma once

#include "Assets/Asset.h"
#include "Assets/AssetManager.h"

#include "Audio/AudioClip.h"

#include "Core/Concurrency/AsyncIO.h"

#include <memory>
#include <string>

namespace Limitless::Assets
{
    // -----------------------------------------------------------------------------
    // AudioClipAsset
    // Unity-style audio clip asset:
    // - Decoded to engine mixer format (float32 stereo @ 48 kHz).
    // - Decode happens on AsyncIO.
    //
    // Notes:
    // - Uses FFmpeg when available (LT_ENABLE_FFMPEG).
    // - Designed to support AssetBundle memory loading in shipped builds.
    // -----------------------------------------------------------------------------
    class AudioClipAsset final : public Limitless::Asset
    {
    public:
        using Ptr = std::shared_ptr<AudioClipAsset>;

        struct Settings
        {
            uint32_t TargetSampleRateHz = 48000;
            uint32_t TargetChannelCount = 2;
        };

        static Async::Task<Ptr> LoadAsync(const std::string& assetPath, const Settings& settings = {});
        static Async::Task<Ptr> LoadAsync(const std::string& assetPath, const Settings& settings, uint64_t generation);
        static Ptr LoadBlocking(const std::string& assetPath, const Settings& settings = {});

        std::shared_ptr<const Audio::AudioClip> GetClip() const { return m_Clip; }

        bool Reload() override;

    private:
        AudioClipAsset(std::string key, std::string guid, std::shared_ptr<const Audio::AudioClip> clip, Settings settings)
            : Asset(std::move(key), std::move(guid))
            , m_Clip(std::move(clip))
            , m_Settings(settings)
        {
        }

    private:
        std::shared_ptr<const Audio::AudioClip> m_Clip;
        Settings m_Settings{};
    };
}

