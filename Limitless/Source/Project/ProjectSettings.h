#pragma once

#include "Core/Error.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Limitless::Project
{
    struct RenderSettings final
    {
        uint32_t Version = 1;

        bool VSync = true;
        int MsaaSamples = 1;
        float RenderScale = 1.0f;

        // RGBA in linear space (editor-facing; the renderer can convert as needed).
        float ClearColor[4] = {0.08f, 0.08f, 0.10f, 1.0f};
    };

    struct AudioSettings final
    {
        uint32_t Version = 1;
        float MasterVolume = 1.0f;
        bool Muted = false;
    };

    struct InputSettings final
    {
        uint32_t Version = 1;

        // Project-wide default InputActions asset key (example: "Assets/InputActions/Sandbox.inputactions.json").
        std::string ProjectInputActionsKey;
    };

    struct LayersSettings final
    {
        uint32_t Version = 1;
        std::vector<std::string> Layers;
    };

    [[nodiscard]] std::filesystem::path GetProjectSettingsDirectory(const std::filesystem::path& projectRoot);

    [[nodiscard]] std::filesystem::path GetRenderSettingsPath(const std::filesystem::path& projectRoot);
    [[nodiscard]] std::filesystem::path GetAudioSettingsPath(const std::filesystem::path& projectRoot);
    [[nodiscard]] std::filesystem::path GetInputSettingsPath(const std::filesystem::path& projectRoot);
    [[nodiscard]] std::filesystem::path GetLayersSettingsPath(const std::filesystem::path& projectRoot);

    [[nodiscard]] Result<RenderSettings> LoadRenderSettings(const std::filesystem::path& projectRoot);
    [[nodiscard]] Result<AudioSettings> LoadAudioSettings(const std::filesystem::path& projectRoot);
    [[nodiscard]] Result<InputSettings> LoadInputSettings(const std::filesystem::path& projectRoot);
    [[nodiscard]] Result<LayersSettings> LoadLayersSettings(const std::filesystem::path& projectRoot);

    [[nodiscard]] Result<void> SaveRenderSettings(const std::filesystem::path& projectRoot, const RenderSettings& settings);
    [[nodiscard]] Result<void> SaveAudioSettings(const std::filesystem::path& projectRoot, const AudioSettings& settings);
    [[nodiscard]] Result<void> SaveInputSettings(const std::filesystem::path& projectRoot, const InputSettings& settings);
    [[nodiscard]] Result<void> SaveLayersSettings(const std::filesystem::path& projectRoot, const LayersSettings& settings);
}

