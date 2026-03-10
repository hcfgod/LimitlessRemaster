#include "GameLayer.h"
#include "GameLayerInternal.h"

#include "Assets/AssetBundle.h"
#include "Audio/AudioEngine.h"
#include "Audio/AudioMixerAsset.h"
#include "Core/Application.h"
#include "Core/Debug/Log.h"
#include "Core/Input/InputSystem.h"
#include "Graphics/Lighting2DRenderer.h"
#include "Platform/Platform.h"
#include "Scene/SceneRenderer.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace Limitless
{
    namespace GameLayerInternal
    {
        void ApplyRuntimeProjectSettingsFromBundle()
        {
            auto& bundle = Assets::AssetBundle::GetInstance();
            if (!bundle.IsEnabled() || !bundle.IsLoaded())
                return;

            if (const auto renderSettingsText = bundle.ReadAllTextByKey("Project/Settings/RenderSettings.json");
                renderSettingsText.IsSuccess())
            {
                try
                {
                    const nlohmann::json renderRoot = nlohmann::json::parse(renderSettingsText.GetValue());
                    if (renderRoot.contains("clearColor") && renderRoot["clearColor"].is_array())
                    {
                        const auto& clear = renderRoot["clearColor"];
                        if (clear.size() >= 4)
                        {
                            SceneRenderer::SetViewportClearColor(glm::vec4(
                                clear[0].get<float>(),
                                clear[1].get<float>(),
                                clear[2].get<float>(),
                                clear[3].get<float>()));
                        }
                    }

                    if (renderRoot.contains("vSync") && renderRoot["vSync"].is_boolean())
                        Application::GetInstance().GetWindow().SetVSync(renderRoot["vSync"].get<bool>());
                }
                catch (const std::exception& e)
                {
                    LT_WARN("GameLayer: failed parsing RenderSettings.json: {}", e.what());
                }
            }

            if (const auto audioSettingsText = bundle.ReadAllTextByKey("Project/Settings/AudioSettings.json");
                audioSettingsText.IsSuccess())
            {
                try
                {
                    const nlohmann::json audioRoot = nlohmann::json::parse(audioSettingsText.GetValue());
                    Audio::AudioEngine& audioEngine = Audio::AudioEngine::GetInstance();
                    const bool muted = audioRoot.value("muted", false);
                    const float masterVolume = muted ? 0.0f : std::max(0.0f, audioRoot.value("masterVolume", 1.0f));
                    audioEngine.SetMasterVolume(masterVolume);

                    const std::string mixerAssetKey = audioRoot.value("mixerAssetKey", std::string{});
                    if (!mixerAssetKey.empty())
                    {
                        Audio::AudioMixerDefinition mixerDefinition{};
                        if (Audio::LoadAudioMixerDefinitionFromAssetKey(mixerAssetKey, mixerDefinition))
                        {
                            for (const auto& group : mixerDefinition.Groups)
                            {
                                audioEngine.SetMixerGroupVolume(group.Name, group.Volume);
                                audioEngine.SetMixerGroupReverbSend(group.Name, group.ReverbSend);
                            }
                        }
                    }
                }
                catch (const std::exception& e)
                {
                    LT_WARN("GameLayer: failed parsing AudioSettings.json: {}", e.what());
                }
            }

            if (const auto lightingSettingsText = bundle.ReadAllTextByKey("Project/Settings/Lighting2DSettings.json");
                lightingSettingsText.IsSuccess())
            {
                try
                {
                    const nlohmann::json lightingRoot = nlohmann::json::parse(lightingSettingsText.GetValue());
                    Lighting2DSettings lightingSettings{};
                    lightingSettings.Enabled = lightingRoot.value("enabled", true);
                    lightingSettings.EnableNormalMaps = lightingRoot.value("enableNormalMaps", true);
                    lightingSettings.EnableShadows = lightingRoot.value("enableShadows", true);
                    lightingSettings.AmbientIntensity = std::max(0.0f, lightingRoot.value("ambientIntensity", 0.6f));
                    lightingSettings.ShadowQualityLevel = std::clamp(lightingRoot.value("shadowQualityLevel", 1), 0, 2);
                    lightingSettings.MaxDirectionalLights = std::max(0, lightingRoot.value("maxDirectionalLights", 4));
                    lightingSettings.MaxPointLights = std::max(0, lightingRoot.value("maxPointLights", 32));
                    lightingSettings.MaxShadowSegments = std::max(1, lightingRoot.value("maxShadowSegments", 128));
                    lightingSettings.ShadowSoftnessScale = std::max(0.0f, lightingRoot.value("shadowSoftnessScale", 1.0f));
                    lightingSettings.DirectionalShadowBiasScale = std::max(0.0f, lightingRoot.value("directionalShadowBiasScale", 1.0f));
                    lightingSettings.ShadowAlphaCutoff = std::clamp(lightingRoot.value("shadowAlphaCutoff", 0.5f), 0.0f, 1.0f);
                    lightingSettings.ShadowSegmentSnapPixels = std::max(0.0f, lightingRoot.value("shadowSegmentSnapPixels", 0.75f));
                    lightingSettings.EnableHighAngularVelocityShadowFreeze = lightingRoot.value("enableHighAngularVelocityShadowFreeze", true);
                    lightingSettings.ShadowFreezeAngularVelocityDegreesPerSecond = std::max(1.0f, lightingRoot.value("shadowFreezeAngularVelocityDegreesPerSecond", 180.0f));
                    lightingSettings.ShadowFreezeFrameCount = std::max(1, lightingRoot.value("shadowFreezeFrameCount", 2));
                    lightingSettings.MaxShadowSamplesPerLight = std::max(1, lightingRoot.value("maxShadowSamplesPerLight", 12));
                    if (lightingRoot.contains("ambientColor") && lightingRoot["ambientColor"].is_array())
                    {
                        const auto& ambientColor = lightingRoot["ambientColor"];
                        if (ambientColor.size() >= 3)
                        {
                            lightingSettings.AmbientColor = glm::vec3(
                                ambientColor[0].get<float>(),
                                ambientColor[1].get<float>(),
                                ambientColor[2].get<float>());
                        }
                    }

                    Lighting2DRenderer::Default().SetSettings(lightingSettings);
                }
                catch (const std::exception& e)
                {
                    LT_WARN("GameLayer: failed parsing Lighting2DSettings.json: {}", e.what());
                }
            }

            if (const auto inputSettingsText = bundle.ReadAllTextByKey("Project/Settings/InputSettings.json");
                inputSettingsText.IsSuccess())
            {
                try
                {
                    const nlohmann::json inputRoot = nlohmann::json::parse(inputSettingsText.GetValue());
                    const std::string projectInputActionsKey = inputRoot.value("projectInputActionsKey", std::string{});
                    InputSystem& inputSystem = Application::GetInstance().GetInputSystem();
                    if (!projectInputActionsKey.empty())
                        inputSystem.SetProjectActionAssetFromKey(projectInputActionsKey);
                    else
                        inputSystem.SetProjectActionAsset(nullptr);

                    std::vector<std::string> additionalInputActionKeys;
                    if (inputRoot.contains("additionalInputActionsAssets") && inputRoot["additionalInputActionsAssets"].is_array())
                    {
                        for (const auto& value : inputRoot["additionalInputActionsAssets"])
                        {
                            if (value.is_object())
                            {
                                const std::string key = value.value("assetKey", std::string{});
                                if (!key.empty())
                                    additionalInputActionKeys.push_back(key);
                            }
                            else if (value.is_string())
                            {
                                const std::string key = value.get<std::string>();
                                if (!key.empty())
                                    additionalInputActionKeys.push_back(key);
                            }
                        }
                    }

                    if (inputRoot.contains("additionalInputActionsKeys") && inputRoot["additionalInputActionsKeys"].is_array())
                    {
                        for (const auto& value : inputRoot["additionalInputActionsKeys"])
                        {
                            if (!value.is_string())
                                continue;
                            const std::string key = value.get<std::string>();
                            if (!key.empty())
                                additionalInputActionKeys.push_back(key);
                        }
                    }

                    std::sort(additionalInputActionKeys.begin(), additionalInputActionKeys.end());
                    additionalInputActionKeys.erase(
                        std::unique(additionalInputActionKeys.begin(), additionalInputActionKeys.end()),
                        additionalInputActionKeys.end());
                    inputSystem.SetProjectAdditionalActionAssetsFromKeys(additionalInputActionKeys);
                }
                catch (const std::exception& e)
                {
                    LT_WARN("GameLayer: failed parsing InputSettings.json: {}", e.what());
                }
            }
        }
    }

    bool GameLayer::LoadBootstrap()
    {
        std::filesystem::path bootstrapPath;
        const auto& platformInfo = PlatformDetection::GetPlatformInfo();
        if (!platformInfo.executablePath.empty())
            bootstrapPath = std::filesystem::path(platformInfo.executablePath).parent_path() / "GameBootstrap.json";

        if (bootstrapPath.empty() || !std::filesystem::exists(bootstrapPath))
            bootstrapPath = std::filesystem::current_path() / "GameBootstrap.json";

        if (!std::filesystem::exists(bootstrapPath))
        {
            LT_ERROR("GameLayer: GameBootstrap.json not found.");
            return false;
        }

        try
        {
            std::ifstream inputStream(bootstrapPath, std::ios::in | std::ios::binary);
            if (!inputStream.is_open())
                return false;

            nlohmann::json root;
            inputStream >> root;

            m_ProjectName = root.value("projectName", std::string{"Game"});
            m_StartupSceneKey = root.value("startupSceneKey", std::string{});
            m_BuildScenes.clear();

            if (root.contains("buildScenes") && root["buildScenes"].is_array())
            {
                for (const auto& scene : root["buildScenes"])
                {
                    if (scene.is_string())
                        m_BuildScenes.push_back(scene.get<std::string>());
                }
            }

            if (m_StartupSceneKey.empty() && !m_BuildScenes.empty())
                m_StartupSceneKey = m_BuildScenes.front();

            LT_INFO("GameLayer: loaded bootstrap for '{}' with {} scene(s).", m_ProjectName, m_BuildScenes.size());
            return !m_StartupSceneKey.empty();
        }
        catch (const std::exception& e)
        {
            LT_ERROR("GameLayer: failed to parse GameBootstrap.json: {}", e.what());
            return false;
        }
    }
}
