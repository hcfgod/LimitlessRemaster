#include "Scene/Scene.h"
#include "Scene/Components/PhysicsComponents.h"
#include "Scene/Components/RenderingComponents.h"
#include "Scene/Components/TilemapComponents.h"
#include "Scene/SceneSerialization.h"

#include "Assets/AssetBundle.h"
#include "Core/Error.h"
#include "Physics/Physics2DWorld.h"
#include "Scripting/Coroutine.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <exception>
#include <fstream>
#include <limits>
#include <string_view>
#include <vector>

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
                animator.RuntimeAppliedPositionOffset = glm::vec3(0.0f);
                animator.RuntimeAppliedScaleOffset = glm::vec3(0.0f);
                animator.RuntimeAppliedRotationOffset = glm::vec3(0.0f);
            }
            animator.RuntimeHasPosition = false;
            animator.RuntimeHasScale = false;
            animator.RuntimeHasRotation = false;
            animator.RuntimePosition = glm::vec3(0.0f);
            animator.RuntimeScale = glm::vec3(0.0f);
            animator.RuntimeRotation = glm::vec3(0.0f);
        }

        void ResetRuntimeStateForEntity(Scene& scene, entt::entity entity)
        {
            auto& registry = scene.GetRegistry();
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

            if (auto* audioListener3D = registry.try_get<AudioListener3DComponent>(entity))
            {
                audioListener3D->RuntimeHasPreviousWorldPosition = false;
                audioListener3D->RuntimePreviousWorldPosition = glm::vec3(0.0f);
            }

            if (auto* audioSource = registry.try_get<AudioSourceComponent>(entity))
            {
                audioSource->RuntimeVoiceId = 0;
                audioSource->RuntimePlaybackStarted = false;
                audioSource->RuntimePlayOnStartConsumed = false;
                audioSource->RuntimeHasPreviousWorldPosition = false;
                audioSource->RuntimePreviousWorldPosition = glm::vec3(0.0f);
            }

            if (auto* rigidbody2D = registry.try_get<Rigidbody2DComponent>(entity))
            {
                rigidbody2D->RuntimeBodyId = kNullPhysics2DBody;
                rigidbody2D->RuntimeBodyCreated = false;
                rigidbody2D->RuntimeWorldSlot = 0;
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

            if (auto* polygonCollider2D = registry.try_get<PolygonCollider2DComponent>(entity))
            {
                polygonCollider2D->RuntimeShapeId = kNullPhysics2DShape;
                polygonCollider2D->RuntimeShapeCreated = false;
            }

            if (auto* edgeCollider2D = registry.try_get<EdgeCollider2DComponent>(entity))
            {
                edgeCollider2D->RuntimeShapeId = kNullPhysics2DShape;
                edgeCollider2D->RuntimeShapeCreated = false;
            }

            if (auto* capsuleCollider2D = registry.try_get<CapsuleCollider2DComponent>(entity))
            {
                capsuleCollider2D->RuntimeShapeId = kNullPhysics2DShape;
                capsuleCollider2D->RuntimeShapeCreated = false;
            }

            if (auto* joint2D = registry.try_get<Joint2DComponent>(entity))
            {
                joint2D->RuntimeJointId = kNullPhysics2DJoint;
                joint2D->RuntimeJointCreated = false;
                joint2D->RuntimeWorldSlot = 0;
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

            for (entt::entity scriptEntity : scene.GetScriptComponentEntities(entity))
            {
                auto* scriptComponent = registry.try_get<ScriptComponent>(scriptEntity);
                if (!scriptComponent)
                    continue;
                auto& scriptEntry = scriptComponent->Script;
                scriptEntry.RuntimeInitialized = false;
                if (scriptEntry.RuntimeInstance)
                    Coroutine::StopAll(*scriptEntry.RuntimeInstance);
                scriptEntry.RuntimeInstance.reset();
                scriptEntry.RuntimeUpdateCount = 0;
                scriptEntry.RuntimeWarnedOnUpdateTransformMutation = false;
                scriptEntry.RuntimeWarnedMissingAccessDeclaration = false;
                scriptEntry.RuntimeWarnedAccessMaskMismatch = false;
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
                settings.WorldCount = static_cast<uint16_t>(std::clamp(physicsSettingsJson.value("WorldCount", 1), 1, 16));
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

        void DeserializeLayoutComponents(const nlohmann::json& entry, Scene* scene, entt::entity entity)
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
                auto referenceResolution = canvasJson.value(
                    "ReferenceResolution", std::vector<float>{ canvas.ReferenceResolution.x, canvas.ReferenceResolution.y });
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
                auto anchoredPosition =
                    rectJson.value("AnchoredPosition", std::vector<float>{ rect.AnchoredPosition.x, rect.AnchoredPosition.y });
                if (anchoredPosition.size() >= 2)
                    rect.AnchoredPosition = glm::vec2(anchoredPosition[0], anchoredPosition[1]);
            }
        }

        void DeserializeRenderAndAnimationComponents(const nlohmann::json& entry, Scene* scene, entt::entity entity)
        {
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
        }

        void DeserializeUiComponents(const nlohmann::json& entry, Scene* scene, entt::entity entity)
        {
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
        }

        void DeserializeGridCameraAndAudioComponents(const nlohmann::json& entry, Scene* scene, entt::entity entity)
        {
            if (entry.contains("Grid2D") && entry["Grid2D"].is_object())
            {
                const auto& grid2DJson = entry["Grid2D"];
                auto& grid2D = scene->GetRegistry().emplace<Grid2DComponent>(entity);

                auto cellSize = grid2DJson.value("CellSize", std::vector<float>{ grid2D.CellSize.x, grid2D.CellSize.y });
                if (cellSize.size() >= 2)
                    grid2D.CellSize = glm::vec2(
                        std::max(0.001f, cellSize[0]),
                        std::max(0.001f, cellSize[1]));

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

            if (entry.contains("AudioListener3D") && entry["AudioListener3D"].is_object())
            {
                const auto& audioListener3DJson = entry["AudioListener3D"];
                auto& audioListener3D = scene->GetRegistry().emplace<AudioListener3DComponent>(entity);
                audioListener3D.Enabled = audioListener3DJson.value("Enabled", true);
                audioListener3D.UsePrimaryCameraTransform = audioListener3DJson.value("UsePrimaryCameraTransform", true);
                audioListener3D.RuntimeHasPreviousWorldPosition = false;
                audioListener3D.RuntimePreviousWorldPosition = glm::vec3(0.0f);
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
                    auto parseRolloffMode = [](const std::string& modeName) {
                        if (modeName == "Linear")
                            return AudioSourceComponent::RolloffMode::Linear;
                        if (modeName == "Inverse")
                            return AudioSourceComponent::RolloffMode::Inverse;
                        return AudioSourceComponent::RolloffMode::SmoothStep;
                    };

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
                    audioSource.SpatialRolloffMode = parseRolloffMode(audioSourceJson.value("SpatialRolloffMode", std::string("Linear")));
                    audioSource.StereoPanStrength = std::clamp(audioSourceJson.value("StereoPanStrength", 1.0f), 0.0f, 1.0f);
                    audioSource.DopplerFactor = std::max(0.0f, audioSourceJson.value("DopplerFactor", 1.0f));
                    audioSource.EnableDirectionalAttenuation = audioSourceJson.value("EnableDirectionalAttenuation", false);
                    audioSource.DirectionalInnerAngleDegrees = std::clamp(audioSourceJson.value("DirectionalInnerAngleDegrees", 360.0f), 0.0f, 360.0f);
                    audioSource.DirectionalOuterAngleDegrees = std::clamp(audioSourceJson.value("DirectionalOuterAngleDegrees", 360.0f), 0.0f, 360.0f);
                    audioSource.DirectionalOuterVolume = std::clamp(audioSourceJson.value("DirectionalOuterVolume", 1.0f), 0.0f, 1.0f);
                    audioSource.AttenuationCurveKey = audioSourceJson.value("AttenuationCurveKey", std::string{});
                }

                audioSource.RuntimeVoiceId = 0;
                audioSource.RuntimePlaybackStarted = false;
                audioSource.RuntimePlayOnStartConsumed = false;
                audioSource.RuntimeHasPreviousWorldPosition = false;
                audioSource.RuntimePreviousWorldPosition = glm::vec3(0.0f);
            }
        }

        void DeserializePhysicsComponents(const nlohmann::json& entry, Scene* scene, entt::entity entity, int32_t& outJointConnectedEntityIndex)
        {
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
                rigidbody2D.PhysicsWorldSlot = static_cast<uint16_t>(std::clamp(rigidbody2DJson.value("PhysicsWorldSlot", 0), 0, 15));
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
                rigidbody2D.RuntimeWorldSlot = 0;
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

            if (entry.contains("PolygonCollider2D") && entry["PolygonCollider2D"].is_object())
            {
                const auto& polygonCollider2DJson = entry["PolygonCollider2D"];
                auto& polygonCollider2D = scene->GetRegistry().emplace<PolygonCollider2DComponent>(entity);
                auto offset = polygonCollider2DJson.value("Offset", std::vector<float>{ 0.0f, 0.0f });
                if (offset.size() >= 2)
                    polygonCollider2D.Offset = glm::vec2(offset[0], offset[1]);
                polygonCollider2D.Points.clear();
                if (polygonCollider2DJson.contains("Points") && polygonCollider2DJson["Points"].is_array())
                {
                    for (const auto& pointJson : polygonCollider2DJson["Points"])
                    {
                        if (!pointJson.is_array() || pointJson.size() < 2)
                            continue;
                        polygonCollider2D.Points.emplace_back(pointJson[0].get<float>(), pointJson[1].get<float>());
                    }
                }
                if (polygonCollider2D.Points.empty())
                {
                    polygonCollider2D.Points = {
                        glm::vec2(-0.5f, -0.5f),
                        glm::vec2(0.5f, -0.5f),
                        glm::vec2(0.5f, 0.5f),
                        glm::vec2(-0.5f, 0.5f)
                    };
                }
                polygonCollider2D.Density = polygonCollider2DJson.value("Density", 1.0f);
                polygonCollider2D.Friction = polygonCollider2DJson.value("Friction", 0.5f);
                polygonCollider2D.Restitution = polygonCollider2DJson.value("Restitution", 0.0f);
                polygonCollider2D.IsSensor = polygonCollider2DJson.value("IsSensor", false);
                polygonCollider2D.CollisionLayer = polygonCollider2DJson.value("CollisionLayer", 1ull);
                polygonCollider2D.CollisionMask = polygonCollider2DJson.value("CollisionMask", ~0ull);
                polygonCollider2D.RuntimeShapeId = kNullPhysics2DShape;
                polygonCollider2D.RuntimeShapeCreated = false;
            }

            if (entry.contains("EdgeCollider2D") && entry["EdgeCollider2D"].is_object())
            {
                const auto& edgeCollider2DJson = entry["EdgeCollider2D"];
                auto& edgeCollider2D = scene->GetRegistry().emplace<EdgeCollider2DComponent>(entity);
                auto offset = edgeCollider2DJson.value("Offset", std::vector<float>{ 0.0f, 0.0f });
                if (offset.size() >= 2)
                    edgeCollider2D.Offset = glm::vec2(offset[0], offset[1]);
                auto pointA = edgeCollider2DJson.value("PointA", std::vector<float>{ -0.5f, 0.0f });
                if (pointA.size() >= 2)
                    edgeCollider2D.PointA = glm::vec2(pointA[0], pointA[1]);
                auto pointB = edgeCollider2DJson.value("PointB", std::vector<float>{ 0.5f, 0.0f });
                if (pointB.size() >= 2)
                    edgeCollider2D.PointB = glm::vec2(pointB[0], pointB[1]);
                edgeCollider2D.Friction = edgeCollider2DJson.value("Friction", 0.5f);
                edgeCollider2D.Restitution = edgeCollider2DJson.value("Restitution", 0.0f);
                edgeCollider2D.IsSensor = edgeCollider2DJson.value("IsSensor", false);
                edgeCollider2D.CollisionLayer = edgeCollider2DJson.value("CollisionLayer", 1ull);
                edgeCollider2D.CollisionMask = edgeCollider2DJson.value("CollisionMask", ~0ull);
                edgeCollider2D.RuntimeShapeId = kNullPhysics2DShape;
                edgeCollider2D.RuntimeShapeCreated = false;
            }

            if (entry.contains("CapsuleCollider2D") && entry["CapsuleCollider2D"].is_object())
            {
                const auto& capsuleCollider2DJson = entry["CapsuleCollider2D"];
                auto& capsuleCollider2D = scene->GetRegistry().emplace<CapsuleCollider2DComponent>(entity);
                auto offset = capsuleCollider2DJson.value("Offset", std::vector<float>{ 0.0f, 0.0f });
                if (offset.size() >= 2)
                    capsuleCollider2D.Offset = glm::vec2(offset[0], offset[1]);
                auto size = capsuleCollider2DJson.value("Size", std::vector<float>{ 1.0f, 2.0f });
                if (size.size() >= 2)
                    capsuleCollider2D.Size = glm::vec2(size[0], size[1]);
                const std::string direction = capsuleCollider2DJson.value("Direction", std::string("Vertical"));
                capsuleCollider2D.Direction = direction == "Horizontal"
                    ? CapsuleCollider2DComponent::Orientation::Horizontal
                    : CapsuleCollider2DComponent::Orientation::Vertical;
                capsuleCollider2D.Density = capsuleCollider2DJson.value("Density", 1.0f);
                capsuleCollider2D.Friction = capsuleCollider2DJson.value("Friction", 0.5f);
                capsuleCollider2D.Restitution = capsuleCollider2DJson.value("Restitution", 0.0f);
                capsuleCollider2D.IsSensor = capsuleCollider2DJson.value("IsSensor", false);
                capsuleCollider2D.CollisionLayer = capsuleCollider2DJson.value("CollisionLayer", 1ull);
                capsuleCollider2D.CollisionMask = capsuleCollider2DJson.value("CollisionMask", ~0ull);
                capsuleCollider2D.RuntimeShapeId = kNullPhysics2DShape;
                capsuleCollider2D.RuntimeShapeCreated = false;
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
                joint2D.RuntimeWorldSlot = 0;
            }
        }

        void LoadNativeScriptEntryFromJson(const nlohmann::json& nativeScriptJson, NativeScriptEntry& outScriptEntry)
        {
            outScriptEntry.ScriptClassName = nativeScriptJson.value("Class", std::string{});
            outScriptEntry.ScriptAssetRelativePath = nativeScriptJson.value("AssetPath", std::string{});
            outScriptEntry.Enabled = nativeScriptJson.value("Enabled", true);
            const std::string executionPolicy = nativeScriptJson.value("ExecutionPolicy", std::string("MainThread"));
            outScriptEntry.ExecutionPolicy = executionPolicy == "ParallelSafe"
                ? ScriptExecutionPolicy::ParallelSafe
                : ScriptExecutionPolicy::MainThread;
            outScriptEntry.DeclaredReadAccessMask = nativeScriptJson.value("DeclaredReadAccessMask", 0ull);
            outScriptEntry.DeclaredWriteAccessMask = nativeScriptJson.value("DeclaredWriteAccessMask", 0ull);
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
            outScriptEntry.RuntimeExposedPropertiesRevision = 1;
            outScriptEntry.RuntimeInstance.reset();
            outScriptEntry.RuntimeUpdateCount = 0;
            outScriptEntry.RuntimeWarnedOnUpdateTransformMutation = false;
            outScriptEntry.RuntimeWarnedMissingAccessDeclaration = false;
            outScriptEntry.RuntimeWarnedAccessMaskMismatch = false;
        }

        void DeserializeScriptAndPrefabComponents(const nlohmann::json& entry, Scene* scene, entt::entity entity)
        {
            if (entry.contains("NativeScripts") && entry["NativeScripts"].is_array())
            {
                for (const auto& scriptEntryJson : entry["NativeScripts"])
                {
                    if (!scriptEntryJson.is_object())
                        continue;
                    NativeScriptEntry loadedScriptEntry{};
                    LoadNativeScriptEntryFromJson(scriptEntryJson, loadedScriptEntry);
                    (void)scene->AttachScriptComponent(entity, std::move(loadedScriptEntry));
                }
            }
            else if (entry.contains("NativeScript") && entry["NativeScript"].is_object())
            {
                NativeScriptEntry loadedScriptEntry{};
                LoadNativeScriptEntryFromJson(entry["NativeScript"], loadedScriptEntry);
                (void)scene->AttachScriptComponent(entity, std::move(loadedScriptEntry));
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

                pe.MaxParticles = std::min(peJson.value("MaxParticles", 1024u), ParticleEmitterComponent::kMaxParticlesCap);
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

        void DeserializeComponentsFromJson(const nlohmann::json& entry, Scene* scene, entt::entity entity,
            int32_t& outJointConnectedEntityIndex)
        {
            DeserializeLayoutComponents(entry, scene, entity);
            DeserializeRenderAndAnimationComponents(entry, scene, entity);
            DeserializeUiComponents(entry, scene, entity);
            DeserializeGridCameraAndAudioComponents(entry, scene, entity);
            DeserializePhysicsComponents(entry, scene, entity, outJointConnectedEntityIndex);
            DeserializeScriptAndPrefabComponents(entry, scene, entity);
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

            ResetRuntimeStateForEntity(*scene, info.Entity);
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

    Result<std::unique_ptr<Scene>> Scene::LoadFromFile(const std::filesystem::path& path)
    {
        auto jsonResult = TryReadSceneJson(path);
        if (jsonResult.IsFailure())
            return Result<std::unique_ptr<Scene>>(jsonResult.GetError());
        nlohmann::json root = std::move(jsonResult.GetValue());

        const int loadedVersion = root.value("Version", 0);

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
        for (size_t index = 0; index < createdEntities.size(); ++index)
        {
            if (parentIndices[index] >= 0)
                continue;
            scene->MarkTransformDirty(createdEntities[index]);
        }

        if (loadedVersion < kSceneSerializationVersion)
            RunPostLoadSliderMigration(scene.get());

        return Result<std::unique_ptr<Scene>>(std::move(scene));
    }
}
