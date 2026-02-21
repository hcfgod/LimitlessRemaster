#include "Scene/Scene.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetBundle.h"
#include "Assets/AssetManager.h"
#include "Assets/AssetPaths.h"
#include "Assets/AssetTypes.h"
#include "Assets/AssetUtils.h"
#include "Assets/AnimationClipAsset.h"
#include "Assets/AnimationClipAssetImporter.h"
#include "Assets/AnimatorControllerAsset.h"
#include "Assets/AnimatorControllerAssetImporter.h"
#include "Assets/MaterialAsset.h"
#include "Assets/MaterialAssetImporter.h"
#include "Assets/TileAsset.h"
#include "Assets/SpriteImportSettings.h"
#include "Assets/TextureAsset.h"
#include "Graphics/Camera/Camera.h"
#include "Graphics/Framebuffer.h"
#include "Graphics/Lighting2DRenderer.h"
#include "Graphics/RenderCommand.h"
#include "Graphics/Renderer.h"
#include "Graphics/Renderer2D.h"
#include "Core/Application.h"
#include "Core/Input/InputSystem.h"
#include "Core/Concurrency/AsyncIO.h"
#include "Core/Time.h"
#include "Platform/Window.h"
#include "Physics/Physics2DQueries.h"
#include "Physics/Physics2DWorld.h"
#include "Scene/ParticleEmitterSystem.h"
#include "Scripting/Coroutine.h"
#include "Scripting/NativeScriptRegistry.h"

#include <nlohmann/json.hpp>

#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>
#include <SDL3/SDL_mouse.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace Limitless
{
    namespace
    {
        constexpr int32_t kSiblingOrderStep = 10;
        std::unordered_map<std::string, Async::Task<Assets::TextureAsset::Ptr>> g_PendingTextureLoads;
        std::unordered_map<std::string, Async::Task<Assets::MaterialAsset::Ptr>> g_PendingMaterialLoads;
        glm::vec4 g_ViewportClearColor = glm::vec4(0.08f, 0.08f, 0.10f, 1.0f);
        struct UiInputViewportRect
        {
            bool Enabled = false;
            glm::vec2 MinPixels = glm::vec2(0.0f);
            glm::vec2 SizePixels = glm::vec2(0.0f);
        };
        UiInputViewportRect g_UiInputViewportRect{};

        // Resolve legacy/stale asset keys to the latest known key in AssetDatabase.
        // This keeps scene references resilient across asset moves/renames.
        std::string ResolveLatestKeyFromDatabase(const std::string& assetKey)
        {
            if (assetKey.empty())
                return {};

            const auto record = Assets::AssetDatabase::GetInstance().FindByKey(assetKey);
            if (record.IsSuccess() && !record.GetValue().Key.empty())
                return record.GetValue().Key;

            return assetKey;
        }

        std::string GetUnqualifiedScriptClassName(std::string_view className)
        {
            const size_t separator = className.rfind("::");
            if (separator == std::string_view::npos)
                return std::string(className);
            return std::string(className.substr(separator + 2));
        }

        std::string ResolveRegisteredScriptClassName(const std::string& requestedClassName,
                                                     const std::string& scriptAssetRelativePath)
        {
            if (requestedClassName.empty())
                return {};
            if (NativeScriptRegistry::HasScript(requestedClassName))
                return requestedClassName;

            const auto registeredScriptNames = NativeScriptRegistry::GetRegisteredScriptNames();
            auto resolveByToken = [&](const std::string& classToken) -> std::string {
                if (classToken.empty())
                    return {};
                if (NativeScriptRegistry::HasScript(classToken))
                    return classToken;

                std::string matchedClassName;
                for (const std::string& candidate : registeredScriptNames)
                {
                    if (candidate == classToken || GetUnqualifiedScriptClassName(candidate) == classToken)
                    {
                        if (!matchedClassName.empty())
                            return {};
                        matchedClassName = candidate;
                    }
                }
                return matchedClassName;
            };

            if (const std::string fromRequested = resolveByToken(requestedClassName); !fromRequested.empty())
                return fromRequested;

            if (!scriptAssetRelativePath.empty())
            {
                const std::string stem = std::filesystem::path(scriptAssetRelativePath).stem().string();
                if (const std::string fromAssetPath = resolveByToken(stem); !fromAssetPath.empty())
                    return fromAssetPath;
            }

            if (registeredScriptNames.size() == 1)
                return registeredScriptNames.front();

            return {};
        }

        // Unity-style reference object for scene asset links.
        // Prefer GUID stability, but also store key for convenience and bundle-only scenarios.
        nlohmann::json MakeAssetReferenceJson(const std::string& assetKey, Assets::AssetType type)
        {
            using json = nlohmann::json;

            json ref = json::object();
            ref["guid"] = "";
            ref["key"] = assetKey;

            if (assetKey.empty())
            {
                return ref;
            }

            // Preferred: AssetDatabase GUID (stable, fast).
            const auto record = Assets::AssetDatabase::GetInstance().FindByKey(assetKey);
            if (record.IsSuccess() && !record.GetValue().Guid.empty())
            {
                ref["guid"] = record.GetValue().Guid;
                return ref;
            }

            // Fallback: resolve path and ensure `.meta` exists.
            const auto resolved = Assets::ResolveAssetKeyToPath(assetKey);
            if (resolved.IsSuccess())
            {
                const auto guidResult = Assets::LoadOrCreateGuid(resolved.GetValue().string(), {{"key", assetKey}, {"type", Assets::ToString(type)}});
                if (guidResult.IsSuccess())
                {
                    ref["guid"] = guidResult.GetValue();
                    (void)Assets::AssetDatabase::GetInstance().ImportOrUpdate(assetKey, type);
                }
            }

            return ref;
        }

        // Resolve a scene reference object to a key.
        // Accepts either:
        // - legacy string key
        // - object { guid, key }
        std::string ResolveAssetKeyFromSceneJson(const nlohmann::json& value)
        {
            if (value.is_string())
            {
                return ResolveLatestKeyFromDatabase(value.get<std::string>());
            }

            if (!value.is_object())
            {
                return {};
            }

            // Preferred: GUID -> key.
            if (value.contains("guid") && value["guid"].is_string())
            {
                const std::string guid = value["guid"].get<std::string>();
                if (!guid.empty())
                {
                    const auto rec = Assets::AssetDatabase::GetInstance().FindByGuid(guid);
                    if (rec.IsSuccess() && !rec.GetValue().Key.empty())
                    {
                        return rec.GetValue().Key;
                    }
                }
            }

            // Fallback: embedded key.
            if (value.contains("key") && value["key"].is_string())
            {
                return ResolveLatestKeyFromDatabase(value["key"].get<std::string>());
            }

            return {};
        }

        bool TryGetOwningCanvasEntity(const entt::registry& registry, entt::entity entity, entt::entity& outCanvasEntity)
        {
            entt::entity current = entity;
            while (current != entt::null)
            {
                if (registry.all_of<CanvasComponent>(current))
                {
                    outCanvasEntity = current;
                    return true;
                }

                const auto* hierarchy = registry.try_get<HierarchyComponent>(current);
                if (!hierarchy)
                    break;
                current = hierarchy->Parent;
            }

            return false;
        }

        bool IsEntityInCanvasUiHierarchy(const entt::registry& registry, entt::entity entity)
        {
            if (!registry.all_of<RectTransformComponent>(entity))
                return false;

            entt::entity canvasEntity = entt::null;
            return TryGetOwningCanvasEntity(registry, entity, canvasEntity);
        }

        entt::entity FindDirectChildByTag(const entt::registry& registry, entt::entity parent, std::string_view childTag)
        {
            auto childView = registry.view<HierarchyComponent, TagComponent>();
            entt::entity bestChild = entt::null;
            int32_t bestScore = std::numeric_limits<int32_t>::min();
            int32_t bestSiblingOrder = std::numeric_limits<int32_t>::min();
            for (entt::entity child : childView)
            {
                const auto& hierarchy = childView.get<HierarchyComponent>(child);
                if (hierarchy.Parent != parent)
                    continue;
                const auto& tag = childView.get<TagComponent>(child);
                if (tag.Tag != childTag)
                    continue;

                // Prefer authored/customized children over generated defaults.
                int32_t score = 0;
                if (registry.all_of<RectTransformComponent>(child))
                    score += 20;
                if (registry.all_of<UIImageComponent>(child))
                    score += 20;
                if (const auto* sprite = registry.try_get<SpriteComponent>(child))
                {
                    score += 20;
                    if (!sprite->TextureKey.empty())
                        score += 100;
                    if (sprite->Color != glm::vec4(1.0f))
                        score += 10;
                }

                if (score > bestScore || (score == bestScore && hierarchy.SiblingOrder > bestSiblingOrder))
                {
                    bestChild = child;
                    bestScore = score;
                    bestSiblingOrder = hierarchy.SiblingOrder;
                }
            }
            return bestChild;
        }

        void SyncSliderVisualChildren(entt::registry& registry, entt::entity sliderEntity, const UISliderComponent& slider)
        {
            const float valueRange = std::max(0.0f, slider.MaxValue - slider.MinValue);
            const float normalizedValue = (valueRange > 0.0001f)
                ? std::clamp((slider.Value - slider.MinValue) / valueRange, 0.0f, 1.0f)
                : 0.0f;

            if (const entt::entity fillEntity = FindDirectChildByTag(registry, sliderEntity, "Slider Fill");
                fillEntity != entt::null)
            {
                if (auto* fillRect = registry.try_get<RectTransformComponent>(fillEntity))
                {
                    fillRect->AnchorMin.x = 0.0f;
                    fillRect->AnchorMax.x = normalizedValue;
                    if (fillRect->AnchorMax.x < fillRect->AnchorMin.x)
                        fillRect->AnchorMax.x = fillRect->AnchorMin.x;
                }
            }

            if (const entt::entity handleEntity = FindDirectChildByTag(registry, sliderEntity, "Slider Handle");
                handleEntity != entt::null)
            {
                if (auto* handleRect = registry.try_get<RectTransformComponent>(handleEntity))
                {
                    handleRect->AnchorMin.x = normalizedValue;
                    handleRect->AnchorMax.x = normalizedValue;
                    handleRect->AnchoredPosition.x = 0.0f;
                }
                if (auto* handleTag = registry.try_get<TagComponent>(handleEntity))
                    handleTag->Enabled = slider.ShowHandle;
            }
        }

        void ProcessUiInteractionSystem(Scene& scene, uint32_t windowWidth, uint32_t windowHeight);

        nlohmann::json SerializeScriptPropertyValue(const ScriptPropertyValue& value)
        {
            nlohmann::json root = nlohmann::json::object();
            if (const auto* floatValue = std::get_if<float>(&value))
            {
                root["Type"] = "Float";
                root["Value"] = *floatValue;
            }
            else if (const auto* integerValue = std::get_if<int32_t>(&value))
            {
                root["Type"] = "Integer";
                root["Value"] = *integerValue;
            }
            else if (const auto* booleanValue = std::get_if<bool>(&value))
            {
                root["Type"] = "Boolean";
                root["Value"] = *booleanValue;
            }
            else if (const auto* vectorValue = std::get_if<glm::vec3>(&value))
            {
                root["Type"] = "Vector3";
                root["Value"] = { vectorValue->x, vectorValue->y, vectorValue->z };
            }
            else if (const auto* stringValue = std::get_if<std::string>(&value))
            {
                root["Type"] = "String";
                root["Value"] = *stringValue;
            }
            else if (const auto* entityValue = std::get_if<ScriptEntityReference>(&value))
            {
                root["Type"] = "Entity";
                root["Value"] = {
                    { "Tag", entityValue->Tag },
                    { "PrefabAssetKey", entityValue->PrefabAssetKey }
                };
            }
            else if (const auto* prefabValue = std::get_if<ScriptPrefabReference>(&value))
            {
                root["Type"] = "Prefab";
                root["Value"] = {
                    { "AssetKey", prefabValue->AssetKey }
                };
            }
            return root;
        }

        bool DeserializeScriptPropertyValue(const nlohmann::json& root, ScriptPropertyValue& outValue)
        {
            if (!root.is_object())
                return false;

            const std::string typeName = root.value("Type", std::string{});
            if (typeName == "Float")
            {
                outValue = root.value("Value", 0.0f);
                return true;
            }
            if (typeName == "Integer")
            {
                outValue = root.value("Value", 0);
                return true;
            }
            if (typeName == "Boolean")
            {
                outValue = root.value("Value", false);
                return true;
            }
            if (typeName == "Vector3")
            {
                const auto vector = root.value("Value", std::vector<float>{ 0.0f, 0.0f, 0.0f });
                if (vector.size() >= 3)
                    outValue = glm::vec3(vector[0], vector[1], vector[2]);
                else
                    outValue = glm::vec3(0.0f);
                return true;
            }
            if (typeName == "String")
            {
                outValue = root.value("Value", std::string{});
                return true;
            }
            if (typeName == "Entity")
            {
                ScriptEntityReference entityReference{};
                if (root.contains("Value"))
                {
                    const auto& value = root["Value"];
                    if (value.is_object())
                    {
                        entityReference.Tag = value.value("Tag", std::string{});
                        entityReference.PrefabAssetKey = value.value("PrefabAssetKey", std::string{});
                    }
                    else if (value.is_string())
                        entityReference.Tag = value.get<std::string>();
                }
                outValue = std::move(entityReference);
                return true;
            }
            if (typeName == "Prefab")
            {
                ScriptEntityReference entityReference{};
                if (root.contains("Value"))
                {
                    const auto& value = root["Value"];
                    if (value.is_object())
                        entityReference.PrefabAssetKey = value.value("AssetKey", std::string{});
                    else if (value.is_string())
                        entityReference.PrefabAssetKey = value.get<std::string>();
                }
                outValue = std::move(entityReference);
                return true;
            }

            return false;
        }

        float WrapAngleRadians(float angleRadians)
        {
            while (angleRadians > glm::pi<float>())
                angleRadians -= glm::two_pi<float>();
            while (angleRadians < -glm::pi<float>())
                angleRadians += glm::two_pi<float>();
            return angleRadians;
        }

        const char* ToAudioPlaybackSpaceName(AudioSourceComponent::PlaybackSpace space)
        {
            switch (space)
            {
                case AudioSourceComponent::PlaybackSpace::Spatial2D: return "Spatial2D";
                case AudioSourceComponent::PlaybackSpace::Global:
                default: return "Global";
            }
        }

        AudioSourceComponent::PlaybackSpace ParseAudioPlaybackSpaceName(const std::string& spaceName)
        {
            if (spaceName == "Spatial2D")
                return AudioSourceComponent::PlaybackSpace::Spatial2D;
            return AudioSourceComponent::PlaybackSpace::Global;
        }

        void ResetAnimatorRuntimeOutput(AnimatorComponent& animator, bool clearSpriteTextureOverrideCache)
        {
            animator.RuntimeHasSpriteSubRect = false;
            animator.RuntimeSpriteUvMin = glm::vec2(0.0f, 0.0f);
            animator.RuntimeSpriteUvMax = glm::vec2(1.0f, 1.0f);
            animator.RuntimeSpriteTextureOverrideKey.clear();
            if (clearSpriteTextureOverrideCache)
            {
                animator.RuntimeCachedSpriteTextureOverride.reset();
                animator.RuntimeSpriteTextureOverrideLoadAttempted = false;
            }
            animator.RuntimeHasPosition = false;
            animator.RuntimeHasScale = false;
            animator.RuntimeHasRotationZ = false;
            animator.RuntimePosition = glm::vec3(0.0f);
            animator.RuntimeScale = glm::vec3(1.0f);
            animator.RuntimeRotationZDegrees = 0.0f;
        }

        bool TryResolveAnimatorControllerAsset(AnimatorComponent& animator)
        {
            if (animator.ControllerKey.empty())
            {
                animator.CachedController.reset();
                animator.ControllerLoadAttempted = false;
                return false;
            }

            if (animator.CachedController && animator.CachedController->GetKey() == animator.ControllerKey)
                return true;

            animator.CachedController = std::dynamic_pointer_cast<Assets::AnimatorControllerAsset>(
                Assets::AssetManager::GetCachedByKey(animator.ControllerKey));
            if (!animator.CachedController)
            {
                animator.CachedController = Assets::AssetManager::LoadBlocking<Assets::AnimatorControllerAsset>(animator.ControllerKey);
            }

            animator.ControllerLoadAttempted = true;
            return (animator.CachedController != nullptr);
        }

        bool TryResolveAnimationClipAssetByKey(const std::string& clipKey, Assets::AnimationClipAsset::Ptr& outClip)
        {
            outClip.reset();
            if (clipKey.empty())
                return false;

            outClip = std::dynamic_pointer_cast<Assets::AnimationClipAsset>(Assets::AssetManager::GetCachedByKey(clipKey));
            if (!outClip)
                outClip = Assets::AssetManager::LoadBlocking<Assets::AnimationClipAsset>(clipKey);
            return (outClip != nullptr);
        }

        bool TryResolveDefaultClipAsset(AnimatorComponent& animator)
        {
            if (animator.DefaultClipKey.empty())
            {
                animator.CachedDefaultClip.reset();
                animator.DefaultClipLoadAttempted = false;
                return false;
            }

            if (animator.CachedDefaultClip && animator.CachedDefaultClip->GetKey() == animator.DefaultClipKey)
                return true;

            animator.CachedDefaultClip = std::dynamic_pointer_cast<Assets::AnimationClipAsset>(
                Assets::AssetManager::GetCachedByKey(animator.DefaultClipKey));
            if (!animator.CachedDefaultClip)
                animator.CachedDefaultClip = Assets::AssetManager::LoadBlocking<Assets::AnimationClipAsset>(animator.DefaultClipKey);

            animator.DefaultClipLoadAttempted = true;
            return (animator.CachedDefaultClip != nullptr);
        }

        const Assets::AnimatorControllerAsset::StateDefinition* FindControllerState(
            const Assets::AnimatorControllerAsset::Data& controllerData,
            const std::string& stateName)
        {
            for (const auto& state : controllerData.States)
            {
                if (state.Name == stateName)
                    return &state;
            }
            return nullptr;
        }

        void EnsureAnimatorParametersFromControllerDefaults(
            AnimatorComponent& animator,
            const Assets::AnimatorControllerAsset::Data& controllerData)
        {
            for (const auto& parameter : controllerData.Parameters)
            {
                switch (parameter.Type)
                {
                    case Assets::AnimatorControllerAsset::ParameterType::Bool:
                    {
                        if (!animator.BoolParameters.contains(parameter.Name))
                            animator.BoolParameters[parameter.Name] = parameter.DefaultBool;
                        break;
                    }
                    case Assets::AnimatorControllerAsset::ParameterType::Float:
                    {
                        if (!animator.FloatParameters.contains(parameter.Name))
                            animator.FloatParameters[parameter.Name] = parameter.DefaultFloat;
                        break;
                    }
                    case Assets::AnimatorControllerAsset::ParameterType::Integer:
                    {
                        if (!animator.IntegerParameters.contains(parameter.Name))
                            animator.IntegerParameters[parameter.Name] = parameter.DefaultInteger;
                        break;
                    }
                    case Assets::AnimatorControllerAsset::ParameterType::Trigger:
                    {
                        if (!animator.TriggerParameters.contains(parameter.Name))
                            animator.TriggerParameters[parameter.Name] = false;
                        break;
                    }
                }
            }
        }

        bool EvaluateAnimatorTransitionConditions(
            const Assets::AnimatorControllerAsset::TransitionDefinition& transition,
            AnimatorComponent& animator,
            std::vector<std::string>& outTriggersToConsume)
        {
            outTriggersToConsume.clear();
            for (const auto& condition : transition.Conditions)
            {
                switch (condition.Mode)
                {
                    case Assets::AnimatorControllerAsset::ConditionMode::If:
                    {
                        const bool value = animator.GetBoolParameter(condition.ParameterName, false);
                        if (!value)
                            return false;
                        break;
                    }
                    case Assets::AnimatorControllerAsset::ConditionMode::IfNot:
                    {
                        const bool value = animator.GetBoolParameter(condition.ParameterName, false);
                        if (value)
                            return false;
                        break;
                    }
                    case Assets::AnimatorControllerAsset::ConditionMode::Greater:
                    {
                        const float value = animator.GetFloatParameter(condition.ParameterName, 0.0f);
                        if (!(value > condition.FloatThreshold))
                            return false;
                        break;
                    }
                    case Assets::AnimatorControllerAsset::ConditionMode::Less:
                    {
                        const float value = animator.GetFloatParameter(condition.ParameterName, 0.0f);
                        if (!(value < condition.FloatThreshold))
                            return false;
                        break;
                    }
                    case Assets::AnimatorControllerAsset::ConditionMode::Equals:
                    {
                        const int32_t value = animator.GetIntegerParameter(condition.ParameterName, 0);
                        if (value != condition.IntegerThreshold)
                            return false;
                        break;
                    }
                    case Assets::AnimatorControllerAsset::ConditionMode::NotEquals:
                    {
                        const int32_t value = animator.GetIntegerParameter(condition.ParameterName, 0);
                        if (value == condition.IntegerThreshold)
                            return false;
                        break;
                    }
                    case Assets::AnimatorControllerAsset::ConditionMode::Triggered:
                    {
                        const auto found = animator.TriggerParameters.find(condition.ParameterName);
                        if (found == animator.TriggerParameters.end() || !found->second)
                            return false;
                        outTriggersToConsume.push_back(condition.ParameterName);
                        break;
                    }
                }
            }

            return true;
        }

        template<typename TTrackKeyframe>
        const TTrackKeyframe* SampleStepTrackKeyframe(const std::vector<TTrackKeyframe>& track, float timeSeconds)
        {
            if (track.empty())
                return nullptr;
            const TTrackKeyframe* sampled = &track.front();
            for (const auto& keyframe : track)
            {
                if (keyframe.TimeSeconds <= timeSeconds)
                {
                    sampled = &keyframe;
                    continue;
                }
                break;
            }
            return sampled;
        }

        glm::vec3 SampleVector3Track(const std::vector<Assets::AnimationClipAsset::Vector3Keyframe>& track,
                                     float timeSeconds,
                                     bool& outHasSample)
        {
            outHasSample = false;
            if (track.empty())
                return glm::vec3(0.0f);

            outHasSample = true;
            if (track.size() == 1 || timeSeconds <= track.front().TimeSeconds)
                return track.front().Value;
            if (timeSeconds >= track.back().TimeSeconds)
                return track.back().Value;

            for (size_t index = 1; index < track.size(); ++index)
            {
                const auto& right = track[index];
                if (timeSeconds > right.TimeSeconds)
                    continue;

                const auto& left = track[index - 1];
                const float segmentLength = std::max(0.0001f, right.TimeSeconds - left.TimeSeconds);
                const float interpolation = std::clamp((timeSeconds - left.TimeSeconds) / segmentLength, 0.0f, 1.0f);
                if (left.Interpolation == Assets::AnimationClipAsset::InterpolationMode::Step)
                    return left.Value;
                return glm::mix(left.Value, right.Value, interpolation);
            }

            return track.back().Value;
        }

        float SampleFloatTrack(const std::vector<Assets::AnimationClipAsset::FloatKeyframe>& track,
                               float timeSeconds,
                               bool& outHasSample)
        {
            outHasSample = false;
            if (track.empty())
                return 0.0f;

            outHasSample = true;
            if (track.size() == 1 || timeSeconds <= track.front().TimeSeconds)
                return track.front().Value;
            if (timeSeconds >= track.back().TimeSeconds)
                return track.back().Value;

            for (size_t index = 1; index < track.size(); ++index)
            {
                const auto& right = track[index];
                if (timeSeconds > right.TimeSeconds)
                    continue;

                const auto& left = track[index - 1];
                const float segmentLength = std::max(0.0001f, right.TimeSeconds - left.TimeSeconds);
                const float interpolation = std::clamp((timeSeconds - left.TimeSeconds) / segmentLength, 0.0f, 1.0f);
                if (left.Interpolation == Assets::AnimationClipAsset::InterpolationMode::Step)
                    return left.Value;
                return glm::mix(left.Value, right.Value, interpolation);
            }

            return track.back().Value;
        }

        bool ShouldDispatchAnimationEventAtTime(float eventTimeSeconds,
                                                float previousTimeSeconds,
                                                float currentTimeSeconds,
                                                bool loopedThisFrame,
                                                float durationSeconds)
        {
            if (durationSeconds <= 0.0001f)
                return false;

            if (!loopedThisFrame)
            {
                if (currentTimeSeconds < previousTimeSeconds)
                    return false;
                if (previousTimeSeconds <= 0.0001f)
                    return eventTimeSeconds >= previousTimeSeconds && eventTimeSeconds <= currentTimeSeconds;
                return eventTimeSeconds > previousTimeSeconds && eventTimeSeconds <= currentTimeSeconds;
            }

            // Wrapped around clip end this frame.
            const bool inTail = eventTimeSeconds > previousTimeSeconds && eventTimeSeconds <= durationSeconds;
            const bool inHead = eventTimeSeconds >= 0.0f && eventTimeSeconds <= currentTimeSeconds;
            return inTail || inHead;
        }

        void UpdateAnimatorComponentForEntity(Scene& scene, entt::entity entity, float deltaTime, uint64_t dispatchFrame)
        {
            auto& registry = scene.GetRegistry();
            auto* animator = registry.try_get<AnimatorComponent>(entity);
            if (!animator)
                return;

            auto* eventReceiver = registry.try_get<AnimationEventReceiverComponent>(entity);
            if (eventReceiver)
            {
                eventReceiver->RuntimeDispatchedEvents.clear();
                eventReceiver->RuntimeDispatchFrame = dispatchFrame;
            }

            const std::string previousSpriteTextureOverrideKey = animator->RuntimeSpriteTextureOverrideKey;
            ResetAnimatorRuntimeOutput(*animator, false);

            if (!animator->Enabled || !scene.IsEntityEnabledInHierarchy(entity))
                return;

            const bool hasController = TryResolveAnimatorControllerAsset(*animator);
            const bool hasDefaultClip = TryResolveDefaultClipAsset(*animator);

            const Assets::AnimatorControllerAsset::Data* controllerData = nullptr;
            const Assets::AnimatorControllerAsset::StateDefinition* activeState = nullptr;
            if (hasController && animator->CachedController)
            {
                controllerData = &animator->CachedController->GetData();
                EnsureAnimatorParametersFromControllerDefaults(*animator, *controllerData);

                if (!animator->RuntimeInitialized)
                {
                    if (!controllerData->DefaultStateName.empty())
                        animator->RuntimeCurrentStateName = controllerData->DefaultStateName;
                    else if (!controllerData->States.empty())
                        animator->RuntimeCurrentStateName = controllerData->States.front().Name;
                    animator->RuntimeStateTimeSeconds = 0.0f;
                    animator->RuntimePreviousStateTimeSeconds = 0.0f;
                    animator->RuntimeInitialized = true;
                }

                activeState = FindControllerState(*controllerData, animator->RuntimeCurrentStateName);
                if (!activeState && !controllerData->States.empty())
                {
                    activeState = &controllerData->States.front();
                    animator->RuntimeCurrentStateName = activeState->Name;
                    animator->RuntimeStateTimeSeconds = 0.0f;
                    animator->RuntimePreviousStateTimeSeconds = 0.0f;
                }

                if (activeState)
                {
                    for (const auto& transition : activeState->Transitions)
                    {
                        if (!transition.CanTransitionToSelf && transition.ToState == activeState->Name)
                            continue;

                        if (transition.HasExitTime)
                        {
                            const float duration = std::max(0.0001f, animator->RuntimeCurrentStateDurationSeconds);
                            float normalizedTime = animator->RuntimeStateTimeSeconds / duration;
                            normalizedTime = std::clamp(normalizedTime, 0.0f, 1.0f);
                            if (normalizedTime < transition.ExitTimeNormalized)
                                continue;
                        }

                        std::vector<std::string> triggersToConsume;
                        if (!EvaluateAnimatorTransitionConditions(transition, *animator, triggersToConsume))
                            continue;

                        const auto* nextState = FindControllerState(*controllerData, transition.ToState);
                        if (!nextState)
                            continue;

                        for (const auto& triggerName : triggersToConsume)
                            animator->ResetTrigger(triggerName);

                        animator->RuntimeCurrentStateName = nextState->Name;
                        animator->RuntimeStateTimeSeconds = 0.0f;
                        animator->RuntimePreviousStateTimeSeconds = 0.0f;
                        activeState = nextState;
                        break;
                    }
                }
            }

            std::string resolvedClipKey;
            bool loopOverrideEnabled = false;
            bool loopOverrideValue = true;
            animator->RuntimeStateSpeedMultiplier = 1.0f;

            if (activeState)
            {
                resolvedClipKey = activeState->ClipKey;
                loopOverrideEnabled = activeState->LoopOverrideEnabled;
                loopOverrideValue = activeState->LoopOverride;
                animator->RuntimeStateSpeedMultiplier = std::max(0.0f, activeState->SpeedMultiplier);
            }

            if (resolvedClipKey.empty())
                resolvedClipKey = animator->DefaultClipKey;

            Assets::AnimationClipAsset::Ptr clipAsset;
            if (!resolvedClipKey.empty())
                (void)TryResolveAnimationClipAssetByKey(resolvedClipKey, clipAsset);
            if (!clipAsset && hasDefaultClip && animator->CachedDefaultClip)
                clipAsset = animator->CachedDefaultClip;

            if (!clipAsset)
            {
                animator->RuntimeCurrentClipKey.clear();
                animator->RuntimeCurrentStateDurationSeconds = 1.0f;
                return;
            }

            animator->RuntimeCurrentClipKey = clipAsset->GetKey();
            const auto& clipData = clipAsset->GetData();
            const float durationSeconds = std::max(0.0001f, clipData.DurationSeconds);
            animator->RuntimeCurrentStateDurationSeconds = durationSeconds;
            const bool clipLoops = loopOverrideEnabled ? loopOverrideValue : clipData.Loop;

            const float safeDeltaSeconds = std::max(0.0f, deltaTime);
            const float speed = std::max(0.0f, animator->PlaybackSpeed) * std::max(0.0f, animator->RuntimeStateSpeedMultiplier);

            const float previousTime = animator->RuntimeStateTimeSeconds;
            float currentTime = previousTime + safeDeltaSeconds * speed;
            bool loopedThisFrame = false;
            if (clipLoops)
            {
                if (currentTime >= durationSeconds)
                {
                    currentTime = std::fmod(currentTime, durationSeconds);
                    loopedThisFrame = true;
                }
            }
            else
            {
                currentTime = std::clamp(currentTime, 0.0f, durationSeconds);
            }

            animator->RuntimePreviousStateTimeSeconds = previousTime;
            animator->RuntimeStateTimeSeconds = currentTime;

            if (const auto* spriteSubRect = SampleStepTrackKeyframe(clipData.SpriteSubRectTrack, currentTime))
            {
                animator->RuntimeHasSpriteSubRect = true;
                animator->RuntimeSpriteUvMin = spriteSubRect->UvMin;
                animator->RuntimeSpriteUvMax = spriteSubRect->UvMax;
            }

            if (const auto* spriteTexture = SampleStepTrackKeyframe(clipData.SpriteTextureTrack, currentTime))
            {
                animator->RuntimeSpriteTextureOverrideKey = spriteTexture->TextureKey;
            }

            if (animator->RuntimeSpriteTextureOverrideKey != previousSpriteTextureOverrideKey)
            {
                animator->RuntimeCachedSpriteTextureOverride.reset();
                animator->RuntimeSpriteTextureOverrideLoadAttempted = false;
            }

            bool hasPositionSample = false;
            const glm::vec3 sampledPosition = SampleVector3Track(clipData.PositionTrack, currentTime, hasPositionSample);
            animator->RuntimeHasPosition = hasPositionSample;
            animator->RuntimePosition = sampledPosition;

            bool hasScaleSample = false;
            const glm::vec3 sampledScale = SampleVector3Track(clipData.ScaleTrack, currentTime, hasScaleSample);
            animator->RuntimeHasScale = hasScaleSample;
            animator->RuntimeScale = sampledScale;

            bool hasRotationSample = false;
            const float sampledRotation = SampleFloatTrack(clipData.RotationZTrack, currentTime, hasRotationSample);
            animator->RuntimeHasRotationZ = hasRotationSample;
            animator->RuntimeRotationZDegrees = sampledRotation;

            if (animator->ApplyToTransform)
            {
                if (auto* transform = registry.try_get<TransformComponent>(entity))
                {
                    if (animator->RuntimeHasPosition)
                        transform->Position = animator->RuntimePosition;
                    if (animator->RuntimeHasScale)
                        transform->Scale = animator->RuntimeScale;
                    if (animator->RuntimeHasRotationZ)
                        transform->Rotation.z = animator->RuntimeRotationZDegrees;
                }
            }

            if (eventReceiver && eventReceiver->Enabled && !clipData.EventTrack.empty())
            {
                const float normalizedTime = std::clamp(currentTime / durationSeconds, 0.0f, 1.0f);
                for (const auto& eventKeyframe : clipData.EventTrack)
                {
                    if (!ShouldDispatchAnimationEventAtTime(
                            eventKeyframe.TimeSeconds,
                            previousTime,
                            currentTime,
                            loopedThisFrame,
                            durationSeconds))
                    {
                        continue;
                    }

                    AnimationEventMessage message{};
                    message.Name = eventKeyframe.Name;
                    message.StringPayload = eventKeyframe.StringPayload;
                    message.FloatPayload = eventKeyframe.FloatPayload;
                    message.IntegerPayload = eventKeyframe.IntegerPayload;
                    message.BooleanPayload = eventKeyframe.BooleanPayload;
                    message.TimeSeconds = eventKeyframe.TimeSeconds;
                    message.NormalizedTime = normalizedTime;
                    eventReceiver->RuntimeDispatchedEvents.push_back(std::move(message));
                }
            }
        }

        void UpdateAnimation2DSystem(Scene& scene, float deltaTime, uint64_t dispatchFrame)
        {
            auto& registry = scene.GetRegistry();
            auto view = registry.view<AnimatorComponent>();
            for (entt::entity entity : view)
            {
                UpdateAnimatorComponentForEntity(scene, entity, deltaTime, dispatchFrame);
            }
        }

        // Keep clone/load runtime cleanup centralized so newly added components
        // only need one update point to stay Play Mode-safe.
        void ResetRuntimeStateForEntity(entt::registry& registry, entt::entity entity)
        {
            if (auto* sprite = registry.try_get<SpriteComponent>(entity))
            {
                sprite->CachedTexture.reset();
                sprite->TextureLoadAttempted = false;
            }

            if (auto* material = registry.try_get<MaterialComponent>(entity))
            {
                material->CachedMaterial.reset();
                material->MaterialLoadAttempted = false;
            }

            if (auto* uiText = registry.try_get<UITextComponent>(entity))
            {
                uiText->CachedFont.reset();
                uiText->FontLoadAttempted = false;
            }

            if (auto* uiButton = registry.try_get<UIButtonComponent>(entity))
            {
                uiButton->IsHovered = false;
                uiButton->IsPressed = false;
                uiButton->RuntimeHoverEnteredThisFrame = false;
                uiButton->RuntimeHoverExitedThisFrame = false;
                uiButton->RuntimePressedThisFrame = false;
                uiButton->RuntimeClickedThisFrame = false;
            }

            if (auto* uiSlider = registry.try_get<UISliderComponent>(entity))
            {
                uiSlider->RuntimeDragging = false;
                uiSlider->RuntimeValueChangedThisFrame = false;
            }

            if (auto* directionalLight = registry.try_get<DirectionalLight2DComponent>(entity))
                directionalLight->RuntimeResolvedDirection = glm::vec2(0.0f, -1.0f);

            if (auto* pointLight = registry.try_get<PointLight2DComponent>(entity))
            {
                pointLight->RuntimeViewportPosition = glm::vec2(0.0f);
                pointLight->RuntimeViewportRadius = 0.0f;
            }

            if (auto* shadowOccluder = registry.try_get<ShadowOccluder2DComponent>(entity))
            {
                shadowOccluder->RuntimeResolvedPolygonPoints.clear();
                shadowOccluder->RuntimeGeometryRevision = 0;
            }

            if (auto* audioSource = registry.try_get<AudioSourceComponent>(entity))
            {
                audioSource->RuntimeVoiceId = 0;
                audioSource->RuntimePlaybackStarted = false;
            }

            if (auto* rigidbody2D = registry.try_get<Rigidbody2DComponent>(entity))
            {
                rigidbody2D->RuntimeBodyId = kNullPhysics2DBody;
                rigidbody2D->RuntimeBodyCreated = false;
                rigidbody2D->RuntimePreviousPosition = glm::vec2(0.0f);
                rigidbody2D->RuntimePreviousAngleRadians = 0.0f;
                rigidbody2D->RuntimeRenderPreviousPosition = glm::vec2(0.0f);
                rigidbody2D->RuntimeRenderPreviousAngleRadians = 0.0f;
                rigidbody2D->RuntimeRenderCurrentPosition = glm::vec2(0.0f);
                rigidbody2D->RuntimeRenderCurrentAngleRadians = 0.0f;
                rigidbody2D->RuntimeLinearVelocity = glm::vec2(0.0f);
                rigidbody2D->RuntimePendingLinearVelocity = glm::vec2(0.0f);
                rigidbody2D->RuntimeHasPendingLinearVelocity = false;
                rigidbody2D->RuntimePendingLinearVelocityX = 0.0f;
                rigidbody2D->RuntimeHasPendingLinearVelocityX = false;
                rigidbody2D->RuntimePendingLinearVelocityY = 0.0f;
                rigidbody2D->RuntimeHasPendingLinearVelocityY = false;
                rigidbody2D->RuntimeContactCount = 0;
                rigidbody2D->RuntimeContactCountExcludingSensors = 0;
            }

            if (auto* boxCollider2D = registry.try_get<BoxCollider2DComponent>(entity))
            {
                boxCollider2D->RuntimeShapeId = kNullPhysics2DShape;
                boxCollider2D->RuntimeShapeCreated = false;
            }

            if (auto* circleCollider2D = registry.try_get<CircleCollider2DComponent>(entity))
            {
                circleCollider2D->RuntimeShapeId = kNullPhysics2DShape;
                circleCollider2D->RuntimeShapeCreated = false;
            }

            if (auto* joint2D = registry.try_get<Joint2DComponent>(entity))
            {
                joint2D->RuntimeJointId = kNullPhysics2DJoint;
                joint2D->RuntimeJointCreated = false;
            }

            if (auto* animator = registry.try_get<AnimatorComponent>(entity))
            {
                animator->CachedController.reset();
                animator->ControllerLoadAttempted = false;
                animator->CachedDefaultClip.reset();
                animator->DefaultClipLoadAttempted = false;
                animator->RuntimeInitialized = false;
                animator->RuntimeCurrentStateName.clear();
                animator->RuntimeCurrentClipKey.clear();
                animator->RuntimePreviousStateTimeSeconds = 0.0f;
                animator->RuntimeStateTimeSeconds = 0.0f;
                animator->RuntimeCurrentStateDurationSeconds = 1.0f;
                animator->RuntimeStateSpeedMultiplier = 1.0f;
                animator->ResetAllTriggers();
                ResetAnimatorRuntimeOutput(*animator, true);
            }

            if (auto* eventReceiver = registry.try_get<AnimationEventReceiverComponent>(entity))
            {
                eventReceiver->RuntimeDispatchedEvents.clear();
                eventReceiver->RuntimeDispatchFrame = 0;
            }

            if (auto* particleEmitter = registry.try_get<ParticleEmitterComponent>(entity))
            {
                particleEmitter->CachedTexture.reset();
                particleEmitter->TextureLoadAttempted = false;
                particleEmitter->RuntimeState.reset();
                particleEmitter->Playing = false;
                particleEmitter->Paused = false;
            }

            if (auto* nativeScript = registry.try_get<NativeScriptComponent>(entity))
            {
                for (auto& scriptEntry : nativeScript->Scripts)
                {
                    scriptEntry.RuntimeInitialized = false;
                    if (scriptEntry.RuntimeInstance)
                        Coroutine::StopAll(*scriptEntry.RuntimeInstance);
                    scriptEntry.RuntimeInstance.reset();
                    scriptEntry.RuntimeUpdateCount = 0;
                    scriptEntry.RuntimeWarnedOnUpdateTransformMutation = false;
                }
            }
        }

        bool CopyEntitySubtreeToScene(const Scene& sourceScene,
                                      Scene& destinationScene,
                                      entt::entity sourceRootEntity,
                                      entt::entity destinationParentEntity,
                                      entt::entity* outDestinationRootEntity)
        {
            if (!sourceScene.IsValid(sourceRootEntity))
                return false;

            const auto& sourceRegistry = sourceScene.GetRegistry();
            auto& destinationRegistry = destinationScene.GetRegistry();
            const entt::entity resolvedDestinationParentEntity =
                (destinationParentEntity != entt::null && destinationScene.IsValid(destinationParentEntity))
                ? destinationParentEntity
                : entt::null;

            std::vector<entt::entity> sourceEntities;
            sourceEntities.push_back(sourceRootEntity);
            for (size_t index = 0; index < sourceEntities.size(); ++index)
            {
                const auto children = sourceScene.GetChildren(sourceEntities[index]);
                sourceEntities.insert(sourceEntities.end(), children.begin(), children.end());
            }

            std::unordered_map<entt::entity, entt::entity> entityMap;
            entityMap.reserve(sourceEntities.size());

            for (entt::entity sourceEntity : sourceEntities)
            {
                const auto* sourceTag = sourceRegistry.try_get<TagComponent>(sourceEntity);
                const auto* sourceTransform = sourceRegistry.try_get<TransformComponent>(sourceEntity);
                if (!sourceTag || !sourceTransform)
                    return false;

                const entt::entity destinationEntity = destinationScene.CreateEntity(sourceTag->Tag);
                entityMap.emplace(sourceEntity, destinationEntity);
                if (auto* destinationTag = destinationRegistry.try_get<TagComponent>(destinationEntity))
                    destinationTag->Enabled = sourceTag->Enabled;
                destinationRegistry.replace<TransformComponent>(destinationEntity, *sourceTransform);

                if (const auto* sourceCanvas = sourceRegistry.try_get<CanvasComponent>(sourceEntity))
                    destinationRegistry.emplace<CanvasComponent>(destinationEntity, *sourceCanvas);

                if (const auto* sourceRectTransform = sourceRegistry.try_get<RectTransformComponent>(sourceEntity))
                    destinationRegistry.emplace<RectTransformComponent>(destinationEntity, *sourceRectTransform);

                if (const auto* sourceSprite = sourceRegistry.try_get<SpriteComponent>(sourceEntity))
                {
                    auto& destinationSprite = destinationRegistry.emplace<SpriteComponent>(destinationEntity);
                    destinationSprite.TextureKey = sourceSprite->TextureKey;
                    destinationSprite.CachedTexture.reset();
                    destinationSprite.TextureLoadAttempted = false;
                    destinationSprite.Color = sourceSprite->Color;
                    destinationSprite.TilingFactor = sourceSprite->TilingFactor;
                    destinationSprite.RenderOrder = sourceSprite->RenderOrder;
                    destinationSprite.CastShadows = sourceSprite->CastShadows;
                    destinationSprite.ReceiveShadows = sourceSprite->ReceiveShadows;
                }

                if (const auto* sourceAnimator = sourceRegistry.try_get<AnimatorComponent>(sourceEntity))
                    destinationRegistry.emplace<AnimatorComponent>(destinationEntity, *sourceAnimator);

                if (const auto* sourceAnimationEventReceiver = sourceRegistry.try_get<AnimationEventReceiverComponent>(sourceEntity))
                    destinationRegistry.emplace<AnimationEventReceiverComponent>(destinationEntity, *sourceAnimationEventReceiver);

                if (const auto* sourceMaterial = sourceRegistry.try_get<MaterialComponent>(sourceEntity))
                {
                    auto& destinationMaterial = destinationRegistry.emplace<MaterialComponent>(destinationEntity);
                    destinationMaterial.MaterialKey = sourceMaterial->MaterialKey;
                    destinationMaterial.CachedMaterial.reset();
                    destinationMaterial.MaterialLoadAttempted = false;
                }

                if (const auto* sourceDirectionalLight = sourceRegistry.try_get<DirectionalLight2DComponent>(sourceEntity))
                    destinationRegistry.emplace<DirectionalLight2DComponent>(destinationEntity, *sourceDirectionalLight);

                if (const auto* sourcePointLight = sourceRegistry.try_get<PointLight2DComponent>(sourceEntity))
                    destinationRegistry.emplace<PointLight2DComponent>(destinationEntity, *sourcePointLight);

                if (const auto* sourceShadowOccluder = sourceRegistry.try_get<ShadowOccluder2DComponent>(sourceEntity))
                    destinationRegistry.emplace<ShadowOccluder2DComponent>(destinationEntity, *sourceShadowOccluder);

                if (const auto* sourceUIImage = sourceRegistry.try_get<UIImageComponent>(sourceEntity))
                    destinationRegistry.emplace<UIImageComponent>(destinationEntity, *sourceUIImage);

                if (const auto* sourceUIPanel = sourceRegistry.try_get<UIPanelComponent>(sourceEntity))
                    destinationRegistry.emplace<UIPanelComponent>(destinationEntity, *sourceUIPanel);

                if (const auto* sourceUIText = sourceRegistry.try_get<UITextComponent>(sourceEntity))
                {
                    auto& destinationUIText = destinationRegistry.emplace<UITextComponent>(destinationEntity);
                    destinationUIText.Text = sourceUIText->Text;
                    destinationUIText.FontFilePath = sourceUIText->FontFilePath;
                    destinationUIText.CachedFont.reset();
                    destinationUIText.FontLoadAttempted = false;
                    destinationUIText.FontSize = sourceUIText->FontSize;
                    destinationUIText.Color = sourceUIText->Color;
                    destinationUIText.RaycastTarget = sourceUIText->RaycastTarget;
                }

                if (const auto* sourceUIButton = sourceRegistry.try_get<UIButtonComponent>(sourceEntity))
                    destinationRegistry.emplace<UIButtonComponent>(destinationEntity, *sourceUIButton);

                if (const auto* sourceUISlider = sourceRegistry.try_get<UISliderComponent>(sourceEntity))
                    destinationRegistry.emplace<UISliderComponent>(destinationEntity, *sourceUISlider);

                if (const auto* sourceCamera = sourceRegistry.try_get<CameraComponent>(sourceEntity))
                    destinationRegistry.emplace<CameraComponent>(destinationEntity, *sourceCamera);

                if (const auto* sourceAudioListener = sourceRegistry.try_get<AudioListener2DComponent>(sourceEntity))
                    destinationRegistry.emplace<AudioListener2DComponent>(destinationEntity, *sourceAudioListener);

                if (const auto* sourceAudio = sourceRegistry.try_get<AudioSourceComponent>(sourceEntity))
                {
                    auto& destinationAudio = destinationRegistry.emplace<AudioSourceComponent>(destinationEntity);
                    destinationAudio.AudioClipKey = sourceAudio->AudioClipKey;
                    destinationAudio.Volume = sourceAudio->Volume;
                    destinationAudio.Pitch = sourceAudio->Pitch;
                    destinationAudio.PlayOnStart = sourceAudio->PlayOnStart;
                    destinationAudio.Loop = sourceAudio->Loop;
                    destinationAudio.Muted = sourceAudio->Muted;
                    destinationAudio.Space = sourceAudio->Space;
                    destinationAudio.MixerGroup = sourceAudio->MixerGroup;
                    destinationAudio.SpatialMinDistance = sourceAudio->SpatialMinDistance;
                    destinationAudio.SpatialMaxDistance = sourceAudio->SpatialMaxDistance;
                    destinationAudio.SpatialRolloffExponent = sourceAudio->SpatialRolloffExponent;
                    destinationAudio.StereoPanStrength = sourceAudio->StereoPanStrength;
                    destinationAudio.AttenuationCurveKey = sourceAudio->AttenuationCurveKey;
                    destinationAudio.RuntimeVoiceId = 0;
                    destinationAudio.RuntimePlaybackStarted = false;
                }

                if (const auto* sourceRigidbody2D = sourceRegistry.try_get<Rigidbody2DComponent>(sourceEntity))
                    destinationRegistry.emplace<Rigidbody2DComponent>(destinationEntity, *sourceRigidbody2D);

                if (const auto* sourceBoxCollider2D = sourceRegistry.try_get<BoxCollider2DComponent>(sourceEntity))
                    destinationRegistry.emplace<BoxCollider2DComponent>(destinationEntity, *sourceBoxCollider2D);

                if (const auto* sourceCircleCollider2D = sourceRegistry.try_get<CircleCollider2DComponent>(sourceEntity))
                    destinationRegistry.emplace<CircleCollider2DComponent>(destinationEntity, *sourceCircleCollider2D);

                if (const auto* sourceJoint2D = sourceRegistry.try_get<Joint2DComponent>(sourceEntity))
                    destinationRegistry.emplace<Joint2DComponent>(destinationEntity, *sourceJoint2D);

                if (const auto* sourceScripts = sourceRegistry.try_get<NativeScriptComponent>(sourceEntity))
                {
                    auto& destinationScripts = destinationRegistry.emplace<NativeScriptComponent>(destinationEntity);
                    destinationScripts.Scripts.reserve(sourceScripts->Scripts.size());
                    for (const auto& sourceScriptEntry : sourceScripts->Scripts)
                    {
                        auto& destinationScriptEntry = destinationScripts.Scripts.emplace_back();
                        destinationScriptEntry.ScriptClassName = sourceScriptEntry.ScriptClassName;
                        destinationScriptEntry.ScriptAssetRelativePath = sourceScriptEntry.ScriptAssetRelativePath;
                        destinationScriptEntry.Enabled = sourceScriptEntry.Enabled;
                        destinationScriptEntry.ExposedProperties = sourceScriptEntry.ExposedProperties;
                        destinationScriptEntry.RuntimeInitialized = false;
                        destinationScriptEntry.RuntimeInstance.reset();
                    }
                }

                if (const auto* sourceParticleEmitter = sourceRegistry.try_get<ParticleEmitterComponent>(sourceEntity))
                    destinationRegistry.emplace<ParticleEmitterComponent>(destinationEntity, *sourceParticleEmitter);

                if (const auto* sourcePrefabInstance = sourceRegistry.try_get<PrefabInstanceComponent>(sourceEntity))
                    destinationRegistry.emplace<PrefabInstanceComponent>(destinationEntity, *sourcePrefabInstance);

                ResetRuntimeStateForEntity(destinationRegistry, destinationEntity);
            }

            for (entt::entity sourceEntity : sourceEntities)
            {
                const auto mappedEntity = entityMap.find(sourceEntity);
                if (mappedEntity == entityMap.end())
                    continue;
                const entt::entity destinationEntity = mappedEntity->second;

                auto* destinationHierarchy = destinationRegistry.try_get<HierarchyComponent>(destinationEntity);
                if (!destinationHierarchy)
                    destinationHierarchy = &destinationRegistry.emplace<HierarchyComponent>(destinationEntity);

                const entt::entity sourceParent = sourceScene.GetParent(sourceEntity);
                if (sourceParent != entt::null)
                {
                    const auto mappedParent = entityMap.find(sourceParent);
                    destinationHierarchy->Parent = (mappedParent != entityMap.end()) ? mappedParent->second : resolvedDestinationParentEntity;
                }
                else
                {
                    destinationHierarchy->Parent = resolvedDestinationParentEntity;
                }

                if (const auto* sourceHierarchy = sourceRegistry.try_get<HierarchyComponent>(sourceEntity))
                    destinationHierarchy->SiblingOrder = sourceHierarchy->SiblingOrder;
                else
                    destinationHierarchy->SiblingOrder = 0;

                // Preserve prefab-authored root world transform even when instantiated under
                // a non-null parent. Without this, direct parent assignment can skew scale/size
                // and offset position due to inherited parent transforms.
                if (sourceEntity == sourceRootEntity && resolvedDestinationParentEntity != entt::null)
                {
                    if (auto* destinationTransform = destinationRegistry.try_get<TransformComponent>(destinationEntity))
                    {
                        const glm::mat4 sourceRootWorld = sourceScene.GetWorldTransformMatrix(sourceEntity);
                        const glm::mat4 parentWorld = destinationScene.GetWorldTransformMatrix(resolvedDestinationParentEntity);
                        const glm::mat4 childLocal = glm::inverse(parentWorld) * sourceRootWorld;

                        glm::vec3 skew(0.0f);
                        glm::vec4 perspective(0.0f);
                        glm::quat orientation(1.0f, 0.0f, 0.0f, 0.0f);
                        glm::vec3 translation(0.0f);
                        glm::vec3 scale(1.0f);
                        if (glm::decompose(childLocal, scale, orientation, translation, skew, perspective))
                        {
                            destinationTransform->Position = translation;
                            destinationTransform->Rotation = glm::degrees(glm::eulerAngles(orientation));
                            destinationTransform->Scale = scale;
                        }
                    }
                }
            }

            for (entt::entity sourceEntity : sourceEntities)
            {
                const auto* sourceJoint = sourceRegistry.try_get<Joint2DComponent>(sourceEntity);
                if (!sourceJoint)
                    continue;

                const auto mappedEntity = entityMap.find(sourceEntity);
                if (mappedEntity == entityMap.end())
                    continue;

                auto* destinationJoint = destinationRegistry.try_get<Joint2DComponent>(mappedEntity->second);
                if (!destinationJoint)
                    continue;

                if (sourceJoint->ConnectedEntity == entt::null)
                {
                    destinationJoint->ConnectedEntity = entt::null;
                    continue;
                }

                const auto mappedConnectedEntity = entityMap.find(sourceJoint->ConnectedEntity);
                destinationJoint->ConnectedEntity = (mappedConnectedEntity != entityMap.end())
                    ? mappedConnectedEntity->second
                    : entt::null;
            }

            const auto mappedRoot = entityMap.find(sourceRootEntity);
            if (mappedRoot == entityMap.end())
                return false;

            if (outDestinationRootEntity)
                *outDestinationRootEntity = mappedRoot->second;
            return true;
        }

    }

    Scene::Scene() = default;

    Scene::~Scene()
    {
        if (m_Physics2DWorld)
            m_Physics2DWorld->Shutdown(*this);

        auto view = m_Registry.view<NativeScriptComponent>();
        for (entt::entity entity : view)
        {
            (void)entity;
            auto& nativeScript = view.get<NativeScriptComponent>(entity);
            for (auto& scriptEntry : nativeScript.Scripts)
            {
                if (scriptEntry.RuntimeInstance)
                {
                    if (scriptEntry.RuntimeInitialized)
                        scriptEntry.RuntimeInstance->OnDestroy();
                    Coroutine::StopAll(*scriptEntry.RuntimeInstance);
                    scriptEntry.RuntimeInstance.reset();
                }
                scriptEntry.RuntimeInitialized = false;
                scriptEntry.RuntimeUpdateCount = 0;
                scriptEntry.RuntimeWarnedOnUpdateTransformMutation = false;
            }
        }
    }

    entt::entity Scene::CreateEntity(const std::string& name)
    {
        entt::entity entity = m_Registry.create();
        TagComponent tag{};
        tag.Tag = name;
        tag.Enabled = true;
        m_Registry.emplace<TagComponent>(entity, std::move(tag));
        m_Registry.emplace<TransformComponent>(entity);
        auto& hierarchy = m_Registry.emplace<HierarchyComponent>(entity);

        int32_t maxSiblingOrder = -kSiblingOrderStep;
        auto hierarchyView = m_Registry.view<HierarchyComponent>();
        for (entt::entity otherEntity : hierarchyView)
        {
            if (otherEntity == entity)
                continue;
            const auto& otherHierarchy = hierarchyView.get<HierarchyComponent>(otherEntity);
            if (otherHierarchy.Parent == entt::null)
                maxSiblingOrder = std::max(maxSiblingOrder, otherHierarchy.SiblingOrder);
        }
        hierarchy.SiblingOrder = maxSiblingOrder + kSiblingOrderStep;
        return entity;
    }

    Entity Scene::CreateEntityWrapped(const std::string& name)
    {
        return Entity(&m_Registry, CreateEntity(name));
    }

    entt::entity Scene::InstantiatePrefab(const std::string& prefabAssetKey, entt::entity parentEntity)
    {
        if (prefabAssetKey.empty())
            return entt::null;

        auto loadedPrefabSceneResult = Scene::LoadFromFile(prefabAssetKey);
        if (const auto resolvedPath = Assets::ResolveAssetKeyToPath(prefabAssetKey); resolvedPath.IsSuccess())
        {
            auto resolvedLoadResult = Scene::LoadFromFile(resolvedPath.GetValue());
            if (!resolvedLoadResult.IsFailure())
                loadedPrefabSceneResult = std::move(resolvedLoadResult);
        }

        if (loadedPrefabSceneResult.IsFailure())
        {
            LT_WARN("Scene::InstantiatePrefab failed for '{}': {}",
                    prefabAssetKey,
                    loadedPrefabSceneResult.GetError().GetErrorMessage());
            return entt::null;
        }

        auto& loadedPrefabScene = *loadedPrefabSceneResult.GetValue();
        const auto prefabRoots = loadedPrefabScene.GetChildren(entt::null);
        if (prefabRoots.empty())
            return entt::null;

        entt::entity createdRoot = entt::null;
        if (!CopyEntitySubtreeToScene(loadedPrefabScene, *this, prefabRoots.front(), parentEntity, &createdRoot))
            return entt::null;

        if (createdRoot == entt::null || !IsValid(createdRoot))
            return entt::null;

        auto& prefabInstance = m_Registry.emplace_or_replace<PrefabInstanceComponent>(createdRoot);
        prefabInstance.PrefabAssetKey = prefabAssetKey;
        return createdRoot;
    }

    void Scene::DestroyEntity(entt::entity entity)
    {
        if (!IsValid(entity))
            return;

        const auto children = GetChildren(entity);
        for (entt::entity child : children)
            DestroyEntity(child);

        if (auto* nativeScript = m_Registry.try_get<NativeScriptComponent>(entity))
        {
            for (auto& scriptEntry : nativeScript->Scripts)
            {
                if (scriptEntry.RuntimeInstance)
                {
                    if (scriptEntry.RuntimeInitialized)
                        scriptEntry.RuntimeInstance->OnDestroy();
                    Coroutine::StopAll(*scriptEntry.RuntimeInstance);
                    scriptEntry.RuntimeInstance.reset();
                }
                scriptEntry.RuntimeInitialized = false;
                scriptEntry.RuntimeUpdateCount = 0;
                scriptEntry.RuntimeWarnedOnUpdateTransformMutation = false;
            }
        }

        m_Registry.destroy(entity);
        ResetPhysicsRuntimeState();
    }

    bool Scene::IsValid(entt::entity entity) const
    {
        return m_Registry.valid(entity);
    }

    bool Scene::IsEntityEnabledInHierarchy(entt::entity entity) const
    {
        if (!IsValid(entity))
            return false;

        entt::entity current = entity;
        while (current != entt::null)
        {
            const auto* tag = m_Registry.try_get<TagComponent>(current);
            if (tag && !tag->Enabled)
                return false;
            current = GetParent(current);
        }
        return true;
    }

    bool Scene::SetParent(entt::entity child, entt::entity parent)
    {
        if (!IsValid(child))
            return false;

        if (parent != entt::null && !IsValid(parent))
            return false;

        if (child == parent)
            return false;

        // Prevent hierarchy cycles.
        if (parent != entt::null && IsDescendantOf(parent, child))
            return false;

        const glm::mat4 childWorldBefore = GetWorldTransformMatrix(child);

        auto* hierarchy = m_Registry.try_get<HierarchyComponent>(child);
        if (!hierarchy)
            hierarchy = &m_Registry.emplace<HierarchyComponent>(child);

        if (hierarchy->Parent == parent)
            return true;

        hierarchy->Parent = parent;
        int32_t maxSiblingOrder = -kSiblingOrderStep;
        auto hierarchyView = m_Registry.view<HierarchyComponent>();
        for (entt::entity entity : hierarchyView)
        {
            if (entity == child)
                continue;
            const auto& otherHierarchy = hierarchyView.get<HierarchyComponent>(entity);
            if (otherHierarchy.Parent == parent)
                maxSiblingOrder = std::max(maxSiblingOrder, otherHierarchy.SiblingOrder);
        }
        hierarchy->SiblingOrder = maxSiblingOrder + kSiblingOrderStep;

        if (auto* childTransform = m_Registry.try_get<TransformComponent>(child))
        {
            const glm::mat4 parentWorld = (parent != entt::null) ? GetWorldTransformMatrix(parent) : glm::mat4(1.0f);
            const glm::mat4 childLocal = glm::inverse(parentWorld) * childWorldBefore;

            glm::vec3 skew(0.0f);
            glm::vec4 perspective(0.0f);
            glm::quat orientation(1.0f, 0.0f, 0.0f, 0.0f);
            glm::vec3 translation(0.0f);
            glm::vec3 scale(1.0f);
            if (glm::decompose(childLocal, scale, orientation, translation, skew, perspective))
            {
                childTransform->Position = translation;
                childTransform->Rotation = glm::degrees(glm::eulerAngles(orientation));
                childTransform->Scale = scale;
            }
        }

        return true;
    }

    bool Scene::SetSiblingOrderBefore(entt::entity entity, entt::entity targetSibling)
    {
        if (!IsValid(entity) || !IsValid(targetSibling) || entity == targetSibling)
            return false;

        const entt::entity targetParent = GetParent(targetSibling);
        if (!SetParent(entity, targetParent))
            return false;

        auto siblings = GetChildren(targetParent);
        siblings.erase(std::remove(siblings.begin(), siblings.end(), entity), siblings.end());
        const auto insertIt = std::find(siblings.begin(), siblings.end(), targetSibling);
        if (insertIt == siblings.end())
            return false;
        siblings.insert(insertIt, entity);

        for (size_t index = 0; index < siblings.size(); ++index)
        {
            auto* hierarchy = m_Registry.try_get<HierarchyComponent>(siblings[index]);
            if (hierarchy)
                hierarchy->SiblingOrder = static_cast<int32_t>(index * kSiblingOrderStep);
        }
        return true;
    }

    bool Scene::SetSiblingOrderAfter(entt::entity entity, entt::entity targetSibling)
    {
        if (!IsValid(entity) || !IsValid(targetSibling) || entity == targetSibling)
            return false;

        const entt::entity targetParent = GetParent(targetSibling);
        if (!SetParent(entity, targetParent))
            return false;

        auto siblings = GetChildren(targetParent);
        siblings.erase(std::remove(siblings.begin(), siblings.end(), entity), siblings.end());
        const auto targetIt = std::find(siblings.begin(), siblings.end(), targetSibling);
        if (targetIt == siblings.end())
            return false;
        siblings.insert(std::next(targetIt), entity);

        for (size_t index = 0; index < siblings.size(); ++index)
        {
            auto* hierarchy = m_Registry.try_get<HierarchyComponent>(siblings[index]);
            if (hierarchy)
                hierarchy->SiblingOrder = static_cast<int32_t>(index * kSiblingOrderStep);
        }
        return true;
    }

    entt::entity Scene::GetParent(entt::entity entity) const
    {
        if (!IsValid(entity))
            return entt::null;

        const auto* hierarchy = m_Registry.try_get<HierarchyComponent>(entity);
        if (!hierarchy)
            return entt::null;

        if (!IsValid(hierarchy->Parent))
            return entt::null;

        return hierarchy->Parent;
    }

    bool Scene::IsDescendantOf(entt::entity entity, entt::entity potentialAncestor) const
    {
        if (!IsValid(entity) || !IsValid(potentialAncestor))
            return false;

        entt::entity current = GetParent(entity);
        while (current != entt::null)
        {
            if (current == potentialAncestor)
                return true;
            current = GetParent(current);
        }

        return false;
    }

    std::vector<entt::entity> Scene::GetChildren(entt::entity parent) const
    {
        std::vector<entt::entity> children;
        if (parent != entt::null && !IsValid(parent))
            return children;

        auto view = m_Registry.view<HierarchyComponent>();
        for (entt::entity entity : view)
        {
            const auto& hierarchy = view.get<HierarchyComponent>(entity);
            if (hierarchy.Parent == parent)
                children.push_back(entity);
        }

        std::sort(children.begin(), children.end(), [this](entt::entity left, entt::entity right) {
            const auto* leftHierarchy = m_Registry.try_get<HierarchyComponent>(left);
            const auto* rightHierarchy = m_Registry.try_get<HierarchyComponent>(right);
            const int32_t leftOrder = leftHierarchy ? leftHierarchy->SiblingOrder : 0;
            const int32_t rightOrder = rightHierarchy ? rightHierarchy->SiblingOrder : 0;
            if (leftOrder != rightOrder)
                return leftOrder < rightOrder;
            return static_cast<uint32_t>(left) < static_cast<uint32_t>(right);
        });

        return children;
    }

    glm::mat4 Scene::GetWorldTransformMatrix(entt::entity entity) const
    {
        if (!IsValid(entity))
            return glm::mat4(1.0f);

        std::vector<entt::entity> chain;
        entt::entity current = entity;
        while (current != entt::null && IsValid(current))
        {
            chain.push_back(current);
            current = GetParent(current);
        }

        glm::mat4 worldMatrix(1.0f);
        for (auto it = chain.rbegin(); it != chain.rend(); ++it)
        {
            const auto* transform = m_Registry.try_get<TransformComponent>(*it);
            if (transform)
                worldMatrix *= transform->GetLocalMatrix();
        }

        return worldMatrix;
    }

    glm::mat4 Scene::GetWorldTransformMatrixForRendering(entt::entity entity, float interpolationAlpha) const
    {
        if (!IsValid(entity))
            return glm::mat4(1.0f);

        const float alpha = std::clamp(interpolationAlpha, 0.0f, 1.0f);
        std::vector<entt::entity> chain;
        entt::entity current = entity;
        while (current != entt::null && IsValid(current))
        {
            chain.push_back(current);
            current = GetParent(current);
        }

        glm::mat4 worldMatrix(1.0f);
        for (auto it = chain.rbegin(); it != chain.rend(); ++it)
        {
            const auto* transform = m_Registry.try_get<TransformComponent>(*it);
            if (!transform)
                continue;

            TransformComponent localForRender = *transform;
            const auto* rigidbody = m_Registry.try_get<Rigidbody2DComponent>(*it);
            if (rigidbody &&
                rigidbody->RuntimeBodyCreated &&
                rigidbody->Interpolate &&
                rigidbody->Type == Rigidbody2DComponent::BodyType::Kinematic)
            {
                localForRender.Position.x = glm::mix(rigidbody->RuntimeRenderPreviousPosition.x, rigidbody->RuntimeRenderCurrentPosition.x, alpha);
                localForRender.Position.y = glm::mix(rigidbody->RuntimeRenderPreviousPosition.y, rigidbody->RuntimeRenderCurrentPosition.y, alpha);
                const float angleDelta = WrapAngleRadians(rigidbody->RuntimeRenderCurrentAngleRadians - rigidbody->RuntimeRenderPreviousAngleRadians);
                localForRender.Rotation.z = glm::degrees(rigidbody->RuntimeRenderPreviousAngleRadians + angleDelta * alpha);
            }

            worldMatrix *= localForRender.GetLocalMatrix();
        }

        return worldMatrix;
    }

    void Scene::Update(float deltaTime)
    {
        Physics2DQueries::SetActiveSceneForScriptQueries(this);
        if (Application::HasInstance())
        {
            auto& window = Application::GetInstance().GetWindow();
            ProcessUiInteractionSystem(*this, window.GetWidth(), window.GetHeight());
        }
        else
        {
            ProcessUiInteractionSystem(*this, 0, 0);
        }
        static uint64_t s_AnimationDispatchFrameCounter = 0;
        auto view = m_Registry.view<NativeScriptComponent>();
        for (entt::entity entity : view)
        {
            auto& nativeScript = view.get<NativeScriptComponent>(entity);
            for (auto& scriptEntry : nativeScript.Scripts)
            {
                if (!scriptEntry.Enabled || scriptEntry.ScriptClassName.empty() || !IsEntityEnabledInHierarchy(entity))
                {
                    if (scriptEntry.RuntimeInstance)
                    {
                        if (scriptEntry.RuntimeInitialized)
                            scriptEntry.RuntimeInstance->OnDestroy();
                        Coroutine::StopAll(*scriptEntry.RuntimeInstance);
                        scriptEntry.RuntimeInstance.reset();
                    }
                    scriptEntry.RuntimeInitialized = false;
                    scriptEntry.RuntimeUpdateCount = 0;
                    scriptEntry.RuntimeWarnedOnUpdateTransformMutation = false;
                    scriptEntry.RuntimeWarnedMissingCompiledScript = false;
                    continue;
                }

                if (!scriptEntry.RuntimeInstance)
                {
                    scriptEntry.RuntimeInstance = NativeScriptRegistry::CreateScript(scriptEntry.ScriptClassName);
                    if (!scriptEntry.RuntimeInstance)
                    {
                        const std::string resolvedClassName = ResolveRegisteredScriptClassName(scriptEntry.ScriptClassName,
                                                                                               scriptEntry.ScriptAssetRelativePath);
                        if (!resolvedClassName.empty())
                        {
                            scriptEntry.ScriptClassName = resolvedClassName;
                            scriptEntry.RuntimeInstance = NativeScriptRegistry::CreateScript(scriptEntry.ScriptClassName);
                        }
                    }
                    if (scriptEntry.RuntimeInstance)
                    {
                        scriptEntry.RuntimeInstance->m_Scene = this;
                        scriptEntry.RuntimeInstance->m_Registry = &m_Registry;
                        scriptEntry.RuntimeInstance->m_EntityHandle = entity;
                        scriptEntry.RuntimeInstance->m_ExposedProperties = &scriptEntry.ExposedProperties;
                        scriptEntry.RuntimeInitialized = false;
                        scriptEntry.RuntimeUpdateCount = 0;
                        scriptEntry.RuntimeWarnedOnUpdateTransformMutation = false;
                        scriptEntry.RuntimeWarnedMissingCompiledScript = false;
                    }
                }

                if (!scriptEntry.RuntimeInstance)
                {
                    if (!scriptEntry.RuntimeWarnedMissingCompiledScript)
                    {
                        const auto* tag = m_Registry.try_get<TagComponent>(entity);
                        LT_WARN("Script '{}' on entity '{}' is not compiled/registered in ScriptCore. Build project scripts before entering Play Mode.",
                                scriptEntry.ScriptClassName,
                                tag ? tag->Tag : "Entity");
                        scriptEntry.RuntimeWarnedMissingCompiledScript = true;
                    }
                    continue;
                }

                // Rebind runtime context every frame. NativeScriptEntry objects can move in memory
                // when the scripts vector grows/reorders, so cached pointers must be refreshed.
                scriptEntry.RuntimeInstance->m_Scene = this;
                scriptEntry.RuntimeInstance->m_Registry = &m_Registry;
                scriptEntry.RuntimeInstance->m_EntityHandle = entity;
                scriptEntry.RuntimeInstance->m_ExposedProperties = &scriptEntry.ExposedProperties;

                if (!scriptEntry.RuntimeInitialized)
                {
                    scriptEntry.RuntimeInstance->OnSynchronizeExposedFields();
                    scriptEntry.RuntimeInstance->OnCreate();
                    scriptEntry.RuntimeInitialized = true;
                }

                TransformComponent transformBeforeUpdate{};
                bool trackTransformMutation = false;
                if (auto* transform = m_Registry.try_get<TransformComponent>(entity))
                {
                    transformBeforeUpdate = *transform;
                    if (const auto* rigidbody2D = m_Registry.try_get<Rigidbody2DComponent>(entity))
                    {
                        trackTransformMutation = rigidbody2D->Type == Rigidbody2DComponent::BodyType::Dynamic ||
                                                rigidbody2D->Type == Rigidbody2DComponent::BodyType::Kinematic;
                    }
                }

                scriptEntry.RuntimeInstance->OnSynchronizeExposedFields();
                scriptEntry.RuntimeInstance->OnUpdate(deltaTime);
                Coroutine::TickOwner(*scriptEntry.RuntimeInstance, deltaTime);
                ++scriptEntry.RuntimeUpdateCount;

                if (trackTransformMutation && !scriptEntry.RuntimeWarnedOnUpdateTransformMutation)
                {
                    const auto* transformAfterUpdate = m_Registry.try_get<TransformComponent>(entity);
                    if (transformAfterUpdate)
                    {
                        constexpr float kGuardrailEpsilon = 0.0001f;
                        const bool positionChanged = glm::length(transformAfterUpdate->Position - transformBeforeUpdate.Position) > kGuardrailEpsilon;
                        const bool rotationChanged = glm::length(transformAfterUpdate->Rotation - transformBeforeUpdate.Rotation) > kGuardrailEpsilon;
                        const bool scaleChanged = glm::length(transformAfterUpdate->Scale - transformBeforeUpdate.Scale) > kGuardrailEpsilon;
                        if (positionChanged || rotationChanged || scaleChanged)
                        {
                            const auto* tag = m_Registry.try_get<TagComponent>(entity);
                            LT_WARN("Script '{}' on entity '{}' is mutating Transform in OnUpdate while Rigidbody2D is Dynamic/Kinematic. Move physics-related transform writes to OnFixedUpdate for stable simulation.",
                                    scriptEntry.ScriptClassName,
                                    tag ? tag->Tag : "Entity");
                            scriptEntry.RuntimeWarnedOnUpdateTransformMutation = true;
                        }
                    }
                }
            }
        }

        UpdateAnimation2DSystem(*this, deltaTime, ++s_AnimationDispatchFrameCounter);

        UpdateParticleEmitterSystem(m_Registry, deltaTime);
    }

    void Scene::FixedUpdate(float fixedDeltaTime)
    {
        Physics2DQueries::SetActiveSceneForScriptQueries(this);
        auto view = m_Registry.view<NativeScriptComponent>();
        for (entt::entity entity : view)
        {
            auto& nativeScript = view.get<NativeScriptComponent>(entity);
            for (auto& scriptEntry : nativeScript.Scripts)
            {
                if (!scriptEntry.Enabled || scriptEntry.ScriptClassName.empty() || !IsEntityEnabledInHierarchy(entity))
                {
                    if (scriptEntry.RuntimeInstance)
                    {
                        if (scriptEntry.RuntimeInitialized)
                            scriptEntry.RuntimeInstance->OnDestroy();
                        Coroutine::StopAll(*scriptEntry.RuntimeInstance);
                        scriptEntry.RuntimeInstance.reset();
                    }
                    scriptEntry.RuntimeInitialized = false;
                    scriptEntry.RuntimeUpdateCount = 0;
                    scriptEntry.RuntimeWarnedMissingCompiledScript = false;
                    continue;
                }

                if (!scriptEntry.RuntimeInstance)
                {
                    scriptEntry.RuntimeInstance = NativeScriptRegistry::CreateScript(scriptEntry.ScriptClassName);
                    if (!scriptEntry.RuntimeInstance)
                    {
                        const std::string resolvedClassName = ResolveRegisteredScriptClassName(scriptEntry.ScriptClassName,
                                                                                               scriptEntry.ScriptAssetRelativePath);
                        if (!resolvedClassName.empty())
                        {
                            scriptEntry.ScriptClassName = resolvedClassName;
                            scriptEntry.RuntimeInstance = NativeScriptRegistry::CreateScript(scriptEntry.ScriptClassName);
                        }
                    }
                    if (scriptEntry.RuntimeInstance)
                    {
                        scriptEntry.RuntimeInstance->m_Scene = this;
                        scriptEntry.RuntimeInstance->m_Registry = &m_Registry;
                        scriptEntry.RuntimeInstance->m_EntityHandle = entity;
                        scriptEntry.RuntimeInstance->m_ExposedProperties = &scriptEntry.ExposedProperties;
                        scriptEntry.RuntimeInitialized = false;
                        scriptEntry.RuntimeUpdateCount = 0;
                        scriptEntry.RuntimeWarnedOnUpdateTransformMutation = false;
                        scriptEntry.RuntimeWarnedMissingCompiledScript = false;
                    }
                }

                if (!scriptEntry.RuntimeInstance)
                {
                    if (!scriptEntry.RuntimeWarnedMissingCompiledScript)
                    {
                        const auto* tag = m_Registry.try_get<TagComponent>(entity);
                        LT_WARN("Script '{}' on entity '{}' is not compiled/registered in ScriptCore. Build project scripts before entering Play Mode.",
                                scriptEntry.ScriptClassName,
                                tag ? tag->Tag : "Entity");
                        scriptEntry.RuntimeWarnedMissingCompiledScript = true;
                    }
                    continue;
                }

                scriptEntry.RuntimeInstance->m_Scene = this;
                scriptEntry.RuntimeInstance->m_Registry = &m_Registry;
                scriptEntry.RuntimeInstance->m_EntityHandle = entity;
                scriptEntry.RuntimeInstance->m_ExposedProperties = &scriptEntry.ExposedProperties;

                if (!scriptEntry.RuntimeInitialized)
                {
                    scriptEntry.RuntimeInstance->OnSynchronizeExposedFields();
                    scriptEntry.RuntimeInstance->OnCreate();
                    scriptEntry.RuntimeInitialized = true;
                }

                scriptEntry.RuntimeInstance->OnSynchronizeExposedFields();
                scriptEntry.RuntimeInstance->OnFixedUpdate(fixedDeltaTime);
            }
        }
    }

    void Scene::BeginLoadingState()
    {
        m_LoadState = LoadState::Loading;
        m_SceneObjectsInitialized = false;
        m_PhysicsWorldInitializedForLoading = false;
    }

    void Scene::MarkSceneObjectsInitialized()
    {
        m_SceneObjectsInitialized = true;
    }

    bool Scene::InitializePhysicsWorldForLoading()
    {
        if (m_PhysicsWorldInitializedForLoading)
            return true;

        if (!m_Physics2DWorld)
            m_Physics2DWorld = std::make_unique<Physics2DWorld>();

        if (!m_Physics2DWorld->IsInitialized())
            m_Physics2DWorld->Initialize(m_Physics2DSettings);

        m_Physics2DWorld->SetSettings(m_Physics2DSettings);
        m_Physics2DWorld->RebuildScene(*this);
        m_PhysicsWorldInitializedForLoading = true;
        return true;
    }

    void Scene::SetLoadStateReady()
    {
        m_LoadState = LoadState::Ready;
        m_SceneObjectsInitialized = true;
        m_PhysicsWorldInitializedForLoading = true;
    }

    void Scene::StepPhysics2D(float fixedDeltaTime)
    {
        Physics2DQueries::SetActiveSceneForScriptQueries(this);
        if (!m_Physics2DWorld)
            m_Physics2DWorld = std::make_unique<Physics2DWorld>();
        if (!m_Physics2DWorld->IsInitialized())
            m_Physics2DWorld->Initialize(m_Physics2DSettings);
        m_Physics2DWorld->SetSettings(m_Physics2DSettings);
        m_Physics2DWorld->Step(*this, fixedDeltaTime);
        m_PhysicsWorldInitializedForLoading = true;
    }

    void Scene::SetPhysics2DSettings(const Physics2DWorldSettings& settings)
    {
        m_Physics2DSettings = settings;
        if (m_Physics2DWorld)
            m_Physics2DWorld->SetSettings(m_Physics2DSettings);
    }

    Physics2DWorld* Scene::GetPhysics2DWorld()
    {
        return m_Physics2DWorld.get();
    }

    const Physics2DWorld* Scene::GetPhysics2DWorld() const
    {
        return m_Physics2DWorld.get();
    }

    const Physics2DContactListener* Scene::GetPhysics2DContactEvents() const
    {
        if (!m_Physics2DWorld)
            return nullptr;
        return &m_Physics2DWorld->GetContactListener();
    }

    void Scene::ResetPhysicsRuntimeState()
    {
        if (m_Physics2DWorld)
            m_Physics2DWorld->Shutdown(*this);
    }

    std::unique_ptr<Scene> Scene::Clone() const
    {
        auto clone = std::make_unique<Scene>();
        const auto& sourceRegistry = GetRegistry();
        auto& destinationRegistry = clone->GetRegistry();
        std::unordered_map<entt::entity, entt::entity> entityMap;

        auto view = sourceRegistry.view<TagComponent, TransformComponent>();
        for (entt::entity sourceEntity : view)
        {
            const auto& tag = view.get<TagComponent>(sourceEntity);
            const auto& transform = view.get<TransformComponent>(sourceEntity);

            // CreateEntity ensures default baseline components are initialized first.
            entt::entity destinationEntity = clone->CreateEntity(tag.Tag);
            entityMap.emplace(sourceEntity, destinationEntity);
            if (auto* destinationTag = destinationRegistry.try_get<TagComponent>(destinationEntity))
                destinationTag->Enabled = tag.Enabled;
            destinationRegistry.replace<TransformComponent>(destinationEntity, transform);

            if (const auto* canvas = sourceRegistry.try_get<CanvasComponent>(sourceEntity))
            {
                destinationRegistry.emplace<CanvasComponent>(destinationEntity, *canvas);
            }

            if (const auto* rectTransform = sourceRegistry.try_get<RectTransformComponent>(sourceEntity))
            {
                destinationRegistry.emplace<RectTransformComponent>(destinationEntity, *rectTransform);
            }

            if (const auto* sprite = sourceRegistry.try_get<SpriteComponent>(sourceEntity))
            {
                auto& destinationSprite = destinationRegistry.emplace<SpriteComponent>(destinationEntity);
                destinationSprite.TextureKey = sprite->TextureKey;
                destinationSprite.CachedTexture.reset();
                destinationSprite.TextureLoadAttempted = false;
                destinationSprite.Color = sprite->Color;
                destinationSprite.TilingFactor = sprite->TilingFactor;
                destinationSprite.RenderOrder = sprite->RenderOrder;
                destinationSprite.CastShadows = sprite->CastShadows;
                destinationSprite.ReceiveShadows = sprite->ReceiveShadows;
            }

            if (const auto* animator = sourceRegistry.try_get<AnimatorComponent>(sourceEntity))
            {
                destinationRegistry.emplace<AnimatorComponent>(destinationEntity, *animator);
            }

            if (const auto* animationEventReceiver = sourceRegistry.try_get<AnimationEventReceiverComponent>(sourceEntity))
            {
                auto& destinationReceiver = destinationRegistry.emplace<AnimationEventReceiverComponent>(destinationEntity, *animationEventReceiver);
                destinationReceiver.RuntimeDispatchedEvents.clear();
                destinationReceiver.RuntimeDispatchFrame = 0;
            }

            if (const auto* material = sourceRegistry.try_get<MaterialComponent>(sourceEntity))
            {
                auto& destinationMaterial = destinationRegistry.emplace<MaterialComponent>(destinationEntity);
                destinationMaterial.MaterialKey = material->MaterialKey;
                destinationMaterial.CachedMaterial.reset();
                destinationMaterial.MaterialLoadAttempted = false;
            }

            if (const auto* directionalLight = sourceRegistry.try_get<DirectionalLight2DComponent>(sourceEntity))
            {
                auto& destinationDirectionalLight = destinationRegistry.emplace<DirectionalLight2DComponent>(destinationEntity, *directionalLight);
                destinationDirectionalLight.RuntimeResolvedDirection = glm::vec2(0.0f, -1.0f);
            }

            if (const auto* pointLight = sourceRegistry.try_get<PointLight2DComponent>(sourceEntity))
            {
                auto& destinationPointLight = destinationRegistry.emplace<PointLight2DComponent>(destinationEntity, *pointLight);
                destinationPointLight.RuntimeViewportPosition = glm::vec2(0.0f);
                destinationPointLight.RuntimeViewportRadius = 0.0f;
            }

            if (const auto* shadowOccluder = sourceRegistry.try_get<ShadowOccluder2DComponent>(sourceEntity))
            {
                auto& destinationShadowOccluder = destinationRegistry.emplace<ShadowOccluder2DComponent>(destinationEntity, *shadowOccluder);
                destinationShadowOccluder.RuntimeResolvedPolygonPoints.clear();
                destinationShadowOccluder.RuntimeGeometryRevision = 0;
            }

            if (const auto* uiImage = sourceRegistry.try_get<UIImageComponent>(sourceEntity))
            {
                destinationRegistry.emplace<UIImageComponent>(destinationEntity, *uiImage);
            }

            if (const auto* uiPanel = sourceRegistry.try_get<UIPanelComponent>(sourceEntity))
            {
                destinationRegistry.emplace<UIPanelComponent>(destinationEntity, *uiPanel);
            }

            if (const auto* uiText = sourceRegistry.try_get<UITextComponent>(sourceEntity))
            {
                auto& destinationUIText = destinationRegistry.emplace<UITextComponent>(destinationEntity);
                destinationUIText.Text = uiText->Text;
                destinationUIText.FontFilePath = uiText->FontFilePath;
                destinationUIText.CachedFont.reset();
                destinationUIText.FontLoadAttempted = false;
                destinationUIText.FontSize = uiText->FontSize;
                destinationUIText.Color = uiText->Color;
                destinationUIText.RaycastTarget = uiText->RaycastTarget;
            }

            if (const auto* uiButton = sourceRegistry.try_get<UIButtonComponent>(sourceEntity))
            {
                auto& destinationButton = destinationRegistry.emplace<UIButtonComponent>(destinationEntity, *uiButton);
                destinationButton.IsHovered = false;
                destinationButton.IsPressed = false;
                destinationButton.RuntimeHoverEnteredThisFrame = false;
                destinationButton.RuntimeHoverExitedThisFrame = false;
                destinationButton.RuntimePressedThisFrame = false;
                destinationButton.RuntimeClickedThisFrame = false;
            }

            if (const auto* uiSlider = sourceRegistry.try_get<UISliderComponent>(sourceEntity))
            {
                auto& destinationSlider = destinationRegistry.emplace<UISliderComponent>(destinationEntity, *uiSlider);
                destinationSlider.Value = std::clamp(destinationSlider.Value, destinationSlider.MinValue, destinationSlider.MaxValue);
                destinationSlider.RuntimeDragging = false;
                destinationSlider.RuntimeValueChangedThisFrame = false;
            }

            if (const auto* grid2D = sourceRegistry.try_get<Grid2DComponent>(sourceEntity))
            {
                destinationRegistry.emplace<Grid2DComponent>(destinationEntity, *grid2D);
            }

            if (const auto* tilemapLayer = sourceRegistry.try_get<TilemapLayerComponent>(sourceEntity))
            {
                auto& destinationLayer = destinationRegistry.emplace<TilemapLayerComponent>(destinationEntity, *tilemapLayer);
                destinationLayer.CachedTileRender.clear();
                destinationLayer.RenderCacheDirty = true;
            }

            if (const auto* camera = sourceRegistry.try_get<CameraComponent>(sourceEntity))
            {
                destinationRegistry.emplace<CameraComponent>(destinationEntity, *camera);
            }

            if (const auto* audioListener = sourceRegistry.try_get<AudioListener2DComponent>(sourceEntity))
            {
                destinationRegistry.emplace<AudioListener2DComponent>(destinationEntity, *audioListener);
            }

            if (const auto* rigidbody2D = sourceRegistry.try_get<Rigidbody2DComponent>(sourceEntity))
            {
                auto& destinationRigidbody2D = destinationRegistry.emplace<Rigidbody2DComponent>(destinationEntity, *rigidbody2D);
                destinationRigidbody2D.RuntimeBodyId = kNullPhysics2DBody;
                destinationRigidbody2D.RuntimeBodyCreated = false;
                destinationRigidbody2D.RuntimePreviousPosition = glm::vec2(0.0f);
                destinationRigidbody2D.RuntimePreviousAngleRadians = 0.0f;
                destinationRigidbody2D.RuntimeRenderPreviousPosition = glm::vec2(0.0f);
                destinationRigidbody2D.RuntimeRenderPreviousAngleRadians = 0.0f;
                destinationRigidbody2D.RuntimeRenderCurrentPosition = glm::vec2(0.0f);
                destinationRigidbody2D.RuntimeRenderCurrentAngleRadians = 0.0f;
                destinationRigidbody2D.RuntimeLinearVelocity = glm::vec2(0.0f);
                destinationRigidbody2D.RuntimePendingLinearVelocity = glm::vec2(0.0f);
                destinationRigidbody2D.RuntimeHasPendingLinearVelocity = false;
                destinationRigidbody2D.RuntimePendingLinearVelocityX = 0.0f;
                destinationRigidbody2D.RuntimeHasPendingLinearVelocityX = false;
                destinationRigidbody2D.RuntimePendingLinearVelocityY = 0.0f;
                destinationRigidbody2D.RuntimeHasPendingLinearVelocityY = false;
                destinationRigidbody2D.RuntimeContactCount = 0;
                destinationRigidbody2D.RuntimeContactCountExcludingSensors = 0;
            }

            if (const auto* boxCollider2D = sourceRegistry.try_get<BoxCollider2DComponent>(sourceEntity))
            {
                auto& destinationBoxCollider2D = destinationRegistry.emplace<BoxCollider2DComponent>(destinationEntity, *boxCollider2D);
                destinationBoxCollider2D.RuntimeShapeId = kNullPhysics2DShape;
                destinationBoxCollider2D.RuntimeShapeCreated = false;
            }

            if (const auto* circleCollider2D = sourceRegistry.try_get<CircleCollider2DComponent>(sourceEntity))
            {
                auto& destinationCircleCollider2D = destinationRegistry.emplace<CircleCollider2DComponent>(destinationEntity, *circleCollider2D);
                destinationCircleCollider2D.RuntimeShapeId = kNullPhysics2DShape;
                destinationCircleCollider2D.RuntimeShapeCreated = false;
            }

            if (const auto* joint2D = sourceRegistry.try_get<Joint2DComponent>(sourceEntity))
            {
                auto& destinationJoint2D = destinationRegistry.emplace<Joint2DComponent>(destinationEntity, *joint2D);
                destinationJoint2D.RuntimeJointId = kNullPhysics2DJoint;
                destinationJoint2D.RuntimeJointCreated = false;
            }

            if (const auto* audioSource = sourceRegistry.try_get<AudioSourceComponent>(sourceEntity))
            {
                auto& destinationAudioSource = destinationRegistry.emplace<AudioSourceComponent>(destinationEntity);
                destinationAudioSource.AudioClipKey = audioSource->AudioClipKey;
                destinationAudioSource.Volume = audioSource->Volume;
                destinationAudioSource.Pitch = audioSource->Pitch;
                destinationAudioSource.PlayOnStart = audioSource->PlayOnStart;
                destinationAudioSource.Loop = audioSource->Loop;
                destinationAudioSource.Muted = audioSource->Muted;
                destinationAudioSource.Space = audioSource->Space;
                destinationAudioSource.MixerGroup = audioSource->MixerGroup;
                destinationAudioSource.SpatialMinDistance = audioSource->SpatialMinDistance;
                destinationAudioSource.SpatialMaxDistance = audioSource->SpatialMaxDistance;
                destinationAudioSource.SpatialRolloffExponent = audioSource->SpatialRolloffExponent;
                destinationAudioSource.StereoPanStrength = audioSource->StereoPanStrength;
                destinationAudioSource.AttenuationCurveKey = audioSource->AttenuationCurveKey;
                destinationAudioSource.RuntimeVoiceId = 0;
                destinationAudioSource.RuntimePlaybackStarted = false;
            }

            if (const auto* nativeScript = sourceRegistry.try_get<NativeScriptComponent>(sourceEntity))
            {
                auto& destinationNativeScript = destinationRegistry.emplace<NativeScriptComponent>(destinationEntity);
                destinationNativeScript.Scripts.reserve(nativeScript->Scripts.size());
                for (const auto& sourceScriptEntry : nativeScript->Scripts)
                {
                    auto& destinationScriptEntry = destinationNativeScript.Scripts.emplace_back();
                    destinationScriptEntry.ScriptClassName = sourceScriptEntry.ScriptClassName;
                    destinationScriptEntry.ScriptAssetRelativePath = sourceScriptEntry.ScriptAssetRelativePath;
                    destinationScriptEntry.Enabled = sourceScriptEntry.Enabled;
                    destinationScriptEntry.ExposedProperties = sourceScriptEntry.ExposedProperties;
                    destinationScriptEntry.RuntimeInitialized = false;
                    destinationScriptEntry.RuntimeInstance.reset();
                    destinationScriptEntry.RuntimeWarnedOnUpdateTransformMutation = false;
                }
            }

            if (const auto* particleEmitter = sourceRegistry.try_get<ParticleEmitterComponent>(sourceEntity))
            {
                // Copy constructor copies authoring fields and resets runtime state
                destinationRegistry.emplace<ParticleEmitterComponent>(destinationEntity, *particleEmitter);
            }

            if (const auto* prefabInstance = sourceRegistry.try_get<PrefabInstanceComponent>(sourceEntity))
            {
                destinationRegistry.emplace<PrefabInstanceComponent>(destinationEntity, *prefabInstance);
            }

            ResetRuntimeStateForEntity(destinationRegistry, destinationEntity);
        }

        for (const auto& [sourceEntity, destinationEntity] : entityMap)
        {
            const auto* sourceHierarchy = sourceRegistry.try_get<HierarchyComponent>(sourceEntity);
            if (!sourceHierarchy)
                continue;

            auto* destinationHierarchy = destinationRegistry.try_get<HierarchyComponent>(destinationEntity);
            if (!destinationHierarchy)
                destinationHierarchy = &destinationRegistry.emplace<HierarchyComponent>(destinationEntity);

            // Preserve exact local transform values from edit scene.
            // Using SetParent() would preserve world transform and rewrite local transform,
            // which causes children to shift when entering Play Mode.
            destinationHierarchy->Parent = entt::null;
            if (sourceHierarchy->Parent != entt::null)
            {
                auto foundParent = entityMap.find(sourceHierarchy->Parent);
                if (foundParent != entityMap.end())
                    destinationHierarchy->Parent = foundParent->second;
            }
            destinationHierarchy->SiblingOrder = sourceHierarchy->SiblingOrder;
        }

        for (const auto& [sourceEntity, destinationEntity] : entityMap)
        {
            const auto* sourceJoint = sourceRegistry.try_get<Joint2DComponent>(sourceEntity);
            auto* destinationJoint = destinationRegistry.try_get<Joint2DComponent>(destinationEntity);
            if (!sourceJoint || !destinationJoint)
                continue;
            if (sourceJoint->ConnectedEntity == entt::null)
            {
                destinationJoint->ConnectedEntity = entt::null;
                continue;
            }
            const auto mappedConnectedEntity = entityMap.find(sourceJoint->ConnectedEntity);
            destinationJoint->ConnectedEntity = (mappedConnectedEntity != entityMap.end())
                ? mappedConnectedEntity->second
                : entt::null;
        }

        clone->m_EditorCameraBookmark = m_EditorCameraBookmark;
        clone->m_Physics2DSettings = m_Physics2DSettings;
        return clone;
    }

    Result<void> Scene::SaveToFile(const std::filesystem::path& path) const
    {
        std::error_code errorCode;
        const std::filesystem::path parentDirectory = path.parent_path();
        if (!parentDirectory.empty() && !std::filesystem::exists(parentDirectory, errorCode))
            std::filesystem::create_directories(parentDirectory, errorCode);
        if (errorCode)
            return Result<void>(ErrorCode::FileAccessDenied, "Scene::SaveToFile failed creating parent directories");

        auto view = m_Registry.view<TagComponent, TransformComponent>();
        std::vector<entt::entity> entities;
        entities.reserve(view.size_hint());
        for (entt::entity entity : view)
            entities.push_back(entity);
        std::sort(entities.begin(), entities.end(), [](entt::entity left, entt::entity right) {
            return static_cast<uint32_t>(left) < static_cast<uint32_t>(right);
        });

        std::unordered_map<entt::entity, int32_t> indexByEntity;
        indexByEntity.reserve(entities.size());
        for (size_t index = 0; index < entities.size(); ++index)
            indexByEntity.emplace(entities[index], static_cast<int32_t>(index));

        nlohmann::json root = nlohmann::json::object();
        root["Version"] = kSceneSerializationVersion;
        if (m_EditorCameraBookmark.has_value())
        {
            root["EditorCamera"] = {
                { "Position", { m_EditorCameraBookmark->Position.x, m_EditorCameraBookmark->Position.y, m_EditorCameraBookmark->Position.z } },
                { "YawDegrees", m_EditorCameraBookmark->YawDegrees },
                { "PitchDegrees", m_EditorCameraBookmark->PitchDegrees }
            };
        }
        root["Physics2DSettings"] = {
            { "Gravity", { m_Physics2DSettings.Gravity.x, m_Physics2DSettings.Gravity.y } },
            { "VelocitySubSteps", m_Physics2DSettings.VelocitySubSteps },
            { "EnableSleep", m_Physics2DSettings.EnableSleep },
            { "EnableContinuousCollision", m_Physics2DSettings.EnableContinuousCollision },
            { "HighContactQualityMode", m_Physics2DSettings.HighContactQualityMode },
            { "HighContactQualityExtraSubSteps", m_Physics2DSettings.HighContactQualityExtraSubSteps },
            { "ContactHertz", m_Physics2DSettings.ContactHertz },
            { "ContactDampingRatio", m_Physics2DSettings.ContactDampingRatio },
            { "ContactPushSpeed", m_Physics2DSettings.ContactPushSpeed }
        };
        root["Entities"] = nlohmann::json::array();

        for (entt::entity entity : entities)
        {
            const auto& tag = view.get<TagComponent>(entity);
            const auto& transform = view.get<TransformComponent>(entity);
            nlohmann::json entry = nlohmann::json::object();
            entry["Tag"] = tag.Tag;
            entry["EntityEnabled"] = tag.Enabled;
            entry["Transform"] = {
                { "Position", { transform.Position.x, transform.Position.y, transform.Position.z } },
                { "Rotation", { transform.Rotation.x, transform.Rotation.y, transform.Rotation.z } },
                { "Scale", { transform.Scale.x, transform.Scale.y, transform.Scale.z } }
            };

            const entt::entity parent = GetParent(entity);
            int32_t parentIndex = -1;
            if (parent != entt::null)
            {
                const auto it = indexByEntity.find(parent);
                if (it != indexByEntity.end())
                    parentIndex = it->second;
            }

            const auto* hierarchy = m_Registry.try_get<HierarchyComponent>(entity);
            entry["Hierarchy"] = {
                { "ParentIndex", parentIndex },
                { "SiblingOrder", hierarchy ? hierarchy->SiblingOrder : 0 }
            };

            if (const auto* canvas = m_Registry.try_get<CanvasComponent>(entity))
            {
                entry["Canvas"] = {
                    { "RenderMode", canvas->Mode == CanvasComponent::RenderMode::WorldSpace ? "WorldSpace" : "ScreenSpace" },
                    { "SortOrder", canvas->SortOrder },
                    { "ReferenceResolution", { canvas->ReferenceResolution.x, canvas->ReferenceResolution.y } }
                };
            }

            if (const auto* rectTransform = m_Registry.try_get<RectTransformComponent>(entity))
            {
                entry["RectTransform"] = {
                    { "AnchorMin", { rectTransform->AnchorMin.x, rectTransform->AnchorMin.y } },
                    { "AnchorMax", { rectTransform->AnchorMax.x, rectTransform->AnchorMax.y } },
                    { "Pivot", { rectTransform->Pivot.x, rectTransform->Pivot.y } },
                    { "SizeDelta", { rectTransform->SizeDelta.x, rectTransform->SizeDelta.y } },
                    { "AnchoredPosition", { rectTransform->AnchoredPosition.x, rectTransform->AnchoredPosition.y } }
                };
            }

            if (const auto* sprite = m_Registry.try_get<SpriteComponent>(entity))
            {
                nlohmann::json spriteJson = {
                    { "Texture", MakeAssetReferenceJson(sprite->TextureKey, Assets::AssetType::Texture2D) },
                    { "Color", { sprite->Color.r, sprite->Color.g, sprite->Color.b, sprite->Color.a } },
                    { "TilingFactor", { sprite->TilingFactor.x, sprite->TilingFactor.y } },
                    { "RenderOrder", sprite->RenderOrder },
                    { "CastShadows", sprite->CastShadows },
                    { "ReceiveShadows", sprite->ReceiveShadows }
                };

                if (sprite->SubSpriteIndex >= 0)
                {
                    spriteJson["SubSpriteIndex"] = sprite->SubSpriteIndex;
                    spriteJson["UvMin"] = { sprite->UvMin.x, sprite->UvMin.y };
                    spriteJson["UvMax"] = { sprite->UvMax.x, sprite->UvMax.y };
                }

                entry["Sprite"] = std::move(spriteJson);
            }

            if (const auto* animator = m_Registry.try_get<AnimatorComponent>(entity))
            {
                nlohmann::json boolParameters = nlohmann::json::object();
                for (const auto& [name, value] : animator->BoolParameters)
                    boolParameters[name] = value;

                nlohmann::json floatParameters = nlohmann::json::object();
                for (const auto& [name, value] : animator->FloatParameters)
                    floatParameters[name] = value;

                nlohmann::json integerParameters = nlohmann::json::object();
                for (const auto& [name, value] : animator->IntegerParameters)
                    integerParameters[name] = value;

                nlohmann::json triggerParameters = nlohmann::json::object();
                for (const auto& [name, value] : animator->TriggerParameters)
                    triggerParameters[name] = value;

                entry["Animator"] = {
                    { "Controller", MakeAssetReferenceJson(animator->ControllerKey, Assets::AssetType::AnimatorController) },
                    { "DefaultClip", MakeAssetReferenceJson(animator->DefaultClipKey, Assets::AssetType::AnimationClip) },
                    { "PlaybackSpeed", animator->PlaybackSpeed },
                    { "Enabled", animator->Enabled },
                    { "ApplyToSprite", animator->ApplyToSprite },
                    { "ApplyToTransform", animator->ApplyToTransform },
                    { "AutoPlay", animator->AutoPlay },
                    { "BoolParameters", std::move(boolParameters) },
                    { "FloatParameters", std::move(floatParameters) },
                    { "IntegerParameters", std::move(integerParameters) },
                    { "TriggerParameters", std::move(triggerParameters) }
                };
            }

            if (const auto* animationEventReceiver = m_Registry.try_get<AnimationEventReceiverComponent>(entity))
            {
                entry["AnimationEventReceiver"] = {
                    { "Enabled", animationEventReceiver->Enabled }
                };
            }

            if (const auto* material = m_Registry.try_get<MaterialComponent>(entity))
            {
                entry["Material"] = MakeAssetReferenceJson(material->MaterialKey, Assets::AssetType::Material);
            }

            if (const auto* directionalLight = m_Registry.try_get<DirectionalLight2DComponent>(entity))
            {
                entry["DirectionalLight2D"] = {
                    { "Enabled", directionalLight->Enabled },
                    { "Color", { directionalLight->Color.r, directionalLight->Color.g, directionalLight->Color.b } },
                    { "Intensity", directionalLight->Intensity },
                    { "UseEntityRotation", directionalLight->UseEntityRotation },
                    { "Direction", { directionalLight->Direction.x, directionalLight->Direction.y } },
                    { "CastShadows", directionalLight->CastShadows },
                    { "ShadowStrength", directionalLight->ShadowStrength },
                    { "ShadowSoftness", directionalLight->ShadowSoftness },
                    { "ShadowSamples", directionalLight->ShadowSamples },
                    { "ShadowDistance", directionalLight->ShadowDistance },
                    { "ShadowBias", directionalLight->ShadowBias }
                };
            }

            if (const auto* pointLight = m_Registry.try_get<PointLight2DComponent>(entity))
            {
                entry["PointLight2D"] = {
                    { "Enabled", pointLight->Enabled },
                    { "Color", { pointLight->Color.r, pointLight->Color.g, pointLight->Color.b } },
                    { "Intensity", pointLight->Intensity },
                    { "Radius", pointLight->Radius },
                    { "Falloff", pointLight->Falloff },
                    { "CastShadows", pointLight->CastShadows },
                    { "ShadowStrength", pointLight->ShadowStrength },
                    { "ShadowSoftness", pointLight->ShadowSoftness },
                    { "ShadowSamples", pointLight->ShadowSamples },
                    { "ShadowBias", pointLight->ShadowBias }
                };
            }

            if (const auto* shadowOccluder = m_Registry.try_get<ShadowOccluder2DComponent>(entity))
            {
                nlohmann::json polygonPoints = nlohmann::json::array();
                for (const glm::vec2& point : shadowOccluder->PolygonPoints)
                    polygonPoints.push_back({ point.x, point.y });

                const char* sourceModeName = (shadowOccluder->Source == ShadowOccluder2DComponent::SourceMode::PhysicsCollider)
                    ? "PhysicsCollider"
                    : "ManualPolygon";

                entry["ShadowOccluder2D"] = {
                    { "Enabled", shadowOccluder->Enabled },
                    { "SourceMode", sourceModeName },
                    { "Closed", shadowOccluder->Closed },
                    { "PolygonPoints", std::move(polygonPoints) },
                    { "Extrusion", shadowOccluder->Extrusion }
                };
            }

            if (const auto* uiImage = m_Registry.try_get<UIImageComponent>(entity))
            {
                entry["UIImage"] = {
                    { "RaycastTarget", uiImage->RaycastTarget }
                };
            }

            if (const auto* uiPanel = m_Registry.try_get<UIPanelComponent>(entity))
            {
                entry["UIPanel"] = {
                    { "BackgroundColor", { uiPanel->BackgroundColor.r, uiPanel->BackgroundColor.g, uiPanel->BackgroundColor.b, uiPanel->BackgroundColor.a } },
                    { "UseSpriteTexture", uiPanel->UseSpriteTexture },
                    { "RaycastTarget", uiPanel->RaycastTarget }
                };
            }

            if (const auto* uiText = m_Registry.try_get<UITextComponent>(entity))
            {
                entry["UIText"] = {
                    { "Value", uiText->Text },
                    { "FontFilePath", uiText->FontFilePath },
                    { "FontSize", uiText->FontSize },
                    { "Color", { uiText->Color.r, uiText->Color.g, uiText->Color.b, uiText->Color.a } },
                    { "RaycastTarget", uiText->RaycastTarget }
                };
            }

            if (const auto* uiButton = m_Registry.try_get<UIButtonComponent>(entity))
            {
                entry["UIButton"] = {
                    { "Interactable", uiButton->Interactable },
                    { "UseStateColors", uiButton->UseStateColors },
                    { "NormalColor", { uiButton->NormalColor.r, uiButton->NormalColor.g, uiButton->NormalColor.b, uiButton->NormalColor.a } },
                    { "HoveredColor", { uiButton->HoveredColor.r, uiButton->HoveredColor.g, uiButton->HoveredColor.b, uiButton->HoveredColor.a } },
                    { "PressedColor", { uiButton->PressedColor.r, uiButton->PressedColor.g, uiButton->PressedColor.b, uiButton->PressedColor.a } },
                    { "DisabledColor", { uiButton->DisabledColor.r, uiButton->DisabledColor.g, uiButton->DisabledColor.b, uiButton->DisabledColor.a } },
                    { "OnClickEvent", uiButton->OnClickEvent },
                    { "OnHoverEnterEvent", uiButton->OnHoverEnterEvent },
                    { "OnHoverExitEvent", uiButton->OnHoverExitEvent },
                    { "OnPressedEvent", uiButton->OnPressedEvent }
                };
            }

            if (const auto* uiSlider = m_Registry.try_get<UISliderComponent>(entity))
            {
                entry["UISlider"] = {
                    { "Interactable", uiSlider->Interactable },
                    { "MinValue", uiSlider->MinValue },
                    { "MaxValue", uiSlider->MaxValue },
                    { "Value", uiSlider->Value },
                    { "BackgroundColor", { uiSlider->BackgroundColor.r, uiSlider->BackgroundColor.g, uiSlider->BackgroundColor.b, uiSlider->BackgroundColor.a } },
                    { "FillColor", { uiSlider->FillColor.r, uiSlider->FillColor.g, uiSlider->FillColor.b, uiSlider->FillColor.a } },
                    { "HandleColor", { uiSlider->HandleColor.r, uiSlider->HandleColor.g, uiSlider->HandleColor.b, uiSlider->HandleColor.a } },
                    { "HandleWidth", uiSlider->HandleWidth },
                    { "HandleHeightMultiplier", uiSlider->HandleHeightMultiplier },
                    { "ShowHandle", uiSlider->ShowHandle },
                    { "OnValueChangedEvent", uiSlider->OnValueChangedEvent }
                };
            }

            if (const auto* grid2D = m_Registry.try_get<Grid2DComponent>(entity))
            {
                entry["Grid2D"] = {
                    { "CellSize", { grid2D->CellSize.x, grid2D->CellSize.y } },
                    { "CellGap",  { grid2D->CellGap.x,  grid2D->CellGap.y  } }
                };
            }

            if (const auto* tilemapLayer = m_Registry.try_get<TilemapLayerComponent>(entity))
            {
                nlohmann::json tileTableJson = nlohmann::json::array();
                for (const auto& key : tilemapLayer->TileTable)
                    tileTableJson.push_back(MakeAssetReferenceJson(key, Assets::AssetType::Tile));

                // Only serialize the Tiles array if it contains any non-zero entries.
                // An empty or all-zero grid would bloat the scene file with thousands of zeros.
                bool hasPaintedTiles = false;
                for (const uint32_t t : tilemapLayer->Tiles)
                {
                    if (t != 0u) { hasPaintedTiles = true; break; }
                }

                nlohmann::json tilemapLayerJson = {
                    { "GridSize",         { tilemapLayer->GridSize.x, tilemapLayer->GridSize.y } },
                    { "RenderOrder",      tilemapLayer->RenderOrder },
                    { "CollisionEnabled", tilemapLayer->CollisionEnabled },
                    { "CastShadows",      tilemapLayer->CastShadows },
                    { "TileTable",        std::move(tileTableJson) }
                };
                if (hasPaintedTiles)
                    tilemapLayerJson["Tiles"] = tilemapLayer->Tiles;

                entry["TilemapLayer"] = std::move(tilemapLayerJson);
            }

            if (const auto* camera = m_Registry.try_get<CameraComponent>(entity))
            {
                const char* projectionName = (camera->Projection == CameraComponent::ProjectionType::Perspective3D)
                    ? "Perspective3D"
                    : "Orthographic2D";

                entry["Camera"] = {
                    { "Projection", projectionName },
                    { "IsPrimary", camera->IsPrimary },
                    { "Zoom", camera->Zoom },
                    { "NearPlane", camera->NearPlane },
                    { "FarPlane", camera->FarPlane },
                    { "FieldOfViewYDegrees", camera->FieldOfViewYDegrees }
                };
            }

            if (const auto* audioListener = m_Registry.try_get<AudioListener2DComponent>(entity))
            {
                entry["AudioListener2D"] = {
                    { "Enabled", audioListener->Enabled },
                    { "UsePrimaryCameraPosition", audioListener->UsePrimaryCameraPosition }
                };
            }

            if (const auto* audioSource = m_Registry.try_get<AudioSourceComponent>(entity))
            {
                entry["AudioSource"] = {
                    { "AudioClip", MakeAssetReferenceJson(audioSource->AudioClipKey, Assets::AssetType::AudioClip) },
                    { "Volume", audioSource->Volume },
                    { "Pitch", audioSource->Pitch },
                    { "PlayOnStart", audioSource->PlayOnStart },
                    { "Loop", audioSource->Loop },
                    { "Muted", audioSource->Muted },
                    { "PlaybackSpace", ToAudioPlaybackSpaceName(audioSource->Space) },
                    { "MixerGroup", audioSource->MixerGroup },
                    { "SpatialMinDistance", audioSource->SpatialMinDistance },
                    { "SpatialMaxDistance", audioSource->SpatialMaxDistance },
                    { "SpatialRolloffExponent", audioSource->SpatialRolloffExponent },
                    { "StereoPanStrength", audioSource->StereoPanStrength },
                    { "AttenuationCurveKey", audioSource->AttenuationCurveKey }
                };
            }

            if (const auto* rigidbody2D = m_Registry.try_get<Rigidbody2DComponent>(entity))
            {
                auto toBodyTypeString = [](Rigidbody2DComponent::BodyType type) -> const char*
                {
                    switch (type)
                    {
                        case Rigidbody2DComponent::BodyType::Static: return "Static";
                        case Rigidbody2DComponent::BodyType::Kinematic: return "Kinematic";
                        case Rigidbody2DComponent::BodyType::Dynamic:
                        default: return "Dynamic";
                    }
                };
                entry["Rigidbody2D"] = {
                    { "BodyType", toBodyTypeString(rigidbody2D->Type) },
                    { "FreezePositionX", rigidbody2D->FreezePositionX },
                    { "FreezePositionY", rigidbody2D->FreezePositionY },
                    { "FreezeRotation", rigidbody2D->IsRotationLocked() },
                    { "FixedRotation", rigidbody2D->IsRotationLocked() },
                    { "UseCCD", rigidbody2D->UseCCD },
                    { "EnableSleep", rigidbody2D->EnableSleep },
                    { "StartAwake", rigidbody2D->StartAwake },
                    { "Interpolate", rigidbody2D->Interpolate },
                    { "HighContactQuality", rigidbody2D->HighContactQuality },
                    { "ExtraSolverSubSteps", rigidbody2D->ExtraSolverSubSteps },
                    { "GravityScale", rigidbody2D->GravityScale },
                    { "LinearDamping", rigidbody2D->LinearDamping },
                    { "AngularDamping", rigidbody2D->AngularDamping }
                };
            }

            if (const auto* boxCollider2D = m_Registry.try_get<BoxCollider2DComponent>(entity))
            {
                entry["BoxCollider2D"] = {
                    { "Offset", { boxCollider2D->Offset.x, boxCollider2D->Offset.y } },
                    { "Size", { boxCollider2D->Size.x, boxCollider2D->Size.y } },
                    { "Density", boxCollider2D->Density },
                    { "Friction", boxCollider2D->Friction },
                    { "Restitution", boxCollider2D->Restitution },
                    { "IsSensor", boxCollider2D->IsSensor },
                    { "CollisionLayer", boxCollider2D->CollisionLayer },
                    { "CollisionMask", boxCollider2D->CollisionMask }
                };
            }

            if (const auto* circleCollider2D = m_Registry.try_get<CircleCollider2DComponent>(entity))
            {
                entry["CircleCollider2D"] = {
                    { "Offset", { circleCollider2D->Offset.x, circleCollider2D->Offset.y } },
                    { "Radius", circleCollider2D->Radius },
                    { "Density", circleCollider2D->Density },
                    { "Friction", circleCollider2D->Friction },
                    { "Restitution", circleCollider2D->Restitution },
                    { "IsSensor", circleCollider2D->IsSensor },
                    { "CollisionLayer", circleCollider2D->CollisionLayer },
                    { "CollisionMask", circleCollider2D->CollisionMask }
                };
            }

            if (const auto* joint2D = m_Registry.try_get<Joint2DComponent>(entity))
            {
                auto toJointTypeString = [](Joint2DComponent::JointType type) -> const char*
                {
                    switch (type)
                    {
                        case Joint2DComponent::JointType::Revolute: return "Revolute";
                        case Joint2DComponent::JointType::Prismatic: return "Prismatic";
                        case Joint2DComponent::JointType::Distance:
                        default: return "Distance";
                    }
                };

                int32_t connectedEntityIndex = -1;
                if (joint2D->ConnectedEntity != entt::null)
                {
                    const auto connectedEntityIt = indexByEntity.find(joint2D->ConnectedEntity);
                    if (connectedEntityIt != indexByEntity.end())
                        connectedEntityIndex = connectedEntityIt->second;
                }

                entry["Joint2D"] = {
                    { "Type", toJointTypeString(joint2D->Type) },
                    { "ConnectedEntityIndex", connectedEntityIndex },
                    { "CollideConnected", joint2D->CollideConnected },
                    { "AnchorA", { joint2D->AnchorA.x, joint2D->AnchorA.y } },
                    { "AnchorB", { joint2D->AnchorB.x, joint2D->AnchorB.y } },
                    { "Axis", { joint2D->Axis.x, joint2D->Axis.y } },
                    { "EnableLimit", joint2D->EnableLimit },
                    { "Limits", { joint2D->Limits.x, joint2D->Limits.y } },
                    { "EnableMotor", joint2D->EnableMotor },
                    { "MotorSpeed", joint2D->MotorSpeed },
                    { "MaxMotorForceOrTorque", joint2D->MaxMotorForceOrTorque },
                    { "EnableSpring", joint2D->EnableSpring },
                    { "Hertz", joint2D->Hertz },
                    { "DampingRatio", joint2D->DampingRatio }
                };
            }

            if (const auto* nativeScript = m_Registry.try_get<NativeScriptComponent>(entity))
            {
                nlohmann::json scriptEntries = nlohmann::json::array();
                for (const auto& scriptEntry : nativeScript->Scripts)
                {
                    nlohmann::json exposedProperties = nlohmann::json::object();
                    for (const auto& [propertyName, propertyValue] : scriptEntry.ExposedProperties)
                    {
                        exposedProperties[propertyName] = SerializeScriptPropertyValue(propertyValue);
                    }
                    scriptEntries.push_back({
                        { "Class", scriptEntry.ScriptClassName },
                        { "AssetPath", scriptEntry.ScriptAssetRelativePath },
                        { "ExposedProperties", std::move(exposedProperties) },
                        { "Enabled", scriptEntry.Enabled }
                    });
                }
                entry["NativeScripts"] = std::move(scriptEntries);
            }

            if (const auto* particleEmitter = m_Registry.try_get<ParticleEmitterComponent>(entity))
            {
                entry["ParticleEmitter"] = {
                    { "SpawnRate", particleEmitter->SpawnRate },
                    { "LifetimeMin", particleEmitter->LifetimeMin },
                    { "LifetimeMax", particleEmitter->LifetimeMax },
                    { "Looping", particleEmitter->Looping },
                    { "Duration", particleEmitter->Duration },
                    { "PlayOnStart", particleEmitter->PlayOnStart },
                    { "BurstEnabled", particleEmitter->BurstEnabled },
                    { "BurstCount", particleEmitter->BurstCount },
                    { "SpawnOffsetMin", { particleEmitter->SpawnOffsetMin.x, particleEmitter->SpawnOffsetMin.y } },
                    { "SpawnOffsetMax", { particleEmitter->SpawnOffsetMax.x, particleEmitter->SpawnOffsetMax.y } },
                    { "UseRadialSpawn", particleEmitter->UseRadialSpawn },
                    { "SpawnRadiusMin", particleEmitter->SpawnRadiusMin },
                    { "SpawnRadiusMax", particleEmitter->SpawnRadiusMax },
                    { "SpeedMin", particleEmitter->SpeedMin },
                    { "SpeedMax", particleEmitter->SpeedMax },
                    { "AngleMin", particleEmitter->AngleMin },
                    { "AngleMax", particleEmitter->AngleMax },
                    { "RadialVelocity", particleEmitter->RadialVelocity },
                    { "GravityModifier", particleEmitter->GravityModifier },
                    { "StartSizeMin", particleEmitter->StartSizeMin },
                    { "StartSizeMax", particleEmitter->StartSizeMax },
                    { "EndSize", particleEmitter->EndSize },
                    { "StartColor", { particleEmitter->StartColor.r, particleEmitter->StartColor.g, particleEmitter->StartColor.b, particleEmitter->StartColor.a } },
                    { "EndColor", { particleEmitter->EndColor.r, particleEmitter->EndColor.g, particleEmitter->EndColor.b, particleEmitter->EndColor.a } },
                    { "StartRotationMin", particleEmitter->StartRotationMin },
                    { "StartRotationMax", particleEmitter->StartRotationMax },
                    { "RotationSpeedMin", particleEmitter->RotationSpeedMin },
                    { "RotationSpeedMax", particleEmitter->RotationSpeedMax },
                    { "Texture", MakeAssetReferenceJson(particleEmitter->TextureKey, Assets::AssetType::Texture2D) },
                    { "MaxParticles", particleEmitter->MaxParticles }
                };
            }

            if (const auto* prefabInstance = m_Registry.try_get<PrefabInstanceComponent>(entity))
            {
                entry["PrefabInstance"] = {
                    { "Prefab", MakeAssetReferenceJson(prefabInstance->PrefabAssetKey, Assets::AssetType::Prefab) }
                };
            }

            root["Entities"].push_back(std::move(entry));
        }

        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output.is_open())
            return Result<void>(ErrorCode::FileAccessDenied, "Scene::SaveToFile failed opening destination file");
        output << root.dump(2);
        return Result<void>();
    }

    Result<std::unique_ptr<Scene>> Scene::LoadFromFile(const std::filesystem::path& path)
    {
        nlohmann::json root;
        try
        {
            bool loadedFromBundle = false;
            std::string sceneText;

            auto& bundle = Assets::AssetBundle::GetInstance();
            std::vector<std::string> bundleKeys;
            bundleKeys.push_back(path.generic_string());
            if (!bundleKeys.back().empty())
            {
                const std::string marker = "/Assets/";
                const size_t markerPosition = bundleKeys.back().find(marker);
                if (markerPosition != std::string::npos)
                {
                    bundleKeys.push_back(bundleKeys.back().substr(markerPosition + 1));
                }
            }

            if (bundle.IsEnabled() && bundle.IsLoaded())
            {
                for (const std::string& bundleKey : bundleKeys)
                {
                    if (bundleKey.empty())
                        continue;
                    const auto bundleTextResult = bundle.ReadAllTextByKey(bundleKey);
                    if (bundleTextResult.IsSuccess())
                    {
                        sceneText = bundleTextResult.GetValue();
                        loadedFromBundle = true;
                        break;
                    }
                }
            }

            if (!loadedFromBundle)
            {
                std::ifstream input(path, std::ios::binary);
                if (!input.is_open())
                    return Result<std::unique_ptr<Scene>>(ErrorCode::FileNotFound, "Scene::LoadFromFile failed opening scene file");

                input >> root;
            }
            else
            {
                root = nlohmann::json::parse(sceneText);
            }
        }
        catch (const std::exception& exception)
        {
            return Result<std::unique_ptr<Scene>>(ErrorCode::FileCorrupted, std::string("Scene::LoadFromFile JSON parse failed: ") + exception.what());
        }

        if (!root.is_object() || !root.contains("Entities") || !root["Entities"].is_array())
            return Result<std::unique_ptr<Scene>>(ErrorCode::FileCorrupted, "Scene::LoadFromFile invalid scene JSON format");

        auto scene = std::make_unique<Scene>();
        if (root.contains("Physics2DSettings") && root["Physics2DSettings"].is_object())
        {
            const auto& physicsSettingsJson = root["Physics2DSettings"];
            auto gravity = physicsSettingsJson.value("Gravity", std::vector<float>{ 0.0f, -9.81f });
            if (gravity.size() >= 2)
                scene->m_Physics2DSettings.Gravity = glm::vec2(gravity[0], gravity[1]);
            scene->m_Physics2DSettings.VelocitySubSteps = std::max(1, physicsSettingsJson.value("VelocitySubSteps", 4));
            scene->m_Physics2DSettings.EnableSleep = physicsSettingsJson.value("EnableSleep", true);
            scene->m_Physics2DSettings.EnableContinuousCollision = physicsSettingsJson.value("EnableContinuousCollision", true);
            scene->m_Physics2DSettings.HighContactQualityMode = physicsSettingsJson.value("HighContactQualityMode", false);
            scene->m_Physics2DSettings.HighContactQualityExtraSubSteps = std::max(0, physicsSettingsJson.value("HighContactQualityExtraSubSteps", 4));
            scene->m_Physics2DSettings.ContactHertz = physicsSettingsJson.value("ContactHertz", 90.0f);
            scene->m_Physics2DSettings.ContactDampingRatio = physicsSettingsJson.value("ContactDampingRatio", 1.0f);
            scene->m_Physics2DSettings.ContactPushSpeed = physicsSettingsJson.value("ContactPushSpeed", 8.0f);
        }
        if (root.contains("EditorCamera") && root["EditorCamera"].is_object())
        {
            const auto& editorCameraJson = root["EditorCamera"];
            auto position = editorCameraJson.value("Position", std::vector<float>{ 0.0f, 0.0f, 0.0f });
            Scene::EditorCameraBookmark bookmark{};
            if (position.size() >= 3)
                bookmark.Position = glm::vec3(position[0], position[1], position[2]);
            bookmark.YawDegrees = editorCameraJson.value("YawDegrees", -90.0f);
            bookmark.PitchDegrees = editorCameraJson.value("PitchDegrees", 0.0f);
            scene->SetEditorCameraBookmark(bookmark);
        }

        std::vector<entt::entity> createdEntities;
        std::vector<int32_t> parentIndices;
        std::vector<int32_t> siblingOrders;
        std::vector<int32_t> jointConnectedEntityIndices;
        createdEntities.reserve(root["Entities"].size());
        parentIndices.reserve(root["Entities"].size());
        siblingOrders.reserve(root["Entities"].size());
        jointConnectedEntityIndices.reserve(root["Entities"].size());

        for (const auto& entry : root["Entities"])
        {
            const std::string tag = entry.value("Tag", "Entity");
            const entt::entity entity = scene->CreateEntity(tag);
            if (auto* tagComponent = scene->GetRegistry().try_get<TagComponent>(entity))
                tagComponent->Enabled = entry.value("EntityEnabled", true);
            auto& transform = scene->GetRegistry().get<TransformComponent>(entity);

            if (entry.contains("Transform"))
            {
                const auto& transformJson = entry["Transform"];
                auto position = transformJson.value("Position", std::vector<float>{ 0.0f, 0.0f, 0.0f });
                auto rotation = transformJson.value("Rotation", std::vector<float>{ 0.0f, 0.0f, 0.0f });
                auto scale = transformJson.value("Scale", std::vector<float>{ 1.0f, 1.0f, 1.0f });
                if (position.size() >= 3)
                    transform.Position = glm::vec3(position[0], position[1], position[2]);
                if (rotation.size() >= 3)
                    transform.Rotation = glm::vec3(rotation[0], rotation[1], rotation[2]);
                if (scale.size() >= 3)
                    transform.Scale = glm::vec3(scale[0], scale[1], scale[2]);
            }

            if (entry.contains("Canvas") && entry["Canvas"].is_object())
            {
                const auto& canvasJson = entry["Canvas"];
                auto& canvas = scene->GetRegistry().emplace<CanvasComponent>(entity);
                const std::string renderMode = canvasJson.value("RenderMode", std::string("ScreenSpace"));
                canvas.Mode = (renderMode == "WorldSpace")
                    ? CanvasComponent::RenderMode::WorldSpace
                    : CanvasComponent::RenderMode::ScreenSpace;
                canvas.SortOrder = canvasJson.value("SortOrder", 0);
                auto referenceResolution = canvasJson.value("ReferenceResolution",
                    std::vector<float>{ canvas.ReferenceResolution.x, canvas.ReferenceResolution.y });
                if (referenceResolution.size() >= 2)
                {
                    canvas.ReferenceResolution = glm::vec2(
                        std::max(1.0f, referenceResolution[0]),
                        std::max(1.0f, referenceResolution[1]));
                }
            }

            if (entry.contains("RectTransform") && entry["RectTransform"].is_object())
            {
                const auto& rectJson = entry["RectTransform"];
                auto& rect = scene->GetRegistry().emplace<RectTransformComponent>(entity);
                auto anchorMin = rectJson.value("AnchorMin", std::vector<float>{ rect.AnchorMin.x, rect.AnchorMin.y });
                if (anchorMin.size() >= 2)
                    rect.AnchorMin = glm::vec2(anchorMin[0], anchorMin[1]);
                auto anchorMax = rectJson.value("AnchorMax", std::vector<float>{ rect.AnchorMax.x, rect.AnchorMax.y });
                if (anchorMax.size() >= 2)
                    rect.AnchorMax = glm::vec2(anchorMax[0], anchorMax[1]);
                auto pivot = rectJson.value("Pivot", std::vector<float>{ rect.Pivot.x, rect.Pivot.y });
                if (pivot.size() >= 2)
                    rect.Pivot = glm::vec2(pivot[0], pivot[1]);
                auto sizeDelta = rectJson.value("SizeDelta", std::vector<float>{ rect.SizeDelta.x, rect.SizeDelta.y });
                if (sizeDelta.size() >= 2)
                    rect.SizeDelta = glm::vec2(sizeDelta[0], sizeDelta[1]);
                auto anchoredPosition = rectJson.value("AnchoredPosition",
                    std::vector<float>{ rect.AnchoredPosition.x, rect.AnchoredPosition.y });
                if (anchoredPosition.size() >= 2)
                    rect.AnchoredPosition = glm::vec2(anchoredPosition[0], anchoredPosition[1]);
            }

            if (entry.contains("Sprite"))
            {
                const auto& spriteJson = entry["Sprite"];
                auto& sprite = scene->GetRegistry().emplace<SpriteComponent>(entity);
                // Backward compatible:
                // - v1: Sprite.TextureKey (string)
                // - v2+: Sprite.Texture { guid, key }
                if (spriteJson.contains("Texture"))
                {
                    sprite.TextureKey = ResolveAssetKeyFromSceneJson(spriteJson["Texture"]);
                }
                else
                {
                    sprite.TextureKey = spriteJson.value("TextureKey", "");
                }
                sprite.TextureLoadAttempted = false;
                auto color = spriteJson.value("Color", std::vector<float>{ 1.0f, 1.0f, 1.0f, 1.0f });
                if (color.size() >= 4)
                    sprite.Color = glm::vec4(color[0], color[1], color[2], color[3]);

                // Backward compatible:
                // - v1-v10: TilingFactor as scalar float
                // - v11+:   TilingFactor as [x, y]
                const auto tilingValue = spriteJson.find("TilingFactor");
                if (tilingValue != spriteJson.end())
                {
                    if (tilingValue->is_number())
                    {
                        const float uniformTiling = std::max(0.001f, tilingValue->get<float>());
                        sprite.TilingFactor = glm::vec2(uniformTiling, uniformTiling);
                    }
                    else if (tilingValue->is_array() && tilingValue->size() >= 2)
                    {
                        const float tilingX = std::max(0.001f, (*tilingValue)[0].get<float>());
                        const float tilingY = std::max(0.001f, (*tilingValue)[1].get<float>());
                        sprite.TilingFactor = glm::vec2(tilingX, tilingY);
                    }
                }
                sprite.RenderOrder = spriteJson.value("RenderOrder", 0);
                sprite.CastShadows = spriteJson.value("CastShadows", true);
                sprite.ReceiveShadows = spriteJson.value("ReceiveShadows", true);

                sprite.SubSpriteIndex = spriteJson.value("SubSpriteIndex", -1);
                if (spriteJson.contains("UvMin") && spriteJson["UvMin"].is_array() && spriteJson["UvMin"].size() >= 2)
                    sprite.UvMin = glm::vec2(spriteJson["UvMin"][0].get<float>(), spriteJson["UvMin"][1].get<float>());
                if (spriteJson.contains("UvMax") && spriteJson["UvMax"].is_array() && spriteJson["UvMax"].size() >= 2)
                    sprite.UvMax = glm::vec2(spriteJson["UvMax"][0].get<float>(), spriteJson["UvMax"][1].get<float>());
            }

            if (entry.contains("Animator") && entry["Animator"].is_object())
            {
                const auto& animatorJson = entry["Animator"];
                auto& animator = scene->GetRegistry().emplace<AnimatorComponent>(entity);

                if (animatorJson.contains("Controller"))
                    animator.ControllerKey = ResolveAssetKeyFromSceneJson(animatorJson["Controller"]);
                else
                    animator.ControllerKey = ResolveLatestKeyFromDatabase(animatorJson.value("ControllerKey", std::string{}));

                if (animatorJson.contains("DefaultClip"))
                    animator.DefaultClipKey = ResolveAssetKeyFromSceneJson(animatorJson["DefaultClip"]);
                else
                    animator.DefaultClipKey = ResolveLatestKeyFromDatabase(animatorJson.value("DefaultClipKey", std::string{}));

                animator.PlaybackSpeed = animatorJson.value("PlaybackSpeed", 1.0f);
                animator.Enabled = animatorJson.value("Enabled", true);
                animator.ApplyToSprite = animatorJson.value("ApplyToSprite", true);
                animator.ApplyToTransform = animatorJson.value("ApplyToTransform", true);
                animator.AutoPlay = animatorJson.value("AutoPlay", true);

                if (animatorJson.contains("BoolParameters") && animatorJson["BoolParameters"].is_object())
                {
                    for (auto it = animatorJson["BoolParameters"].begin(); it != animatorJson["BoolParameters"].end(); ++it)
                    {
                        if (it.value().is_boolean())
                            animator.BoolParameters[it.key()] = it.value().get<bool>();
                    }
                }
                if (animatorJson.contains("FloatParameters") && animatorJson["FloatParameters"].is_object())
                {
                    for (auto it = animatorJson["FloatParameters"].begin(); it != animatorJson["FloatParameters"].end(); ++it)
                    {
                        if (it.value().is_number())
                            animator.FloatParameters[it.key()] = it.value().get<float>();
                    }
                }
                if (animatorJson.contains("IntegerParameters") && animatorJson["IntegerParameters"].is_object())
                {
                    for (auto it = animatorJson["IntegerParameters"].begin(); it != animatorJson["IntegerParameters"].end(); ++it)
                    {
                        if (it.value().is_number_integer())
                            animator.IntegerParameters[it.key()] = it.value().get<int32_t>();
                    }
                }
                if (animatorJson.contains("TriggerParameters") && animatorJson["TriggerParameters"].is_object())
                {
                    for (auto it = animatorJson["TriggerParameters"].begin(); it != animatorJson["TriggerParameters"].end(); ++it)
                    {
                        if (it.value().is_boolean())
                            animator.TriggerParameters[it.key()] = it.value().get<bool>();
                    }
                }
            }

            if (entry.contains("AnimationEventReceiver") && entry["AnimationEventReceiver"].is_object())
            {
                const auto& receiverJson = entry["AnimationEventReceiver"];
                auto& receiver = scene->GetRegistry().emplace<AnimationEventReceiverComponent>(entity);
                receiver.Enabled = receiverJson.value("Enabled", true);
                receiver.RuntimeDispatchedEvents.clear();
                receiver.RuntimeDispatchFrame = 0;
            }

            if (entry.contains("Material"))
            {
                const auto& materialJson = entry["Material"];
                auto& material = scene->GetRegistry().emplace<MaterialComponent>(entity);
                // Backward compatible:
                // - v1: Material.MaterialKey (string)
                // - v2+: Material { guid, key }
                if (materialJson.is_object() && materialJson.contains("MaterialKey"))
                {
                    material.MaterialKey = ResolveLatestKeyFromDatabase(materialJson.value("MaterialKey", ""));
                }
                else
                {
                    material.MaterialKey = ResolveAssetKeyFromSceneJson(materialJson);
                }
                material.CachedMaterial.reset();
                material.MaterialLoadAttempted = false;
            }

            if (entry.contains("DirectionalLight2D") && entry["DirectionalLight2D"].is_object())
            {
                const auto& directionalLightJson = entry["DirectionalLight2D"];
                auto& directionalLight = scene->GetRegistry().emplace<DirectionalLight2DComponent>(entity);
                directionalLight.Enabled = directionalLightJson.value("Enabled", true);
                auto color = directionalLightJson.value("Color", std::vector<float>{ 1.0f, 1.0f, 1.0f });
                if (color.size() >= 3)
                    directionalLight.Color = glm::vec3(color[0], color[1], color[2]);
                directionalLight.Intensity = directionalLightJson.value("Intensity", 1.0f);
                directionalLight.UseEntityRotation = directionalLightJson.value("UseEntityRotation", true);
                auto direction = directionalLightJson.value("Direction", std::vector<float>{ 0.0f, -1.0f });
                if (direction.size() >= 2)
                {
                    directionalLight.Direction = glm::vec2(direction[0], direction[1]);
                    if (glm::length(directionalLight.Direction) > 0.0001f)
                        directionalLight.Direction = glm::normalize(directionalLight.Direction);
                    else
                        directionalLight.Direction = glm::vec2(0.0f, -1.0f);
                }
                directionalLight.CastShadows = directionalLightJson.value("CastShadows", true);
                directionalLight.ShadowStrength = directionalLightJson.value("ShadowStrength", 1.0f);
                directionalLight.ShadowSoftness = directionalLightJson.value("ShadowSoftness", 1.0f);
                directionalLight.ShadowSamples = std::max(1, directionalLightJson.value("ShadowSamples", 8));
                directionalLight.ShadowDistance = directionalLightJson.value("ShadowDistance", 25.0f);
                directionalLight.ShadowBias = std::max(0.0f, directionalLightJson.value("ShadowBias", 0.02f));
                directionalLight.RuntimeResolvedDirection = glm::vec2(0.0f, -1.0f);
            }

            if (entry.contains("PointLight2D") && entry["PointLight2D"].is_object())
            {
                const auto& pointLightJson = entry["PointLight2D"];
                auto& pointLight = scene->GetRegistry().emplace<PointLight2DComponent>(entity);
                pointLight.Enabled = pointLightJson.value("Enabled", true);
                auto color = pointLightJson.value("Color", std::vector<float>{ 1.0f, 1.0f, 1.0f });
                if (color.size() >= 3)
                    pointLight.Color = glm::vec3(color[0], color[1], color[2]);
                pointLight.Intensity = pointLightJson.value("Intensity", 1.0f);
                pointLight.Radius = std::max(0.01f, pointLightJson.value("Radius", 5.0f));
                pointLight.Falloff = std::max(0.1f, pointLightJson.value("Falloff", 2.0f));
                pointLight.CastShadows = pointLightJson.value("CastShadows", true);
                pointLight.ShadowStrength = pointLightJson.value("ShadowStrength", 1.0f);
                pointLight.ShadowSoftness = pointLightJson.value("ShadowSoftness", 1.0f);
                pointLight.ShadowSamples = std::max(1, pointLightJson.value("ShadowSamples", 8));
                pointLight.ShadowBias = std::max(0.0f, pointLightJson.value("ShadowBias", 0.0015f));
                pointLight.RuntimeViewportPosition = glm::vec2(0.0f);
                pointLight.RuntimeViewportRadius = 0.0f;
            }

            if (entry.contains("ShadowOccluder2D") && entry["ShadowOccluder2D"].is_object())
            {
                const auto& shadowOccluderJson = entry["ShadowOccluder2D"];
                auto& shadowOccluder = scene->GetRegistry().emplace<ShadowOccluder2DComponent>(entity);
                shadowOccluder.Enabled = shadowOccluderJson.value("Enabled", true);
                const std::string sourceModeName = shadowOccluderJson.value("SourceMode", std::string("ManualPolygon"));
                shadowOccluder.Source = (sourceModeName == "PhysicsCollider")
                    ? ShadowOccluder2DComponent::SourceMode::PhysicsCollider
                    : ShadowOccluder2DComponent::SourceMode::ManualPolygon;
                shadowOccluder.Closed = shadowOccluderJson.value("Closed", true);
                shadowOccluder.Extrusion = std::max(0.0f, shadowOccluderJson.value("Extrusion", 0.0f));
                shadowOccluder.PolygonPoints.clear();
                if (shadowOccluderJson.contains("PolygonPoints") && shadowOccluderJson["PolygonPoints"].is_array())
                {
                    for (const auto& pointJson : shadowOccluderJson["PolygonPoints"])
                    {
                        if (!pointJson.is_array() || pointJson.size() < 2)
                            continue;
                        shadowOccluder.PolygonPoints.emplace_back(pointJson[0].get<float>(), pointJson[1].get<float>());
                    }
                }
                if (shadowOccluder.PolygonPoints.empty())
                {
                    shadowOccluder.PolygonPoints = {
                        glm::vec2(-0.5f, -0.5f),
                        glm::vec2(0.5f, -0.5f),
                        glm::vec2(0.5f, 0.5f),
                        glm::vec2(-0.5f, 0.5f)
                    };
                }
                shadowOccluder.RuntimeResolvedPolygonPoints.clear();
                shadowOccluder.RuntimeGeometryRevision = 0;
            }

            if (entry.contains("UIImage") && entry["UIImage"].is_object())
            {
                const auto& uiImageJson = entry["UIImage"];
                auto& uiImage = scene->GetRegistry().emplace<UIImageComponent>(entity);
                uiImage.RaycastTarget = uiImageJson.value("RaycastTarget", true);
            }

            if (entry.contains("UIPanel") && entry["UIPanel"].is_object())
            {
                const auto& uiPanelJson = entry["UIPanel"];
                auto& uiPanel = scene->GetRegistry().emplace<UIPanelComponent>(entity);
                auto color = uiPanelJson.value("BackgroundColor", std::vector<float>{ uiPanel.BackgroundColor.r, uiPanel.BackgroundColor.g, uiPanel.BackgroundColor.b, uiPanel.BackgroundColor.a });
                if (color.size() >= 4)
                    uiPanel.BackgroundColor = glm::vec4(color[0], color[1], color[2], color[3]);
                uiPanel.UseSpriteTexture = uiPanelJson.value("UseSpriteTexture", false);
                uiPanel.RaycastTarget = uiPanelJson.value("RaycastTarget", false);
            }

            if (entry.contains("UIText") && entry["UIText"].is_object())
            {
                const auto& uiTextJson = entry["UIText"];
                auto& uiText = scene->GetRegistry().emplace<UITextComponent>(entity);
                uiText.Text = uiTextJson.value("Value", std::string("Text"));
                uiText.FontFilePath = uiTextJson.value("FontFilePath", std::string{});
                uiText.FontSize = uiTextJson.value("FontSize", 32.0f);
                auto color = uiTextJson.value("Color", std::vector<float>{ 1.0f, 1.0f, 1.0f, 1.0f });
                if (color.size() >= 4)
                    uiText.Color = glm::vec4(color[0], color[1], color[2], color[3]);
                uiText.CachedFont.reset();
                uiText.FontLoadAttempted = false;
                uiText.RaycastTarget = uiTextJson.value("RaycastTarget", false);
            }

            if (entry.contains("UIButton") && entry["UIButton"].is_object())
            {
                const auto& uiButtonJson = entry["UIButton"];
                auto& uiButton = scene->GetRegistry().emplace<UIButtonComponent>(entity);
                uiButton.Interactable = uiButtonJson.value("Interactable", true);
                uiButton.UseStateColors = uiButtonJson.value("UseStateColors", true);
                const auto normalColor = uiButtonJson.value("NormalColor", std::vector<float>{ uiButton.NormalColor.r, uiButton.NormalColor.g, uiButton.NormalColor.b, uiButton.NormalColor.a });
                if (normalColor.size() >= 4)
                    uiButton.NormalColor = glm::vec4(normalColor[0], normalColor[1], normalColor[2], normalColor[3]);
                const auto hoveredColor = uiButtonJson.value("HoveredColor", std::vector<float>{ uiButton.HoveredColor.r, uiButton.HoveredColor.g, uiButton.HoveredColor.b, uiButton.HoveredColor.a });
                if (hoveredColor.size() >= 4)
                    uiButton.HoveredColor = glm::vec4(hoveredColor[0], hoveredColor[1], hoveredColor[2], hoveredColor[3]);
                const auto pressedColor = uiButtonJson.value("PressedColor", std::vector<float>{ uiButton.PressedColor.r, uiButton.PressedColor.g, uiButton.PressedColor.b, uiButton.PressedColor.a });
                if (pressedColor.size() >= 4)
                    uiButton.PressedColor = glm::vec4(pressedColor[0], pressedColor[1], pressedColor[2], pressedColor[3]);
                const auto disabledColor = uiButtonJson.value("DisabledColor", std::vector<float>{ uiButton.DisabledColor.r, uiButton.DisabledColor.g, uiButton.DisabledColor.b, uiButton.DisabledColor.a });
                if (disabledColor.size() >= 4)
                    uiButton.DisabledColor = glm::vec4(disabledColor[0], disabledColor[1], disabledColor[2], disabledColor[3]);
                uiButton.OnClickEvent = uiButtonJson.value("OnClickEvent", std::string{});
                uiButton.OnHoverEnterEvent = uiButtonJson.value("OnHoverEnterEvent", std::string{});
                uiButton.OnHoverExitEvent = uiButtonJson.value("OnHoverExitEvent", std::string{});
                uiButton.OnPressedEvent = uiButtonJson.value("OnPressedEvent", std::string{});
                uiButton.IsHovered = false;
                uiButton.IsPressed = false;
                uiButton.RuntimeHoverEnteredThisFrame = false;
                uiButton.RuntimeHoverExitedThisFrame = false;
                uiButton.RuntimePressedThisFrame = false;
                uiButton.RuntimeClickedThisFrame = false;
            }

            if (entry.contains("UISlider") && entry["UISlider"].is_object())
            {
                const auto& uiSliderJson = entry["UISlider"];
                auto& uiSlider = scene->GetRegistry().emplace<UISliderComponent>(entity);
                uiSlider.Interactable = uiSliderJson.value("Interactable", true);
                uiSlider.MinValue = uiSliderJson.value("MinValue", 0.0f);
                uiSlider.MaxValue = std::max(uiSlider.MinValue, uiSliderJson.value("MaxValue", 1.0f));
                uiSlider.Value = std::clamp(uiSliderJson.value("Value", uiSlider.MinValue), uiSlider.MinValue, uiSlider.MaxValue);
                const auto backgroundColor = uiSliderJson.value("BackgroundColor", std::vector<float>{ uiSlider.BackgroundColor.r, uiSlider.BackgroundColor.g, uiSlider.BackgroundColor.b, uiSlider.BackgroundColor.a });
                if (backgroundColor.size() >= 4)
                    uiSlider.BackgroundColor = glm::vec4(backgroundColor[0], backgroundColor[1], backgroundColor[2], backgroundColor[3]);
                const auto fillColor = uiSliderJson.value("FillColor", std::vector<float>{ uiSlider.FillColor.r, uiSlider.FillColor.g, uiSlider.FillColor.b, uiSlider.FillColor.a });
                if (fillColor.size() >= 4)
                    uiSlider.FillColor = glm::vec4(fillColor[0], fillColor[1], fillColor[2], fillColor[3]);
                const auto handleColor = uiSliderJson.value("HandleColor", std::vector<float>{ uiSlider.HandleColor.r, uiSlider.HandleColor.g, uiSlider.HandleColor.b, uiSlider.HandleColor.a });
                if (handleColor.size() >= 4)
                    uiSlider.HandleColor = glm::vec4(handleColor[0], handleColor[1], handleColor[2], handleColor[3]);
                uiSlider.HandleWidth = std::max(1.0f, uiSliderJson.value("HandleWidth", uiSlider.HandleWidth));
                uiSlider.HandleHeightMultiplier = std::max(0.1f, uiSliderJson.value("HandleHeightMultiplier", uiSlider.HandleHeightMultiplier));
                uiSlider.ShowHandle = uiSliderJson.value("ShowHandle", uiSlider.ShowHandle);
                uiSlider.OnValueChangedEvent = uiSliderJson.value("OnValueChangedEvent", std::string{});
                uiSlider.RuntimeDragging = false;
                uiSlider.RuntimeValueChangedThisFrame = false;
            }

            // UI compatibility migration: keep legacy scenes interactive by ensuring
            // required render/layout components exist for button/slider entities.
            if (scene->GetRegistry().all_of<UIButtonComponent>(entity))
            {
                if (!scene->GetRegistry().all_of<RectTransformComponent>(entity))
                    scene->GetRegistry().emplace<RectTransformComponent>(entity);
                if (!scene->GetRegistry().all_of<SpriteComponent>(entity))
                    scene->GetRegistry().emplace<SpriteComponent>(entity);
            }

            if (scene->GetRegistry().all_of<UISliderComponent>(entity))
            {
                if (!scene->GetRegistry().all_of<RectTransformComponent>(entity))
                    scene->GetRegistry().emplace<RectTransformComponent>(entity);
            }

            if (scene->GetRegistry().all_of<UIPanelComponent>(entity))
            {
                if (!scene->GetRegistry().all_of<RectTransformComponent>(entity))
                    scene->GetRegistry().emplace<RectTransformComponent>(entity);
            }

            if (entry.contains("Grid2D") && entry["Grid2D"].is_object())
            {
                const auto& grid2DJson = entry["Grid2D"];
                auto& grid2D = scene->GetRegistry().emplace<Grid2DComponent>(entity);

                auto cellSize = grid2DJson.value("CellSize", std::vector<float>{ grid2D.CellSize.x, grid2D.CellSize.y });
                if (cellSize.size() >= 2)
                    grid2D.CellSize = glm::vec2(std::max(0.001f, cellSize[0]), std::max(0.001f, cellSize[1]));

                auto cellGap = grid2DJson.value("CellGap", std::vector<float>{ grid2D.CellGap.x, grid2D.CellGap.y });
                if (cellGap.size() >= 2)
                    grid2D.CellGap = glm::vec2(cellGap[0], cellGap[1]);
            }

            if (entry.contains("TilemapLayer") && entry["TilemapLayer"].is_object())
            {
                const auto& layerJson = entry["TilemapLayer"];
                auto& layer = scene->GetRegistry().emplace<TilemapLayerComponent>(entity);

                auto gridSize = layerJson.value("GridSize", std::vector<int>{ layer.GridSize.x, layer.GridSize.y });
                if (gridSize.size() >= 2)
                    layer.GridSize = glm::ivec2(std::max(1, gridSize[0]), std::max(1, gridSize[1]));

                layer.RenderOrder = layerJson.value("RenderOrder", 0);
                layer.CollisionEnabled = layerJson.value("CollisionEnabled", false);
                layer.CastShadows = layerJson.value("CastShadows", false);

                layer.TileTable.clear();
                if (layerJson.contains("TileTable") && layerJson["TileTable"].is_array())
                {
                    for (const auto& tileRef : layerJson["TileTable"])
                        layer.TileTable.push_back(ResolveAssetKeyFromSceneJson(tileRef));
                }

                if (layerJson.contains("Tiles") && layerJson["Tiles"].is_array())
                    layer.Tiles = layerJson["Tiles"].get<std::vector<uint32_t>>();

                layer.EnsureStorage();
                layer.RenderCacheDirty = true;
            }

            if (entry.contains("Camera") && entry["Camera"].is_object())
            {
                const auto& cameraJson = entry["Camera"];
                auto& camera = scene->GetRegistry().emplace<CameraComponent>(entity);

                const std::string projectionName = cameraJson.value("Projection", "Orthographic2D");
                camera.Projection = (projectionName == "Perspective3D")
                    ? CameraComponent::ProjectionType::Perspective3D
                    : CameraComponent::ProjectionType::Orthographic2D;

                camera.IsPrimary = cameraJson.value("IsPrimary", true);
                camera.Zoom = cameraJson.value("Zoom", 1.0f);
                camera.NearPlane = cameraJson.value("NearPlane", -1.0f);
                camera.FarPlane = cameraJson.value("FarPlane", 1.0f);
                camera.FieldOfViewYDegrees = cameraJson.value("FieldOfViewYDegrees", 60.0f);
            }

            if (entry.contains("AudioListener2D") && entry["AudioListener2D"].is_object())
            {
                const auto& audioListenerJson = entry["AudioListener2D"];
                auto& audioListener = scene->GetRegistry().emplace<AudioListener2DComponent>(entity);
                audioListener.Enabled = audioListenerJson.value("Enabled", true);
                audioListener.UsePrimaryCameraPosition = audioListenerJson.value("UsePrimaryCameraPosition", true);
            }

            if (entry.contains("AudioSource"))
            {
                const auto& audioSourceJson = entry["AudioSource"];
                auto& audioSource = scene->GetRegistry().emplace<AudioSourceComponent>(entity);

                if (audioSourceJson.is_object() && audioSourceJson.contains("AudioClip"))
                    audioSource.AudioClipKey = ResolveAssetKeyFromSceneJson(audioSourceJson["AudioClip"]);
                else if (audioSourceJson.is_object())
                    audioSource.AudioClipKey = ResolveLatestKeyFromDatabase(audioSourceJson.value("AudioClipKey", std::string{}));

                if (audioSourceJson.is_object())
                {
                    audioSource.Volume = audioSourceJson.value("Volume", 1.0f);
                    if (audioSource.Volume < 0.0f)
                        audioSource.Volume = 0.0f;
                    audioSource.Pitch = std::max(0.01f, audioSourceJson.value("Pitch", 1.0f));
                    audioSource.PlayOnStart = audioSourceJson.value("PlayOnStart", true);
                    audioSource.Loop = audioSourceJson.value("Loop", false);
                    audioSource.Muted = audioSourceJson.value("Muted", false);
                    audioSource.Space = ParseAudioPlaybackSpaceName(audioSourceJson.value("PlaybackSpace", std::string("Global")));
                    if (audioSourceJson.value("Spatial", false))
                        audioSource.Space = AudioSourceComponent::PlaybackSpace::Spatial2D;
                    audioSource.MixerGroup = audioSourceJson.value("MixerGroup", std::string("SFX"));
                    if (audioSource.MixerGroup.empty())
                        audioSource.MixerGroup = "SFX";
                    audioSource.SpatialMinDistance = std::max(0.001f, audioSourceJson.value("SpatialMinDistance", 1.0f));
                    audioSource.SpatialMaxDistance = std::max(audioSource.SpatialMinDistance, audioSourceJson.value("SpatialMaxDistance", 20.0f));
                    audioSource.SpatialRolloffExponent = std::max(0.01f, audioSourceJson.value("SpatialRolloffExponent", 1.0f));
                    audioSource.StereoPanStrength = std::clamp(audioSourceJson.value("StereoPanStrength", 1.0f), 0.0f, 1.0f);
                    audioSource.AttenuationCurveKey = audioSourceJson.value("AttenuationCurveKey", std::string{});
                }

                audioSource.RuntimeVoiceId = 0;
                audioSource.RuntimePlaybackStarted = false;
            }

            if (entry.contains("Rigidbody2D") && entry["Rigidbody2D"].is_object())
            {
                const auto& rigidbody2DJson = entry["Rigidbody2D"];
                auto& rigidbody2D = scene->GetRegistry().emplace<Rigidbody2DComponent>(entity);
                const std::string bodyTypeName = rigidbody2DJson.value("BodyType", std::string("Dynamic"));
                if (bodyTypeName == "Static")
                    rigidbody2D.Type = Rigidbody2DComponent::BodyType::Static;
                else if (bodyTypeName == "Kinematic")
                    rigidbody2D.Type = Rigidbody2DComponent::BodyType::Kinematic;
                else
                    rigidbody2D.Type = Rigidbody2DComponent::BodyType::Dynamic;
                rigidbody2D.FreezePositionX = rigidbody2DJson.value("FreezePositionX", false);
                rigidbody2D.FreezePositionY = rigidbody2DJson.value("FreezePositionY", false);
                const bool freezeRotation = rigidbody2DJson.value("FreezeRotation", rigidbody2DJson.value("FixedRotation", false));
                rigidbody2D.FixedRotation = freezeRotation;
                rigidbody2D.UseCCD = rigidbody2DJson.value("UseCCD", rigidbody2DJson.value("IsBullet", false));
                rigidbody2D.EnableSleep = rigidbody2DJson.value("EnableSleep", true);
                rigidbody2D.StartAwake = rigidbody2DJson.value("StartAwake", true);
                rigidbody2D.Interpolate = rigidbody2DJson.value("Interpolate", true);
                rigidbody2D.HighContactQuality = rigidbody2DJson.value("HighContactQuality", false);
                rigidbody2D.ExtraSolverSubSteps = std::max(0, rigidbody2DJson.value("ExtraSolverSubSteps", 0));
                rigidbody2D.GravityScale = rigidbody2DJson.value("GravityScale", 1.0f);
                rigidbody2D.LinearDamping = rigidbody2DJson.value("LinearDamping", 0.0f);
                rigidbody2D.AngularDamping = rigidbody2DJson.value("AngularDamping", 0.01f);
                rigidbody2D.RuntimeBodyId = kNullPhysics2DBody;
                rigidbody2D.RuntimeBodyCreated = false;
                rigidbody2D.RuntimePreviousPosition = glm::vec2(0.0f);
                rigidbody2D.RuntimePreviousAngleRadians = 0.0f;
                rigidbody2D.RuntimeRenderPreviousPosition = glm::vec2(0.0f);
                rigidbody2D.RuntimeRenderPreviousAngleRadians = 0.0f;
                rigidbody2D.RuntimeRenderCurrentPosition = glm::vec2(0.0f);
                rigidbody2D.RuntimeRenderCurrentAngleRadians = 0.0f;
            }

            if (entry.contains("BoxCollider2D") && entry["BoxCollider2D"].is_object())
            {
                const auto& boxCollider2DJson = entry["BoxCollider2D"];
                auto& boxCollider2D = scene->GetRegistry().emplace<BoxCollider2DComponent>(entity);
                auto offset = boxCollider2DJson.value("Offset", std::vector<float>{ 0.0f, 0.0f });
                if (offset.size() >= 2)
                    boxCollider2D.Offset = glm::vec2(offset[0], offset[1]);
                auto size = boxCollider2DJson.value("Size", std::vector<float>{ 1.0f, 1.0f });
                if (size.size() >= 2)
                    boxCollider2D.Size = glm::vec2(size[0], size[1]);
                boxCollider2D.Density = boxCollider2DJson.value("Density", 1.0f);
                boxCollider2D.Friction = boxCollider2DJson.value("Friction", 0.5f);
                boxCollider2D.Restitution = boxCollider2DJson.value("Restitution", 0.0f);
                boxCollider2D.IsSensor = boxCollider2DJson.value("IsSensor", false);
                boxCollider2D.CollisionLayer = boxCollider2DJson.value("CollisionLayer", 1ull);
                boxCollider2D.CollisionMask = boxCollider2DJson.value("CollisionMask", ~0ull);
                boxCollider2D.RuntimeShapeId = kNullPhysics2DShape;
                boxCollider2D.RuntimeShapeCreated = false;
            }

            if (entry.contains("CircleCollider2D") && entry["CircleCollider2D"].is_object())
            {
                const auto& circleCollider2DJson = entry["CircleCollider2D"];
                auto& circleCollider2D = scene->GetRegistry().emplace<CircleCollider2DComponent>(entity);
                auto offset = circleCollider2DJson.value("Offset", std::vector<float>{ 0.0f, 0.0f });
                if (offset.size() >= 2)
                    circleCollider2D.Offset = glm::vec2(offset[0], offset[1]);
                circleCollider2D.Radius = circleCollider2DJson.value("Radius", 0.5f);
                circleCollider2D.Density = circleCollider2DJson.value("Density", 1.0f);
                circleCollider2D.Friction = circleCollider2DJson.value("Friction", 0.5f);
                circleCollider2D.Restitution = circleCollider2DJson.value("Restitution", 0.0f);
                circleCollider2D.IsSensor = circleCollider2DJson.value("IsSensor", false);
                circleCollider2D.CollisionLayer = circleCollider2DJson.value("CollisionLayer", 1ull);
                circleCollider2D.CollisionMask = circleCollider2DJson.value("CollisionMask", ~0ull);
                circleCollider2D.RuntimeShapeId = kNullPhysics2DShape;
                circleCollider2D.RuntimeShapeCreated = false;
            }

            int32_t jointConnectedEntityIndex = -1;
            if (entry.contains("Joint2D") && entry["Joint2D"].is_object())
            {
                const auto& joint2DJson = entry["Joint2D"];
                auto& joint2D = scene->GetRegistry().emplace<Joint2DComponent>(entity);
                const std::string jointTypeName = joint2DJson.value("Type", std::string("Distance"));
                if (jointTypeName == "Revolute")
                    joint2D.Type = Joint2DComponent::JointType::Revolute;
                else if (jointTypeName == "Prismatic")
                    joint2D.Type = Joint2DComponent::JointType::Prismatic;
                else
                    joint2D.Type = Joint2DComponent::JointType::Distance;
                jointConnectedEntityIndex = joint2DJson.value("ConnectedEntityIndex", -1);
                joint2D.CollideConnected = joint2DJson.value("CollideConnected", false);
                auto anchorA = joint2DJson.value("AnchorA", std::vector<float>{ 0.0f, 0.0f });
                if (anchorA.size() >= 2)
                    joint2D.AnchorA = glm::vec2(anchorA[0], anchorA[1]);
                auto anchorB = joint2DJson.value("AnchorB", std::vector<float>{ 0.0f, 0.0f });
                if (anchorB.size() >= 2)
                    joint2D.AnchorB = glm::vec2(anchorB[0], anchorB[1]);
                auto axis = joint2DJson.value("Axis", std::vector<float>{ 1.0f, 0.0f });
                if (axis.size() >= 2)
                    joint2D.Axis = glm::vec2(axis[0], axis[1]);
                joint2D.EnableLimit = joint2DJson.value("EnableLimit", false);
                auto limits = joint2DJson.value("Limits", std::vector<float>{ -1.0f, 1.0f });
                if (limits.size() >= 2)
                    joint2D.Limits = glm::vec2(limits[0], limits[1]);
                joint2D.EnableMotor = joint2DJson.value("EnableMotor", false);
                joint2D.MotorSpeed = joint2DJson.value("MotorSpeed", 0.0f);
                joint2D.MaxMotorForceOrTorque = joint2DJson.value("MaxMotorForceOrTorque", 10.0f);
                joint2D.EnableSpring = joint2DJson.value("EnableSpring", false);
                joint2D.Hertz = joint2DJson.value("Hertz", 5.0f);
                joint2D.DampingRatio = joint2DJson.value("DampingRatio", 0.7f);
                joint2D.ConnectedEntity = entt::null;
                joint2D.RuntimeJointId = kNullPhysics2DJoint;
                joint2D.RuntimeJointCreated = false;
            }

            auto loadNativeScriptEntry = [](const nlohmann::json& nativeScriptJson, NativeScriptEntry& outScriptEntry) {
                outScriptEntry.ScriptClassName = nativeScriptJson.value("Class", std::string{});
                outScriptEntry.ScriptAssetRelativePath = nativeScriptJson.value("AssetPath", std::string{});
                outScriptEntry.Enabled = nativeScriptJson.value("Enabled", true);
                if (nativeScriptJson.contains("ExposedProperties") && nativeScriptJson["ExposedProperties"].is_object())
                {
                    for (auto it = nativeScriptJson["ExposedProperties"].begin(); it != nativeScriptJson["ExposedProperties"].end(); ++it)
                    {
                        ScriptPropertyValue propertyValue;
                        if (DeserializeScriptPropertyValue(it.value(), propertyValue))
                            outScriptEntry.ExposedProperties[it.key()] = propertyValue;
                    }
                }
                outScriptEntry.RuntimeInitialized = false;
                outScriptEntry.RuntimeInstance.reset();
                outScriptEntry.RuntimeUpdateCount = 0;
                outScriptEntry.RuntimeWarnedOnUpdateTransformMutation = false;
            };

            if (entry.contains("NativeScripts") && entry["NativeScripts"].is_array())
            {
                auto& nativeScript = scene->GetRegistry().emplace<NativeScriptComponent>(entity);
                for (const auto& scriptEntryJson : entry["NativeScripts"])
                {
                    if (!scriptEntryJson.is_object())
                        continue;
                    auto& loadedScriptEntry = nativeScript.Scripts.emplace_back();
                    loadNativeScriptEntry(scriptEntryJson, loadedScriptEntry);
                }
            }
            else if (entry.contains("NativeScript") && entry["NativeScript"].is_object())
            {
                // Backward compatibility: legacy scenes with a single native script object.
                auto& nativeScript = scene->GetRegistry().emplace<NativeScriptComponent>(entity);
                auto& loadedScriptEntry = nativeScript.Scripts.emplace_back();
                loadNativeScriptEntry(entry["NativeScript"], loadedScriptEntry);
            }

            if (entry.contains("ParticleEmitter") && entry["ParticleEmitter"].is_object())
            {
                const auto& peJson = entry["ParticleEmitter"];
                auto& pe = scene->GetRegistry().emplace<ParticleEmitterComponent>(entity);

                pe.SpawnRate       = peJson.value("SpawnRate", 10.0f);
                pe.LifetimeMin     = peJson.value("LifetimeMin", 1.0f);
                pe.LifetimeMax     = peJson.value("LifetimeMax", 2.0f);
                pe.Looping         = peJson.value("Looping", true);
                pe.Duration        = peJson.value("Duration", 5.0f);
                pe.PlayOnStart     = peJson.value("PlayOnStart", true);
                pe.BurstEnabled    = peJson.value("BurstEnabled", false);
                pe.BurstCount      = peJson.value("BurstCount", 10u);
                auto spawnOffsetMin = peJson.value("SpawnOffsetMin", std::vector<float>{ 0.0f, 0.0f });
                if (spawnOffsetMin.size() >= 2)
                    pe.SpawnOffsetMin = glm::vec2(spawnOffsetMin[0], spawnOffsetMin[1]);
                auto spawnOffsetMax = peJson.value("SpawnOffsetMax", std::vector<float>{ 0.0f, 0.0f });
                if (spawnOffsetMax.size() >= 2)
                    pe.SpawnOffsetMax = glm::vec2(spawnOffsetMax[0], spawnOffsetMax[1]);
                pe.UseRadialSpawn = peJson.value("UseRadialSpawn", false);
                pe.SpawnRadiusMin = peJson.value("SpawnRadiusMin", 0.0f);
                pe.SpawnRadiusMax = peJson.value("SpawnRadiusMax", 0.0f);
                pe.SpeedMin        = peJson.value("SpeedMin", 50.0f);
                pe.SpeedMax        = peJson.value("SpeedMax", 100.0f);
                pe.AngleMin        = peJson.value("AngleMin", 0.0f);
                pe.AngleMax        = peJson.value("AngleMax", 360.0f);
                pe.RadialVelocity  = peJson.value("RadialVelocity", false);
                pe.GravityModifier = peJson.value("GravityModifier", 0.0f);
                pe.StartSizeMin    = peJson.value("StartSizeMin", 1.0f);
                pe.StartSizeMax    = peJson.value("StartSizeMax", 1.0f);
                pe.EndSize         = peJson.value("EndSize", 0.0f);

                auto startColor = peJson.value("StartColor", std::vector<float>{ 1.0f, 1.0f, 1.0f, 1.0f });
                if (startColor.size() >= 4)
                    pe.StartColor = glm::vec4(startColor[0], startColor[1], startColor[2], startColor[3]);
                auto endColor = peJson.value("EndColor", std::vector<float>{ 1.0f, 1.0f, 1.0f, 0.0f });
                if (endColor.size() >= 4)
                    pe.EndColor = glm::vec4(endColor[0], endColor[1], endColor[2], endColor[3]);

                pe.StartRotationMin = peJson.value("StartRotationMin", 0.0f);
                pe.StartRotationMax = peJson.value("StartRotationMax", 0.0f);
                pe.RotationSpeedMin = peJson.value("RotationSpeedMin", 0.0f);
                pe.RotationSpeedMax = peJson.value("RotationSpeedMax", 0.0f);

                if (peJson.contains("Texture"))
                    pe.TextureKey = ResolveAssetKeyFromSceneJson(peJson["Texture"]);

                pe.MaxParticles = std::min(peJson.value("MaxParticles", 1024u),
                                           ParticleEmitterComponent::kMaxParticlesCap);

                pe.CachedTexture = nullptr;
                pe.TextureLoadAttempted = false;
                pe.RuntimeState.reset();
                pe.Playing = false;
                pe.Paused = false;
            }

            if (entry.contains("PrefabInstance") && entry["PrefabInstance"].is_object())
            {
                const auto& prefabJson = entry["PrefabInstance"];
                auto& prefabInstance = scene->GetRegistry().emplace<PrefabInstanceComponent>(entity);
                if (prefabJson.contains("Prefab"))
                    prefabInstance.PrefabAssetKey = ResolveAssetKeyFromSceneJson(prefabJson["Prefab"]);
            }

            int32_t parentIndex = -1;
            int32_t siblingOrder = 0;
            if (entry.contains("Hierarchy"))
            {
                const auto& hierarchyJson = entry["Hierarchy"];
                parentIndex = hierarchyJson.value("ParentIndex", -1);
                siblingOrder = hierarchyJson.value("SiblingOrder", 0);
            }

            ResetRuntimeStateForEntity(scene->GetRegistry(), entity);

            createdEntities.push_back(entity);
            parentIndices.push_back(parentIndex);
            siblingOrders.push_back(siblingOrder);
            jointConnectedEntityIndices.push_back(jointConnectedEntityIndex);
        }

        auto& registry = scene->GetRegistry();
        for (size_t index = 0; index < createdEntities.size(); ++index)
        {
            auto* hierarchy = registry.try_get<HierarchyComponent>(createdEntities[index]);
            if (!hierarchy)
                hierarchy = &registry.emplace<HierarchyComponent>(createdEntities[index]);

            const int32_t parentIndex = parentIndices[index];
            if (parentIndex >= 0 && static_cast<size_t>(parentIndex) < createdEntities.size())
                hierarchy->Parent = createdEntities[static_cast<size_t>(parentIndex)];
            else
                hierarchy->Parent = entt::null;
            hierarchy->SiblingOrder = siblingOrders[index];

            auto* joint2D = registry.try_get<Joint2DComponent>(createdEntities[index]);
            if (joint2D)
            {
                const int32_t connectedEntityIndex = jointConnectedEntityIndices[index];
                if (connectedEntityIndex >= 0 && static_cast<size_t>(connectedEntityIndex) < createdEntities.size())
                    joint2D->ConnectedEntity = createdEntities[static_cast<size_t>(connectedEntityIndex)];
                else
                    joint2D->ConnectedEntity = entt::null;
            }
        }

        // Post-load UI slider migration:
        // Run after hierarchy restoration so direct-child lookups are stable and
        // we do not create duplicate visual children while entities are still loading.
        auto ensureSliderVisualChild = [&](entt::entity sliderEntity,
                                           const char* childName,
                                           const glm::vec4& defaultColor,
                                           int32_t siblingOrder,
                                           auto&& initializeRectTransform) {
            entt::entity childEntity = FindDirectChildByTag(registry, sliderEntity, childName);
            bool created = false;
            if (childEntity == entt::null)
            {
                childEntity = scene->CreateEntity(childName);
                scene->SetParent(childEntity, sliderEntity);
                created = true;
            }

            if (created)
            {
                if (auto* hierarchy = registry.try_get<HierarchyComponent>(childEntity))
                    hierarchy->SiblingOrder = siblingOrder;
            }

            if (!registry.all_of<RectTransformComponent>(childEntity))
                registry.emplace<RectTransformComponent>(childEntity);
            if (!registry.all_of<UIImageComponent>(childEntity))
                registry.emplace<UIImageComponent>(childEntity);
            if (!registry.all_of<SpriteComponent>(childEntity))
            {
                auto& childSprite = registry.emplace<SpriteComponent>(childEntity);
                childSprite.Color = defaultColor;
            }

            if (created)
            {
                auto& rect = registry.get<RectTransformComponent>(childEntity);
                initializeRectTransform(rect);
            }
        };

        auto sliderView = registry.view<UISliderComponent>();
        for (entt::entity sliderEntity : sliderView)
        {
            const auto& slider = sliderView.get<UISliderComponent>(sliderEntity);
            const float sliderRange = std::max(0.0001f, slider.MaxValue - slider.MinValue);
            const float sliderNormalized = std::clamp((slider.Value - slider.MinValue) / sliderRange, 0.0f, 1.0f);

            ensureSliderVisualChild(sliderEntity, "Slider Background", slider.BackgroundColor, 0, [](RectTransformComponent& rect) {
                rect.AnchorMin = glm::vec2(0.0f, 0.0f);
                rect.AnchorMax = glm::vec2(1.0f, 1.0f);
                rect.Pivot = glm::vec2(0.5f, 0.5f);
                rect.SizeDelta = glm::vec2(0.0f, 0.0f);
                rect.AnchoredPosition = glm::vec2(0.0f, 0.0f);
            });
            ensureSliderVisualChild(sliderEntity, "Slider Fill", slider.FillColor, 10, [sliderNormalized](RectTransformComponent& rect) {
                rect.AnchorMin = glm::vec2(0.0f, 0.0f);
                rect.AnchorMax = glm::vec2(sliderNormalized, 1.0f);
                rect.Pivot = glm::vec2(0.5f, 0.5f);
                rect.SizeDelta = glm::vec2(0.0f, 0.0f);
                rect.AnchoredPosition = glm::vec2(0.0f, 0.0f);
            });
            ensureSliderVisualChild(sliderEntity, "Slider Handle", slider.HandleColor, 20, [sliderNormalized](RectTransformComponent& rect) {
                rect.AnchorMin = glm::vec2(sliderNormalized, 0.5f);
                rect.AnchorMax = glm::vec2(sliderNormalized, 0.5f);
                rect.Pivot = glm::vec2(0.5f, 0.5f);
                rect.SizeDelta = glm::vec2(16.0f, 48.0f);
                rect.AnchoredPosition = glm::vec2(0.0f, 0.0f);
            });
        }

        return Result<std::unique_ptr<Scene>>(std::move(scene));
    }

    void SceneRenderer::Render(Scene& scene, const Camera& camera)
    {
        Renderer2D::BeginScene(camera.GetViewProjectionMatrix(), false);
        const float fixedDelta = Time::GetFixedDeltaTimeSeconds();
        const float interpolationAlpha = (fixedDelta > 0.0f)
            ? std::clamp(Time::GetFixedTimeAccumulatorSeconds() / fixedDelta, 0.0f, 1.0f)
            : 1.0f;

        auto& registry = scene.GetRegistry();
        // ---- Grid2D + TilemapLayer rendering path ----------------------------
        // Renders entities using the new Grid2DComponent + child TilemapLayerComponent
        // architecture. Each Grid2D entity defines the cell layout; its children
        // with TilemapLayerComponent store per-cell tile references.
        auto renderGrid2DLayersAtOrder = [&](int32_t targetRenderOrder) {
            auto gridView = registry.view<TransformComponent, Grid2DComponent>();
            for (entt::entity gridEntity : gridView)
            {
                if (!scene.IsEntityEnabledInHierarchy(gridEntity))
                    continue;

                const auto& grid2D = registry.get<Grid2DComponent>(gridEntity);
                const glm::vec2 cellSize(
                    std::max(0.001f, grid2D.CellSize.x),
                    std::max(0.001f, grid2D.CellSize.y));

                const glm::mat4 gridWorldTransform =
                    scene.GetWorldTransformMatrixForRendering(gridEntity, interpolationAlpha);

                // Collect and sort layer children by RenderOrder.
                const auto children = scene.GetChildren(gridEntity);
                std::vector<entt::entity> layerEntities;
                layerEntities.reserve(children.size());
                for (entt::entity child : children)
                {
                    if (registry.all_of<TilemapLayerComponent>(child) &&
                        scene.IsEntityEnabledInHierarchy(child))
                    {
                        layerEntities.push_back(child);
                    }
                }
                std::sort(layerEntities.begin(), layerEntities.end(),
                    [&registry](entt::entity a, entt::entity b) {
                        return registry.get<TilemapLayerComponent>(a).RenderOrder
                             < registry.get<TilemapLayerComponent>(b).RenderOrder;
                    });

                for (entt::entity layerEntity : layerEntities)
                {
                    auto& layer = registry.get<TilemapLayerComponent>(layerEntity);

                    // Filter by exact render order so tile layers can interleave
                    // with regular sprites using the same order channel.
                    if (layer.RenderOrder != targetRenderOrder)
                        continue;

                    layer.EnsureStorage();
                    // Track which TileTable entries are actually referenced by
                    // cells in this layer. Large palettes may populate a huge
                    // TileTable, but only a subset is typically painted.
                    std::vector<bool> usedTileTableEntries(layer.TileTable.size(), false);
                    for (uint32_t tileId : layer.Tiles)
                    {
                        if (tileId == 0u)
                            continue;
                        if (static_cast<size_t>(tileId) < usedTileTableEntries.size())
                            usedTileTableEntries[tileId] = true;
                    }

                    // Rebuild render cache if dirty.
                    if (layer.RenderCacheDirty)
                    {
                        layer.CachedTileRender.clear();
                        layer.CachedTileRender.resize(layer.TileTable.size());
                        bool allResolved = true;

                        // Cache tile data and sprite settings per texture key
                        // to avoid redundant disk reads.
                        std::unordered_map<std::string, Assets::SpriteImportSettings> settingsCache;
                        std::vector<Assets::TileAssetData> tileDataCache(layer.TileTable.size());
                        std::vector<bool> tileDataValid(layer.TileTable.size(), false);

                        for (size_t ti = 0; ti < layer.TileTable.size(); ++ti)
                        {
                            if (ti >= usedTileTableEntries.size() || !usedTileTableEntries[ti])
                                continue;

                            const std::string& tileKey = layer.TileTable[ti];
                            if (tileKey.empty())
                                continue;

                            auto tileResult = Assets::LoadTileAssetData(tileKey);
                            if (tileResult.IsFailure())
                                continue;

                            tileDataCache[ti] = std::move(tileResult.GetValue());
                            tileDataValid[ti] = true;

                            const auto& tileData = tileDataCache[ti];
                            auto& cached = layer.CachedTileRender[ti];
                            cached.Color = tileData.Color;

                            if (tileData.SpriteTextureKey.empty())
                                continue;

                            cached.Texture = std::dynamic_pointer_cast<Assets::TextureAsset>(
                                Assets::AssetManager::GetCachedByKey(tileData.SpriteTextureKey));
                            if (!cached.Texture)
                            {
                                if (!g_PendingTextureLoads.contains(tileData.SpriteTextureKey))
                                    g_PendingTextureLoads.emplace(tileData.SpriteTextureKey,
                                        Assets::TextureAsset::LoadAsync(tileData.SpriteTextureKey));
                                allResolved = false;
                                continue;
                            }

                            if (tileData.SubSpriteIndex >= 0 && cached.Texture->GetTexture())
                            {
                                auto settingsIt = settingsCache.find(tileData.SpriteTextureKey);
                                if (settingsIt == settingsCache.end())
                                    settingsIt = settingsCache.emplace(tileData.SpriteTextureKey,
                                        Assets::LoadSpriteImportSettings(tileData.SpriteTextureKey)).first;

                                if (tileData.SubSpriteIndex < static_cast<int32_t>(settingsIt->second.SubSprites.size()))
                                {
                                    const auto& rect = settingsIt->second.SubSprites[
                                        static_cast<size_t>(tileData.SubSpriteIndex)].RectPixels;
                                    const glm::vec4 uvs = Assets::ComputeSubSpriteUvs(
                                        rect,
                                        cached.Texture->GetTexture()->GetWidth(),
                                        cached.Texture->GetTexture()->GetHeight());
                                    // ComputeSubSpriteUvs returns UVs in image space
                                    // (Y=0 at top). Textures are loaded with FlipVerticallyOnLoad,
                                    // so OpenGL UV Y=0 maps to the image bottom. Swap Y to match.
                                    cached.UvMin = glm::vec2(uvs.x, 1.0f - uvs.w);
                                    cached.UvMax = glm::vec2(uvs.z, 1.0f - uvs.y);
                                }
                            }
                            else
                            {
                                cached.UvMin = glm::vec2(0.0f);
                                cached.UvMax = glm::vec2(1.0f);
                            }
                        }

                        if (allResolved)
                            layer.RenderCacheDirty = false;
                    }
                    else
                    {
                        // Check for textures that finished async loading.
                        // Only check the asset manager cache — don't re-read tile
                        // files from disk every frame.
                        for (size_t ti = 0; ti < layer.CachedTileRender.size(); ++ti)
                        {
                            auto& cached = layer.CachedTileRender[ti];
                            if (!cached.Texture &&
                                ti < layer.TileTable.size() &&
                                ti < usedTileTableEntries.size() &&
                                usedTileTableEntries[ti] &&
                                !layer.TileTable[ti].empty())
                            {
                                // Re-check pending texture loads without re-reading tile JSON.
                                // Mark dirty so the full rebuild runs once textures become available.
                                auto pendingIt = g_PendingTextureLoads.begin();
                                bool anyNewlyAvailable = false;
                                while (pendingIt != g_PendingTextureLoads.end())
                                {
                                    auto loaded = std::dynamic_pointer_cast<Assets::TextureAsset>(
                                        Assets::AssetManager::GetCachedByKey(pendingIt->first));
                                    if (loaded && loaded->GetTexture())
                                    {
                                        anyNewlyAvailable = true;
                                        pendingIt = g_PendingTextureLoads.erase(pendingIt);
                                    }
                                    else
                                    {
                                        ++pendingIt;
                                    }
                                }
                                if (anyNewlyAvailable)
                                    layer.RenderCacheDirty = true;
                                break;
                            }
                        }
                    }

                    // Render tiles.
                    const int32_t gridWidth  = std::max(1, layer.GridSize.x);
                    const int32_t gridHeight = std::max(1, layer.GridSize.y);
                    const glm::vec2 mapCenterOffset =
                        -0.5f * glm::vec2(gridWidth - 1, gridHeight - 1) * cellSize;

                    for (int32_t cellY = 0; cellY < gridHeight; ++cellY)
                    {
                        for (int32_t cellX = 0; cellX < gridWidth; ++cellX)
                        {
                            const size_t tileIdx =
                                static_cast<size_t>(cellY * gridWidth + cellX);
                            if (tileIdx >= layer.Tiles.size())
                                continue;

                            const uint32_t tileId = layer.Tiles[tileIdx];
                            if (tileId == 0u)
                                continue;

                            if (tileId >= layer.CachedTileRender.size())
                                continue;

                            const auto& cached = layer.CachedTileRender[tileId];
                            if (!cached.Texture || !cached.Texture->GetTexture())
                                continue;

                            const glm::vec3 localPosition = glm::vec3(
                                mapCenterOffset.x + static_cast<float>(cellX) * cellSize.x,
                                mapCenterOffset.y + static_cast<float>(cellY) * cellSize.y,
                                0.0f);
                            glm::mat4 tileTransform = gridWorldTransform;
                            tileTransform = glm::translate(tileTransform, localPosition);
                            tileTransform = glm::scale(tileTransform, glm::vec3(cellSize, 1.0f));
                            Renderer2D::DrawQuad(tileTransform, cached.Texture,
                                                 cached.Color, cached.UvMin, cached.UvMax);
                        }
                    }
                }
            }
        };

        auto drawSpriteEntity = [&](entt::entity entity) {
            if (!scene.IsEntityEnabledInHierarchy(entity))
                return;
            if (IsEntityInCanvasUiHierarchy(registry, entity))
                return;

            auto& sprite = registry.get<SpriteComponent>(entity);
            auto* animator = registry.try_get<AnimatorComponent>(entity);

            glm::mat4 model = scene.GetWorldTransformMatrixForRendering(entity, interpolationAlpha);
            bool useMissingAssetFallback = false;

            const glm::vec2 safeTilingFactor(
                std::max(0.001f, sprite.TilingFactor.x),
                std::max(0.001f, sprite.TilingFactor.y));
            glm::vec2 renderUvMin(0.0f, 0.0f);
            glm::vec2 renderUvMax = safeTilingFactor;
            if (animator && animator->Enabled && animator->ApplyToSprite && animator->RuntimeHasSpriteSubRect)
            {
                renderUvMin = animator->RuntimeSpriteUvMin;
                glm::vec2 frameSpan = animator->RuntimeSpriteUvMax - animator->RuntimeSpriteUvMin;
                if (frameSpan.x <= 0.0001f || frameSpan.y <= 0.0001f)
                    frameSpan = glm::vec2(1.0f, 1.0f);
                renderUvMax = animator->RuntimeSpriteUvMin + frameSpan * safeTilingFactor;
            }
            else if (sprite.SubSpriteIndex >= 0)
            {
                renderUvMin = sprite.UvMin;
                glm::vec2 subSpan = sprite.UvMax - sprite.UvMin;
                if (subSpan.x <= 0.0001f || subSpan.y <= 0.0001f)
                    subSpan = glm::vec2(1.0f, 1.0f);
                renderUvMax = sprite.UvMin + subSpan * safeTilingFactor;
            }

            Assets::TextureAsset::Ptr animatedTextureAsset;
            std::string animatedTextureKey;
            if (animator && animator->Enabled && animator->ApplyToSprite && !animator->RuntimeSpriteTextureOverrideKey.empty())
            {
                animatedTextureKey = animator->RuntimeSpriteTextureOverrideKey;
                if (!animator->RuntimeCachedSpriteTextureOverride && !animator->RuntimeSpriteTextureOverrideLoadAttempted)
                {
                    animator->RuntimeCachedSpriteTextureOverride = std::dynamic_pointer_cast<Assets::TextureAsset>(
                        Assets::AssetManager::GetCachedByKey(animatedTextureKey));
                    if (!animator->RuntimeCachedSpriteTextureOverride)
                    {
                        if (!g_PendingTextureLoads.contains(animatedTextureKey))
                            g_PendingTextureLoads.emplace(animatedTextureKey, Assets::TextureAsset::LoadAsync(animatedTextureKey));
                    }
                    animator->RuntimeSpriteTextureOverrideLoadAttempted = true;
                }
                else if (!animator->RuntimeCachedSpriteTextureOverride && animator->RuntimeSpriteTextureOverrideLoadAttempted)
                {
                    animator->RuntimeCachedSpriteTextureOverride = std::dynamic_pointer_cast<Assets::TextureAsset>(
                        Assets::AssetManager::GetCachedByKey(animatedTextureKey));
                    if (!animator->RuntimeCachedSpriteTextureOverride)
                    {
                        const auto pendingIt = g_PendingTextureLoads.find(animatedTextureKey);
                        if (pendingIt != g_PendingTextureLoads.end() && pendingIt->second.IsDone())
                        {
                            animator->RuntimeCachedSpriteTextureOverride = pendingIt->second.Get();
                            g_PendingTextureLoads.erase(pendingIt);
                        }
                    }
                }
                animatedTextureAsset = animator->RuntimeCachedSpriteTextureOverride;
            }

            // Material override (Unity-style): if the entity has a MaterialComponent, prefer its main texture.
            // Animator texture override still has highest priority so animation remains visible.
            Assets::TextureAsset::Ptr materialMainTextureAsset;
            if (auto* material = registry.try_get<MaterialComponent>(entity))
            {
                if (!material->MaterialKey.empty())
                {
                    if (!material->CachedMaterial && !material->MaterialLoadAttempted)
                    {
                        material->CachedMaterial = std::dynamic_pointer_cast<Assets::MaterialAsset>(
                            Assets::AssetManager::GetCachedByKey(material->MaterialKey));
                        if (!material->CachedMaterial)
                        {
                            if (!g_PendingMaterialLoads.contains(material->MaterialKey))
                                g_PendingMaterialLoads.emplace(material->MaterialKey, Assets::AssetManager::LoadAsync<Assets::MaterialAsset>(material->MaterialKey));
                        }
                        material->MaterialLoadAttempted = true;
                    }
                    else if (!material->CachedMaterial && material->MaterialLoadAttempted)
                    {
                        material->CachedMaterial = std::dynamic_pointer_cast<Assets::MaterialAsset>(
                            Assets::AssetManager::GetCachedByKey(material->MaterialKey));
                        if (!material->CachedMaterial)
                        {
                            const auto pendingIt = g_PendingMaterialLoads.find(material->MaterialKey);
                            if (pendingIt != g_PendingMaterialLoads.end() && pendingIt->second.IsDone())
                            {
                                material->CachedMaterial = pendingIt->second.Get();
                                g_PendingMaterialLoads.erase(pendingIt);
                            }
                        }
                    }
                    if (material->CachedMaterial)
                    {
                        materialMainTextureAsset = material->CachedMaterial->GetMainTextureHandle().Lock();
                    }
                    else
                    {
                        useMissingAssetFallback = true;
                    }
                }
            }

            Assets::TextureAsset::Ptr resolvedTextureAsset = animatedTextureAsset ? animatedTextureAsset : materialMainTextureAsset;

            // Fallback to Sprite texture when no animated texture override/material texture is active/ready.
            if (!resolvedTextureAsset && !sprite.TextureKey.empty())
            {
                if (!sprite.CachedTexture && !sprite.TextureLoadAttempted)
                {
                    auto tex = std::dynamic_pointer_cast<Assets::TextureAsset>(
                        Assets::AssetManager::GetCachedByKey(sprite.TextureKey));
                    if (!tex)
                    {
                        if (!g_PendingTextureLoads.contains(sprite.TextureKey))
                            g_PendingTextureLoads.emplace(sprite.TextureKey, Assets::TextureAsset::LoadAsync(sprite.TextureKey));
                    }
                    sprite.CachedTexture = tex;
                    sprite.TextureLoadAttempted = true;
                }
                else if (!sprite.CachedTexture && sprite.TextureLoadAttempted)
                {
                    sprite.CachedTexture = std::dynamic_pointer_cast<Assets::TextureAsset>(
                        Assets::AssetManager::GetCachedByKey(sprite.TextureKey));
                    if (!sprite.CachedTexture)
                    {
                        const auto pendingIt = g_PendingTextureLoads.find(sprite.TextureKey);
                        if (pendingIt != g_PendingTextureLoads.end() && pendingIt->second.IsDone())
                        {
                            sprite.CachedTexture = pendingIt->second.Get();
                            g_PendingTextureLoads.erase(pendingIt);
                        }
                    }
                }
                resolvedTextureAsset = sprite.CachedTexture;
            }

            if (resolvedTextureAsset)
            {
                Renderer2D::DrawQuad(model,
                                     resolvedTextureAsset,
                                     sprite.Color,
                                     renderUvMin,
                                     renderUvMax);
            }
            else if (useMissingAssetFallback || !sprite.TextureKey.empty() || !animatedTextureKey.empty())
            {
                Renderer2D::DrawQuad(model, glm::vec4(1.0f, 0.0f, 1.0f, sprite.Color.a));
            }
            else
            {
                Renderer2D::DrawQuad(model, sprite.Color);
            }
        };

        auto drawParticleEmitters = [&]() {
            auto particleView = registry.view<ParticleEmitterComponent, TransformComponent>();
            for (entt::entity entity : particleView)
            {
                if (!scene.IsEntityEnabledInHierarchy(entity))
                    continue;

                auto& emitter = particleView.get<ParticleEmitterComponent>(entity);
                if (!emitter.RuntimeState || emitter.RuntimeState->AliveCount == 0)
                    continue;

                // Resolve texture once per emitter (same lazy-load pattern as SpriteComponent)
                if (!emitter.TextureKey.empty() && !emitter.CachedTexture && !emitter.TextureLoadAttempted)
                {
                    emitter.CachedTexture = std::dynamic_pointer_cast<Assets::TextureAsset>(
                        Assets::AssetManager::GetCachedByKey(emitter.TextureKey));
                    if (!emitter.CachedTexture)
                    {
                        if (!g_PendingTextureLoads.contains(emitter.TextureKey))
                            g_PendingTextureLoads.emplace(emitter.TextureKey, Assets::TextureAsset::LoadAsync(emitter.TextureKey));
                    }
                    emitter.TextureLoadAttempted = true;
                }
                else if (!emitter.CachedTexture && emitter.TextureLoadAttempted && !emitter.TextureKey.empty())
                {
                    emitter.CachedTexture = std::dynamic_pointer_cast<Assets::TextureAsset>(
                        Assets::AssetManager::GetCachedByKey(emitter.TextureKey));
                    if (!emitter.CachedTexture)
                    {
                        const auto pendingIt = g_PendingTextureLoads.find(emitter.TextureKey);
                        if (pendingIt != g_PendingTextureLoads.end() && pendingIt->second.IsDone())
                        {
                            emitter.CachedTexture = pendingIt->second.Get();
                            g_PendingTextureLoads.erase(pendingIt);
                        }
                    }
                }

                const auto& runtime = *emitter.RuntimeState;
                const auto& emitterTransform = particleView.get<TransformComponent>(entity);
                const float emitterZ = emitterTransform.Position.z;

                for (uint32_t i = 0; i < runtime.AliveCount; ++i)
                {
                    const float t = 1.0f - (runtime.Lifetimes[i] / runtime.MaxLifetimes[i]);
                    const float size = glm::mix(runtime.StartSizes[i], runtime.EndSizes[i], t);
                    const glm::vec4 color = glm::mix(runtime.StartColors[i], runtime.EndColors[i], t);

                    glm::mat4 particleTransform(1.0f);
                    particleTransform = glm::translate(particleTransform, glm::vec3(runtime.Positions[i], emitterZ));
                    if (runtime.Rotations[i] != 0.0f)
                        particleTransform = glm::rotate(particleTransform, glm::radians(runtime.Rotations[i]), glm::vec3(0.0f, 0.0f, 1.0f));
                    particleTransform = glm::scale(particleTransform, glm::vec3(size, size, 1.0f));

                    if (emitter.CachedTexture)
                        Renderer2D::DrawQuad(particleTransform, emitter.CachedTexture, color);
                    else
                        Renderer2D::DrawQuad(particleTransform, color);
                }
            }
        };

        auto view = registry.view<TransformComponent, SpriteComponent>();
        std::vector<entt::entity> renderEntities;
        renderEntities.reserve(view.size_hint());
        std::set<int32_t> renderOrders;
        for (entt::entity entity : view)
        {
            renderEntities.push_back(entity);
            const auto& sprite = view.get<SpriteComponent>(entity);
            renderOrders.insert(sprite.RenderOrder);
        }

        auto gridView = registry.view<TransformComponent, Grid2DComponent>();
        for (entt::entity gridEntity : gridView)
        {
            if (!scene.IsEntityEnabledInHierarchy(gridEntity))
                continue;
            const auto children = scene.GetChildren(gridEntity);
            for (entt::entity child : children)
            {
                if (!registry.all_of<TilemapLayerComponent>(child) || !scene.IsEntityEnabledInHierarchy(child))
                    continue;
                renderOrders.insert(registry.get<TilemapLayerComponent>(child).RenderOrder);
            }
        }

        std::sort(renderEntities.begin(), renderEntities.end(), [&scene, &registry, interpolationAlpha](entt::entity left, entt::entity right) {
            const auto& leftSprite = registry.get<SpriteComponent>(left);
            const auto& rightSprite = registry.get<SpriteComponent>(right);
            if (leftSprite.RenderOrder != rightSprite.RenderOrder)
                return leftSprite.RenderOrder < rightSprite.RenderOrder;

            const glm::mat4 leftWorld = scene.GetWorldTransformMatrixForRendering(left, interpolationAlpha);
            const glm::mat4 rightWorld = scene.GetWorldTransformMatrixForRendering(right, interpolationAlpha);
            // Sort by world-space Z rather than view-space Z so the order stays
            // stable regardless of the editor camera orientation.
            const float leftWorldZ = leftWorld[3][2];
            const float rightWorldZ = rightWorld[3][2];
            constexpr float kDepthSortEpsilon = 0.005f;
            if (std::abs(leftWorldZ - rightWorldZ) > kDepthSortEpsilon)
                return leftWorldZ < rightWorldZ;

            const auto* leftHierarchy = registry.try_get<HierarchyComponent>(left);
            const auto* rightHierarchy = registry.try_get<HierarchyComponent>(right);
            const int32_t leftOrder = leftHierarchy ? leftHierarchy->SiblingOrder : 0;
            const int32_t rightOrder = rightHierarchy ? rightHierarchy->SiblingOrder : 0;
            if (leftOrder != rightOrder)
                return leftOrder < rightOrder;

            return static_cast<uint32_t>(left) < static_cast<uint32_t>(right);
        });

        size_t spriteCursor = 0;
        bool particlesRendered = false;
        for (int32_t renderOrder : renderOrders)
        {
            if (!particlesRendered && renderOrder > 0)
            {
                drawParticleEmitters();
                particlesRendered = true;
            }

            renderGrid2DLayersAtOrder(renderOrder);

            while (spriteCursor < renderEntities.size())
            {
                const entt::entity entity = renderEntities[spriteCursor];
                const auto& sprite = registry.get<SpriteComponent>(entity);
                if (sprite.RenderOrder != renderOrder)
                    break;
                ++spriteCursor;
                drawSpriteEntity(entity);
            }
        }

        if (!particlesRendered)
            drawParticleEmitters();

        Renderer2D::EndScene();
    }

    void SceneRenderer::SetViewportClearColor(const glm::vec4& clearColor)
    {
        g_ViewportClearColor = glm::vec4(
            std::clamp(clearColor.r, 0.0f, 1.0f),
            std::clamp(clearColor.g, 0.0f, 1.0f),
            std::clamp(clearColor.b, 0.0f, 1.0f),
            std::clamp(clearColor.a, 0.0f, 1.0f));
    }

    glm::vec4 SceneRenderer::GetViewportClearColor()
    {
        return g_ViewportClearColor;
    }

    void SceneRenderer::SetUiInputViewportRectPixels(float minX, float minY, float width, float height, bool enabled)
    {
        g_UiInputViewportRect.Enabled = enabled;
        g_UiInputViewportRect.MinPixels = glm::vec2(minX, minY);
        g_UiInputViewportRect.SizePixels = glm::vec2(std::max(0.0f, width), std::max(0.0f, height));
    }

    namespace
    {
        struct UiLayoutRect
        {
            glm::vec2 Min = glm::vec2(0.0f);
            glm::vec2 Max = glm::vec2(0.0f);
        };

        struct UiCanvasScreenSpaceMetrics
        {
            UiLayoutRect RootRect{};
            UiLayoutRect ProjectionRect{};
        };

        glm::vec2 Clamp01(const glm::vec2& value)
        {
            return glm::vec2(
                std::clamp(value.x, 0.0f, 1.0f),
                std::clamp(value.y, 0.0f, 1.0f));
        }

        UiLayoutRect GetCanvasRootRect(const CanvasComponent& canvas, uint32_t width, uint32_t height)
        {
            const float fallbackWidth = std::max(1.0f, static_cast<float>(width));
            const float fallbackHeight = std::max(1.0f, static_cast<float>(height));
            const float canvasWidth = std::max(1.0f, canvas.ReferenceResolution.x > 0.0f ? canvas.ReferenceResolution.x : fallbackWidth);
            const float canvasHeight = std::max(1.0f, canvas.ReferenceResolution.y > 0.0f ? canvas.ReferenceResolution.y : fallbackHeight);
            const float halfWidth = canvasWidth * 0.5f;
            const float halfHeight = canvasHeight * 0.5f;
            return {
                glm::vec2(-halfWidth, -halfHeight),
                glm::vec2(halfWidth, halfHeight)
            };
        }

        UiCanvasScreenSpaceMetrics BuildScreenSpaceCanvasMetrics(const CanvasComponent& canvas, uint32_t width, uint32_t height)
        {
            UiCanvasScreenSpaceMetrics metrics{};
            metrics.RootRect = GetCanvasRootRect(canvas, width, height);

            const glm::vec2 rootSize = metrics.RootRect.Max - metrics.RootRect.Min;
            const float rootWidth = std::max(1.0f, rootSize.x);
            const float rootHeight = std::max(1.0f, rootSize.y);
            const float viewportWidth = std::max(1.0f, static_cast<float>(width));
            const float viewportHeight = std::max(1.0f, static_cast<float>(height));

            const float uniformScale = std::max(0.0001f, std::min(viewportWidth / rootWidth, viewportHeight / rootHeight));
            const glm::vec2 projectionSize = glm::vec2(viewportWidth / uniformScale, viewportHeight / uniformScale);
            const glm::vec2 projectionCenter = (metrics.RootRect.Min + metrics.RootRect.Max) * 0.5f;
            const glm::vec2 projectionHalf = projectionSize * 0.5f;
            metrics.ProjectionRect.Min = projectionCenter - projectionHalf;
            metrics.ProjectionRect.Max = projectionCenter + projectionHalf;
            return metrics;
        }

        UiLayoutRect ResolveUiLayoutRect(const entt::registry& registry,
                                         entt::entity entity,
                                         entt::entity canvasEntity,
                                         const UiLayoutRect& canvasRootRect,
                                         std::unordered_map<entt::entity, UiLayoutRect>& cache)
        {
            const auto cached = cache.find(entity);
            if (cached != cache.end())
                return cached->second;

            UiLayoutRect parentRect = canvasRootRect;
            if (const auto* hierarchy = registry.try_get<HierarchyComponent>(entity))
            {
                const entt::entity parent = hierarchy->Parent;
                if (parent != entt::null &&
                    parent != canvasEntity &&
                    registry.all_of<RectTransformComponent>(parent))
                {
                    parentRect = ResolveUiLayoutRect(registry, parent, canvasEntity, canvasRootRect, cache);
                }
            }

            const auto& rect = registry.get<RectTransformComponent>(entity);
            glm::vec2 anchorMin = Clamp01(rect.AnchorMin);
            glm::vec2 anchorMax = Clamp01(rect.AnchorMax);
            if (anchorMin.x > anchorMax.x)
                std::swap(anchorMin.x, anchorMax.x);
            if (anchorMin.y > anchorMax.y)
                std::swap(anchorMin.y, anchorMax.y);

            const glm::vec2 parentSize = parentRect.Max - parentRect.Min;
            const glm::vec2 anchorMinPosition = parentRect.Min + parentSize * anchorMin;
            const glm::vec2 anchorMaxPosition = parentRect.Min + parentSize * anchorMax;
            glm::vec2 size = (anchorMaxPosition - anchorMinPosition) + rect.SizeDelta;
            size.x = std::max(1.0f, size.x);
            size.y = std::max(1.0f, size.y);

            const glm::vec2 pivot = Clamp01(rect.Pivot);
            const glm::vec2 anchorCenter = (anchorMinPosition + anchorMaxPosition) * 0.5f;
            const glm::vec2 pivotPosition = anchorCenter + rect.AnchoredPosition;
            const glm::vec2 centerPosition = pivotPosition + (glm::vec2(0.5f, 0.5f) - pivot) * size;

            UiLayoutRect result;
            result.Min = centerPosition - size * 0.5f;
            result.Max = centerPosition + size * 0.5f;
            cache.emplace(entity, result);
            return result;
        }

        Assets::TextureAsset::Ptr ResolveUiSpriteTexture(SpriteComponent& sprite)
        {
            if (sprite.TextureKey.empty())
                return nullptr;

            if (!sprite.CachedTexture && !sprite.TextureLoadAttempted)
            {
                sprite.CachedTexture = std::dynamic_pointer_cast<Assets::TextureAsset>(
                    Assets::AssetManager::GetCachedByKey(sprite.TextureKey));
                if (!sprite.CachedTexture && !g_PendingTextureLoads.contains(sprite.TextureKey))
                    g_PendingTextureLoads.emplace(sprite.TextureKey, Assets::TextureAsset::LoadAsync(sprite.TextureKey));
                sprite.TextureLoadAttempted = true;
            }
            else if (!sprite.CachedTexture)
            {
                sprite.CachedTexture = std::dynamic_pointer_cast<Assets::TextureAsset>(
                    Assets::AssetManager::GetCachedByKey(sprite.TextureKey));
                if (!sprite.CachedTexture)
                {
                    const auto pendingLoad = g_PendingTextureLoads.find(sprite.TextureKey);
                    if (pendingLoad != g_PendingTextureLoads.end() && pendingLoad->second.IsDone())
                    {
                        sprite.CachedTexture = pendingLoad->second.Get();
                        g_PendingTextureLoads.erase(pendingLoad);
                    }
                }
            }
            return sprite.CachedTexture;
        }

        glm::vec2 ConvertMousePixelsToCanvasSpace(const glm::vec2& mousePixels,
                                                  const CanvasComponent& canvas,
                                                  uint32_t windowWidth,
                                                  uint32_t windowHeight)
        {
            const UiCanvasScreenSpaceMetrics metrics = BuildScreenSpaceCanvasMetrics(canvas, windowWidth, windowHeight);
            const float safeWindowWidth = std::max(1.0f, static_cast<float>(windowWidth));
            const float safeWindowHeight = std::max(1.0f, static_cast<float>(windowHeight));
            const float normalizedX = mousePixels.x / safeWindowWidth;
            const float normalizedY = mousePixels.y / safeWindowHeight;
            const glm::vec2 projectionSize = metrics.ProjectionRect.Max - metrics.ProjectionRect.Min;
            return glm::vec2(
                metrics.ProjectionRect.Min.x + normalizedX * projectionSize.x,
                metrics.ProjectionRect.Max.y - normalizedY * projectionSize.y);
        }

        bool IsPointInsideRect(const glm::vec2& point, const UiLayoutRect& rect)
        {
            return point.x >= rect.Min.x && point.x <= rect.Max.x &&
                   point.y >= rect.Min.y && point.y <= rect.Max.y;
        }

        bool IsUiRaycastTargetEntity(const entt::registry& registry, entt::entity entity)
        {
            if (registry.any_of<UIButtonComponent, UISliderComponent>(entity))
                return true;
            if (const auto* uiImage = registry.try_get<UIImageComponent>(entity); uiImage && uiImage->RaycastTarget)
                return true;
            if (const auto* uiPanel = registry.try_get<UIPanelComponent>(entity); uiPanel && uiPanel->RaycastTarget)
                return true;
            if (const auto* uiText = registry.try_get<UITextComponent>(entity); uiText && uiText->RaycastTarget)
                return true;
            return false;
        }

        entt::entity FindInteractiveOwnerEntity(const entt::registry& registry, entt::entity entity)
        {
            entt::entity current = entity;
            while (current != entt::null)
            {
                if (IsUiRaycastTargetEntity(registry, current))
                    return current;
                const auto* hierarchy = registry.try_get<HierarchyComponent>(current);
                if (!hierarchy)
                    break;
                current = hierarchy->Parent;
            }
            return entt::null;
        }

        UiLayoutRect ApplyTransformToUiRect(const entt::registry& registry, entt::entity entity, const UiLayoutRect& rect)
        {
            UiLayoutRect adjusted = rect;
            if (const auto* transform = registry.try_get<TransformComponent>(entity))
            {
                const glm::vec2 baseSize = rect.Max - rect.Min;
                const glm::vec2 scaledSize = glm::vec2(
                    baseSize.x * std::max(0.001f, transform->Scale.x),
                    baseSize.y * std::max(0.001f, transform->Scale.y));
                glm::vec2 center = (rect.Min + rect.Max) * 0.5f;
                center += glm::vec2(transform->Position.x, transform->Position.y);
                adjusted.Min = center - scaledSize * 0.5f;
                adjusted.Max = center + scaledSize * 0.5f;
            }
            return adjusted;
        }

        void ProcessUiInteractionSystem(Scene& scene, uint32_t windowWidth, uint32_t windowHeight)
        {
            auto& registry = scene.GetRegistry();
            scene.SetUiPointerOverInteractiveElement(false);

            auto buttonView = registry.view<UIButtonComponent>();
            for (entt::entity entity : buttonView)
            {
                auto& button = buttonView.get<UIButtonComponent>(entity);
                button.RuntimeHoverEnteredThisFrame = false;
                button.RuntimeHoverExitedThisFrame = false;
                button.RuntimePressedThisFrame = false;
                button.RuntimeClickedThisFrame = false;
            }

            auto sliderView = registry.view<UISliderComponent>();
            for (entt::entity entity : sliderView)
            {
                auto& slider = sliderView.get<UISliderComponent>(entity);
                slider.RuntimeValueChangedThisFrame = false;
            }

            uint32_t interactionViewportWidthPixels = windowWidth;
            uint32_t interactionViewportHeightPixels = windowHeight;
            glm::vec2 interactionMousePixels = glm::vec2(0.0f);

            const InputSystem& input = GetInputSystem();
            interactionMousePixels = input.GetMousePosition();

            if (g_UiInputViewportRect.Enabled)
            {
                interactionViewportWidthPixels = static_cast<uint32_t>(std::max(0.0f, g_UiInputViewportRect.SizePixels.x));
                interactionViewportHeightPixels = static_cast<uint32_t>(std::max(0.0f, g_UiInputViewportRect.SizePixels.y));
                interactionMousePixels -= g_UiInputViewportRect.MinPixels;
            }

            if (interactionViewportWidthPixels == 0 || interactionViewportHeightPixels == 0)
            {
                for (entt::entity entity : buttonView)
                {
                    auto& button = buttonView.get<UIButtonComponent>(entity);
                    button.IsHovered = false;
                    button.IsPressed = false;
                }
                for (entt::entity entity : sliderView)
                    sliderView.get<UISliderComponent>(entity).RuntimeDragging = false;
                return;
            }

            auto canvasView = registry.view<CanvasComponent>();
            std::vector<entt::entity> canvases;
            for (entt::entity entity : canvasView)
            {
                if (scene.IsEntityEnabledInHierarchy(entity))
                    canvases.push_back(entity);
            }
            std::sort(canvases.begin(), canvases.end(), [&registry](entt::entity left, entt::entity right) {
                const auto& leftCanvas = registry.get<CanvasComponent>(left);
                const auto& rightCanvas = registry.get<CanvasComponent>(right);
                if (leftCanvas.SortOrder != rightCanvas.SortOrder)
                    return leftCanvas.SortOrder < rightCanvas.SortOrder;
                return static_cast<uint32_t>(left) < static_cast<uint32_t>(right);
            });

            struct UiInteractiveLayout
            {
                entt::entity CanvasEntity = entt::null;
                UiLayoutRect Rect;
                int32_t CanvasSortOrder = 0;
                float Z = 0.0f;
                int32_t SiblingOrder = 0;
            };
            struct UiHoveredCandidate
            {
                entt::entity OwnerEntity = entt::null;
                int32_t CanvasSortOrder = 0;
                float Z = 0.0f;
                int32_t SiblingOrder = 0;
            };

            std::unordered_map<entt::entity, UiInteractiveLayout> interactiveLayouts;
            interactiveLayouts.reserve(128);
            std::vector<UiHoveredCandidate> hoveredCandidates;
            hoveredCandidates.reserve(64);

            auto rectView = registry.view<RectTransformComponent>();
            for (entt::entity canvasEntity : canvases)
            {
                const auto& canvas = registry.get<CanvasComponent>(canvasEntity);
                if (canvas.Mode != CanvasComponent::RenderMode::ScreenSpace)
                    continue;

                const glm::vec2 mouseInCanvasSpace = ConvertMousePixelsToCanvasSpace(
                    interactionMousePixels,
                    canvas,
                    interactionViewportWidthPixels,
                    interactionViewportHeightPixels);
                const UiCanvasScreenSpaceMetrics canvasMetrics = BuildScreenSpaceCanvasMetrics(canvas, interactionViewportWidthPixels, interactionViewportHeightPixels);
                const UiLayoutRect canvasRootRect = canvasMetrics.ProjectionRect;
                std::unordered_map<entt::entity, UiLayoutRect> layoutCache;

                for (entt::entity entity : rectView)
                {
                    if (!scene.IsEntityEnabledInHierarchy(entity))
                        continue;

                    entt::entity owningCanvas = entt::null;
                    if (!TryGetOwningCanvasEntity(registry, entity, owningCanvas) || owningCanvas != canvasEntity)
                        continue;

                    const entt::entity interactiveOwner = FindInteractiveOwnerEntity(registry, entity);
                    if (interactiveOwner == entt::null || !registry.all_of<RectTransformComponent>(interactiveOwner))
                        continue;

                    if (!interactiveLayouts.contains(interactiveOwner))
                    {
                        UiInteractiveLayout ownerLayout;
                        ownerLayout.CanvasEntity = canvasEntity;
                        ownerLayout.Rect = ApplyTransformToUiRect(
                            registry,
                            interactiveOwner,
                            ResolveUiLayoutRect(registry, interactiveOwner, canvasEntity, canvasRootRect, layoutCache));
                        ownerLayout.CanvasSortOrder = canvas.SortOrder;
                        if (const auto* transform = registry.try_get<TransformComponent>(interactiveOwner))
                            ownerLayout.Z = transform->Position.z;
                        if (const auto* hierarchy = registry.try_get<HierarchyComponent>(interactiveOwner))
                            ownerLayout.SiblingOrder = hierarchy->SiblingOrder;
                        interactiveLayouts[interactiveOwner] = ownerLayout;
                    }

                    const UiLayoutRect hitRect = ApplyTransformToUiRect(
                        registry,
                        entity,
                        ResolveUiLayoutRect(registry, entity, canvasEntity, canvasRootRect, layoutCache));
                    if (IsPointInsideRect(mouseInCanvasSpace, hitRect))
                    {
                        UiHoveredCandidate candidate{};
                        candidate.OwnerEntity = interactiveOwner;
                        candidate.CanvasSortOrder = canvas.SortOrder;
                        if (const auto* transform = registry.try_get<TransformComponent>(entity))
                            candidate.Z = transform->Position.z;
                        if (const auto* hierarchy = registry.try_get<HierarchyComponent>(entity))
                            candidate.SiblingOrder = hierarchy->SiblingOrder;
                        hoveredCandidates.push_back(candidate);
                    }
                }
            }

            auto isCandidateOnTop = [](const UiHoveredCandidate& left, const UiHoveredCandidate& right) {
                if (left.CanvasSortOrder != right.CanvasSortOrder)
                    return left.CanvasSortOrder > right.CanvasSortOrder;
                if (left.Z != right.Z)
                    return left.Z > right.Z;
                if (left.SiblingOrder != right.SiblingOrder)
                    return left.SiblingOrder > right.SiblingOrder;
                return static_cast<uint32_t>(left.OwnerEntity) > static_cast<uint32_t>(right.OwnerEntity);
            };

            entt::entity topHoveredOwnerEntity = entt::null;
            UiHoveredCandidate topHoveredCandidate{};
            bool hasTopHoveredCandidate = false;
            for (const UiHoveredCandidate& candidate : hoveredCandidates)
            {
                if (!hasTopHoveredCandidate || isCandidateOnTop(candidate, topHoveredCandidate))
                {
                    topHoveredCandidate = candidate;
                    topHoveredOwnerEntity = candidate.OwnerEntity;
                    hasTopHoveredCandidate = true;
                }
            }
            scene.SetUiPointerOverInteractiveElement(topHoveredOwnerEntity != entt::null);

            const bool mouseDown = input.IsMouseButtonDown(SDL_BUTTON_LEFT);
            const bool mousePressedThisFrame = input.WasMouseButtonPressedThisFrame(SDL_BUTTON_LEFT);
            const bool mouseReleasedThisFrame = input.WasMouseButtonReleasedThisFrame(SDL_BUTTON_LEFT);

            for (entt::entity entity : buttonView)
            {
                auto& button = buttonView.get<UIButtonComponent>(entity);
                const bool hovered = button.Interactable && entity == topHoveredOwnerEntity;
                const bool wasHovered = button.IsHovered;
                const bool wasPressed = button.IsPressed;

                button.IsHovered = hovered;
                button.RuntimeHoverEnteredThisFrame = hovered && !wasHovered;
                button.RuntimeHoverExitedThisFrame = !hovered && wasHovered;

                if (!button.Interactable)
                {
                    button.IsPressed = false;
                    continue;
                }

                if (hovered && mousePressedThisFrame)
                {
                    button.IsPressed = true;
                    button.RuntimePressedThisFrame = true;
                }
                if (!mouseDown)
                    button.IsPressed = false;
                if (wasPressed && mouseReleasedThisFrame && hovered)
                    button.RuntimeClickedThisFrame = true;
            }

            for (entt::entity entity : sliderView)
            {
                auto& slider = sliderView.get<UISliderComponent>(entity);
                const auto layoutIt = interactiveLayouts.find(entity);

                if (layoutIt == interactiveLayouts.end())
                {
                    slider.RuntimeDragging = false;
                    continue;
                }

                const bool hovered = slider.Interactable && entity == topHoveredOwnerEntity;
                const UiInteractiveLayout& layout = layoutIt->second;
                const auto* canvas = registry.try_get<CanvasComponent>(layout.CanvasEntity);
                if (!canvas)
                {
                    slider.RuntimeDragging = false;
                    continue;
                }

                if (slider.Interactable && hovered && mouseDown && (mousePressedThisFrame || !slider.RuntimeDragging))
                    slider.RuntimeDragging = true;
                if (!slider.Interactable)
                    slider.RuntimeDragging = false;
                if (mouseReleasedThisFrame || !mouseDown)
                    slider.RuntimeDragging = false;

                if (slider.Interactable && slider.RuntimeDragging)
                {
                    const glm::vec2 mouseInCanvasSpace = ConvertMousePixelsToCanvasSpace(
                        interactionMousePixels,
                        *canvas,
                        interactionViewportWidthPixels,
                        interactionViewportHeightPixels);
                    const float width = std::max(1.0f, layout.Rect.Max.x - layout.Rect.Min.x);
                    const float normalized = std::clamp((mouseInCanvasSpace.x - layout.Rect.Min.x) / width, 0.0f, 1.0f);
                    const float minValue = slider.MinValue;
                    const float maxValue = std::max(slider.MinValue, slider.MaxValue);
                    const float updatedValue = minValue + (maxValue - minValue) * normalized;
                    if (std::abs(updatedValue - slider.Value) > 0.0001f)
                    {
                        slider.Value = updatedValue;
                        slider.RuntimeValueChangedThisFrame = true;
                    }
                }

                SyncSliderVisualChildren(registry, entity, slider);
            }
        }

        void RenderCanvasUiPass(Scene& scene, const Camera& camera, uint32_t width, uint32_t height)
        {
            if (width == 0 || height == 0)
                return;

            auto& registry = scene.GetRegistry();

            auto canvasView = registry.view<CanvasComponent>();
            std::vector<entt::entity> canvases;
            for (entt::entity entity : canvasView)
            {
                if (scene.IsEntityEnabledInHierarchy(entity))
                    canvases.push_back(entity);
            }

            std::sort(canvases.begin(), canvases.end(), [&registry](entt::entity left, entt::entity right) {
                const auto& leftCanvas = registry.get<CanvasComponent>(left);
                const auto& rightCanvas = registry.get<CanvasComponent>(right);
                if (leftCanvas.SortOrder != rightCanvas.SortOrder)
                    return leftCanvas.SortOrder < rightCanvas.SortOrder;
                return static_cast<uint32_t>(left) < static_cast<uint32_t>(right);
            });

            for (entt::entity canvasEntity : canvases)
            {
                const auto& canvas = registry.get<CanvasComponent>(canvasEntity);
                UiLayoutRect canvasRect = GetCanvasRootRect(canvas, width, height);
                std::unordered_map<entt::entity, UiLayoutRect> layoutCache;

                if (canvas.Mode == CanvasComponent::RenderMode::ScreenSpace)
                {
                    const UiCanvasScreenSpaceMetrics metrics = BuildScreenSpaceCanvasMetrics(canvas, width, height);
                    canvasRect = metrics.ProjectionRect;
                    const glm::mat4 screenProjection = glm::ortho(
                        metrics.ProjectionRect.Min.x, metrics.ProjectionRect.Max.x,
                        metrics.ProjectionRect.Min.y, metrics.ProjectionRect.Max.y,
                        -10000.0f, 10000.0f);
                    Renderer2D::BeginScene(screenProjection, false);
                }
                else
                {
                    Renderer2D::BeginScene(camera);
                }

                std::vector<entt::entity> uiEntities;
                auto rectView = registry.view<RectTransformComponent>();
                for (entt::entity entity : rectView)
                {
                    if (!scene.IsEntityEnabledInHierarchy(entity))
                        continue;
                    if (!registry.any_of<SpriteComponent, UITextComponent, UIPanelComponent>(entity))
                        continue;

                    entt::entity owningCanvas = entt::null;
                    if (TryGetOwningCanvasEntity(registry, entity, owningCanvas) && owningCanvas == canvasEntity)
                        uiEntities.push_back(entity);
                }

                std::sort(uiEntities.begin(), uiEntities.end(), [&registry](entt::entity left, entt::entity right) {
                    const auto* leftTransform = registry.try_get<TransformComponent>(left);
                    const auto* rightTransform = registry.try_get<TransformComponent>(right);
                    const float leftZ = leftTransform ? leftTransform->Position.z : 0.0f;
                    const float rightZ = rightTransform ? rightTransform->Position.z : 0.0f;
                    if (leftZ != rightZ)
                        return leftZ < rightZ;

                    const auto* leftHierarchy = registry.try_get<HierarchyComponent>(left);
                    const auto* rightHierarchy = registry.try_get<HierarchyComponent>(right);
                    const int32_t leftOrder = leftHierarchy ? leftHierarchy->SiblingOrder : 0;
                    const int32_t rightOrder = rightHierarchy ? rightHierarchy->SiblingOrder : 0;
                    if (leftOrder != rightOrder)
                        return leftOrder < rightOrder;

                    return static_cast<uint32_t>(left) < static_cast<uint32_t>(right);
                });

                const glm::mat4 canvasWorldTransform = (canvas.Mode == CanvasComponent::RenderMode::WorldSpace)
                    ? scene.GetWorldTransformMatrix(canvasEntity)
                    : glm::mat4(1.0f);

                for (entt::entity uiEntity : uiEntities)
                {
                    const UiLayoutRect layoutRect = ResolveUiLayoutRect(registry, uiEntity, canvasEntity, canvasRect, layoutCache);
                    const glm::vec2 layoutSize = layoutRect.Max - layoutRect.Min;
                    if (layoutSize.x <= 0.0f || layoutSize.y <= 0.0f)
                        continue;

                    const auto& transform = registry.get<TransformComponent>(uiEntity);
                    glm::vec2 center = (layoutRect.Min + layoutRect.Max) * 0.5f;
                    center += glm::vec2(transform.Position.x, transform.Position.y);

                    const glm::vec2 scaledSize = glm::vec2(
                        layoutSize.x * std::max(0.001f, transform.Scale.x),
                        layoutSize.y * std::max(0.001f, transform.Scale.y));
                    glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(center.x, center.y, transform.Position.z));
                    model = glm::rotate(model, glm::radians(transform.Rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
                    model = glm::scale(model, glm::vec3(scaledSize, 1.0f));

                    if (canvas.Mode == CanvasComponent::RenderMode::WorldSpace)
                        model = canvasWorldTransform * model;

                    SpriteComponent* sprite = registry.try_get<SpriteComponent>(uiEntity);
                    UIPanelComponent* panel = registry.try_get<UIPanelComponent>(uiEntity);
                    UIButtonComponent* button = registry.try_get<UIButtonComponent>(uiEntity);
                    UISliderComponent* slider = registry.try_get<UISliderComponent>(uiEntity);
                    const entt::entity sliderBackgroundVisualEntity = slider ? FindDirectChildByTag(registry, uiEntity, "Slider Background") : entt::null;
                    const entt::entity sliderFillVisualEntity = slider ? FindDirectChildByTag(registry, uiEntity, "Slider Fill") : entt::null;
                    const entt::entity sliderHandleVisualEntity = slider ? FindDirectChildByTag(registry, uiEntity, "Slider Handle") : entt::null;
                    const bool sliderHasVisualChildren = slider &&
                        (sliderBackgroundVisualEntity != entt::null ||
                         sliderFillVisualEntity != entt::null ||
                         sliderHandleVisualEntity != entt::null);

                    if (slider && sliderHasVisualChildren)
                        SyncSliderVisualChildren(registry, uiEntity, *slider);

                    if (slider && !sliderHasVisualChildren)
                    {
                        const float valueRange = std::max(0.0f, slider->MaxValue - slider->MinValue);
                        const float normalizedValue = (valueRange > 0.0001f)
                            ? std::clamp((slider->Value - slider->MinValue) / valueRange, 0.0f, 1.0f)
                            : 0.0f;

                        glm::vec4 backgroundColor = slider->BackgroundColor;
                        glm::vec4 fillColor = slider->FillColor;
                        glm::vec4 handleColor = slider->HandleColor;
                        if (!slider->Interactable)
                        {
                            backgroundColor *= glm::vec4(0.65f, 0.65f, 0.65f, 1.0f);
                            fillColor *= glm::vec4(0.65f, 0.65f, 0.65f, 1.0f);
                            handleColor *= glm::vec4(0.65f, 0.65f, 0.65f, 1.0f);
                        }

                        if (sprite)
                        {
                            Assets::TextureAsset::Ptr textureAsset = ResolveUiSpriteTexture(*sprite);
                            if (textureAsset)
                                Renderer2D::DrawQuad(model, textureAsset, backgroundColor);
                            else if (!sprite->TextureKey.empty())
                                Renderer2D::DrawQuad(model, glm::vec4(1.0f, 0.0f, 1.0f, backgroundColor.a));
                            else
                                Renderer2D::DrawQuad(model, backgroundColor);
                        }
                        else
                        {
                            Renderer2D::DrawQuad(model, backgroundColor);
                        }

                        if (normalizedValue > 0.0f)
                        {
                            glm::mat4 fillModel = glm::translate(model, glm::vec3((normalizedValue - 1.0f) * 0.5f, 0.0f, 0.0f));
                            fillModel = glm::scale(fillModel, glm::vec3(normalizedValue, 1.0f, 1.0f));
                            Renderer2D::DrawQuad(fillModel, fillColor);
                        }

                        if (slider->ShowHandle)
                        {
                            const float clampedHandleWidth = std::clamp(slider->HandleWidth, 1.0f, std::max(1.0f, scaledSize.x));
                            const float handleWidthScale = clampedHandleWidth / std::max(1.0f, scaledSize.x);
                            const float handleHeightScale = std::max(0.1f, slider->HandleHeightMultiplier);
                            glm::mat4 handleModel = glm::translate(model, glm::vec3(normalizedValue - 0.5f, 0.0f, 0.0f));
                            handleModel = glm::scale(handleModel, glm::vec3(handleWidthScale, handleHeightScale, 1.0f));
                            Renderer2D::DrawQuad(handleModel, handleColor);
                        }
                    }
                    else if (sprite && !panel && !sliderHasVisualChildren)
                    {
                        glm::vec4 resolvedColor = sprite->Color;
                        if (button && button->UseStateColors)
                        {
                            if (!button->Interactable)
                                resolvedColor = button->DisabledColor;
                            else if (button->IsPressed)
                                resolvedColor = button->PressedColor;
                            else if (button->IsHovered)
                                resolvedColor = button->HoveredColor;
                            else
                                resolvedColor = button->NormalColor;
                        }

                        Assets::TextureAsset::Ptr textureAsset = ResolveUiSpriteTexture(*sprite);
                        if (textureAsset)
                            Renderer2D::DrawQuad(model, textureAsset, resolvedColor);
                        else if (!sprite->TextureKey.empty())
                            Renderer2D::DrawQuad(model, glm::vec4(1.0f, 0.0f, 1.0f, resolvedColor.a));
                        else
                            Renderer2D::DrawQuad(model, resolvedColor);
                    }
                    else if (panel && !sliderHasVisualChildren)
                    {
                        glm::vec4 resolvedPanelColor = panel->BackgroundColor;
                        if (button && button->UseStateColors)
                        {
                            if (!button->Interactable)
                                resolvedPanelColor = button->DisabledColor;
                            else if (button->IsPressed)
                                resolvedPanelColor = button->PressedColor;
                            else if (button->IsHovered)
                                resolvedPanelColor = button->HoveredColor;
                            else
                                resolvedPanelColor = button->NormalColor;
                        }

                        if (panel->UseSpriteTexture && sprite)
                        {
                            Assets::TextureAsset::Ptr textureAsset = ResolveUiSpriteTexture(*sprite);
                            if (textureAsset)
                                Renderer2D::DrawQuad(model, textureAsset, resolvedPanelColor);
                            else if (!sprite->TextureKey.empty())
                                Renderer2D::DrawQuad(model, glm::vec4(1.0f, 0.0f, 1.0f, resolvedPanelColor.a));
                            else
                                Renderer2D::DrawQuad(model, resolvedPanelColor);
                        }
                        else
                        {
                            Renderer2D::DrawQuad(model, resolvedPanelColor);
                        }
                    }
                    else if (!sliderHasVisualChildren && button && button->UseStateColors)
                    {
                        glm::vec4 resolvedColor = button->NormalColor;
                        if (!button->Interactable)
                            resolvedColor = button->DisabledColor;
                        else if (button->IsPressed)
                            resolvedColor = button->PressedColor;
                        else if (button->IsHovered)
                            resolvedColor = button->HoveredColor;
                        Renderer2D::DrawQuad(model, resolvedColor);
                    }

                    if (auto* text = registry.try_get<UITextComponent>(uiEntity))
                    {
                        if (text->Text.empty() || text->FontFilePath.empty())
                            continue;

                        if (!text->CachedFont && !text->FontLoadAttempted)
                        {
                            text->CachedFont = Font::CreateFromFile(text->FontFilePath);
                            text->FontLoadAttempted = true;
                            if (!text->CachedFont)
                                LT_CORE_WARN("SceneRenderer: failed to load text font '{}'", text->FontFilePath);
                        }

                        if (!text->CachedFont)
                            continue;

                        Renderer2D::DrawText(model, text->Text, text->CachedFont, text->FontSize, text->Color);
                    }
                }

                Renderer2D::EndScene();
            }

        }
    }

    void SceneRenderer::RenderToViewport(Scene& scene, const Camera& camera,
        const std::shared_ptr<Framebuffer>& framebuffer, uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0)
            return;

        auto& renderer = Renderer::GetInstance();
        if (!renderer.IsInitialized())
            return;

        const bool renderedWithLighting = Lighting2DRenderer::RenderToViewport(scene, camera, framebuffer, width, height, [&scene, &camera]() {
            SceneRenderer::Render(scene, camera);
        });

        if (!renderedWithLighting)
        {
            // Null framebuffer means render directly to the default backbuffer.
            renderer.SubmitCommand(std::make_unique<BindFramebufferCommand>(framebuffer));
            renderer.SubmitCommand(std::make_unique<SetViewportCommand>(0, 0, static_cast<int>(width), static_cast<int>(height)));
            const glm::vec4 fallbackClearColor = SceneRenderer::GetViewportClearColor();

            ClearCommand::ClearFlags clearFlags;
            clearFlags.color = true;
            clearFlags.depth = true;
            clearFlags.stencil = false;
            renderer.SubmitCommand(std::make_unique<ClearCommand>(
                clearFlags,
                fallbackClearColor.r,
                fallbackClearColor.g,
                fallbackClearColor.b,
                fallbackClearColor.a));

            Render(scene, camera);
        }
        RenderCanvasUiPass(scene, camera, width, height);

        renderer.SubmitCommand(std::make_unique<BindFramebufferCommand>(nullptr));
    }
}
