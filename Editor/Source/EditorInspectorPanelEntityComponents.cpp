#include "EditorInspectorPanelEntityComponents.h"

#include "EditorAssetNaming.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetPaths.h"
#include "Assets/SpriteImportSettings.h"
#include "Assets/AssetTypes.h"
#include "Assets/AudioClipAsset.h"
#include "Scene/ParticleEmitterSystem.h"
#include "Undo/EditorUndoService.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string_view>
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

        std::vector<std::string> BuildAudioClipPickerKeys()
        {
            std::vector<std::string> keys;
            std::unordered_set<std::string> seen;

            const auto records = Assets::AssetDatabase::GetInstance().GetAllRecords();
            for (const auto& record : records)
            {
                if (record.Type != Assets::AssetType::AudioClip || record.Key.empty())
                    continue;
                if (seen.insert(record.Key).second)
                    keys.push_back(record.Key);
            }

            std::sort(keys.begin(), keys.end());
            return keys;
        }

        std::vector<std::string> BuildAnimationClipPickerKeys()
        {
            std::vector<std::string> keys;
            std::unordered_set<std::string> seen;

            const auto records = Assets::AssetDatabase::GetInstance().GetAllRecords();
            for (const auto& record : records)
            {
                if (record.Type != Assets::AssetType::AnimationClip || record.Key.empty())
                    continue;
                if (seen.insert(record.Key).second)
                    keys.push_back(record.Key);
            }

            std::sort(keys.begin(), keys.end());
            return keys;
        }

        std::vector<std::string> BuildAnimatorControllerPickerKeys()
        {
            std::vector<std::string> keys;
            std::unordered_set<std::string> seen;

            const auto records = Assets::AssetDatabase::GetInstance().GetAllRecords();
            for (const auto& record : records)
            {
                if (record.Type != Assets::AssetType::AnimatorController || record.Key.empty())
                    continue;
                if (seen.insert(record.Key).second)
                    keys.push_back(record.Key);
            }

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

        entt::entity FindDirectChildByTag(const entt::registry& registry, entt::entity parentEntity, std::string_view childTag)
        {
            auto childView = registry.view<HierarchyComponent, TagComponent>();
            for (entt::entity child : childView)
            {
                const auto& hierarchy = childView.get<HierarchyComponent>(child);
                if (hierarchy.Parent != parentEntity)
                    continue;
                const auto& tag = childView.get<TagComponent>(child);
                if (tag.Tag == childTag)
                    return child;
            }

            return entt::null;
        }

        bool SliderHasVisualChildren(const entt::registry& registry, entt::entity sliderEntity)
        {
            return FindDirectChildByTag(registry, sliderEntity, "Slider Background") != entt::null
                || FindDirectChildByTag(registry, sliderEntity, "Slider Fill") != entt::null
                || FindDirectChildByTag(registry, sliderEntity, "Slider Handle") != entt::null;
        }

    }

    void DrawStandardEntityComponentSections(Scene* scene,
                                             entt::registry& registry,
                                             entt::entity selectedEntity,
                                             const char* texturePayloadId,
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
            const float enabledCheckboxWidth = ImGui::CalcTextSize("Enabled").x + ImGui::GetStyle().FramePadding.x * 2.0f + 28.0f;
            ImGui::SetNextItemWidth(std::max(40.0f, ImGui::GetContentRegionAvail().x - enabledCheckboxWidth));
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

            ImGui::SameLine();
            ImGui::Checkbox("Enabled##EntityEnabled", &tag->Enabled);
            TrackInteractiveMutation(undoService, "Edit Entity Enabled");
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

        if (auto* canvas = registry.try_get<CanvasComponent>(selectedEntity))
        {
            const bool canvasOpen = ImGui::TreeNodeEx("Canvas", ImGuiTreeNodeFlags_DefaultOpen);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                ImGui::OpenPopup("CanvasComponentOptions");
            ImGui::SameLine();
            if (ImGui::Button("...##CanvasComponentOptionsButton"))
                ImGui::OpenPopup("CanvasComponentOptions");

            if (ImGui::BeginPopup("CanvasComponentOptions"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    pendingRemovals.RemoveCanvasComponent = true;
                ImGui::EndPopup();
            }

            if (canvasOpen)
            {
                int renderModeIndex = static_cast<int>(canvas->Mode);
                const char* renderModes[] = { "Screen Space", "World Space" };
                if (ImGui::Combo("Render Mode", &renderModeIndex, renderModes, 2))
                    canvas->Mode = static_cast<CanvasComponent::RenderMode>(renderModeIndex);
                TrackInteractiveMutation(undoService, "Edit Canvas Render Mode");

                ImGui::DragInt("Sort Order", &canvas->SortOrder, 1.0f);
                TrackInteractiveMutation(undoService, "Edit Canvas Sort Order");

                ImGui::DragFloat2("Reference Resolution", &canvas->ReferenceResolution.x, 1.0f, 1.0f, 16384.0f, "%.0f");
                canvas->ReferenceResolution.x = std::max(1.0f, canvas->ReferenceResolution.x);
                canvas->ReferenceResolution.y = std::max(1.0f, canvas->ReferenceResolution.y);
                TrackInteractiveMutation(undoService, "Edit Canvas Reference Resolution");
                ImGui::TreePop();
            }
        }

        if (auto* rectTransform = registry.try_get<RectTransformComponent>(selectedEntity))
        {
            const bool rectTransformOpen = ImGui::TreeNodeEx("RectTransform", ImGuiTreeNodeFlags_DefaultOpen);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                ImGui::OpenPopup("RectTransformComponentOptions");
            ImGui::SameLine();
            if (ImGui::Button("...##RectTransformComponentOptionsButton"))
                ImGui::OpenPopup("RectTransformComponentOptions");

            if (ImGui::BeginPopup("RectTransformComponentOptions"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    pendingRemovals.RemoveRectTransformComponent = true;
                ImGui::EndPopup();
            }

            if (rectTransformOpen)
            {
                struct AnchorPreset
                {
                    const char* Name;
                    glm::vec2 AnchorMin;
                    glm::vec2 AnchorMax;
                    glm::vec2 Pivot;
                };
                static const std::array<AnchorPreset, 9> anchorPresets = {{
                    { "Top Left",     glm::vec2(0.0f, 1.0f), glm::vec2(0.0f, 1.0f), glm::vec2(0.0f, 1.0f) },
                    { "Top Center",   glm::vec2(0.5f, 1.0f), glm::vec2(0.5f, 1.0f), glm::vec2(0.5f, 1.0f) },
                    { "Top Right",    glm::vec2(1.0f, 1.0f), glm::vec2(1.0f, 1.0f), glm::vec2(1.0f, 1.0f) },
                    { "Middle Left",  glm::vec2(0.0f, 0.5f), glm::vec2(0.0f, 0.5f), glm::vec2(0.0f, 0.5f) },
                    { "Middle Center",glm::vec2(0.5f, 0.5f), glm::vec2(0.5f, 0.5f), glm::vec2(0.5f, 0.5f) },
                    { "Middle Right", glm::vec2(1.0f, 0.5f), glm::vec2(1.0f, 0.5f), glm::vec2(1.0f, 0.5f) },
                    { "Bottom Left",  glm::vec2(0.0f, 0.0f), glm::vec2(0.0f, 0.0f), glm::vec2(0.0f, 0.0f) },
                    { "Bottom Center",glm::vec2(0.5f, 0.0f), glm::vec2(0.5f, 0.0f), glm::vec2(0.5f, 0.0f) },
                    { "Bottom Right", glm::vec2(1.0f, 0.0f), glm::vec2(1.0f, 0.0f), glm::vec2(1.0f, 0.0f) }
                }};

                int selectedPresetIndex = -1;
                for (int presetIndex = 0; presetIndex < static_cast<int>(anchorPresets.size()); ++presetIndex)
                {
                    const AnchorPreset& preset = anchorPresets[static_cast<size_t>(presetIndex)];
                    const auto nearlyEqual = [](const glm::vec2& left, const glm::vec2& right) {
                        return std::abs(left.x - right.x) < 0.0001f &&
                               std::abs(left.y - right.y) < 0.0001f;
                    };
                    if (nearlyEqual(preset.AnchorMin, rectTransform->AnchorMin) &&
                        nearlyEqual(preset.AnchorMax, rectTransform->AnchorMax) &&
                        nearlyEqual(preset.Pivot, rectTransform->Pivot))
                    {
                        selectedPresetIndex = presetIndex;
                        break;
                    }
                }

                const char* selectedPresetName = selectedPresetIndex >= 0
                    ? anchorPresets[static_cast<size_t>(selectedPresetIndex)].Name
                    : "Custom";
                if (ImGui::BeginCombo("Anchor Preset", selectedPresetName))
                {
                    for (int presetIndex = 0; presetIndex < static_cast<int>(anchorPresets.size()); ++presetIndex)
                    {
                        const bool isSelected = presetIndex == selectedPresetIndex;
                        if (ImGui::Selectable(anchorPresets[static_cast<size_t>(presetIndex)].Name, isSelected))
                        {
                            const AnchorPreset& preset = anchorPresets[static_cast<size_t>(presetIndex)];
                            rectTransform->AnchorMin = preset.AnchorMin;
                            rectTransform->AnchorMax = preset.AnchorMax;
                            rectTransform->Pivot = preset.Pivot;
                        }
                        if (isSelected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                TrackInteractiveMutation(undoService, "Edit RectTransform Anchor Preset");

                ImGui::DragFloat2("Anchor Min", &rectTransform->AnchorMin.x, 0.01f, 0.0f, 1.0f);
                rectTransform->AnchorMin.x = std::clamp(rectTransform->AnchorMin.x, 0.0f, 1.0f);
                rectTransform->AnchorMin.y = std::clamp(rectTransform->AnchorMin.y, 0.0f, 1.0f);
                TrackInteractiveMutation(undoService, "Edit RectTransform Anchor Min");

                ImGui::DragFloat2("Anchor Max", &rectTransform->AnchorMax.x, 0.01f, 0.0f, 1.0f);
                rectTransform->AnchorMax.x = std::clamp(rectTransform->AnchorMax.x, 0.0f, 1.0f);
                rectTransform->AnchorMax.y = std::clamp(rectTransform->AnchorMax.y, 0.0f, 1.0f);
                TrackInteractiveMutation(undoService, "Edit RectTransform Anchor Max");

                if (rectTransform->AnchorMin.x > rectTransform->AnchorMax.x)
                    std::swap(rectTransform->AnchorMin.x, rectTransform->AnchorMax.x);
                if (rectTransform->AnchorMin.y > rectTransform->AnchorMax.y)
                    std::swap(rectTransform->AnchorMin.y, rectTransform->AnchorMax.y);

                ImGui::DragFloat2("Pivot", &rectTransform->Pivot.x, 0.01f, 0.0f, 1.0f);
                rectTransform->Pivot.x = std::clamp(rectTransform->Pivot.x, 0.0f, 1.0f);
                rectTransform->Pivot.y = std::clamp(rectTransform->Pivot.y, 0.0f, 1.0f);
                TrackInteractiveMutation(undoService, "Edit RectTransform Pivot");

                ImGui::DragFloat2("Size Delta", &rectTransform->SizeDelta.x, 1.0f, -16384.0f, 16384.0f);
                TrackInteractiveMutation(undoService, "Edit RectTransform Size Delta");

                ImGui::DragFloat2("Anchored Position", &rectTransform->AnchoredPosition.x, 1.0f);
                TrackInteractiveMutation(undoService, "Edit RectTransform Anchored Position");

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
                const bool sliderRootUsesChildVisuals = registry.all_of<UISliderComponent>(selectedEntity)
                    && SliderHasVisualChildren(registry, selectedEntity);
                if (sliderRootUsesChildVisuals)
                {
                    ImGui::TextWrapped("This Sprite is not used when the slider has child visuals.");
                    ImGui::TextWrapped("Edit colors on child entities: Slider Background, Slider Fill, and Slider Handle.");
                    if (ImGui::Button("Remove Root Sprite Component##SliderRootSprite"))
                        pendingRemovals.RemoveSpriteComponent = true;
                    ImGui::TreePop();
                }
                else
                {
                    ImGui::ColorEdit4("Color", &sprite->Color.r);
                    TrackInteractiveMutation(undoService, "Edit Sprite Color");
                    ImGui::DragFloat2("Tiling Factor", &sprite->TilingFactor.x, 0.05f, 0.001f, 100.0f, "%.3f");
                    sprite->TilingFactor.x = std::max(0.001f, sprite->TilingFactor.x);
                    sprite->TilingFactor.y = std::max(0.001f, sprite->TilingFactor.y);
                    TrackInteractiveMutation(undoService, "Edit Sprite Tiling Factor");
                    ImGui::DragInt("Render Order", &sprite->RenderOrder, 1.0f);
                    TrackInteractiveMutation(undoService, "Edit Sprite Render Order");
                    ImGui::Checkbox("Cast Shadows", &sprite->CastShadows);
                    TrackInteractiveMutation(undoService, "Edit Sprite Cast Shadows");
                    ImGui::Checkbox("Receive Shadows", &sprite->ReceiveShadows);
                    TrackInteractiveMutation(undoService, "Edit Sprite Receive Shadows");

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

                // Sub-sprite region (when the assigned texture has a sprite sheet).
                if (!sprite->TextureKey.empty())
                {
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::TextDisabled("Sub-Sprite");

                    const auto spriteSettings = Assets::LoadSpriteImportSettings(sprite->TextureKey);
                    if (spriteSettings.Mode == Assets::SpriteImportSettings::SpriteMode::Multiple &&
                        !spriteSettings.SubSprites.empty())
                    {
                        int subIndex = sprite->SubSpriteIndex;
                        const int maxIndex = static_cast<int>(spriteSettings.SubSprites.size()) - 1;
                        const std::string previewName = (subIndex >= 0 && subIndex <= maxIndex)
                            ? spriteSettings.SubSprites[subIndex].Name
                            : std::string("Full Texture");

                        if (ImGui::BeginCombo("Sub-Sprite##Picker", previewName.c_str()))
                        {
                            if (ImGui::Selectable("Full Texture", subIndex < 0))
                            {
                                sprite->SubSpriteIndex = -1;
                                sprite->UvMin = glm::vec2(0.0f);
                                sprite->UvMax = glm::vec2(1.0f);
                            }

                            for (int i = 0; i <= maxIndex; ++i)
                            {
                                const bool selected = (subIndex == i);
                                const auto& sub = spriteSettings.SubSprites[i];
                                if (ImGui::Selectable((sub.Name + "##" + std::to_string(i)).c_str(), selected))
                                {
                                    sprite->SubSpriteIndex = i;

                                    // Compute UVs from the sub-sprite rect.
                                    if (sprite->CachedTexture && sprite->CachedTexture->GetTexture())
                                    {
                                        const auto uvs = Assets::ComputeSubSpriteUvs(
                                            sub.RectPixels,
                                            sprite->CachedTexture->GetTexture()->GetWidth(),
                                            sprite->CachedTexture->GetTexture()->GetHeight());
                                        sprite->UvMin = glm::vec2(uvs.x, uvs.y);
                                        sprite->UvMax = glm::vec2(uvs.z, uvs.w);
                                    }
                                }
                                if (selected)
                                    ImGui::SetItemDefaultFocus();
                            }

                            ImGui::EndCombo();
                        }
                    }
                    else
                    {
                        ImGui::TextDisabled("Set Sprite Mode to Multiple and slice to use sub-sprites.");
                    }
                }

                    ImGui::TreePop();
                }
            }
        }

        if (auto* animator = registry.try_get<AnimatorComponent>(selectedEntity))
        {
            const bool animatorOpen = ImGui::TreeNodeEx("Animator", ImGuiTreeNodeFlags_DefaultOpen);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                ImGui::OpenPopup("AnimatorComponentOptions");
            ImGui::SameLine();
            if (ImGui::Button("...##AnimatorComponentOptionsButton"))
                ImGui::OpenPopup("AnimatorComponentOptions");

            if (ImGui::BeginPopup("AnimatorComponentOptions"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    pendingRemovals.RemoveAnimatorComponent = true;
                ImGui::EndPopup();
            }

            if (animatorOpen)
            {
                ImGui::Checkbox("Enabled", &animator->Enabled);
                TrackInteractiveMutation(undoService, "Edit Animator Enabled");

                ImGui::Checkbox("Auto Play", &animator->AutoPlay);
                TrackInteractiveMutation(undoService, "Edit Animator Auto Play");

                ImGui::Checkbox("Apply To Sprite", &animator->ApplyToSprite);
                TrackInteractiveMutation(undoService, "Edit Animator Apply To Sprite");

                ImGui::Checkbox("Apply To Transform", &animator->ApplyToTransform);
                TrackInteractiveMutation(undoService, "Edit Animator Apply To Transform");

                ImGui::DragFloat("Playback Speed", &animator->PlaybackSpeed, 0.01f, 0.0f, 10.0f);
                animator->PlaybackSpeed = std::max(0.0f, animator->PlaybackSpeed);
                TrackInteractiveMutation(undoService, "Edit Animator Playback Speed");

                const auto assignControllerKey = [&](const std::string& key) {
                    if (undoService)
                    {
                        (void)undoService->ExecuteSceneMutation(key.empty() ? "Clear Animator Controller" : "Assign Animator Controller", [&](Scene& mutableScene) {
                            auto* mutableAnimator = mutableScene.GetRegistry().try_get<AnimatorComponent>(selectedEntity);
                            if (!mutableAnimator)
                                return false;
                            mutableAnimator->ControllerKey = key;
                            mutableAnimator->CachedController.reset();
                            mutableAnimator->ControllerLoadAttempted = false;
                            mutableAnimator->RuntimeInitialized = false;
                            return true;
                        });
                    }
                    else
                    {
                        animator->ControllerKey = key;
                        animator->CachedController.reset();
                        animator->ControllerLoadAttempted = false;
                        animator->RuntimeInitialized = false;
                    }
                };

                const auto assignDefaultClipKey = [&](const std::string& key) {
                    if (undoService)
                    {
                        (void)undoService->ExecuteSceneMutation(key.empty() ? "Clear Animator Default Clip" : "Assign Animator Default Clip", [&](Scene& mutableScene) {
                            auto* mutableAnimator = mutableScene.GetRegistry().try_get<AnimatorComponent>(selectedEntity);
                            if (!mutableAnimator)
                                return false;
                            mutableAnimator->DefaultClipKey = key;
                            mutableAnimator->CachedDefaultClip.reset();
                            mutableAnimator->DefaultClipLoadAttempted = false;
                            mutableAnimator->RuntimeInitialized = false;
                            return true;
                        });
                    }
                    else
                    {
                        animator->DefaultClipKey = key;
                        animator->CachedDefaultClip.reset();
                        animator->DefaultClipLoadAttempted = false;
                        animator->RuntimeInitialized = false;
                    }
                };

                const std::string controllerLabel = animator->ControllerKey.empty()
                    ? std::string("None")
                    : EditorAssetNaming::GetAssetDisplayNameFromAssetKey(animator->ControllerKey);
                ImGui::Text("Controller");
                ImGui::SameLine(100.0f);
                ImGui::Button((controllerLabel + "##AnimatorController").c_str(), ImVec2(ImGui::GetContentRegionAvail().x - 90.0f, 0.0f));
                if (ImGui::BeginPopupContextItem("AnimatorControllerContext"))
                {
                    if (ImGui::Selectable("Clear"))
                        assignControllerKey({});
                    ImGui::EndPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("...##AnimatorControllerPicker"))
                    ImGui::OpenPopup("AnimatorControllerPickerPopup");
                if (ImGui::BeginPopup("AnimatorControllerPickerPopup"))
                {
                    if (ImGui::Selectable("None##AnimatorControllerNone"))
                        assignControllerKey({});
                    ImGui::Separator();
                    const auto controllerKeys = BuildAnimatorControllerPickerKeys();
                    for (const auto& key : controllerKeys)
                    {
                        const bool isSelected = animator->ControllerKey == key;
                        const std::string display = EditorAssetNaming::GetAssetDisplayNameFromAssetKey(key);
                        if (ImGui::Selectable((display + "##AnimatorController_" + key).c_str(), isSelected))
                            assignControllerKey(key);
                    }
                    ImGui::EndPopup();
                }

                const std::string clipLabel = animator->DefaultClipKey.empty()
                    ? std::string("None")
                    : EditorAssetNaming::GetAssetDisplayNameFromAssetKey(animator->DefaultClipKey);
                ImGui::Text("Default Clip");
                ImGui::SameLine(100.0f);
                ImGui::Button((clipLabel + "##AnimatorDefaultClip").c_str(), ImVec2(ImGui::GetContentRegionAvail().x - 90.0f, 0.0f));
                if (ImGui::BeginPopupContextItem("AnimatorDefaultClipContext"))
                {
                    if (ImGui::Selectable("Clear"))
                        assignDefaultClipKey({});
                    ImGui::EndPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("...##AnimatorDefaultClipPicker"))
                    ImGui::OpenPopup("AnimatorDefaultClipPickerPopup");
                if (ImGui::BeginPopup("AnimatorDefaultClipPickerPopup"))
                {
                    if (ImGui::Selectable("None##AnimatorDefaultClipNone"))
                        assignDefaultClipKey({});
                    ImGui::Separator();
                    const auto clipKeys = BuildAnimationClipPickerKeys();
                    for (const auto& key : clipKeys)
                    {
                        const bool isSelected = animator->DefaultClipKey == key;
                        const std::string display = EditorAssetNaming::GetAssetDisplayNameFromAssetKey(key);
                        if (ImGui::Selectable((display + "##AnimatorDefaultClip_" + key).c_str(), isSelected))
                            assignDefaultClipKey(key);
                    }
                    ImGui::EndPopup();
                }

                if (ImGui::TreeNodeEx("Parameter Overrides", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    if (ImGui::Button("Add Bool"))
                    {
                        std::string parameterName = "BoolParameter";
                        int32_t suffix = 1;
                        while (animator->BoolParameters.contains(parameterName))
                        {
                            ++suffix;
                            parameterName = "BoolParameter" + std::to_string(suffix);
                        }
                        animator->BoolParameters[parameterName] = false;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Add Float"))
                    {
                        std::string parameterName = "FloatParameter";
                        int32_t suffix = 1;
                        while (animator->FloatParameters.contains(parameterName))
                        {
                            ++suffix;
                            parameterName = "FloatParameter" + std::to_string(suffix);
                        }
                        animator->FloatParameters[parameterName] = 0.0f;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Add Integer"))
                    {
                        std::string parameterName = "IntegerParameter";
                        int32_t suffix = 1;
                        while (animator->IntegerParameters.contains(parameterName))
                        {
                            ++suffix;
                            parameterName = "IntegerParameter" + std::to_string(suffix);
                        }
                        animator->IntegerParameters[parameterName] = 0;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Add Trigger"))
                    {
                        std::string parameterName = "TriggerParameter";
                        int32_t suffix = 1;
                        while (animator->TriggerParameters.contains(parameterName))
                        {
                            ++suffix;
                            parameterName = "TriggerParameter" + std::to_string(suffix);
                        }
                        animator->TriggerParameters[parameterName] = false;
                    }

                    int32_t removeBoolIndex = -1;
                    int32_t boolIndex = 0;
                    for (auto it = animator->BoolParameters.begin(); it != animator->BoolParameters.end(); ++it, ++boolIndex)
                    {
                        ImGui::PushID(("AnimatorBool_" + it->first).c_str());
                        std::array<char, 128> nameBuffer{};
                        std::snprintf(nameBuffer.data(), nameBuffer.size(), "%s", it->first.c_str());
                        bool renameRequested = ImGui::InputText("Name", nameBuffer.data(), nameBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);
                        ImGui::SameLine();
                        ImGui::Checkbox("Value", &it->second);
                        ImGui::SameLine();
                        if (ImGui::Button("X"))
                            removeBoolIndex = boolIndex;
                        if (renameRequested)
                        {
                            const std::string newName = nameBuffer.data();
                            if (!newName.empty() && !animator->BoolParameters.contains(newName))
                            {
                                const bool value = it->second;
                                animator->BoolParameters.erase(it);
                                animator->BoolParameters[newName] = value;
                                ImGui::PopID();
                                break;
                            }
                        }
                        ImGui::PopID();
                    }
                    if (removeBoolIndex >= 0)
                    {
                        int32_t currentIndex = 0;
                        for (auto it = animator->BoolParameters.begin(); it != animator->BoolParameters.end(); ++it, ++currentIndex)
                        {
                            if (currentIndex == removeBoolIndex)
                            {
                                animator->BoolParameters.erase(it);
                                break;
                            }
                        }
                    }

                    int32_t removeFloatIndex = -1;
                    int32_t floatIndex = 0;
                    for (auto it = animator->FloatParameters.begin(); it != animator->FloatParameters.end(); ++it, ++floatIndex)
                    {
                        ImGui::PushID(("AnimatorFloat_" + it->first).c_str());
                        std::array<char, 128> nameBuffer{};
                        std::snprintf(nameBuffer.data(), nameBuffer.size(), "%s", it->first.c_str());
                        bool renameRequested = ImGui::InputText("Name", nameBuffer.data(), nameBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);
                        ImGui::SameLine();
                        ImGui::DragFloat("Value", &it->second, 0.01f);
                        ImGui::SameLine();
                        if (ImGui::Button("X"))
                            removeFloatIndex = floatIndex;
                        if (renameRequested)
                        {
                            const std::string newName = nameBuffer.data();
                            if (!newName.empty() && !animator->FloatParameters.contains(newName))
                            {
                                const float value = it->second;
                                animator->FloatParameters.erase(it);
                                animator->FloatParameters[newName] = value;
                                ImGui::PopID();
                                break;
                            }
                        }
                        ImGui::PopID();
                    }
                    if (removeFloatIndex >= 0)
                    {
                        int32_t currentIndex = 0;
                        for (auto it = animator->FloatParameters.begin(); it != animator->FloatParameters.end(); ++it, ++currentIndex)
                        {
                            if (currentIndex == removeFloatIndex)
                            {
                                animator->FloatParameters.erase(it);
                                break;
                            }
                        }
                    }

                    int32_t removeIntegerIndex = -1;
                    int32_t integerIndex = 0;
                    for (auto it = animator->IntegerParameters.begin(); it != animator->IntegerParameters.end(); ++it, ++integerIndex)
                    {
                        ImGui::PushID(("AnimatorInteger_" + it->first).c_str());
                        std::array<char, 128> nameBuffer{};
                        std::snprintf(nameBuffer.data(), nameBuffer.size(), "%s", it->first.c_str());
                        bool renameRequested = ImGui::InputText("Name", nameBuffer.data(), nameBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);
                        ImGui::SameLine();
                        ImGui::DragInt("Value", &it->second);
                        ImGui::SameLine();
                        if (ImGui::Button("X"))
                            removeIntegerIndex = integerIndex;
                        if (renameRequested)
                        {
                            const std::string newName = nameBuffer.data();
                            if (!newName.empty() && !animator->IntegerParameters.contains(newName))
                            {
                                const int32_t value = it->second;
                                animator->IntegerParameters.erase(it);
                                animator->IntegerParameters[newName] = value;
                                ImGui::PopID();
                                break;
                            }
                        }
                        ImGui::PopID();
                    }
                    if (removeIntegerIndex >= 0)
                    {
                        int32_t currentIndex = 0;
                        for (auto it = animator->IntegerParameters.begin(); it != animator->IntegerParameters.end(); ++it, ++currentIndex)
                        {
                            if (currentIndex == removeIntegerIndex)
                            {
                                animator->IntegerParameters.erase(it);
                                break;
                            }
                        }
                    }

                    int32_t removeTriggerIndex = -1;
                    int32_t triggerIndex = 0;
                    for (auto it = animator->TriggerParameters.begin(); it != animator->TriggerParameters.end(); ++it, ++triggerIndex)
                    {
                        ImGui::PushID(("AnimatorTrigger_" + it->first).c_str());
                        std::array<char, 128> nameBuffer{};
                        std::snprintf(nameBuffer.data(), nameBuffer.size(), "%s", it->first.c_str());
                        bool renameRequested = ImGui::InputText("Name", nameBuffer.data(), nameBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);
                        ImGui::SameLine();
                        ImGui::Checkbox("Active", &it->second);
                        ImGui::SameLine();
                        if (ImGui::Button("X"))
                            removeTriggerIndex = triggerIndex;
                        if (renameRequested)
                        {
                            const std::string newName = nameBuffer.data();
                            if (!newName.empty() && !animator->TriggerParameters.contains(newName))
                            {
                                const bool value = it->second;
                                animator->TriggerParameters.erase(it);
                                animator->TriggerParameters[newName] = value;
                                ImGui::PopID();
                                break;
                            }
                        }
                        ImGui::PopID();
                    }
                    if (removeTriggerIndex >= 0)
                    {
                        int32_t currentIndex = 0;
                        for (auto it = animator->TriggerParameters.begin(); it != animator->TriggerParameters.end(); ++it, ++currentIndex)
                        {
                            if (currentIndex == removeTriggerIndex)
                            {
                                animator->TriggerParameters.erase(it);
                                break;
                            }
                        }
                    }

                    TrackInteractiveMutation(undoService, "Edit Animator Parameters");
                    ImGui::TreePop();
                }

                ImGui::Separator();
                ImGui::TextDisabled("Runtime");
                ImGui::Text("State: %s", animator->RuntimeCurrentStateName.empty() ? "<none>" : animator->RuntimeCurrentStateName.c_str());
                ImGui::Text("Clip: %s", animator->RuntimeCurrentClipKey.empty() ? "<none>" : animator->RuntimeCurrentClipKey.c_str());
                ImGui::Text("State Time: %.3f", animator->RuntimeStateTimeSeconds);
                ImGui::Text("Duration: %.3f", animator->RuntimeCurrentStateDurationSeconds);
                ImGui::TreePop();
            }
        }

        if (auto* animationEventReceiver = registry.try_get<AnimationEventReceiverComponent>(selectedEntity))
        {
            const bool receiverOpen = ImGui::TreeNodeEx("Animation Event Receiver", ImGuiTreeNodeFlags_DefaultOpen);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                ImGui::OpenPopup("AnimationEventReceiverComponentOptions");
            ImGui::SameLine();
            if (ImGui::Button("...##AnimationEventReceiverComponentOptionsButton"))
                ImGui::OpenPopup("AnimationEventReceiverComponentOptions");

            if (ImGui::BeginPopup("AnimationEventReceiverComponentOptions"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    pendingRemovals.RemoveAnimationEventReceiverComponent = true;
                ImGui::EndPopup();
            }

            if (receiverOpen)
            {
                ImGui::Checkbox("Enabled", &animationEventReceiver->Enabled);
                TrackInteractiveMutation(undoService, "Edit Animation Event Receiver Enabled");

                ImGui::Text("Received Events This Frame: %u", static_cast<uint32_t>(animationEventReceiver->RuntimeDispatchedEvents.size()));
                for (const auto& eventMessage : animationEventReceiver->RuntimeDispatchedEvents)
                {
                    ImGui::BulletText("%s | string='%s' float=%.3f int=%d bool=%s",
                                      eventMessage.Name.c_str(),
                                      eventMessage.StringPayload.c_str(),
                                      eventMessage.FloatPayload,
                                      eventMessage.IntegerPayload,
                                      eventMessage.BooleanPayload ? "true" : "false");
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

        if (auto* audioListener = registry.try_get<AudioListener2DComponent>(selectedEntity))
        {
            const bool listenerOpen = ImGui::TreeNodeEx("Audio Listener 2D", ImGuiTreeNodeFlags_DefaultOpen);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                ImGui::OpenPopup("AudioListener2DComponentOptions");
            ImGui::SameLine();
            if (ImGui::Button("...##AudioListener2DComponentOptionsButton"))
                ImGui::OpenPopup("AudioListener2DComponentOptions");

            if (ImGui::BeginPopup("AudioListener2DComponentOptions"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    pendingRemovals.RemoveAudioListener2DComponent = true;
                ImGui::EndPopup();
            }

            if (listenerOpen)
            {
                ImGui::Checkbox("Enabled", &audioListener->Enabled);
                TrackInteractiveMutation(undoService, "Edit Audio Listener 2D Enabled");
                ImGui::Checkbox("Use Primary Camera Position", &audioListener->UsePrimaryCameraPosition);
                TrackInteractiveMutation(undoService, "Edit Audio Listener 2D Camera Follow");
                ImGui::TextDisabled("When enabled, spatial 2D sources attenuate and pan relative to this listener.");
                ImGui::TreePop();
            }
        }

        if (auto* grid2D = registry.try_get<Grid2DComponent>(selectedEntity))
        {
            const bool grid2DOpen = ImGui::TreeNodeEx("Grid 2D", ImGuiTreeNodeFlags_DefaultOpen);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                ImGui::OpenPopup("Grid2DComponentOptions");
            ImGui::SameLine();
            if (ImGui::Button("...##Grid2DComponentOptionsButton"))
                ImGui::OpenPopup("Grid2DComponentOptions");

            if (ImGui::BeginPopup("Grid2DComponentOptions"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    pendingRemovals.RemoveGrid2DComponent = true;
                ImGui::EndPopup();
            }

            if (grid2DOpen)
            {
                ImGui::DragFloat2("Cell Size", &grid2D->CellSize.x, 0.05f, 0.001f, 100.0f);
                TrackInteractiveMutation(undoService, "Edit Grid2D Cell Size");
                ImGui::DragFloat2("Cell Gap", &grid2D->CellGap.x, 0.01f, 0.0f, 10.0f);
                TrackInteractiveMutation(undoService, "Edit Grid2D Cell Gap");

                ImGui::Separator();
                if (ImGui::Button("Add Layer"))
                {
                    if (scene && undoService)
                    {
                        (void)undoService->ExecuteSceneMutation("Add Tilemap Layer", [&](Scene& mutableScene) {
                            const auto children = mutableScene.GetChildren(selectedEntity);
                            const int32_t layerNumber = static_cast<int32_t>(children.size()) + 1;
                            const std::string layerName = "Layer " + std::to_string(layerNumber);
                            const int32_t renderOrder = static_cast<int32_t>(children.size()) * 10;

                            entt::entity layerEntity = mutableScene.CreateEntity(layerName);
                            mutableScene.SetParent(layerEntity, selectedEntity);

                            auto& layer = mutableScene.GetRegistry().emplace<TilemapLayerComponent>(layerEntity);
                            layer.RenderOrder = renderOrder;
                            layer.EnsureStorage();
                            return true;
                        });
                    }
                    else if (scene)
                    {
                        const auto children = scene->GetChildren(selectedEntity);
                        const int32_t layerNumber = static_cast<int32_t>(children.size()) + 1;
                        const std::string layerName = "Layer " + std::to_string(layerNumber);
                        const int32_t renderOrder = static_cast<int32_t>(children.size()) * 10;

                        entt::entity layerEntity = scene->CreateEntity(layerName);
                        scene->SetParent(layerEntity, selectedEntity);

                        auto& layer = registry.emplace<TilemapLayerComponent>(layerEntity);
                        layer.RenderOrder = renderOrder;
                        layer.EnsureStorage();
                    }
                }

                if (scene)
                {
                    const auto children = scene->GetChildren(selectedEntity);
                    if (!children.empty())
                    {
                        ImGui::TextDisabled("Layers: %d", static_cast<int>(children.size()));
                        for (entt::entity child : children)
                        {
                            if (!registry.all_of<TilemapLayerComponent>(child))
                                continue;
                            const auto* tag = registry.try_get<TagComponent>(child);
                            const auto* childLayer = registry.try_get<TilemapLayerComponent>(child);
                            if (tag && childLayer)
                                ImGui::BulletText("%s (order %d)", tag->Tag.c_str(), childLayer->RenderOrder);
                        }
                    }
                }

                ImGui::TreePop();
            }
        }

        if (auto* tilemapLayer = registry.try_get<TilemapLayerComponent>(selectedEntity))
        {
            tilemapLayer->EnsureStorage();
            const bool layerOpen = ImGui::TreeNodeEx("Tilemap Layer", ImGuiTreeNodeFlags_DefaultOpen);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                ImGui::OpenPopup("TilemapLayerComponentOptions");
            ImGui::SameLine();
            if (ImGui::Button("...##TilemapLayerComponentOptionsButton"))
                ImGui::OpenPopup("TilemapLayerComponentOptions");

            if (ImGui::BeginPopup("TilemapLayerComponentOptions"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    pendingRemovals.RemoveTilemapLayerComponent = true;
                ImGui::EndPopup();
            }

            if (layerOpen)
            {
                glm::ivec2 gridSize = tilemapLayer->GridSize;
                if (ImGui::DragInt2("Grid Size", &gridSize.x, 1.0f, 1, 4096))
                {
                    gridSize = glm::ivec2(std::max(1, gridSize.x), std::max(1, gridSize.y));
                    if (gridSize != tilemapLayer->GridSize)
                        tilemapLayer->ResizeGrid(gridSize);
                }
                TrackInteractiveMutation(undoService, "Edit TilemapLayer Grid Size");

                ImGui::DragInt("Render Order", &tilemapLayer->RenderOrder);
                TrackInteractiveMutation(undoService, "Edit TilemapLayer Render Order");

                ImGui::Checkbox("Collision Enabled", &tilemapLayer->CollisionEnabled);
                TrackInteractiveMutation(undoService, "Edit TilemapLayer Collision");

                const int32_t tileCount = tilemapLayer->GetCellCount();
                int32_t nonEmptyCount = 0;
                for (uint32_t t : tilemapLayer->Tiles)
                {
                    if (t != 0)
                        ++nonEmptyCount;
                }
                ImGui::TextDisabled("Cells: %d  |  Painted: %d  |  Tile types: %d",
                                    tileCount, nonEmptyCount,
                                    static_cast<int>(tilemapLayer->TileTable.size()));

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
                auto assignAudioClipKey = [&](const std::string& key) {
                    if (key == audioSource->AudioClipKey)
                        return;
                    if (audioSource->RuntimeVoiceId != 0)
                        Audio::AudioEngine::GetInstance().Stop(audioSource->RuntimeVoiceId);
                    audioSource->AudioClipKey = key;
                    audioSource->RuntimeVoiceId = 0;
                    audioSource->RuntimePlaybackStarted = false;
                };

                const std::string clipLabel = audioSource->AudioClipKey.empty()
                    ? std::string("None")
                    : EditorAssetNaming::GetAssetDisplayNameFromAssetKey(audioSource->AudioClipKey);

                ImGui::Text("Clip");
                ImGui::SameLine(80);
                ImGui::Button((clipLabel + "##AudioClip").c_str(), ImVec2(ImGui::GetContentRegionAvail().x - 90.0f, 0.0f));
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(audioPayloadId))
                    {
                        const char* key = static_cast<const char*>(payload->Data);
                        if (key && key[0])
                            assignAudioClipKey(key);
                    }
                    ImGui::EndDragDropTarget();
                }

                ImGui::SameLine();
                if (ImGui::Button("...##AudioClipPicker"))
                    ImGui::OpenPopup("AudioClipPickerPopup");

                if (ImGui::BeginPopup("AudioClipPickerPopup"))
                {
                    if (ImGui::Selectable("None##AudioClipPickerNone"))
                    {
                        assignAudioClipKey({});
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::Separator();

                    const std::vector<std::string> audioClipKeys = BuildAudioClipPickerKeys();
                    for (const auto& key : audioClipKeys)
                    {
                        const bool isSelected = (audioSource->AudioClipKey == key);
                        const std::string display = EditorAssetNaming::GetAssetDisplayNameFromAssetKey(key);
                        if (ImGui::Selectable((display + "##AudioClipPicker_" + key).c_str(), isSelected))
                        {
                            assignAudioClipKey(key);
                            ImGui::CloseCurrentPopup();
                        }
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                            ImGui::SetTooltip("%s", key.c_str());
                    }
                    ImGui::EndPopup();
                }

                if (!audioSource->AudioClipKey.empty())
                {
                    ImGui::SameLine();
                    if (ImGui::Button("Clear##AudioClip"))
                        assignAudioClipKey({});
                }

                ImGui::Checkbox("Play On Start", &audioSource->PlayOnStart);
                TrackInteractiveMutation(undoService, "Edit Audio Play On Start");
                ImGui::Checkbox("Loop", &audioSource->Loop);
                TrackInteractiveMutation(undoService, "Edit Audio Loop");
                ImGui::Checkbox("Muted", &audioSource->Muted);
                TrackInteractiveMutation(undoService, "Edit Audio Muted");
                ImGui::SliderFloat("Volume", &audioSource->Volume, 0.0f, 2.0f, "%.2f");
                TrackInteractiveMutation(undoService, "Edit Audio Volume");
                ImGui::SliderFloat("Pitch", &audioSource->Pitch, 0.1f, 4.0f, "%.2f");
                audioSource->Pitch = std::max(0.01f, audioSource->Pitch);
                TrackInteractiveMutation(undoService, "Edit Audio Pitch");

                int playbackSpaceIndex = static_cast<int>(audioSource->Space);
                const char* playbackSpaceNames[] = { "Global", "Spatial 2D" };
                if (ImGui::Combo("Playback Space", &playbackSpaceIndex, playbackSpaceNames, 2))
                    audioSource->Space = static_cast<AudioSourceComponent::PlaybackSpace>(playbackSpaceIndex);
                TrackInteractiveMutation(undoService, "Edit Audio Playback Space");

                std::array<char, 128> mixerGroupBuffer{};
                std::snprintf(mixerGroupBuffer.data(), mixerGroupBuffer.size(), "%s", audioSource->MixerGroup.c_str());
                if (ImGui::InputText("Mixer Group", mixerGroupBuffer.data(), mixerGroupBuffer.size()))
                    audioSource->MixerGroup = mixerGroupBuffer.data();
                if (audioSource->MixerGroup.empty())
                    audioSource->MixerGroup = "SFX";
                TrackInteractiveMutation(undoService, "Edit Audio Mixer Group");

                if (audioSource->Space == AudioSourceComponent::PlaybackSpace::Spatial2D)
                {
                    ImGui::DragFloat("Min Distance", &audioSource->SpatialMinDistance, 0.01f, 0.001f, 10000.0f, "%.3f");
                    audioSource->SpatialMinDistance = std::max(0.001f, audioSource->SpatialMinDistance);
                    TrackInteractiveMutation(undoService, "Edit Audio Spatial Min Distance");

                    ImGui::DragFloat("Max Distance", &audioSource->SpatialMaxDistance, 0.05f, 0.001f, 10000.0f, "%.3f");
                    audioSource->SpatialMaxDistance = std::max(audioSource->SpatialMinDistance, audioSource->SpatialMaxDistance);
                    TrackInteractiveMutation(undoService, "Edit Audio Spatial Max Distance");

                    ImGui::DragFloat("Rolloff Exponent", &audioSource->SpatialRolloffExponent, 0.01f, 0.01f, 16.0f, "%.2f");
                    audioSource->SpatialRolloffExponent = std::max(0.01f, audioSource->SpatialRolloffExponent);
                    TrackInteractiveMutation(undoService, "Edit Audio Spatial Rolloff Exponent");

                    ImGui::SliderFloat("Stereo Pan Strength", &audioSource->StereoPanStrength, 0.0f, 1.0f, "%.2f");
                    audioSource->StereoPanStrength = std::clamp(audioSource->StereoPanStrength, 0.0f, 1.0f);
                    TrackInteractiveMutation(undoService, "Edit Audio Spatial Pan Strength");

                    std::array<char, 256> attenuationCurveBuffer{};
                    std::snprintf(attenuationCurveBuffer.data(), attenuationCurveBuffer.size(), "%s", audioSource->AttenuationCurveKey.c_str());
                    if (ImGui::InputText("Attenuation Curve Key", attenuationCurveBuffer.data(), attenuationCurveBuffer.size()))
                        audioSource->AttenuationCurveKey = attenuationCurveBuffer.data();
                    TrackInteractiveMutation(undoService, "Edit Audio Attenuation Curve Key");
                }

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
                                    audioSource->Loop,
                                    audioSource->MixerGroup,
                                    0.0f,
                                    audioSource->Pitch);
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
                if (rigidbody2D->Type == Rigidbody2DComponent::BodyType::Kinematic)
                {
                    ImGui::TextWrapped(
                        "Note: Box2D kinematic bodies do not generate physical collision response against static/kinematic bodies "
                        "(including static tilemap colliders). Use Dynamic + Gravity Scale 0 for moving collidable platforms.");
                }

                bool freezeRotation = rigidbody2D->IsRotationLocked();
                ImGui::TextDisabled("Constraints");
                ImGui::Checkbox("Freeze Position X", &rigidbody2D->FreezePositionX);
                TrackInteractiveMutation(undoService, "Edit Rigidbody2D Freeze Position X");
                ImGui::Checkbox("Freeze Position Y", &rigidbody2D->FreezePositionY);
                TrackInteractiveMutation(undoService, "Edit Rigidbody2D Freeze Position Y");
                if (ImGui::Checkbox("Freeze Rotation", &freezeRotation))
                {
                    rigidbody2D->FixedRotation = freezeRotation;
                }
                TrackInteractiveMutation(undoService, "Edit Rigidbody2D Freeze Rotation");
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

                ImGui::Separator();
                ImGui::TextDisabled("Runtime Diagnostics");
                if (!scene)
                {
                    ImGui::TextDisabled("Scene unavailable.");
                }
                else
                {
                    const Physics2DWorld* physicsWorld = scene->GetPhysics2DWorld();
                    if (!physicsWorld)
                    {
                        ImGui::TextDisabled("Physics world is not initialized.");
                    }
                    else
                    {
                        const Physics2DDiagnostics& worldDiagnostics = physicsWorld->GetDiagnostics();
                        ImGui::Text("Bodies: %d (Awake: %d, Sleeping: %d)",
                                    worldDiagnostics.BodyCount,
                                    worldDiagnostics.AwakeBodyCount,
                                    worldDiagnostics.SleepingBodyCount);
                        ImGui::Text("Contacts: %d | Penetrating Points: %d",
                                    worldDiagnostics.ContactPairCount,
                                    worldDiagnostics.PenetratingContactPointCount);
                        ImGui::Text("Max Penetration Depth: %.5f", worldDiagnostics.MaxPenetrationDepth);

                        Physics2DBodyDiagnostics bodyDiagnostics{};
                        if (physicsWorld->TryGetBodyDiagnostics(selectedEntity, bodyDiagnostics))
                        {
                            ImGui::Text("Body Awake: %s", bodyDiagnostics.IsAwake ? "Yes" : "No");
                            ImGui::Text("Body Contact Pairs: %d", bodyDiagnostics.ContactPairCount);
                            ImGui::Text("Body Penetrating Points: %d", bodyDiagnostics.PenetratingContactPointCount);
                            ImGui::Text("Body Max Penetration: %.5f", bodyDiagnostics.MaxPenetrationDepth);
                        }
                        else
                        {
                            ImGui::TextDisabled("No active runtime body diagnostics for selected entity.");
                        }
                    }
                }

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
                ImGui::TextDisabled("Directional light intensity is global (no distance falloff).");
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

        if (auto* uiImage = registry.try_get<UIImageComponent>(selectedEntity))
        {
            const bool uiImageOpen = ImGui::TreeNodeEx("UI Image", ImGuiTreeNodeFlags_DefaultOpen);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                ImGui::OpenPopup("UIImageComponentOptions");
            ImGui::SameLine();
            if (ImGui::Button("...##UIImageComponentOptionsButton"))
                ImGui::OpenPopup("UIImageComponentOptions");

            if (ImGui::BeginPopup("UIImageComponentOptions"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    pendingRemovals.RemoveUIImageComponent = true;
                ImGui::EndPopup();
            }

            if (uiImageOpen)
            {
                const auto assignUiImageTextureKey = [&](const std::string& key) {
                    if (undoService)
                    {
                        (void)undoService->ExecuteSceneMutation(key.empty() ? "Clear UI Image Texture" : "Assign UI Image Texture", [&](Scene& mutableScene) {
                            auto& mutableRegistry = mutableScene.GetRegistry();
                            auto* mutableSprite = mutableRegistry.try_get<SpriteComponent>(selectedEntity);
                            if (!mutableSprite)
                                mutableSprite = &mutableRegistry.emplace<SpriteComponent>(selectedEntity);
                            mutableSprite->TextureKey = key;
                            mutableSprite->CachedTexture.reset();
                            mutableSprite->TextureLoadAttempted = false;
                            return true;
                        });
                    }
                    else
                    {
                        auto* sprite = registry.try_get<SpriteComponent>(selectedEntity);
                        if (!sprite)
                            sprite = &registry.emplace<SpriteComponent>(selectedEntity);
                        sprite->TextureKey = key;
                        sprite->CachedTexture.reset();
                        sprite->TextureLoadAttempted = false;
                    }
                };

                auto* uiImageSprite = registry.try_get<SpriteComponent>(selectedEntity);
                const std::string imageTextureLabel = (uiImageSprite && !uiImageSprite->TextureKey.empty())
                    ? EditorAssetNaming::GetAssetDisplayNameFromAssetKey(uiImageSprite->TextureKey)
                    : std::string("None (White Quad)");
                ImGui::Text("Image");
                ImGui::SameLine(80);
                ImGui::Button((imageTextureLabel + "##UIImageTexture").c_str(), ImVec2(ImGui::GetContentRegionAvail().x - 60.0f, 0.0f));
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(texturePayloadId))
                    {
                        const char* key = static_cast<const char*>(payload->Data);
                        if (key && key[0])
                            assignUiImageTextureKey(key);
                    }
                    ImGui::EndDragDropTarget();
                }
                if (uiImageSprite && !uiImageSprite->TextureKey.empty())
                {
                    ImGui::SameLine();
                    if (ImGui::Button("Clear##UIImageTexture"))
                        assignUiImageTextureKey({});
                }

                ImGui::Checkbox("Raycast Target", &uiImage->RaycastTarget);
                TrackInteractiveMutation(undoService, "Edit UIImage Raycast Target");
                ImGui::TreePop();
            }
        }

        if (auto* uiPanel = registry.try_get<UIPanelComponent>(selectedEntity))
        {
            const bool uiPanelOpen = ImGui::TreeNodeEx("UI Panel", ImGuiTreeNodeFlags_DefaultOpen);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                ImGui::OpenPopup("UIPanelComponentOptions");
            ImGui::SameLine();
            if (ImGui::Button("...##UIPanelComponentOptionsButton"))
                ImGui::OpenPopup("UIPanelComponentOptions");

            if (ImGui::BeginPopup("UIPanelComponentOptions"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    pendingRemovals.RemoveUIPanelComponent = true;
                ImGui::EndPopup();
            }

            if (uiPanelOpen)
            {
                const auto assignUiPanelTextureKey = [&](const std::string& key) {
                    if (undoService)
                    {
                        (void)undoService->ExecuteSceneMutation(key.empty() ? "Clear UI Panel Texture" : "Assign UI Panel Texture", [&](Scene& mutableScene) {
                            auto& mutableRegistry = mutableScene.GetRegistry();
                            auto* mutableSprite = mutableRegistry.try_get<SpriteComponent>(selectedEntity);
                            if (!mutableSprite)
                                mutableSprite = &mutableRegistry.emplace<SpriteComponent>(selectedEntity);
                            mutableSprite->TextureKey = key;
                            mutableSprite->CachedTexture.reset();
                            mutableSprite->TextureLoadAttempted = false;
                            return true;
                        });
                    }
                    else
                    {
                        auto* sprite = registry.try_get<SpriteComponent>(selectedEntity);
                        if (!sprite)
                            sprite = &registry.emplace<SpriteComponent>(selectedEntity);
                        sprite->TextureKey = key;
                        sprite->CachedTexture.reset();
                        sprite->TextureLoadAttempted = false;
                    }
                };

                auto* panelSprite = registry.try_get<SpriteComponent>(selectedEntity);
                const std::string panelTextureLabel = (panelSprite && !panelSprite->TextureKey.empty())
                    ? EditorAssetNaming::GetAssetDisplayNameFromAssetKey(panelSprite->TextureKey)
                    : std::string("None (Solid Color)");
                ImGui::Text("Background");
                ImGui::SameLine(80);
                ImGui::Button((panelTextureLabel + "##UIPanelTexture").c_str(), ImVec2(ImGui::GetContentRegionAvail().x - 60.0f, 0.0f));
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(texturePayloadId))
                    {
                        const char* key = static_cast<const char*>(payload->Data);
                        if (key && key[0])
                            assignUiPanelTextureKey(key);
                    }
                    ImGui::EndDragDropTarget();
                }
                if (panelSprite && !panelSprite->TextureKey.empty())
                {
                    ImGui::SameLine();
                    if (ImGui::Button("Clear##UIPanelTexture"))
                        assignUiPanelTextureKey({});
                }

                ImGui::ColorEdit4("Background Color##UIPanel", &uiPanel->BackgroundColor.r);
                TrackInteractiveMutation(undoService, "Edit UIPanel Background Color");

                ImGui::Checkbox("Use Sprite Texture##UIPanel", &uiPanel->UseSpriteTexture);
                TrackInteractiveMutation(undoService, "Edit UIPanel Use Sprite Texture");

                ImGui::Checkbox("Raycast Target##UIPanel", &uiPanel->RaycastTarget);
                TrackInteractiveMutation(undoService, "Edit UIPanel Raycast Target");
                ImGui::TreePop();
            }
        }

        if (auto* uiText = registry.try_get<UITextComponent>(selectedEntity))
        {
            const bool uiTextOpen = ImGui::TreeNodeEx("UI Text", ImGuiTreeNodeFlags_DefaultOpen);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                ImGui::OpenPopup("UITextComponentOptions");
            ImGui::SameLine();
            if (ImGui::Button("...##UITextComponentOptionsButton"))
                ImGui::OpenPopup("UITextComponentOptions");

            if (ImGui::BeginPopup("UITextComponentOptions"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    pendingRemovals.RemoveUITextComponent = true;
                ImGui::EndPopup();
            }

            if (uiTextOpen)
            {
                static entt::entity uiTextEditEntity = entt::null;
                static std::array<char, 2048> uiTextValueBuffer{};
                static std::array<char, 512> uiTextFontPathBuffer{};
                if (uiTextEditEntity != selectedEntity)
                {
                    uiTextEditEntity = selectedEntity;
                    std::snprintf(uiTextValueBuffer.data(), uiTextValueBuffer.size(), "%s", uiText->Text.c_str());
                    std::snprintf(uiTextFontPathBuffer.data(), uiTextFontPathBuffer.size(), "%s", uiText->FontFilePath.c_str());
                }

                ImGui::InputTextMultiline("Text Value##UIText", uiTextValueBuffer.data(), uiTextValueBuffer.size(), ImVec2(-1.0f, 84.0f));
                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    uiText->Text = uiTextValueBuffer.data();
                    if (undoService)
                        (void)undoService->CommitInteractiveSceneMutation("Edit UI Text Value");
                }
                else if (ImGui::IsItemActivated() && undoService)
                {
                    undoService->BeginInteractiveSceneMutation();
                }

                ImGui::InputText("Font File Path##UIText", uiTextFontPathBuffer.data(), uiTextFontPathBuffer.size());
                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    uiText->FontFilePath = uiTextFontPathBuffer.data();
                    uiText->CachedFont.reset();
                    uiText->FontLoadAttempted = false;
                    if (undoService)
                        (void)undoService->CommitInteractiveSceneMutation("Edit UI Font File Path");
                }
                else if (ImGui::IsItemActivated() && undoService)
                {
                    undoService->BeginInteractiveSceneMutation();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Example: Assets/Fonts/YourFont.ttf");

                const std::string uiFontLabel = uiText->FontFilePath.empty()
                    ? std::string("None")
                    : EditorAssetNaming::GetAssetDisplayNameFromAssetKey(uiText->FontFilePath);
                ImGui::Text("Font Asset");
                ImGui::SameLine(80);
                ImGui::Button((uiFontLabel + "##UITextFontAsset").c_str(), ImVec2(ImGui::GetContentRegionAvail().x - 60.0f, 0.0f));
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(fontPayloadId))
                    {
                        const char* key = static_cast<const char*>(payload->Data);
                        if (key && key[0])
                        {
                            uiText->FontFilePath = key;
                            std::snprintf(uiTextFontPathBuffer.data(), uiTextFontPathBuffer.size(), "%s", uiText->FontFilePath.c_str());
                            uiText->CachedFont.reset();
                            uiText->FontLoadAttempted = false;
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                if (!uiText->FontFilePath.empty())
                {
                    ImGui::SameLine();
                    if (ImGui::Button("Clear##UITextFontAsset"))
                    {
                        uiText->FontFilePath.clear();
                        uiTextFontPathBuffer[0] = '\0';
                        uiText->CachedFont.reset();
                        uiText->FontLoadAttempted = false;
                    }
                }

                if (ImGui::DragFloat("Font Size##UIText", &uiText->FontSize, 1.0f, 4.0f, 512.0f))
                    uiText->FontSize = std::max(4.0f, uiText->FontSize);
                TrackInteractiveMutation(undoService, "Edit UI Font Size");

                ImGui::ColorEdit4("Color##UIText", &uiText->Color.r);
                TrackInteractiveMutation(undoService, "Edit UI Text Color");

                ImGui::Checkbox("Raycast Target", &uiText->RaycastTarget);
                TrackInteractiveMutation(undoService, "Edit UIText Raycast Target");
                ImGui::TreePop();
            }
        }

        if (auto* uiButton = registry.try_get<UIButtonComponent>(selectedEntity))
        {
            const bool uiButtonOpen = ImGui::TreeNodeEx("UI Button", ImGuiTreeNodeFlags_DefaultOpen);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                ImGui::OpenPopup("UIButtonComponentOptions");
            ImGui::SameLine();
            if (ImGui::Button("...##UIButtonComponentOptionsButton"))
                ImGui::OpenPopup("UIButtonComponentOptions");

            if (ImGui::BeginPopup("UIButtonComponentOptions"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    pendingRemovals.RemoveUIButtonComponent = true;
                ImGui::EndPopup();
            }

            if (uiButtonOpen)
            {
                ImGui::Checkbox("Interactable", &uiButton->Interactable);
                TrackInteractiveMutation(undoService, "Edit UIButton Interactable");
                ImGui::Checkbox("Use State Colors", &uiButton->UseStateColors);
                TrackInteractiveMutation(undoService, "Edit UIButton Use State Colors");
                if (uiButton->UseStateColors)
                {
                    ImGui::ColorEdit4("Normal Color##UIButton", &uiButton->NormalColor.r);
                    TrackInteractiveMutation(undoService, "Edit UIButton Normal Color");
                    ImGui::ColorEdit4("Hovered Color##UIButton", &uiButton->HoveredColor.r);
                    TrackInteractiveMutation(undoService, "Edit UIButton Hovered Color");
                    ImGui::ColorEdit4("Pressed Color##UIButton", &uiButton->PressedColor.r);
                    TrackInteractiveMutation(undoService, "Edit UIButton Pressed Color");
                    ImGui::ColorEdit4("Disabled Color##UIButton", &uiButton->DisabledColor.r);
                    TrackInteractiveMutation(undoService, "Edit UIButton Disabled Color");
                }
                std::array<char, 256> onClickEventBuffer{};
                std::snprintf(onClickEventBuffer.data(), onClickEventBuffer.size(), "%s", uiButton->OnClickEvent.c_str());
                if (ImGui::InputText("On Click Event", onClickEventBuffer.data(), onClickEventBuffer.size()))
                    uiButton->OnClickEvent = onClickEventBuffer.data();
                TrackInteractiveMutation(undoService, "Edit UIButton OnClick Event");
                std::array<char, 256> onHoverEnterEventBuffer{};
                std::snprintf(onHoverEnterEventBuffer.data(), onHoverEnterEventBuffer.size(), "%s", uiButton->OnHoverEnterEvent.c_str());
                if (ImGui::InputText("On Hover Enter Event", onHoverEnterEventBuffer.data(), onHoverEnterEventBuffer.size()))
                    uiButton->OnHoverEnterEvent = onHoverEnterEventBuffer.data();
                TrackInteractiveMutation(undoService, "Edit UIButton OnHoverEnter Event");
                std::array<char, 256> onHoverExitEventBuffer{};
                std::snprintf(onHoverExitEventBuffer.data(), onHoverExitEventBuffer.size(), "%s", uiButton->OnHoverExitEvent.c_str());
                if (ImGui::InputText("On Hover Exit Event", onHoverExitEventBuffer.data(), onHoverExitEventBuffer.size()))
                    uiButton->OnHoverExitEvent = onHoverExitEventBuffer.data();
                TrackInteractiveMutation(undoService, "Edit UIButton OnHoverExit Event");
                std::array<char, 256> onPressedEventBuffer{};
                std::snprintf(onPressedEventBuffer.data(), onPressedEventBuffer.size(), "%s", uiButton->OnPressedEvent.c_str());
                if (ImGui::InputText("On Pressed Event", onPressedEventBuffer.data(), onPressedEventBuffer.size()))
                    uiButton->OnPressedEvent = onPressedEventBuffer.data();
                TrackInteractiveMutation(undoService, "Edit UIButton OnPressed Event");
                ImGui::TreePop();
            }
        }

        if (auto* uiSlider = registry.try_get<UISliderComponent>(selectedEntity))
        {
            const bool uiSliderOpen = ImGui::TreeNodeEx("UI Slider", ImGuiTreeNodeFlags_DefaultOpen);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                ImGui::OpenPopup("UISliderComponentOptions");
            ImGui::SameLine();
            if (ImGui::Button("...##UISliderComponentOptionsButton"))
                ImGui::OpenPopup("UISliderComponentOptions");

            if (ImGui::BeginPopup("UISliderComponentOptions"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    pendingRemovals.RemoveUISliderComponent = true;
                ImGui::EndPopup();
            }

            if (uiSliderOpen)
            {
                const bool sliderUsesVisualChildren = SliderHasVisualChildren(registry, selectedEntity);
                ImGui::Checkbox("Interactable", &uiSlider->Interactable);
                TrackInteractiveMutation(undoService, "Edit UISlider Interactable");
                ImGui::DragFloat("Min Value", &uiSlider->MinValue, 0.1f);
                TrackInteractiveMutation(undoService, "Edit UISlider Min Value");
                ImGui::DragFloat("Max Value", &uiSlider->MaxValue, 0.1f);
                if (uiSlider->MaxValue < uiSlider->MinValue)
                    uiSlider->MaxValue = uiSlider->MinValue;
                TrackInteractiveMutation(undoService, "Edit UISlider Max Value");
                ImGui::SliderFloat("Value", &uiSlider->Value, uiSlider->MinValue, uiSlider->MaxValue);
                uiSlider->Value = std::clamp(uiSlider->Value, uiSlider->MinValue, uiSlider->MaxValue);
                TrackInteractiveMutation(undoService, "Edit UISlider Value");
                if (sliderUsesVisualChildren)
                {
                    ImGui::TextWrapped("Unity-style slider visuals are authored on child entities:");
                    ImGui::BulletText("Slider Background");
                    ImGui::BulletText("Slider Fill");
                    ImGui::BulletText("Slider Handle");
                    if (ImGui::TreeNodeEx("Fallback Colors (Used Only Without Visual Children)"))
                    {
                        ImGui::ColorEdit4("Background Color##UISlider", &uiSlider->BackgroundColor.r);
                        TrackInteractiveMutation(undoService, "Edit UISlider Background Color");
                        ImGui::ColorEdit4("Fill Color##UISlider", &uiSlider->FillColor.r);
                        TrackInteractiveMutation(undoService, "Edit UISlider Fill Color");
                        ImGui::ColorEdit4("Handle Color##UISlider", &uiSlider->HandleColor.r);
                        TrackInteractiveMutation(undoService, "Edit UISlider Handle Color");
                        ImGui::TreePop();
                    }
                }
                else
                {
                    ImGui::ColorEdit4("Background Color##UISlider", &uiSlider->BackgroundColor.r);
                    TrackInteractiveMutation(undoService, "Edit UISlider Background Color");
                    ImGui::ColorEdit4("Fill Color##UISlider", &uiSlider->FillColor.r);
                    TrackInteractiveMutation(undoService, "Edit UISlider Fill Color");
                    ImGui::ColorEdit4("Handle Color##UISlider", &uiSlider->HandleColor.r);
                    TrackInteractiveMutation(undoService, "Edit UISlider Handle Color");
                }
                ImGui::DragFloat("Handle Width##UISlider", &uiSlider->HandleWidth, 0.5f, 1.0f, 4096.0f);
                uiSlider->HandleWidth = std::max(1.0f, uiSlider->HandleWidth);
                TrackInteractiveMutation(undoService, "Edit UISlider Handle Width");
                ImGui::DragFloat("Handle Height Multiplier##UISlider", &uiSlider->HandleHeightMultiplier, 0.01f, 0.1f, 8.0f);
                uiSlider->HandleHeightMultiplier = std::max(0.1f, uiSlider->HandleHeightMultiplier);
                TrackInteractiveMutation(undoService, "Edit UISlider Handle Height Multiplier");
                ImGui::Checkbox("Show Handle##UISlider", &uiSlider->ShowHandle);
                TrackInteractiveMutation(undoService, "Edit UISlider Show Handle");
                std::array<char, 256> onValueChangedEventBuffer{};
                std::snprintf(onValueChangedEventBuffer.data(), onValueChangedEventBuffer.size(), "%s", uiSlider->OnValueChangedEvent.c_str());
                if (ImGui::InputText("On Value Changed Event", onValueChangedEventBuffer.data(), onValueChangedEventBuffer.size()))
                    uiSlider->OnValueChangedEvent = onValueChangedEventBuffer.data();
                TrackInteractiveMutation(undoService, "Edit UISlider OnValueChanged Event");
                ImGui::TreePop();
            }
        }

        // -----------------------------------------------------------------
        // Particle Emitter
        // -----------------------------------------------------------------
        if (auto* particleEmitter = registry.try_get<ParticleEmitterComponent>(selectedEntity))
        {
            const bool particleOpen = ImGui::TreeNodeEx("Particle Emitter", ImGuiTreeNodeFlags_DefaultOpen);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                ImGui::OpenPopup("ParticleEmitterComponentOptions");
            ImGui::SameLine();
            if (ImGui::Button("...##ParticleEmitterComponentOptionsButton"))
                ImGui::OpenPopup("ParticleEmitterComponentOptions");

            if (ImGui::BeginPopup("ParticleEmitterComponentOptions"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    pendingRemovals.RemoveParticleEmitterComponent = true;
                ImGui::EndPopup();
            }

            if (particleOpen)
            {
                // -- Playback controls (work in both edit and play mode) --
                {
                    const bool isPlaying = particleEmitter->Playing && !particleEmitter->Paused;
                    const bool isPaused  = particleEmitter->Playing && particleEmitter->Paused;

                    if (!particleEmitter->Playing)
                    {
                        if (ImGui::Button("Play##ParticleEmitter"))
                            ParticleEmitterPlay(*particleEmitter);
                    }
                    else
                    {
                        if (ImGui::Button("Stop##ParticleEmitter"))
                            ParticleEmitterStop(*particleEmitter, true);
                    }

                    ImGui::SameLine();
                    if (!isPaused)
                    {
                        if (ImGui::Button("Pause##ParticleEmitter"))
                            ParticleEmitterPause(*particleEmitter);
                    }
                    else
                    {
                        if (ImGui::Button("Resume##ParticleEmitter"))
                            ParticleEmitterResume(*particleEmitter);
                    }

                    if (particleEmitter->RuntimeState)
                    {
                        ImGui::SameLine();
                        ImGui::Text("Alive: %u", particleEmitter->RuntimeState->AliveCount);
                    }
                }

                ImGui::Separator();

                // -- Emission --
                if (ImGui::TreeNodeEx("Emission##ParticleEmitter", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::DragFloat("Spawn Rate", &particleEmitter->SpawnRate, 0.5f, 0.0f, 10000.0f);
                    particleEmitter->SpawnRate = std::max(0.0f, particleEmitter->SpawnRate);
                    TrackInteractiveMutation(undoService, "Edit Particle Spawn Rate");

                    ImGui::DragFloat("Lifetime Min", &particleEmitter->LifetimeMin, 0.05f, 0.01f, 100.0f);
                    particleEmitter->LifetimeMin = std::max(0.01f, particleEmitter->LifetimeMin);
                    TrackInteractiveMutation(undoService, "Edit Particle Lifetime Min");

                    ImGui::DragFloat("Lifetime Max", &particleEmitter->LifetimeMax, 0.05f, 0.01f, 100.0f);
                    particleEmitter->LifetimeMax = std::max(particleEmitter->LifetimeMin, particleEmitter->LifetimeMax);
                    TrackInteractiveMutation(undoService, "Edit Particle Lifetime Max");

                    ImGui::Checkbox("Looping", &particleEmitter->Looping);
                    TrackInteractiveMutation(undoService, "Edit Particle Looping");

                    if (!particleEmitter->Looping)
                    {
                        ImGui::DragFloat("Duration", &particleEmitter->Duration, 0.1f, 0.1f, 600.0f);
                        particleEmitter->Duration = std::max(0.1f, particleEmitter->Duration);
                        TrackInteractiveMutation(undoService, "Edit Particle Duration");
                    }

                    ImGui::Checkbox("Play On Start", &particleEmitter->PlayOnStart);
                    TrackInteractiveMutation(undoService, "Edit Particle Play On Start");

                    ImGui::Checkbox("Burst Enabled", &particleEmitter->BurstEnabled);
                    TrackInteractiveMutation(undoService, "Edit Particle Burst Enabled");

                    if (particleEmitter->BurstEnabled)
                    {
                        int burstCount = static_cast<int>(particleEmitter->BurstCount);
                        ImGui::DragInt("Burst Count", &burstCount, 1, 1, static_cast<int>(ParticleEmitterComponent::kMaxParticlesCap));
                        particleEmitter->BurstCount = static_cast<uint32_t>(std::max(1, burstCount));
                        TrackInteractiveMutation(undoService, "Edit Particle Burst Count");
                    }

                    ImGui::Checkbox("Radial Spawn Position", &particleEmitter->UseRadialSpawn);
                    TrackInteractiveMutation(undoService, "Edit Particle Radial Spawn Position");

                    if (particleEmitter->UseRadialSpawn)
                    {
                        ImGui::DragFloat("Spawn Radius Min", &particleEmitter->SpawnRadiusMin, 0.01f, 0.0f, 1000.0f);
                        particleEmitter->SpawnRadiusMin = std::max(0.0f, particleEmitter->SpawnRadiusMin);
                        TrackInteractiveMutation(undoService, "Edit Particle Spawn Radius Min");

                        ImGui::DragFloat("Spawn Radius Max", &particleEmitter->SpawnRadiusMax, 0.01f, 0.0f, 1000.0f);
                        particleEmitter->SpawnRadiusMax = std::max(particleEmitter->SpawnRadiusMin, particleEmitter->SpawnRadiusMax);
                        TrackInteractiveMutation(undoService, "Edit Particle Spawn Radius Max");
                    }
                    else
                    {
                        ImGui::DragFloat2("Spawn Offset Min", &particleEmitter->SpawnOffsetMin.x, 0.01f, -1000.0f, 1000.0f, "%.3f");
                        TrackInteractiveMutation(undoService, "Edit Particle Spawn Offset Min");

                        ImGui::DragFloat2("Spawn Offset Max", &particleEmitter->SpawnOffsetMax.x, 0.01f, -1000.0f, 1000.0f, "%.3f");
                        TrackInteractiveMutation(undoService, "Edit Particle Spawn Offset Max");
                    }

                    ImGui::TreePop();
                }

                // -- Velocity --
                if (ImGui::TreeNodeEx("Velocity##ParticleEmitter", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::DragFloat("Speed Min", &particleEmitter->SpeedMin, 0.5f, 0.0f, 10000.0f);
                    particleEmitter->SpeedMin = std::max(0.0f, particleEmitter->SpeedMin);
                    TrackInteractiveMutation(undoService, "Edit Particle Speed Min");

                    ImGui::DragFloat("Speed Max", &particleEmitter->SpeedMax, 0.5f, 0.0f, 10000.0f);
                    particleEmitter->SpeedMax = std::max(particleEmitter->SpeedMin, particleEmitter->SpeedMax);
                    TrackInteractiveMutation(undoService, "Edit Particle Speed Max");

                    ImGui::DragFloat("Angle Min", &particleEmitter->AngleMin, 1.0f, 0.0f, 360.0f);
                    TrackInteractiveMutation(undoService, "Edit Particle Angle Min");

                    ImGui::DragFloat("Angle Max", &particleEmitter->AngleMax, 1.0f, 0.0f, 360.0f);
                    TrackInteractiveMutation(undoService, "Edit Particle Angle Max");

                    ImGui::Checkbox("Radial Velocity", &particleEmitter->RadialVelocity);
                    TrackInteractiveMutation(undoService, "Edit Particle Radial Velocity");

                    ImGui::DragFloat("Gravity Modifier", &particleEmitter->GravityModifier, 0.05f, -100.0f, 100.0f);
                    TrackInteractiveMutation(undoService, "Edit Particle Gravity Modifier");

                    ImGui::TreePop();
                }

                // -- Appearance --
                if (ImGui::TreeNodeEx("Appearance##ParticleEmitter", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::DragFloat("Start Size Min", &particleEmitter->StartSizeMin, 0.01f, 0.001f, 100.0f);
                    particleEmitter->StartSizeMin = std::max(0.001f, particleEmitter->StartSizeMin);
                    TrackInteractiveMutation(undoService, "Edit Particle Start Size Min");

                    ImGui::DragFloat("Start Size Max", &particleEmitter->StartSizeMax, 0.01f, 0.001f, 100.0f);
                    particleEmitter->StartSizeMax = std::max(particleEmitter->StartSizeMin, particleEmitter->StartSizeMax);
                    TrackInteractiveMutation(undoService, "Edit Particle Start Size Max");

                    ImGui::DragFloat("End Size", &particleEmitter->EndSize, 0.01f, 0.0f, 100.0f);
                    particleEmitter->EndSize = std::max(0.0f, particleEmitter->EndSize);
                    TrackInteractiveMutation(undoService, "Edit Particle End Size");

                    ImGui::ColorEdit4("Start Color", &particleEmitter->StartColor.r);
                    TrackInteractiveMutation(undoService, "Edit Particle Start Color");

                    ImGui::ColorEdit4("End Color", &particleEmitter->EndColor.r);
                    TrackInteractiveMutation(undoService, "Edit Particle End Color");

                    ImGui::TreePop();
                }

                // -- Rotation --
                if (ImGui::TreeNodeEx("Rotation##ParticleEmitter"))
                {
                    ImGui::DragFloat("Start Rotation Min", &particleEmitter->StartRotationMin, 1.0f, -360.0f, 360.0f);
                    TrackInteractiveMutation(undoService, "Edit Particle Start Rotation Min");

                    ImGui::DragFloat("Start Rotation Max", &particleEmitter->StartRotationMax, 1.0f, -360.0f, 360.0f);
                    TrackInteractiveMutation(undoService, "Edit Particle Start Rotation Max");

                    ImGui::DragFloat("Rotation Speed Min", &particleEmitter->RotationSpeedMin, 1.0f, -1000.0f, 1000.0f);
                    TrackInteractiveMutation(undoService, "Edit Particle Rotation Speed Min");

                    ImGui::DragFloat("Rotation Speed Max", &particleEmitter->RotationSpeedMax, 1.0f, -1000.0f, 1000.0f);
                    TrackInteractiveMutation(undoService, "Edit Particle Rotation Speed Max");

                    ImGui::TreePop();
                }

                // -- Texture --
                if (ImGui::TreeNodeEx("Texture##ParticleEmitter"))
                {
                    const std::string textureLabel = !particleEmitter->TextureKey.empty()
                        ? EditorAssetNaming::GetAssetDisplayNameFromAssetKey(particleEmitter->TextureKey)
                        : std::string("None (White Quad)");
                    ImGui::Text("Texture");
                    ImGui::SameLine(80);
                    ImGui::Button((textureLabel + "##ParticleEmitterTexture").c_str(), ImVec2(ImGui::GetContentRegionAvail().x - 90, 0));

                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(texturePayloadId))
                        {
                            const char* key = static_cast<const char*>(payload->Data);
                            if (key && key[0])
                            {
                                particleEmitter->TextureKey = key;
                                particleEmitter->CachedTexture.reset();
                                particleEmitter->TextureLoadAttempted = false;
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("Clear##ParticleEmitterTexture"))
                    {
                        particleEmitter->TextureKey.clear();
                        particleEmitter->CachedTexture.reset();
                        particleEmitter->TextureLoadAttempted = false;
                    }
                    TrackInteractiveMutation(undoService, "Edit Particle Emitter Texture");

                    ImGui::TreePop();
                }

                // -- Limits --
                {
                    int maxParticles = static_cast<int>(particleEmitter->MaxParticles);
                    ImGui::DragInt("Max Particles", &maxParticles, 16, 1, static_cast<int>(ParticleEmitterComponent::kMaxParticlesCap));
                    particleEmitter->MaxParticles = static_cast<uint32_t>(std::clamp(maxParticles, 1, static_cast<int>(ParticleEmitterComponent::kMaxParticlesCap)));
                    TrackInteractiveMutation(undoService, "Edit Particle Max Particles");
                }

                ImGui::TreePop();
            }
        }
    }
}
