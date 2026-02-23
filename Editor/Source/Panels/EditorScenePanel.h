#pragma once

#include "Assets/MaterialAsset.h"
#include "Assets/TextureAsset.h"
#include "Limitless.h"

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Limitless
{
    class Scene;
    class EditorUndoService;

    struct EditorScenePanelState
    {
        std::vector<entt::entity> PendingDeleteEntities;
        entt::entity RenameEntity = entt::null;
        entt::entity PendingClickSelectionEntity = entt::null;
        bool PendingClickCtrlModifier = false;
        bool PendingClickShiftModifier = false;
        entt::entity SelectionAnchorEntity = entt::null;
        std::vector<entt::entity> MultiSelectedEntities;
        std::vector<entt::entity> DrawOrderEntities;
        bool RenamePopupOpen = false;
        std::unique_ptr<Scene> EntityClipboardScene;
        std::vector<std::unique_ptr<Scene>> EntityClipboardScenes;
        std::array<char, 256> RenameBuffer{};
    };

    namespace EditorScenePanel
    {
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
                  const std::function<bool(entt::entity)>& onUnpackPrefabEntity);
    }
}
