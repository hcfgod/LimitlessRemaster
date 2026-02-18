#pragma once

#include "Core/Error.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Limitless::Project
{
    struct InputActionsAssetAliasEntry final
    {
        // Logical name used by code (example: "Gameplay", "Vehicle", "UiNavigation").
        std::string Alias;

        // Unity-style asset key (example: "Assets/InputActions/Gameplay.inputactions.json").
        std::string AssetKey;
    };

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
        std::string MixerAssetKey;
    };

    struct InputSettings final
    {
        uint32_t Version = 1;

        // Project-wide default InputActions asset key (example: "Assets/InputActions/Runtime.inputactions.json").
        std::string ProjectInputActionsKey;

        // Additional project-level InputActions assets with logical aliases.
        // This is the canonical representation used by tooling/runtime helpers.
        std::vector<InputActionsAssetAliasEntry> AdditionalInputActionsAssets;

        // Legacy compatibility list used by older settings files.
        // New code should prefer AdditionalInputActionsAssets.
        std::vector<std::string> AdditionalInputActionsKeys;
    };

    struct LayersSettings final
    {
        uint32_t Version = 1;
        std::vector<std::string> Layers;
    };

    struct Physics2DSettings final
    {
        uint32_t Version = 1;
        float GravityX = 0.0f;
        float GravityY = -9.81f;
        int VelocitySubSteps = 8;
        bool EnableSleep = true;
        bool EnableContinuousCollision = true;
        bool HighContactQualityMode = false;
        int HighContactQualityExtraSubSteps = 4;
        float ContactHertz = 90.0f;
        float ContactDampingRatio = 1.0f;
        float ContactPushSpeed = 8.0f;
    };

    struct Lighting2DSettings final
    {
        uint32_t Version = 1;
        bool Enabled = true;
        bool EnableNormalMaps = true;
        bool EnableShadows = true;
        float AmbientColor[3] = { 0.12f, 0.12f, 0.14f };
        float AmbientIntensity = 0.6f;
        int ShadowQualityLevel = 1;
        int MaxDirectionalLights = 4;
        int MaxPointLights = 32;
        int MaxShadowSegments = 128;
        float ShadowSoftnessScale = 1.0f;
        float DirectionalShadowBiasScale = 1.0f;
        float ShadowAlphaCutoff = 0.5f;
        float ShadowSegmentSnapPixels = 0.75f;
        bool EnableHighAngularVelocityShadowFreeze = true;
        float ShadowFreezeAngularVelocityDegreesPerSecond = 180.0f;
        int ShadowFreezeFrameCount = 2;
        int MaxShadowSamplesPerLight = 12;
    };

    [[nodiscard]] std::filesystem::path GetProjectSettingsDirectory(const std::filesystem::path& projectRoot);

    [[nodiscard]] std::filesystem::path GetRenderSettingsPath(const std::filesystem::path& projectRoot);
    [[nodiscard]] std::filesystem::path GetAudioSettingsPath(const std::filesystem::path& projectRoot);
    [[nodiscard]] std::filesystem::path GetInputSettingsPath(const std::filesystem::path& projectRoot);
    [[nodiscard]] std::filesystem::path GetLayersSettingsPath(const std::filesystem::path& projectRoot);
    [[nodiscard]] std::filesystem::path GetPhysics2DSettingsPath(const std::filesystem::path& projectRoot);
    [[nodiscard]] std::filesystem::path GetLighting2DSettingsPath(const std::filesystem::path& projectRoot);

    [[nodiscard]] Result<RenderSettings> LoadRenderSettings(const std::filesystem::path& projectRoot);
    [[nodiscard]] Result<AudioSettings> LoadAudioSettings(const std::filesystem::path& projectRoot);
    [[nodiscard]] Result<InputSettings> LoadInputSettings(const std::filesystem::path& projectRoot);
    [[nodiscard]] Result<LayersSettings> LoadLayersSettings(const std::filesystem::path& projectRoot);
    [[nodiscard]] Result<Physics2DSettings> LoadPhysics2DSettings(const std::filesystem::path& projectRoot);
    [[nodiscard]] Result<Lighting2DSettings> LoadLighting2DSettings(const std::filesystem::path& projectRoot);

    [[nodiscard]] Result<void> SaveRenderSettings(const std::filesystem::path& projectRoot, const RenderSettings& settings);
    [[nodiscard]] Result<void> SaveAudioSettings(const std::filesystem::path& projectRoot, const AudioSettings& settings);
    [[nodiscard]] Result<void> SaveInputSettings(const std::filesystem::path& projectRoot, const InputSettings& settings);
    [[nodiscard]] Result<void> SaveLayersSettings(const std::filesystem::path& projectRoot, const LayersSettings& settings);
    [[nodiscard]] Result<void> SavePhysics2DSettings(const std::filesystem::path& projectRoot, const Physics2DSettings& settings);
    [[nodiscard]] Result<void> SaveLighting2DSettings(const std::filesystem::path& projectRoot, const Lighting2DSettings& settings);

    // Returns a de-duplicated list of additional InputActions keys, merged from canonical and legacy fields.
    [[nodiscard]] std::vector<std::string> CollectAdditionalInputActionsAssetKeys(const InputSettings& settings);

    // Resolve an InputActions key by logical alias (case-insensitive).
    // Built-in alias "Default" resolves ProjectInputActionsKey.
    [[nodiscard]] Result<std::string> ResolveInputActionsAssetKeyByAlias(const InputSettings& settings, const std::string& alias);
    [[nodiscard]] Result<std::string> ResolveInputActionsAssetKeyByAlias(const std::filesystem::path& projectRoot, const std::string& alias);
}

