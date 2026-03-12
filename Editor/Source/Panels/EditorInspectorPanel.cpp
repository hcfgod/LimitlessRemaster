#include "EditorInspectorPanel.h"

#include "EditorInspectorPanelAssetInspectors.h"
#include "EditorInspectorPanelComponentManagement.h"
#include "EditorInspectorPanelEntityComponents.h"
#include "EditorInspectorPanelNativeScriptComponent.h"
#include "EditorInspectorPanelNativeScriptEditor.h"
#include "EditorPanelLock.h"
#include "EditorPanelStyle.h"
#include "Undo/EditorUndoService.h"
#include "Scene/Scene.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string_view>

namespace Limitless::EditorInspectorPanel
{
    namespace
    {
        constexpr const char* kInspectorSectionPayload = "INSPECTOR_SECTION";
        constexpr ImU32 kInspectorDropIndicatorColor = IM_COL32(80, 160, 255, 255);
        constexpr ImU32 kInspectorDropIndicatorTintColor = IM_COL32(80, 160, 255, 48);
        constexpr float kInspectorDropIndicatorThickness = 3.0f;

        bool IsInspectorSectionDragActive()
        {
            const ImGuiPayload* payload = ImGui::GetDragDropPayload();
            if (!payload)
                return false;
            return std::strcmp(payload->DataType, kInspectorSectionPayload) == 0;
        }

        std::string ReadStringPayload(const ImGuiPayload* payload)
        {
            if (!payload || !payload->Data || payload->DataSize <= 0)
                return {};

            const char* bytes = static_cast<const char*>(payload->Data);
            const int payloadLength = std::max(0, payload->DataSize - 1);
            return std::string(bytes, bytes + payloadLength);
        }

        void DrawInspectorDropTint(const ImVec2& min, const ImVec2& max)
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            if (!drawList)
                return;
            drawList->AddRectFilled(min, max, kInspectorDropIndicatorTintColor, 6.0f);
        }

        void DrawInspectorDropLine(float minX, float maxX, float y)
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            if (!drawList)
                return;
            drawList->AddLine(ImVec2(minX, y), ImVec2(maxX, y), kInspectorDropIndicatorColor, kInspectorDropIndicatorThickness);
        }

        bool IsScriptSectionKey(std::string_view sectionKey)
        {
            return sectionKey.rfind("Script:", 0) == 0;
        }

        std::string GetInspectorSectionDisplayName(Scene* scene,
                                                   entt::registry& registry,
                                                   entt::entity selectedEntity,
                                                   std::string_view sectionKey)
        {
            (void)registry;
            if (!IsScriptSectionKey(sectionKey))
                return std::string(sectionKey);

            if (!scene || selectedEntity == entt::null || !scene->IsValid(selectedEntity))
                return std::string(sectionKey);

            const std::string entityText(sectionKey.substr(std::string_view("Script:").size()));
            int32_t scriptOrder = 0;
            try
            {
                scriptOrder = std::stoi(entityText);
            }
            catch (...)
            {
                return "Script";
            }

            const ScriptComponent* scriptComponent = nullptr;
            for (entt::entity scriptEntity : scene->GetScriptComponentEntities(selectedEntity))
            {
                const ScriptComponent* candidate = scene->GetScriptComponent(scriptEntity);
                if (!candidate || candidate->OwnerEntity != selectedEntity)
                    continue;
                if (candidate->ComponentOrder != scriptOrder)
                    continue;
                scriptComponent = candidate;
                break;
            }
            if (!scriptComponent)
                return "Script";

            if (const ManagedScriptEntry* managedEntry = scriptComponent->TryGetManagedEntry())
            {
                if (!managedEntry->ScriptClassName.empty())
                    return managedEntry->ScriptClassName;
            }
            else if (const NativeScriptEntry* nativeEntry = scriptComponent->TryGetNativeEntry())
            {
                if (!nativeEntry->ScriptClassName.empty())
                    return nativeEntry->ScriptClassName;
            }

            return "Script";
        }

        struct InspectorPersistentUiState final
        {
            std::unordered_map<std::string, bool> FoldoutState;
            std::unordered_map<std::string, std::vector<std::string>> SectionOrderState;
            std::string ActiveEntityContextKey;
        };

        InspectorPersistentUiState& GetInspectorPersistentUiState()
        {
            static InspectorPersistentUiState state;
            return state;
        }

        std::string NormalizePersistenceKey(std::string value)
        {
            std::replace(value.begin(), value.end(), '\\', '/');
            return value;
        }

        std::string BuildEntityContextKey(const std::string& sceneAssetKey, Scene* scene, entt::entity entity)
        {
            if (!scene || entity == entt::null || !scene->IsValid(entity))
                return {};

            auto& registry = scene->GetRegistry();
            std::vector<std::string> segments;
            std::unordered_set<entt::entity> visited;
            entt::entity current = entity;
            while (current != entt::null && scene->IsValid(current) && visited.insert(current).second)
            {
                std::string tag = "Entity";
                if (const auto* tagComponent = registry.try_get<TagComponent>(current);
                    tagComponent && !tagComponent->Tag.empty())
                {
                    tag = tagComponent->Tag;
                }

                const entt::entity parent = scene->GetParent(current);
                const std::vector<entt::entity> siblings = scene->GetChildren(parent);
                size_t siblingIndex = 0;
                for (size_t i = 0; i < siblings.size(); ++i)
                {
                    if (siblings[i] == current)
                    {
                        siblingIndex = i;
                        break;
                    }
                }

                segments.push_back(tag + "#" + std::to_string(siblingIndex));
                current = parent;
            }

            std::reverse(segments.begin(), segments.end());

            std::string entityPath;
            for (size_t i = 0; i < segments.size(); ++i)
            {
                if (!entityPath.empty())
                    entityPath += "/";
                entityPath += segments[i];
            }

            return NormalizePersistenceKey(sceneAssetKey.empty() ? "<unsaved>" : sceneAssetKey) + "|" + entityPath;
        }

        std::string BuildScopedFoldoutKey(std::string_view stateKeySuffix)
        {
            auto& persistentState = GetInspectorPersistentUiState();
            if (persistentState.ActiveEntityContextKey.empty())
                return std::string(stateKeySuffix);
            return persistentState.ActiveEntityContextKey + "|" + std::string(stateKeySuffix);
        }

        bool MoveSectionKeyToIndex(std::vector<std::string>& sectionKeys, std::string_view sectionKey, size_t destinationIndex)
        {
            if (sectionKeys.empty())
                return false;

            const auto sectionIt = std::find(sectionKeys.begin(), sectionKeys.end(), sectionKey);
            if (sectionIt == sectionKeys.end())
                return false;

            const size_t sourceIndex = static_cast<size_t>(std::distance(sectionKeys.begin(), sectionIt));
            destinationIndex = std::min(destinationIndex, sectionKeys.size());
            if (sourceIndex == destinationIndex || (sourceIndex + 1 == destinationIndex))
                return false;

            std::string movedKey = *sectionIt;
            sectionKeys.erase(sectionIt);
            if (sourceIndex < destinationIndex)
                --destinationIndex;
            destinationIndex = std::min(destinationIndex, sectionKeys.size());
            sectionKeys.insert(sectionKeys.begin() + static_cast<std::ptrdiff_t>(destinationIndex), std::move(movedKey));
            return true;
        }

        bool UpdateStoredSectionOrder(const std::vector<std::string>& orderedSectionKeys)
        {
            auto& persistentState = GetInspectorPersistentUiState();
            if (persistentState.ActiveEntityContextKey.empty())
                return false;

            persistentState.SectionOrderState[persistentState.ActiveEntityContextKey] = orderedSectionKeys;
            return true;
        }
    }

    void GetPersistentFoldoutState(std::unordered_map<std::string, bool>& outState)
    {
        outState = GetInspectorPersistentUiState().FoldoutState;
    }

    void ApplyPersistentFoldoutState(const std::unordered_map<std::string, bool>& state)
    {
        auto& persistentState = GetInspectorPersistentUiState();
        persistentState.FoldoutState = state;
    }

    void GetPersistentSectionOrderState(std::unordered_map<std::string, std::vector<std::string>>& outState)
    {
        outState = GetInspectorPersistentUiState().SectionOrderState;
    }

    void ApplyPersistentSectionOrderState(const std::unordered_map<std::string, std::vector<std::string>>& state)
    {
        auto& persistentState = GetInspectorPersistentUiState();
        persistentState.SectionOrderState = state;
    }

    std::vector<std::string> GetOrderedSectionKeys(const std::vector<std::string>& availableSectionKeys)
    {
        auto& persistentState = GetInspectorPersistentUiState();
        if (persistentState.ActiveEntityContextKey.empty() || availableSectionKeys.empty())
            return availableSectionKeys;

        auto& storedOrder = persistentState.SectionOrderState[persistentState.ActiveEntityContextKey];
        std::vector<std::string> orderedKeys;
        orderedKeys.reserve(availableSectionKeys.size());

        for (const std::string& storedKey : storedOrder)
        {
            if (std::find(availableSectionKeys.begin(), availableSectionKeys.end(), storedKey) == availableSectionKeys.end())
                continue;
            if (std::find(orderedKeys.begin(), orderedKeys.end(), storedKey) != orderedKeys.end())
                continue;
            orderedKeys.push_back(storedKey);
        }

        for (const std::string& availableKey : availableSectionKeys)
        {
            if (std::find(orderedKeys.begin(), orderedKeys.end(), availableKey) == orderedKeys.end())
                orderedKeys.push_back(availableKey);
        }

        if (storedOrder != orderedKeys)
            storedOrder = orderedKeys;

        return orderedKeys;
    }

    bool MovePersistentSectionKeyToIndex(std::string_view sectionKey,
                                         const std::vector<std::string>& orderedSectionKeys,
                                         size_t destinationIndex)
    {
        std::vector<std::string> updatedOrder = orderedSectionKeys;
        if (!MoveSectionKeyToIndex(updatedOrder, sectionKey, destinationIndex))
            return false;
        return UpdateStoredSectionOrder(updatedOrder);
    }

    bool HandleSectionDragDrop(std::string_view sectionKey,
                               const std::vector<std::string>& orderedSectionKeys,
                               const char* dragLabel)
    {
        if (sectionKey.empty() || orderedSectionKeys.size() <= 1)
            return false;

        const auto sectionIt = std::find(orderedSectionKeys.begin(), orderedSectionKeys.end(), sectionKey);
        if (sectionIt == orderedSectionKeys.end())
            return false;

        const size_t sectionIndex = static_cast<size_t>(std::distance(orderedSectionKeys.begin(), sectionIt));
        const std::string sectionKeyString(sectionKey);
        bool changed = false;

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
        {
            ImGui::SetDragDropPayload(kInspectorSectionPayload,
                                      sectionKeyString.c_str(),
                                      static_cast<int>(sectionKeyString.size() + 1),
                                      ImGuiCond_Once);
            ImGui::TextUnformatted((dragLabel && dragLabel[0]) ? dragLabel : sectionKeyString.c_str());
            ImGui::EndDragDropSource();
        }

        const ImVec2 itemMin = ImGui::GetItemRectMin();
        const ImVec2 itemMax = ImGui::GetItemRectMax();
        const float zoneHeight = 3.0f;
        const float zoneWidth = itemMax.x - itemMin.x;

        ImGui::PushID(sectionKeyString.c_str());

        ImGui::SetCursorScreenPos(ImVec2(itemMin.x, itemMin.y - zoneHeight * 0.5f));
        ImGui::InvisibleButton("DropBefore", ImVec2(zoneWidth, zoneHeight));
        if (ImGui::IsItemHovered() && IsInspectorSectionDragActive())
        {
            DrawInspectorDropTint(itemMin, itemMax);
            DrawInspectorDropLine(itemMin.x + 6.0f, itemMax.x - 6.0f, itemMin.y);
        }
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kInspectorSectionPayload))
            {
                const std::string draggedSectionKey = ReadStringPayload(payload);
                if (!draggedSectionKey.empty() && draggedSectionKey != sectionKeyString)
                    changed |= MovePersistentSectionKeyToIndex(draggedSectionKey, orderedSectionKeys, sectionIndex);
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::SetCursorScreenPos(ImVec2(itemMin.x, itemMax.y - zoneHeight * 0.5f));
        ImGui::InvisibleButton("DropAfter", ImVec2(zoneWidth, zoneHeight));
        if (ImGui::IsItemHovered() && IsInspectorSectionDragActive())
        {
            DrawInspectorDropTint(itemMin, itemMax);
            DrawInspectorDropLine(itemMin.x + 6.0f, itemMax.x - 6.0f, itemMax.y);
        }
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kInspectorSectionPayload))
            {
                const std::string draggedSectionKey = ReadStringPayload(payload);
                if (!draggedSectionKey.empty() && draggedSectionKey != sectionKeyString)
                    changed |= MovePersistentSectionKeyToIndex(draggedSectionKey, orderedSectionKeys, sectionIndex + 1);
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::PopID();
        return changed;
    }

    bool BeginPersistentTreeNode(const char* stateKeySuffix, const char* label, int treeNodeFlags)
    {
        auto& persistentState = GetInspectorPersistentUiState();
        const std::string fullKey = BuildScopedFoldoutKey(stateKeySuffix ? stateKeySuffix : (label ? label : ""));
        if (const auto foldoutIt = persistentState.FoldoutState.find(fullKey);
            foldoutIt != persistentState.FoldoutState.end())
        {
            ImGui::SetNextItemOpen(foldoutIt->second, ImGuiCond_Always);
        }

        const bool isOpen = ImGui::TreeNodeEx(label, static_cast<ImGuiTreeNodeFlags>(treeNodeFlags));
        persistentState.FoldoutState[fullKey] = isOpen;
        return isOpen;
    }

    void Draw(Scene* scene,
              const std::string& sceneAssetKey,
              bool& isOpen,
              entt::entity selectedEntity,
              const char* texturePayloadId,
              std::string& selectedTextureAssetKey,
              Assets::TextureAsset::Ptr& cachedTextureAsset,
              const char* audioPayloadId,
              const char* materialPayloadId,
              const char* shaderPayloadId,
              const char* fontPayloadId,
              EditorAssetPreview::MaterialPreviewCache& materialPreviewCache,
              std::string& selectedMaterialAssetKey,
              Assets::MaterialAsset::Ptr& cachedMaterialAsset,
              std::string& selectedNativeScriptAssetKey,
              std::string& selectedPrefabAssetKey,
              std::string& selectedTilesetAssetKey,
              std::string& selectedAudioMixerAssetKey,
              std::string& selectedInputActionsAssetKey,
              std::string& selectedAnimationClipAssetKey,
              std::string& selectedAnimatorControllerAssetKey,
              EditorUndoService* undoService)
    {
        RestorePendingNativeScriptEditorSession();

        EditorPanelStyle::PushPanelVisualStyle();
        if (!ImGui::Begin("Inspector", &isOpen))
        {
            GetInspectorPersistentUiState().ActiveEntityContextKey.clear();
            ImGui::End();
            EditorPanelStyle::PopPanelVisualStyle();
            DrawNativeScriptEditorWindow();
            return;
        }

        const bool hasValidSelectedEntity = scene && selectedEntity != entt::null && scene->IsValid(selectedEntity);

        // Animation clip/controller selections live in their own dedicated panels,
        // so they must NOT prevent the entity inspector from appearing and must NOT
        // be cleared when an entity is selected.
        const bool hasSelectedAsset =
            !selectedInputActionsAssetKey.empty() ||
            !selectedAudioMixerAssetKey.empty() ||
            !selectedMaterialAssetKey.empty() ||
            !selectedTextureAssetKey.empty() ||
            !selectedNativeScriptAssetKey.empty() ||
            !selectedPrefabAssetKey.empty() ||
            !selectedTilesetAssetKey.empty();
        const bool showEntityInspector = hasValidSelectedEntity && !hasSelectedAsset;
        auto& persistentState = GetInspectorPersistentUiState();
        if (showEntityInspector)
        {
            persistentState.ActiveEntityContextKey = BuildEntityContextKey(sceneAssetKey, scene, selectedEntity);
            selectedInputActionsAssetKey.clear();
            selectedAudioMixerAssetKey.clear();
            selectedMaterialAssetKey.clear();
            selectedTextureAssetKey.clear();
            cachedTextureAsset.reset();
            cachedMaterialAsset.reset();
            selectedNativeScriptAssetKey.clear();
            selectedPrefabAssetKey.clear();
            selectedTilesetAssetKey.clear();
        }
        else if (!hasValidSelectedEntity)
        {
            persistentState.ActiveEntityContextKey.clear();
        }

        if (showEntityInspector)
        {
            auto& registry = scene->GetRegistry();
            PendingEntityComponentRemovals pendingRemovals{};
            DrawEntityHeaderSection(scene, registry, selectedEntity, undoService);

            std::vector<std::string> availableSectionKeys = CollectStandardEntityComponentSectionKeys(registry, selectedEntity);
            const std::vector<std::string> scriptSectionKeys = CollectScriptComponentSectionKeys(scene, selectedEntity);
            availableSectionKeys.insert(availableSectionKeys.end(), scriptSectionKeys.begin(), scriptSectionKeys.end());
            const std::vector<std::string> orderedSectionKeys = GetOrderedSectionKeys(availableSectionKeys);

            for (const std::string& sectionKey : orderedSectionKeys)
            {
                if (IsScriptSectionKey(sectionKey))
                {
                    DrawScriptComponentSections(scene, registry, selectedEntity, undoService, sectionKey, &orderedSectionKeys);
                    continue;
                }

                DrawStandardEntityComponentSections(
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
                    sectionKey,
                    &orderedSectionKeys);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            DrawAddComponentPopup(scene, registry, selectedEntity, undoService);

            ApplyPendingEntityComponentRemovals(scene, registry, selectedEntity, pendingRemovals, undoService);
        }
        else if (!selectedInputActionsAssetKey.empty())
        {
            DrawInputActionsAssetInspector(selectedInputActionsAssetKey);
        }
        else if (!selectedAnimationClipAssetKey.empty())
        {
            DrawAnimationClipAssetInspector(selectedAnimationClipAssetKey);
        }
        else if (!selectedAnimatorControllerAssetKey.empty())
        {
            DrawAnimatorControllerAssetInspector(selectedAnimatorControllerAssetKey);
        }
        else if (!selectedAudioMixerAssetKey.empty())
        {
            DrawAudioMixerAssetInspector(selectedAudioMixerAssetKey);
        }
        else if (!selectedMaterialAssetKey.empty())
        {
            DrawMaterialInspector(texturePayloadId, shaderPayloadId, materialPreviewCache, selectedMaterialAssetKey, cachedMaterialAsset);
        }
        else if (!selectedTextureAssetKey.empty())
        {
            DrawTextureInspector(scene, selectedTextureAssetKey, cachedTextureAsset);
        }
        else if (!selectedNativeScriptAssetKey.empty())
        {
            DrawNativeScriptAssetInspector(selectedNativeScriptAssetKey);
        }
        else if (!selectedPrefabAssetKey.empty())
        {
            DrawPrefabAssetInspector(selectedPrefabAssetKey);
        }
        else
        {
            ImGui::Text("Select an object to edit.");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Text("No selection.");
        }

        persistentState.ActiveEntityContextKey.clear();
        ImGui::End();
        EditorPanelStyle::PopPanelVisualStyle();
        DrawNativeScriptEditorWindow();
    }

    void DrawInstance(const char* windowName,
                      EditorInspectorInstanceState& instanceState,
                      Scene* scene,
                      const std::string& sceneAssetKey,
                      entt::entity liveSelectedEntity,
                      const char* texturePayloadId,
                      std::string& liveSelectedTextureAssetKey,
                      Assets::TextureAsset::Ptr& liveCachedTextureAsset,
                      const char* audioPayloadId,
                      const char* materialPayloadId,
                      const char* shaderPayloadId,
                      const char* fontPayloadId,
                      EditorAssetPreview::MaterialPreviewCache& materialPreviewCache,
                      std::string& liveSelectedMaterialAssetKey,
                      Assets::MaterialAsset::Ptr& liveCachedMaterialAsset,
                      std::string& liveSelectedNativeScriptAssetKey,
                      std::string& liveSelectedPrefabAssetKey,
                      std::string& liveSelectedTilesetAssetKey,
                      std::string& liveSelectedAudioMixerAssetKey,
                      std::string& liveSelectedInputActionsAssetKey,
                      std::string& liveSelectedAnimationClipAssetKey,
                      std::string& liveSelectedAnimatorControllerAssetKey,
                      EditorUndoService* undoService)
    {
        if (!instanceState.IsOpen)
            return;

        RestorePendingNativeScriptEditorSession();

        EditorPanelStyle::PushPanelVisualStyle();
        if (!ImGui::Begin(windowName, &instanceState.IsOpen))
        {
            GetInspectorPersistentUiState().ActiveEntityContextKey.clear();
            ImGui::End();
            EditorPanelStyle::PopPanelVisualStyle();
            DrawNativeScriptEditorWindow();
            return;
        }

        instanceState.IsLocked = EditorPanelLock::DrawLockToggle(instanceState.IsLocked);

        // Resolve effective selection: locked panels use snapshot, unlocked use live.
        entt::entity effectiveEntity = liveSelectedEntity;
        std::string effectiveTextureKey = liveSelectedTextureAssetKey;
        Assets::TextureAsset::Ptr effectiveCachedTexture = liveCachedTextureAsset;
        std::string effectiveMaterialKey = liveSelectedMaterialAssetKey;
        Assets::MaterialAsset::Ptr effectiveCachedMaterial = liveCachedMaterialAsset;
        std::string effectiveNativeScriptKey = liveSelectedNativeScriptAssetKey;
        std::string effectivePrefabKey = liveSelectedPrefabAssetKey;
        std::string effectiveTilesetKey = liveSelectedTilesetAssetKey;
        std::string effectiveAudioMixerKey = liveSelectedAudioMixerAssetKey;
        std::string effectiveInputActionsKey = liveSelectedInputActionsAssetKey;
        std::string effectiveAnimationClipKey = liveSelectedAnimationClipAssetKey;
        std::string effectiveAnimatorControllerKey = liveSelectedAnimatorControllerAssetKey;

        if (instanceState.IsLocked)
        {
            effectiveEntity = instanceState.LockedEntity;
            effectiveTextureKey = instanceState.LockedTextureAssetKey;
            effectiveCachedTexture = instanceState.LockedCachedTextureAsset;
            effectiveMaterialKey = instanceState.LockedMaterialAssetKey;
            effectiveCachedMaterial = instanceState.LockedCachedMaterialAsset;
            effectiveNativeScriptKey = instanceState.LockedNativeScriptAssetKey;
            effectivePrefabKey = instanceState.LockedPrefabAssetKey;
            effectiveTilesetKey = instanceState.LockedTilesetAssetKey;
            effectiveAudioMixerKey = instanceState.LockedAudioMixerAssetKey;
            effectiveInputActionsKey = instanceState.LockedInputActionsAssetKey;
            effectiveAnimationClipKey = instanceState.LockedAnimationClipAssetKey;
            effectiveAnimatorControllerKey = instanceState.LockedAnimatorControllerAssetKey;
        }
        else
        {
            // Snapshot current live selection for when lock is toggled on.
            const bool hasAnyAsset =
                !liveSelectedTextureAssetKey.empty() ||
                !liveSelectedMaterialAssetKey.empty() ||
                !liveSelectedNativeScriptAssetKey.empty() ||
                !liveSelectedPrefabAssetKey.empty() ||
                !liveSelectedTilesetAssetKey.empty() ||
                !liveSelectedAudioMixerAssetKey.empty() ||
                !liveSelectedInputActionsAssetKey.empty() ||
                !liveSelectedAnimationClipAssetKey.empty() ||
                !liveSelectedAnimatorControllerAssetKey.empty();
            const bool hasEntity = scene && liveSelectedEntity != entt::null && scene->IsValid(liveSelectedEntity);

            if (hasAnyAsset || hasEntity)
            {
                instanceState.LockedEntity = liveSelectedEntity;
                instanceState.LockedTextureAssetKey = liveSelectedTextureAssetKey;
                instanceState.LockedCachedTextureAsset = liveCachedTextureAsset;
                instanceState.LockedMaterialAssetKey = liveSelectedMaterialAssetKey;
                instanceState.LockedCachedMaterialAsset = liveCachedMaterialAsset;
                instanceState.LockedNativeScriptAssetKey = liveSelectedNativeScriptAssetKey;
                instanceState.LockedPrefabAssetKey = liveSelectedPrefabAssetKey;
                instanceState.LockedTilesetAssetKey = liveSelectedTilesetAssetKey;
                instanceState.LockedAudioMixerAssetKey = liveSelectedAudioMixerAssetKey;
                instanceState.LockedInputActionsAssetKey = liveSelectedInputActionsAssetKey;
                instanceState.LockedAnimationClipAssetKey = liveSelectedAnimationClipAssetKey;
                instanceState.LockedAnimatorControllerAssetKey = liveSelectedAnimatorControllerAssetKey;
            }
        }

        // Animation clip/controller selections live in their own dedicated panels,
        // so they must NOT prevent the entity inspector from appearing.
        const bool hasSelectedAsset =
            !effectiveInputActionsKey.empty() ||
            !effectiveAudioMixerKey.empty() ||
            !effectiveMaterialKey.empty() ||
            !effectiveTextureKey.empty() ||
            !effectiveNativeScriptKey.empty() ||
            !effectivePrefabKey.empty() ||
            !effectiveTilesetKey.empty();
        const bool hasValidSelectedEntity = scene && effectiveEntity != entt::null && scene->IsValid(effectiveEntity);
        const bool showEntityInspector = hasValidSelectedEntity && !hasSelectedAsset;
        auto& persistentState = GetInspectorPersistentUiState();
        if (showEntityInspector)
        {
            persistentState.ActiveEntityContextKey = BuildEntityContextKey(sceneAssetKey, scene, effectiveEntity);
        }
        else
        {
            persistentState.ActiveEntityContextKey.clear();
        }

        if (showEntityInspector)
        {
            auto& registry = scene->GetRegistry();
            PendingEntityComponentRemovals pendingRemovals{};
            DrawEntityHeaderSection(scene, registry, effectiveEntity, undoService);

            std::vector<std::string> availableSectionKeys = CollectStandardEntityComponentSectionKeys(registry, effectiveEntity);
            const std::vector<std::string> scriptSectionKeys = CollectScriptComponentSectionKeys(scene, effectiveEntity);
            availableSectionKeys.insert(availableSectionKeys.end(), scriptSectionKeys.begin(), scriptSectionKeys.end());
            const std::vector<std::string> orderedSectionKeys = GetOrderedSectionKeys(availableSectionKeys);

            for (const std::string& sectionKey : orderedSectionKeys)
            {
                if (IsScriptSectionKey(sectionKey))
                {
                    DrawScriptComponentSections(scene, registry, effectiveEntity, undoService, sectionKey, &orderedSectionKeys);
                    continue;
                }

                DrawStandardEntityComponentSections(
                    scene,
                    registry,
                    effectiveEntity,
                    texturePayloadId,
                    audioPayloadId,
                    materialPayloadId,
                    fontPayloadId,
                    effectiveAnimationClipKey,
                    effectiveAnimatorControllerKey,
                    pendingRemovals,
                    undoService,
                    sectionKey,
                    &orderedSectionKeys);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            DrawAddComponentPopup(scene, registry, effectiveEntity, undoService);
            ApplyPendingEntityComponentRemovals(scene, registry, effectiveEntity, pendingRemovals, undoService);
        }
        else if (!effectiveInputActionsKey.empty())
        {
            DrawInputActionsAssetInspector(effectiveInputActionsKey);
        }
        else if (!effectiveAnimationClipKey.empty())
        {
            DrawAnimationClipAssetInspector(effectiveAnimationClipKey);
        }
        else if (!effectiveAnimatorControllerKey.empty())
        {
            DrawAnimatorControllerAssetInspector(effectiveAnimatorControllerKey);
        }
        else if (!effectiveAudioMixerKey.empty())
        {
            DrawAudioMixerAssetInspector(effectiveAudioMixerKey);
        }
        else if (!effectiveMaterialKey.empty())
        {
            DrawMaterialInspector(texturePayloadId, shaderPayloadId, materialPreviewCache, effectiveMaterialKey, effectiveCachedMaterial);
        }
        else if (!effectiveTextureKey.empty())
        {
            DrawTextureInspector(scene, effectiveTextureKey, effectiveCachedTexture);
        }
        else if (!effectiveNativeScriptKey.empty())
        {
            DrawNativeScriptAssetInspector(effectiveNativeScriptKey);
        }
        else if (!effectivePrefabKey.empty())
        {
            DrawPrefabAssetInspector(effectivePrefabKey);
        }
        else
        {
            ImGui::Text("Select an object to edit.");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Text("No selection.");
        }

        persistentState.ActiveEntityContextKey.clear();
        ImGui::End();
        EditorPanelStyle::PopPanelVisualStyle();
        DrawNativeScriptEditorWindow();
    }
}
