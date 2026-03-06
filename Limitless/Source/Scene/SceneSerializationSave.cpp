#include "Scene/Scene.h"
#include "Scene/Components/PhysicsComponents.h"
#include "Scene/Components/RenderingComponents.h"
#include "Scene/Components/TilemapComponents.h"
#include "Scene/SceneSerialization.h"

#include "Assets/AssetTypes.h"

#include <algorithm>
#include <fstream>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Limitless
{
    namespace
    {
        Result<void> ensure_parent_directory(const std::filesystem::path& path)
        {
            std::error_code errorCode;
            const std::filesystem::path parentDirectory = path.parent_path();
            if (!parentDirectory.empty() && !std::filesystem::exists(parentDirectory, errorCode))
                std::filesystem::create_directories(parentDirectory, errorCode);
            if (errorCode)
                return Result<void>(ErrorCode::FileAccessDenied, "Scene::SaveToFile failed creating parent directories");
            return Result<void>();
        }

        std::pair<std::vector<entt::entity>, std::unordered_map<entt::entity, int32_t>>
        collect_entities_and_index_map(const entt::registry& registry)
        {
            auto view = registry.view<TagComponent, TransformComponent>();
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

            return { std::move(entities), std::move(indexByEntity) };
        }

        nlohmann::json build_root_metadata(const Scene& scene)
        {
            const auto& physicsSettings = scene.GetPhysics2DSettings();
            nlohmann::json root = nlohmann::json::object();
            root["Version"] = kSceneSerializationVersion;
            const auto& editorCamera = scene.GetEditorCameraBookmark();
            if (editorCamera.has_value())
            {
                root["EditorCamera"] = {
                    { "Position", { editorCamera->Position.x, editorCamera->Position.y, editorCamera->Position.z } },
                    { "YawDegrees", editorCamera->YawDegrees },
                    { "PitchDegrees", editorCamera->PitchDegrees }
                };
            }
            root["Physics2DSettings"] = {
                { "WorldCount", physicsSettings.WorldCount },
                { "Gravity", { physicsSettings.Gravity.x, physicsSettings.Gravity.y } },
                { "VelocitySubSteps", physicsSettings.VelocitySubSteps },
                { "EnableSleep", physicsSettings.EnableSleep },
                { "EnableContinuousCollision", physicsSettings.EnableContinuousCollision },
                { "HighContactQualityMode", physicsSettings.HighContactQualityMode },
                { "HighContactQualityExtraSubSteps", physicsSettings.HighContactQualityExtraSubSteps },
                { "ContactHertz", physicsSettings.ContactHertz },
                { "ContactDampingRatio", physicsSettings.ContactDampingRatio },
                { "ContactPushSpeed", physicsSettings.ContactPushSpeed }
            };
            root["Entities"] = nlohmann::json::array();
            return root;
        }

        void serialize_identity_and_hierarchy(const Scene& scene,
                                              const entt::registry& registry,
                                              entt::entity entity,
                                              const std::unordered_map<entt::entity, int32_t>& indexByEntity,
                                              nlohmann::json& entry)
        {
            const auto& tag = registry.get<TagComponent>(entity);
            const auto& transform = registry.get<TransformComponent>(entity);
            entry["Tag"] = tag.Tag;
            entry["EntityEnabled"] = tag.Enabled;
            entry["Transform"] = {
                { "Position", { transform.Position.x, transform.Position.y, transform.Position.z } },
                { "Rotation", { transform.Rotation.x, transform.Rotation.y, transform.Rotation.z } },
                { "Scale", { transform.Scale.x, transform.Scale.y, transform.Scale.z } }
            };

            const entt::entity parent = scene.GetParent(entity);
            int32_t parentIndex = -1;
            if (parent != entt::null)
            {
                const auto it = indexByEntity.find(parent);
                if (it != indexByEntity.end())
                    parentIndex = it->second;
            }

            const auto* hierarchy = registry.try_get<HierarchyComponent>(entity);
            entry["Hierarchy"] = {
                { "ParentIndex", parentIndex },
                { "SiblingOrder", hierarchy ? hierarchy->SiblingOrder : 0 }
            };
        }

        void serialize_layout_components(const entt::registry& registry, entt::entity entity, nlohmann::json& entry)
        {
            if (const auto* canvas = registry.try_get<CanvasComponent>(entity))
            {
                entry["Canvas"] = {
                    { "RenderMode", canvas->Mode == CanvasComponent::RenderMode::WorldSpace ? "WorldSpace" : "ScreenSpace" },
                    { "SortOrder", canvas->SortOrder },
                    { "ReferenceResolution", { canvas->ReferenceResolution.x, canvas->ReferenceResolution.y } }
                };
            }

            if (const auto* rectTransform = registry.try_get<RectTransformComponent>(entity))
            {
                entry["RectTransform"] = {
                    { "AnchorMin", { rectTransform->AnchorMin.x, rectTransform->AnchorMin.y } },
                    { "AnchorMax", { rectTransform->AnchorMax.x, rectTransform->AnchorMax.y } },
                    { "Pivot", { rectTransform->Pivot.x, rectTransform->Pivot.y } },
                    { "SizeDelta", { rectTransform->SizeDelta.x, rectTransform->SizeDelta.y } },
                    { "AnchoredPosition", { rectTransform->AnchoredPosition.x, rectTransform->AnchoredPosition.y } }
                };
            }
        }

        void serialize_render_and_animation_components(const entt::registry& registry, entt::entity entity, nlohmann::json& entry)
        {
            if (const auto* sprite = registry.try_get<SpriteComponent>(entity))
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

            if (const auto* animator = registry.try_get<AnimatorComponent>(entity))
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

            if (const auto* animationEventReceiver = registry.try_get<AnimationEventReceiverComponent>(entity))
            {
                entry["AnimationEventReceiver"] = {
                    { "Enabled", animationEventReceiver->Enabled }
                };
            }

            if (const auto* material = registry.try_get<MaterialComponent>(entity))
            {
                entry["Material"] = SceneSerialization::MakeAssetReferenceJson(material->MaterialKey, Assets::AssetType::Material);
            }

            if (const auto* directionalLight = registry.try_get<DirectionalLight2DComponent>(entity))
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

            if (const auto* pointLight = registry.try_get<PointLight2DComponent>(entity))
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

            if (const auto* shadowOccluder = registry.try_get<ShadowOccluder2DComponent>(entity))
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
        }

        void serialize_ui_components(const entt::registry& registry, entt::entity entity, nlohmann::json& entry)
        {
            if (const auto* uiImage = registry.try_get<UIImageComponent>(entity))
            {
                entry["UIImage"] = {
                    { "RaycastTarget", uiImage->RaycastTarget }
                };
            }

            if (const auto* uiPanel = registry.try_get<UIPanelComponent>(entity))
            {
                entry["UIPanel"] = {
                    { "BackgroundColor", { uiPanel->BackgroundColor.r, uiPanel->BackgroundColor.g, uiPanel->BackgroundColor.b, uiPanel->BackgroundColor.a } },
                    { "UseSpriteTexture", uiPanel->UseSpriteTexture },
                    { "RaycastTarget", uiPanel->RaycastTarget }
                };
            }

            if (const auto* uiText = registry.try_get<UITextComponent>(entity))
            {
                entry["UIText"] = {
                    { "Value", uiText->Text },
                    { "FontFilePath", uiText->FontFilePath },
                    { "FontSize", uiText->FontSize },
                    { "Color", { uiText->Color.r, uiText->Color.g, uiText->Color.b, uiText->Color.a } },
                    { "RaycastTarget", uiText->RaycastTarget }
                };
            }

            if (const auto* uiButton = registry.try_get<UIButtonComponent>(entity))
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

            if (const auto* uiSlider = registry.try_get<UISliderComponent>(entity))
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
        }

        void serialize_grid_and_tilemap_components(const entt::registry& registry, entt::entity entity, nlohmann::json& entry)
        {
            if (const auto* grid2D = registry.try_get<Grid2DComponent>(entity))
            {
                entry["Grid2D"] = {
                    { "CellSize", { grid2D->CellSize.x, grid2D->CellSize.y } },
                    { "CellGap",  { grid2D->CellGap.x,  grid2D->CellGap.y  } }
                };
            }

            if (const auto* tilemapLayer = registry.try_get<TilemapLayerComponent>(entity))
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
        }

        void serialize_camera_and_audio_components(const entt::registry& registry, entt::entity entity, nlohmann::json& entry)
        {
            if (const auto* camera = registry.try_get<CameraComponent>(entity))
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

            if (const auto* audioListener = registry.try_get<AudioListener2DComponent>(entity))
            {
                entry["AudioListener2D"] = {
                    { "Enabled", audioListener->Enabled },
                    { "UsePrimaryCameraPosition", audioListener->UsePrimaryCameraPosition }
                };
            }

            if (const auto* audioListener3D = registry.try_get<AudioListener3DComponent>(entity))
            {
                entry["AudioListener3D"] = {
                    { "Enabled", audioListener3D->Enabled },
                    { "UsePrimaryCameraTransform", audioListener3D->UsePrimaryCameraTransform }
                };
            }

            if (const auto* audioSource = registry.try_get<AudioSourceComponent>(entity))
            {
                const char* rolloffModeName = "SmoothStep";
                switch (audioSource->SpatialRolloffMode)
                {
                    case AudioSourceComponent::RolloffMode::Linear:
                        rolloffModeName = "Linear";
                        break;
                    case AudioSourceComponent::RolloffMode::Inverse:
                        rolloffModeName = "Inverse";
                        break;
                    case AudioSourceComponent::RolloffMode::SmoothStep:
                    default:
                        rolloffModeName = "SmoothStep";
                        break;
                }

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
                    { "SpatialRolloffMode", rolloffModeName },
                    { "StereoPanStrength", audioSource->StereoPanStrength },
                    { "DopplerFactor", audioSource->DopplerFactor },
                    { "EnableDirectionalAttenuation", audioSource->EnableDirectionalAttenuation },
                    { "DirectionalInnerAngleDegrees", audioSource->DirectionalInnerAngleDegrees },
                    { "DirectionalOuterAngleDegrees", audioSource->DirectionalOuterAngleDegrees },
                    { "DirectionalOuterVolume", audioSource->DirectionalOuterVolume },
                    { "AttenuationCurveKey", audioSource->AttenuationCurveKey }
                };
            }
        }

        void serialize_physics_components(const entt::registry& registry,
                                          entt::entity entity,
                                          const std::unordered_map<entt::entity, int32_t>& indexByEntity,
                                          nlohmann::json& entry)
        {
            if (const auto* rigidbody2D = registry.try_get<Rigidbody2DComponent>(entity))
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
                    { "PhysicsWorldSlot", rigidbody2D->PhysicsWorldSlot },
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

            if (const auto* boxCollider2D = registry.try_get<BoxCollider2DComponent>(entity))
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

            if (const auto* circleCollider2D = registry.try_get<CircleCollider2DComponent>(entity))
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

            if (const auto* polygonCollider2D = registry.try_get<PolygonCollider2DComponent>(entity))
            {
                nlohmann::json points = nlohmann::json::array();
                for (const glm::vec2& point : polygonCollider2D->Points)
                    points.push_back({ point.x, point.y });
                entry["PolygonCollider2D"] = {
                    { "Offset", { polygonCollider2D->Offset.x, polygonCollider2D->Offset.y } },
                    { "Points", points },
                    { "Density", polygonCollider2D->Density },
                    { "Friction", polygonCollider2D->Friction },
                    { "Restitution", polygonCollider2D->Restitution },
                    { "IsSensor", polygonCollider2D->IsSensor },
                    { "CollisionLayer", polygonCollider2D->CollisionLayer },
                    { "CollisionMask", polygonCollider2D->CollisionMask }
                };
            }

            if (const auto* edgeCollider2D = registry.try_get<EdgeCollider2DComponent>(entity))
            {
                entry["EdgeCollider2D"] = {
                    { "Offset", { edgeCollider2D->Offset.x, edgeCollider2D->Offset.y } },
                    { "PointA", { edgeCollider2D->PointA.x, edgeCollider2D->PointA.y } },
                    { "PointB", { edgeCollider2D->PointB.x, edgeCollider2D->PointB.y } },
                    { "Friction", edgeCollider2D->Friction },
                    { "Restitution", edgeCollider2D->Restitution },
                    { "IsSensor", edgeCollider2D->IsSensor },
                    { "CollisionLayer", edgeCollider2D->CollisionLayer },
                    { "CollisionMask", edgeCollider2D->CollisionMask }
                };
            }

            if (const auto* capsuleCollider2D = registry.try_get<CapsuleCollider2DComponent>(entity))
            {
                entry["CapsuleCollider2D"] = {
                    { "Offset", { capsuleCollider2D->Offset.x, capsuleCollider2D->Offset.y } },
                    { "Size", { capsuleCollider2D->Size.x, capsuleCollider2D->Size.y } },
                    { "Direction", capsuleCollider2D->Direction == CapsuleCollider2DComponent::Orientation::Horizontal ? "Horizontal" : "Vertical" },
                    { "Density", capsuleCollider2D->Density },
                    { "Friction", capsuleCollider2D->Friction },
                    { "Restitution", capsuleCollider2D->Restitution },
                    { "IsSensor", capsuleCollider2D->IsSensor },
                    { "CollisionLayer", capsuleCollider2D->CollisionLayer },
                    { "CollisionMask", capsuleCollider2D->CollisionMask }
                };
            }

            if (const auto* joint2D = registry.try_get<Joint2DComponent>(entity))
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
        }

        void serialize_script_and_prefab_components(const entt::registry& registry, entt::entity entity, nlohmann::json& entry)
        {
            if (const auto* nativeScript = registry.try_get<NativeScriptComponent>(entity))
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
                        { "Enabled", scriptEntry.Enabled },
                        { "ExecutionPolicy", scriptEntry.ExecutionPolicy == ScriptExecutionPolicy::ParallelSafe ? "ParallelSafe" : "MainThread" },
                        { "DeclaredReadAccessMask", scriptEntry.DeclaredReadAccessMask },
                        { "DeclaredWriteAccessMask", scriptEntry.DeclaredWriteAccessMask }
                    });
                }
                entry["NativeScripts"] = std::move(scriptEntries);
            }

            if (const auto* particleEmitter = registry.try_get<ParticleEmitterComponent>(entity))
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

            if (const auto* prefabInstance = registry.try_get<PrefabInstanceComponent>(entity))
            {
                entry["PrefabInstance"] = {
                    { "Prefab", SceneSerialization::MakeAssetReferenceJson(prefabInstance->PrefabAssetKey, Assets::AssetType::Prefab) }
                };
            }
        }

        void serialize_entity_entries(const Scene& scene,
                                      const std::vector<entt::entity>& entities,
                                      const std::unordered_map<entt::entity, int32_t>& indexByEntity,
                                      nlohmann::json& root)
        {
            const auto& registry = scene.GetRegistry();
            for (entt::entity entity : entities)
            {
                nlohmann::json entry = nlohmann::json::object();
                serialize_identity_and_hierarchy(scene, registry, entity, indexByEntity, entry);
                serialize_layout_components(registry, entity, entry);
                serialize_render_and_animation_components(registry, entity, entry);
                serialize_ui_components(registry, entity, entry);
                serialize_grid_and_tilemap_components(registry, entity, entry);
                serialize_camera_and_audio_components(registry, entity, entry);
                serialize_physics_components(registry, entity, indexByEntity, entry);
                serialize_script_and_prefab_components(registry, entity, entry);
                root["Entities"].push_back(std::move(entry));
            }
        }

        Result<void> atomic_write(const std::filesystem::path& path, const nlohmann::json& root)
        {
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
    }

    Result<void> Scene::SaveToFile(const std::filesystem::path& path) const
    {
        if (auto result = ensure_parent_directory(path); !result.IsSuccess())
            return result;

        auto [entities, indexByEntity] = collect_entities_and_index_map(GetRegistry());

        nlohmann::json root = build_root_metadata(*this);
        serialize_entity_entries(*this, entities, indexByEntity, root);

        return atomic_write(path, root);
    }
}
