#include "EditorInspectorPanelEntityComponents.h"

#include "EditorAssetNaming.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetPaths.h"
#include "Assets/SpriteImportSettings.h"
#include "Assets/TextureAssetImporter.h"
#include "Assets/AssetTypes.h"
#include "Assets/AudioClipAsset.h"
#include "Project/ProjectManager.h"
#include "Scene/ParticleEmitterSystem.h"
#include "Undo/EditorUndoService.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
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

        template<typename TValue, typename TApply>
        void TrackInteractiveValueMutation(EditorUndoService* undoService,
                                           const char* label,
                                           const TValue& currentValue,
                                           TApply&& applyValue)
        {
            if (!undoService || !label)
                return;

            const ImGuiID itemId = ImGui::GetItemID();
            if (itemId == 0)
                return;

            using DecayedValueType = std::decay_t<TValue>;
            static std::unordered_map<ImGuiID, DecayedValueType> beforeValues;
            if (ImGui::IsItemActivated())
                beforeValues[itemId] = currentValue;

            if (!ImGui::IsItemDeactivatedAfterEdit())
                return;

            const auto beforeIt = beforeValues.find(itemId);
            if (beforeIt == beforeValues.end())
                return;

            const DecayedValueType beforeValue = beforeIt->second;
            beforeValues.erase(beforeIt);

            const DecayedValueType afterValue = currentValue;
            std::function<bool(const DecayedValueType&)> applyCallback = std::forward<TApply>(applyValue);
            (void)undoService->ExecuteValueMutation(label, beforeValue, afterValue, std::move(applyCallback));
        }

        template<typename TComponent, typename TValue>
        void TrackInteractiveMemberMutation(EditorUndoService* undoService,
                                            const char* label,
                                            entt::entity entity,
                                            TValue TComponent::* member,
                                            const TValue& currentValue,
                                            const std::function<void(Scene&, TComponent&)>& onApply = {})
        {
            TrackInteractiveValueMutation(undoService, label, currentValue, [undoService, entity, member, onApply](const TValue& value) {
                if (!undoService)
                    return false;
                Scene* activeScene = undoService->GetActiveScene();
                if (!activeScene || !activeScene->IsValid(entity))
                    return false;

                auto* component = activeScene->GetRegistry().try_get<TComponent>(entity);
                if (!component)
                    return false;

                component->*member = value;
                if (onApply)
                    onApply(*activeScene, *component);
                return true;
            });
        }

        constexpr const char* kSubSpritePayloadId = "SUB_SPRITE_KEY";

        struct SpriteDropAssignment
        {
            std::string TextureKey;
            int32_t SubSpriteIndex = -1;
            glm::vec2 UvMin = glm::vec2(0.0f);
            glm::vec2 UvMax = glm::vec2(1.0f);
        };

        struct AnimatorParametersSnapshot
        {
            std::unordered_map<std::string, bool> BoolParameters;
            std::unordered_map<std::string, float> FloatParameters;
            std::unordered_map<std::string, int32_t> IntegerParameters;
            std::unordered_map<std::string, bool> TriggerParameters;

            bool operator==(const AnimatorParametersSnapshot& other) const = default;
        };

        bool ResolveSpriteDropAssignment(const std::string& droppedKey, SpriteDropAssignment& outAssignment)
        {
            outAssignment = SpriteDropAssignment{};
            if (droppedKey.empty())
                return false;

            std::string textureKey;
            int32_t subSpriteIndex = -1;
            if (!Assets::TryParseSubSpriteAssetKey(droppedKey, textureKey, subSpriteIndex))
            {
                outAssignment.TextureKey = droppedKey;
                return true;
            }

            outAssignment.TextureKey = textureKey;
            if (textureKey.empty())
                return false;

            const auto settings = Assets::LoadSpriteImportSettings(textureKey);
            if (subSpriteIndex < 0 || subSpriteIndex >= static_cast<int32_t>(settings.SubSprites.size()))
                return true;

            auto textureAsset = Assets::AssetManager::LoadBlocking<Assets::TextureAsset>(textureKey);
            if (!textureAsset || !textureAsset->GetTexture())
                return true;

            const auto uvs = Assets::ComputeSubSpriteUvs(
                settings.SubSprites[static_cast<size_t>(subSpriteIndex)].RectPixels,
                textureAsset->GetTexture()->GetWidth(),
                textureAsset->GetTexture()->GetHeight());
            outAssignment.SubSpriteIndex = subSpriteIndex;
            outAssignment.UvMin = glm::vec2(uvs.x, uvs.y);
            outAssignment.UvMax = glm::vec2(uvs.z, uvs.w);
            return true;
        }

        std::string ResolveTextureKeyFromDroppedKey(const std::string& droppedKey)
        {
            std::string textureKey;
            int32_t subSpriteIndex = -1;
            if (Assets::TryParseSubSpriteAssetKey(droppedKey, textureKey, subSpriteIndex))
                return textureKey;
            return droppedKey;
        }

        bool IsAssetKeyUnderOpenProjectAssets(const std::string& assetKey)
        {
            if (assetKey.empty())
                return false;

            const auto& projectManager = Project::ProjectManager::GetInstance();
            if (!projectManager.HasOpenProject())
                return true;

            const auto resolvedResult = Assets::ResolveAssetKeyToPath(assetKey);
            if (resolvedResult.IsFailure())
                return false;

            std::error_code ec;
            const std::filesystem::path resolvedPath = std::filesystem::weakly_canonical(resolvedResult.GetValue(), ec);
            if (ec)
                return false;

            ec.clear();
            if (!std::filesystem::exists(resolvedPath, ec))
                return false;

            ec.clear();
            const std::filesystem::path assetsRoot = std::filesystem::weakly_canonical(projectManager.GetProjectRoot() / "Assets", ec);
            if (ec)
                return false;

            ec.clear();
            const std::filesystem::path rel = std::filesystem::relative(resolvedPath, assetsRoot, ec);
            if (ec)
                return false;
            if (rel.empty())
                return true;

            const std::string relText = rel.generic_string();
            return !(relText == ".." || relText.rfind("../", 0) == 0);
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
                if (!IsAssetKeyUnderOpenProjectAssets(record.Key))
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
                if (!IsAssetKeyUnderOpenProjectAssets(record.Key))
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
                if (!IsAssetKeyUnderOpenProjectAssets(record.Key))
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
                if (!IsAssetKeyUnderOpenProjectAssets(record.Key))
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
                                             std::string& selectedAnimationClipAssetKey,
                                             std::string& selectedAnimatorControllerAssetKey,
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

            ImGui::TextUnformatted("Enabled");
            ImGui::Checkbox("##EntityEnabled", &tag->Enabled);
            TrackInteractiveMemberMutation<TagComponent>(
                undoService, "Edit Entity Enabled", selectedEntity, &TagComponent::Enabled, tag->Enabled);
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
                ImGui::TextUnformatted("Position");
                const bool positionChanged = ImGui::DragFloat3("##TransformPosition", &transform->Position.x, 0.1f);
                if (positionChanged && scene)
                    scene->MarkTransformDirty(selectedEntity);
                TrackInteractiveMemberMutation<TransformComponent>(
                    undoService,
                    "Edit Transform Position",
                    selectedEntity,
                    &TransformComponent::Position,
                    transform->Position,
                    [selectedEntity](Scene& activeScene, TransformComponent&) {
                        activeScene.MarkTransformDirty(selectedEntity);
                    });
                ImGui::TextUnformatted("Rotation");
                const bool rotationChanged = ImGui::DragFloat3("##TransformRotation", &transform->Rotation.x, 1.0f);
                if (rotationChanged && scene)
                    scene->MarkTransformDirty(selectedEntity);
                TrackInteractiveMemberMutation<TransformComponent>(
                    undoService,
                    "Edit Transform Rotation",
                    selectedEntity,
                    &TransformComponent::Rotation,
                    transform->Rotation,
                    [selectedEntity](Scene& activeScene, TransformComponent&) {
                        activeScene.MarkTransformDirty(selectedEntity);
                    });
                ImGui::TextUnformatted("Scale");
                const bool scaleChanged = ImGui::DragFloat3("##TransformScale", &transform->Scale.x, 0.1f);
                if (scaleChanged && scene)
                    scene->MarkTransformDirty(selectedEntity);
                TrackInteractiveMemberMutation<TransformComponent>(
                    undoService,
                    "Edit Transform Scale",
                    selectedEntity,
                    &TransformComponent::Scale,
                    transform->Scale,
                    [selectedEntity](Scene& activeScene, TransformComponent&) {
                        activeScene.MarkTransformDirty(selectedEntity);
                    });
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
                ImGui::DragFloat2("##CanvasReferenceResolution", &canvas->ReferenceResolution.x, 1.0f, 1.0f, 16384.0f, "%.0f");
                canvas->ReferenceResolution.x = std::max(1.0f, canvas->ReferenceResolution.x);
                canvas->ReferenceResolution.y = std::max(1.0f, canvas->ReferenceResolution.y);
                TrackInteractiveMemberMutation<CanvasComponent>(
                    undoService,
                    "Edit Canvas Reference Resolution",
                    selectedEntity,
                    &CanvasComponent::ReferenceResolution,
                    canvas->ReferenceResolution);
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
                ImGui::DragFloat2("##RectTransformAnchorMin", &rectTransform->AnchorMin.x, 0.01f, 0.0f, 1.0f);
                rectTransform->AnchorMin.x = std::clamp(rectTransform->AnchorMin.x, 0.0f, 1.0f);
                rectTransform->AnchorMin.y = std::clamp(rectTransform->AnchorMin.y, 0.0f, 1.0f);
                TrackInteractiveMemberMutation<RectTransformComponent>(
                    undoService, "Edit RectTransform Anchor Min", selectedEntity, &RectTransformComponent::AnchorMin, rectTransform->AnchorMin);

                ImGui::TextUnformatted("Anchor Max");
                ImGui::DragFloat2("##RectTransformAnchorMax", &rectTransform->AnchorMax.x, 0.01f, 0.0f, 1.0f);
                rectTransform->AnchorMax.x = std::clamp(rectTransform->AnchorMax.x, 0.0f, 1.0f);
                rectTransform->AnchorMax.y = std::clamp(rectTransform->AnchorMax.y, 0.0f, 1.0f);
                TrackInteractiveMemberMutation<RectTransformComponent>(
                    undoService, "Edit RectTransform Anchor Max", selectedEntity, &RectTransformComponent::AnchorMax, rectTransform->AnchorMax);

                if (rectTransform->AnchorMin.x > rectTransform->AnchorMax.x)
                    std::swap(rectTransform->AnchorMin.x, rectTransform->AnchorMax.x);
                if (rectTransform->AnchorMin.y > rectTransform->AnchorMax.y)
                    std::swap(rectTransform->AnchorMin.y, rectTransform->AnchorMax.y);

                ImGui::TextUnformatted("Pivot");
                ImGui::DragFloat2("##RectTransformPivot", &rectTransform->Pivot.x, 0.01f, 0.0f, 1.0f);
                rectTransform->Pivot.x = std::clamp(rectTransform->Pivot.x, 0.0f, 1.0f);
                rectTransform->Pivot.y = std::clamp(rectTransform->Pivot.y, 0.0f, 1.0f);
                TrackInteractiveMemberMutation<RectTransformComponent>(
                    undoService, "Edit RectTransform Pivot", selectedEntity, &RectTransformComponent::Pivot, rectTransform->Pivot);

                ImGui::TextUnformatted("Size Delta");
                ImGui::DragFloat2("##RectTransformSizeDelta", &rectTransform->SizeDelta.x, 1.0f, -16384.0f, 16384.0f);
                TrackInteractiveMemberMutation<RectTransformComponent>(
                    undoService, "Edit RectTransform Size Delta", selectedEntity, &RectTransformComponent::SizeDelta, rectTransform->SizeDelta);

                ImGui::TextUnformatted("Anchored Position");
                ImGui::DragFloat2("##RectTransformAnchoredPosition", &rectTransform->AnchoredPosition.x, 1.0f);
                TrackInteractiveMemberMutation<RectTransformComponent>(
                    undoService,
                    "Edit RectTransform Anchored Position",
                    selectedEntity,
                    &RectTransformComponent::AnchoredPosition,
                    rectTransform->AnchoredPosition);

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
                    ImGui::TextUnformatted("Color");
                    ImGui::ColorEdit4("##SpriteColor", &sprite->Color.r);
                    TrackInteractiveMemberMutation<SpriteComponent>(
                        undoService, "Edit Sprite Color", selectedEntity, &SpriteComponent::Color, sprite->Color);
                    ImGui::TextUnformatted("Tiling Factor");
                    ImGui::DragFloat2("##SpriteTilingFactor", &sprite->TilingFactor.x, 0.05f, 0.001f, 100.0f, "%.3f");
                    sprite->TilingFactor.x = std::max(0.001f, sprite->TilingFactor.x);
                    sprite->TilingFactor.y = std::max(0.001f, sprite->TilingFactor.y);
                    TrackInteractiveMemberMutation<SpriteComponent>(
                        undoService, "Edit Sprite Tiling Factor", selectedEntity, &SpriteComponent::TilingFactor, sprite->TilingFactor);
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
                ImGui::TextUnformatted("Enabled");
                ImGui::Checkbox("##AnimationEventReceiverEnabled", &animationEventReceiver->Enabled);
                TrackInteractiveMemberMutation<AnimationEventReceiverComponent>(
                    undoService,
                    "Edit Animation Event Receiver Enabled",
                    selectedEntity,
                    &AnimationEventReceiverComponent::Enabled,
                    animationEventReceiver->Enabled);

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
                ImGui::TextUnformatted("Enabled");
                ImGui::Checkbox("##AudioListenerEnabled", &audioListener->Enabled);
                TrackInteractiveMemberMutation<AudioListener2DComponent>(
                    undoService, "Edit Audio Listener 2D Enabled", selectedEntity, &AudioListener2DComponent::Enabled, audioListener->Enabled);
                ImGui::TextUnformatted("Use Primary Camera Position");
                ImGui::Checkbox("##AudioListenerUsePrimaryCameraPosition", &audioListener->UsePrimaryCameraPosition);
                TrackInteractiveMemberMutation<AudioListener2DComponent>(
                    undoService,
                    "Edit Audio Listener 2D Camera Follow",
                    selectedEntity,
                    &AudioListener2DComponent::UsePrimaryCameraPosition,
                    audioListener->UsePrimaryCameraPosition);
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
                ImGui::TextUnformatted("Cell Size");
                ImGui::DragFloat2("##Grid2DCellSize", &grid2D->CellSize.x, 0.05f, 0.001f, 100.0f);
                TrackInteractiveMemberMutation<Grid2DComponent>(
                    undoService, "Edit Grid2D Cell Size", selectedEntity, &Grid2DComponent::CellSize, grid2D->CellSize);
                ImGui::TextUnformatted("Cell Gap");
                ImGui::DragFloat2("##Grid2DCellGap", &grid2D->CellGap.x, 0.01f, 0.0f, 10.0f);
                TrackInteractiveMemberMutation<Grid2DComponent>(
                    undoService, "Edit Grid2D Cell Gap", selectedEntity, &Grid2DComponent::CellGap, grid2D->CellGap);

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
                ImGui::TextUnformatted("Grid Size");
                if (ImGui::DragInt2("##TilemapGridSize", &gridSize.x, 1.0f, 1, 4096))
                {
                    gridSize = glm::ivec2(std::max(1, gridSize.x), std::max(1, gridSize.y));
                    if (gridSize != tilemapLayer->GridSize)
                        tilemapLayer->ResizeGrid(gridSize);
                }
                TrackInteractiveValueMutation(undoService, "Edit TilemapLayer Grid Size", tilemapLayer->GridSize, [undoService, selectedEntity](const glm::ivec2& value) {
                    if (!undoService)
                        return false;
                    Scene* activeScene = undoService->GetActiveScene();
                    if (!activeScene || !activeScene->IsValid(selectedEntity))
                        return false;
                    auto* activeLayer = activeScene->GetRegistry().try_get<TilemapLayerComponent>(selectedEntity);
                    if (!activeLayer)
                        return false;
                    const glm::ivec2 clampedSize(std::max(1, value.x), std::max(1, value.y));
                    if (clampedSize != activeLayer->GridSize)
                        activeLayer->ResizeGrid(clampedSize);
                    return true;
                });

                ImGui::TextUnformatted("Render Order");
                ImGui::DragInt("##TilemapRenderOrder", &tilemapLayer->RenderOrder);
                TrackInteractiveMemberMutation<TilemapLayerComponent>(
                    undoService, "Edit TilemapLayer Render Order", selectedEntity, &TilemapLayerComponent::RenderOrder, tilemapLayer->RenderOrder);

                ImGui::TextUnformatted("Collision Enabled");
                ImGui::Checkbox("##TilemapCollisionEnabled", &tilemapLayer->CollisionEnabled);
                TrackInteractiveMemberMutation<TilemapLayerComponent>(
                    undoService,
                    "Edit TilemapLayer Collision",
                    selectedEntity,
                    &TilemapLayerComponent::CollisionEnabled,
                    tilemapLayer->CollisionEnabled);

                ImGui::TextUnformatted("Cast Shadows");
                ImGui::Checkbox("##TilemapCastShadows", &tilemapLayer->CastShadows);
                TrackInteractiveMemberMutation<TilemapLayerComponent>(
                    undoService, "Edit TilemapLayer Cast Shadows", selectedEntity, &TilemapLayerComponent::CastShadows, tilemapLayer->CastShadows);

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
                ImGui::Button((clipLabel + "##AudioClip").c_str(),
                              ImVec2(std::max(60.0f, ImGui::GetContentRegionAvail().x - 90.0f), 0.0f));
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

                ImGui::TextUnformatted("Play On Start");
                ImGui::Checkbox("##AudioSourcePlayOnStart", &audioSource->PlayOnStart);
                TrackInteractiveMemberMutation<AudioSourceComponent>(
                    undoService, "Edit Audio Play On Start", selectedEntity, &AudioSourceComponent::PlayOnStart, audioSource->PlayOnStart);
                ImGui::TextUnformatted("Loop");
                ImGui::Checkbox("##AudioSourceLoop", &audioSource->Loop);
                TrackInteractiveMemberMutation<AudioSourceComponent>(
                    undoService, "Edit Audio Loop", selectedEntity, &AudioSourceComponent::Loop, audioSource->Loop);
                ImGui::TextUnformatted("Muted");
                ImGui::Checkbox("##AudioSourceMuted", &audioSource->Muted);
                TrackInteractiveMemberMutation<AudioSourceComponent>(
                    undoService, "Edit Audio Muted", selectedEntity, &AudioSourceComponent::Muted, audioSource->Muted);
                ImGui::TextUnformatted("Volume");
                ImGui::SliderFloat("##AudioSourceVolume", &audioSource->Volume, 0.0f, 2.0f, "%.2f");
                TrackInteractiveMemberMutation<AudioSourceComponent>(
                    undoService, "Edit Audio Volume", selectedEntity, &AudioSourceComponent::Volume, audioSource->Volume);
                ImGui::TextUnformatted("Pitch");
                ImGui::SliderFloat("##AudioSourcePitch", &audioSource->Pitch, 0.1f, 4.0f, "%.2f");
                audioSource->Pitch = std::max(0.01f, audioSource->Pitch);
                TrackInteractiveMemberMutation<AudioSourceComponent>(
                    undoService, "Edit Audio Pitch", selectedEntity, &AudioSourceComponent::Pitch, audioSource->Pitch);

                int playbackSpaceIndex = static_cast<int>(audioSource->Space);
                const char* playbackSpaceNames[] = { "Global", "Spatial 2D" };
                ImGui::TextUnformatted("Playback Space");
                if (ImGui::Combo("##AudioSourcePlaybackSpace", &playbackSpaceIndex, playbackSpaceNames, 2))
                    audioSource->Space = static_cast<AudioSourceComponent::PlaybackSpace>(playbackSpaceIndex);
                TrackInteractiveMemberMutation<AudioSourceComponent>(
                    undoService, "Edit Audio Playback Space", selectedEntity, &AudioSourceComponent::Space, audioSource->Space);

                std::array<char, 128> mixerGroupBuffer{};
                std::snprintf(mixerGroupBuffer.data(), mixerGroupBuffer.size(), "%s", audioSource->MixerGroup.c_str());
                ImGui::TextUnformatted("Mixer Group");
                if (ImGui::InputText("##AudioSourceMixerGroup", mixerGroupBuffer.data(), mixerGroupBuffer.size()))
                    audioSource->MixerGroup = mixerGroupBuffer.data();
                if (audioSource->MixerGroup.empty())
                    audioSource->MixerGroup = "SFX";
                TrackInteractiveMemberMutation<AudioSourceComponent>(
                    undoService, "Edit Audio Mixer Group", selectedEntity, &AudioSourceComponent::MixerGroup, audioSource->MixerGroup);

                if (audioSource->Space == AudioSourceComponent::PlaybackSpace::Spatial2D)
                {
                    ImGui::TextUnformatted("Min Distance");
                    ImGui::DragFloat("##AudioSourceSpatialMinDistance", &audioSource->SpatialMinDistance, 0.01f, 0.001f, 10000.0f, "%.3f");
                    audioSource->SpatialMinDistance = std::max(0.001f, audioSource->SpatialMinDistance);
                    TrackInteractiveMemberMutation<AudioSourceComponent>(
                        undoService,
                        "Edit Audio Spatial Min Distance",
                        selectedEntity,
                        &AudioSourceComponent::SpatialMinDistance,
                        audioSource->SpatialMinDistance);

                    ImGui::TextUnformatted("Max Distance");
                    ImGui::DragFloat("##AudioSourceSpatialMaxDistance", &audioSource->SpatialMaxDistance, 0.05f, 0.001f, 10000.0f, "%.3f");
                    audioSource->SpatialMaxDistance = std::max(audioSource->SpatialMinDistance, audioSource->SpatialMaxDistance);
                    TrackInteractiveMemberMutation<AudioSourceComponent>(
                        undoService,
                        "Edit Audio Spatial Max Distance",
                        selectedEntity,
                        &AudioSourceComponent::SpatialMaxDistance,
                        audioSource->SpatialMaxDistance);

                    ImGui::TextUnformatted("Rolloff Exponent");
                    ImGui::DragFloat("##AudioSourceSpatialRolloffExponent", &audioSource->SpatialRolloffExponent, 0.01f, 0.01f, 16.0f, "%.2f");
                    audioSource->SpatialRolloffExponent = std::max(0.01f, audioSource->SpatialRolloffExponent);
                    TrackInteractiveMemberMutation<AudioSourceComponent>(
                        undoService,
                        "Edit Audio Spatial Rolloff Exponent",
                        selectedEntity,
                        &AudioSourceComponent::SpatialRolloffExponent,
                        audioSource->SpatialRolloffExponent);

                    ImGui::TextUnformatted("Stereo Pan Strength");
                    ImGui::SliderFloat("##AudioSourceStereoPanStrength", &audioSource->StereoPanStrength, 0.0f, 1.0f, "%.2f");
                    audioSource->StereoPanStrength = std::clamp(audioSource->StereoPanStrength, 0.0f, 1.0f);
                    TrackInteractiveMemberMutation<AudioSourceComponent>(
                        undoService,
                        "Edit Audio Spatial Pan Strength",
                        selectedEntity,
                        &AudioSourceComponent::StereoPanStrength,
                        audioSource->StereoPanStrength);

                    std::array<char, 256> attenuationCurveBuffer{};
                    std::snprintf(attenuationCurveBuffer.data(), attenuationCurveBuffer.size(), "%s", audioSource->AttenuationCurveKey.c_str());
                    ImGui::TextUnformatted("Attenuation Curve Key");
                    if (ImGui::InputText("##AudioSourceAttenuationCurveKey", attenuationCurveBuffer.data(), attenuationCurveBuffer.size()))
                        audioSource->AttenuationCurveKey = attenuationCurveBuffer.data();
                    TrackInteractiveMemberMutation<AudioSourceComponent>(
                        undoService,
                        "Edit Audio Attenuation Curve Key",
                        selectedEntity,
                        &AudioSourceComponent::AttenuationCurveKey,
                        audioSource->AttenuationCurveKey);
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
                ImGui::TextUnformatted("Body Type");
                if (ImGui::Combo("##Rigidbody2DBodyType", &bodyTypeIndex, bodyTypeNames, 3))
                    rigidbody2D->Type = static_cast<Rigidbody2DComponent::BodyType>(bodyTypeIndex);
                TrackInteractiveMemberMutation<Rigidbody2DComponent>(
                    undoService, "Edit Rigidbody2D Body Type", selectedEntity, &Rigidbody2DComponent::Type, rigidbody2D->Type);
                if (rigidbody2D->Type == Rigidbody2DComponent::BodyType::Kinematic)
                {
                    ImGui::TextWrapped(
                        "Note: Box2D kinematic bodies do not generate physical collision response against static/kinematic bodies "
                        "(including static tilemap colliders). Use Dynamic + Gravity Scale 0 for moving collidable platforms.");
                }
                const uint16_t sceneWorldCount = scene ? std::max<uint16_t>(1, scene->GetPhysics2DWorldCount()) : 1;
                int physicsWorldSlot = std::min<int>(rigidbody2D->PhysicsWorldSlot, static_cast<int>(sceneWorldCount - 1));
                ImGui::TextUnformatted("Physics World Slot");
                ImGui::DragInt("##Rigidbody2DPhysicsWorldSlot", &physicsWorldSlot, 1.0f, 0, static_cast<int>(sceneWorldCount - 1));
                physicsWorldSlot = std::clamp(physicsWorldSlot, 0, static_cast<int>(sceneWorldCount - 1));
                rigidbody2D->PhysicsWorldSlot = static_cast<uint16_t>(physicsWorldSlot);
                TrackInteractiveMemberMutation<Rigidbody2DComponent>(
                    undoService,
                    "Edit Rigidbody2D Physics World Slot",
                    selectedEntity,
                    &Rigidbody2DComponent::PhysicsWorldSlot,
                    rigidbody2D->PhysicsWorldSlot);

                bool freezeRotation = rigidbody2D->IsRotationLocked();
                ImGui::TextDisabled("Constraints");
                ImGui::TextUnformatted("Freeze Position X");
                ImGui::Checkbox("##Rigidbody2DFreezePositionX", &rigidbody2D->FreezePositionX);
                TrackInteractiveMemberMutation<Rigidbody2DComponent>(
                    undoService, "Edit Rigidbody2D Freeze Position X", selectedEntity, &Rigidbody2DComponent::FreezePositionX, rigidbody2D->FreezePositionX);
                ImGui::TextUnformatted("Freeze Position Y");
                ImGui::Checkbox("##Rigidbody2DFreezePositionY", &rigidbody2D->FreezePositionY);
                TrackInteractiveMemberMutation<Rigidbody2DComponent>(
                    undoService, "Edit Rigidbody2D Freeze Position Y", selectedEntity, &Rigidbody2DComponent::FreezePositionY, rigidbody2D->FreezePositionY);
                ImGui::TextUnformatted("Freeze Rotation");
                if (ImGui::Checkbox("##Rigidbody2DFreezeRotation", &freezeRotation))
                {
                    rigidbody2D->FixedRotation = freezeRotation;
                }
                TrackInteractiveMemberMutation<Rigidbody2DComponent>(
                    undoService, "Edit Rigidbody2D Freeze Rotation", selectedEntity, &Rigidbody2DComponent::FixedRotation, rigidbody2D->FixedRotation);
                ImGui::TextUnformatted("Use CCD");
                ImGui::Checkbox("##Rigidbody2DUseCcd", &rigidbody2D->UseCCD);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip("Continuous Collision Detection. Use for fast-moving bodies to reduce tunneling through colliders.");
                TrackInteractiveMemberMutation<Rigidbody2DComponent>(
                    undoService, "Edit Rigidbody2D Use CCD", selectedEntity, &Rigidbody2DComponent::UseCCD, rigidbody2D->UseCCD);
                ImGui::TextUnformatted("Enable Sleep");
                ImGui::Checkbox("##Rigidbody2DEnableSleep", &rigidbody2D->EnableSleep);
                TrackInteractiveMemberMutation<Rigidbody2DComponent>(
                    undoService, "Edit Rigidbody2D Enable Sleep", selectedEntity, &Rigidbody2DComponent::EnableSleep, rigidbody2D->EnableSleep);
                ImGui::TextUnformatted("Start Awake");
                ImGui::Checkbox("##Rigidbody2DStartAwake", &rigidbody2D->StartAwake);
                TrackInteractiveMemberMutation<Rigidbody2DComponent>(
                    undoService, "Edit Rigidbody2D Start Awake", selectedEntity, &Rigidbody2DComponent::StartAwake, rigidbody2D->StartAwake);
                ImGui::TextUnformatted("Interpolate");
                ImGui::Checkbox("##Rigidbody2DInterpolate", &rigidbody2D->Interpolate);
                TrackInteractiveMemberMutation<Rigidbody2DComponent>(
                    undoService, "Edit Rigidbody2D Interpolate", selectedEntity, &Rigidbody2DComponent::Interpolate, rigidbody2D->Interpolate);
                ImGui::TextUnformatted("High Contact Quality");
                ImGui::Checkbox("##Rigidbody2DHighContactQuality", &rigidbody2D->HighContactQuality);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip("Applies extra world solver sub-steps when this body is present. Useful for rotating platforms and dense contact stacks.");
                TrackInteractiveMemberMutation<Rigidbody2DComponent>(
                    undoService,
                    "Edit Rigidbody2D High Contact Quality",
                    selectedEntity,
                    &Rigidbody2DComponent::HighContactQuality,
                    rigidbody2D->HighContactQuality);
                ImGui::TextUnformatted("Extra Solver Sub Steps");
                ImGui::DragInt("##Rigidbody2DExtraSolverSubSteps", &rigidbody2D->ExtraSolverSubSteps, 1.0f, 0, 24);
                rigidbody2D->ExtraSolverSubSteps = std::max(0, rigidbody2D->ExtraSolverSubSteps);
                TrackInteractiveMemberMutation<Rigidbody2DComponent>(
                    undoService,
                    "Edit Rigidbody2D Extra Solver Sub Steps",
                    selectedEntity,
                    &Rigidbody2DComponent::ExtraSolverSubSteps,
                    rigidbody2D->ExtraSolverSubSteps);
                ImGui::TextUnformatted("Gravity Scale");
                ImGui::DragFloat("##Rigidbody2DGravityScale", &rigidbody2D->GravityScale, 0.01f, -10.0f, 10.0f);
                TrackInteractiveMemberMutation<Rigidbody2DComponent>(
                    undoService, "Edit Rigidbody2D Gravity Scale", selectedEntity, &Rigidbody2DComponent::GravityScale, rigidbody2D->GravityScale);
                ImGui::TextUnformatted("Linear Damping");
                ImGui::DragFloat("##Rigidbody2DLinearDamping", &rigidbody2D->LinearDamping, 0.01f, 0.0f, 100.0f);
                TrackInteractiveMemberMutation<Rigidbody2DComponent>(
                    undoService, "Edit Rigidbody2D Linear Damping", selectedEntity, &Rigidbody2DComponent::LinearDamping, rigidbody2D->LinearDamping);
                ImGui::TextUnformatted("Angular Damping");
                ImGui::DragFloat("##Rigidbody2DAngularDamping", &rigidbody2D->AngularDamping, 0.01f, 0.0f, 100.0f);
                TrackInteractiveMemberMutation<Rigidbody2DComponent>(
                    undoService, "Edit Rigidbody2D Angular Damping", selectedEntity, &Rigidbody2DComponent::AngularDamping, rigidbody2D->AngularDamping);

                ImGui::Separator();
                ImGui::TextDisabled("Runtime Diagnostics");
                if (!scene)
                {
                    ImGui::TextDisabled("Scene unavailable.");
                }
                else
                {
                    const uint16_t worldSlot = std::min<uint16_t>(rigidbody2D->PhysicsWorldSlot, static_cast<uint16_t>(scene->GetPhysics2DWorldCount() - 1));
                    const Physics2DWorld* physicsWorld = scene->GetPhysics2DWorld(worldSlot);
                    if (!physicsWorld)
                    {
                        ImGui::TextDisabled("Physics world is not initialized.");
                    }
                    else
                    {
                        ImGui::Text("World Slot: %u", static_cast<unsigned>(worldSlot));
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
                ImGui::TextUnformatted("Offset");
                ImGui::DragFloat2("##BoxColliderOffset", &boxCollider2D->Offset.x, 0.01f);
                TrackInteractiveMemberMutation<BoxCollider2DComponent>(
                    undoService, "Edit BoxCollider2D Offset", selectedEntity, &BoxCollider2DComponent::Offset, boxCollider2D->Offset);
                ImGui::TextUnformatted("Size");
                ImGui::DragFloat2("##BoxColliderSize", &boxCollider2D->Size.x, 0.01f, 0.001f, 1000.0f);
                TrackInteractiveMemberMutation<BoxCollider2DComponent>(
                    undoService, "Edit BoxCollider2D Size", selectedEntity, &BoxCollider2DComponent::Size, boxCollider2D->Size);
                ImGui::TextUnformatted("Density");
                ImGui::DragFloat("##BoxColliderDensity", &boxCollider2D->Density, 0.01f, 0.0f, 1000.0f);
                TrackInteractiveMemberMutation<BoxCollider2DComponent>(
                    undoService, "Edit BoxCollider2D Density", selectedEntity, &BoxCollider2DComponent::Density, boxCollider2D->Density);
                ImGui::TextUnformatted("Friction");
                ImGui::DragFloat("##BoxColliderFriction", &boxCollider2D->Friction, 0.01f, 0.0f, 1.0f);
                TrackInteractiveMemberMutation<BoxCollider2DComponent>(
                    undoService, "Edit BoxCollider2D Friction", selectedEntity, &BoxCollider2DComponent::Friction, boxCollider2D->Friction);
                ImGui::TextUnformatted("Restitution");
                ImGui::DragFloat("##BoxColliderRestitution", &boxCollider2D->Restitution, 0.01f, 0.0f, 1.0f);
                TrackInteractiveMemberMutation<BoxCollider2DComponent>(
                    undoService, "Edit BoxCollider2D Restitution", selectedEntity, &BoxCollider2DComponent::Restitution, boxCollider2D->Restitution);
                ImGui::TextUnformatted("Is Sensor");
                ImGui::Checkbox("##BoxColliderIsSensor", &boxCollider2D->IsSensor);
                TrackInteractiveMemberMutation<BoxCollider2DComponent>(
                    undoService, "Edit BoxCollider2D Sensor", selectedEntity, &BoxCollider2DComponent::IsSensor, boxCollider2D->IsSensor);
                ImGui::TextUnformatted("Layer Bits");
                ImGui::InputScalar("##BoxColliderLayerBits", ImGuiDataType_U64, &boxCollider2D->CollisionLayer);
                TrackInteractiveMemberMutation<BoxCollider2DComponent>(
                    undoService, "Edit BoxCollider2D Layer", selectedEntity, &BoxCollider2DComponent::CollisionLayer, boxCollider2D->CollisionLayer);
                ImGui::TextUnformatted("Mask Bits");
                ImGui::InputScalar("##BoxColliderMaskBits", ImGuiDataType_U64, &boxCollider2D->CollisionMask);
                TrackInteractiveMemberMutation<BoxCollider2DComponent>(
                    undoService, "Edit BoxCollider2D Mask", selectedEntity, &BoxCollider2DComponent::CollisionMask, boxCollider2D->CollisionMask);

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
                ImGui::TextUnformatted("Offset");
                ImGui::DragFloat2("##CircleColliderOffset", &circleCollider2D->Offset.x, 0.01f);
                TrackInteractiveMemberMutation<CircleCollider2DComponent>(
                    undoService, "Edit CircleCollider2D Offset", selectedEntity, &CircleCollider2DComponent::Offset, circleCollider2D->Offset);
                ImGui::TextUnformatted("Radius");
                ImGui::DragFloat("##CircleColliderRadius", &circleCollider2D->Radius, 0.01f, 0.001f, 1000.0f);
                TrackInteractiveMemberMutation<CircleCollider2DComponent>(
                    undoService, "Edit CircleCollider2D Radius", selectedEntity, &CircleCollider2DComponent::Radius, circleCollider2D->Radius);
                ImGui::TextUnformatted("Density");
                ImGui::DragFloat("##CircleColliderDensity", &circleCollider2D->Density, 0.01f, 0.0f, 1000.0f);
                TrackInteractiveMemberMutation<CircleCollider2DComponent>(
                    undoService, "Edit CircleCollider2D Density", selectedEntity, &CircleCollider2DComponent::Density, circleCollider2D->Density);
                ImGui::TextUnformatted("Friction");
                ImGui::DragFloat("##CircleColliderFriction", &circleCollider2D->Friction, 0.01f, 0.0f, 1.0f);
                TrackInteractiveMemberMutation<CircleCollider2DComponent>(
                    undoService, "Edit CircleCollider2D Friction", selectedEntity, &CircleCollider2DComponent::Friction, circleCollider2D->Friction);
                ImGui::TextUnformatted("Restitution");
                ImGui::DragFloat("##CircleColliderRestitution", &circleCollider2D->Restitution, 0.01f, 0.0f, 1.0f);
                TrackInteractiveMemberMutation<CircleCollider2DComponent>(
                    undoService, "Edit CircleCollider2D Restitution", selectedEntity, &CircleCollider2DComponent::Restitution, circleCollider2D->Restitution);
                ImGui::TextUnformatted("Is Sensor");
                ImGui::Checkbox("##CircleColliderIsSensor", &circleCollider2D->IsSensor);
                TrackInteractiveMemberMutation<CircleCollider2DComponent>(
                    undoService, "Edit CircleCollider2D Sensor", selectedEntity, &CircleCollider2DComponent::IsSensor, circleCollider2D->IsSensor);
                ImGui::TextUnformatted("Layer Bits");
                ImGui::InputScalar("##CircleColliderLayerBits", ImGuiDataType_U64, &circleCollider2D->CollisionLayer);
                TrackInteractiveMemberMutation<CircleCollider2DComponent>(
                    undoService, "Edit CircleCollider2D Layer", selectedEntity, &CircleCollider2DComponent::CollisionLayer, circleCollider2D->CollisionLayer);
                ImGui::TextUnformatted("Mask Bits");
                ImGui::InputScalar("##CircleColliderMaskBits", ImGuiDataType_U64, &circleCollider2D->CollisionMask);
                TrackInteractiveMemberMutation<CircleCollider2DComponent>(
                    undoService, "Edit CircleCollider2D Mask", selectedEntity, &CircleCollider2DComponent::CollisionMask, circleCollider2D->CollisionMask);

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
                ImGui::TextUnformatted("Enabled");
                ImGui::Checkbox("##DirectionalLightEnabled", &directionalLight->Enabled);
                TrackInteractiveMemberMutation<DirectionalLight2DComponent>(
                    undoService,
                    "Edit Directional Light Enabled",
                    selectedEntity,
                    &DirectionalLight2DComponent::Enabled,
                    directionalLight->Enabled);
                ImGui::TextUnformatted("Color");
                ImGui::ColorEdit3("##DirectionalLightColor", &directionalLight->Color.r);
                TrackInteractiveMemberMutation<DirectionalLight2DComponent>(
                    undoService, "Edit Directional Light Color", selectedEntity, &DirectionalLight2DComponent::Color, directionalLight->Color);
                ImGui::TextUnformatted("Intensity");
                ImGui::DragFloat("##DirectionalLightIntensity", &directionalLight->Intensity, 0.01f, 0.0f, 100.0f, "%.2f");
                TrackInteractiveMemberMutation<DirectionalLight2DComponent>(
                    undoService, "Edit Directional Light Intensity", selectedEntity, &DirectionalLight2DComponent::Intensity, directionalLight->Intensity);
                ImGui::TextUnformatted("Use Entity Rotation");
                ImGui::Checkbox("##DirectionalLightUseEntityRotation", &directionalLight->UseEntityRotation);
                TrackInteractiveMemberMutation<DirectionalLight2DComponent>(
                    undoService,
                    "Edit Directional Light Rotation Mode",
                    selectedEntity,
                    &DirectionalLight2DComponent::UseEntityRotation,
                    directionalLight->UseEntityRotation);
                if (!directionalLight->UseEntityRotation)
                {
                    ImGui::TextUnformatted("Direction");
                    ImGui::DragFloat2("##DirectionalLightDirection", &directionalLight->Direction.x, 0.01f, -1.0f, 1.0f, "%.3f");
                    directionalLight->Direction = NormalizeDirectionOrFallback(directionalLight->Direction);
                    TrackInteractiveMemberMutation<DirectionalLight2DComponent>(
                        undoService,
                        "Edit Directional Light Direction",
                        selectedEntity,
                        &DirectionalLight2DComponent::Direction,
                        directionalLight->Direction);
                }

                ImGui::TextUnformatted("Cast Shadows");
                ImGui::Checkbox("##DirectionalLightCastShadows", &directionalLight->CastShadows);
                TrackInteractiveMemberMutation<DirectionalLight2DComponent>(
                    undoService, "Edit Directional Light Cast Shadows", selectedEntity, &DirectionalLight2DComponent::CastShadows, directionalLight->CastShadows);
                ImGui::TextUnformatted("Shadow Strength");
                ImGui::DragFloat("##DirectionalLightShadowStrength", &directionalLight->ShadowStrength, 0.01f, 0.0f, 1.0f, "%.2f");
                directionalLight->ShadowStrength = std::clamp(directionalLight->ShadowStrength, 0.0f, 1.0f);
                TrackInteractiveMemberMutation<DirectionalLight2DComponent>(
                    undoService, "Edit Directional Light Shadow Strength", selectedEntity, &DirectionalLight2DComponent::ShadowStrength, directionalLight->ShadowStrength);
                ImGui::TextUnformatted("Shadow Softness");
                ImGui::DragFloat("##DirectionalLightShadowSoftness", &directionalLight->ShadowSoftness, 0.01f, 0.0f, 256.0f, "%.2f");
                directionalLight->ShadowSoftness = std::max(0.0f, directionalLight->ShadowSoftness);
                TrackInteractiveMemberMutation<DirectionalLight2DComponent>(
                    undoService, "Edit Directional Light Shadow Softness", selectedEntity, &DirectionalLight2DComponent::ShadowSoftness, directionalLight->ShadowSoftness);
                ImGui::TextUnformatted("Shadow Samples");
                ImGui::DragInt("##DirectionalLightShadowSamples", &directionalLight->ShadowSamples, 1.0f, 1, 32);
                directionalLight->ShadowSamples = std::clamp(directionalLight->ShadowSamples, 1, 32);
                TrackInteractiveMemberMutation<DirectionalLight2DComponent>(
                    undoService, "Edit Directional Light Shadow Samples", selectedEntity, &DirectionalLight2DComponent::ShadowSamples, directionalLight->ShadowSamples);
                ImGui::TextDisabled("Directional light intensity is global (no distance falloff).");
                ImGui::TextUnformatted("Shadow Distance");
                ImGui::DragFloat("##DirectionalLightShadowDistance", &directionalLight->ShadowDistance, 0.05f, 0.0f, 10000.0f, "%.2f");
                directionalLight->ShadowDistance = std::max(0.0f, directionalLight->ShadowDistance);
                TrackInteractiveMemberMutation<DirectionalLight2DComponent>(
                    undoService, "Edit Directional Light Shadow Distance", selectedEntity, &DirectionalLight2DComponent::ShadowDistance, directionalLight->ShadowDistance);
                ImGui::TextUnformatted("Shadow Bias");
                ImGui::DragFloat("##DirectionalLightShadowBias", &directionalLight->ShadowBias, 0.0005f, 0.0f, 2.0f, "%.4f");
                directionalLight->ShadowBias = std::max(0.0f, directionalLight->ShadowBias);
                TrackInteractiveMemberMutation<DirectionalLight2DComponent>(
                    undoService, "Edit Directional Light Shadow Bias", selectedEntity, &DirectionalLight2DComponent::ShadowBias, directionalLight->ShadowBias);

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
                ImGui::TextUnformatted("Enabled");
                ImGui::Checkbox("##PointLightEnabled", &pointLight->Enabled);
                TrackInteractiveMemberMutation<PointLight2DComponent>(
                    undoService, "Edit Point Light Enabled", selectedEntity, &PointLight2DComponent::Enabled, pointLight->Enabled);
                ImGui::TextUnformatted("Color");
                ImGui::ColorEdit3("##PointLightColor", &pointLight->Color.r);
                TrackInteractiveMemberMutation<PointLight2DComponent>(
                    undoService, "Edit Point Light Color", selectedEntity, &PointLight2DComponent::Color, pointLight->Color);
                ImGui::TextUnformatted("Intensity");
                ImGui::DragFloat("##PointLightIntensity", &pointLight->Intensity, 0.01f, 0.0f, 100.0f, "%.2f");
                TrackInteractiveMemberMutation<PointLight2DComponent>(
                    undoService, "Edit Point Light Intensity", selectedEntity, &PointLight2DComponent::Intensity, pointLight->Intensity);
                ImGui::TextUnformatted("Radius");
                ImGui::DragFloat("##PointLightRadius", &pointLight->Radius, 0.01f, 0.01f, 10000.0f, "%.2f");
                pointLight->Radius = std::max(0.01f, pointLight->Radius);
                TrackInteractiveMemberMutation<PointLight2DComponent>(
                    undoService, "Edit Point Light Radius", selectedEntity, &PointLight2DComponent::Radius, pointLight->Radius);
                ImGui::TextUnformatted("Falloff");
                ImGui::DragFloat("##PointLightFalloff", &pointLight->Falloff, 0.01f, 0.1f, 8.0f, "%.2f");
                pointLight->Falloff = std::max(0.1f, pointLight->Falloff);
                TrackInteractiveMemberMutation<PointLight2DComponent>(
                    undoService, "Edit Point Light Falloff", selectedEntity, &PointLight2DComponent::Falloff, pointLight->Falloff);
                ImGui::TextUnformatted("Cast Shadows");
                ImGui::Checkbox("##PointLightCastShadows", &pointLight->CastShadows);
                TrackInteractiveMemberMutation<PointLight2DComponent>(
                    undoService, "Edit Point Light Cast Shadows", selectedEntity, &PointLight2DComponent::CastShadows, pointLight->CastShadows);
                ImGui::TextUnformatted("Shadow Strength");
                ImGui::DragFloat("##PointLightShadowStrength", &pointLight->ShadowStrength, 0.01f, 0.0f, 1.0f, "%.2f");
                pointLight->ShadowStrength = std::clamp(pointLight->ShadowStrength, 0.0f, 1.0f);
                TrackInteractiveMemberMutation<PointLight2DComponent>(
                    undoService, "Edit Point Light Shadow Strength", selectedEntity, &PointLight2DComponent::ShadowStrength, pointLight->ShadowStrength);
                ImGui::TextUnformatted("Shadow Softness");
                ImGui::DragFloat("##PointLightShadowSoftness", &pointLight->ShadowSoftness, 0.01f, 0.0f, 256.0f, "%.2f");
                pointLight->ShadowSoftness = std::max(0.0f, pointLight->ShadowSoftness);
                TrackInteractiveMemberMutation<PointLight2DComponent>(
                    undoService, "Edit Point Light Shadow Softness", selectedEntity, &PointLight2DComponent::ShadowSoftness, pointLight->ShadowSoftness);
                ImGui::TextUnformatted("Shadow Samples");
                ImGui::DragInt("##PointLightShadowSamples", &pointLight->ShadowSamples, 1.0f, 1, 32);
                pointLight->ShadowSamples = std::clamp(pointLight->ShadowSamples, 1, 32);
                TrackInteractiveMemberMutation<PointLight2DComponent>(
                    undoService, "Edit Point Light Shadow Samples", selectedEntity, &PointLight2DComponent::ShadowSamples, pointLight->ShadowSamples);
                ImGui::TextUnformatted("Shadow Bias");
                ImGui::DragFloat("##PointLightShadowBias", &pointLight->ShadowBias, 0.0001f, 0.0f, 10.0f, "%.4f");
                pointLight->ShadowBias = std::max(0.0f, pointLight->ShadowBias);
                TrackInteractiveMemberMutation<PointLight2DComponent>(
                    undoService, "Edit Point Light Shadow Bias", selectedEntity, &PointLight2DComponent::ShadowBias, pointLight->ShadowBias);

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
                ImGui::TextUnformatted("Enabled");
                ImGui::Checkbox("##ShadowOccluderEnabled", &shadowOccluder->Enabled);
                TrackInteractiveMemberMutation<ShadowOccluder2DComponent>(
                    undoService, "Edit Shadow Occluder Enabled", selectedEntity, &ShadowOccluder2DComponent::Enabled, shadowOccluder->Enabled);

                int sourceMode = static_cast<int>(shadowOccluder->Source);
                const char* sourceModeNames[] = { "Manual Polygon", "Physics Collider" };
                ImGui::TextUnformatted("Source");
                if (ImGui::Combo("##ShadowOccluderSource", &sourceMode, sourceModeNames, 2))
                    shadowOccluder->Source = static_cast<ShadowOccluder2DComponent::SourceMode>(sourceMode);
                TrackInteractiveMemberMutation<ShadowOccluder2DComponent>(
                    undoService, "Edit Shadow Occluder Source", selectedEntity, &ShadowOccluder2DComponent::Source, shadowOccluder->Source);

                ImGui::TextUnformatted("Closed Polygon");
                ImGui::Checkbox("##ShadowOccluderClosedPolygon", &shadowOccluder->Closed);
                TrackInteractiveMemberMutation<ShadowOccluder2DComponent>(
                    undoService, "Edit Shadow Occluder Closed", selectedEntity, &ShadowOccluder2DComponent::Closed, shadowOccluder->Closed);
                ImGui::TextUnformatted("Extrusion");
                ImGui::DragFloat("##ShadowOccluderExtrusion", &shadowOccluder->Extrusion, 0.01f, 0.0f, 1000.0f, "%.2f");
                shadowOccluder->Extrusion = std::max(0.0f, shadowOccluder->Extrusion);
                TrackInteractiveMemberMutation<ShadowOccluder2DComponent>(
                    undoService, "Edit Shadow Occluder Extrusion", selectedEntity, &ShadowOccluder2DComponent::Extrusion, shadowOccluder->Extrusion);

                if (shadowOccluder->Source == ShadowOccluder2DComponent::SourceMode::ManualPolygon)
                {
                    if (ImGui::Button("Add Point"))
                    {
                        const glm::vec2 newPoint = shadowOccluder->PolygonPoints.empty()
                            ? glm::vec2(0.0f)
                            : (shadowOccluder->PolygonPoints.back() + glm::vec2(0.5f, 0.0f));

                        const std::vector<glm::vec2> beforePoints = shadowOccluder->PolygonPoints;
                        shadowOccluder->PolygonPoints.push_back(newPoint);
                        const std::vector<glm::vec2> afterPoints = shadowOccluder->PolygonPoints;
                        if (undoService)
                        {
                            (void)undoService->ExecuteLambdaCommand(
                                "Add Shadow Occluder Point",
                                [undoService, selectedEntity, beforePoints]() {
                                    Scene* activeScene = undoService ? undoService->GetActiveScene() : nullptr;
                                    if (!activeScene || !activeScene->IsValid(selectedEntity))
                                        return false;
                                    auto* activeOccluder = activeScene->GetRegistry().try_get<ShadowOccluder2DComponent>(selectedEntity);
                                    if (!activeOccluder)
                                        return false;
                                    activeOccluder->PolygonPoints = beforePoints;
                                    return true;
                                },
                                [undoService, selectedEntity, afterPoints]() {
                                    Scene* activeScene = undoService ? undoService->GetActiveScene() : nullptr;
                                    if (!activeScene || !activeScene->IsValid(selectedEntity))
                                        return false;
                                    auto* activeOccluder = activeScene->GetRegistry().try_get<ShadowOccluder2DComponent>(selectedEntity);
                                    if (!activeOccluder)
                                        return false;
                                    activeOccluder->PolygonPoints = afterPoints;
                                    return true;
                                });
                        }
                    }

                    int removePointIndex = -1;
                    for (size_t pointIndex = 0; pointIndex < shadowOccluder->PolygonPoints.size(); ++pointIndex)
                    {
                        ImGui::PushID(static_cast<int>(pointIndex));
                        ImGui::TextUnformatted("Point");
                        ImGui::DragFloat2("##Point", &shadowOccluder->PolygonPoints[pointIndex].x, 0.01f, -10000.0f, 10000.0f, "%.3f");
                        TrackInteractiveValueMutation(
                            undoService,
                            "Edit Shadow Occluder Point",
                            shadowOccluder->PolygonPoints[pointIndex],
                            [undoService, selectedEntity, pointIndex](const glm::vec2& value) {
                                if (!undoService)
                                    return false;
                                Scene* activeScene = undoService->GetActiveScene();
                                if (!activeScene || !activeScene->IsValid(selectedEntity))
                                    return false;
                                auto* activeOccluder = activeScene->GetRegistry().try_get<ShadowOccluder2DComponent>(selectedEntity);
                                if (!activeOccluder || pointIndex >= activeOccluder->PolygonPoints.size())
                                    return false;
                                activeOccluder->PolygonPoints[pointIndex] = value;
                                return true;
                            });
                        ImGui::SameLine();
                        if (ImGui::Button("X"))
                            removePointIndex = static_cast<int>(pointIndex);
                        ImGui::PopID();
                    }

                    if (removePointIndex >= 0)
                    {
                        const std::vector<glm::vec2> beforePoints = shadowOccluder->PolygonPoints;
                        if (removePointIndex >= 0 && removePointIndex < static_cast<int>(shadowOccluder->PolygonPoints.size()))
                            shadowOccluder->PolygonPoints.erase(shadowOccluder->PolygonPoints.begin() + removePointIndex);
                        const std::vector<glm::vec2> afterPoints = shadowOccluder->PolygonPoints;
                        if (undoService)
                        {
                            (void)undoService->ExecuteLambdaCommand(
                                "Remove Shadow Occluder Point",
                                [undoService, selectedEntity, beforePoints]() {
                                    Scene* activeScene = undoService ? undoService->GetActiveScene() : nullptr;
                                    if (!activeScene || !activeScene->IsValid(selectedEntity))
                                        return false;
                                    auto* activeOccluder = activeScene->GetRegistry().try_get<ShadowOccluder2DComponent>(selectedEntity);
                                    if (!activeOccluder)
                                        return false;
                                    activeOccluder->PolygonPoints = beforePoints;
                                    return true;
                                },
                                [undoService, selectedEntity, afterPoints]() {
                                    Scene* activeScene = undoService ? undoService->GetActiveScene() : nullptr;
                                    if (!activeScene || !activeScene->IsValid(selectedEntity))
                                        return false;
                                    auto* activeOccluder = activeScene->GetRegistry().try_get<ShadowOccluder2DComponent>(selectedEntity);
                                    if (!activeOccluder)
                                        return false;
                                    activeOccluder->PolygonPoints = afterPoints;
                                    return true;
                                });
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
                ImGui::TextUnformatted("Type");
                if (ImGui::Combo("##Joint2DType", &jointTypeIndex, jointTypeNames, 3))
                    joint2D->Type = static_cast<Joint2DComponent::JointType>(jointTypeIndex);
                TrackInteractiveMemberMutation<Joint2DComponent>(
                    undoService, "Edit Joint2D Type", selectedEntity, &Joint2DComponent::Type, joint2D->Type);

                int connectedEntityId = (joint2D->ConnectedEntity == entt::null) ? -1 : static_cast<int>(joint2D->ConnectedEntity);
                ImGui::TextUnformatted("Connected Entity ID");
                if (ImGui::InputInt("##Joint2DConnectedEntityId", &connectedEntityId))
                    joint2D->ConnectedEntity = connectedEntityId >= 0 ? static_cast<entt::entity>(connectedEntityId) : entt::null;
                TrackInteractiveMemberMutation<Joint2DComponent>(
                    undoService, "Edit Joint2D Connected Entity", selectedEntity, &Joint2DComponent::ConnectedEntity, joint2D->ConnectedEntity);

                ImGui::TextUnformatted("Collide Connected");
                ImGui::Checkbox("##Joint2DCollideConnected", &joint2D->CollideConnected);
                TrackInteractiveMemberMutation<Joint2DComponent>(
                    undoService, "Edit Joint2D Collide Connected", selectedEntity, &Joint2DComponent::CollideConnected, joint2D->CollideConnected);
                ImGui::TextUnformatted("Anchor A");
                ImGui::DragFloat2("##Joint2DAnchorA", &joint2D->AnchorA.x, 0.01f);
                TrackInteractiveMemberMutation<Joint2DComponent>(
                    undoService, "Edit Joint2D Anchor A", selectedEntity, &Joint2DComponent::AnchorA, joint2D->AnchorA);
                ImGui::TextUnformatted("Anchor B");
                ImGui::DragFloat2("##Joint2DAnchorB", &joint2D->AnchorB.x, 0.01f);
                TrackInteractiveMemberMutation<Joint2DComponent>(
                    undoService, "Edit Joint2D Anchor B", selectedEntity, &Joint2DComponent::AnchorB, joint2D->AnchorB);
                ImGui::TextUnformatted("Axis");
                ImGui::DragFloat2("##Joint2DAxis", &joint2D->Axis.x, 0.01f);
                TrackInteractiveMemberMutation<Joint2DComponent>(
                    undoService, "Edit Joint2D Axis", selectedEntity, &Joint2DComponent::Axis, joint2D->Axis);
                ImGui::TextUnformatted("Enable Limit");
                ImGui::Checkbox("##Joint2DEnableLimit", &joint2D->EnableLimit);
                TrackInteractiveMemberMutation<Joint2DComponent>(
                    undoService, "Edit Joint2D Enable Limit", selectedEntity, &Joint2DComponent::EnableLimit, joint2D->EnableLimit);
                ImGui::TextUnformatted("Limits");
                ImGui::DragFloat2("##Joint2DLimits", &joint2D->Limits.x, 0.01f);
                TrackInteractiveMemberMutation<Joint2DComponent>(
                    undoService, "Edit Joint2D Limits", selectedEntity, &Joint2DComponent::Limits, joint2D->Limits);
                ImGui::TextUnformatted("Enable Motor");
                ImGui::Checkbox("##Joint2DEnableMotor", &joint2D->EnableMotor);
                TrackInteractiveMemberMutation<Joint2DComponent>(
                    undoService, "Edit Joint2D Enable Motor", selectedEntity, &Joint2DComponent::EnableMotor, joint2D->EnableMotor);
                ImGui::TextUnformatted("Motor Speed");
                ImGui::DragFloat("##Joint2DMotorSpeed", &joint2D->MotorSpeed, 0.01f);
                TrackInteractiveMemberMutation<Joint2DComponent>(
                    undoService, "Edit Joint2D Motor Speed", selectedEntity, &Joint2DComponent::MotorSpeed, joint2D->MotorSpeed);
                ImGui::TextUnformatted("Max Motor Force/Torque");
                ImGui::DragFloat("##Joint2DMaxMotorForceOrTorque", &joint2D->MaxMotorForceOrTorque, 0.1f, 0.0f, 100000.0f);
                TrackInteractiveMemberMutation<Joint2DComponent>(
                    undoService, "Edit Joint2D Max Motor", selectedEntity, &Joint2DComponent::MaxMotorForceOrTorque, joint2D->MaxMotorForceOrTorque);
                ImGui::TextUnformatted("Enable Spring");
                ImGui::Checkbox("##Joint2DEnableSpring", &joint2D->EnableSpring);
                TrackInteractiveMemberMutation<Joint2DComponent>(
                    undoService, "Edit Joint2D Enable Spring", selectedEntity, &Joint2DComponent::EnableSpring, joint2D->EnableSpring);
                ImGui::TextUnformatted("Hertz");
                ImGui::DragFloat("##Joint2DHertz", &joint2D->Hertz, 0.1f, 0.0f, 1000.0f);
                TrackInteractiveMemberMutation<Joint2DComponent>(
                    undoService, "Edit Joint2D Hertz", selectedEntity, &Joint2DComponent::Hertz, joint2D->Hertz);
                ImGui::TextUnformatted("Damping Ratio");
                ImGui::DragFloat("##Joint2DDampingRatio", &joint2D->DampingRatio, 0.01f, 0.0f, 10.0f);
                TrackInteractiveMemberMutation<Joint2DComponent>(
                    undoService, "Edit Joint2D Damping", selectedEntity, &Joint2DComponent::DampingRatio, joint2D->DampingRatio);

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
                    if (ImGui::TreeNodeEx("Fallback Colors (Used Only Without Visual Children)"))
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
                    if (isPlaying)
                    {
                        if (ImGui::Button("Pause##ParticleEmitter"))
                            ParticleEmitterPause(*particleEmitter);
                    }
                    else if (isPaused)
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
                    ImGui::TextUnformatted("Spawn Rate");
                    ImGui::DragFloat("##ParticleSpawnRate", &particleEmitter->SpawnRate, 0.5f, 0.0f, 10000.0f);
                    particleEmitter->SpawnRate = std::max(0.0f, particleEmitter->SpawnRate);
                    TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                        undoService, "Edit Particle Spawn Rate", selectedEntity, &ParticleEmitterComponent::SpawnRate, particleEmitter->SpawnRate);

                    ImGui::TextUnformatted("Lifetime Min");
                    ImGui::DragFloat("##ParticleLifetimeMin", &particleEmitter->LifetimeMin, 0.05f, 0.01f, 100.0f);
                    particleEmitter->LifetimeMin = std::max(0.01f, particleEmitter->LifetimeMin);
                    TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                        undoService, "Edit Particle Lifetime Min", selectedEntity, &ParticleEmitterComponent::LifetimeMin, particleEmitter->LifetimeMin);

                    ImGui::TextUnformatted("Lifetime Max");
                    ImGui::DragFloat("##ParticleLifetimeMax", &particleEmitter->LifetimeMax, 0.05f, 0.01f, 100.0f);
                    particleEmitter->LifetimeMax = std::max(particleEmitter->LifetimeMin, particleEmitter->LifetimeMax);
                    TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                        undoService, "Edit Particle Lifetime Max", selectedEntity, &ParticleEmitterComponent::LifetimeMax, particleEmitter->LifetimeMax);

                    ImGui::TextUnformatted("Looping");
                    ImGui::Checkbox("##ParticleLooping", &particleEmitter->Looping);
                    TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                        undoService, "Edit Particle Looping", selectedEntity, &ParticleEmitterComponent::Looping, particleEmitter->Looping);

                    if (!particleEmitter->Looping)
                    {
                        ImGui::TextUnformatted("Duration");
                        ImGui::DragFloat("##ParticleDuration", &particleEmitter->Duration, 0.1f, 0.1f, 600.0f);
                        particleEmitter->Duration = std::max(0.1f, particleEmitter->Duration);
                        TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                            undoService, "Edit Particle Duration", selectedEntity, &ParticleEmitterComponent::Duration, particleEmitter->Duration);
                    }

                    ImGui::TextUnformatted("Play On Start");
                    ImGui::Checkbox("##ParticlePlayOnStart", &particleEmitter->PlayOnStart);
                    TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                        undoService, "Edit Particle Play On Start", selectedEntity, &ParticleEmitterComponent::PlayOnStart, particleEmitter->PlayOnStart);

                    ImGui::TextUnformatted("Burst Enabled");
                    ImGui::Checkbox("##ParticleBurstEnabled", &particleEmitter->BurstEnabled);
                    TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                        undoService, "Edit Particle Burst Enabled", selectedEntity, &ParticleEmitterComponent::BurstEnabled, particleEmitter->BurstEnabled);

                    if (particleEmitter->BurstEnabled)
                    {
                        int burstCount = static_cast<int>(particleEmitter->BurstCount);
                        ImGui::TextUnformatted("Burst Count");
                        ImGui::DragInt("##ParticleBurstCount", &burstCount, 1, 1, static_cast<int>(ParticleEmitterComponent::kMaxParticlesCap));
                        particleEmitter->BurstCount = static_cast<uint32_t>(std::max(1, burstCount));
                        TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                            undoService, "Edit Particle Burst Count", selectedEntity, &ParticleEmitterComponent::BurstCount, particleEmitter->BurstCount);
                    }

                    ImGui::TextUnformatted("Radial Spawn Position");
                    ImGui::Checkbox("##ParticleRadialSpawnPosition", &particleEmitter->UseRadialSpawn);
                    TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                        undoService, "Edit Particle Radial Spawn Position", selectedEntity, &ParticleEmitterComponent::UseRadialSpawn, particleEmitter->UseRadialSpawn);

                    if (particleEmitter->UseRadialSpawn)
                    {
                        ImGui::TextUnformatted("Spawn Radius Min");
                        ImGui::DragFloat("##ParticleSpawnRadiusMin", &particleEmitter->SpawnRadiusMin, 0.01f, 0.0f, 1000.0f);
                        particleEmitter->SpawnRadiusMin = std::max(0.0f, particleEmitter->SpawnRadiusMin);
                        TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                            undoService, "Edit Particle Spawn Radius Min", selectedEntity, &ParticleEmitterComponent::SpawnRadiusMin, particleEmitter->SpawnRadiusMin);

                        ImGui::TextUnformatted("Spawn Radius Max");
                        ImGui::DragFloat("##ParticleSpawnRadiusMax", &particleEmitter->SpawnRadiusMax, 0.01f, 0.0f, 1000.0f);
                        particleEmitter->SpawnRadiusMax = std::max(particleEmitter->SpawnRadiusMin, particleEmitter->SpawnRadiusMax);
                        TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                            undoService, "Edit Particle Spawn Radius Max", selectedEntity, &ParticleEmitterComponent::SpawnRadiusMax, particleEmitter->SpawnRadiusMax);
                    }
                    else
                    {
                        ImGui::TextUnformatted("Spawn Offset Min");
                        ImGui::DragFloat2("##ParticleSpawnOffsetMin", &particleEmitter->SpawnOffsetMin.x, 0.01f, -1000.0f, 1000.0f, "%.3f");
                        TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                            undoService, "Edit Particle Spawn Offset Min", selectedEntity, &ParticleEmitterComponent::SpawnOffsetMin, particleEmitter->SpawnOffsetMin);

                        ImGui::TextUnformatted("Spawn Offset Max");
                        ImGui::DragFloat2("##ParticleSpawnOffsetMax", &particleEmitter->SpawnOffsetMax.x, 0.01f, -1000.0f, 1000.0f, "%.3f");
                        TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                            undoService, "Edit Particle Spawn Offset Max", selectedEntity, &ParticleEmitterComponent::SpawnOffsetMax, particleEmitter->SpawnOffsetMax);
                    }

                    ImGui::TreePop();
                }

                // -- Velocity --
                if (ImGui::TreeNodeEx("Velocity##ParticleEmitter", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::TextUnformatted("Speed Min");
                    ImGui::DragFloat("##ParticleSpeedMin", &particleEmitter->SpeedMin, 0.5f, 0.0f, 10000.0f);
                    particleEmitter->SpeedMin = std::max(0.0f, particleEmitter->SpeedMin);
                    TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                        undoService, "Edit Particle Speed Min", selectedEntity, &ParticleEmitterComponent::SpeedMin, particleEmitter->SpeedMin);

                    ImGui::TextUnformatted("Speed Max");
                    ImGui::DragFloat("##ParticleSpeedMax", &particleEmitter->SpeedMax, 0.5f, 0.0f, 10000.0f);
                    particleEmitter->SpeedMax = std::max(particleEmitter->SpeedMin, particleEmitter->SpeedMax);
                    TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                        undoService, "Edit Particle Speed Max", selectedEntity, &ParticleEmitterComponent::SpeedMax, particleEmitter->SpeedMax);

                    ImGui::TextUnformatted("Angle Min");
                    ImGui::DragFloat("##ParticleAngleMin", &particleEmitter->AngleMin, 1.0f, 0.0f, 360.0f);
                    TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                        undoService, "Edit Particle Angle Min", selectedEntity, &ParticleEmitterComponent::AngleMin, particleEmitter->AngleMin);

                    ImGui::TextUnformatted("Angle Max");
                    ImGui::DragFloat("##ParticleAngleMax", &particleEmitter->AngleMax, 1.0f, 0.0f, 360.0f);
                    TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                        undoService, "Edit Particle Angle Max", selectedEntity, &ParticleEmitterComponent::AngleMax, particleEmitter->AngleMax);

                    ImGui::TextUnformatted("Radial Velocity");
                    ImGui::Checkbox("##ParticleRadialVelocity", &particleEmitter->RadialVelocity);
                    TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                        undoService, "Edit Particle Radial Velocity", selectedEntity, &ParticleEmitterComponent::RadialVelocity, particleEmitter->RadialVelocity);

                    ImGui::TextUnformatted("Gravity Modifier");
                    ImGui::DragFloat("##ParticleGravityModifier", &particleEmitter->GravityModifier, 0.05f, -100.0f, 100.0f);
                    TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                        undoService, "Edit Particle Gravity Modifier", selectedEntity, &ParticleEmitterComponent::GravityModifier, particleEmitter->GravityModifier);

                    ImGui::TreePop();
                }

                // -- Appearance --
                if (ImGui::TreeNodeEx("Appearance##ParticleEmitter", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::TextUnformatted("Start Size Min");
                    ImGui::DragFloat("##ParticleStartSizeMin", &particleEmitter->StartSizeMin, 0.01f, 0.001f, 100.0f);
                    particleEmitter->StartSizeMin = std::max(0.001f, particleEmitter->StartSizeMin);
                    TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                        undoService, "Edit Particle Start Size Min", selectedEntity, &ParticleEmitterComponent::StartSizeMin, particleEmitter->StartSizeMin);

                    ImGui::TextUnformatted("Start Size Max");
                    ImGui::DragFloat("##ParticleStartSizeMax", &particleEmitter->StartSizeMax, 0.01f, 0.001f, 100.0f);
                    particleEmitter->StartSizeMax = std::max(particleEmitter->StartSizeMin, particleEmitter->StartSizeMax);
                    TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                        undoService, "Edit Particle Start Size Max", selectedEntity, &ParticleEmitterComponent::StartSizeMax, particleEmitter->StartSizeMax);

                    ImGui::TextUnformatted("End Size");
                    ImGui::DragFloat("##ParticleEndSize", &particleEmitter->EndSize, 0.01f, 0.0f, 100.0f);
                    particleEmitter->EndSize = std::max(0.0f, particleEmitter->EndSize);
                    TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                        undoService, "Edit Particle End Size", selectedEntity, &ParticleEmitterComponent::EndSize, particleEmitter->EndSize);

                    ImGui::TextUnformatted("Start Color");
                    ImGui::ColorEdit4("##ParticleStartColor", &particleEmitter->StartColor.r);
                    TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                        undoService, "Edit Particle Start Color", selectedEntity, &ParticleEmitterComponent::StartColor, particleEmitter->StartColor);

                    ImGui::TextUnformatted("End Color");
                    ImGui::ColorEdit4("##ParticleEndColor", &particleEmitter->EndColor.r);
                    TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                        undoService, "Edit Particle End Color", selectedEntity, &ParticleEmitterComponent::EndColor, particleEmitter->EndColor);

                    ImGui::TreePop();
                }

                // -- Rotation --
                if (ImGui::TreeNodeEx("Rotation##ParticleEmitter"))
                {
                    ImGui::TextUnformatted("Start Rotation Min");
                    ImGui::DragFloat("##ParticleStartRotationMin", &particleEmitter->StartRotationMin, 1.0f, -360.0f, 360.0f);
                    TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                        undoService, "Edit Particle Start Rotation Min", selectedEntity, &ParticleEmitterComponent::StartRotationMin, particleEmitter->StartRotationMin);

                    ImGui::TextUnformatted("Start Rotation Max");
                    ImGui::DragFloat("##ParticleStartRotationMax", &particleEmitter->StartRotationMax, 1.0f, -360.0f, 360.0f);
                    TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                        undoService, "Edit Particle Start Rotation Max", selectedEntity, &ParticleEmitterComponent::StartRotationMax, particleEmitter->StartRotationMax);

                    ImGui::TextUnformatted("Rotation Speed Min");
                    ImGui::DragFloat("##ParticleRotationSpeedMin", &particleEmitter->RotationSpeedMin, 1.0f, -1000.0f, 1000.0f);
                    TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                        undoService, "Edit Particle Rotation Speed Min", selectedEntity, &ParticleEmitterComponent::RotationSpeedMin, particleEmitter->RotationSpeedMin);

                    ImGui::TextUnformatted("Rotation Speed Max");
                    ImGui::DragFloat("##ParticleRotationSpeedMax", &particleEmitter->RotationSpeedMax, 1.0f, -1000.0f, 1000.0f);
                    TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                        undoService, "Edit Particle Rotation Speed Max", selectedEntity, &ParticleEmitterComponent::RotationSpeedMax, particleEmitter->RotationSpeedMax);

                    ImGui::TreePop();
                }

                // -- Texture --
                if (ImGui::TreeNodeEx("Texture##ParticleEmitter"))
                {
                    const auto assignParticleTextureKey = [&](const std::string& textureKey) {
                        const std::string beforeTextureKey = particleEmitter->TextureKey;
                        if (beforeTextureKey == textureKey)
                            return;

                        particleEmitter->TextureKey = textureKey;
                        particleEmitter->CachedTexture.reset();
                        particleEmitter->TextureLoadAttempted = false;

                        if (!undoService)
                            return;
                        (void)undoService->ExecuteValueMutation<std::string>(
                            "Edit Particle Emitter Texture",
                            beforeTextureKey,
                            textureKey,
                            [undoService, selectedEntity](const std::string& value) {
                                if (!undoService)
                                    return false;
                                Scene* activeScene = undoService->GetActiveScene();
                                if (!activeScene || !activeScene->IsValid(selectedEntity))
                                    return false;
                                auto* activeEmitter = activeScene->GetRegistry().try_get<ParticleEmitterComponent>(selectedEntity);
                                if (!activeEmitter)
                                    return false;
                                activeEmitter->TextureKey = value;
                                activeEmitter->CachedTexture.reset();
                                activeEmitter->TextureLoadAttempted = false;
                                return true;
                            });
                    };

                    const std::string textureLabel = !particleEmitter->TextureKey.empty()
                        ? EditorAssetNaming::GetAssetDisplayNameFromAssetKey(particleEmitter->TextureKey)
                        : std::string("None (White Quad)");
                    ImGui::Text("Texture");
                    ImGui::Button((textureLabel + "##ParticleEmitterTexture").c_str(),
                                  ImVec2(std::max(60.0f, ImGui::GetContentRegionAvail().x - 90.0f), 0));

                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kSubSpritePayloadId))
                        {
                            const char* key = static_cast<const char*>(payload->Data);
                            if (key && key[0])
                            {
                                assignParticleTextureKey(ResolveTextureKeyFromDroppedKey(key));
                            }
                        }
                        else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(texturePayloadId))
                        {
                            const char* key = static_cast<const char*>(payload->Data);
                            if (key && key[0])
                            {
                                assignParticleTextureKey(ResolveTextureKeyFromDroppedKey(key));
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("Clear##ParticleEmitterTexture"))
                    {
                        assignParticleTextureKey({});
                    }

                    ImGui::TreePop();
                }

                // -- Limits --
                {
                    int maxParticles = static_cast<int>(particleEmitter->MaxParticles);
                    ImGui::TextUnformatted("Max Particles");
                    ImGui::DragInt("##ParticleMaxParticles", &maxParticles, 16, 1, static_cast<int>(ParticleEmitterComponent::kMaxParticlesCap));
                    particleEmitter->MaxParticles = static_cast<uint32_t>(std::clamp(maxParticles, 1, static_cast<int>(ParticleEmitterComponent::kMaxParticlesCap)));
                    TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                        undoService, "Edit Particle Max Particles", selectedEntity, &ParticleEmitterComponent::MaxParticles, particleEmitter->MaxParticles);
                }

                ImGui::TreePop();
            }
        }
    }
}
