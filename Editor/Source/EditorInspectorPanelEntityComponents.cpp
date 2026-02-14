#include "EditorInspectorPanelEntityComponents.h"

#include "EditorAssetNaming.h"
#include "Assets/AudioClipAsset.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <array>
#include <cstdio>

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
    }

    void DrawStandardEntityComponentSections(entt::registry& registry,
                                             entt::entity selectedEntity,
                                             const char* audioPayloadId,
                                             const char* materialPayloadId,
                                             const char* fontPayloadId,
                                             PendingEntityComponentRemovals& pendingRemovals)
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
                tag->Tag = renameBuffer.data();
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
                ImGui::DragFloat3("Rotation", &transform->Rotation.x, 1.0f);
                ImGui::DragFloat3("Scale", &transform->Scale.x, 0.1f);
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

                // Material slot (Unity-style): dropping a material assigns it to the renderer.
                auto* material = registry.try_get<MaterialComponent>(selectedEntity);
                const std::string materialLabel = (material && !material->MaterialKey.empty())
                    ? EditorAssetNaming::GetAssetDisplayNameFromAssetKey(material->MaterialKey)
                    : std::string("None");
                ImGui::Text("Material");
                ImGui::SameLine(80);
                ImGui::Button((materialLabel + "##SpriteMaterial").c_str(), ImVec2(ImGui::GetContentRegionAvail().x - 60, 0));

                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(materialPayloadId))
                    {
                        const char* key = static_cast<const char*>(payload->Data);
                        if (key && key[0])
                        {
                            if (!material)
                                material = &registry.emplace<MaterialComponent>(selectedEntity);
                            material->MaterialKey = key;
                            material->CachedMaterial.reset();
                            material->MaterialLoadAttempted = false;
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                if (material && !material->MaterialKey.empty())
                {
                    ImGui::SameLine();
                    if (ImGui::Button("Clear##Material"))
                    {
                        material->MaterialKey.clear();
                        material->CachedMaterial.reset();
                        material->MaterialLoadAttempted = false;
                    }
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

                if (camera->Projection == CameraComponent::ProjectionType::Orthographic2D)
                {
                    if (camera->NearPlane >= camera->FarPlane)
                        camera->FarPlane = camera->NearPlane + 2.0f;
                    ImGui::DragFloat("Zoom", &camera->Zoom, 0.05f, 0.01f, 100.0f);
                    ImGui::DragFloat("Near Plane", &camera->NearPlane, 0.01f);
                    ImGui::DragFloat("Far Plane", &camera->FarPlane, 0.01f);
                }
                else
                {
                    if (camera->NearPlane <= 0.0f)
                        camera->NearPlane = 0.01f;
                    if (camera->FarPlane <= camera->NearPlane)
                        camera->FarPlane = camera->NearPlane + 1000.0f;
                    ImGui::DragFloat("Field Of View", &camera->FieldOfViewYDegrees, 0.1f, 1.0f, 179.0f);
                    ImGui::DragFloat("Near Plane", &camera->NearPlane, 0.01f, 0.001f, 1000.0f);
                    ImGui::DragFloat("Far Plane", &camera->FarPlane, 1.0f, 0.01f, 100000.0f);
                }

                bool isPrimary = camera->IsPrimary;
                if (ImGui::Checkbox("Primary", &isPrimary))
                {
                    camera->IsPrimary = isPrimary;
                    if (camera->IsPrimary)
                        ClearPrimaryFlagFromOtherCameras(registry, selectedEntity);
                }

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
                ImGui::Checkbox("Loop", &audioSource->Loop);
                ImGui::Checkbox("Muted", &audioSource->Muted);
                ImGui::SliderFloat("Volume", &audioSource->Volume, 0.0f, 2.0f, "%.2f");

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
                    text->Text = textValueBuffer.data();

                ImGui::InputText("Font File Path", fontPathBuffer.data(), fontPathBuffer.size());
                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    text->FontFilePath = fontPathBuffer.data();
                    text->CachedFont.reset();
                    text->FontLoadAttempted = false;
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
                int textRenderSpaceIndex = static_cast<int>(text->Space);
                const char* textRenderSpaceOptions[] = { "World", "Screen" };
                if (ImGui::Combo("Render Space", &textRenderSpaceIndex, textRenderSpaceOptions, 2))
                    text->Space = static_cast<TextComponent::RenderSpace>(textRenderSpaceIndex);
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
                }
                ImGui::ColorEdit4("Color", &text->Color.r);

                ImGui::TreePop();
            }
        }
    }
}
