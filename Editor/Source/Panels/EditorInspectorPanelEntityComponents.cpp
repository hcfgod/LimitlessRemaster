#include "EditorInspectorPanelEntityComponentsShared.h"

#include "Assets/AssetDatabase.h"
#include "Assets/AssetPaths.h"
#include "Assets/AssetTypes.h"
#include "Assets/SpriteImportSettings.h"
#include "Assets/TextureAssetImporter.h"
#include "Project/ProjectManager.h"
#include "Project/ProjectSettings.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <unordered_set>

namespace Limitless::EditorInspectorPanel::Internal
{
    bool ShouldDrawInspectorSection(std::string_view onlySectionKey, std::string_view sectionKey)
    {
        return onlySectionKey.empty() || onlySectionKey == sectionKey;
    }

    bool BeginInspectorSectionHeader(const char* label,
                                     const char* popupId,
                                     const char* optionsButtonId)
    {
        (void)optionsButtonId;
        ImGui::Spacing();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 10.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.11f, 0.17f, 0.27f, 0.96f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.16f, 0.24f, 0.38f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.20f, 0.29f, 0.44f, 1.0f));
        const bool isOpen = BeginPersistentTreeNode(
            popupId ? popupId : label,
            label,
            ImGuiTreeNodeFlags_DefaultOpen |
                ImGuiTreeNodeFlags_Framed |
                ImGuiTreeNodeFlags_AllowItemOverlap);
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(2);

        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            ImGui::OpenPopup(popupId);

        return isOpen;
    }

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

        auto tryAddKnownDefault = [&](std::string_view key) {
            if (key.empty())
                return;
            const std::string keyText(key);
            if (seen.contains(keyText))
                return;
            const auto resolved = Assets::ResolveAssetKeyToPath(keyText);
            if (resolved.IsFailure())
                return;
            std::error_code ec;
            if (std::filesystem::exists(resolved.GetValue(), ec))
            {
                seen.insert(keyText);
                keys.emplace_back(keyText);
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

    glm::vec2 NormalizeDirectionOrFallback(const glm::vec2& direction, const glm::vec2& fallback)
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

    void SyncSliderVisualChildrenInEditor(entt::registry& registry, entt::entity sliderEntity, const UISliderComponent& slider)
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
}

namespace Limitless::EditorInspectorPanel
{
    void DrawEntityHeaderSection(Scene* scene,
                                 entt::registry& registry,
                                 entt::entity selectedEntity,
                                 EditorUndoService* undoService)
    {
        (void)scene;

        if (auto* tag = registry.try_get<TagComponent>(selectedEntity))
        {
            static entt::entity renameEntity = entt::null;
            static std::array<char, 256> renameBuffer{};
            if (renameEntity != selectedEntity)
            {
                renameEntity = selectedEntity;
                std::snprintf(renameBuffer.data(), renameBuffer.size(), "%s", tag->Tag.c_str());
            }

            Project::LayersSettings layersSettings;
            const auto& projectManager = Project::ProjectManager::GetInstance();
            if (projectManager.HasOpenProject())
            {
                const auto loadResult = Project::LoadLayersSettings(projectManager.GetProjectRoot());
                if (loadResult.IsSuccess())
                    layersSettings = loadResult.GetValue();
            }

            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06f, 0.09f, 0.15f, 0.92f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.20f, 0.32f, 0.48f, 0.45f));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 10.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 6.0f));
            const float entityCardHeight = ImGui::GetFrameHeight() * 3.0f + 20.0f + ImGui::GetStyle().WindowPadding.y * 2.0f;
            ImGui::BeginChild("InspectorEntityHeader", ImVec2(0.0f, entityCardHeight), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

            ImGui::AlignTextToFramePadding();
            ImGui::Checkbox("##EntityEnabled", &tag->Enabled);
            Internal::TrackInteractiveMemberMutation<TagComponent>(
                undoService, "Edit Entity Enabled", selectedEntity, &TagComponent::Enabled, tag->Enabled);
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("Enabled");
            ImGui::SameLine();
            ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize("ENTITY").x));
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("ENTITY");

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

            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("Layer");
            ImGui::SameLine();
            {
                const uint8_t currentLayer = tag->Layer < Project::LayersSettings::kMaxLayers ? tag->Layer : 0;
                std::string layerPreview = layersSettings.LayerNames[currentLayer];
                if (layerPreview.empty())
                    layerPreview = "Layer " + std::to_string(currentLayer);
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::BeginCombo("##EntityLayer", layerPreview.c_str()))
                {
                    for (uint32_t i = 0; i < Project::LayersSettings::kMaxLayers; ++i)
                    {
                        if (layersSettings.LayerNames[i].empty())
                            continue;
                        const bool selected = (currentLayer == static_cast<uint8_t>(i));
                        const std::string label = layersSettings.LayerNames[i] + "##Layer" + std::to_string(i);
                        if (ImGui::Selectable(label.c_str(), selected))
                        {
                            const uint8_t newLayer = static_cast<uint8_t>(i);
                            if (undoService)
                            {
                                (void)undoService->ExecuteSceneMutation("Change Entity Layer", [&](Scene& mutableScene) {
                                    auto* mutableTag = mutableScene.GetRegistry().try_get<TagComponent>(selectedEntity);
                                    if (!mutableTag)
                                        return false;
                                    mutableTag->Layer = newLayer;
                                    return true;
                                });
                            }
                            else
                            {
                                tag->Layer = newLayer;
                            }
                        }
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            }

            ImGui::EndChild();
            ImGui::PopStyleVar(4);
            ImGui::PopStyleColor(2);
        }

        ImGui::Spacing();
        ImGui::Separator();
    }

    std::vector<std::string> CollectStandardEntityComponentSectionKeys(entt::registry& registry, entt::entity selectedEntity)
    {
        std::vector<std::string> sectionKeys;
        if (registry.try_get<TransformComponent>(selectedEntity))
            sectionKeys.emplace_back("Transform");
        if (registry.try_get<CanvasComponent>(selectedEntity))
            sectionKeys.emplace_back("Canvas");
        if (registry.try_get<RectTransformComponent>(selectedEntity))
            sectionKeys.emplace_back("RectTransform");
        if (registry.try_get<SpriteComponent>(selectedEntity))
            sectionKeys.emplace_back("Sprite");
        if (registry.try_get<AnimatorComponent>(selectedEntity))
            sectionKeys.emplace_back("Animator");
        if (registry.try_get<AnimationEventReceiverComponent>(selectedEntity))
            sectionKeys.emplace_back("AnimationEventReceiver");
        if (registry.try_get<CameraComponent>(selectedEntity))
            sectionKeys.emplace_back("Camera");
        if (registry.try_get<AudioListener2DComponent>(selectedEntity))
            sectionKeys.emplace_back("AudioListener2D");
        if (registry.try_get<AudioListener3DComponent>(selectedEntity))
            sectionKeys.emplace_back("AudioListener3D");
        if (registry.try_get<Grid2DComponent>(selectedEntity))
            sectionKeys.emplace_back("Grid2D");
        if (registry.try_get<TilemapLayerComponent>(selectedEntity))
            sectionKeys.emplace_back("TilemapLayer");
        if (registry.try_get<AudioSourceComponent>(selectedEntity))
            sectionKeys.emplace_back("AudioSource");
        if (registry.try_get<Rigidbody2DComponent>(selectedEntity))
            sectionKeys.emplace_back("Rigidbody2D");
        if (registry.try_get<BoxCollider2DComponent>(selectedEntity))
            sectionKeys.emplace_back("BoxCollider2D");
        if (registry.try_get<CircleCollider2DComponent>(selectedEntity))
            sectionKeys.emplace_back("CircleCollider2D");
        if (registry.try_get<PolygonCollider2DComponent>(selectedEntity))
            sectionKeys.emplace_back("PolygonCollider2D");
        if (registry.try_get<EdgeCollider2DComponent>(selectedEntity))
            sectionKeys.emplace_back("EdgeCollider2D");
        if (registry.try_get<CapsuleCollider2DComponent>(selectedEntity))
            sectionKeys.emplace_back("CapsuleCollider2D");
        if (registry.try_get<DirectionalLight2DComponent>(selectedEntity))
            sectionKeys.emplace_back("DirectionalLight2D");
        if (registry.try_get<PointLight2DComponent>(selectedEntity))
            sectionKeys.emplace_back("PointLight2D");
        if (registry.try_get<ShadowOccluder2DComponent>(selectedEntity))
            sectionKeys.emplace_back("ShadowOccluder2D");
        if (registry.try_get<Joint2DComponent>(selectedEntity))
            sectionKeys.emplace_back("Joint2D");
        if (registry.try_get<UIImageComponent>(selectedEntity))
            sectionKeys.emplace_back("UIImage");
        if (registry.try_get<UIPanelComponent>(selectedEntity))
            sectionKeys.emplace_back("UIPanel");
        if (registry.try_get<UITextComponent>(selectedEntity))
            sectionKeys.emplace_back("UIText");
        if (registry.try_get<UIButtonComponent>(selectedEntity))
            sectionKeys.emplace_back("UIButton");
        if (registry.try_get<UISliderComponent>(selectedEntity))
            sectionKeys.emplace_back("UISlider");
        if (registry.try_get<ParticleEmitterComponent>(selectedEntity))
            sectionKeys.emplace_back("ParticleEmitter");
        return sectionKeys;
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
                                             Limitless::EditorUndoService* undoService,
                                             std::string_view onlySectionKey,
                                             const std::vector<std::string>* orderedSectionKeys)
    {
        Internal::StandardEntityInspectorContext context{
            scene,
            registry,
            selectedEntity,
            texturePayloadId,
            audioPayloadId,
            materialPayloadId,
            fontPayloadId,
            selectedAnimationClipAssetKey,
            selectedAnimatorControllerAssetKey,
            pendingRemovals,
            undoService,
            onlySectionKey,
            orderedSectionKeys
        };

        const auto drawSection = [&](std::string_view sectionKey) {
            context.OnlySectionKey = sectionKey;
            Internal::DrawSceneComponentSections(context);
            Internal::DrawAudioComponentSections(context);
            Internal::DrawGridComponentSections(context);
            Internal::DrawPhysics2DComponentSections(context);
            Internal::DrawLighting2DComponentSections(context);
            Internal::DrawUiComponentSections(context);
            Internal::DrawParticleComponentSections(context);
        };

        if (!onlySectionKey.empty())
        {
            drawSection(onlySectionKey);
            return;
        }

        if (orderedSectionKeys)
        {
            for (const std::string& sectionKey : *orderedSectionKeys)
                drawSection(sectionKey);
            return;
        }

        static constexpr std::array<std::string_view, 28> kDefaultSectionOrder = {
            "Transform",
            "Canvas",
            "RectTransform",
            "Sprite",
            "Animator",
            "AnimationEventReceiver",
            "Camera",
            "AudioListener2D",
            "AudioListener3D",
            "Grid2D",
            "TilemapLayer",
            "AudioSource",
            "Rigidbody2D",
            "BoxCollider2D",
            "CircleCollider2D",
            "PolygonCollider2D",
            "EdgeCollider2D",
            "CapsuleCollider2D",
            "DirectionalLight2D",
            "PointLight2D",
            "ShadowOccluder2D",
            "Joint2D",
            "UIImage",
            "UIPanel",
            "UIText",
            "UIButton",
            "UISlider",
            "ParticleEmitter"
        };

        for (std::string_view sectionKey : kDefaultSectionOrder)
            drawSection(sectionKey);
    }
}
