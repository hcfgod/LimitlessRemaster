#include "Scene/SceneSerialization.h"
#include "Scene/Scene.h"

#include "Assets/AssetBundle.h"
#include "Core/Error.h"
#include "Physics/Physics2DWorld.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetPaths.h"
#include "Scripting/Coroutine.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <exception>
#include <fstream>
#include <limits>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Limitless::SceneSerialization
{
    namespace
    {
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
    }

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
            Prefab prefabReference{};
            if (root.contains("Value"))
            {
                const auto& value = root["Value"];
                if (value.is_object())
                {
                    prefabReference.AssetKey = value.value("AssetKey", std::string{});
                    if (prefabReference.AssetKey.empty())
                        prefabReference.AssetKey = value.value("PrefabAssetKey", std::string{});
                }
                else if (value.is_string())
                    prefabReference.AssetKey = value.get<std::string>();
            }
            outValue = std::move(prefabReference);
            return true;
        }

        return false;
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
}

// -----------------------------------------------------------------------------
// Scene::SaveToFile and Scene::LoadFromFile implementations
// -----------------------------------------------------------------------------

namespace Limitless
{
    namespace
    {
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

        Result<nlohmann::json> TryReadSceneJson(const std::filesystem::path& path)
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
                        return Result<nlohmann::json>(ErrorCode::FileNotFound, "Scene::LoadFromFile failed opening scene file");

                    input >> root;
                }
                else
                {
                    root = nlohmann::json::parse(sceneText);
                }
            }
            catch (const std::exception& exception)
            {
                return Result<nlohmann::json>(ErrorCode::FileCorrupted, std::string("Scene::LoadFromFile JSON parse failed: ") + exception.what());
            }

            if (!root.is_object() || !root.contains("Entities") || !root["Entities"].is_array())
                return Result<nlohmann::json>(ErrorCode::FileCorrupted, "Scene::LoadFromFile invalid scene JSON format");

            return Result<nlohmann::json>(std::move(root));
        }

        void ApplySceneMetadataFromJson(const nlohmann::json& root, Scene* scene)
        {
            if (root.contains("Physics2DSettings") && root["Physics2DSettings"].is_object())
            {
                const auto& physicsSettingsJson = root["Physics2DSettings"];
                Physics2DWorldSettings settings{};
                auto gravity = physicsSettingsJson.value("Gravity", std::vector<float>{ 0.0f, -9.81f });
                if (gravity.size() >= 2)
                    settings.Gravity = glm::vec2(gravity[0], gravity[1]);
                settings.VelocitySubSteps = std::max(1, physicsSettingsJson.value("VelocitySubSteps", 4));
                settings.EnableSleep = physicsSettingsJson.value("EnableSleep", true);
                settings.EnableContinuousCollision = physicsSettingsJson.value("EnableContinuousCollision", true);
                settings.HighContactQualityMode = physicsSettingsJson.value("HighContactQualityMode", false);
                settings.HighContactQualityExtraSubSteps = std::max(0, physicsSettingsJson.value("HighContactQualityExtraSubSteps", 4));
                settings.ContactHertz = physicsSettingsJson.value("ContactHertz", 90.0f);
                settings.ContactDampingRatio = physicsSettingsJson.value("ContactDampingRatio", 1.0f);
                settings.ContactPushSpeed = physicsSettingsJson.value("ContactPushSpeed", 8.0f);
                scene->SetPhysics2DSettings(settings);
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
        }

        struct DeserializedEntityInfo
        {
            entt::entity Entity = entt::null;
            int32_t ParentIndex = -1;
            int32_t SiblingOrder = 0;
            int32_t JointConnectedEntityIndex = -1;
        };

        void DeserializeComponentsFromJson(const nlohmann::json& entry, Scene* scene, entt::entity entity,
            int32_t& outJointConnectedEntityIndex)
        {
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
                if (spriteJson.contains("Texture"))
                    sprite.TextureKey = SceneSerialization::ResolveAssetKeyFromSceneJson(spriteJson["Texture"]);
                else
                    sprite.TextureKey = spriteJson.value("TextureKey", "");
                sprite.TextureLoadAttempted = false;
                auto color = spriteJson.value("Color", std::vector<float>{ 1.0f, 1.0f, 1.0f, 1.0f });
                if (color.size() >= 4)
                    sprite.Color = glm::vec4(color[0], color[1], color[2], color[3]);

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
                    animator.ControllerKey = SceneSerialization::ResolveAssetKeyFromSceneJson(animatorJson["Controller"]);
                else
                    animator.ControllerKey = SceneSerialization::ResolveAssetKeyFromSceneJson(animatorJson.value("ControllerKey", std::string{}));

                if (animatorJson.contains("DefaultClip"))
                    animator.DefaultClipKey = SceneSerialization::ResolveAssetKeyFromSceneJson(animatorJson["DefaultClip"]);
                else
                    animator.DefaultClipKey = SceneSerialization::ResolveAssetKeyFromSceneJson(animatorJson.value("DefaultClipKey", std::string{}));

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
                if (materialJson.is_object() && materialJson.contains("MaterialKey"))
                    material.MaterialKey = SceneSerialization::ResolveAssetKeyFromSceneJson(materialJson.value("MaterialKey", std::string{}));
                else
                    material.MaterialKey = SceneSerialization::ResolveAssetKeyFromSceneJson(materialJson);
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
                        layer.TileTable.push_back(SceneSerialization::ResolveAssetKeyFromSceneJson(tileRef));
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
                    audioSource.AudioClipKey = SceneSerialization::ResolveAssetKeyFromSceneJson(audioSourceJson["AudioClip"]);
                else if (audioSourceJson.is_object())
                    audioSource.AudioClipKey = SceneSerialization::ResolveAssetKeyFromSceneJson(audioSourceJson.value("AudioClipKey", std::string{}));

                if (audioSourceJson.is_object())
                {
                    audioSource.Volume = audioSourceJson.value("Volume", 1.0f);
                    if (audioSource.Volume < 0.0f)
                        audioSource.Volume = 0.0f;
                    audioSource.Pitch = std::max(0.01f, audioSourceJson.value("Pitch", 1.0f));
                    audioSource.PlayOnStart = audioSourceJson.value("PlayOnStart", true);
                    audioSource.Loop = audioSourceJson.value("Loop", false);
                    audioSource.Muted = audioSourceJson.value("Muted", false);
                    audioSource.Space = SceneSerialization::ParseAudioPlaybackSpaceName(audioSourceJson.value("PlaybackSpace", std::string("Global")));
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
                outJointConnectedEntityIndex = joint2DJson.value("ConnectedEntityIndex", -1);
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
                        if (SceneSerialization::DeserializeScriptPropertyValue(it.value(), propertyValue))
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
                    pe.TextureKey = SceneSerialization::ResolveAssetKeyFromSceneJson(peJson["Texture"]);

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
                    prefabInstance.PrefabAssetKey = SceneSerialization::ResolveAssetKeyFromSceneJson(prefabJson["Prefab"]);
            }
        }

        DeserializedEntityInfo DeserializeEntityFromJson(const nlohmann::json& entry, Scene* scene)
        {
            DeserializedEntityInfo info;
            const std::string tag = entry.value("Tag", "Entity");
            info.Entity = scene->CreateEntity(tag);
            if (auto* tagComponent = scene->GetRegistry().try_get<TagComponent>(info.Entity))
                tagComponent->Enabled = entry.value("EntityEnabled", true);
            auto& transform = scene->GetRegistry().get<TransformComponent>(info.Entity);

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

            DeserializeComponentsFromJson(entry, scene, info.Entity, info.JointConnectedEntityIndex);

            if (entry.contains("Hierarchy"))
            {
                const auto& hierarchyJson = entry["Hierarchy"];
                info.ParentIndex = hierarchyJson.value("ParentIndex", -1);
                info.SiblingOrder = hierarchyJson.value("SiblingOrder", 0);
            }

            ResetRuntimeStateForEntity(scene->GetRegistry(), info.Entity);
            return info;
        }

        void RestoreHierarchyAndJointReferences(entt::registry& registry,
            const std::vector<entt::entity>& createdEntities,
            const std::vector<int32_t>& parentIndices,
            const std::vector<int32_t>& siblingOrders,
            const std::vector<int32_t>& jointConnectedEntityIndices)
        {
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
        }

        void RunPostLoadSliderMigration(Scene* scene)
        {
            auto& registry = scene->GetRegistry();
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
        }
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
                    { "Texture", SceneSerialization::MakeAssetReferenceJson(sprite->TextureKey, Assets::AssetType::Texture2D) },
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
                    { "Controller", SceneSerialization::MakeAssetReferenceJson(animator->ControllerKey, Assets::AssetType::AnimatorController) },
                    { "DefaultClip", SceneSerialization::MakeAssetReferenceJson(animator->DefaultClipKey, Assets::AssetType::AnimationClip) },
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
                entry["Material"] = SceneSerialization::MakeAssetReferenceJson(material->MaterialKey, Assets::AssetType::Material);
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
                    tileTableJson.push_back(SceneSerialization::MakeAssetReferenceJson(key, Assets::AssetType::Tile));

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
                    { "AudioClip", SceneSerialization::MakeAssetReferenceJson(audioSource->AudioClipKey, Assets::AssetType::AudioClip) },
                    { "Volume", audioSource->Volume },
                    { "Pitch", audioSource->Pitch },
                    { "PlayOnStart", audioSource->PlayOnStart },
                    { "Loop", audioSource->Loop },
                    { "Muted", audioSource->Muted },
                    { "PlaybackSpace", SceneSerialization::ToAudioPlaybackSpaceName(audioSource->Space) },
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
                        exposedProperties[propertyName] = SceneSerialization::SerializeScriptPropertyValue(propertyValue);
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
                    { "Texture", SceneSerialization::MakeAssetReferenceJson(particleEmitter->TextureKey, Assets::AssetType::Texture2D) },
                    { "MaxParticles", particleEmitter->MaxParticles }
                };
            }

            if (const auto* prefabInstance = m_Registry.try_get<PrefabInstanceComponent>(entity))
            {
                entry["PrefabInstance"] = {
                    { "Prefab", SceneSerialization::MakeAssetReferenceJson(prefabInstance->PrefabAssetKey, Assets::AssetType::Prefab) }
                };
            }

            root["Entities"].push_back(std::move(entry));
        }

        const std::filesystem::path tempPath = path.string() + ".tmp";
        {
            std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
            if (!output.is_open())
                return Result<void>(ErrorCode::FileAccessDenied, "Scene::SaveToFile failed opening temp destination file");

            output << root.dump(2);
            output.flush();
            if (!output.good())
            {
                std::error_code cleanupError;
                std::filesystem::remove(tempPath, cleanupError);
                return Result<void>(ErrorCode::FileAccessDenied, "Scene::SaveToFile failed writing scene data");
            }
        }

        std::error_code renameError;
        std::filesystem::rename(tempPath, path, renameError);
        if (renameError)
        {
            std::error_code copyError;
            std::filesystem::copy_file(tempPath, path, std::filesystem::copy_options::overwrite_existing, copyError);
            if (copyError)
            {
                std::error_code cleanupError;
                std::filesystem::remove(tempPath, cleanupError);
                return Result<void>(
                    ErrorCode::FileAccessDenied,
                    "Scene::SaveToFile failed replacing destination file. Rename failed: " + renameError.message() +
                    "; fallback copy failed: " + copyError.message());
            }

            std::error_code cleanupError;
            std::filesystem::remove(tempPath, cleanupError);
        }

        return Result<void>();
    }

    Result<std::unique_ptr<Scene>> Scene::LoadFromFile(const std::filesystem::path& path)
    {
        auto jsonResult = TryReadSceneJson(path);
        if (jsonResult.IsFailure())
            return Result<std::unique_ptr<Scene>>(jsonResult.GetError());
        nlohmann::json root = std::move(jsonResult.GetValue());

        auto scene = std::make_unique<Scene>();
        ApplySceneMetadataFromJson(root, scene.get());

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
            DeserializedEntityInfo info = DeserializeEntityFromJson(entry, scene.get());
            createdEntities.push_back(info.Entity);
            parentIndices.push_back(info.ParentIndex);
            siblingOrders.push_back(info.SiblingOrder);
            jointConnectedEntityIndices.push_back(info.JointConnectedEntityIndex);
        }

        RestoreHierarchyAndJointReferences(scene->GetRegistry(), createdEntities, parentIndices, siblingOrders, jointConnectedEntityIndices);
        RunPostLoadSliderMigration(scene.get());

        return Result<std::unique_ptr<Scene>>(std::move(scene));
    }
}