#include "EditorInspectorPanel.h"

#include "EditorInspectorPanelAssetInspectors.h"
#include "EditorInspectorPanelComponentManagement.h"
#include "EditorInspectorPanelEntityComponents.h"
#include "EditorInspectorPanelNativeScriptComponent.h"
#include "EditorInspectorPanelNativeScriptEditor.h"
#include "EditorPanelStyle.h"
#include "Undo/EditorUndoService.h"
#include "Scene/Scene.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string_view>

namespace Limitless::EditorInspectorPanel
{
    namespace
    {
        struct InspectorPersistentUiState final
        {
            std::unordered_map<std::string, bool> FoldoutState;
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

        static entt::entity animationPanelSelectionOwner = entt::null;
        const bool hasValidSelectedEntity = scene && selectedEntity != entt::null && scene->IsValid(selectedEntity);
        auto& persistentState = GetInspectorPersistentUiState();
        if (hasValidSelectedEntity)
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

            // Keep timeline/graph selection while editing the same entity's Animator.
            if (animationPanelSelectionOwner != selectedEntity)
            {
                selectedAnimationClipAssetKey.clear();
                selectedAnimatorControllerAssetKey.clear();
                animationPanelSelectionOwner = selectedEntity;
            }
        }
        else
        {
            animationPanelSelectionOwner = entt::null;
            persistentState.ActiveEntityContextKey.clear();
        }

        if (hasValidSelectedEntity)
        {
            auto& registry = scene->GetRegistry();
            PendingEntityComponentRemovals pendingRemovals{};
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
                undoService);

            DrawScriptComponentSections(scene, registry, selectedEntity, undoService);

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
}
