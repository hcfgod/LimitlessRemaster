#pragma once

#include "Audio/AudioClip.h"
#include "Core/Error.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Limitless::Audio::Decoders
{
    // -----------------------------------------------------------------------------
    // FfmpegAudioDecoder
    //
    // Decodes an audio file to the engine mixer format:
    // - Interleaved float32 stereo at 48 kHz
    //
    // Build notes:
    // - This is compiled only when LT_ENABLE_FFMPEG is defined.
    // - On builds where FFmpeg libs are not available, Decode() will return an error.
    // -----------------------------------------------------------------------------
    class FfmpegAudioDecoder final
    {
    public:
        struct DecodeSettings
        {
            uint32_t TargetSampleRateHz = 48000;
            uint32_t TargetChannelCount = 2;
        };

        static Result<std::shared_ptr<AudioClip>> DecodeFromFile(const std::string& absolutePath, const DecodeSettings& settings = {});
        static Result<std::shared_ptr<AudioClip>> DecodeFromMemory(const uint8_t* bytes, size_t byteCount, const std::string& debugName, const DecodeSettings& settings = {});
    };
}

