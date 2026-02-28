#include "EditorScenePanel.h"

#include "EditorPrefabSystem.h"
#include "Scene/Scene.h"
#include "Undo/EditorUndoService.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>

namespace Limitless::EditorScenePanel
{
    namespace
    {
        constexpr const char* kSceneEntityPayload = "SCENE_ENTITY";
        constexpr ImU32 kDropIndicatorColor = IM_COL32(80, 160, 255, 255);
        constexpr ImU32 kDropIndicatorTintColor = IM_COL32(80, 160, 255, 48);
        constexpr float kDropIndicatorThickness = 3.0f;
        constexpr float kDropIndicatorCapHeight = 8.0f;
        constexpr ImU32 kPrefabHighlightTextColor = IM_COL32(80, 170, 255, 255);
        constexpr ImU32 kPrefabBadgeFillColor = IM_COL32(58, 125, 198, 255);
        constexpr ImU32 kPrefabBadgeBorderColor = IM_COL32(120, 190, 255, 255);
        constexpr ImU32 kPrefabBadgeTextColor = IM_COL32(235, 245, 255, 255);
        constexpr float kPrefabBadgeSize = 12.0f;
        constexpr ImVec4 kDisabledEntityTextColor = ImVec4(0.58f, 0.58f, 0.58f, 1.0f);

        void CopyTextToBuffer(std::array<char, 256>& destination, const char* source)
        {
            if (!source)
            {
                destination[0] = '\0';
                return;
            }

            std::snprintf(destination.data(), destination.size(), "%s", source);
        }

        bool IsSceneEntityDragActive()
        {
            const ImGuiPayload* payload = ImGui::GetDragDropPayload();
            if (!payload)
                return false;
            return std::strcmp(payload->DataType, kSceneEntityPayload) == 0;
        }

        std::string ReadStringPayload(const ImGuiPayload* payload)
        {
            if (!payload || !payload->Data || payload->DataSize <= 0)
                return {};

            const char* bytes = static_cast<const char*>(payload->Data);
            const int payloadLength = std::max(0, payload->DataSize - 1);
            return std::string(bytes, bytes + payloadLength);
        }

        void ClearAssetSelectionState(std::string& selectedTextureAssetKey,
                                      Assets::TextureAsset::Ptr& cachedTextureAsset,
                                      std::string& selectedMaterialAssetKey,
                                      Assets::MaterialAsset::Ptr& cachedMaterialAsset,
                                      std::string& selectedNativeScriptAssetKey,
                                      std::string& selectedPrefabAssetKey,
                                      std::string& selectedTilesetAssetKey,
                                      std::string& selectedAudioMixerAssetKey,
                                      std::string& selectedInputActionsAssetKey)
        {
            selectedTextureAssetKey.clear();
            cachedTextureAsset.reset();
            selectedMaterialAssetKey.clear();
            cachedMaterialAsset.reset();
            selectedNativeScriptAssetKey.clear();
            selectedPrefabAssetKey.clear();
            selectedTilesetAssetKey.clear();
            selectedAudioMixerAssetKey.clear();
            selectedInputActionsAssetKey.clear();
        }

        bool ContainsEntity(const std::vector<entt::entity>& entities, entt::entity entity)
        {
            return std::find(entities.begin(), entities.end(), entity) != entities.end();
        }

        void AddEntityIfMissing(std::vector<entt::entity>& entities, entt::entity entity)
        {
            if (entity == entt::null)
                return;
            if (!ContainsEntity(entities, entity))
                entities.push_back(entity);
        }

        void SelectSingleEntity(EditorScenePanelState& state, entt::entity& selectedEntity, entt::entity entity)
        {
            selectedEntity = entity;
            state.MultiSelectedEntities.clear();
            if (entity != entt::null)
                state.MultiSelectedEntities.push_back(entity);
            state.SelectionAnchorEntity = entity;
        }

        void ClearEntitySelectionState(EditorScenePanelState& state, entt::entity& selectedEntity)
        {
            selectedEntity = entt::null;
            state.MultiSelectedEntities.clear();
            state.SelectionAnchorEntity = entt::null;
        }

        void PruneInvalidEntitySelection(const Scene* scene, std::vector<entt::entity>& entities)
        {
            if (!scene)
            {
                entities.clear();
                return;
            }

            entities.erase(
                std::remove_if(
                    entities.begin(),
                    entities.end(),
                    [&](entt::entity entity) { return !scene->IsValid(entity); }),
                entities.end());
        }

        bool HasSelectedAncestor(const Scene& scene, entt::entity entity, const std::unordered_set<uint32_t>& selectedIds)
        {
            entt::entity parent = scene.GetParent(entity);
            while (parent != entt::null && scene.IsValid(parent))
            {
                if (selectedIds.contains(static_cast<uint32_t>(parent)))
                    return true;
                parent = scene.GetParent(parent);
            }
            return false;
        }

        std::vector<entt::entity> BuildSelectionRoots(const Scene& scene,
                                                      const std::vector<entt::entity>& selectedEntities,
                                                      const std::vector<entt::entity>& drawOrderEntities)
        {
            std::unordered_set<uint32_t> selectedIds;
            selectedIds.reserve(selectedEntities.size());
            for (entt::entity entity : selectedEntities)
            {
                if (scene.IsValid(entity))
                    selectedIds.insert(static_cast<uint32_t>(entity));
            }

            if (selectedIds.empty())
                return {};

            std::vector<entt::entity> orderedSelection;
            orderedSelection.reserve(selectedIds.size());
            for (entt::entity entity : drawOrderEntities)
            {
                if (selectedIds.contains(static_cast<uint32_t>(entity)))
                    orderedSelection.push_back(entity);
            }

            for (entt::entity entity : selectedEntities)
            {
                if (!scene.IsValid(entity))
                    continue;
                if (!ContainsEntity(orderedSelection, entity))
                    orderedSelection.push_back(entity);
            }

            std::vector<entt::entity> roots;
            roots.reserve(orderedSelection.size());
            for (entt::entity entity : orderedSelection)
            {
                if (!HasSelectedAncestor(scene, entity, selectedIds))
                    roots.push_back(entity);
            }
            return roots;
        }

        std::vector<entt::entity> ResolveSelectionForSceneOperations(const Scene& scene,
                                                                      const EditorScenePanelState& state,
                                                                      entt::entity selectedEntity)
        {
            std::vector<entt::entity> selectedEntities = state.MultiSelectedEntities;
            if (selectedEntities.empty() && scene.IsValid(selectedEntity))
                selectedEntities.push_back(selectedEntity);

            return BuildSelectionRoots(scene, selectedEntities, state.DrawOrderEntities);
        }

        void DrawHorizontalDropIndicator(float leftX, float rightX, float y)
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddLine(ImVec2(leftX, y), ImVec2(rightX, y), kDropIndicatorColor, kDropIndicatorThickness);
            drawList->AddLine(ImVec2(leftX, y - kDropIndicatorCapHeight * 0.5f),
                              ImVec2(leftX, y + kDropIndicatorCapHeight * 0.5f),
                              kDropIndicatorColor,
                              kDropIndicatorThickness);
        }

        void DrawDropTint(const ImVec2& min, const ImVec2& max)
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(min, max, kDropIndicatorTintColor);
        }

        bool IsEntityInPrefabInstanceSubtree(const Scene& scene, entt::entity entity)
        {
            if (!scene.IsValid(entity))
                return false;

            const auto& registry = scene.GetRegistry();
            entt::entity cursor = entity;
            while (cursor != entt::null && scene.IsValid(cursor))
            {
                const auto* prefabInstance = registry.try_get<PrefabInstanceComponent>(cursor);
                if (prefabInstance && !prefabInstance->PrefabAssetKey.empty())
                    return true;
                cursor = scene.GetParent(cursor);
            }
            return false;
        }

        void DrawPrefabBadgeForLastItem()
        {
            const ImVec2 itemMax = ImGui::GetItemRectMax();
            const ImVec2 itemMin = ImGui::GetItemRectMin();
            const float badgeHalf = kPrefabBadgeSize * 0.5f;
            const ImVec2 badgeCenter(itemMax.x - (kPrefabBadgeSize + 6.0f), itemMin.y + (itemMax.y - itemMin.y) * 0.5f);
            const ImVec2 badgeMin(badgeCenter.x - badgeHalf, badgeCenter.y - badgeHalf);
            const ImVec2 badgeMax(badgeCenter.x + badgeHalf, badgeCenter.y + badgeHalf);

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(badgeMin, badgeMax, kPrefabBadgeFillColor, 2.0f);
            drawList->AddRect(badgeMin, badgeMax, kPrefabBadgeBorderColor, 2.0f, 0, 1.0f);

            const char* glyph = "P";
            const ImVec2 textSize = ImGui::CalcTextSize(glyph);
            const ImVec2 textPos(badgeCenter.x - textSize.x * 0.5f, badgeCenter.y - textSize.y * 0.5f - 1.0f);
            drawList->AddText(textPos, kPrefabBadgeTextColor, glyph);
        }

        bool DrawEntityNode(Scene* scene,
                            entt::entity entity,
                            EditorScenePanelState& state,
                            entt::entity& selectedEntity,
                            std::string& selectedTextureAssetKey,
                            Assets::TextureAsset::Ptr& cachedTextureAsset,
                            std::string& selectedMaterialAssetKey,
                            Assets::MaterialAsset::Ptr& cachedMaterialAsset,
                            std::string& selectedNativeScriptAssetKey,
                            std::string& selectedPrefabAssetKey,
                            std::string& selectedTilesetAssetKey,
                            std::string& selectedAudioMixerAssetKey,
                            std::string& selectedInputActionsAssetKey,
                            const char* materialPayloadId,
                            const char* prefabPayloadId,
                            EditorUndoService* undoService,
                            const std::function<entt::entity(const std::string&, entt::entity)>& onInstantiatePrefabAtParent,
                            const std::function<bool(entt::entity)>& onCreatePrefabFromEntity,
                            const std::function<bool(entt::entity)>& onApplyPrefabFromEntity,
                            const std::function<entt::entity(entt::entity)>& onRevertPrefabEntity,
                            const std::function<bool(entt::entity)>& onUnpackPrefabEntity)
        {
            if (!scene || !scene->IsValid(entity))
                return false;

            AddEntityIfMissing(state.DrawOrderEntities, entity);

            auto& registry = scene->GetRegistry();
            const auto* tag = registry.try_get<TagComponent>(entity);
            const std::string label = (tag && !tag->Tag.empty()) ? tag->Tag : "Entity";
            const bool isDisabledEntity = tag && !tag->Enabled;
            const auto* prefabInstanceForNode = registry.try_get<PrefabInstanceComponent>(entity);
            const bool isPrefabInstanceRoot = prefabInstanceForNode && !prefabInstanceForNode->PrefabAssetKey.empty();
            const bool isPrefabLinkedEntity = IsEntityInPrefabInstanceSubtree(*scene, entity);
            const std::string nodeLabel = isPrefabInstanceRoot ? (label + " [Prefab]") : label;

            const auto children = scene->GetChildren(entity);
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (children.empty())
                flags |= ImGuiTreeNodeFlags_Leaf;
            if (selectedEntity == entity || ContainsEntity(state.MultiSelectedEntities, entity))
                flags |= ImGuiTreeNodeFlags_Selected;

            const bool pushCustomTextColor = isDisabledEntity || isPrefabLinkedEntity;
            if (pushCustomTextColor)
            {
                if (isDisabledEntity)
                    ImGui::PushStyleColor(ImGuiCol_Text, kDisabledEntityTextColor);
                else
                    ImGui::PushStyleColor(ImGuiCol_Text, kPrefabHighlightTextColor);
            }
            const bool opened = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uintptr_t>(static_cast<uint32_t>(entity))), flags, "%s", nodeLabel.c_str());
            if (pushCustomTextColor)
                ImGui::PopStyleColor();
            if (isPrefabLinkedEntity)
                DrawPrefabBadgeForLastItem();

            if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            {
                state.PendingClickSelectionEntity = entity;
                const ImGuiIO& io = ImGui::GetIO();
                state.PendingClickCtrlModifier = io.KeyCtrl || io.KeySuper;
                state.PendingClickShiftModifier = io.KeyShift;
            }

            if (ImGui::BeginPopupContextItem())
            {
                if (ImGui::MenuItem("Create Child"))
                {
                    entt::entity child = entt::null;
                    const bool created = undoService
                        ? undoService->ExecuteSceneMutation("Create Child", [&](Scene& mutableScene) {
                            child = mutableScene.CreateEntity("Entity");
                            return mutableScene.SetParent(child, entity);
                        })
                        : [&]() {
                            child = scene->CreateEntity("Entity");
                            return scene->SetParent(child, entity);
                        }();
                    if (!created)
                        child = entt::null;

                    SelectSingleEntity(state, selectedEntity, child);
                    selectedTextureAssetKey.clear();
                    cachedTextureAsset.reset();
                    selectedMaterialAssetKey.clear();
                    cachedMaterialAsset.reset();
                    selectedNativeScriptAssetKey.clear();
                    selectedPrefabAssetKey.clear();
                    selectedTilesetAssetKey.clear();
                    selectedAudioMixerAssetKey.clear();
                    selectedInputActionsAssetKey.clear();
                }

                if (ImGui::MenuItem("Rename"))
                {
                    state.RenameEntity = entity;
                    CopyTextToBuffer(state.RenameBuffer, label.c_str());
                    state.RenamePopupOpen = true;
                    ImGui::OpenPopup("Rename Entity");
                }

                if (ImGui::MenuItem("Delete"))
                {
                    std::vector<entt::entity> pendingDeleteSelection = { entity };
                    const bool clickedEntityInMultiSelection =
                        state.MultiSelectedEntities.size() > 1 &&
                        ContainsEntity(state.MultiSelectedEntities, entity);
                    if (clickedEntityInMultiSelection)
                    {
                        pendingDeleteSelection = BuildSelectionRoots(
                            *scene,
                            state.MultiSelectedEntities,
                            state.DrawOrderEntities);
                    }
                    state.PendingDeleteEntities = std::move(pendingDeleteSelection);
                }

                const auto* prefabInstance = registry.try_get<PrefabInstanceComponent>(entity);
                ImGui::Separator();
                if (prefabInstance && !prefabInstance->PrefabAssetKey.empty())
                {
                    if (ImGui::MenuItem("Apply Prefab") && onApplyPrefabFromEntity)
                        (void)onApplyPrefabFromEntity(entity);

                    if (ImGui::MenuItem("Revert Prefab") && onRevertPrefabEntity)
                    {
                        const entt::entity revertedEntity = onRevertPrefabEntity(entity);
                        if (revertedEntity != entt::null)
                        {
                            SelectSingleEntity(state, selectedEntity, revertedEntity);
                            selectedTextureAssetKey.clear();
                            cachedTextureAsset.reset();
                            selectedMaterialAssetKey.clear();
                            cachedMaterialAsset.reset();
                            selectedNativeScriptAssetKey.clear();
                            selectedPrefabAssetKey.clear();
                            selectedTilesetAssetKey.clear();
                            selectedAudioMixerAssetKey.clear();
                            selectedInputActionsAssetKey.clear();
                        }
                    }

                    if (ImGui::MenuItem("Unpack Prefab") && onUnpackPrefabEntity)
                        (void)onUnpackPrefabEntity(entity);
                }
                else
                {
                    if (ImGui::MenuItem("Create Prefab") && onCreatePrefabFromEntity)
                        (void)onCreatePrefabFromEntity(entity);
                }

                ImGui::EndPopup();
            }

            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
            {
                // Dragging from hierarchy should not immediately retarget Inspector selection.
                state.PendingClickSelectionEntity = entt::null;
                entt::entity payloadEntity = entity;
                ImGui::SetDragDropPayload(kSceneEntityPayload, &payloadEntity, sizeof(payloadEntity), ImGuiCond_Once);
                ImGui::Text("%s", label.c_str());
                ImGui::EndDragDropSource();
            }

            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kSceneEntityPayload))
                {
                    const auto* childEntity = static_cast<const entt::entity*>(payload->Data);
                    if (childEntity)
                    {
                        if (undoService)
                        {
                            (void)undoService->ExecuteSceneMutation("Reparent Entity", [&](Scene& mutableScene) {
                                return mutableScene.SetParent(*childEntity, entity);
                            });
                        }
                        else
                        {
                            scene->SetParent(*childEntity, entity);
                        }
                    }
                }
                else if (materialPayloadId)
                {
                    // Unity-style: dropping a material onto an entity assigns it to the renderer.
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(materialPayloadId))
                    {
                        const char* key = static_cast<const char*>(payload->Data);
                        if (key && key[0])
                        {
                            // Only entities with a SpriteComponent (quad renderer) can use materials right now.
                            if (registry.all_of<SpriteComponent>(entity))
                            {
                                if (undoService)
                                {
                                    (void)undoService->ExecuteSceneMutation("Assign Material", [&](Scene& mutableScene) {
                                        auto& mutableRegistry = mutableScene.GetRegistry();
                                        auto* material = mutableRegistry.try_get<MaterialComponent>(entity);
                                        if (!material)
                                            material = &mutableRegistry.emplace<MaterialComponent>(entity);
                                        material->MaterialKey = key;
                                        material->CachedMaterial.reset();
                                        material->MaterialLoadAttempted = false;
                                        return true;
                                    });
                                }
                                else
                                {
                                    auto* material = registry.try_get<MaterialComponent>(entity);
                                    if (!material)
                                        material = &registry.emplace<MaterialComponent>(entity);

                                    material->MaterialKey = key;
                                    material->CachedMaterial.reset();
                                    material->MaterialLoadAttempted = false;
                                }

                                // Make the drop feel like Unity: select the target object (not the asset).
                                SelectSingleEntity(state, selectedEntity, entity);
                                selectedTextureAssetKey.clear();
                                cachedTextureAsset.reset();
                                selectedMaterialAssetKey.clear();
                                cachedMaterialAsset.reset();
                                selectedNativeScriptAssetKey.clear();
                                selectedPrefabAssetKey.clear();
                                selectedTilesetAssetKey.clear();
                                selectedAudioMixerAssetKey.clear();
                                selectedInputActionsAssetKey.clear();
                            }
                        }
                    }
                }
                if (prefabPayloadId)
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(prefabPayloadId))
                    {
                        const std::string prefabAssetKey = ReadStringPayload(payload);
                        if (!prefabAssetKey.empty() && onInstantiatePrefabAtParent)
                        {
                            const entt::entity createdEntity = onInstantiatePrefabAtParent(prefabAssetKey, entity);
                            if (createdEntity != entt::null)
                            {
                                SelectSingleEntity(state, selectedEntity, createdEntity);
                                selectedTextureAssetKey.clear();
                                cachedTextureAsset.reset();
                                selectedMaterialAssetKey.clear();
                                cachedMaterialAsset.reset();
                                selectedNativeScriptAssetKey.clear();
                                selectedPrefabAssetKey.clear();
                                selectedTilesetAssetKey.clear();
                                selectedAudioMixerAssetKey.clear();
                                selectedInputActionsAssetKey.clear();
                            }
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }

            // Sibling drop zones (above/below item) emulate Unity-style "drop between rows".
            // This reparents the dragged entity to the hovered entity's parent.
            const ImVec2 itemMin = ImGui::GetItemRectMin();
            const ImVec2 itemMax = ImGui::GetItemRectMax();
            const float zoneHeight = 2.0f;
            const float zoneWidth = itemMax.x - itemMin.x;

            ImGui::PushID(static_cast<int>(entity));
            ImGui::SetCursorScreenPos(ImVec2(itemMin.x, itemMin.y - zoneHeight * 0.5f));
            ImGui::InvisibleButton("DropBefore", ImVec2(zoneWidth, zoneHeight));
            if (ImGui::IsItemHovered() && IsSceneEntityDragActive())
            {
                DrawDropTint(itemMin, itemMax);
                DrawHorizontalDropIndicator(itemMin.x + 6.0f, itemMax.x - 6.0f, itemMin.y);
            }
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kSceneEntityPayload))
                {
                    const auto* childEntity = static_cast<const entt::entity*>(payload->Data);
                    if (childEntity && *childEntity != entity)
                    {
                        if (undoService)
                        {
                            (void)undoService->ExecuteSceneMutation("Reorder Entity", [&](Scene& mutableScene) {
                                return mutableScene.SetSiblingOrderBefore(*childEntity, entity);
                            });
                        }
                        else
                        {
                            scene->SetSiblingOrderBefore(*childEntity, entity);
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::SetCursorScreenPos(ImVec2(itemMin.x, itemMax.y - zoneHeight * 0.5f));
            ImGui::InvisibleButton("DropAfter", ImVec2(zoneWidth, zoneHeight));
            if (ImGui::IsItemHovered() && IsSceneEntityDragActive())
            {
                DrawDropTint(itemMin, itemMax);
                DrawHorizontalDropIndicator(itemMin.x + 6.0f, itemMax.x - 6.0f, itemMax.y);
            }
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kSceneEntityPayload))
                {
                    const auto* childEntity = static_cast<const entt::entity*>(payload->Data);
                    if (childEntity && *childEntity != entity)
                    {
                        if (undoService)
                        {
                            (void)undoService->ExecuteSceneMutation("Reorder Entity", [&](Scene& mutableScene) {
                                return mutableScene.SetSiblingOrderAfter(*childEntity, entity);
                            });
                        }
                        else
                        {
                            scene->SetSiblingOrderAfter(*childEntity, entity);
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::PopID();

            bool deletedSelection = false;
            if (opened)
            {
                for (entt::entity child : children)
                {
                    if (DrawEntityNode(scene,
                                       child,
                                       state,
                                       selectedEntity,
                                       selectedTextureAssetKey,
                                       cachedTextureAsset,
                                       selectedMaterialAssetKey,
                                       cachedMaterialAsset,
                                       selectedNativeScriptAssetKey,
                                       selectedPrefabAssetKey,
                                       selectedTilesetAssetKey,
                                       selectedAudioMixerAssetKey,
                                       selectedInputActionsAssetKey,
                                       materialPayloadId,
                                       prefabPayloadId,
                                       undoService,
                                       onInstantiatePrefabAtParent,
                                       onCreatePrefabFromEntity,
                                       onApplyPrefabFromEntity,
                                       onRevertPrefabEntity,
                                       onUnpackPrefabEntity))
                        deletedSelection = true;
                }
                ImGui::TreePop();
            }

            return deletedSelection;
        }
    }

    void Draw(Scene* scene,
              EditorScenePanelState& state,
              entt::entity& selectedEntity,
              std::string& selectedTextureAssetKey,
              Assets::TextureAsset::Ptr& cachedTextureAsset,
              std::string& selectedMaterialAssetKey,
              Assets::MaterialAsset::Ptr& cachedMaterialAsset,
              std::string& selectedNativeScriptAssetKey,
              std::string& selectedPrefabAssetKey,
              std::string& selectedTilesetAssetKey,
              std::string& selectedAudioMixerAssetKey,
              std::string& selectedInputActionsAssetKey,
              const char* materialPayloadId,
              const char* prefabPayloadId,
              const std::string& sceneRootDisplayName,
              EditorUndoService* undoService,
              const std::function<entt::entity(const std::string&, entt::entity)>& onInstantiatePrefabAtParent,
              const std::function<bool(entt::entity)>& onCreatePrefabFromEntity,
              const std::function<bool(entt::entity)>& onApplyPrefabFromEntity,
              const std::function<entt::entity(entt::entity)>& onRevertPrefabEntity,
              const std::function<bool(entt::entity)>& onUnpackPrefabEntity)
    {
        ImGui::Begin("Scene");

        state.DrawOrderEntities.clear();
        PruneInvalidEntitySelection(scene, state.MultiSelectedEntities);
        if (scene && !scene->IsValid(state.SelectionAnchorEntity))
            state.SelectionAnchorEntity = entt::null;

        if (scene && !scene->IsValid(selectedEntity))
        {
            if (!state.MultiSelectedEntities.empty())
                selectedEntity = state.MultiSelectedEntities.back();
            else
                selectedEntity = entt::null;
        }

        if (scene && selectedEntity != entt::null && !ContainsEntity(state.MultiSelectedEntities, selectedEntity))
        {
            state.MultiSelectedEntities.clear();
            state.MultiSelectedEntities.push_back(selectedEntity);
            state.SelectionAnchorEntity = selectedEntity;
        }

        bool deletedSelection = false;
        if (scene && ImGui::BeginPopupContextWindow("SceneContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
        {
            if (ImGui::MenuItem("Create Entity"))
            {
                entt::entity createdEntity = entt::null;
                if (undoService)
                {
                    (void)undoService->ExecuteSceneMutation("Create Entity", [&](Scene& mutableScene) {
                        createdEntity = mutableScene.CreateEntity("Entity");
                        return true;
                    });
                }
                else
                {
                    createdEntity = scene->CreateEntity("Entity");
                }
                SelectSingleEntity(state, selectedEntity, createdEntity);
                selectedTextureAssetKey.clear();
                cachedTextureAsset.reset();
                selectedMaterialAssetKey.clear();
                cachedMaterialAsset.reset();
                selectedNativeScriptAssetKey.clear();
                selectedPrefabAssetKey.clear();
                selectedTilesetAssetKey.clear();
                selectedAudioMixerAssetKey.clear();
                selectedInputActionsAssetKey.clear();
            }
            if (ImGui::MenuItem("Create Grid (Tilemap)"))
            {
                entt::entity gridEntity = entt::null;
                if (undoService)
                {
                    (void)undoService->ExecuteSceneMutation("Create Grid (Tilemap)", [&](Scene& mutableScene) {
                        gridEntity = mutableScene.CreateEntity("Grid");
                        auto& reg = mutableScene.GetRegistry();
                        reg.emplace<Grid2DComponent>(gridEntity);

                        auto createLayer = [&](const char* name, int32_t order, bool collision) {
                            entt::entity layerEntity = mutableScene.CreateEntity(name);
                            mutableScene.SetParent(layerEntity, gridEntity);
                            auto& layer = reg.emplace<TilemapLayerComponent>(layerEntity);
                            layer.RenderOrder = order;
                            layer.CollisionEnabled = collision;
                            layer.EnsureStorage();
                        };
                        createLayer("Background", -20, false);
                        createLayer("Collision",    0, true);
                        createLayer("Foreground",  20, false);
                        return true;
                    });
                }
                else
                {
                    gridEntity = scene->CreateEntity("Grid");
                    auto& reg = scene->GetRegistry();
                    reg.emplace<Grid2DComponent>(gridEntity);

                    auto createLayer = [&](const char* name, int32_t order, bool collision) {
                        entt::entity layerEntity = scene->CreateEntity(name);
                        scene->SetParent(layerEntity, gridEntity);
                        auto& layer = reg.emplace<TilemapLayerComponent>(layerEntity);
                        layer.RenderOrder = order;
                        layer.CollisionEnabled = collision;
                        layer.EnsureStorage();
                    };
                    createLayer("Background", -20, false);
                    createLayer("Collision",    0, true);
                    createLayer("Foreground",  20, false);
                }
                SelectSingleEntity(state, selectedEntity, gridEntity);
                selectedTextureAssetKey.clear();
                cachedTextureAsset.reset();
                selectedMaterialAssetKey.clear();
                cachedMaterialAsset.reset();
                selectedNativeScriptAssetKey.clear();
                selectedPrefabAssetKey.clear();
                selectedTilesetAssetKey.clear();
                selectedAudioMixerAssetKey.clear();
                selectedInputActionsAssetKey.clear();
            }
            ImGui::EndPopup();
        }

        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !ImGui::IsAnyItemHovered())
        {
            state.PendingClickSelectionEntity = entt::null;
            ClearEntitySelectionState(state, selectedEntity);
            selectedTextureAssetKey.clear();
            cachedTextureAsset.reset();
            selectedMaterialAssetKey.clear();
            cachedMaterialAsset.reset();
            selectedNativeScriptAssetKey.clear();
            selectedPrefabAssetKey.clear();
            selectedTilesetAssetKey.clear();
            selectedAudioMixerAssetKey.clear();
            selectedInputActionsAssetKey.clear();
        }

        if (scene)
        {
            const char* rootDisplayLabel = sceneRootDisplayName.empty() ? "Scene Root" : sceneRootDisplayName.c_str();
            if (ImGui::TreeNodeEx("SceneRootTreeNode", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth, "%s", rootDisplayLabel))
            {
                if (ImGui::BeginPopupContextItem())
                {
                    if (ImGui::MenuItem("Create Entity"))
                    {
                        entt::entity createdEntity = entt::null;
                        if (undoService)
                        {
                            (void)undoService->ExecuteSceneMutation("Create Entity", [&](Scene& mutableScene) {
                                createdEntity = mutableScene.CreateEntity("Entity");
                                return true;
                            });
                        }
                        else
                        {
                            createdEntity = scene->CreateEntity("Entity");
                        }
                        SelectSingleEntity(state, selectedEntity, createdEntity);
                        selectedTextureAssetKey.clear();
                        cachedTextureAsset.reset();
                        selectedMaterialAssetKey.clear();
                        cachedMaterialAsset.reset();
                        selectedNativeScriptAssetKey.clear();
                        selectedPrefabAssetKey.clear();
                        selectedTilesetAssetKey.clear();
                        selectedAudioMixerAssetKey.clear();
                        selectedInputActionsAssetKey.clear();
                    }
                    ImGui::EndPopup();
                }

                const auto rootEntities = scene->GetChildren(entt::null);
                if (ImGui::BeginDragDropTarget())
                {
                    if (ImGui::IsItemHovered() && IsSceneEntityDragActive())
                    {
                        const ImVec2 rootMin = ImGui::GetItemRectMin();
                        const ImVec2 rootMax = ImGui::GetItemRectMax();
                        DrawDropTint(rootMin, rootMax);
                        DrawHorizontalDropIndicator(rootMin.x + 6.0f, rootMax.x - 6.0f, rootMax.y - 2.0f);
                    }

                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kSceneEntityPayload))
                    {
                        const auto* childEntity = static_cast<const entt::entity*>(payload->Data);
                        if (childEntity)
                        {
                            if (undoService)
                            {
                                (void)undoService->ExecuteSceneMutation("Reparent Entity", [&](Scene& mutableScene) {
                                    return mutableScene.SetParent(*childEntity, entt::null);
                                });
                            }
                            else
                            {
                                scene->SetParent(*childEntity, entt::null);
                            }
                        }
                    }
                    if (prefabPayloadId)
                    {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(prefabPayloadId))
                        {
                            const std::string prefabAssetKey = ReadStringPayload(payload);
                            if (!prefabAssetKey.empty() && onInstantiatePrefabAtParent)
                            {
                                const entt::entity createdEntity = onInstantiatePrefabAtParent(prefabAssetKey, entt::null);
                                if (createdEntity != entt::null)
                                {
                                    SelectSingleEntity(state, selectedEntity, createdEntity);
                                    selectedTextureAssetKey.clear();
                                    cachedTextureAsset.reset();
                                    selectedMaterialAssetKey.clear();
                                    cachedMaterialAsset.reset();
                                    selectedNativeScriptAssetKey.clear();
                                    selectedPrefabAssetKey.clear();
                                    selectedTilesetAssetKey.clear();
                                    selectedAudioMixerAssetKey.clear();
                                    selectedInputActionsAssetKey.clear();
                                }
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                for (entt::entity entity : rootEntities)
                {
                    if (DrawEntityNode(scene,
                                       entity,
                                       state,
                                       selectedEntity,
                                       selectedTextureAssetKey,
                                       cachedTextureAsset,
                                       selectedMaterialAssetKey,
                                       cachedMaterialAsset,
                                       selectedNativeScriptAssetKey,
                                       selectedPrefabAssetKey,
                                       selectedTilesetAssetKey,
                                       selectedAudioMixerAssetKey,
                                       selectedInputActionsAssetKey,
                                       materialPayloadId,
                                       prefabPayloadId,
                                       undoService,
                                       onInstantiatePrefabAtParent,
                                       onCreatePrefabFromEntity,
                                       onApplyPrefabFromEntity,
                                       onRevertPrefabEntity,
                                       onUnpackPrefabEntity))
                        deletedSelection = true;
                }

                // Dedicated root drop zone so dropping in Scene panel empty area always works.
                const ImVec2 rootZoneMin = ImGui::GetCursorScreenPos();
                const float rootZoneWidth = ImGui::GetContentRegionAvail().x;
                const float rootZoneHeight = (ImGui::GetContentRegionAvail().y > 40.0f) ? ImGui::GetContentRegionAvail().y : 40.0f;
                const ImVec2 rootZoneSize(rootZoneWidth, rootZoneHeight);
                ImGui::PushID("RootDropZone");
                ImGui::InvisibleButton("RootDropZoneButton", rootZoneSize);
                if (ImGui::BeginPopupContextItem())
                {
                    if (ImGui::MenuItem("Create Entity"))
                    {
                        entt::entity createdEntity = entt::null;
                        if (undoService)
                        {
                            (void)undoService->ExecuteSceneMutation("Create Entity", [&](Scene& mutableScene) {
                                createdEntity = mutableScene.CreateEntity("Entity");
                                return true;
                            });
                        }
                        else
                        {
                            createdEntity = scene->CreateEntity("Entity");
                        }
                        SelectSingleEntity(state, selectedEntity, createdEntity);
                        selectedTextureAssetKey.clear();
                        cachedTextureAsset.reset();
                        selectedMaterialAssetKey.clear();
                        cachedMaterialAsset.reset();
                        selectedNativeScriptAssetKey.clear();
                        selectedPrefabAssetKey.clear();
                        selectedTilesetAssetKey.clear();
                        selectedAudioMixerAssetKey.clear();
                        selectedInputActionsAssetKey.clear();
                    }
                    ImGui::EndPopup();
                }
                if (ImGui::IsItemHovered() && IsSceneEntityDragActive())
                {
                    const ImVec2 rootZoneMax(rootZoneMin.x + rootZoneSize.x, rootZoneMin.y + rootZoneSize.y);
                    DrawDropTint(rootZoneMin, rootZoneMax);
                    DrawHorizontalDropIndicator(rootZoneMin.x + 6.0f, rootZoneMax.x - 6.0f, rootZoneMin.y + rootZoneSize.y * 0.5f);
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    drawList->AddText(ImVec2(rootZoneMin.x + 10.0f, rootZoneMin.y + 2.0f), kDropIndicatorColor, "Drop to unparent");
                }
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kSceneEntityPayload))
                    {
                        const auto* childEntity = static_cast<const entt::entity*>(payload->Data);
                        if (childEntity)
                        {
                            if (undoService)
                            {
                                (void)undoService->ExecuteSceneMutation("Reparent Entity", [&](Scene& mutableScene) {
                                    return mutableScene.SetParent(*childEntity, entt::null);
                                });
                            }
                            else
                            {
                                scene->SetParent(*childEntity, entt::null);
                            }
                        }
                    }
                    if (prefabPayloadId)
                    {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(prefabPayloadId))
                        {
                            const std::string prefabAssetKey = ReadStringPayload(payload);
                            if (!prefabAssetKey.empty() && onInstantiatePrefabAtParent)
                            {
                                const entt::entity createdEntity = onInstantiatePrefabAtParent(prefabAssetKey, entt::null);
                                if (createdEntity != entt::null)
                                {
                                    SelectSingleEntity(state, selectedEntity, createdEntity);
                                    selectedTextureAssetKey.clear();
                                    cachedTextureAsset.reset();
                                    selectedMaterialAssetKey.clear();
                                    cachedMaterialAsset.reset();
                                    selectedNativeScriptAssetKey.clear();
                                    selectedPrefabAssetKey.clear();
                                    selectedTilesetAssetKey.clear();
                                    selectedAudioMixerAssetKey.clear();
                                    selectedInputActionsAssetKey.clear();
                                }
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::PopID();

                ImGui::TreePop();
            }
        }

        // Unity-like hierarchy shortcuts:
        // - Ctrl/Cmd + C: copy selected entity subtree(s).
        // - Ctrl/Cmd + V: paste copied subtree(s) as siblings of selection (or root if nothing selected).
        // - Ctrl/Cmd + D: duplicate selected entity subtree(s) as next siblings.
        // - Delete: delete selected entity subtree(s).
        if (scene && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
        {
            const ImGuiIO& io = ImGui::GetIO();
            const bool commandDown = io.KeyCtrl || io.KeySuper;
            const bool canHandleDeleteShortcut =
                !io.WantTextInput &&
                !ImGui::IsAnyItemActive() &&
                !state.RenamePopupOpen;
            const bool canHandleHierarchyShortcuts =
                commandDown &&
                canHandleDeleteShortcut;

            if (canHandleHierarchyShortcuts)
            {
                if (ImGui::IsKeyPressed(ImGuiKey_C, false))
                {
                    const std::vector<entt::entity> selectionRoots =
                        ResolveSelectionForSceneOperations(*scene, state, selectedEntity);
                    state.EntityClipboardScenes.clear();
                    state.EntityClipboardScene.reset();

                    for (entt::entity sourceEntity : selectionRoots)
                    {
                        auto copiedSubtree = EditorPrefabSystem::CreateDetachedEntitySubtree(*scene, sourceEntity);
                        if (copiedSubtree)
                            state.EntityClipboardScenes.push_back(std::move(copiedSubtree));
                    }

                    if (selectionRoots.size() == 1)
                        state.EntityClipboardScene = EditorPrefabSystem::CreateDetachedEntitySubtree(*scene, selectionRoots.front());
                }

                if (ImGui::IsKeyPressed(ImGuiKey_V, false))
                {
                    std::vector<const Scene*> clipboardScenes;
                    clipboardScenes.reserve(state.EntityClipboardScenes.size() + 1);
                    for (const auto& clipboardScene : state.EntityClipboardScenes)
                    {
                        if (clipboardScene)
                            clipboardScenes.push_back(clipboardScene.get());
                    }
                    if (clipboardScenes.empty() && state.EntityClipboardScene)
                        clipboardScenes.push_back(state.EntityClipboardScene.get());

                    if (!clipboardScenes.empty())
                    {
                        const entt::entity destinationParent =
                            scene->IsValid(selectedEntity) ? scene->GetParent(selectedEntity) : entt::null;
                        std::vector<entt::entity> pastedEntities;
                        pastedEntities.reserve(clipboardScenes.size());

                        const bool pasted = undoService
                            ? undoService->ExecuteSceneMutation("Paste Entity", [&](Scene& mutableScene) {
                                for (const Scene* clipboardScene : clipboardScenes)
                                {
                                    if (!clipboardScene)
                                        continue;

                                    const entt::entity pastedEntity = EditorPrefabSystem::InstantiateDetachedEntitySubtree(
                                        mutableScene,
                                        *clipboardScene,
                                        destinationParent);
                                    if (pastedEntity != entt::null)
                                        pastedEntities.push_back(pastedEntity);
                                }
                                return !pastedEntities.empty();
                            })
                            : [&]() {
                                for (const Scene* clipboardScene : clipboardScenes)
                                {
                                    if (!clipboardScene)
                                        continue;

                                    const entt::entity pastedEntity = EditorPrefabSystem::InstantiateDetachedEntitySubtree(
                                        *scene,
                                        *clipboardScene,
                                        destinationParent);
                                    if (pastedEntity != entt::null)
                                        pastedEntities.push_back(pastedEntity);
                                }
                                return !pastedEntities.empty();
                            }();

                        if (pasted)
                        {
                            selectedEntity = pastedEntities.back();
                            state.MultiSelectedEntities = pastedEntities;
                            state.SelectionAnchorEntity = selectedEntity;
                            ClearAssetSelectionState(
                                selectedTextureAssetKey,
                                cachedTextureAsset,
                                selectedMaterialAssetKey,
                                cachedMaterialAsset,
                                selectedNativeScriptAssetKey,
                                selectedPrefabAssetKey,
                                selectedTilesetAssetKey,
                                selectedAudioMixerAssetKey,
                                selectedInputActionsAssetKey);
                        }
                    }
                }

                if (ImGui::IsKeyPressed(ImGuiKey_D, false))
                {
                    const std::vector<entt::entity> sourceEntities =
                        ResolveSelectionForSceneOperations(*scene, state, selectedEntity);
                    if (!sourceEntities.empty())
                    {
                        std::vector<std::unique_ptr<Scene>> copiedSubtrees;
                        copiedSubtrees.reserve(sourceEntities.size());
                        for (entt::entity sourceEntity : sourceEntities)
                            copiedSubtrees.push_back(EditorPrefabSystem::CreateDetachedEntitySubtree(*scene, sourceEntity));

                        std::vector<entt::entity> duplicatedEntities;
                        duplicatedEntities.reserve(sourceEntities.size());
                        const bool duplicated = undoService
                            ? undoService->ExecuteSceneMutation("Duplicate Entity", [&](Scene& mutableScene) {
                                for (size_t index = 0; index < sourceEntities.size(); ++index)
                                {
                                    const entt::entity sourceEntity = sourceEntities[index];
                                    if (!mutableScene.IsValid(sourceEntity))
                                        continue;
                                    if (index >= copiedSubtrees.size() || !copiedSubtrees[index])
                                        continue;

                                    const entt::entity destinationParent = mutableScene.GetParent(sourceEntity);
                                    const entt::entity duplicatedEntity = EditorPrefabSystem::InstantiateDetachedEntitySubtree(
                                        mutableScene,
                                        *copiedSubtrees[index],
                                        destinationParent);
                                    if (duplicatedEntity == entt::null)
                                        continue;

                                    (void)mutableScene.SetSiblingOrderAfter(duplicatedEntity, sourceEntity);
                                    duplicatedEntities.push_back(duplicatedEntity);
                                }
                                return !duplicatedEntities.empty();
                            })
                            : [&]() {
                                for (size_t index = 0; index < sourceEntities.size(); ++index)
                                {
                                    const entt::entity sourceEntity = sourceEntities[index];
                                    if (!scene->IsValid(sourceEntity))
                                        continue;
                                    if (index >= copiedSubtrees.size() || !copiedSubtrees[index])
                                        continue;

                                    const entt::entity destinationParent = scene->GetParent(sourceEntity);
                                    const entt::entity duplicatedEntity = EditorPrefabSystem::InstantiateDetachedEntitySubtree(
                                        *scene,
                                        *copiedSubtrees[index],
                                        destinationParent);
                                    if (duplicatedEntity == entt::null)
                                        continue;

                                    (void)scene->SetSiblingOrderAfter(duplicatedEntity, sourceEntity);
                                    duplicatedEntities.push_back(duplicatedEntity);
                                }
                                return !duplicatedEntities.empty();
                            }();

                        if (duplicated)
                        {
                            selectedEntity = duplicatedEntities.back();
                            state.MultiSelectedEntities = duplicatedEntities;
                            state.SelectionAnchorEntity = selectedEntity;
                            ClearAssetSelectionState(
                                selectedTextureAssetKey,
                                cachedTextureAsset,
                                selectedMaterialAssetKey,
                                cachedMaterialAsset,
                                selectedNativeScriptAssetKey,
                                selectedPrefabAssetKey,
                                selectedTilesetAssetKey,
                                selectedAudioMixerAssetKey,
                                selectedInputActionsAssetKey);
                        }
                    }
                }
            }

            if (canHandleDeleteShortcut && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
            {
                const std::vector<entt::entity> selectionRoots =
                    ResolveSelectionForSceneOperations(*scene, state, selectedEntity);
                if (!selectionRoots.empty())
                    state.PendingDeleteEntities = selectionRoots;
            }
        }

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            if (state.PendingClickSelectionEntity != entt::null && ImGui::GetDragDropPayload() == nullptr)
            {
                const entt::entity clickedEntity = state.PendingClickSelectionEntity;
                const bool ctrlModifier = state.PendingClickCtrlModifier;
                const bool shiftModifier = state.PendingClickShiftModifier;

                if (!scene || !scene->IsValid(clickedEntity))
                {
                    ClearEntitySelectionState(state, selectedEntity);
                }
                else if (shiftModifier)
                {
                    const entt::entity anchorEntity = scene->IsValid(state.SelectionAnchorEntity)
                        ? state.SelectionAnchorEntity
                        : clickedEntity;
                    auto findDrawOrderIndex = [&](entt::entity entity) -> int32_t {
                        for (size_t index = 0; index < state.DrawOrderEntities.size(); ++index)
                        {
                            if (state.DrawOrderEntities[index] == entity)
                                return static_cast<int32_t>(index);
                        }
                        return -1;
                    };

                    const int32_t anchorIndex = findDrawOrderIndex(anchorEntity);
                    const int32_t clickedIndex = findDrawOrderIndex(clickedEntity);
                    if (!ctrlModifier)
                        state.MultiSelectedEntities.clear();

                    if (anchorIndex >= 0 && clickedIndex >= 0)
                    {
                        const int32_t minIndex = std::min(anchorIndex, clickedIndex);
                        const int32_t maxIndex = std::max(anchorIndex, clickedIndex);
                        for (int32_t index = minIndex; index <= maxIndex; ++index)
                            AddEntityIfMissing(state.MultiSelectedEntities, state.DrawOrderEntities[static_cast<size_t>(index)]);
                    }
                    else
                    {
                        AddEntityIfMissing(state.MultiSelectedEntities, clickedEntity);
                    }

                    state.SelectionAnchorEntity = clickedEntity;
                }
                else if (ctrlModifier)
                {
                    const auto foundIt = std::find(state.MultiSelectedEntities.begin(), state.MultiSelectedEntities.end(), clickedEntity);
                    if (foundIt != state.MultiSelectedEntities.end())
                        state.MultiSelectedEntities.erase(foundIt);
                    else
                        state.MultiSelectedEntities.push_back(clickedEntity);

                    state.SelectionAnchorEntity = clickedEntity;
                }
                else
                {
                    SelectSingleEntity(state, selectedEntity, clickedEntity);
                }

                if (!state.MultiSelectedEntities.empty())
                {
                    if (ContainsEntity(state.MultiSelectedEntities, clickedEntity))
                        selectedEntity = clickedEntity;
                    else
                        selectedEntity = state.MultiSelectedEntities.back();
                }
                else
                {
                    selectedEntity = entt::null;
                }

                ClearAssetSelectionState(
                    selectedTextureAssetKey,
                    cachedTextureAsset,
                    selectedMaterialAssetKey,
                    cachedMaterialAsset,
                    selectedNativeScriptAssetKey,
                    selectedPrefabAssetKey,
                    selectedTilesetAssetKey,
                    selectedAudioMixerAssetKey,
                    selectedInputActionsAssetKey);
            }

            state.PendingClickSelectionEntity = entt::null;
            state.PendingClickCtrlModifier = false;
            state.PendingClickShiftModifier = false;
        }

        if (state.RenamePopupOpen)
            ImGui::OpenPopup("Rename Entity");

        if (ImGui::BeginPopupModal("Rename Entity", &state.RenamePopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
        {
            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();

            const bool rename = ImGui::InputText("##RenameEntityName", state.RenameBuffer.data(), state.RenameBuffer.size(),
                                                 ImGuiInputTextFlags_EnterReturnsTrue);
            if (ImGui::Button("Rename", ImVec2(120, 0)) || rename)
            {
                if (scene && scene->IsValid(state.RenameEntity))
                {
                    if (auto* tag = scene->GetRegistry().try_get<TagComponent>(state.RenameEntity))
                    {
                        const std::string renameValue = state.RenameBuffer.data();
                        if (undoService)
                        {
                            (void)undoService->ExecuteSceneMutation("Rename Entity", [&](Scene& mutableScene) {
                                if (!mutableScene.IsValid(state.RenameEntity))
                                    return false;
                                if (auto* mutableTag = mutableScene.GetRegistry().try_get<TagComponent>(state.RenameEntity))
                                {
                                    mutableTag->Tag = renameValue;
                                    return true;
                                }
                                return false;
                            });
                        }
                        else
                        {
                            tag->Tag = renameValue;
                        }
                    }
                }
                state.RenamePopupOpen = false;
                state.RenameEntity = entt::null;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                state.RenamePopupOpen = false;
                state.RenameEntity = entt::null;
            }
            ImGui::EndPopup();
        }

        if (scene && !state.PendingDeleteEntities.empty())
        {
            const std::vector<entt::entity> entitiesToDelete =
                BuildSelectionRoots(*scene, state.PendingDeleteEntities, state.DrawOrderEntities);
            for (entt::entity entityToDelete : entitiesToDelete)
            {
                if (selectedEntity == entityToDelete || scene->IsDescendantOf(selectedEntity, entityToDelete))
                    deletedSelection = true;
            }

            if (undoService)
            {
                (void)undoService->ExecuteSceneMutation("Delete Entity", [&](Scene& mutableScene) {
                    bool deletedAny = false;
                    for (entt::entity entityToDelete : entitiesToDelete)
                    {
                        if (!mutableScene.IsValid(entityToDelete))
                            continue;

                        mutableScene.DestroyEntity(entityToDelete);
                        deletedAny = true;
                    }
                    return deletedAny;
                });
            }
            else
            {
                for (entt::entity entityToDelete : entitiesToDelete)
                {
                    if (scene->IsValid(entityToDelete))
                        scene->DestroyEntity(entityToDelete);
                }
            }
        }
        state.PendingDeleteEntities.clear();

        if (deletedSelection)
        {
            ClearEntitySelectionState(state, selectedEntity);
            selectedTextureAssetKey.clear();
            cachedTextureAsset.reset();
            selectedMaterialAssetKey.clear();
            cachedMaterialAsset.reset();
            selectedNativeScriptAssetKey.clear();
            selectedPrefabAssetKey.clear();
            selectedTilesetAssetKey.clear();
            selectedAudioMixerAssetKey.clear();
            selectedInputActionsAssetKey.clear();
        }

        ImGui::End();
    }
}
