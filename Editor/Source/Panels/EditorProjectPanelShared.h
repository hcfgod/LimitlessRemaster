#pragma once

#include "EditorProjectPanel.h"

#include "Assets/MaterialAsset.h"
#include "Assets/SpriteImportSettings.h"
#include "Assets/TextureAsset.h"
#include "Graphics/Texture.h"
#include "imgui/imgui.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

namespace Limitless::EditorAssetPreview
{
    struct MaterialPreviewCache;
}

namespace Limitless::EditorProjectPanel::Internal
{
    inline constexpr const char* kSceneEntityPayload = "SCENE_ENTITY";
    inline constexpr const char* kAssetMultiSelectionPayload = "ASSET_MULTI_KEYS";
    inline constexpr const char* kSubSpritePayloadId = "SUB_SPRITE_KEY";
    inline constexpr uint8_t kScriptPairHeaderBit = 1u << 0u;
    inline constexpr uint8_t kScriptPairSourceBit = 1u << 1u;
    inline constexpr std::chrono::milliseconds kDirectoryCacheRefreshInterval(250);
    inline constexpr float kCompactListScaleThreshold = 0.20f;
    inline constexpr std::chrono::milliseconds kSpriteSettingsCacheLifetime(2000);
    inline constexpr std::chrono::milliseconds kTextureThumbnailCacheLifetime(2000);
    inline constexpr std::chrono::milliseconds kPrefabThumbnailCacheLifetime(2000);
    inline constexpr uint32_t kPrefabThumbnailSnapshotSize = 256;

    struct ProjectAssetTreeEntry
    {
        std::filesystem::path AbsolutePath;
        std::filesystem::path RelativePath;
        std::string FileName;
        std::string LowerFileName;
        std::string LowerExtension;
        std::string AssetKey;
        bool IsDirectory = false;
    };

    struct ProjectAssetDirectoryCacheEntry
    {
        std::vector<ProjectAssetTreeEntry> Entries;
        std::chrono::steady_clock::time_point LastRefreshTime = {};
    };

    struct SpriteSettingsCacheEntry
    {
        Assets::SpriteImportSettings Settings;
        std::chrono::steady_clock::time_point LoadTime = {};
    };

    struct TextureThumbnailCacheEntry
    {
        Assets::TextureAsset::Ptr TextureAsset;
        std::chrono::steady_clock::time_point LoadTime = {};
    };

    struct PrefabThumbnailCacheEntry
    {
        std::shared_ptr<Texture2D> PreviewTexture;
        ImVec2 UvMin = ImVec2(0.0f, 1.0f);
        ImVec2 UvMax = ImVec2(1.0f, 0.0f);
        float SourceWidth = 1.0f;
        float SourceHeight = 1.0f;
        bool HasPreview = false;
        std::chrono::steady_clock::time_point LoadTime = {};
    };

    struct ProjectPanelCacheState
    {
        std::unordered_map<std::string, ProjectAssetDirectoryCacheEntry> ProjectAssetDirectoryCache;
        std::unordered_map<std::string, SpriteSettingsCacheEntry> SpriteSettingsCache;
        std::unordered_map<std::string, TextureThumbnailCacheEntry> TextureThumbnailCache;
        std::unordered_map<std::string, PrefabThumbnailCacheEntry> PrefabThumbnailCache;
    };

    struct AssetTypeBadgeInfo
    {
        const char* Label;
        ImU32 FillColor;
        ImU32 BorderColor;
        ImU32 TextColor;
    };

    inline constexpr AssetTypeBadgeInfo kBadgeFolder = { "FLD", IM_COL32(55, 120, 190, 255), IM_COL32(100, 170, 240, 255), IM_COL32(230, 245, 255, 255) };
    inline constexpr AssetTypeBadgeInfo kBadgeTexture = { "TEX", IM_COL32(45, 145, 70, 255), IM_COL32(90, 200, 120, 255), IM_COL32(230, 255, 235, 255) };
    inline constexpr AssetTypeBadgeInfo kBadgeScene = { "SCN", IM_COL32(185, 120, 30, 255), IM_COL32(235, 175, 65, 255), IM_COL32(255, 245, 225, 255) };
    inline constexpr AssetTypeBadgeInfo kBadgeMaterial = { "MAT", IM_COL32(120, 60, 175, 255), IM_COL32(170, 110, 225, 255), IM_COL32(240, 230, 255, 255) };
    inline constexpr AssetTypeBadgeInfo kBadgeAudio = { "SND", IM_COL32(170, 50, 120, 255), IM_COL32(220, 100, 170, 255), IM_COL32(255, 230, 245, 255) };
    inline constexpr AssetTypeBadgeInfo kBadgeFont = { "FNT", IM_COL32(40, 140, 150, 255), IM_COL32(80, 195, 200, 255), IM_COL32(225, 250, 252, 255) };
    inline constexpr AssetTypeBadgeInfo kBadgePrefab = { "PFB", IM_COL32(58, 125, 198, 255), IM_COL32(120, 190, 255, 255), IM_COL32(235, 245, 255, 255) };
    inline constexpr AssetTypeBadgeInfo kBadgeScript = { "CPP", IM_COL32(165, 145, 35, 255), IM_COL32(215, 200, 80, 255), IM_COL32(255, 252, 225, 255) };
    inline constexpr AssetTypeBadgeInfo kBadgeManagedScript = { "C#", IM_COL32(80, 120, 60, 255), IM_COL32(125, 180, 105, 255), IM_COL32(235, 255, 230, 255) };
    inline constexpr AssetTypeBadgeInfo kBadgeShader = { "SHD", IM_COL32(30, 150, 180, 255), IM_COL32(70, 200, 230, 255), IM_COL32(225, 250, 255, 255) };
    inline constexpr AssetTypeBadgeInfo kBadgeAudioMixer = { "MIX", IM_COL32(155, 55, 140, 255), IM_COL32(210, 110, 195, 255), IM_COL32(255, 230, 250, 255) };
    inline constexpr AssetTypeBadgeInfo kBadgeInputActions = { "INP", IM_COL32(70, 155, 50, 255), IM_COL32(120, 210, 100, 255), IM_COL32(235, 255, 230, 255) };
    inline constexpr AssetTypeBadgeInfo kBadgeAnimationClip = { "ANI", IM_COL32(190, 65, 55, 255), IM_COL32(240, 115, 105, 255), IM_COL32(255, 232, 230, 255) };
    inline constexpr AssetTypeBadgeInfo kBadgeAnimController = { "ACT", IM_COL32(175, 45, 70, 255), IM_COL32(230, 95, 120, 255), IM_COL32(255, 230, 235, 255) };
    inline constexpr AssetTypeBadgeInfo kBadgeSubSprite = { "SUB", IM_COL32(80, 130, 80, 255), IM_COL32(130, 185, 130, 255), IM_COL32(235, 255, 235, 255) };
    inline constexpr AssetTypeBadgeInfo kBadgeTileset = { "TLS", IM_COL32(140, 110, 50, 255), IM_COL32(195, 165, 90, 255), IM_COL32(255, 248, 230, 255) };
    inline constexpr AssetTypeBadgeInfo kBadgeTilePalette = { "PAL", IM_COL32(110, 95, 55, 255), IM_COL32(180, 155, 95, 255), IM_COL32(255, 246, 225, 255) };
    inline constexpr AssetTypeBadgeInfo kBadgeUnknown = { "---", IM_COL32(90, 90, 100, 255), IM_COL32(140, 140, 155, 255), IM_COL32(220, 220, 230, 255) };

    struct ProjectGridEntry
    {
        ProjectAssetTreeEntry Entry;
        std::string DisplayName;
        std::string PrimaryAssetKey;
        std::string SecondaryAssetKey;
        const AssetTypeBadgeInfo* Badge = &kBadgeUnknown;
        bool IsDirectory = false;
        bool IsTexture = false;
        bool IsScene = false;
        bool IsMaterial = false;
        bool IsTileset = false;
        bool IsTilePalette = false;
        bool IsAudioMixer = false;
        bool IsInputActions = false;
        bool IsAnimationClip = false;
        bool IsAnimatorController = false;
        bool IsPrefab = false;
        bool IsShader = false;
        bool IsAudio = false;
        bool IsFont = false;
        bool IsNativeScriptFile = false;
        bool IsManagedScriptFile = false;
        bool IsScriptPair = false;
        bool HasPairedScriptFile = false;
        bool IsSubSprite = false;
        bool HasExpandableSubSprites = false;
        bool HasThumbnailSubRect = false;
        glm::ivec4 ThumbnailRectPixels = glm::ivec4(0);
        ImVec2 ThumbnailUvMin = ImVec2(0.0f, 1.0f);
        ImVec2 ThumbnailUvMax = ImVec2(1.0f, 0.0f);
    };

    struct ProjectPanelSelectionRefs
    {
        entt::entity& SelectedEntity;
        std::string& SelectedTextureAssetKey;
        Assets::TextureAsset::Ptr& CachedTextureAsset;
        std::string& SelectedMaterialAssetKey;
        Assets::MaterialAsset::Ptr& CachedMaterialAsset;
        std::string& SelectedNativeScriptAssetKey;
        std::string& SelectedPrefabAssetKey;
        std::string& SelectedTilesetAssetKey;
        std::string& SelectedAudioMixerAssetKey;
        std::string& SelectedInputActionsAssetKey;
        std::string& SelectedAnimationClipAssetKey;
        std::string& SelectedAnimatorControllerAssetKey;
    };

    struct ProjectPanelCallbacks
    {
        const char* TexturePayloadId;
        const char* AudioPayloadId;
        const char* AssetMovePayloadId;
        const char* ScenePayloadId;
        const char* MaterialPayloadId;
        const char* PrefabPayloadId;
        const char* ShaderPayloadId;
        const char* FontPayloadId;
        const std::function<void(const std::string&)>& OnSceneActivated;
        const std::function<void(const std::filesystem::path&)>& OnCreateSceneRequested;
        const std::function<void(const std::filesystem::path&, const std::string&)>& OnCreateMaterialRequested;
        const std::function<void(const std::filesystem::path&, const std::string&)>& OnCreateTilesetRequested;
        const std::function<void(const std::filesystem::path&, const std::string&)>& OnCreateAudioMixerRequested;
        const std::function<void(const std::filesystem::path&, const std::string&)>& OnCreateInputActionsRequested;
        const std::function<void(const std::filesystem::path&, const std::string&)>& OnCreateAnimationClipRequested;
        const std::function<void(const std::filesystem::path&, const std::string&)>& OnCreateAnimatorControllerRequested;
        const std::function<void(entt::entity, const std::filesystem::path&)>& OnCreatePrefabFromSceneEntityRequested;
        const std::function<void(const std::string&)>& OnPrefabOpened;
        const std::function<void(const std::string&)>& OnPrefabInstantiated;
        const std::function<void(const std::string&)>& OnSetDefaultSceneRequested;
        const std::function<void(const std::string&, const std::string&)>& OnAssetRenamed;
        const std::function<bool(const std::vector<std::string>&)>& OnDeleteSceneAssetsRequested;
        const std::function<void(const std::string&)>& OnNativeScriptAssetActivated;
    };

    std::vector<std::string> ParseAssetKeyListPayload(const void* payloadData, int payloadSize);
    std::string EncodeAssetKeyListPayload(const std::vector<std::string>& keys);

    void DrawAssetTypeBadge(const AssetTypeBadgeInfo& badge, float indentScreenX);
    std::string BadgePadLabel(const std::string& label);
    const AssetTypeBadgeInfo& ResolveAssetBadge(bool isTexture,
                                                bool isScene,
                                                bool isMaterial,
                                                bool isAudioMixer,
                                                bool isInputActions,
                                                bool isAnimationClip,
                                                bool isAnimatorController,
                                                bool isPrefab,
                                                bool isShader,
                                                bool isAudio,
                                                bool isFont,
                                                bool isNativeScriptFile,
                                                bool isManagedScriptFile);

    std::string ToLowerAscii(std::string value);
    bool IsTextureExtensionLower(const std::string& lowerExtension);
    bool IsSceneFileNameLower(const std::string& lowerFileName);
    bool IsMaterialFileNameLower(const std::string& lowerFileName);
    bool IsAudioMixerFileNameLower(const std::string& lowerFileName);
    bool IsInputActionsFileNameLower(const std::string& lowerFileName);
    bool IsPrefabFileNameLower(const std::string& lowerFileName);
    bool IsAnimationClipFileNameLower(const std::string& lowerFileName);
    bool IsAnimatorControllerFileNameLower(const std::string& lowerFileName);
    bool IsTilesetFileNameLower(const std::string& lowerFileName);
    bool IsTilePaletteFileNameLower(const std::string& lowerFileName);
    bool IsShaderExtensionLower(const std::string& lowerExtension);
    bool IsAudioExtensionLower(const std::string& lowerExtension);
    bool IsFontExtensionLower(const std::string& lowerExtension);
    bool IsNativeScriptExtensionLower(const std::string& lowerExtension);
    bool IsManagedScriptExtensionLower(const std::string& lowerExtension);
    bool IsScriptAssetExtensionLower(const std::string& lowerExtension);

    const Assets::SpriteImportSettings& GetCachedSpriteImportSettings(EditorProjectPanelState& state, const std::string& textureAssetKey);
    Assets::TextureAsset::Ptr GetCachedThumbnailTextureAsset(EditorProjectPanelState& state, const std::string& textureAssetKey);
    const PrefabThumbnailCacheEntry* GetCachedPrefabThumbnail(EditorProjectPanelState& state, const std::string& prefabAssetKey);
    const std::vector<ProjectAssetTreeEntry>& GetCachedProjectDirectoryEntries(EditorProjectPanelState& state,
                                                                                const std::filesystem::path& assetsDirectory,
                                                                                const std::filesystem::path& relativePath);
    std::string BuildProjectScriptAssetDisplayName(const std::filesystem::path& path);
    std::string GetAssetDisplayName(const std::filesystem::path& path);
    void DrawProjectGridEntryPreview(EditorProjectPanelState& state,
                                     EditorAssetPreview::MaterialPreviewCache& materialPreviewCache,
                                     const ProjectGridEntry& entry,
                                     const ImVec2& previewMin,
                                     const ImVec2& previewMax);

    void SetProjectSearchFilter(EditorProjectPanelState& state, std::string value);
    const std::string& GetProjectSearchFilterLower(const EditorProjectPanelState& state);
    bool HasProjectSearchFilter(const EditorProjectPanelState& state);
    void ClearProjectSearchMatchCache(EditorProjectPanelState& state);
    bool MatchesProjectSearchFilter(const EditorProjectPanelState& state, const std::string& value);
    bool EntryMatchesProjectSearchFilter(const EditorProjectPanelState& state, const ProjectAssetTreeEntry& entry, const std::string& displayName);
    bool DirectoryContainsProjectSearchMatch(EditorProjectPanelState& state,
                                             const std::filesystem::path& assetsDirectory,
                                             const std::filesystem::path& relativePath);

    void MoveAssetOrFolderToTargetFolder(EditorProjectPanelState& state,
                                         const char* assetOrFolderKey,
                                         const std::filesystem::path& destinationFolderRelativePath);
    void MoveAssetListToTargetFolder(EditorProjectPanelState& state,
                                     const std::vector<std::string>& assetKeys,
                                     const std::filesystem::path& destinationFolderRelativePath);
    bool DeleteAssetKeysWithSceneHandling(EditorProjectPanelState& state,
                                          const std::filesystem::path& assetsDirectory,
                                          const std::vector<std::string>& assetKeys,
                                          const std::function<bool(const std::vector<std::string>&)>& onDeleteSceneAssetsRequested);
    void ClearProjectAssetSelection(EditorProjectPanelState& state, ProjectPanelSelectionRefs& selection);
    void ClearPrimaryAssetSelection(ProjectPanelSelectionRefs& selection);
    bool IsGridEntryPrimarySelected(const ProjectGridEntry& entry, const ProjectPanelSelectionRefs& selection);
    void SetPrimarySelectionForGridEntry(const ProjectGridEntry& entry, ProjectPanelSelectionRefs& selection);
    void HandleProjectGridItemSelection(EditorProjectPanelState& state,
                                        ProjectPanelSelectionRefs& selection,
                                        const std::vector<std::string>& visibleAssetKeys,
                                        const ProjectGridEntry& entry,
                                        bool releasedOnItemWithoutDrag);
    void HandleProjectGridItemActivation(EditorProjectPanelState& state,
                                         ProjectPanelSelectionRefs& selection,
                                         const ProjectGridEntry& entry,
                                         bool hovered,
                                         const ProjectPanelCallbacks& callbacks);
    void DrawProjectGridItemContextMenu(const std::filesystem::path& assetsDirectory,
                                        EditorProjectPanelState& state,
                                        ProjectPanelSelectionRefs& selection,
                                        const ProjectGridEntry& entry,
                                        const ProjectPanelCallbacks& callbacks);
    void DrawProjectGridItemDragDropSource(EditorProjectPanelState& state,
                                           const ProjectGridEntry& entry,
                                           const ProjectPanelCallbacks& callbacks);

    void DrawCreateMenuItems(const std::filesystem::path& parentRelativePath,
                             EditorProjectPanelState& state,
                             const ProjectPanelCallbacks& callbacks);
    void SetActiveFolder(EditorProjectPanelState& state, std::filesystem::path relativePath);
    void DrawProjectBrowserRegion(const std::filesystem::path& assetsDirectory,
                                  EditorProjectPanelState& state,
                                  EditorAssetPreview::MaterialPreviewCache& materialPreviewCache,
                                  ProjectPanelSelectionRefs& selection,
                                  const ProjectPanelCallbacks& callbacks);
}
