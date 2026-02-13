#pragma once

#include "Assets/MaterialAsset.h"
#include "Assets/TextureAsset.h"
#include "Limitless.h"

#include <array>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Limitless
{
    /// Modal popup flow used by the Project panel for folder operations.
    enum class EditorProjectFolderPopup
    {
        None,
        Create,
        Rename
    };

    /// Mutable UI state required by the Project panel across frames.
    struct EditorProjectPanelState
    {
        EditorProjectFolderPopup FolderPopupPending = EditorProjectFolderPopup::None;
        std::filesystem::path FolderPopupParent;
        std::array<char, 256> FolderPopupBuffer{};
        bool CreateFolderPopupOpen = false;
        bool RenameFolderPopupOpen = false;

        // External OS drop import (Explorer/Finder -> Project panel).
        std::vector<std::filesystem::path> PendingExternalDropPaths;
        std::filesystem::path HoveredFolderRelativePathForExternalDrop;

        // Asset file rename popup state.
        bool RenameAssetPopupPending = false;
        bool RenameAssetPopupOpen = false;
        std::filesystem::path RenameAssetRelativePath;
        std::array<char, 256> RenameAssetBuffer{};
    };

    namespace EditorProjectPanel
    {
        /// Draws the full Project panel tree and folder popup workflow.
        void Draw(EditorProjectPanelState& state,
                  entt::entity& selectedEntity,
                  std::string& selectedTextureAssetKey,
                  Assets::TextureAsset::Ptr& cachedTextureAsset,
                  std::string& selectedMaterialAssetKey,
                  Assets::MaterialAsset::Ptr& cachedMaterialAsset,
                  const char* texturePayloadId,
                  const char* audioPayloadId,
                  const char* assetMovePayloadId,
                  const char* scenePayloadId,
                  const char* materialPayloadId,
                  const char* shaderPayloadId,
                  const char* fontPayloadId,
                  const std::function<void(const std::string&)>& onSceneActivated,
                  const std::function<void(const std::filesystem::path&)>& onCreateSceneRequested,
                  const std::function<void(const std::string&)>& onSetDefaultSceneRequested,
                  const std::function<void(const std::string&, const std::string&)>& onAssetRenamed);
    }
}
