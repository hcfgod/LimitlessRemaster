#pragma once

#include <cstdint>
#include <vector>

namespace Limitless::Audio
{
    // -----------------------------------------------------------------------------
    // AudioClip
    // Unity-style clip data container (decoded PCM).
    //
    // Engine policy:
    // - Mixer format is fixed to interleaved float32 stereo at 48 kHz.
    // - Decoders are expected to resample/remix into this format so the audio
    //   callback stays trivial and allocation-free.
    // -----------------------------------------------------------------------------
    struct AudioClip final
    {
        // Interleaved float32 stereo samples in range [-1, +1].
        // Layout: L, R, L, R, ...
        std::vector<float> Samples;

        uint32_t SampleRateHz = 48000;
        uint32_t ChannelCount = 2;

        uint64_t GetFrameCount() const
        {
            const uint64_t denom = static_cast<uint64_t>(ChannelCount);
            return denom == 0 ? 0 : (static_cast<uint64_t>(Samples.size()) / denom);
        }

        double GetDurationSeconds() const
        {
            const uint32_t sr = SampleRateHz;
            if (sr == 0)
            {
                return 0.0;
            }
            return static_cast<double>(GetFrameCount()) / static_cast<double>(sr);
        }
    };
}

