#pragma once

#include "Assets/MaterialAsset.h"
#include "Assets/TextureAsset.h"
#include "Limitless.h"

#include <array>
#include <string>

namespace Limitless
{
    class Scene;

    struct EditorScenePanelState
    {
        entt::entity PendingDeleteEntity = entt::null;
        entt::entity RenameEntity = entt::null;
        bool RenamePopupOpen = false;
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
                  const char* materialPayloadId,
                  const std::string& sceneRootDisplayName);
    }
}
