#pragma once

#include <filesystem>
#include <cstdint>
#include <string>
#include <vector>

namespace Limitless::Audio
{
    struct AudioMixerGroupEntry
    {
        std::string Name;
        float Volume = 1.0f;
        float ReverbSend = 0.0f;
    };

    struct AudioMixerDefinition
    {
        uint32_t Version = 1;
        std::vector<AudioMixerGroupEntry> Groups;
    };

    void NormalizeAudioMixerDefinition(AudioMixerDefinition& definition);

    bool LoadAudioMixerDefinitionFromAssetKey(const std::string& assetKey,
                                              AudioMixerDefinition& outDefinition,
                                              std::filesystem::path* outResolvedPath = nullptr);

    bool SaveAudioMixerDefinitionToPath(const std::filesystem::path& path,
                                        const AudioMixerDefinition& definition);
}
