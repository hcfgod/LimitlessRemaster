#include "EditorScenePanel.h"

#include "Scene/Scene.h"
#include "imgui/imgui.h"

#include <cstdio>
#include <cstdint>
#include <cstring>

namespace Limitless::EditorScenePanel
{
    namespace
    {
        constexpr const char* kSceneEntityPayload = "SCENE_ENTITY";
        constexpr ImU32 kDropIndicatorColor = IM_COL32(80, 160, 255, 255);
        constexpr ImU32 kDropIndicatorTintColor = IM_COL32(80, 160, 255, 48);
        constexpr float kDropIndicatorThickness = 3.0f;
        constexpr float kDropIndicatorCapHeight = 8.0f;

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

        bool DrawEntityNode(Scene* scene,
                            entt::entity entity,
                            EditorScenePanelState& state,
                            entt::entity& selectedEntity,
                            std::string& selectedTextureAssetKey,
                            Assets::TextureAsset::Ptr& cachedTextureAsset,
                            std::string& selectedMaterialAssetKey,
                            Assets::MaterialAsset::Ptr& cachedMaterialAsset,
                            const char* materialPayloadId)
        {
            if (!scene || !scene->IsValid(entity))
                return false;

            auto& registry = scene->GetRegistry();
            const auto* tag = registry.try_get<TagComponent>(entity);
            const std::string label = (tag && !tag->Tag.empty()) ? tag->Tag : "Entity";

            const auto children = scene->GetChildren(entity);
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (children.empty())
                flags |= ImGuiTreeNodeFlags_Leaf;
            if (selectedEntity == entity)
                flags |= ImGuiTreeNodeFlags_Selected;

            const bool opened = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uintptr_t>(static_cast<uint32_t>(entity))), flags, "%s", label.c_str());

            if (ImGui::IsItemClicked())
            {
                selectedEntity = entity;
                selectedTextureAssetKey.clear();
                cachedTextureAsset.reset();
                selectedMaterialAssetKey.clear();
                cachedMaterialAsset.reset();
            }

            if (ImGui::BeginPopupContextItem())
            {
                if (ImGui::MenuItem("Create Child"))
                {
                    entt::entity child = scene->CreateEntity("Entity");
                    scene->SetParent(child, entity);
                    selectedEntity = child;
                    selectedTextureAssetKey.clear();
                    cachedTextureAsset.reset();
                    selectedMaterialAssetKey.clear();
                    cachedMaterialAsset.reset();
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
                    state.PendingDeleteEntity = entity;
                }

                ImGui::EndPopup();
            }

            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
            {
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
                        scene->SetParent(*childEntity, entity);
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
                                auto* material = registry.try_get<MaterialComponent>(entity);
                                if (!material)
                                    material = &registry.emplace<MaterialComponent>(entity);

                                material->MaterialKey = key;
                                material->CachedMaterial.reset();

                                // Make the drop feel like Unity: select the target object (not the asset).
                                selectedEntity = entity;
                                selectedTextureAssetKey.clear();
                                cachedTextureAsset.reset();
                                selectedMaterialAssetKey.clear();
                                cachedMaterialAsset.reset();
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
            const entt::entity parentEntity = scene->GetParent(entity);

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
                        scene->SetSiblingOrderBefore(*childEntity, entity);
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
                        scene->SetSiblingOrderAfter(*childEntity, entity);
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
                                       materialPayloadId))
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
              const char* materialPayloadId)
    {
        ImGui::Begin("Scene");

        bool deletedSelection = false;
        if (scene && ImGui::BeginPopupContextWindow("SceneContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
        {
            if (ImGui::MenuItem("Create Entity"))
            {
                entt::entity createdEntity = scene->CreateEntity("Entity");
                selectedEntity = createdEntity;
                selectedTextureAssetKey.clear();
                cachedTextureAsset.reset();
                selectedMaterialAssetKey.clear();
                cachedMaterialAsset.reset();
            }
            ImGui::EndPopup();
        }

        if (scene)
        {
            if (ImGui::TreeNodeEx("Scene Root", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth))
            {
                if (ImGui::BeginPopupContextItem())
                {
                    if (ImGui::MenuItem("Create Entity"))
                    {
                        entt::entity createdEntity = scene->CreateEntity("Entity");
                        selectedEntity = createdEntity;
                        selectedTextureAssetKey.clear();
                        cachedTextureAsset.reset();
                        selectedMaterialAssetKey.clear();
                        cachedMaterialAsset.reset();
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
                            scene->SetParent(*childEntity, entt::null);
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
                                       materialPayloadId))
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
                        entt::entity createdEntity = scene->CreateEntity("Entity");
                        selectedEntity = createdEntity;
                        selectedTextureAssetKey.clear();
                        cachedTextureAsset.reset();
                        selectedMaterialAssetKey.clear();
                        cachedMaterialAsset.reset();
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
                            scene->SetParent(*childEntity, entt::null);
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::PopID();

                ImGui::TreePop();
            }
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
                        tag->Tag = state.RenameBuffer.data();
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

        if (scene && state.PendingDeleteEntity != entt::null && scene->IsValid(state.PendingDeleteEntity))
        {
            if (selectedEntity == state.PendingDeleteEntity || scene->IsDescendantOf(selectedEntity, state.PendingDeleteEntity))
                deletedSelection = true;
            scene->DestroyEntity(state.PendingDeleteEntity);
        }
        state.PendingDeleteEntity = entt::null;

        if (deletedSelection)
        {
            selectedEntity = entt::null;
            selectedTextureAssetKey.clear();
            cachedTextureAsset.reset();
            selectedMaterialAssetKey.clear();
            cachedMaterialAsset.reset();
        }

        ImGui::End();
    }
}
