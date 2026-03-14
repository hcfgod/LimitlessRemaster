#include "EditorInspectorPanelEntityComponentsShared.h"

#include "EditorAssetNaming.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Limitless::EditorInspectorPanel::Internal
{
    void DrawUiComponentSections(StandardEntityInspectorContext& context)
    {
        auto& registry = context.Registry;
        const entt::entity selectedEntity = context.SelectedEntity;
        const char* texturePayloadId = context.TexturePayloadId;
        const char* fontPayloadId = context.FontPayloadId;
        PendingEntityComponentRemovals& pendingRemovals = context.PendingRemovals;
        EditorUndoService* undoService = context.UndoService;
        const std::string_view onlySectionKey = context.OnlySectionKey;
        const std::vector<std::string>* orderedSectionKeys = context.OrderedSectionKeys;

        if (ShouldDrawInspectorSection(onlySectionKey, "Canvas") && (registry.try_get<CanvasComponent>(selectedEntity) != nullptr))
        {
            auto* canvas = registry.try_get<CanvasComponent>(selectedEntity);
            const bool canvasOpen = BeginInspectorSectionHeader("Canvas", "CanvasComponentOptions", "...##CanvasComponentOptionsButton");
            if (orderedSectionKeys)
                (void)HandleSectionDragDrop("Canvas", *orderedSectionKeys, "Canvas");

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
                ImGui::TextUnformatted("Render Mode");
                if (ImGui::Combo("##CanvasRenderMode", &renderModeIndex, renderModes, 2))
                    canvas->Mode = static_cast<CanvasComponent::RenderMode>(renderModeIndex);
                TrackInteractiveMemberMutation<CanvasComponent>(
                    undoService, "Edit Canvas Render Mode", selectedEntity, &CanvasComponent::Mode, canvas->Mode);

                ImGui::TextUnformatted("Sort Order");
                ImGui::DragInt("##CanvasSortOrder", &canvas->SortOrder, 1.0f);
                TrackInteractiveMemberMutation<CanvasComponent>(
                    undoService, "Edit Canvas Sort Order", selectedEntity, &CanvasComponent::SortOrder, canvas->SortOrder);

                ImGui::TextUnformatted("Reference Resolution");
                EditorPanelStyle::AxisVectorDragState referenceResolutionInteractionState{};
                EditorPanelStyle::DragFloatNWithAxisLabels("##CanvasReferenceResolution", &canvas->ReferenceResolution.x, 2, 1.0f, 1.0f, 16384.0f, "%.0f", 0, &referenceResolutionInteractionState);
                canvas->ReferenceResolution.x = std::max(1.0f, canvas->ReferenceResolution.x);
                canvas->ReferenceResolution.y = std::max(1.0f, canvas->ReferenceResolution.y);
                TrackInteractiveVectorMemberMutation<CanvasComponent>(
                    undoService,
                    "Edit Canvas Reference Resolution",
                    referenceResolutionInteractionState,
                    selectedEntity,
                    &CanvasComponent::ReferenceResolution,
                    canvas->ReferenceResolution);
                ImGui::TreePop();
            }
        }

        if (ShouldDrawInspectorSection(onlySectionKey, "RectTransform") && (registry.try_get<RectTransformComponent>(selectedEntity) != nullptr))
        {
            auto* rectTransform = registry.try_get<RectTransformComponent>(selectedEntity);
            const bool rectTransformOpen = BeginInspectorSectionHeader("RectTransform", "RectTransformComponentOptions", "...##RectTransformComponentOptionsButton");
            if (orderedSectionKeys)
                (void)HandleSectionDragDrop("RectTransform", *orderedSectionKeys, "RectTransform");

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
                ImGui::TextUnformatted("Anchor Preset");
                if (ImGui::BeginCombo("##RectTransformAnchorPreset", selectedPresetName))
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
                struct AnchorPresetSnapshot
                {
                    glm::vec2 AnchorMin;
                    glm::vec2 AnchorMax;
                    glm::vec2 Pivot;
                };
                const AnchorPresetSnapshot anchorPresetSnapshot{ rectTransform->AnchorMin, rectTransform->AnchorMax, rectTransform->Pivot };
                TrackInteractiveValueMutation(undoService, "Edit RectTransform Anchor Preset", anchorPresetSnapshot, [undoService, selectedEntity](const AnchorPresetSnapshot& value) {
                    if (!undoService)
                        return false;
                    Scene* activeScene = undoService->GetActiveScene();
                    if (!activeScene || !activeScene->IsValid(selectedEntity))
                        return false;
                    auto* activeRectTransform = activeScene->GetRegistry().try_get<RectTransformComponent>(selectedEntity);
                    if (!activeRectTransform)
                        return false;
                    activeRectTransform->AnchorMin = value.AnchorMin;
                    activeRectTransform->AnchorMax = value.AnchorMax;
                    activeRectTransform->Pivot = value.Pivot;
                    return true;
                });

                ImGui::TextUnformatted("Anchor Min");
                EditorPanelStyle::AxisVectorDragState anchorMinInteractionState{};
                EditorPanelStyle::DragFloatNWithAxisLabels("##RectTransformAnchorMin", &rectTransform->AnchorMin.x, 2, 0.01f, 0.0f, 1.0f, "%.3f", 0, &anchorMinInteractionState);
                rectTransform->AnchorMin.x = std::clamp(rectTransform->AnchorMin.x, 0.0f, 1.0f);
                rectTransform->AnchorMin.y = std::clamp(rectTransform->AnchorMin.y, 0.0f, 1.0f);
                TrackInteractiveVectorMemberMutation<RectTransformComponent>(
                    undoService, "Edit RectTransform Anchor Min", anchorMinInteractionState, selectedEntity, &RectTransformComponent::AnchorMin, rectTransform->AnchorMin);

                ImGui::TextUnformatted("Anchor Max");
                EditorPanelStyle::AxisVectorDragState anchorMaxInteractionState{};
                EditorPanelStyle::DragFloatNWithAxisLabels("##RectTransformAnchorMax", &rectTransform->AnchorMax.x, 2, 0.01f, 0.0f, 1.0f, "%.3f", 0, &anchorMaxInteractionState);
                rectTransform->AnchorMax.x = std::clamp(rectTransform->AnchorMax.x, 0.0f, 1.0f);
                rectTransform->AnchorMax.y = std::clamp(rectTransform->AnchorMax.y, 0.0f, 1.0f);
                TrackInteractiveVectorMemberMutation<RectTransformComponent>(
                    undoService, "Edit RectTransform Anchor Max", anchorMaxInteractionState, selectedEntity, &RectTransformComponent::AnchorMax, rectTransform->AnchorMax);

                if (rectTransform->AnchorMin.x > rectTransform->AnchorMax.x)
                    std::swap(rectTransform->AnchorMin.x, rectTransform->AnchorMax.x);
                if (rectTransform->AnchorMin.y > rectTransform->AnchorMax.y)
                    std::swap(rectTransform->AnchorMin.y, rectTransform->AnchorMax.y);

                ImGui::TextUnformatted("Pivot");
                EditorPanelStyle::AxisVectorDragState pivotInteractionState{};
                EditorPanelStyle::DragFloatNWithAxisLabels("##RectTransformPivot", &rectTransform->Pivot.x, 2, 0.01f, 0.0f, 1.0f, "%.3f", 0, &pivotInteractionState);
                rectTransform->Pivot.x = std::clamp(rectTransform->Pivot.x, 0.0f, 1.0f);
                rectTransform->Pivot.y = std::clamp(rectTransform->Pivot.y, 0.0f, 1.0f);
                TrackInteractiveVectorMemberMutation<RectTransformComponent>(
                    undoService, "Edit RectTransform Pivot", pivotInteractionState, selectedEntity, &RectTransformComponent::Pivot, rectTransform->Pivot);

                ImGui::TextUnformatted("Size Delta");
                EditorPanelStyle::AxisVectorDragState sizeDeltaInteractionState{};
                EditorPanelStyle::DragFloatNWithAxisLabels("##RectTransformSizeDelta", &rectTransform->SizeDelta.x, 2, 1.0f, -16384.0f, 16384.0f, "%.3f", 0, &sizeDeltaInteractionState);
                TrackInteractiveVectorMemberMutation<RectTransformComponent>(
                    undoService, "Edit RectTransform Size Delta", sizeDeltaInteractionState, selectedEntity, &RectTransformComponent::SizeDelta, rectTransform->SizeDelta);

                ImGui::TextUnformatted("Anchored Position");
                EditorPanelStyle::AxisVectorDragState anchoredPositionInteractionState{};
                EditorPanelStyle::DragFloatNWithAxisLabels("##RectTransformAnchoredPosition", &rectTransform->AnchoredPosition.x, 2, 1.0f, 0.0f, 0.0f, "%.3f", 0, &anchoredPositionInteractionState);
                TrackInteractiveVectorMemberMutation<RectTransformComponent>(
                    undoService,
                    "Edit RectTransform Anchored Position",
                    anchoredPositionInteractionState,
                    selectedEntity,
                    &RectTransformComponent::AnchoredPosition,
                    rectTransform->AnchoredPosition);

                ImGui::TreePop();
            }
        }

        if (ShouldDrawInspectorSection(onlySectionKey, "UIImage") && (registry.try_get<UIImageComponent>(selectedEntity) != nullptr))
        {
            auto* uiImage = registry.try_get<UIImageComponent>(selectedEntity);
            const bool uiImageOpen = BeginInspectorSectionHeader("UI Image", "UIImageComponentOptions", "...##UIImageComponentOptionsButton");
            if (orderedSectionKeys)
                (void)HandleSectionDragDrop("UIImage", *orderedSectionKeys, "UI Image");

            if (ImGui::BeginPopup("UIImageComponentOptions"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    pendingRemovals.RemoveUIImageComponent = true;
                ImGui::EndPopup();
            }

            if (uiImageOpen)
            {
                const auto assignUiImageTexture = [&](const SpriteDropAssignment& assignment) {
                    if (undoService)
                    {
                        (void)undoService->ExecuteSceneMutation(assignment.TextureKey.empty() ? "Clear UI Image Texture" : "Assign UI Image Texture", [&](Scene& mutableScene) {
                            auto& mutableRegistry = mutableScene.GetRegistry();
                            auto* mutableSprite = mutableRegistry.try_get<SpriteComponent>(selectedEntity);
                            if (!mutableSprite)
                                mutableSprite = &mutableRegistry.emplace<SpriteComponent>(selectedEntity);
                            mutableSprite->TextureKey = assignment.TextureKey;
                            mutableSprite->SubSpriteIndex = assignment.SubSpriteIndex;
                            mutableSprite->UvMin = assignment.UvMin;
                            mutableSprite->UvMax = assignment.UvMax;
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
                        sprite->TextureKey = assignment.TextureKey;
                        sprite->SubSpriteIndex = assignment.SubSpriteIndex;
                        sprite->UvMin = assignment.UvMin;
                        sprite->UvMax = assignment.UvMax;
                        sprite->CachedTexture.reset();
                        sprite->TextureLoadAttempted = false;
                    }
                };

                auto* uiImageSprite = registry.try_get<SpriteComponent>(selectedEntity);
                const std::string imageTextureLabel = (uiImageSprite && !uiImageSprite->TextureKey.empty())
                    ? EditorAssetNaming::GetAssetDisplayNameFromAssetKey(uiImageSprite->TextureKey)
                    : std::string("None (White Quad)");
                ImGui::Text("Image");
                ImGui::Button((imageTextureLabel + "##UIImageTexture").c_str(),
                              ImVec2(std::max(60.0f, ImGui::GetContentRegionAvail().x - 60.0f), 0.0f));
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kSubSpritePayloadId))
                    {
                        const char* key = static_cast<const char*>(payload->Data);
                        if (key && key[0])
                        {
                            SpriteDropAssignment assignment;
                            if (ResolveSpriteDropAssignment(key, assignment))
                                assignUiImageTexture(assignment);
                        }
                    }
                    else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(texturePayloadId))
                    {
                        const char* key = static_cast<const char*>(payload->Data);
                        if (key && key[0])
                        {
                            SpriteDropAssignment assignment;
                            if (ResolveSpriteDropAssignment(key, assignment))
                                assignUiImageTexture(assignment);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                if (uiImageSprite && !uiImageSprite->TextureKey.empty())
                {
                    ImGui::SameLine();
                    if (ImGui::Button("Clear##UIImageTexture"))
                        assignUiImageTexture(SpriteDropAssignment{});
                }

                ImGui::TextUnformatted("Raycast Target");
                ImGui::Checkbox("##UIImageRaycastTarget", &uiImage->RaycastTarget);
                TrackInteractiveMemberMutation<UIImageComponent>(
                    undoService, "Edit UIImage Raycast Target", selectedEntity, &UIImageComponent::RaycastTarget, uiImage->RaycastTarget);
                ImGui::TreePop();
            }
        }

        if (ShouldDrawInspectorSection(onlySectionKey, "UIPanel") && (registry.try_get<UIPanelComponent>(selectedEntity) != nullptr))
        {
            auto* uiPanel = registry.try_get<UIPanelComponent>(selectedEntity);
            const bool uiPanelOpen = BeginInspectorSectionHeader("UI Panel", "UIPanelComponentOptions", "...##UIPanelComponentOptionsButton");
            if (orderedSectionKeys)
                (void)HandleSectionDragDrop("UIPanel", *orderedSectionKeys, "UI Panel");

            if (ImGui::BeginPopup("UIPanelComponentOptions"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    pendingRemovals.RemoveUIPanelComponent = true;
                ImGui::EndPopup();
            }

            if (uiPanelOpen)
            {
                const auto assignUiPanelTexture = [&](const SpriteDropAssignment& assignment) {
                    if (undoService)
                    {
                        (void)undoService->ExecuteSceneMutation(assignment.TextureKey.empty() ? "Clear UI Panel Texture" : "Assign UI Panel Texture", [&](Scene& mutableScene) {
                            auto& mutableRegistry = mutableScene.GetRegistry();
                            auto* mutableSprite = mutableRegistry.try_get<SpriteComponent>(selectedEntity);
                            if (!mutableSprite)
                                mutableSprite = &mutableRegistry.emplace<SpriteComponent>(selectedEntity);
                            mutableSprite->TextureKey = assignment.TextureKey;
                            mutableSprite->SubSpriteIndex = assignment.SubSpriteIndex;
                            mutableSprite->UvMin = assignment.UvMin;
                            mutableSprite->UvMax = assignment.UvMax;
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
                        sprite->TextureKey = assignment.TextureKey;
                        sprite->SubSpriteIndex = assignment.SubSpriteIndex;
                        sprite->UvMin = assignment.UvMin;
                        sprite->UvMax = assignment.UvMax;
                        sprite->CachedTexture.reset();
                        sprite->TextureLoadAttempted = false;
                    }
                };

                auto* panelSprite = registry.try_get<SpriteComponent>(selectedEntity);
                const std::string panelTextureLabel = (panelSprite && !panelSprite->TextureKey.empty())
                    ? EditorAssetNaming::GetAssetDisplayNameFromAssetKey(panelSprite->TextureKey)
                    : std::string("None (Solid Color)");
                ImGui::Text("Background");
                ImGui::Button((panelTextureLabel + "##UIPanelTexture").c_str(),
                              ImVec2(std::max(60.0f, ImGui::GetContentRegionAvail().x - 60.0f), 0.0f));
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kSubSpritePayloadId))
                    {
                        const char* key = static_cast<const char*>(payload->Data);
                        if (key && key[0])
                        {
                            SpriteDropAssignment assignment;
                            if (ResolveSpriteDropAssignment(key, assignment))
                                assignUiPanelTexture(assignment);
                        }
                    }
                    else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(texturePayloadId))
                    {
                        const char* key = static_cast<const char*>(payload->Data);
                        if (key && key[0])
                        {
                            SpriteDropAssignment assignment;
                            if (ResolveSpriteDropAssignment(key, assignment))
                                assignUiPanelTexture(assignment);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                if (panelSprite && !panelSprite->TextureKey.empty())
                {
                    ImGui::SameLine();
                    if (ImGui::Button("Clear##UIPanelTexture"))
                        assignUiPanelTexture(SpriteDropAssignment{});
                }

                ImGui::TextUnformatted("Background Color");
                ImGui::ColorEdit4("##UIPanelBackgroundColor", &uiPanel->BackgroundColor.r);
                TrackInteractiveMemberMutation<UIPanelComponent>(
                    undoService, "Edit UIPanel Background Color", selectedEntity, &UIPanelComponent::BackgroundColor, uiPanel->BackgroundColor);

                ImGui::TextUnformatted("Use Sprite Texture");
                ImGui::Checkbox("##UIPanelUseSpriteTexture", &uiPanel->UseSpriteTexture);
                TrackInteractiveMemberMutation<UIPanelComponent>(
                    undoService, "Edit UIPanel Use Sprite Texture", selectedEntity, &UIPanelComponent::UseSpriteTexture, uiPanel->UseSpriteTexture);

                ImGui::TextUnformatted("Raycast Target");
                ImGui::Checkbox("##UIPanelRaycastTarget", &uiPanel->RaycastTarget);
                TrackInteractiveMemberMutation<UIPanelComponent>(
                    undoService, "Edit UIPanel Raycast Target", selectedEntity, &UIPanelComponent::RaycastTarget, uiPanel->RaycastTarget);
                ImGui::TreePop();
            }
        }

        if (ShouldDrawInspectorSection(onlySectionKey, "UIText") && (registry.try_get<UITextComponent>(selectedEntity) != nullptr))
        {
            auto* uiText = registry.try_get<UITextComponent>(selectedEntity);
            const bool uiTextOpen = BeginInspectorSectionHeader("UI Text", "UITextComponentOptions", "...##UITextComponentOptionsButton");
            if (orderedSectionKeys)
                (void)HandleSectionDragDrop("UIText", *orderedSectionKeys, "UI Text");

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

                ImGui::TextUnformatted("Text Value");
                ImGui::InputTextMultiline("##UITextValue", uiTextValueBuffer.data(), uiTextValueBuffer.size(), ImVec2(-1.0f, 84.0f));
                uiText->Text = uiTextValueBuffer.data();
                TrackInteractiveValueMutation(
                    undoService,
                    "Edit UI Text Value",
                    uiText->Text,
                    [undoService, selectedEntity](const std::string& value) {
                        if (!undoService)
                            return false;
                        Scene* activeScene = undoService->GetActiveScene();
                        if (!activeScene || !activeScene->IsValid(selectedEntity))
                            return false;
                        auto* activeText = activeScene->GetRegistry().try_get<UITextComponent>(selectedEntity);
                        if (!activeText)
                            return false;
                        activeText->Text = value;
                        return true;
                    });

                ImGui::TextUnformatted("Font File Path");
                ImGui::InputText("##UITextFontFilePath", uiTextFontPathBuffer.data(), uiTextFontPathBuffer.size());
                uiText->FontFilePath = uiTextFontPathBuffer.data();
                uiText->CachedFont.reset();
                uiText->FontLoadAttempted = false;
                TrackInteractiveValueMutation(
                    undoService,
                    "Edit UI Font File Path",
                    uiText->FontFilePath,
                    [undoService, selectedEntity](const std::string& value) {
                        if (!undoService)
                            return false;
                        Scene* activeScene = undoService->GetActiveScene();
                        if (!activeScene || !activeScene->IsValid(selectedEntity))
                            return false;
                        auto* activeText = activeScene->GetRegistry().try_get<UITextComponent>(selectedEntity);
                        if (!activeText)
                            return false;
                        activeText->FontFilePath = value;
                        activeText->CachedFont.reset();
                        activeText->FontLoadAttempted = false;
                        return true;
                    });
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Example: Assets/Fonts/YourFont.ttf");

                const std::string uiFontLabel = uiText->FontFilePath.empty()
                    ? std::string("None")
                    : EditorAssetNaming::GetAssetDisplayNameFromAssetKey(uiText->FontFilePath);
                ImGui::Text("Font Asset");
                ImGui::Button((uiFontLabel + "##UITextFontAsset").c_str(),
                              ImVec2(std::max(60.0f, ImGui::GetContentRegionAvail().x - 60.0f), 0.0f));
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

                ImGui::TextUnformatted("Font Size");
                if (ImGui::DragFloat("##UITextFontSize", &uiText->FontSize, 1.0f, 4.0f, 512.0f))
                    uiText->FontSize = std::max(4.0f, uiText->FontSize);
                TrackInteractiveMemberMutation<UITextComponent>(
                    undoService, "Edit UI Font Size", selectedEntity, &UITextComponent::FontSize, uiText->FontSize);

                ImGui::TextUnformatted("Color");
                ImGui::ColorEdit4("##UITextColor", &uiText->Color.r);
                TrackInteractiveMemberMutation<UITextComponent>(
                    undoService, "Edit UI Text Color", selectedEntity, &UITextComponent::Color, uiText->Color);

                ImGui::TextUnformatted("Raycast Target");
                ImGui::Checkbox("##UITextRaycastTarget", &uiText->RaycastTarget);
                TrackInteractiveMemberMutation<UITextComponent>(
                    undoService, "Edit UIText Raycast Target", selectedEntity, &UITextComponent::RaycastTarget, uiText->RaycastTarget);
                ImGui::TreePop();
            }
        }

        if (ShouldDrawInspectorSection(onlySectionKey, "UIButton") && (registry.try_get<UIButtonComponent>(selectedEntity) != nullptr))
        {
            auto* uiButton = registry.try_get<UIButtonComponent>(selectedEntity);
            const bool uiButtonOpen = BeginInspectorSectionHeader("UI Button", "UIButtonComponentOptions", "...##UIButtonComponentOptionsButton");
            if (orderedSectionKeys)
                (void)HandleSectionDragDrop("UIButton", *orderedSectionKeys, "UI Button");

            if (ImGui::BeginPopup("UIButtonComponentOptions"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    pendingRemovals.RemoveUIButtonComponent = true;
                ImGui::EndPopup();
            }

            if (uiButtonOpen)
            {
                ImGui::TextUnformatted("Interactable");
                ImGui::Checkbox("##UIButtonInteractable", &uiButton->Interactable);
                TrackInteractiveMemberMutation<UIButtonComponent>(
                    undoService, "Edit UIButton Interactable", selectedEntity, &UIButtonComponent::Interactable, uiButton->Interactable);
                ImGui::TextUnformatted("Use State Colors");
                ImGui::Checkbox("##UIButtonUseStateColors", &uiButton->UseStateColors);
                TrackInteractiveMemberMutation<UIButtonComponent>(
                    undoService, "Edit UIButton Use State Colors", selectedEntity, &UIButtonComponent::UseStateColors, uiButton->UseStateColors);
                if (uiButton->UseStateColors)
                {
                    ImGui::TextUnformatted("Normal Color");
                    ImGui::ColorEdit4("##UIButtonNormalColor", &uiButton->NormalColor.r);
                    TrackInteractiveMemberMutation<UIButtonComponent>(
                        undoService, "Edit UIButton Normal Color", selectedEntity, &UIButtonComponent::NormalColor, uiButton->NormalColor);
                    ImGui::TextUnformatted("Hovered Color");
                    ImGui::ColorEdit4("##UIButtonHoveredColor", &uiButton->HoveredColor.r);
                    TrackInteractiveMemberMutation<UIButtonComponent>(
                        undoService, "Edit UIButton Hovered Color", selectedEntity, &UIButtonComponent::HoveredColor, uiButton->HoveredColor);
                    ImGui::TextUnformatted("Pressed Color");
                    ImGui::ColorEdit4("##UIButtonPressedColor", &uiButton->PressedColor.r);
                    TrackInteractiveMemberMutation<UIButtonComponent>(
                        undoService, "Edit UIButton Pressed Color", selectedEntity, &UIButtonComponent::PressedColor, uiButton->PressedColor);
                    ImGui::TextUnformatted("Disabled Color");
                    ImGui::ColorEdit4("##UIButtonDisabledColor", &uiButton->DisabledColor.r);
                    TrackInteractiveMemberMutation<UIButtonComponent>(
                        undoService, "Edit UIButton Disabled Color", selectedEntity, &UIButtonComponent::DisabledColor, uiButton->DisabledColor);
                }
                std::array<char, 256> onClickEventBuffer{};
                std::snprintf(onClickEventBuffer.data(), onClickEventBuffer.size(), "%s", uiButton->OnClickEvent.c_str());
                ImGui::TextUnformatted("On Click Event");
                if (ImGui::InputText("##UIButtonOnClickEvent", onClickEventBuffer.data(), onClickEventBuffer.size()))
                    uiButton->OnClickEvent = onClickEventBuffer.data();
                TrackInteractiveMemberMutation<UIButtonComponent>(
                    undoService, "Edit UIButton OnClick Event", selectedEntity, &UIButtonComponent::OnClickEvent, uiButton->OnClickEvent);
                std::array<char, 256> onHoverEnterEventBuffer{};
                std::snprintf(onHoverEnterEventBuffer.data(), onHoverEnterEventBuffer.size(), "%s", uiButton->OnHoverEnterEvent.c_str());
                ImGui::TextUnformatted("On Hover Enter Event");
                if (ImGui::InputText("##UIButtonOnHoverEnterEvent", onHoverEnterEventBuffer.data(), onHoverEnterEventBuffer.size()))
                    uiButton->OnHoverEnterEvent = onHoverEnterEventBuffer.data();
                TrackInteractiveMemberMutation<UIButtonComponent>(
                    undoService, "Edit UIButton OnHoverEnter Event", selectedEntity, &UIButtonComponent::OnHoverEnterEvent, uiButton->OnHoverEnterEvent);
                std::array<char, 256> onHoverExitEventBuffer{};
                std::snprintf(onHoverExitEventBuffer.data(), onHoverExitEventBuffer.size(), "%s", uiButton->OnHoverExitEvent.c_str());
                ImGui::TextUnformatted("On Hover Exit Event");
                if (ImGui::InputText("##UIButtonOnHoverExitEvent", onHoverExitEventBuffer.data(), onHoverExitEventBuffer.size()))
                    uiButton->OnHoverExitEvent = onHoverExitEventBuffer.data();
                TrackInteractiveMemberMutation<UIButtonComponent>(
                    undoService, "Edit UIButton OnHoverExit Event", selectedEntity, &UIButtonComponent::OnHoverExitEvent, uiButton->OnHoverExitEvent);
                std::array<char, 256> onPressedEventBuffer{};
                std::snprintf(onPressedEventBuffer.data(), onPressedEventBuffer.size(), "%s", uiButton->OnPressedEvent.c_str());
                ImGui::TextUnformatted("On Pressed Event");
                if (ImGui::InputText("##UIButtonOnPressedEvent", onPressedEventBuffer.data(), onPressedEventBuffer.size()))
                    uiButton->OnPressedEvent = onPressedEventBuffer.data();
                TrackInteractiveMemberMutation<UIButtonComponent>(
                    undoService, "Edit UIButton OnPressed Event", selectedEntity, &UIButtonComponent::OnPressedEvent, uiButton->OnPressedEvent);
                ImGui::TreePop();
            }
        }

        if (ShouldDrawInspectorSection(onlySectionKey, "UISlider") && (registry.try_get<UISliderComponent>(selectedEntity) != nullptr))
        {
            auto* uiSlider = registry.try_get<UISliderComponent>(selectedEntity);
            const bool uiSliderOpen = BeginInspectorSectionHeader("UI Slider", "UISliderComponentOptions", "...##UISliderComponentOptionsButton");
            if (orderedSectionKeys)
                (void)HandleSectionDragDrop("UISlider", *orderedSectionKeys, "UI Slider");

            if (ImGui::BeginPopup("UISliderComponentOptions"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    pendingRemovals.RemoveUISliderComponent = true;
                ImGui::EndPopup();
            }

            if (uiSliderOpen)
            {
                const bool sliderUsesVisualChildren = SliderHasVisualChildren(registry, selectedEntity);
                ImGui::TextUnformatted("Interactable");
                ImGui::Checkbox("##UISliderInteractable", &uiSlider->Interactable);
                TrackInteractiveMemberMutation<UISliderComponent>(
                    undoService, "Edit UISlider Interactable", selectedEntity, &UISliderComponent::Interactable, uiSlider->Interactable);
                ImGui::TextUnformatted("Min Value");
                ImGui::DragFloat("##UISliderMinValue", &uiSlider->MinValue, 0.1f);
                TrackInteractiveMemberMutation<UISliderComponent>(
                    undoService, "Edit UISlider Min Value", selectedEntity, &UISliderComponent::MinValue, uiSlider->MinValue);
                ImGui::TextUnformatted("Max Value");
                ImGui::DragFloat("##UISliderMaxValue", &uiSlider->MaxValue, 0.1f);
                if (uiSlider->MaxValue < uiSlider->MinValue)
                    uiSlider->MaxValue = uiSlider->MinValue;
                TrackInteractiveMemberMutation<UISliderComponent>(
                    undoService, "Edit UISlider Max Value", selectedEntity, &UISliderComponent::MaxValue, uiSlider->MaxValue);
                ImGui::TextUnformatted("Value");
                ImGui::SliderFloat("##UISliderValue", &uiSlider->Value, uiSlider->MinValue, uiSlider->MaxValue);
                uiSlider->Value = std::clamp(uiSlider->Value, uiSlider->MinValue, uiSlider->MaxValue);
                TrackInteractiveMemberMutation<UISliderComponent>(
                    undoService, "Edit UISlider Value", selectedEntity, &UISliderComponent::Value, uiSlider->Value);
                if (sliderUsesVisualChildren)
                {
                    ImGui::TextWrapped("Unity-style slider visuals are authored on child entities:");
                    ImGui::BulletText("Slider Background");
                    ImGui::BulletText("Slider Fill");
                    ImGui::BulletText("Slider Handle");
                    if (BeginPersistentTreeNode("UISlider.FallbackColors", "Fallback Colors (Used Only Without Visual Children)"))
                    {
                        ImGui::TextUnformatted("Background Color");
                        ImGui::ColorEdit4("##UISliderBackgroundColorFallback", &uiSlider->BackgroundColor.r);
                        TrackInteractiveMemberMutation<UISliderComponent>(
                            undoService, "Edit UISlider Background Color", selectedEntity, &UISliderComponent::BackgroundColor, uiSlider->BackgroundColor);
                        ImGui::TextUnformatted("Fill Color");
                        ImGui::ColorEdit4("##UISliderFillColorFallback", &uiSlider->FillColor.r);
                        TrackInteractiveMemberMutation<UISliderComponent>(
                            undoService, "Edit UISlider Fill Color", selectedEntity, &UISliderComponent::FillColor, uiSlider->FillColor);
                        ImGui::TextUnformatted("Handle Color");
                        ImGui::ColorEdit4("##UISliderHandleColorFallback", &uiSlider->HandleColor.r);
                        TrackInteractiveMemberMutation<UISliderComponent>(
                            undoService, "Edit UISlider Handle Color", selectedEntity, &UISliderComponent::HandleColor, uiSlider->HandleColor);
                        ImGui::TreePop();
                    }
                }
                else
                {
                    ImGui::TextUnformatted("Background Color");
                    ImGui::ColorEdit4("##UISliderBackgroundColor", &uiSlider->BackgroundColor.r);
                    TrackInteractiveMemberMutation<UISliderComponent>(
                        undoService, "Edit UISlider Background Color", selectedEntity, &UISliderComponent::BackgroundColor, uiSlider->BackgroundColor);
                    ImGui::TextUnformatted("Fill Color");
                    ImGui::ColorEdit4("##UISliderFillColor", &uiSlider->FillColor.r);
                    TrackInteractiveMemberMutation<UISliderComponent>(
                        undoService, "Edit UISlider Fill Color", selectedEntity, &UISliderComponent::FillColor, uiSlider->FillColor);
                    ImGui::TextUnformatted("Handle Color");
                    ImGui::ColorEdit4("##UISliderHandleColor", &uiSlider->HandleColor.r);
                    TrackInteractiveMemberMutation<UISliderComponent>(
                        undoService, "Edit UISlider Handle Color", selectedEntity, &UISliderComponent::HandleColor, uiSlider->HandleColor);
                }
                ImGui::TextUnformatted("Handle Width");
                ImGui::DragFloat("##UISliderHandleWidth", &uiSlider->HandleWidth, 0.5f, 1.0f, 4096.0f);
                uiSlider->HandleWidth = std::max(1.0f, uiSlider->HandleWidth);
                TrackInteractiveMemberMutation<UISliderComponent>(
                    undoService, "Edit UISlider Handle Width", selectedEntity, &UISliderComponent::HandleWidth, uiSlider->HandleWidth);
                ImGui::TextUnformatted("Handle Height Multiplier");
                ImGui::DragFloat("##UISliderHandleHeightMultiplier", &uiSlider->HandleHeightMultiplier, 0.01f, 0.1f, 8.0f);
                uiSlider->HandleHeightMultiplier = std::max(0.1f, uiSlider->HandleHeightMultiplier);
                TrackInteractiveMemberMutation<UISliderComponent>(
                    undoService, "Edit UISlider Handle Height Multiplier", selectedEntity, &UISliderComponent::HandleHeightMultiplier, uiSlider->HandleHeightMultiplier);
                ImGui::TextUnformatted("Show Handle");
                ImGui::Checkbox("##UISliderShowHandle", &uiSlider->ShowHandle);
                TrackInteractiveMemberMutation<UISliderComponent>(
                    undoService, "Edit UISlider Show Handle", selectedEntity, &UISliderComponent::ShowHandle, uiSlider->ShowHandle);
                std::array<char, 256> onValueChangedEventBuffer{};
                std::snprintf(onValueChangedEventBuffer.data(), onValueChangedEventBuffer.size(), "%s", uiSlider->OnValueChangedEvent.c_str());
                ImGui::TextUnformatted("On Value Changed Event");
                if (ImGui::InputText("##UISliderOnValueChangedEvent", onValueChangedEventBuffer.data(), onValueChangedEventBuffer.size()))
                    uiSlider->OnValueChangedEvent = onValueChangedEventBuffer.data();
                TrackInteractiveMemberMutation<UISliderComponent>(
                    undoService,
                    "Edit UISlider OnValueChanged Event",
                    selectedEntity,
                    &UISliderComponent::OnValueChangedEvent,
                    uiSlider->OnValueChangedEvent);

                if (sliderUsesVisualChildren)
                    SyncSliderVisualChildrenInEditor(registry, selectedEntity, *uiSlider);
                ImGui::TreePop();
            }
        }
    }
}
