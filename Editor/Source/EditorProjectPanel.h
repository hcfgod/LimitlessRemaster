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
        bool RenameAssetAsNativeScriptPair = false;

        // Native script creation popup state.
        bool CreateNativeScriptPopupPending = false;
        bool CreateNativeScriptPopupOpen = false;
        std::filesystem::path CreateNativeScriptParentRelativePath;
        std::array<char, 256> CreateNativeScriptClassNameBuffer{};

        // Material asset creation popup state.
        bool CreateMaterialPopupPending = false;
        bool CreateMaterialPopupOpen = false;
        std::filesystem::path CreateMaterialParentRelativePath;
        std::array<char, 256> CreateMaterialNameBuffer{};

        // Tileset asset creation popup state.
        bool CreateTilesetPopupPending = false;
        bool CreateTilesetPopupOpen = false;
        std::filesystem::path CreateTilesetParentRelativePath;
        std::array<char, 256> CreateTilesetNameBuffer{};
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
                  std::string& selectedNativeScriptAssetKey,
                  std::string& selectedPrefabAssetKey,
                  std::string& selectedTilesetAssetKey,
                  std::string& selectedInputActionsAssetKey,
                  const char* texturePayloadId,
                  const char* audioPayloadId,
                  const char* assetMovePayloadId,
                  const char* scenePayloadId,
                  const char* materialPayloadId,
                  const char* prefabPayloadId,
                  const char* shaderPayloadId,
                  const char* fontPayloadId,
                  const std::function<void(const std::string&)>& onSceneActivated,
                  const std::function<void(const std::filesystem::path&)>& onCreateSceneRequested,
                  const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateMaterialRequested,
                  const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateTilesetRequested,
                  const std::function<void(entt::entity, const std::filesystem::path&)>& onCreatePrefabFromSceneEntityRequested,
                  const std::function<void(const std::string&)>& onPrefabOpened,
                  const std::function<void(const std::string&)>& onPrefabInstantiated,
                  const std::function<void(const std::string&)>& onSetDefaultSceneRequested,
                  const std::function<void(const std::string&, const std::string&)>& onAssetRenamed,
                  const std::function<void(const std::string&)>& onNativeScriptAssetActivated);
    }
}
