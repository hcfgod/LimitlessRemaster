#include "GameLayer.h"

#include "Assets/AudioClipAsset.h"
#include "Assets/AssetBundle.h"
#include "Assets/AssetManager.h"
#include "Audio/AudioEngine.h"
#include "Audio/AudioMixerAsset.h"
#include "Core/Application.h"
#include "Core/Input/InputSystem.h"
#include "Graphics/Camera/Camera.h"
#include "Graphics/Camera/CameraManager.h"
#include "Graphics/Lighting2DRenderer.h"
#include "Graphics/RenderCommand.h"
#include "Graphics/Renderer.h"
#include "Platform/Platform.h"
#include "Physics/Physics2DQueries.h"
#include "Scene/Components.h"
#include "Scene/Scene.h"
#include "Scene/SceneManager.h"
#include "Scripting/NativeScriptRegistry.h"
#include "Scripting/ScriptableEntity.h"
#include "Scripting/ScriptCoreApi.h"
#include "Scripting/Debug.h"
#include "Scripting/Input.h"
#include "Scripting/InputActions.h"
#include "Scripting/Physics2D.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace Limitless
{
    // -------------------------------------------------------------------------
    // ScriptCore bridge callbacks (matching signatures from engine typedefs)
    // -------------------------------------------------------------------------
    namespace
    {
        using RegisterScriptCoreTypesFunction = void (*)(NativeScriptRegistrationCallback registrationCallback);
        using GetScriptCoreAbiVersionFunction = uint32_t (*)();
        using SetSceneTransitionBridgeFunction = void (*)(SceneTransitionBridgeCallback callback);
        using SetInputActionAxis1DBridgeFunction = void (*)(InputActionAxis1DBridgeCallback callback);
        using SetInputActionAxis2DBridgeFunction = void (*)(InputActionAxis2DBridgeCallback callback);
        using SetInputActionExistsBridgeFunction = void (*)(InputActionExistsBridgeCallback callback);
        using SetInputActionPressedBridgeFunction = void (*)(InputActionPressedBridgeCallback callback);
        using SetInputActionTriggerBridgeFunction = void (*)(InputActionTriggerBridgeCallback callback);
        using SetPhysics2DRaycastBridgeFunction = void (*)(Physics2DRaycastBridgeCallback callback);
        using SetScriptLogBridgeFunction = void (*)(ScriptLogBridgeCallback callback);
        using SetScriptCreateEntityBridgeFunction = void (*)(ScriptCreateEntityBridgeCallback callback);
        using SetScriptDestroyEntityBridgeFunction = void (*)(ScriptDestroyEntityBridgeCallback callback);

        Scene* s_ActiveScene = nullptr;

        bool ForwardSceneTransitionToHost(SceneTransitionType transitionType, const char* sceneIdentifier)
        {
            switch (transitionType)
            {
                case SceneTransitionType::LoadByAssetKey:
                    if (!sceneIdentifier) return false;
                    return SceneManager::LoadScene(sceneIdentifier);
                case SceneTransitionType::ReloadCurrentScene:
                    return SceneManager::ReloadCurrentScene();
            }
            return false;
        }

        float ForwardInputActionAxis1DToHost(const char* mapName, const char* actionName)
        {
            const std::string_view safeMap = mapName ? std::string_view(mapName) : std::string_view();
            const std::string_view safeAction = actionName ? std::string_view(actionName) : std::string_view();
            return InputSystem::GetInstance().ReadActionAxis1D(safeMap, safeAction);
        }

        glm::vec2 ForwardInputActionAxis2DToHost(const char* mapName, const char* actionName)
        {
            const std::string_view safeMap = mapName ? std::string_view(mapName) : std::string_view();
            const std::string_view safeAction = actionName ? std::string_view(actionName) : std::string_view();
            return InputSystem::GetInstance().ReadActionAxis2D(safeMap, safeAction);
        }

        bool ForwardInputActionExistsToHost(const char* mapName, const char* actionName)
        {
            const std::string_view safeMap = mapName ? std::string_view(mapName) : std::string_view();
            const std::string_view safeAction = actionName ? std::string_view(actionName) : std::string_view();
            return InputSystem::GetInstance().HasAction(safeMap, safeAction);
        }

        bool ForwardInputActionPressedToHost(const char* mapName, const char* actionName, float deadzone)
        {
            const std::string_view safeMap = mapName ? std::string_view(mapName) : std::string_view();
            const std::string_view safeAction = actionName ? std::string_view(actionName) : std::string_view();
            return InputSystem::GetInstance().IsActionPressed(safeMap, safeAction, deadzone);
        }

        bool ForwardInputActionStartedToHost(const char* mapName, const char* actionName)
        {
            const std::string_view safeMap = mapName ? std::string_view(mapName) : std::string_view();
            const std::string_view safeAction = actionName ? std::string_view(actionName) : std::string_view();
            return InputSystem::GetInstance().WasActionStartedThisFrame(safeMap, safeAction);
        }

        bool ForwardInputActionPerformedToHost(const char* mapName, const char* actionName)
        {
            const std::string_view safeMap = mapName ? std::string_view(mapName) : std::string_view();
            const std::string_view safeAction = actionName ? std::string_view(actionName) : std::string_view();
            return InputSystem::GetInstance().WasActionPerformedThisFrame(safeMap, safeAction);
        }

        bool ForwardInputActionCanceledToHost(const char* mapName, const char* actionName)
        {
            const std::string_view safeMap = mapName ? std::string_view(mapName) : std::string_view();
            const std::string_view safeAction = actionName ? std::string_view(actionName) : std::string_view();
            return InputSystem::GetInstance().WasActionCanceledThisFrame(safeMap, safeAction);
        }

        bool ForwardInputActionButtonToHost(const char* mapName, const char* actionName)
        {
            const std::string_view safeMap = mapName ? std::string_view(mapName) : std::string_view();
            const std::string_view safeAction = actionName ? std::string_view(actionName) : std::string_view();
            return InputSystem::GetInstance().ReadActionButton(safeMap, safeAction);
        }

        bool ForwardPhysics2DRaycastToHost(float originX, float originY,
                                           float directionX, float directionY,
                                           float maxDistance,
                                           uint64_t collisionMask,
                                           RaycastHit2D* outHit)
        {
            if (!outHit) return false;
            *outHit = RaycastHit2D{};

            if (!s_ActiveScene) return false;

            const Physics2DRaycastHit nativeHit = Physics2DQueries::RaycastClosest(
                s_ActiveScene,
                glm::vec2(originX, originY),
                glm::vec2(directionX, directionY),
                maxDistance,
                collisionMask);

            if (!nativeHit.HasHit) return false;

            outHit->HasHit = true;
            outHit->Entity = nativeHit.Entity;
            outHit->Point = nativeHit.Point;
            outHit->Normal = nativeHit.Normal;
            outHit->Fraction = nativeHit.Fraction;
            return true;
        }

        void ForwardScriptLogToHost(ScriptLogSeverity severity, const char* message)
        {
            const std::string safeMessage = message ? message : "";
            switch (severity)
            {
                case ScriptLogSeverity::Info:
                    LT_INFO("[Script] {}", safeMessage);
                    break;
                case ScriptLogSeverity::Warning:
                    LT_WARN("[Script] {}", safeMessage);
                    break;
                case ScriptLogSeverity::Error:
                    LT_ERROR("[Script] {}", safeMessage);
                    break;
            }
        }

        entt::entity ForwardScriptCreateEntityToHost(const char* name)
        {
            if (!s_ActiveScene) return entt::null;
            if (!name || name[0] == '\0')
                return s_ActiveScene->CreateEntity("Entity");
            return s_ActiveScene->CreateEntity(name);
        }

        void ForwardScriptDestroyEntityToHost(entt::entity entity)
        {
            if (!s_ActiveScene) return;
            s_ActiveScene->DestroyEntity(entity);
        }

        entt::entity ForwardScriptInstantiatePrefabToHost(const char* prefabAssetKey, entt::entity parentEntity)
        {
            if (!s_ActiveScene || !prefabAssetKey || prefabAssetKey[0] == '\0')
                return entt::null;
            return s_ActiveScene->InstantiatePrefab(prefabAssetKey, parentEntity);
        }

        uint32_t ForwardScriptContactEntityHandlesToHost(entt::entity entity,
                                                         bool includeSensorContacts,
                                                         entt::entity* outHandles,
                                                         uint32_t capacity)
        {
            if (!s_ActiveScene || entity == entt::null)
                return 0u;
            auto& registry = s_ActiveScene->GetRegistry();
            if (!registry.valid(entity))
                return 0u;

            const Physics2DContactListener* contacts = s_ActiveScene->GetPhysics2DContactEventsForEntity(entity);
            if (!contacts)
                return 0u;

            std::vector<entt::entity> uniqueContacts;
            std::unordered_set<entt::entity> seenContacts;
            const auto& events = contacts->GetEvents();
            for (const auto& eventData : events)
            {
                if (!includeSensorContacts && eventData.IsSensor)
                    continue;

                entt::entity other = entt::null;
                if (eventData.EntityA == entity)
                    other = eventData.EntityB;
                else if (eventData.EntityB == entity)
                    other = eventData.EntityA;

                if (other == entt::null || !registry.valid(other))
                    continue;
                if (seenContacts.insert(other).second)
                    uniqueContacts.push_back(other);
            }

            const uint32_t totalCount = static_cast<uint32_t>(uniqueContacts.size());
            if (!outHandles || capacity == 0u)
                return totalCount;

            const uint32_t toWrite = std::min(totalCount, capacity);
            for (uint32_t index = 0; index < toWrite; ++index)
                outHandles[index] = uniqueContacts[static_cast<size_t>(index)];
            return toWrite;
        }

        bool ForwardScriptParallelExecutionStateToHost()
        {
            return Scene::IsCurrentThreadParallelScriptExecution();
        }

        void RegisterScriptFromModule(const char* className, NativeScriptCreateFunction createFunction)
        {
            if (className && createFunction)
                NativeScriptRegistry::RegisterScript(className, createFunction);
        }

        std::string GetScriptCoreLibraryFileName()
        {
#if defined(LT_PLATFORM_WINDOWS)
            return "ScriptCore.dll";
#elif defined(LT_PLATFORM_MACOS)
            return "libScriptCore.dylib";
#else
            return "libScriptCore.so";
#endif
        }

        struct AudioListener2DRuntimeState
        {
            bool HasListener = false;
            glm::vec2 Position = glm::vec2(0.0f);
        };

        using AudioListenerPositions2D = std::vector<glm::vec2>;

        struct AudioSpatialMix2D
        {
            float Gain = 1.0f;
            float Pan = 0.0f;
        };

        glm::vec2 ComputeEntityWorldPosition2D(const Scene& scene, entt::entity entity)
        {
            const glm::mat4 worldTransform = scene.GetWorldTransformMatrix(entity);
            return glm::vec2(worldTransform[3][0], worldTransform[3][1]);
        }

        bool TryFindPrimaryCameraEntity(const Scene& scene, entt::entity& outEntity)
        {
            const auto& registry = scene.GetRegistry();
            auto cameraView = registry.view<CameraComponent>();
            entt::entity fallbackEntity = entt::null;
            for (entt::entity entity : cameraView)
            {
                if (fallbackEntity == entt::null)
                    fallbackEntity = entity;

                const auto& camera = cameraView.get<CameraComponent>(entity);
                if (camera.IsPrimary)
                {
                    outEntity = entity;
                    return true;
                }
            }

            if (fallbackEntity != entt::null)
            {
                outEntity = fallbackEntity;
                return true;
            }

            return false;
        }

        AudioListenerPositions2D CollectAudioListenerPositions2D(const Scene& scene)
        {
            AudioListenerPositions2D listenerPositions;
            const auto& registry = scene.GetRegistry();

            auto listenerView = registry.view<AudioListener2DComponent>();
            for (entt::entity entity : listenerView)
            {
                if (!scene.IsEntityEnabledInHierarchy(entity))
                    continue;

                const auto& listener = listenerView.get<AudioListener2DComponent>(entity);
                if (!listener.Enabled)
                    continue;

                if (listener.UsePrimaryCameraPosition)
                {
                    entt::entity cameraEntity = entt::null;
                    if (TryFindPrimaryCameraEntity(scene, cameraEntity))
                    {
                        listenerPositions.push_back(ComputeEntityWorldPosition2D(scene, cameraEntity));
                        continue;
                    }
                }

                listenerPositions.push_back(ComputeEntityWorldPosition2D(scene, entity));
            }

            // Fallback keeps authored scenes audible before listeners are explicitly added.
            if (listenerPositions.empty())
            {
                entt::entity fallbackCameraEntity = entt::null;
                if (TryFindPrimaryCameraEntity(scene, fallbackCameraEntity))
                    listenerPositions.push_back(ComputeEntityWorldPosition2D(scene, fallbackCameraEntity));
            }

            return listenerPositions;
        }

        AudioListener2DRuntimeState ResolveNearestAudioListener2DRuntimeState(const AudioListenerPositions2D& listeners,
                                                                              const glm::vec2& sourcePosition)
        {
            AudioListener2DRuntimeState listenerState{};
            if (listeners.empty())
                return listenerState;

            listenerState.HasListener = true;
            listenerState.Position = listeners.front();
            float bestDistanceSq = glm::dot(sourcePosition - listenerState.Position, sourcePosition - listenerState.Position);
            for (size_t i = 1; i < listeners.size(); ++i)
            {
                const glm::vec2& listenerPosition = listeners[i];
                const float distanceSq = glm::dot(sourcePosition - listenerPosition, sourcePosition - listenerPosition);
                if (distanceSq < bestDistanceSq)
                {
                    bestDistanceSq = distanceSq;
                    listenerState.Position = listenerPosition;
                }
            }

            return listenerState;
        }

        AudioSpatialMix2D ComputeAudioSpatialMix2D(const AudioSourceComponent& audioSource,
                                                   const glm::vec2& sourcePosition,
                                                   const AudioListenerPositions2D& listeners)
        {
            AudioSpatialMix2D result{};
            if (audioSource.Space != AudioSourceComponent::PlaybackSpace::Spatial2D || listeners.empty())
                return result;

            const AudioListener2DRuntimeState listenerState =
                ResolveNearestAudioListener2DRuntimeState(listeners, sourcePosition);

            const float minDistance = std::max(0.001f, audioSource.SpatialMinDistance);
            const float maxDistance = std::max(minDistance, audioSource.SpatialMaxDistance);
            const float rolloffExponent = std::max(0.01f, audioSource.SpatialRolloffExponent);
            const float panStrength = std::clamp(audioSource.StereoPanStrength, 0.0f, 1.0f);

            const float distanceToListener = glm::length(sourcePosition - listenerState.Position);
            if (distanceToListener <= minDistance)
            {
                result.Gain = 1.0f;
            }
            else if (distanceToListener >= maxDistance)
            {
                result.Gain = 0.0f;
            }
            else
            {
                const float normalized = (distanceToListener - minDistance) / (maxDistance - minDistance);
                result.Gain = std::pow(std::max(0.0f, 1.0f - normalized), rolloffExponent);
            }

            const float panNormalizationDistance = std::max(maxDistance, 0.001f);
            const float signedPan = std::clamp((sourcePosition.x - listenerState.Position.x) / panNormalizationDistance, -1.0f, 1.0f);
            result.Pan = signedPan * panStrength;
            return result;
        }

        void StopAudioSourcesInScene(Scene* scene)
        {
            if (!scene)
                return;

            auto& registry = scene->GetRegistry();
            auto audioView = registry.view<AudioSourceComponent>();
            for (entt::entity entity : audioView)
            {
                auto& audioSource = audioView.get<AudioSourceComponent>(entity);
                if (audioSource.RuntimeVoiceId != 0)
                    Audio::AudioEngine::GetInstance().Stop(audioSource.RuntimeVoiceId);
                audioSource.RuntimeVoiceId = 0;
                audioSource.RuntimePlaybackStarted = false;
            }
        }

        void UpdateSceneAudioSources(Scene* scene)
        {
            if (!scene)
                return;

            auto& registry = scene->GetRegistry();
            const AudioListenerPositions2D listenerPositions = CollectAudioListenerPositions2D(*scene);
            auto audioView = registry.view<AudioSourceComponent>();
            for (entt::entity entity : audioView)
            {
                auto& audioSource = audioView.get<AudioSourceComponent>(entity);
                const bool entityEnabled = scene->IsEntityEnabledInHierarchy(entity);
                if (audioSource.RuntimeVoiceId != 0 &&
                    !Audio::AudioEngine::GetInstance().IsVoiceActive(audioSource.RuntimeVoiceId))
                {
                    audioSource.RuntimeVoiceId = 0;
                }

                if (!entityEnabled)
                {
                    if (audioSource.RuntimeVoiceId != 0)
                        Audio::AudioEngine::GetInstance().Stop(audioSource.RuntimeVoiceId);
                    audioSource.RuntimeVoiceId = 0;
                    audioSource.RuntimePlaybackStarted = false;
                    continue;
                }

                const glm::vec2 sourcePosition = ComputeEntityWorldPosition2D(*scene, entity);
                const AudioSpatialMix2D spatialMix = ComputeAudioSpatialMix2D(audioSource, sourcePosition, listenerPositions);
                const float authoredVolume = audioSource.Muted ? 0.0f : std::max(0.0f, audioSource.Volume);
                const float runtimeVolume = authoredVolume * spatialMix.Gain;
                const float runtimePan = spatialMix.Pan;
                const float runtimePitch = std::max(0.01f, audioSource.Pitch);

                const bool shouldPlayOnStart =
                    audioSource.PlayOnStart &&
                    !audioSource.AudioClipKey.empty();

                if (shouldPlayOnStart && !audioSource.RuntimePlaybackStarted)
                {
                    auto clipAsset = Assets::AudioClipAsset::LoadBlocking(audioSource.AudioClipKey);
                    if (clipAsset && clipAsset->GetClip())
                    {
                        audioSource.RuntimeVoiceId = Audio::AudioEngine::GetInstance().PlayClip(
                            clipAsset->GetClip(),
                            runtimeVolume,
                            audioSource.Loop,
                            audioSource.MixerGroup,
                            runtimePan,
                            runtimePitch);
                        audioSource.RuntimePlaybackStarted = (audioSource.RuntimeVoiceId != 0);
                    }
                }
                else if (shouldPlayOnStart && audioSource.RuntimeVoiceId != 0)
                {
                    (void)Audio::AudioEngine::GetInstance().SetVoiceMixParameters(
                        audioSource.RuntimeVoiceId,
                        runtimeVolume,
                        runtimePan,
                        audioSource.MixerGroup,
                        runtimePitch);
                }
                else if (!shouldPlayOnStart && audioSource.RuntimeVoiceId != 0)
                {
                    Audio::AudioEngine::GetInstance().Stop(audioSource.RuntimeVoiceId);
                    audioSource.RuntimeVoiceId = 0;
                    audioSource.RuntimePlaybackStarted = false;
                }
            }
        }

        void ApplyRuntimeProjectSettingsFromBundle()
        {
            auto& bundle = Assets::AssetBundle::GetInstance();
            if (!bundle.IsEnabled() || !bundle.IsLoaded())
                return;

            // Render settings (clear color + vSync).
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
                    {
                        Application::GetInstance().GetWindow().SetVSync(renderRoot["vSync"].get<bool>());
                    }
                }
                catch (const std::exception& e)
                {
                    LT_WARN("GameLayer: failed parsing RenderSettings.json: {}", e.what());
                }
            }

            // Audio settings (master volume + mixer groups).
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

            // Lighting settings.
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

                    Lighting2DRenderer::SetSettings(lightingSettings);
                }
                catch (const std::exception& e)
                {
                    LT_WARN("GameLayer: failed parsing Lighting2DSettings.json: {}", e.what());
                }
            }

            // Input settings (default project action asset).
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

    // -------------------------------------------------------------------------
    // GameLayer implementation
    // -------------------------------------------------------------------------

    GameLayer::GameLayer()
        : Layer("GameLayer")
    {
    }

    GameLayer::~GameLayer() = default;

    void GameLayer::OnAttach()
    {
        LT_INFO("GameLayer: attaching (shipped game mode).");

        if (!LoadBootstrap())
        {
            LT_ERROR("GameLayer: Failed to load GameBootstrap.json. Cannot start game.");
            Application::GetInstance().SetRunning(false);
            return;
        }

        // Match runtime visual/input behavior with editor project settings.
        ApplyRuntimeProjectSettingsFromBundle();

        if (!m_ProjectName.empty())
            Application::GetInstance().GetWindow().SetTitle(m_ProjectName);

        InitializeScriptCore();

        if (!LoadScene(m_StartupSceneKey))
        {
            LT_ERROR("GameLayer: Failed to load startup scene '{}'.", m_StartupSceneKey);
            Application::GetInstance().SetRunning(false);
            return;
        }

        LT_INFO("GameLayer: game started with scene '{}'.", m_StartupSceneKey);
    }

    void GameLayer::OnDetach()
    {
        if (m_Scene)
        {
            StopAudioSourcesInScene(m_Scene.get());
            s_ActiveScene = nullptr;
            Physics2DQueries::SetActiveSceneForScriptQueries(nullptr);
            m_Scene.reset();
        }

        ShutdownScriptCore();
        LT_INFO("GameLayer: detached.");
    }

    void GameLayer::OnUpdate(float deltaTime)
    {
        UpdateSceneAudioSources(m_Scene.get());

        if (m_Scene && m_Scene->IsReady())
            m_Scene->Update(deltaTime);

        ProcessPendingSceneTransitions();
    }

    void GameLayer::OnFixedUpdate(float fixedDeltaTime)
    {
        if (!m_Scene || !m_Scene->IsReady())
            return;

        m_Scene->FixedUpdate(fixedDeltaTime);
        m_Scene->StepPhysics2D(fixedDeltaTime);
    }

    void GameLayer::OnRender()
    {
        if (!m_Scene || !m_Scene->IsReady())
            return;

        Camera* camera = ResolveGameplayCamera();
        if (camera)
        {
            m_LoggedMissingGameplayCamera = false;
            // Use the same path as editor viewports so shipped runtime gets
            // lighting/shadows and screen-space text rendering.
            SceneRenderer::RenderToViewport(*m_Scene, *camera, {}, m_ViewportWidth, m_ViewportHeight);
        }
        else if (!m_LoggedMissingGameplayCamera)
        {
            LT_WARN("GameLayer: no gameplay camera resolved for render.");
            m_LoggedMissingGameplayCamera = true;
        }
    }

    void GameLayer::OnWindowResize(Events::WindowResizeEvent& event)
    {
        m_ViewportWidth = event.GetWidth();
        m_ViewportHeight = event.GetHeight();

        // Update the gameplay camera viewport if it exists.
        if (auto* camera = m_CameraManager.GetOrthographic2D(m_GameplayCameraId))
            camera->SetViewportSize(m_ViewportWidth, m_ViewportHeight);
    }

    // -------------------------------------------------------------------------
    // Bootstrap loading
    // -------------------------------------------------------------------------

    bool GameLayer::LoadBootstrap()
    {
        // Look for GameBootstrap.json next to the executable.
        std::filesystem::path bootstrapPath;
        const auto& platformInfo = PlatformDetection::GetPlatformInfo();
        if (!platformInfo.executablePath.empty())
        {
            bootstrapPath = std::filesystem::path(platformInfo.executablePath).parent_path() / "GameBootstrap.json";
        }

        // Fallback: check current working directory.
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

    // -------------------------------------------------------------------------
    // Scene loading
    // -------------------------------------------------------------------------

    bool GameLayer::LoadScene(const std::string& sceneAssetKey)
    {
        if (sceneAssetKey.empty())
            return false;

        StopAudioSourcesInScene(m_Scene.get());

        // Scene::LoadFromFile can read directly from AssetBundle when enabled by key.
        auto loadResult = Scene::LoadFromFile(sceneAssetKey);

        if (!loadResult.IsSuccess())
        {
            LT_ERROR("GameLayer: scene load failed: {}", loadResult.GetError().GetErrorMessage());
            return false;
        }

        m_Scene = std::move(loadResult.GetValue());
        m_CurrentSceneAssetKey = sceneAssetKey;
        s_ActiveScene = m_Scene.get();
        Physics2DQueries::SetActiveSceneForScriptQueries(m_Scene.get());

        // Initialize the scene for runtime.
        m_Scene->BeginLoadingState();
        m_Scene->MarkSceneObjectsInitialized();
        m_Scene->InitializePhysicsWorldForLoading();
        m_Scene->SetLoadStateReady();

        // Reset gameplay camera so it gets recreated from the scene.
        m_GameplayCameraId = {};
        m_LoggedMissingGameplayCamera = false;

        // One-time startup diagnostics for packaged runtime troubleshooting.
        auto& registry = m_Scene->GetRegistry();
        const size_t cameraCount = registry.storage<CameraComponent>().size();
        const size_t spriteCount = registry.storage<SpriteComponent>().size();
        const size_t grid2DCount = registry.storage<Grid2DComponent>().size();
        const size_t tilemapLayerCount = registry.storage<TilemapLayerComponent>().size();
        LT_INFO("GameLayer: scene diagnostics -> cameras={}, sprites={}, grids={}, layers={}",
            cameraCount, spriteCount, grid2DCount, tilemapLayerCount);

        LT_INFO("GameLayer: scene '{}' loaded.", sceneAssetKey);
        return true;
    }

    // -------------------------------------------------------------------------
    // ScriptCore DLL management
    // -------------------------------------------------------------------------

    void GameLayer::InitializeScriptCore()
    {
        NativeScriptRegistry::Clear();

        // Find ScriptCore library next to the executable.
        std::filesystem::path libraryPath;
        const auto& platformInfo = PlatformDetection::GetPlatformInfo();
        if (!platformInfo.executablePath.empty())
            libraryPath = std::filesystem::path(platformInfo.executablePath).parent_path() / GetScriptCoreLibraryFileName();

        if (libraryPath.empty() || !std::filesystem::exists(libraryPath))
            libraryPath = std::filesystem::current_path() / GetScriptCoreLibraryFileName();

        if (!std::filesystem::exists(libraryPath))
        {
            LT_WARN("GameLayer: ScriptCore library not found. Running without scripts.");
            return;
        }

        m_ScriptCoreLibraryHandle = PlatformUtils::LoadLibrary(libraryPath.string());
        if (!m_ScriptCoreLibraryHandle)
        {
            LT_ERROR("GameLayer: failed to load ScriptCore library.");
            return;
        }

        const auto getAbiVersionFunction = reinterpret_cast<GetScriptCoreAbiVersionFunction>(
            PlatformUtils::GetProcAddress(m_ScriptCoreLibraryHandle, "LT_GetScriptCoreAbiVersion"));
        if (!getAbiVersionFunction)
        {
            LT_ERROR("GameLayer: ScriptCore missing LT_GetScriptCoreAbiVersion export.");
            PlatformUtils::FreeLibrary(m_ScriptCoreLibraryHandle);
            m_ScriptCoreLibraryHandle = nullptr;
            return;
        }

        const uint32_t reportedAbiVersion = getAbiVersionFunction();
        if (reportedAbiVersion != kScriptCoreAbiVersion)
        {
            LT_ERROR("GameLayer: ScriptCore ABI mismatch. expected={}, got={}. Rebuild project scripts/ScriptCore for this engine revision.",
                     kScriptCoreAbiVersion,
                     reportedAbiVersion);
            PlatformUtils::FreeLibrary(m_ScriptCoreLibraryHandle);
            m_ScriptCoreLibraryHandle = nullptr;
            return;
        }

        // Get the registration function.
        const auto registerFunction = reinterpret_cast<RegisterScriptCoreTypesFunction>(
            PlatformUtils::GetProcAddress(m_ScriptCoreLibraryHandle, "LT_RegisterScriptCoreTypes"));

        if (!registerFunction)
        {
            LT_ERROR("GameLayer: ScriptCore missing LT_RegisterScriptCoreTypes export.");
            PlatformUtils::FreeLibrary(m_ScriptCoreLibraryHandle);
            m_ScriptCoreLibraryHandle = nullptr;
            return;
        }

        // Helper to connect a bridge function by export name.
        auto connectBridge = [&](const char* exportName, auto callback) {
            using FuncType = void (*)(decltype(callback));
            auto setter = reinterpret_cast<FuncType>(PlatformUtils::GetProcAddress(m_ScriptCoreLibraryHandle, exportName));
            if (setter) setter(callback);
        };

        // Connect all bridge functions (must match ScriptCore DLL exports).
        connectBridge("LT_SetSceneTransitionBridge", &ForwardSceneTransitionToHost);
        connectBridge("LT_SetInputActionAxis1DBridge", &ForwardInputActionAxis1DToHost);
        connectBridge("LT_SetInputActionAxis2DBridge", &ForwardInputActionAxis2DToHost);
        connectBridge("LT_SetInputActionExistsBridge", &ForwardInputActionExistsToHost);
        connectBridge("LT_SetInputActionPressedBridge", &ForwardInputActionPressedToHost);
        connectBridge("LT_SetInputActionStartedBridge", &ForwardInputActionStartedToHost);
        connectBridge("LT_SetInputActionPerformedBridge", &ForwardInputActionPerformedToHost);
        connectBridge("LT_SetInputActionCanceledBridge", &ForwardInputActionCanceledToHost);
        connectBridge("LT_SetInputActionButtonBridge", &ForwardInputActionButtonToHost);
        connectBridge("LT_SetPhysics2DRaycastBridge", &ForwardPhysics2DRaycastToHost);
        connectBridge("LT_SetScriptLogBridge", &ForwardScriptLogToHost);
        connectBridge("LT_SetScriptCreateEntityBridge", &ForwardScriptCreateEntityToHost);
        connectBridge("LT_SetScriptDestroyEntityBridge", &ForwardScriptDestroyEntityToHost);
        connectBridge("LT_SetScriptInstantiatePrefabBridge", &ForwardScriptInstantiatePrefabToHost);
        connectBridge("LT_SetScriptContactEntityHandlesBridge", &ForwardScriptContactEntityHandlesToHost);

        // Legacy bridge names (backward compat with older ScriptCore builds).
        connectBridge("LT_SetInputButtonDownBridge", &ForwardInputActionStartedToHost);
        connectBridge("LT_SetInputButtonBridge", &ForwardInputActionButtonToHost);

        // Register all scripts from the DLL.
        registerFunction(&RegisterScriptFromModule);

        // Wire up engine-side entity bridge callbacks.
        ScriptableEntity::SetCreateEntityBridgeCallback(&ForwardScriptCreateEntityToHost);
        ScriptableEntity::SetDestroyEntityBridgeCallback(&ForwardScriptDestroyEntityToHost);
        ScriptableEntity::SetInstantiatePrefabBridgeCallback(&ForwardScriptInstantiatePrefabToHost);
        ScriptableEntity::SetContactEntityHandlesBridgeCallback(&ForwardScriptContactEntityHandlesToHost);
        ScriptableEntity::SetParallelScriptExecutionBridgeCallback(&ForwardScriptParallelExecutionStateToHost);
        Entity::SetDestroyBridgeCallback(&ForwardScriptDestroyEntityToHost);
        Entity::SetParallelExecutionBridgeCallback(&ForwardScriptParallelExecutionStateToHost);

        LT_INFO("GameLayer: ScriptCore loaded with {} script(s).",
                 NativeScriptRegistry::GetRegisteredScriptNames().size());
    }

    void GameLayer::ShutdownScriptCore()
    {
        if (m_ScriptCoreLibraryHandle)
        {
            PlatformUtils::FreeLibrary(m_ScriptCoreLibraryHandle);
            m_ScriptCoreLibraryHandle = nullptr;
        }

        NativeScriptRegistry::Clear();
    }

    // -------------------------------------------------------------------------
    // Scene transitions
    // -------------------------------------------------------------------------

    void GameLayer::ProcessPendingSceneTransitions()
    {
        auto transition = SceneManager::ConsumePendingSceneTransition();
        if (!transition.has_value())
            return;

        if (transition->Type == SceneTransitionType::ReloadCurrentScene)
        {
            LT_INFO("GameLayer: reloading current scene '{}'.", m_CurrentSceneAssetKey);
            LoadScene(m_CurrentSceneAssetKey);
            return;
        }

        // Try to match the scene identifier to a build scene key.
        std::string targetKey = transition->SceneIdentifier;

        // Direct match check.
        auto matchIt = std::find(m_BuildScenes.begin(), m_BuildScenes.end(), targetKey);
        if (matchIt != m_BuildScenes.end())
        {
            LoadScene(targetKey);
            return;
        }

        // Try matching by scene name (without path/extension).
        for (const auto& buildSceneKey : m_BuildScenes)
        {
            std::filesystem::path scenePath(buildSceneKey);
            std::string stem = scenePath.stem().string();
            // Remove .scene suffix if present (e.g. "Level01.scene" -> "Level01").
            if (stem.size() > 6 && stem.substr(stem.size() - 6) == ".scene")
                stem = stem.substr(0, stem.size() - 6);

            if (stem == targetKey || buildSceneKey.find(targetKey) != std::string::npos)
            {
                LoadScene(buildSceneKey);
                return;
            }
        }

        LT_WARN("GameLayer: scene '{}' not found in build scenes list.", targetKey);
    }

    // -------------------------------------------------------------------------
    // Camera resolution
    // -------------------------------------------------------------------------

    Camera* GameLayer::ResolveGameplayCamera()
    {
        if (!m_Scene)
            return nullptr;

        auto& registry = m_Scene->GetRegistry();
        auto cameraView = registry.view<CameraComponent>();

        entt::entity selectedCameraEntity = entt::null;
        CameraComponent selectedCameraComponent{};
        bool hasSelection = false;
        for (auto entity : cameraView)
        {
            const auto& cameraComponent = cameraView.get<CameraComponent>(entity);
            if (!hasSelection)
            {
                selectedCameraEntity = entity;
                selectedCameraComponent = cameraComponent;
                hasSelection = true;
            }

            if (cameraComponent.IsPrimary)
            {
                selectedCameraEntity = entity;
                selectedCameraComponent = cameraComponent;
                hasSelection = true;
                break;
            }
        }

        if (!hasSelection)
            return nullptr;

        const CameraType expectedType = (selectedCameraComponent.Projection == CameraComponent::ProjectionType::Perspective3D)
            ? CameraType::Perspective3D
            : CameraType::Orthographic2D;

        Camera* gameplayCamera = m_CameraManager.GetCamera(m_GameplayCameraId);
        if (!gameplayCamera || gameplayCamera->GetType() != expectedType || gameplayCamera->GetUsage() != CameraUsage::Gameplay)
        {
            if (m_GameplayCameraId)
            {
                (void)m_CameraManager.DestroyCamera(m_GameplayCameraId);
                m_GameplayCameraId = {};
            }

            if (expectedType == CameraType::Orthographic2D)
            {
                CameraManager::Orthographic2DCreateInfo info{};
                info.Name = "GameplayCamera";
                info.Usage = CameraUsage::Gameplay;
                info.ViewportWidthPixels = m_ViewportWidth;
                info.ViewportHeightPixels = m_ViewportHeight;
                info.Zoom = selectedCameraComponent.Zoom > 0.0f ? selectedCameraComponent.Zoom : 1.0f;
                info.NearPlane = selectedCameraComponent.NearPlane;
                info.FarPlane = selectedCameraComponent.FarPlane > selectedCameraComponent.NearPlane
                    ? selectedCameraComponent.FarPlane
                    : (selectedCameraComponent.NearPlane + 2.0f);
                m_GameplayCameraId = m_CameraManager.CreateOrthographic2D(info);
            }
            else
            {
                CameraManager::Perspective3DCreateInfo info{};
                info.Name = "GameplayCamera";
                info.Usage = CameraUsage::Gameplay;
                info.ViewportWidthPixels = m_ViewportWidth;
                info.ViewportHeightPixels = m_ViewportHeight;
                info.FieldOfViewYDegrees = selectedCameraComponent.FieldOfViewYDegrees > 1.0f
                    ? selectedCameraComponent.FieldOfViewYDegrees
                    : 60.0f;
                info.NearPlane = selectedCameraComponent.NearPlane > 0.0f ? selectedCameraComponent.NearPlane : 0.01f;
                info.FarPlane = selectedCameraComponent.FarPlane > info.NearPlane
                    ? selectedCameraComponent.FarPlane
                    : (info.NearPlane + 1000.0f);
                if (selectedCameraComponent.NearPlane <= 0.0f && selectedCameraComponent.FarPlane <= 1.0f)
                {
                    info.NearPlane = 0.1f;
                    info.FarPlane = 1000.0f;
                }
                m_GameplayCameraId = m_CameraManager.CreatePerspective3D(info);
            }

            m_CameraManager.SetActiveCamera(m_GameplayCameraId);
            gameplayCamera = m_CameraManager.GetCamera(m_GameplayCameraId);
        }

        if (!gameplayCamera)
            return nullptr;

        gameplayCamera->SetViewportSize(m_ViewportWidth, m_ViewportHeight);
        const glm::mat4 worldTransform = m_Scene->GetWorldTransformMatrix(selectedCameraEntity);
        const glm::vec3 position = glm::vec3(worldTransform[3]);

        if (auto* orthographicCamera = m_CameraManager.GetOrthographic2D(m_GameplayCameraId))
        {
            const float zoom = selectedCameraComponent.Zoom > 0.0f ? selectedCameraComponent.Zoom : 1.0f;
            const float nearPlane = selectedCameraComponent.NearPlane;
            const float farPlane = selectedCameraComponent.FarPlane > nearPlane
                ? selectedCameraComponent.FarPlane
                : (nearPlane + 2.0f);
            orthographicCamera->SetProjection(zoom, nearPlane, farPlane);
            orthographicCamera->SetPosition(position);

            const float rotationRadians = std::atan2(worldTransform[1][0], worldTransform[0][0]);
            orthographicCamera->SetRotationRadians(rotationRadians);
            return orthographicCamera;
        }

        if (auto* perspectiveCamera = m_CameraManager.GetPerspective3D(m_GameplayCameraId))
        {
            const float fieldOfViewY = selectedCameraComponent.FieldOfViewYDegrees > 1.0f
                ? selectedCameraComponent.FieldOfViewYDegrees
                : 60.0f;
            float nearPlane = selectedCameraComponent.NearPlane > 0.0f ? selectedCameraComponent.NearPlane : 0.01f;
            float farPlane = selectedCameraComponent.FarPlane > nearPlane
                ? selectedCameraComponent.FarPlane
                : (nearPlane + 1000.0f);
            if (selectedCameraComponent.NearPlane <= 0.0f && selectedCameraComponent.FarPlane <= 1.0f)
            {
                nearPlane = 0.1f;
                farPlane = 1000.0f;
            }

            perspectiveCamera->SetPerspective(fieldOfViewY, nearPlane, farPlane);
            perspectiveCamera->SetPosition(position);

            const glm::vec3 forward = glm::normalize(glm::vec3(worldTransform * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
            const float yawDegrees = glm::degrees(std::atan2(forward.z, forward.x));
            const float pitchDegrees = glm::degrees(std::asin(glm::clamp(forward.y, -1.0f, 1.0f)));
            perspectiveCamera->SetYawPitchDegrees(yawDegrees, pitchDegrees);
            return perspectiveCamera;
        }

        return gameplayCamera;
    }
}
