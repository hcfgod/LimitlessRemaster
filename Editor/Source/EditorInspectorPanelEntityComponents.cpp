#include "EditorInspectorPanelEntityComponents.h"

#include "EditorAssetNaming.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetPaths.h"
#include "Assets/AssetTypes.h"
#include "Assets/AudioClipAsset.h"
#include "Undo/EditorUndoService.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <unordered_set>
#include <vector>

namespace Limitless::EditorInspectorPanel
{
    namespace
    {
        void ClearPrimaryFlagFromOtherCameras(entt::registry& registry, entt::entity currentEntity)
        {
            auto view = registry.view<CameraComponent>();
            for (entt::entity entity : view)
            {
                if (entity == currentEntity)
                    continue;

                auto& otherCamera = view.get<CameraComponent>(entity);
                otherCamera.IsPrimary = false;
            }
        }

        void TrackInteractiveMutation(EditorUndoService* undoService, const char* label)
        {
            if (!undoService || !label)
                return;
            if (ImGui::IsItemActivated())
                undoService->BeginInteractiveSceneMutation();
            if (ImGui::IsItemDeactivatedAfterEdit())
                (void)undoService->CommitInteractiveSceneMutation(label);
        }

        std::vector<std::string> BuildMaterialPickerKeys()
        {
            std::vector<std::string> keys;
            std::unordered_set<std::string> seen;

            const auto records = Assets::AssetDatabase::GetInstance().GetAllRecords();
            for (const auto& record : records)
            {
                if (record.Type != Assets::AssetType::Material || record.Key.empty())
                    continue;
                if (seen.insert(record.Key).second)
                    keys.push_back(record.Key);
            }

            auto tryAddKnownDefault = [&](const char* key) {
                if (!key || !key[0] || seen.contains(key))
                    return;
                const auto resolved = Assets::ResolveAssetKeyToPath(key);
                if (resolved.IsFailure())
                    return;
                std::error_code ec;
                if (std::filesystem::exists(resolved.GetValue(), ec))
                {
                    seen.insert(key);
                    keys.emplace_back(key);
                }
            };

            // Shared editor defaults (project assets still take precedence by key resolution).
            tryAddKnownDefault("Assets/Materials/Renderer2D_TexturedQuad.material.json");
            tryAddKnownDefault("Assets/Materials/Renderer2D_MSDFText.material.json");
            tryAddKnownDefault("Assets/Materials/Lighting2D_DefaultLit.material.json");

            std::sort(keys.begin(), keys.end());
            return keys;
        }

        glm::vec2 NormalizeDirectionOrFallback(const glm::vec2& direction, const glm::vec2& fallback = glm::vec2(0.0f, -1.0f))
        {
            const float length = glm::length(direction);
            if (length <= 0.0001f)
                return fallback;
            return direction / length;
        }
    }

    void DrawStandardEntityComponentSections(entt::registry& registry,
                                             entt::entity selectedEntity,
                                             const char* audioPayloadId,
                                             const char* materialPayloadId,
                                             const char* fontPayloadId,
                                             PendingEntityComponentRemovals& pendingRemovals,
                                             Limitless::EditorUndoService* undoService)
    {
        if (auto* tag = registry.try_get<TagComponent>(selectedEntity))
        {
            static entt::entity renameEntity = entt::null;
            static std::array<char, 256> renameBuffer{};
            if (renameEntity != selectedEntity)
            {
                renameEntity = selectedEntity;
                std::snprintf(renameBuffer.data(), renameBuffer.size(), "%s", tag->Tag.c_str());
            }

            ImGui::AlignTextToFramePadding();
            ImGui::Text("Name");
            ImGui::SameLine(80);
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##EntityName", renameBuffer.data(), renameBuffer.size());
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                const std::string updatedName = renameBuffer.data();
                if (undoService)
                {
                    (void)undoService->ExecuteSceneMutation("Rename Entity", [&](Scene& mutableScene) {
                        auto* mutableTag = mutableScene.GetRegistry().try_get<TagComponent>(selectedEntity);
                        if (!mutableTag)
                            return false;
                        mutableTag->Tag = updatedName;
                        return true;
                    });
                }
                else
                {
                    tag->Tag = updatedName;
                }
            }
        }

        ImGui::Spacing();
        ImGui::Separator();

        if (auto* transform = registry.try_get<TransformComponent>(selectedEntity))
        {
            const bool transformOpen = ImGui::TreeNodeEx("Transform", ImGuiTreeNodeFlags_DefaultOpen);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                ImGui::OpenPopup("TransformComponentOptions");
            ImGui::SameLine();
            if (ImGui::Button("...##TransformComponentOptionsButton"))
                ImGui::OpenPopup("TransformComponentOptions");

            if (ImGui::BeginPopup("TransformComponentOptions"))
            {
                ImGui::BeginDisabled();
                ImGui::MenuItem("Remove Component");
                ImGui::EndDisabled();
                ImGui::Separator();
                ImGui::TextDisabled("Transform cannot be removed.");
                ImGui::EndPopup();
            }

            if (transformOpen)
            {
                ImGui::DragFloat3("Position", &transform->Position.x, 0.1f);
                TrackInteractiveMutation(undoService, "Edit Transform Position");
                ImGui::DragFloat3("Rotation", &transform->Rotation.x, 1.0f);
                TrackInteractiveMutation(undoService, "Edit Transform Rotation");
                ImGui::DragFloat3("Scale", &transform->Scale.x, 0.1f);
                TrackInteractiveMutation(undoService, "Edit Transform Scale");
                ImGui::TreePop();
            }
        }

        if (auto* sprite = registry.try_get<SpriteComponent>(selectedEntity))
        {
            const bool spriteOpen = ImGui::TreeNodeEx("Sprite", ImGuiTreeNodeFlags_DefaultOpen);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                ImGui::OpenPopup("SpriteComponentOptions");
            ImGui::SameLine();
            if (ImGui::Button("...##SpriteComponentOptionsButton"))
                ImGui::OpenPopup("SpriteComponentOptions");

            if (ImGui::BeginPopup("SpriteComponentOptions"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    pendingRemovals.RemoveSpriteComponent = true;
                ImGui::EndPopup();
            }

            if (spriteOpen)
            {
                ImGui::ColorEdit4("Color", &sprite->Color.r);
                TrackInteractiveMutation(undoService, "Edit Sprite Color");

                // Material slot (Unity-style): dropping a material assigns it to the renderer.
                auto* material = registry.try_get<MaterialComponent>(selectedEntity);
                const auto assignMaterialKey = [&](const std::string& key) {
                    if (undoService)
                    {
                        (void)undoService->ExecuteSceneMutation(key.empty() ? "Clear Material" : "Assign Material", [&](Scene& mutableScene) {
                            auto& mutableRegistry = mutableScene.GetRegistry();
                            auto* mutableMaterial = mutableRegistry.try_get<MaterialComponent>(selectedEntity);
                            if (!mutableMaterial)
                                mutableMaterial = &mutableRegistry.emplace<MaterialComponent>(selectedEntity);
                            mutableMaterial->MaterialKey = key;
                            mutableMaterial->CachedMaterial.reset();
                            mutableMaterial->MaterialLoadAttempted = false;
                            return true;
                        });
                    }
                    else
                    {
                        if (!material)
                            material = &registry.emplace<MaterialComponent>(selectedEntity);
                        material->MaterialKey = key;
                        material->CachedMaterial.reset();
                        material->MaterialLoadAttempted = false;
                    }
                };
                const std::string materialLabel = (material && !material->MaterialKey.empty())
                    ? EditorAssetNaming::GetAssetDisplayNameFromAssetKey(material->MaterialKey)
                    : std::string("None");
                ImGui::Text("Material");
                ImGui::SameLine(80);
                ImGui::Button((materialLabel + "##SpriteMaterial").c_str(), ImVec2(ImGui::GetContentRegionAvail().x - 90, 0));

                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(materialPayloadId))
                    {
                        const char* key = static_cast<const char*>(payload->Data);
                        if (key && key[0])
                            assignMaterialKey(key);
                    }
                    ImGui::EndDragDropTarget();
                }

                ImGui::SameLine();
                if (ImGui::Button("...##SpriteMaterialPicker"))
                    ImGui::OpenPopup("SpriteMaterialPickerPopup");

                if (ImGui::BeginPopup("SpriteMaterialPickerPopup"))
                {
                    if (ImGui::Selectable("None##SpriteMaterialPickerNone"))
                        assignMaterialKey({});
                    ImGui::Separator();

                    const std::vector<std::string> materialKeys = BuildMaterialPickerKeys();
                    for (const auto& key : materialKeys)
                    {
                        const bool isSelected = material && material->MaterialKey == key;
                        const std::string display = EditorAssetNaming::GetAssetDisplayNameFromAssetKey(key);
                        if (ImGui::Selectable((display + "##SpriteMaterialPicker_" + key).c_str(), isSelected))
                        {
                            assignMaterialKey(key);
                            ImGui::CloseCurrentPopup();
                        }
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                            ImGui::SetTooltip("%s", key.c_str());
                    }
                    ImGui::EndPopup();
                }

                if (material && !material->MaterialKey.empty())
                {
                    ImGui::SameLine();
                    if (ImGui::Button("Clear##Material"))
                        assignMaterialKey({});
                }

                ImGui::TreePop();
            }
        }

        if (auto* camera = registry.try_get<CameraComponent>(selectedEntity))
        {
            const bool cameraOpen = ImGui::TreeNodeEx("Camera", ImGuiTreeNodeFlags_DefaultOpen);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                ImGui::OpenPopup("CameraComponentOptions");
            ImGui::SameLine();
            if (ImGui::Button("...##CameraComponentOptionsButton"))
                ImGui::OpenPopup("CameraComponentOptions");

            if (ImGui::BeginPopup("CameraComponentOptions"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    pendingRemovals.RemoveCameraComponent = true;
                ImGui::EndPopup();
            }

            if (cameraOpen)
            {
                int projectionIndex = static_cast<int>(camera->Projection);
                const char* projectionOptions[] = { "Orthographic 2D", "Perspective 3D" };
                const CameraComponent::ProjectionType previousProjection = camera->Projection;
                if (ImGui::Combo("Projection", &projectionIndex, projectionOptions, 2))
                {
                    camera->Projection = static_cast<CameraComponent::ProjectionType>(projectionIndex);
                    if (previousProjection != camera->Projection)
                    {
                        if (camera->Projection == CameraComponent::ProjectionType::Perspective3D)
                        {
                            // Switching from ortho to perspective should use perspective-safe clip defaults.
                            camera->NearPlane = 0.1f;
                            camera->FarPlane = 1000.0f;
                        }
                        else
                        {
                            // Switching from perspective to ortho uses the classic 2D clip volume.
                            camera->NearPlane = -1.0f;
                            camera->FarPlane = 1.0f;
                        }
                    }
                }
                TrackInteractiveMutation(undoService, "Edit Camera Projection");

                if (camera->Projection == CameraComponent::ProjectionType::Orthographic2D)
                {
                    if (camera->NearPlane >= camera->FarPlane)
                        camera->FarPlane = camera->NearPlane + 2.0f;
                    ImGui::DragFloat("Zoom", &camera->Zoom, 0.05f, 0.01f, 100.0f);
                    TrackInteractiveMutation(undoService, "Edit Camera Zoom");
                    ImGui::DragFloat("Near Plane", &camera->NearPlane, 0.01f);
                    TrackInteractiveMutation(undoService, "Edit Camera Near Plane");
                    ImGui::DragFloat("Far Plane", &camera->FarPlane, 0.01f);
                    TrackInteractiveMutation(undoService, "Edit Camera Far Plane");
                }
                else
                {
                    if (camera->NearPlane <= 0.0f)
                        camera->NearPlane = 0.01f;
                    if (camera->FarPlane <= camera->NearPlane)
                        camera->FarPlane = camera->NearPlane + 1000.0f;
                    ImGui::DragFloat("Field Of View", &camera->FieldOfViewYDegrees, 0.1f, 1.0f, 179.0f);
                    TrackInteractiveMutation(undoService, "Edit Camera Field Of View");
                    ImGui::DragFloat("Near Plane", &camera->NearPlane, 0.01f, 0.001f, 1000.0f);
                    TrackInteractiveMutation(undoService, "Edit Camera Near Plane");
                    ImGui::DragFloat("Far Plane", &camera->FarPlane, 1.0f, 0.01f, 100000.0f);
                    TrackInteractiveMutation(undoService, "Edit Camera Far Plane");
                }

                bool isPrimary = camera->IsPrimary;
                if (ImGui::Checkbox("Primary", &isPrimary))
                {
                    camera->IsPrimary = isPrimary;
                    if (camera->IsPrimary)
                        ClearPrimaryFlagFromOtherCameras(registry, selectedEntity);
                }
                TrackInteractiveMutation(undoService, "Edit Camera Primary");

                ImGui::TreePop();
            }
        }

        if (auto* audioSource = registry.try_get<AudioSourceComponent>(selectedEntity))
        {
            const bool audioOpen = ImGui::TreeNodeEx("Audio Source", ImGuiTreeNodeFlags_DefaultOpen);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                ImGui::OpenPopup("AudioSourceComponentOptions");
            ImGui::SameLine();
            if (ImGui::Button("...##AudioSourceComponentOptionsButton"))
                ImGui::OpenPopup("AudioSourceComponentOptions");

            if (ImGui::BeginPopup("AudioSourceComponentOptions"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    pendingRemovals.RemoveAudioSourceComponent = true;
                ImGui::EndPopup();
            }

            if (audioOpen)
            {
                const std::string clipLabel = audioSource->AudioClipKey.empty()
                    ? std::string("None")
                    : EditorAssetNaming::GetAssetDisplayNameFromAssetKey(audioSource->AudioClipKey);

                ImGui::Text("Clip");
                ImGui::SameLine(80);
                ImGui::Button((clipLabel + "##AudioClip").c_str(), ImVec2(ImGui::GetContentRegionAvail().x - 60.0f, 0.0f));
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(audioPayloadId))
                    {
                        const char* key = static_cast<const char*>(payload->Data);
                        if (key && key[0])
                        {
                            audioSource->AudioClipKey = key;
                            audioSource->RuntimePlaybackStarted = false;
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                if (!audioSource->AudioClipKey.empty())
                {
                    ImGui::SameLine();
                    if (ImGui::Button("Clear##AudioClip"))
                    {
                        if (audioSource->RuntimeVoiceId != 0)
                            Audio::AudioEngine::GetInstance().Stop(audioSource->RuntimeVoiceId);
                        audioSource->AudioClipKey.clear();
                        audioSource->RuntimeVoiceId = 0;
                        audioSource->RuntimePlaybackStarted = false;
                    }
                }

                ImGui::Checkbox("Play On Start", &audioSource->PlayOnStart);
                TrackInteractiveMutation(undoService, "Edit Audio Play On Start");
                ImGui::Checkbox("Loop", &audioSource->Loop);
                TrackInteractiveMutation(undoService, "Edit Audio Loop");
                ImGui::Checkbox("Muted", &audioSource->Muted);
                TrackInteractiveMutation(undoService, "Edit Audio Muted");
                ImGui::SliderFloat("Volume", &audioSource->Volume, 0.0f, 2.0f, "%.2f");
                TrackInteractiveMutation(undoService, "Edit Audio Volume");

                if (audioSource->RuntimeVoiceId != 0 &&
                    !Audio::AudioEngine::GetInstance().IsVoiceActive(audioSource->RuntimeVoiceId))
                {
                    audioSource->RuntimeVoiceId = 0;
                    audioSource->RuntimePlaybackStarted = false;
                }
                const bool isPlaying = (audioSource->RuntimeVoiceId != 0);
                if (isPlaying)
                {
                    if (ImGui::Button("Stop##AudioSourcePreview", ImVec2(120, 0)))
                    {
                        Audio::AudioEngine::GetInstance().Stop(audioSource->RuntimeVoiceId);
                        audioSource->RuntimeVoiceId = 0;
                        audioSource->RuntimePlaybackStarted = false;
                    }
                }
                else
                {
                    if (ImGui::Button("Play##AudioSourcePreview", ImVec2(120, 0)))
                    {
                        if (!audioSource->AudioClipKey.empty())
                        {
                            auto clipAsset = Assets::AudioClipAsset::LoadBlocking(audioSource->AudioClipKey);
                            if (clipAsset && clipAsset->GetClip())
                            {
                                const float volume = audioSource->Muted ? 0.0f : audioSource->Volume;
                                audioSource->RuntimeVoiceId = Audio::AudioEngine::GetInstance().PlayClip(
                                    clipAsset->GetClip(),
                                    volume,
                                    audioSource->Loop);
                                audioSource->RuntimePlaybackStarted = (audioSource->RuntimeVoiceId != 0);
                            }
                        }
                    }
                }

                ImGui::TreePop();
            }
        }

        if (auto* rigidbody2D = registry.try_get<Rigidbody2DComponent>(selectedEntity))
        {
            const bool rigidbodyOpen = ImGui::TreeNodeEx("Rigidbody 2D", ImGuiTreeNodeFlags_DefaultOpen);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                ImGui::OpenPopup("Rigidbody2DComponentOptions");
            ImGui::SameLine();
            if (ImGui::Button("...##Rigidbody2DComponentOptionsButton"))
                ImGui::OpenPopup("Rigidbody2DComponentOptions");

            if (ImGui::BeginPopup("Rigidbody2DComponentOptions"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    pendingRemovals.RemoveRigidbody2DComponent = true;
                ImGui::EndPopup();
            }

            if (rigidbodyOpen)
            {
                int bodyTypeIndex = static_cast<int>(rigidbody2D->Type);
                const char* bodyTypeNames[] = { "Static", "Dynamic", "Kinematic" };
                if (ImGui::Combo("Body Type", &bodyTypeIndex, bodyTypeNames, 3))
                    rigidbody2D->Type = static_cast<Rigidbody2DComponent::BodyType>(bodyTypeIndex);
                TrackInteractiveMutation(undoService, "Edit Rigidbody2D Body Type");

                ImGui::Checkbox("Fixed Rotation", &rigidbody2D->FixedRotation);
                TrackInteractiveMutation(undoService, "Edit Rigidbody2D Fixed Rotation");
                ImGui::Checkbox("Use CCD", &rigidbody2D->UseCCD);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip("Continuous Collision Detection. Use for fast-moving bodies to reduce tunneling through colliders.");
                TrackInteractiveMutation(undoService, "Edit Rigidbody2D Use CCD");
                ImGui::Checkbox("Enable Sleep", &rigidbody2D->EnableSleep);
                TrackInteractiveMutation(undoService, "Edit Rigidbody2D Enable Sleep");
                ImGui::Checkbox("Start Awake", &rigidbody2D->StartAwake);
                TrackInteractiveMutation(undoService, "Edit Rigidbody2D Start Awake");
                ImGui::Checkbox("Interpolate", &rigidbody2D->Interpolate);
                TrackInteractiveMutation(undoService, "Edit Rigidbody2D Interpolate");
                ImGui::Checkbox("High Contact Quality", &rigidbody2D->HighContactQuality);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip("Applies extra world solver sub-steps when this body is present. Useful for rotating platforms and dense contact stacks.");
                TrackInteractiveMutation(undoService, "Edit Rigidbody2D High Contact Quality");
                ImGui::DragInt("Extra Solver Sub Steps", &rigidbody2D->ExtraSolverSubSteps, 1.0f, 0, 24);
                rigidbody2D->ExtraSolverSubSteps = std::max(0, rigidbody2D->ExtraSolverSubSteps);
                TrackInteractiveMutation(undoService, "Edit Rigidbody2D Extra Solver Sub Steps");
                ImGui::DragFloat("Gravity Scale", &rigidbody2D->GravityScale, 0.01f, -10.0f, 10.0f);
                TrackInteractiveMutation(undoService, "Edit Rigidbody2D Gravity Scale");
                ImGui::DragFloat("Linear Damping", &rigidbody2D->LinearDamping, 0.01f, 0.0f, 100.0f);
                TrackInteractiveMutation(undoService, "Edit Rigidbody2D Linear Damping");
                ImGui::DragFloat("Angular Damping", &rigidbody2D->AngularDamping, 0.01f, 0.0f, 100.0f);
                TrackInteractiveMutation(undoService, "Edit Rigidbody2D Angular Damping");

                ImGui::TreePop();
            }
        }

        if (auto* boxCollider2D = registry.try_get<BoxCollider2DComponent>(selectedEntity))
        {
            const bool boxColliderOpen = ImGui::TreeNodeEx("Box Collider 2D", ImGuiTreeNodeFlags_DefaultOpen);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                ImGui::OpenPopup("BoxCollider2DComponentOptions");
            ImGui::SameLine();
            if (ImGui::Button("...##BoxCollider2DComponentOptionsButton"))
                ImGui::OpenPopup("BoxCollider2DComponentOptions");

            if (ImGui::BeginPopup("BoxCollider2DComponentOptions"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    pendingRemovals.RemoveBoxCollider2DComponent = true;
                ImGui::EndPopup();
            }

            if (boxColliderOpen)
            {
                ImGui::DragFloat2("Offset", &boxCollider2D->Offset.x, 0.01f);
                TrackInteractiveMutation(undoService, "Edit BoxCollider2D Offset");
                ImGui::DragFloat2("Size", &boxCollider2D->Size.x, 0.01f, 0.001f, 1000.0f);
                TrackInteractiveMutation(undoService, "Edit BoxCollider2D Size");
                ImGui::DragFloat("Density", &boxCollider2D->Density, 0.01f, 0.0f, 1000.0f);
                TrackInteractiveMutation(undoService, "Edit BoxCollider2D Density");
                ImGui::DragFloat("Friction", &boxCollider2D->Friction, 0.01f, 0.0f, 1.0f);
                TrackInteractiveMutation(undoService, "Edit BoxCollider2D Friction");
                ImGui::DragFloat("Restitution", &boxCollider2D->Restitution, 0.01f, 0.0f, 1.0f);
                TrackInteractiveMutation(undoService, "Edit BoxCollider2D Restitution");
                ImGui::Checkbox("Is Sensor", &boxCollider2D->IsSensor);
                TrackInteractiveMutation(undoService, "Edit BoxCollider2D Sensor");
                ImGui::InputScalar("Layer Bits", ImGuiDataType_U64, &boxCollider2D->CollisionLayer);
                TrackInteractiveMutation(undoService, "Edit BoxCollider2D Layer");
                ImGui::InputScalar("Mask Bits", ImGuiDataType_U64, &boxCollider2D->CollisionMask);
                TrackInteractiveMutation(undoService, "Edit BoxCollider2D Mask");

                ImGui::TreePop();
            }
        }

        if (auto* circleCollider2D = registry.try_get<CircleCollider2DComponent>(selectedEntity))
        {
            const bool circleColliderOpen = ImGui::TreeNodeEx("Circle Collider 2D", ImGuiTreeNodeFlags_DefaultOpen);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                ImGui::OpenPopup("CircleCollider2DComponentOptions");
            ImGui::SameLine();
            if (ImGui::Button("...##CircleCollider2DComponentOptionsButton"))
                ImGui::OpenPopup("CircleCollider2DComponentOptions");

            if (ImGui::BeginPopup("CircleCollider2DComponentOptions"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    pendingRemovals.RemoveCircleCollider2DComponent = true;
                ImGui::EndPopup();
            }

            if (circleColliderOpen)
            {
                ImGui::DragFloat2("Offset", &circleCollider2D->Offset.x, 0.01f);
                TrackInteractiveMutation(undoService, "Edit CircleCollider2D Offset");
                ImGui::DragFloat("Radius", &circleCollider2D->Radius, 0.01f, 0.001f, 1000.0f);
                TrackInteractiveMutation(undoService, "Edit CircleCollider2D Radius");
                ImGui::DragFloat("Density", &circleCollider2D->Density, 0.01f, 0.0f, 1000.0f);
                TrackInteractiveMutation(undoService, "Edit CircleCollider2D Density");
                ImGui::DragFloat("Friction", &circleCollider2D->Friction, 0.01f, 0.0f, 1.0f);
                TrackInteractiveMutation(undoService, "Edit CircleCollider2D Friction");
                ImGui::DragFloat("Restitution", &circleCollider2D->Restitution, 0.01f, 0.0f, 1.0f);
                TrackInteractiveMutation(undoService, "Edit CircleCollider2D Restitution");
                ImGui::Checkbox("Is Sensor", &circleCollider2D->IsSensor);
                TrackInteractiveMutation(undoService, "Edit CircleCollider2D Sensor");
                ImGui::InputScalar("Layer Bits", ImGuiDataType_U64, &circleCollider2D->CollisionLayer);
                TrackInteractiveMutation(undoService, "Edit CircleCollider2D Layer");
                ImGui::InputScalar("Mask Bits", ImGuiDataType_U64, &circleCollider2D->CollisionMask);
                TrackInteractiveMutation(undoService, "Edit CircleCollider2D Mask");

                ImGui::TreePop();
            }
        }

        if (auto* directionalLight = registry.try_get<DirectionalLight2DComponent>(selectedEntity))
        {
            const bool directionalLightOpen = ImGui::TreeNodeEx("Directional Light 2D", ImGuiTreeNodeFlags_DefaultOpen);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                ImGui::OpenPopup("DirectionalLight2DComponentOptions");
            ImGui::SameLine();
            if (ImGui::Button("...##DirectionalLight2DComponentOptionsButton"))
                ImGui::OpenPopup("DirectionalLight2DComponentOptions");

            if (ImGui::BeginPopup("DirectionalLight2DComponentOptions"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    pendingRemovals.RemoveDirectionalLight2DComponent = true;
                ImGui::EndPopup();
            }

            if (directionalLightOpen)
            {
                ImGui::Checkbox("Enabled", &directionalLight->Enabled);
                TrackInteractiveMutation(undoService, "Edit Directional Light Enabled");
                ImGui::ColorEdit3("Color", &directionalLight->Color.r);
                TrackInteractiveMutation(undoService, "Edit Directional Light Color");
                ImGui::DragFloat("Intensity", &directionalLight->Intensity, 0.01f, 0.0f, 100.0f, "%.2f");
                TrackInteractiveMutation(undoService, "Edit Directional Light Intensity");
                ImGui::Checkbox("Use Entity Rotation", &directionalLight->UseEntityRotation);
                TrackInteractiveMutation(undoService, "Edit Directional Light Rotation Mode");
                if (!directionalLight->UseEntityRotation)
                {
                    ImGui::DragFloat2("Direction", &directionalLight->Direction.x, 0.01f, -1.0f, 1.0f, "%.3f");
                    directionalLight->Direction = NormalizeDirectionOrFallback(directionalLight->Direction);
                    TrackInteractiveMutation(undoService, "Edit Directional Light Direction");
                }

                ImGui::Checkbox("Cast Shadows", &directionalLight->CastShadows);
                TrackInteractiveMutation(undoService, "Edit Directional Light Cast Shadows");
                ImGui::DragFloat("Shadow Strength", &directionalLight->ShadowStrength, 0.01f, 0.0f, 1.0f, "%.2f");
                directionalLight->ShadowStrength = std::clamp(directionalLight->ShadowStrength, 0.0f, 1.0f);
                TrackInteractiveMutation(undoService, "Edit Directional Light Shadow Strength");
                ImGui::DragFloat("Shadow Softness", &directionalLight->ShadowSoftness, 0.01f, 0.0f, 256.0f, "%.2f");
                directionalLight->ShadowSoftness = std::max(0.0f, directionalLight->ShadowSoftness);
                TrackInteractiveMutation(undoService, "Edit Directional Light Shadow Softness");
                ImGui::DragInt("Shadow Samples", &directionalLight->ShadowSamples, 1.0f, 1, 32);
                directionalLight->ShadowSamples = std::clamp(directionalLight->ShadowSamples, 1, 32);
                TrackInteractiveMutation(undoService, "Edit Directional Light Shadow Samples");
                ImGui::DragFloat("Shadow Distance", &directionalLight->ShadowDistance, 0.05f, 0.0f, 10000.0f, "%.2f");
                directionalLight->ShadowDistance = std::max(0.0f, directionalLight->ShadowDistance);
                TrackInteractiveMutation(undoService, "Edit Directional Light Shadow Distance");
                ImGui::DragFloat("Shadow Bias", &directionalLight->ShadowBias, 0.0005f, 0.0f, 2.0f, "%.4f");
                directionalLight->ShadowBias = std::max(0.0f, directionalLight->ShadowBias);
                TrackInteractiveMutation(undoService, "Edit Directional Light Shadow Bias");

                ImGui::TreePop();
            }
        }

        if (auto* pointLight = registry.try_get<PointLight2DComponent>(selectedEntity))
        {
            const bool pointLightOpen = ImGui::TreeNodeEx("Point Light 2D", ImGuiTreeNodeFlags_DefaultOpen);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                ImGui::OpenPopup("PointLight2DComponentOptions");
            ImGui::SameLine();
            if (ImGui::Button("...##PointLight2DComponentOptionsButton"))
                ImGui::OpenPopup("PointLight2DComponentOptions");

            if (ImGui::BeginPopup("PointLight2DComponentOptions"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    pendingRemovals.RemovePointLight2DComponent = true;
                ImGui::EndPopup();
            }

            if (pointLightOpen)
            {
                ImGui::Checkbox("Enabled", &pointLight->Enabled);
                TrackInteractiveMutation(undoService, "Edit Point Light Enabled");
                ImGui::ColorEdit3("Color", &pointLight->Color.r);
                TrackInteractiveMutation(undoService, "Edit Point Light Color");
                ImGui::DragFloat("Intensity", &pointLight->Intensity, 0.01f, 0.0f, 100.0f, "%.2f");
                TrackInteractiveMutation(undoService, "Edit Point Light Intensity");
                ImGui::DragFloat("Radius", &pointLight->Radius, 0.01f, 0.01f, 10000.0f, "%.2f");
                pointLight->Radius = std::max(0.01f, pointLight->Radius);
                TrackInteractiveMutation(undoService, "Edit Point Light Radius");
                ImGui::DragFloat("Falloff", &pointLight->Falloff, 0.01f, 0.1f, 8.0f, "%.2f");
                pointLight->Falloff = std::max(0.1f, pointLight->Falloff);
                TrackInteractiveMutation(undoService, "Edit Point Light Falloff");
                ImGui::Checkbox("Cast Shadows", &pointLight->CastShadows);
                TrackInteractiveMutation(undoService, "Edit Point Light Cast Shadows");
                ImGui::DragFloat("Shadow Strength", &pointLight->ShadowStrength, 0.01f, 0.0f, 1.0f, "%.2f");
                pointLight->ShadowStrength = std::clamp(pointLight->ShadowStrength, 0.0f, 1.0f);
                TrackInteractiveMutation(undoService, "Edit Point Light Shadow Strength");
                ImGui::DragFloat("Shadow Softness", &pointLight->ShadowSoftness, 0.01f, 0.0f, 256.0f, "%.2f");
                pointLight->ShadowSoftness = std::max(0.0f, pointLight->ShadowSoftness);
                TrackInteractiveMutation(undoService, "Edit Point Light Shadow Softness");
                ImGui::DragInt("Shadow Samples", &pointLight->ShadowSamples, 1.0f, 1, 32);
                pointLight->ShadowSamples = std::clamp(pointLight->ShadowSamples, 1, 32);
                TrackInteractiveMutation(undoService, "Edit Point Light Shadow Samples");
                ImGui::DragFloat("Shadow Bias", &pointLight->ShadowBias, 0.0001f, 0.0f, 10.0f, "%.4f");
                pointLight->ShadowBias = std::max(0.0f, pointLight->ShadowBias);
                TrackInteractiveMutation(undoService, "Edit Point Light Shadow Bias");

                ImGui::TreePop();
            }
        }

        if (auto* shadowOccluder = registry.try_get<ShadowOccluder2DComponent>(selectedEntity))
        {
            const bool occluderOpen = ImGui::TreeNodeEx("Shadow Occluder 2D", ImGuiTreeNodeFlags_DefaultOpen);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                ImGui::OpenPopup("ShadowOccluder2DComponentOptions");
            ImGui::SameLine();
            if (ImGui::Button("...##ShadowOccluder2DComponentOptionsButton"))
                ImGui::OpenPopup("ShadowOccluder2DComponentOptions");

            if (ImGui::BeginPopup("ShadowOccluder2DComponentOptions"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    pendingRemovals.RemoveShadowOccluder2DComponent = true;
                ImGui::EndPopup();
            }

            if (occluderOpen)
            {
                ImGui::Checkbox("Enabled", &shadowOccluder->Enabled);
                TrackInteractiveMutation(undoService, "Edit Shadow Occluder Enabled");

                int sourceMode = static_cast<int>(shadowOccluder->Source);
                const char* sourceModeNames[] = { "Manual Polygon", "Physics Collider" };
                if (ImGui::Combo("Source", &sourceMode, sourceModeNames, 2))
                    shadowOccluder->Source = static_cast<ShadowOccluder2DComponent::SourceMode>(sourceMode);
                TrackInteractiveMutation(undoService, "Edit Shadow Occluder Source");

                ImGui::Checkbox("Closed Polygon", &shadowOccluder->Closed);
                TrackInteractiveMutation(undoService, "Edit Shadow Occluder Closed");
                ImGui::DragFloat("Extrusion", &shadowOccluder->Extrusion, 0.01f, 0.0f, 1000.0f, "%.2f");
                shadowOccluder->Extrusion = std::max(0.0f, shadowOccluder->Extrusion);
                TrackInteractiveMutation(undoService, "Edit Shadow Occluder Extrusion");

                if (shadowOccluder->Source == ShadowOccluder2DComponent::SourceMode::ManualPolygon)
                {
                    if (ImGui::Button("Add Point"))
                    {
                        const glm::vec2 newPoint = shadowOccluder->PolygonPoints.empty()
                            ? glm::vec2(0.0f)
                            : (shadowOccluder->PolygonPoints.back() + glm::vec2(0.5f, 0.0f));

                        if (undoService)
                        {
                            (void)undoService->ExecuteSceneMutation("Add Shadow Occluder Point", [&](Scene& mutableScene) {
                                auto* mutableOccluder = mutableScene.GetRegistry().try_get<ShadowOccluder2DComponent>(selectedEntity);
                                if (!mutableOccluder)
                                    return false;
                                mutableOccluder->PolygonPoints.push_back(newPoint);
                                return true;
                            });
                            shadowOccluder = registry.try_get<ShadowOccluder2DComponent>(selectedEntity);
                        }
                        else
                        {
                            shadowOccluder->PolygonPoints.push_back(newPoint);
                        }
                    }

                    int removePointIndex = -1;
                    for (size_t pointIndex = 0; pointIndex < shadowOccluder->PolygonPoints.size(); ++pointIndex)
                    {
                        ImGui::PushID(static_cast<int>(pointIndex));
                        ImGui::DragFloat2("Point", &shadowOccluder->PolygonPoints[pointIndex].x, 0.01f, -10000.0f, 10000.0f, "%.3f");
                        TrackInteractiveMutation(undoService, "Edit Shadow Occluder Point");
                        ImGui::SameLine();
                        if (ImGui::Button("X"))
                            removePointIndex = static_cast<int>(pointIndex);
                        ImGui::PopID();
                    }

                    if (removePointIndex >= 0)
                    {
                        if (undoService)
                        {
                            (void)undoService->ExecuteSceneMutation("Remove Shadow Occluder Point", [&](Scene& mutableScene) {
                                auto* mutableOccluder = mutableScene.GetRegistry().try_get<ShadowOccluder2DComponent>(selectedEntity);
                                if (!mutableOccluder)
                                    return false;
                                if (removePointIndex < 0 || removePointIndex >= static_cast<int>(mutableOccluder->PolygonPoints.size()))
                                    return false;
                                mutableOccluder->PolygonPoints.erase(mutableOccluder->PolygonPoints.begin() + removePointIndex);
                                return true;
                            });
                        }
                        else
                        {
                            shadowOccluder->PolygonPoints.erase(shadowOccluder->PolygonPoints.begin() + removePointIndex);
                        }
                    }
                }
                else
                {
                    ImGui::TextDisabled("Uses Box/Circle Collider2D shape on this entity.");
                }

                ImGui::TreePop();
            }
        }

        if (auto* joint2D = registry.try_get<Joint2DComponent>(selectedEntity))
        {
            const bool jointOpen = ImGui::TreeNodeEx("Joint 2D", ImGuiTreeNodeFlags_DefaultOpen);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                ImGui::OpenPopup("Joint2DComponentOptions");
            ImGui::SameLine();
            if (ImGui::Button("...##Joint2DComponentOptionsButton"))
                ImGui::OpenPopup("Joint2DComponentOptions");

            if (ImGui::BeginPopup("Joint2DComponentOptions"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    pendingRemovals.RemoveJoint2DComponent = true;
                ImGui::EndPopup();
            }

            if (jointOpen)
            {
                int jointTypeIndex = static_cast<int>(joint2D->Type);
                const char* jointTypeNames[] = { "Distance", "Revolute", "Prismatic" };
                if (ImGui::Combo("Type", &jointTypeIndex, jointTypeNames, 3))
                    joint2D->Type = static_cast<Joint2DComponent::JointType>(jointTypeIndex);
                TrackInteractiveMutation(undoService, "Edit Joint2D Type");

                int connectedEntityId = (joint2D->ConnectedEntity == entt::null) ? -1 : static_cast<int>(joint2D->ConnectedEntity);
                if (ImGui::InputInt("Connected Entity ID", &connectedEntityId))
                    joint2D->ConnectedEntity = connectedEntityId >= 0 ? static_cast<entt::entity>(connectedEntityId) : entt::null;
                TrackInteractiveMutation(undoService, "Edit Joint2D Connected Entity");

                ImGui::Checkbox("Collide Connected", &joint2D->CollideConnected);
                TrackInteractiveMutation(undoService, "Edit Joint2D Collide Connected");
                ImGui::DragFloat2("Anchor A", &joint2D->AnchorA.x, 0.01f);
                TrackInteractiveMutation(undoService, "Edit Joint2D Anchor A");
                ImGui::DragFloat2("Anchor B", &joint2D->AnchorB.x, 0.01f);
                TrackInteractiveMutation(undoService, "Edit Joint2D Anchor B");
                ImGui::DragFloat2("Axis", &joint2D->Axis.x, 0.01f);
                TrackInteractiveMutation(undoService, "Edit Joint2D Axis");
                ImGui::Checkbox("Enable Limit", &joint2D->EnableLimit);
                TrackInteractiveMutation(undoService, "Edit Joint2D Enable Limit");
                ImGui::DragFloat2("Limits", &joint2D->Limits.x, 0.01f);
                TrackInteractiveMutation(undoService, "Edit Joint2D Limits");
                ImGui::Checkbox("Enable Motor", &joint2D->EnableMotor);
                TrackInteractiveMutation(undoService, "Edit Joint2D Enable Motor");
                ImGui::DragFloat("Motor Speed", &joint2D->MotorSpeed, 0.01f);
                TrackInteractiveMutation(undoService, "Edit Joint2D Motor Speed");
                ImGui::DragFloat("Max Motor Force/Torque", &joint2D->MaxMotorForceOrTorque, 0.1f, 0.0f, 100000.0f);
                TrackInteractiveMutation(undoService, "Edit Joint2D Max Motor");
                ImGui::Checkbox("Enable Spring", &joint2D->EnableSpring);
                TrackInteractiveMutation(undoService, "Edit Joint2D Enable Spring");
                ImGui::DragFloat("Hertz", &joint2D->Hertz, 0.1f, 0.0f, 1000.0f);
                TrackInteractiveMutation(undoService, "Edit Joint2D Hertz");
                ImGui::DragFloat("Damping Ratio", &joint2D->DampingRatio, 0.01f, 0.0f, 10.0f);
                TrackInteractiveMutation(undoService, "Edit Joint2D Damping");

                ImGui::TreePop();
            }
        }

        if (auto* text = registry.try_get<TextComponent>(selectedEntity))
        {
            const bool textOpen = ImGui::TreeNodeEx("Text", ImGuiTreeNodeFlags_DefaultOpen);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                ImGui::OpenPopup("TextComponentOptions");
            ImGui::SameLine();
            if (ImGui::Button("...##TextComponentOptionsButton"))
                ImGui::OpenPopup("TextComponentOptions");

            if (ImGui::BeginPopup("TextComponentOptions"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    pendingRemovals.RemoveTextComponent = true;
                ImGui::EndPopup();
            }

            if (textOpen)
            {
                static entt::entity textEditEntity = entt::null;
                static std::array<char, 2048> textValueBuffer{};
                static std::array<char, 512> fontPathBuffer{};
                if (textEditEntity != selectedEntity)
                {
                    textEditEntity = selectedEntity;
                    std::snprintf(textValueBuffer.data(), textValueBuffer.size(), "%s", text->Text.c_str());
                    std::snprintf(fontPathBuffer.data(), fontPathBuffer.size(), "%s", text->FontFilePath.c_str());
                }

                ImGui::InputTextMultiline("Text Value", textValueBuffer.data(), textValueBuffer.size(), ImVec2(-1.0f, 84.0f));
                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    text->Text = textValueBuffer.data();
                    if (undoService)
                        (void)undoService->CommitInteractiveSceneMutation("Edit Text Value");
                }
                else if (ImGui::IsItemActivated() && undoService)
                {
                    undoService->BeginInteractiveSceneMutation();
                }

                ImGui::InputText("Font File Path", fontPathBuffer.data(), fontPathBuffer.size());
                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    text->FontFilePath = fontPathBuffer.data();
                    text->CachedFont.reset();
                    text->FontLoadAttempted = false;
                    if (undoService)
                        (void)undoService->CommitInteractiveSceneMutation("Edit Font File Path");
                }
                else if (ImGui::IsItemActivated() && undoService)
                {
                    undoService->BeginInteractiveSceneMutation();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Example: Assets/Fonts/YourFont.ttf");

                const std::string fontLabel = text->FontFilePath.empty()
                    ? std::string("None")
                    : EditorAssetNaming::GetAssetDisplayNameFromAssetKey(text->FontFilePath);
                ImGui::Text("Font Asset");
                ImGui::SameLine(80);
                ImGui::Button((fontLabel + "##TextFontAsset").c_str(), ImVec2(ImGui::GetContentRegionAvail().x - 60.0f, 0.0f));
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(fontPayloadId))
                    {
                        const char* key = static_cast<const char*>(payload->Data);
                        if (key && key[0])
                        {
                            text->FontFilePath = key;
                            std::snprintf(fontPathBuffer.data(), fontPathBuffer.size(), "%s", text->FontFilePath.c_str());
                            text->CachedFont.reset();
                            text->FontLoadAttempted = false;
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                if (!text->FontFilePath.empty())
                {
                    ImGui::SameLine();
                    if (ImGui::Button("Clear##TextFontAsset"))
                    {
                        text->FontFilePath.clear();
                        fontPathBuffer[0] = '\0';
                        text->CachedFont.reset();
                        text->FontLoadAttempted = false;
                    }
                }

                if (ImGui::DragFloat("Font Size", &text->FontSize, 1.0f, 4.0f, 512.0f))
                    text->FontSize = std::max(4.0f, text->FontSize);
                TrackInteractiveMutation(undoService, "Edit Font Size");
                int textRenderSpaceIndex = static_cast<int>(text->Space);
                const char* textRenderSpaceOptions[] = { "World", "Screen" };
                if (ImGui::Combo("Render Space", &textRenderSpaceIndex, textRenderSpaceOptions, 2))
                    text->Space = static_cast<TextComponent::RenderSpace>(textRenderSpaceIndex);
                TrackInteractiveMutation(undoService, "Edit Text Render Space");
                if (text->Space == TextComponent::RenderSpace::Screen && ImGui::IsItemHovered())
                    ImGui::SetTooltip("Screen text uses viewport-centered pixel coordinates (0,0 = center, X right, Y up).");
                if (text->Space == TextComponent::RenderSpace::Screen)
                {
                    int screenAnchorIndex = static_cast<int>(text->Anchor);
                    const char* screenAnchorOptions[] = {
                        "Center",
                        "Top Left",
                        "Top Center",
                        "Top Right",
                        "Middle Left",
                        "Middle Right",
                        "Bottom Left",
                        "Bottom Center",
                        "Bottom Right"
                    };
                    if (ImGui::Combo("Screen Anchor", &screenAnchorIndex, screenAnchorOptions, 9))
                        text->Anchor = static_cast<TextComponent::ScreenAnchor>(screenAnchorIndex);
                    TrackInteractiveMutation(undoService, "Edit Text Anchor");
                }
                ImGui::ColorEdit4("Color", &text->Color.r);
                TrackInteractiveMutation(undoService, "Edit Text Color");

                ImGui::TreePop();
            }
        }
    }
}
