#include "EditorInspectorPanelEntityComponentsShared.h"

#include "EditorAssetNaming.h"
#include "Assets/SpriteImportSettings.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Limitless::EditorInspectorPanel::Internal
{
    void DrawSceneComponentSections(StandardEntityInspectorContext& context)
    {
        Scene* scene = context.SceneContext;
        auto& registry = context.Registry;
        const entt::entity selectedEntity = context.SelectedEntity;
        const char* materialPayloadId = context.MaterialPayloadId;
        std::string& selectedAnimationClipAssetKey = context.SelectedAnimationClipAssetKey;
        std::string& selectedAnimatorControllerAssetKey = context.SelectedAnimatorControllerAssetKey;
        PendingEntityComponentRemovals& pendingRemovals = context.PendingRemovals;
        EditorUndoService* undoService = context.UndoService;
        const std::string_view onlySectionKey = context.OnlySectionKey;
        const std::vector<std::string>* orderedSectionKeys = context.OrderedSectionKeys;

        if (ShouldDrawInspectorSection(onlySectionKey, "Transform") && (registry.try_get<TransformComponent>(selectedEntity) != nullptr))
        {
            auto* transform = registry.try_get<TransformComponent>(selectedEntity);
            const bool transformOpen = BeginInspectorSectionHeader("Transform", "TransformComponentOptions", "...##TransformComponentOptionsButton");
            if (orderedSectionKeys)
                (void)HandleSectionDragDrop("Transform", *orderedSectionKeys, "Transform");

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
                ImGui::TextUnformatted("Position");
                EditorPanelStyle::AxisVectorDragState positionInteractionState{};
                const bool positionChanged = EditorPanelStyle::DragFloatNWithAxisLabels("##TransformPosition", &transform->Position.x, 3, 0.1f, 0.0f, 0.0f, "%.3f", 0, &positionInteractionState);
                if (positionChanged && scene)
                    scene->MarkTransformDirty(selectedEntity);
                TrackInteractiveVectorMemberMutation<TransformComponent>(
                    undoService,
                    "Edit Transform Position",
                    positionInteractionState,
                    selectedEntity,
                    &TransformComponent::Position,
                    transform->Position,
                    [selectedEntity](Scene& activeScene, TransformComponent&) {
                        activeScene.MarkTransformDirty(selectedEntity);
                    });
                ImGui::TextUnformatted("Rotation");
                EditorPanelStyle::AxisVectorDragState rotationInteractionState{};
                const bool rotationChanged = EditorPanelStyle::DragFloatNWithAxisLabels("##TransformRotation", &transform->Rotation.x, 3, 1.0f, 0.0f, 0.0f, "%.3f", 0, &rotationInteractionState);
                if (rotationChanged && scene)
                    scene->MarkTransformDirty(selectedEntity);
                TrackInteractiveVectorMemberMutation<TransformComponent>(
                    undoService,
                    "Edit Transform Rotation",
                    rotationInteractionState,
                    selectedEntity,
                    &TransformComponent::Rotation,
                    transform->Rotation,
                    [selectedEntity](Scene& activeScene, TransformComponent&) {
                        activeScene.MarkTransformDirty(selectedEntity);
                    });
                ImGui::TextUnformatted("Scale");
                EditorPanelStyle::AxisVectorDragState scaleInteractionState{};
                const bool scaleChanged = EditorPanelStyle::DragFloatNWithAxisLabels("##TransformScale", &transform->Scale.x, 3, 0.1f, 0.0f, 0.0f, "%.3f", 0, &scaleInteractionState);
                if (scaleChanged && scene)
                    scene->MarkTransformDirty(selectedEntity);
                TrackInteractiveVectorMemberMutation<TransformComponent>(
                    undoService,
                    "Edit Transform Scale",
                    scaleInteractionState,
                    selectedEntity,
                    &TransformComponent::Scale,
                    transform->Scale,
                    [selectedEntity](Scene& activeScene, TransformComponent&) {
                        activeScene.MarkTransformDirty(selectedEntity);
                    });
                ImGui::TreePop();
            }
        }

        if (ShouldDrawInspectorSection(onlySectionKey, "Sprite") && (registry.try_get<SpriteComponent>(selectedEntity) != nullptr))
        {
            auto* sprite = registry.try_get<SpriteComponent>(selectedEntity);
            const bool spriteOpen = BeginInspectorSectionHeader("Sprite", "SpriteComponentOptions", "...##SpriteComponentOptionsButton");
            if (orderedSectionKeys)
                (void)HandleSectionDragDrop("Sprite", *orderedSectionKeys, "Sprite");

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
                    ImGui::TextUnformatted("Color");
                    ImGui::ColorEdit4("##SpriteColor", &sprite->Color.r);
                    TrackInteractiveMemberMutation<SpriteComponent>(
                        undoService, "Edit Sprite Color", selectedEntity, &SpriteComponent::Color, sprite->Color);
                    ImGui::TextUnformatted("Tiling Factor");
                    EditorPanelStyle::AxisVectorDragState tilingFactorInteractionState{};
                    EditorPanelStyle::DragFloatNWithAxisLabels("##SpriteTilingFactor", &sprite->TilingFactor.x, 2, 0.05f, 0.001f, 100.0f, "%.3f", 0, &tilingFactorInteractionState);
                    sprite->TilingFactor.x = std::max(0.001f, sprite->TilingFactor.x);
                    sprite->TilingFactor.y = std::max(0.001f, sprite->TilingFactor.y);
                    TrackInteractiveVectorMemberMutation<SpriteComponent>(
                        undoService, "Edit Sprite Tiling Factor", tilingFactorInteractionState, selectedEntity, &SpriteComponent::TilingFactor, sprite->TilingFactor);
                    ImGui::TextUnformatted("Render Order");
                    ImGui::DragInt("##SpriteRenderOrder", &sprite->RenderOrder, 1.0f);
                    TrackInteractiveMemberMutation<SpriteComponent>(
                        undoService, "Edit Sprite Render Order", selectedEntity, &SpriteComponent::RenderOrder, sprite->RenderOrder);
                    ImGui::TextUnformatted("Cast Shadows");
                    ImGui::Checkbox("##SpriteCastShadows", &sprite->CastShadows);
                    TrackInteractiveMemberMutation<SpriteComponent>(
                        undoService, "Edit Sprite Cast Shadows", selectedEntity, &SpriteComponent::CastShadows, sprite->CastShadows);
                    ImGui::TextUnformatted("Receive Shadows");
                    ImGui::Checkbox("##SpriteReceiveShadows", &sprite->ReceiveShadows);
                    TrackInteractiveMemberMutation<SpriteComponent>(
                        undoService, "Edit Sprite Receive Shadows", selectedEntity, &SpriteComponent::ReceiveShadows, sprite->ReceiveShadows);

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
                    const float spriteMaterialPickerWidth = 90.0f;
                    ImGui::Button((materialLabel + "##SpriteMaterial").c_str(),
                                  ImVec2(std::max(60.0f, ImGui::GetContentRegionAvail().x - spriteMaterialPickerWidth), 0));

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

                            if (ImGui::BeginCombo("##SubSpritePicker", previewName.c_str()))
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

        if (ShouldDrawInspectorSection(onlySectionKey, "Animator") && (registry.try_get<AnimatorComponent>(selectedEntity) != nullptr))
        {
            auto* animator = registry.try_get<AnimatorComponent>(selectedEntity);
            const bool animatorOpen = BeginInspectorSectionHeader("Animator", "AnimatorComponentOptions", "...##AnimatorComponentOptionsButton");
            if (orderedSectionKeys)
                (void)HandleSectionDragDrop("Animator", *orderedSectionKeys, "Animator");

            if (ImGui::BeginPopup("AnimatorComponentOptions"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    pendingRemovals.RemoveAnimatorComponent = true;
                ImGui::EndPopup();
            }

            if (animatorOpen)
            {
                ImGui::TextUnformatted("Enabled");
                ImGui::Checkbox("##AnimatorEnabled", &animator->Enabled);
                TrackInteractiveMemberMutation<AnimatorComponent>(
                    undoService, "Edit Animator Enabled", selectedEntity, &AnimatorComponent::Enabled, animator->Enabled);

                ImGui::TextUnformatted("Auto Play");
                ImGui::Checkbox("##AnimatorAutoPlay", &animator->AutoPlay);
                TrackInteractiveMemberMutation<AnimatorComponent>(
                    undoService, "Edit Animator Auto Play", selectedEntity, &AnimatorComponent::AutoPlay, animator->AutoPlay);

                ImGui::TextUnformatted("Apply To Sprite");
                ImGui::Checkbox("##AnimatorApplyToSprite", &animator->ApplyToSprite);
                TrackInteractiveMemberMutation<AnimatorComponent>(
                    undoService, "Edit Animator Apply To Sprite", selectedEntity, &AnimatorComponent::ApplyToSprite, animator->ApplyToSprite);

                ImGui::TextUnformatted("Apply To Transform");
                ImGui::Checkbox("##AnimatorApplyToTransform", &animator->ApplyToTransform);
                TrackInteractiveMemberMutation<AnimatorComponent>(
                    undoService, "Edit Animator Apply To Transform", selectedEntity, &AnimatorComponent::ApplyToTransform, animator->ApplyToTransform);

                ImGui::TextUnformatted("Playback Speed");
                ImGui::DragFloat("##AnimatorPlaybackSpeed", &animator->PlaybackSpeed, 0.01f, 0.0f, 10.0f);
                animator->PlaybackSpeed = std::max(0.0f, animator->PlaybackSpeed);
                TrackInteractiveMemberMutation<AnimatorComponent>(
                    undoService, "Edit Animator Playback Speed", selectedEntity, &AnimatorComponent::PlaybackSpeed, animator->PlaybackSpeed);

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

                    if (key.empty())
                        selectedAnimatorControllerAssetKey.clear();
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

                    if (key.empty())
                        selectedAnimationClipAssetKey.clear();
                };

                const std::string controllerLabel = animator->ControllerKey.empty()
                    ? std::string("None")
                    : EditorAssetNaming::GetAssetDisplayNameFromAssetKey(animator->ControllerKey);
                ImGui::Text("Controller");
                if (ImGui::Button((controllerLabel + "##AnimatorController").c_str(),
                                  ImVec2(std::max(60.0f, ImGui::GetContentRegionAvail().x - 90.0f), 0.0f)))
                {
                    if (!animator->ControllerKey.empty())
                    {
                        selectedAnimationClipAssetKey.clear();
                        selectedAnimatorControllerAssetKey = animator->ControllerKey;
                    }
                }
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
                        {
                            assignControllerKey(key);
                            selectedAnimationClipAssetKey.clear();
                            selectedAnimatorControllerAssetKey = key;
                        }
                    }
                    ImGui::EndPopup();
                }

                const std::string clipLabel = animator->DefaultClipKey.empty()
                    ? std::string("None")
                    : EditorAssetNaming::GetAssetDisplayNameFromAssetKey(animator->DefaultClipKey);
                ImGui::Text("Default Clip");
                if (ImGui::Button((clipLabel + "##AnimatorDefaultClip").c_str(),
                                  ImVec2(std::max(60.0f, ImGui::GetContentRegionAvail().x - 90.0f), 0.0f)))
                {
                    if (!animator->DefaultClipKey.empty())
                    {
                        selectedAnimatorControllerAssetKey.clear();
                        selectedAnimationClipAssetKey = animator->DefaultClipKey;
                    }
                }
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
                        {
                            assignDefaultClipKey(key);
                            selectedAnimatorControllerAssetKey.clear();
                            selectedAnimationClipAssetKey = key;
                        }
                    }
                    ImGui::EndPopup();
                }

                if (BeginPersistentTreeNode("Animator.ParameterOverrides", "Parameter Overrides", ImGuiTreeNodeFlags_DefaultOpen))
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
                        ImGui::TextUnformatted("Name");
                        bool renameRequested = ImGui::InputText("##Name", nameBuffer.data(), nameBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);
                        ImGui::TextUnformatted("Value");
                        ImGui::Checkbox("##Value", &it->second);
                        if (ImGui::Button("Remove"))
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
                        ImGui::Spacing();
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
                        ImGui::TextUnformatted("Name");
                        bool renameRequested = ImGui::InputText("##Name", nameBuffer.data(), nameBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);
                        ImGui::TextUnformatted("Value");
                        ImGui::DragFloat("##Value", &it->second, 0.01f);
                        if (ImGui::Button("Remove"))
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
                        ImGui::Spacing();
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
                        ImGui::TextUnformatted("Name");
                        bool renameRequested = ImGui::InputText("##Name", nameBuffer.data(), nameBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);
                        ImGui::TextUnformatted("Value");
                        ImGui::DragInt("##Value", &it->second);
                        if (ImGui::Button("Remove"))
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
                        ImGui::Spacing();
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
                        ImGui::TextUnformatted("Name");
                        bool renameRequested = ImGui::InputText("##Name", nameBuffer.data(), nameBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);
                        ImGui::TextUnformatted("Active");
                        ImGui::Checkbox("##Active", &it->second);
                        if (ImGui::Button("Remove"))
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
                        ImGui::Spacing();
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

                    const AnimatorParametersSnapshot animatorParametersSnapshot{
                        animator->BoolParameters,
                        animator->FloatParameters,
                        animator->IntegerParameters,
                        animator->TriggerParameters
                    };
                    TrackInteractiveValueMutation(
                        undoService,
                        "Edit Animator Parameters",
                        animatorParametersSnapshot,
                        [undoService, selectedEntity](const AnimatorParametersSnapshot& value) {
                            if (!undoService)
                                return false;
                            Scene* activeScene = undoService->GetActiveScene();
                            if (!activeScene || !activeScene->IsValid(selectedEntity))
                                return false;
                            auto* activeAnimator = activeScene->GetRegistry().try_get<AnimatorComponent>(selectedEntity);
                            if (!activeAnimator)
                                return false;
                            activeAnimator->BoolParameters = value.BoolParameters;
                            activeAnimator->FloatParameters = value.FloatParameters;
                            activeAnimator->IntegerParameters = value.IntegerParameters;
                            activeAnimator->TriggerParameters = value.TriggerParameters;
                            return true;
                        });
                    ImGui::TreePop();
                }

                ImGui::Separator();
                const bool runtimeOpen = BeginPersistentTreeNode("Animator.Runtime", "Runtime", ImGuiTreeNodeFlags_DefaultOpen);
                if (runtimeOpen)
                {
                    ImGui::Text("State: %s", animator->RuntimeCurrentStateName.empty() ? "<none>" : animator->RuntimeCurrentStateName.c_str());
                    ImGui::Text("Clip: %s", animator->RuntimeCurrentClipKey.empty() ? "<none>" : animator->RuntimeCurrentClipKey.c_str());
                    ImGui::Text("State Time: %.3f", animator->RuntimeStateTimeSeconds);
                    ImGui::Text("Duration: %.3f", animator->RuntimeCurrentStateDurationSeconds);
                    ImGui::TreePop();
                }

                ImGui::TreePop();
            }
        }

        if (ShouldDrawInspectorSection(onlySectionKey, "AnimationEventReceiver") && (registry.try_get<AnimationEventReceiverComponent>(selectedEntity) != nullptr))
        {
            auto* animationEventReceiver = registry.try_get<AnimationEventReceiverComponent>(selectedEntity);
            const bool receiverOpen = BeginInspectorSectionHeader("Animation Event Receiver", "AnimationEventReceiverComponentOptions", "...##AnimationEventReceiverComponentOptionsButton");
            if (orderedSectionKeys)
                (void)HandleSectionDragDrop("AnimationEventReceiver", *orderedSectionKeys, "Animation Event Receiver");

            if (ImGui::BeginPopup("AnimationEventReceiverComponentOptions"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    pendingRemovals.RemoveAnimationEventReceiverComponent = true;
                ImGui::EndPopup();
            }

            if (receiverOpen)
            {
                ImGui::TextUnformatted("Enabled");
                ImGui::Checkbox("##AnimationEventReceiverEnabled", &animationEventReceiver->Enabled);
                TrackInteractiveMemberMutation<AnimationEventReceiverComponent>(
                    undoService, "Edit Animation Event Receiver Enabled", selectedEntity, &AnimationEventReceiverComponent::Enabled, animationEventReceiver->Enabled);

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

        if (ShouldDrawInspectorSection(onlySectionKey, "Camera") && (registry.try_get<CameraComponent>(selectedEntity) != nullptr))
        {
            auto* camera = registry.try_get<CameraComponent>(selectedEntity);
            const bool cameraOpen = BeginInspectorSectionHeader("Camera", "CameraComponentOptions", "...##CameraComponentOptionsButton");
            if (orderedSectionKeys)
                (void)HandleSectionDragDrop("Camera", *orderedSectionKeys, "Camera");

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
                ImGui::TextUnformatted("Projection");
                if (ImGui::Combo("##CameraProjection", &projectionIndex, projectionOptions, 2))
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
                struct CameraProjectionSnapshot
                {
                    CameraComponent::ProjectionType Projection;
                    float NearPlane;
                    float FarPlane;
                };
                const CameraProjectionSnapshot cameraProjectionSnapshot{ camera->Projection, camera->NearPlane, camera->FarPlane };
                TrackInteractiveValueMutation(undoService, "Edit Camera Projection", cameraProjectionSnapshot, [undoService, selectedEntity](const CameraProjectionSnapshot& value) {
                    if (!undoService)
                        return false;
                    Scene* activeScene = undoService->GetActiveScene();
                    if (!activeScene || !activeScene->IsValid(selectedEntity))
                        return false;
                    auto* activeCamera = activeScene->GetRegistry().try_get<CameraComponent>(selectedEntity);
                    if (!activeCamera)
                        return false;
                    activeCamera->Projection = value.Projection;
                    activeCamera->NearPlane = value.NearPlane;
                    activeCamera->FarPlane = value.FarPlane;
                    return true;
                });

                if (camera->Projection == CameraComponent::ProjectionType::Orthographic2D)
                {
                    if (camera->NearPlane >= camera->FarPlane)
                        camera->FarPlane = camera->NearPlane + 2.0f;
                    ImGui::TextUnformatted("Zoom");
                    ImGui::DragFloat("##CameraZoom", &camera->Zoom, 0.05f, 0.01f, 100.0f);
                    TrackInteractiveMemberMutation<CameraComponent>(
                        undoService, "Edit Camera Zoom", selectedEntity, &CameraComponent::Zoom, camera->Zoom);
                    ImGui::TextUnformatted("Near Plane");
                    ImGui::DragFloat("##CameraNearPlaneOrtho", &camera->NearPlane, 0.01f);
                    TrackInteractiveMemberMutation<CameraComponent>(
                        undoService, "Edit Camera Near Plane", selectedEntity, &CameraComponent::NearPlane, camera->NearPlane);
                    ImGui::TextUnformatted("Far Plane");
                    ImGui::DragFloat("##CameraFarPlaneOrtho", &camera->FarPlane, 0.01f);
                    TrackInteractiveMemberMutation<CameraComponent>(
                        undoService, "Edit Camera Far Plane", selectedEntity, &CameraComponent::FarPlane, camera->FarPlane);
                }
                else
                {
                    if (camera->NearPlane <= 0.0f)
                        camera->NearPlane = 0.01f;
                    if (camera->FarPlane <= camera->NearPlane)
                        camera->FarPlane = camera->NearPlane + 1000.0f;
                    ImGui::TextUnformatted("Field Of View");
                    ImGui::DragFloat("##CameraFieldOfView", &camera->FieldOfViewYDegrees, 0.1f, 1.0f, 179.0f);
                    TrackInteractiveMemberMutation<CameraComponent>(
                        undoService,
                        "Edit Camera Field Of View",
                        selectedEntity,
                        &CameraComponent::FieldOfViewYDegrees,
                        camera->FieldOfViewYDegrees);
                    ImGui::TextUnformatted("Near Plane");
                    ImGui::DragFloat("##CameraNearPlanePersp", &camera->NearPlane, 0.01f, 0.001f, 1000.0f);
                    TrackInteractiveMemberMutation<CameraComponent>(
                        undoService, "Edit Camera Near Plane", selectedEntity, &CameraComponent::NearPlane, camera->NearPlane);
                    ImGui::TextUnformatted("Far Plane");
                    ImGui::DragFloat("##CameraFarPlanePersp", &camera->FarPlane, 1.0f, 0.01f, 100000.0f);
                    TrackInteractiveMemberMutation<CameraComponent>(
                        undoService, "Edit Camera Far Plane", selectedEntity, &CameraComponent::FarPlane, camera->FarPlane);
                }

                bool isPrimary = camera->IsPrimary;
                ImGui::TextUnformatted("Primary");
                if (ImGui::Checkbox("##CameraPrimary", &isPrimary))
                {
                    camera->IsPrimary = isPrimary;
                    if (camera->IsPrimary)
                        ClearPrimaryFlagFromOtherCameras(registry, selectedEntity);
                }
                TrackInteractiveValueMutation(undoService, "Edit Camera Primary", camera->IsPrimary, [undoService, selectedEntity](bool value) {
                    if (!undoService)
                        return false;
                    Scene* activeScene = undoService->GetActiveScene();
                    if (!activeScene || !activeScene->IsValid(selectedEntity))
                        return false;

                    auto& activeRegistry = activeScene->GetRegistry();
                    auto* activeCamera = activeRegistry.try_get<CameraComponent>(selectedEntity);
                    if (!activeCamera)
                        return false;
                    activeCamera->IsPrimary = value;
                    if (activeCamera->IsPrimary)
                        ClearPrimaryFlagFromOtherCameras(activeRegistry, selectedEntity);
                    return true;
                });

                // Culling Mask — layer bitmask dropdown
                {
                    ImGui::TextUnformatted("Culling Mask");
                    const char* maskLabel = (camera->CullingMask == ~0u) ? "Everything"
                                          : (camera->CullingMask == 0u) ? "Nothing"
                                          : "Mixed...";
                    if (ImGui::BeginCombo("##CameraCullingMask", maskLabel))
                    {
                        if (ImGui::Selectable("Everything", camera->CullingMask == ~0u))
                            camera->CullingMask = ~0u;
                        if (ImGui::Selectable("Nothing", camera->CullingMask == 0u))
                            camera->CullingMask = 0u;
                        ImGui::Separator();

                        Project::LayersSettings layersSettings{};
                        const auto& pm = Project::ProjectManager::GetInstance();
                        if (pm.HasOpenProject())
                        {
                            const auto result = Project::LoadLayersSettings(pm.GetProjectRoot());
                            if (result.IsSuccess())
                                layersSettings = result.GetValue();
                        }
                        for (int i = 0; i < 32; ++i)
                        {
                            const std::string& layerName = layersSettings.LayerNames[i];
                            if (layerName.empty() && i > 0)
                                continue;
                            char cullingLabel[128];
                            snprintf(cullingLabel, sizeof(cullingLabel), "%d: %s", i, layerName.empty() ? "(unnamed)" : layerName.c_str());
                            bool isSet = (camera->CullingMask & (1u << i)) != 0;
                            if (ImGui::Checkbox(cullingLabel, &isSet))
                            {
                                if (isSet)
                                    camera->CullingMask |= (1u << i);
                                else
                                    camera->CullingMask &= ~(1u << i);
                            }
                        }
                        ImGui::EndCombo();
                    }
                    TrackInteractiveMemberMutation<CameraComponent>(
                        undoService, "Edit Camera Culling Mask", selectedEntity, &CameraComponent::CullingMask, camera->CullingMask);
                }

                ImGui::TreePop();
            }
        }
    }
}
