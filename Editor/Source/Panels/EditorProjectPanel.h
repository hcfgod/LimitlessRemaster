#pragma once

#include "Assets/MaterialAsset.h"
#include "Assets/TextureAsset.h"
#include "Limitless.h"

#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Limitless::EditorAssetPreview
{
    struct MaterialPreviewCache;
}

namespace Limitless::EditorProjectPanel::Internal
{
    struct ProjectPanelCacheState;
}

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
        // External OS drop import (Explorer/Finder -> Project panel).
        std::vector<std::filesystem::path> PendingExternalDropPaths;
        std::filesystem::path PendingExternalDropTargetFolderRelativePath;
        // Multi-select state for Project assets (Ctrl/Shift click + multi-drag).
        std::vector<std::string> MultiSelectedAssetKeys;
        // Multi-select state for texture sub-sprites (virtual keys: "Texture.png#idx").
        std::vector<std::string> MultiSelectedSubSpriteKeys;
        bool RequestFocusAnimationClipEditor = false;
        bool RequestFocusAnimatorControllerEditor = false;
        bool IsProjectPanelHoveredForExternalDrop = false;
        float ExternalDropPanelMinX = 0.0f;
        float ExternalDropPanelMinY = 0.0f;
        float ExternalDropPanelMaxX = 0.0f;
        float ExternalDropPanelMaxY = 0.0f;

        std::string SelectionAnchorAssetKey;
        std::string SubSpriteSelectionAnchorKey;

        std::filesystem::path ActiveFolderRelativePath;
        std::filesystem::path FolderPopupParent;
        std::filesystem::path HoveredFolderRelativePathForExternalDrop;
        std::filesystem::path RenameAssetRelativePath;
        std::filesystem::path CreateNativeScriptParentRelativePath;
        std::filesystem::path CreateManagedScriptParentRelativePath;
        std::filesystem::path CreateMaterialParentRelativePath;
        std::filesystem::path CreateTilesetParentRelativePath;
        std::filesystem::path CreateAudioMixerParentRelativePath;
        std::filesystem::path CreateInputActionsParentRelativePath;
        std::filesystem::path CreateAnimationClipParentRelativePath;
        std::filesystem::path CreateAnimatorControllerParentRelativePath;
        std::filesystem::path CreateTilePaletteParentRelativePath;

        // Persisted expansion state for the Project tree.
        std::unordered_map<std::string, bool> ExpandedFolderState;
        std::unordered_set<std::string> ExpandedSubSpriteTextureKeys;
        std::unordered_map<std::string, bool> SearchMatchCache;
        std::shared_ptr<EditorProjectPanel::Internal::ProjectPanelCacheState> CacheState;
        EditorProjectFolderPopup FolderPopupPending = EditorProjectFolderPopup::None;

        bool CreateFolderPopupOpen = false;
        bool RenameFolderPopupOpen = false;
        bool RenameAssetPopupPending = false;
        bool RenameAssetPopupOpen = false;
        bool RenameAssetAsNativeScriptPair = false;
        bool CreateNativeScriptPopupPending = false;
        bool CreateNativeScriptPopupOpen = false;
        bool CreateManagedScriptPopupPending = false;
        bool CreateManagedScriptPopupOpen = false;
        bool CreateMaterialPopupPending = false;
        bool CreateMaterialPopupOpen = false;
        bool CreateTilesetPopupPending = false;
        bool CreateTilesetPopupOpen = false;
        bool CreateAudioMixerPopupPending = false;
        bool CreateAudioMixerPopupOpen = false;
        bool CreateInputActionsPopupPending = false;
        bool CreateInputActionsPopupOpen = false;
        bool CreateAnimationClipPopupPending = false;
        bool CreateAnimationClipPopupOpen = false;
        bool CreateAnimatorControllerPopupPending = false;
        bool CreateAnimatorControllerPopupOpen = false;
        bool CreateTilePalettePopupPending = false;
        bool CreateTilePalettePopupOpen = false;
        bool IsLocked = false;
        bool AssetsRootExpanded = true;
        float GridScale = 1.0f;
        bool BrowseLocationChanged = false;
        bool GridScaleChanged = false;
        bool TreeExpansionStateChanged = false;

        std::array<char, 256> FolderPopupBuffer{};
        std::array<char, 256> RenameAssetBuffer{};
        std::array<char, 256> SearchBuffer{};
        std::string SearchFilterLower;
        std::array<char, 256> CreateNativeScriptClassNameBuffer{};
        std::array<char, 256> CreateManagedScriptClassNameBuffer{};
        std::array<char, 256> CreateMaterialNameBuffer{};
        std::array<char, 256> CreateTilesetNameBuffer{};
        std::array<char, 256> CreateAudioMixerNameBuffer{};
        std::array<char, 256> CreateInputActionsNameBuffer{};
        std::array<char, 256> CreateAnimationClipNameBuffer{};
        std::array<char, 256> CreateAnimatorControllerNameBuffer{};
        std::array<char, 256> CreateTilePaletteNameBuffer{};
    };

    namespace EditorProjectPanel
    {
        void InvalidateProjectDirectoryCache(EditorProjectPanelState& state);

        /// Draws the full Project panel tree and folder popup workflow.
        void Draw(const char* windowName,
                  bool& isOpen,
                  EditorProjectPanelState& state,
                  EditorAssetPreview::MaterialPreviewCache& materialPreviewCache,
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
                  std::string& selectedAnimationClipAssetKey,
                  std::string& selectedAnimatorControllerAssetKey,
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
                  const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateAudioMixerRequested,
                  const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateInputActionsRequested,
                  const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateAnimationClipRequested,
                  const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateAnimatorControllerRequested,
                  const std::function<void(entt::entity, const std::filesystem::path&)>& onCreatePrefabFromSceneEntityRequested,
                  const std::function<void(const std::string&)>& onPrefabOpened,
                  const std::function<void(const std::string&)>& onPrefabInstantiated,
                  const std::function<void(const std::string&)>& onSetDefaultSceneRequested,
                  const std::function<void(const std::string&, const std::string&)>& onAssetRenamed,
                  const std::function<bool(const std::vector<std::string>&)>& onDeleteSceneAssetsRequested,
                  const std::function<void(const std::string&)>& onNativeScriptAssetActivated);
    }
}
