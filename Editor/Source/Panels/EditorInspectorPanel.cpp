#include "EditorInspectorPanel.h"

#include "EditorInspectorPanelAssetInspectors.h"
#include "EditorInspectorPanelComponentManagement.h"
#include "EditorInspectorPanelEntityComponents.h"
#include "EditorInspectorPanelNativeScriptComponent.h"
#include "EditorInspectorPanelNativeScriptEditor.h"
#include "Undo/EditorUndoService.h"
#include "Scene/Scene.h"
#include "imgui/imgui.h"

namespace Limitless::EditorInspectorPanel
{
    void Draw(Scene* scene,
              entt::entity selectedEntity,
              const char* texturePayloadId,
              std::string& selectedTextureAssetKey,
              Assets::TextureAsset::Ptr& cachedTextureAsset,
              const char* audioPayloadId,
              const char* materialPayloadId,
              const char* shaderPayloadId,
              const char* fontPayloadId,
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

        ImGui::Begin("Inspector");

        static entt::entity animationPanelSelectionOwner = entt::null;
        const bool hasValidSelectedEntity = scene && selectedEntity != entt::null && scene->IsValid(selectedEntity);
        if (hasValidSelectedEntity)
        {
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
        }

        if (hasValidSelectedEntity)
        {
            auto& registry = scene->GetRegistry();
            PendingEntityComponentRemovals pendingRemovals{};
            bool removeNativeScriptComponent = false;
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

            DrawNativeScriptComponentSection(scene, registry, selectedEntity, undoService, removeNativeScriptComponent);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            DrawAddComponentPopup(scene, registry, selectedEntity, undoService);

            ApplyPendingEntityComponentRemovals(scene, registry, selectedEntity, pendingRemovals, removeNativeScriptComponent, undoService);
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
            DrawMaterialInspector(texturePayloadId, shaderPayloadId, selectedMaterialAssetKey, cachedMaterialAsset);
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

        ImGui::End();
        DrawNativeScriptEditorWindow();
    }
}
