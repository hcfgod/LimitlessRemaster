#include "EditorProjectPanelInternal.h"
#include "EditorProjectPanelShared.h"

#include "Assets/AssetImportPipeline.h"
#include "Assets/AssetPaths.h"
#include "ProjectAssetOperations.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <unordered_set>

namespace Limitless::EditorProjectPanel::Internal
{
    namespace
    {
        bool DeleteAssetKeysInAssets(EditorProjectPanelState& state,
                                     const std::filesystem::path& assetsDirectory,
                                     const std::vector<std::string>& assetKeys)
        {
            bool removedAny = false;
            bool removedRegularAsset = false;
            std::unordered_set<std::string> deduplicatedKeys;
            deduplicatedKeys.reserve(assetKeys.size());

            for (const std::string& assetKey : assetKeys)
            {
                if (assetKey.empty() || !deduplicatedKeys.insert(assetKey).second)
                    continue;

                std::string textureKey;
                int32_t subSpriteIndex = -1;
                if (Assets::TryParseSubSpriteAssetKey(assetKey, textureKey, subSpriteIndex))
                    continue;

                if (assetKey.rfind("Assets/", 0) != 0)
                    continue;

                const std::filesystem::path relativePath = std::filesystem::path(assetKey.substr(7));
                const std::filesystem::path absolutePath = assetsDirectory / relativePath;
                const std::string lowerExtension = ToLowerAscii(absolutePath.extension().string());

                if (IsNativeScriptExtensionLower(lowerExtension))
                {
                    removedAny |= DeleteNativeScriptPairInAssets(state, assetsDirectory, relativePath);
                    continue;
                }

                std::error_code deleteErrorCode;
                const bool removed = std::filesystem::remove(absolutePath, deleteErrorCode);
                if (!removed || deleteErrorCode)
                    continue;

                removedAny = true;
                removedRegularAsset = true;
                const std::filesystem::path metaPath = absolutePath.parent_path() / (absolutePath.filename().string() + ".meta");
                std::filesystem::remove(metaPath, deleteErrorCode);
            }

            if (removedRegularAsset)
            {
                (void)Assets::AssetImportPipeline::ReimportChanged(true);
                InvalidateProjectDirectoryCache(state);
            }

            return removedAny;
        }

        bool IsSceneAssetKey(const std::string& assetKey)
        {
            if (assetKey.rfind("Assets/", 0) != 0)
                return false;
            const std::string lowerKey = ToLowerAscii(assetKey);
            return lowerKey.ends_with(".scene.json");
        }
    }

    void MoveAssetOrFolderToTargetFolder(EditorProjectPanelState& state,
                                         const char* assetOrFolderKey,
                                         const std::filesystem::path& destinationFolderRelativePath)
    {
        if (!assetOrFolderKey || !assetOrFolderKey[0])
            return;

        std::string textureKey;
        int32_t subSpriteIndex = -1;
        if (Assets::TryParseSubSpriteAssetKey(assetOrFolderKey, textureKey, subSpriteIndex))
            return;

        const bool moved = std::filesystem::path(assetOrFolderKey).extension().empty()
            ? ProjectAssetOperations::MoveFolderToFolder(assetOrFolderKey, destinationFolderRelativePath)
            : ProjectAssetOperations::MoveAssetToFolder(assetOrFolderKey, destinationFolderRelativePath);
        if (moved)
            InvalidateProjectDirectoryCache(state);
    }

    void MoveAssetListToTargetFolder(EditorProjectPanelState& state,
                                     const std::vector<std::string>& assetKeys,
                                     const std::filesystem::path& destinationFolderRelativePath)
    {
        bool movedAny = false;
        for (const auto& key : assetKeys)
        {
            if (key.empty())
                continue;

            std::string textureKey;
            int32_t subSpriteIndex = -1;
            if (Assets::TryParseSubSpriteAssetKey(key, textureKey, subSpriteIndex))
                continue;

            const bool moved = std::filesystem::path(key).extension().empty()
                ? ProjectAssetOperations::MoveFolderToFolder(key.c_str(), destinationFolderRelativePath)
                : ProjectAssetOperations::MoveAssetToFolder(key.c_str(), destinationFolderRelativePath);
            movedAny |= moved;
        }

        if (movedAny)
            InvalidateProjectDirectoryCache(state);
    }

    bool DeleteAssetKeysWithSceneHandling(EditorProjectPanelState& state,
                                          const std::filesystem::path& assetsDirectory,
                                          const std::vector<std::string>& assetKeys,
                                          const std::function<bool(const std::vector<std::string>&)>& onDeleteSceneAssetsRequested)
    {
        if (assetKeys.empty())
            return false;

        std::vector<std::string> deduplicatedSceneKeys;
        std::vector<std::string> deduplicatedNonSceneKeys;
        std::unordered_set<std::string> deduplicatedKeys;
        deduplicatedKeys.reserve(assetKeys.size());

        for (const std::string& assetKey : assetKeys)
        {
            if (assetKey.empty() || !deduplicatedKeys.insert(assetKey).second)
                continue;

            std::string textureKey;
            int32_t subSpriteIndex = -1;
            if (Assets::TryParseSubSpriteAssetKey(assetKey, textureKey, subSpriteIndex))
                continue;

            if (IsSceneAssetKey(assetKey))
                deduplicatedSceneKeys.push_back(assetKey);
            else
                deduplicatedNonSceneKeys.push_back(assetKey);
        }

        bool removedAny = false;
        if (!deduplicatedSceneKeys.empty())
        {
            if (onDeleteSceneAssetsRequested)
                removedAny |= onDeleteSceneAssetsRequested(deduplicatedSceneKeys);
            else
                removedAny |= DeleteAssetKeysInAssets(state, assetsDirectory, deduplicatedSceneKeys);
        }

        if (!deduplicatedNonSceneKeys.empty())
            removedAny |= DeleteAssetKeysInAssets(state, assetsDirectory, deduplicatedNonSceneKeys);

        if (removedAny)
            InvalidateProjectDirectoryCache(state);

        return removedAny;
    }

    void ClearProjectAssetSelection(EditorProjectPanelState& state, ProjectPanelSelectionRefs& selection)
    {
        selection.SelectedTextureAssetKey.clear();
        selection.SelectedMaterialAssetKey.clear();
        selection.SelectedNativeScriptAssetKey.clear();
        selection.SelectedPrefabAssetKey.clear();
        selection.SelectedTilesetAssetKey.clear();
        selection.SelectedAudioMixerAssetKey.clear();
        selection.SelectedInputActionsAssetKey.clear();
        selection.SelectedAnimationClipAssetKey.clear();
        selection.SelectedAnimatorControllerAssetKey.clear();
        selection.SelectedEntity = entt::null;
        selection.CachedTextureAsset.reset();
        selection.CachedMaterialAsset.reset();
        state.MultiSelectedAssetKeys.clear();
        state.SelectionAnchorAssetKey.clear();
        state.MultiSelectedSubSpriteKeys.clear();
        state.SubSpriteSelectionAnchorKey.clear();
    }

    void ClearPrimaryAssetSelection(ProjectPanelSelectionRefs& selection)
    {
        selection.SelectedTextureAssetKey.clear();
        selection.SelectedMaterialAssetKey.clear();
        selection.SelectedNativeScriptAssetKey.clear();
        selection.SelectedPrefabAssetKey.clear();
        selection.SelectedTilesetAssetKey.clear();
        selection.SelectedAudioMixerAssetKey.clear();
        selection.SelectedInputActionsAssetKey.clear();
        selection.SelectedAnimationClipAssetKey.clear();
        selection.SelectedAnimatorControllerAssetKey.clear();
        selection.SelectedEntity = entt::null;
        selection.CachedTextureAsset.reset();
        selection.CachedMaterialAsset.reset();
    }

    bool IsGridEntryPrimarySelected(const ProjectGridEntry& entry, const ProjectPanelSelectionRefs& selection)
    {
        std::string selectedTextureParentKey;
        int32_t selectedTextureSubIndex = -1;
        const bool selectedTextureIsSubSprite =
            Assets::TryParseSubSpriteAssetKey(selection.SelectedTextureAssetKey, selectedTextureParentKey, selectedTextureSubIndex);
        std::string entryTextureParentKey;
        int32_t entrySubSpriteIndex = -1;
        const bool entryIsSubSprite =
            Assets::TryParseSubSpriteAssetKey(entry.PrimaryAssetKey, entryTextureParentKey, entrySubSpriteIndex);
        (void)selectedTextureSubIndex;
        (void)entryTextureParentKey;
        (void)entrySubSpriteIndex;

        if (entry.IsTexture)
        {
            if (entryIsSubSprite)
                return selection.SelectedTextureAssetKey == entry.PrimaryAssetKey;
            return !selectedTextureIsSubSprite && selection.SelectedTextureAssetKey == entry.PrimaryAssetKey;
        }
        if (entry.IsMaterial)
            return selection.SelectedMaterialAssetKey == entry.PrimaryAssetKey;
        if (entry.IsTileset)
            return selection.SelectedTilesetAssetKey == entry.PrimaryAssetKey;
        if (entry.IsAudioMixer)
            return selection.SelectedAudioMixerAssetKey == entry.PrimaryAssetKey;
        if (entry.IsInputActions)
            return selection.SelectedInputActionsAssetKey == entry.PrimaryAssetKey;
        if (entry.IsAnimationClip)
            return selection.SelectedAnimationClipAssetKey == entry.PrimaryAssetKey;
        if (entry.IsAnimatorController)
            return selection.SelectedAnimatorControllerAssetKey == entry.PrimaryAssetKey;
        if (entry.IsNativeScriptFile || entry.IsManagedScriptFile)
            return selection.SelectedNativeScriptAssetKey == entry.PrimaryAssetKey || (!entry.SecondaryAssetKey.empty() && selection.SelectedNativeScriptAssetKey == entry.SecondaryAssetKey);
        if (entry.IsPrefab)
            return selection.SelectedPrefabAssetKey == entry.PrimaryAssetKey;
        return false;
    }

    void SetPrimarySelectionForGridEntry(const ProjectGridEntry& entry, ProjectPanelSelectionRefs& selection)
    {
        ClearPrimaryAssetSelection(selection);
        if (entry.IsTexture)
            selection.SelectedTextureAssetKey = entry.PrimaryAssetKey;
        else if (entry.IsMaterial)
            selection.SelectedMaterialAssetKey = entry.PrimaryAssetKey;
        else if (entry.IsNativeScriptFile || entry.IsManagedScriptFile)
            selection.SelectedNativeScriptAssetKey = entry.PrimaryAssetKey;
        else if (entry.IsPrefab)
            selection.SelectedPrefabAssetKey = entry.PrimaryAssetKey;
        else if (entry.IsTileset)
            selection.SelectedTilesetAssetKey = entry.PrimaryAssetKey;
        else if (entry.IsAudioMixer)
            selection.SelectedAudioMixerAssetKey = entry.PrimaryAssetKey;
        else if (entry.IsInputActions)
            selection.SelectedInputActionsAssetKey = entry.PrimaryAssetKey;
        else if (entry.IsAnimationClip)
            selection.SelectedAnimationClipAssetKey = entry.PrimaryAssetKey;
        else if (entry.IsAnimatorController)
            selection.SelectedAnimatorControllerAssetKey = entry.PrimaryAssetKey;
    }

    void HandleProjectGridItemSelection(EditorProjectPanelState& state,
                                        ProjectPanelSelectionRefs& selection,
                                        const std::vector<std::string>& visibleAssetKeys,
                                        const ProjectGridEntry& entry,
                                        bool releasedOnItemWithoutDrag)
    {
        if (!releasedOnItemWithoutDrag || entry.IsDirectory)
            return;

        const ImGuiIO& io = ImGui::GetIO();
        const bool shiftPressed = io.KeyShift;
        const bool controlPressed = io.KeyCtrl;
        state.MultiSelectedSubSpriteKeys.clear();
        state.SubSpriteSelectionAnchorKey.clear();

        if (shiftPressed)
        {
            const auto findIndex = [&](const std::string& key) -> int32_t {
                for (size_t selectionIndex = 0; selectionIndex < visibleAssetKeys.size(); ++selectionIndex)
                {
                    if (visibleAssetKeys[selectionIndex] == key)
                        return static_cast<int32_t>(selectionIndex);
                }
                return -1;
            };

            const int32_t anchorIndex = findIndex(state.SelectionAnchorAssetKey);
            const int32_t clickedIndex = findIndex(entry.PrimaryAssetKey);
            if (!controlPressed)
                state.MultiSelectedAssetKeys.clear();

            if (anchorIndex >= 0 && clickedIndex >= 0)
            {
                const int32_t minIndex = std::min(anchorIndex, clickedIndex);
                const int32_t maxIndex = std::max(anchorIndex, clickedIndex);
                for (int32_t rangeIndex = minIndex; rangeIndex <= maxIndex; ++rangeIndex)
                {
                    const std::string& rangeKey = visibleAssetKeys[static_cast<size_t>(rangeIndex)];
                    if (std::find(state.MultiSelectedAssetKeys.begin(), state.MultiSelectedAssetKeys.end(), rangeKey) == state.MultiSelectedAssetKeys.end())
                        state.MultiSelectedAssetKeys.push_back(rangeKey);
                }
            }
            else if (std::find(state.MultiSelectedAssetKeys.begin(), state.MultiSelectedAssetKeys.end(), entry.PrimaryAssetKey) == state.MultiSelectedAssetKeys.end())
            {
                state.MultiSelectedAssetKeys.push_back(entry.PrimaryAssetKey);
            }

            SetPrimarySelectionForGridEntry(entry, selection);
        }
        else if (controlPressed)
        {
            const auto foundIt = std::find(state.MultiSelectedAssetKeys.begin(), state.MultiSelectedAssetKeys.end(), entry.PrimaryAssetKey);
            const bool wasSelected = foundIt != state.MultiSelectedAssetKeys.end();
            if (wasSelected)
            {
                state.MultiSelectedAssetKeys.erase(foundIt);
                if (state.MultiSelectedAssetKeys.empty())
                    ClearPrimaryAssetSelection(selection);
            }
            else
            {
                state.MultiSelectedAssetKeys.push_back(entry.PrimaryAssetKey);
                SetPrimarySelectionForGridEntry(entry, selection);
            }
            state.SelectionAnchorAssetKey = entry.PrimaryAssetKey;
        }
        else
        {
            state.MultiSelectedAssetKeys.clear();
            state.MultiSelectedAssetKeys.push_back(entry.PrimaryAssetKey);
            state.SelectionAnchorAssetKey = entry.PrimaryAssetKey;
            SetPrimarySelectionForGridEntry(entry, selection);
        }
    }

    void HandleProjectGridItemActivation(EditorProjectPanelState& state,
                                         ProjectPanelSelectionRefs& selection,
                                         const ProjectGridEntry& entry,
                                         bool hovered,
                                         const ProjectPanelCallbacks& callbacks)
    {
        if (entry.IsDirectory || !hovered || !ImGui::IsMouseDoubleClicked(0))
            return;

        state.MultiSelectedAssetKeys = { entry.PrimaryAssetKey };
        state.SelectionAnchorAssetKey = entry.PrimaryAssetKey;
        ClearPrimaryAssetSelection(selection);

        if (entry.IsTexture)
        {
            selection.SelectedTextureAssetKey = entry.PrimaryAssetKey;
        }
        else if (entry.IsScene && callbacks.OnSceneActivated)
        {
            selection.SelectedNativeScriptAssetKey.clear();
            callbacks.OnSceneActivated(entry.PrimaryAssetKey);
        }
        else if (entry.IsPrefab && callbacks.OnPrefabOpened)
        {
            selection.SelectedPrefabAssetKey = entry.PrimaryAssetKey;
            callbacks.OnPrefabOpened(entry.PrimaryAssetKey);
        }
        else if (entry.IsMaterial)
        {
            selection.SelectedMaterialAssetKey = entry.PrimaryAssetKey;
        }
        else if (entry.IsTileset)
        {
            selection.SelectedTilesetAssetKey = entry.PrimaryAssetKey;
        }
        else if (entry.IsAudioMixer)
        {
            selection.SelectedAudioMixerAssetKey = entry.PrimaryAssetKey;
        }
        else if (entry.IsAnimationClip)
        {
            selection.SelectedAnimationClipAssetKey = entry.PrimaryAssetKey;
            state.RequestFocusAnimationClipEditor = true;
        }
        else if (entry.IsAnimatorController)
        {
            selection.SelectedAnimatorControllerAssetKey = entry.PrimaryAssetKey;
            state.RequestFocusAnimatorControllerEditor = true;
        }
        else if ((entry.IsNativeScriptFile || entry.IsManagedScriptFile) && callbacks.OnNativeScriptAssetActivated)
        {
            selection.SelectedNativeScriptAssetKey = entry.PrimaryAssetKey;
            callbacks.OnNativeScriptAssetActivated(entry.PrimaryAssetKey);
        }
    }

    void DrawProjectGridItemContextMenu(const std::filesystem::path& assetsDirectory,
                                        EditorProjectPanelState& state,
                                        ProjectPanelSelectionRefs& selection,
                                        const ProjectGridEntry& entry,
                                        const ProjectPanelCallbacks& callbacks)
    {
        if (!ImGui::BeginPopupContextItem())
            return;

        std::string subSpriteTextureKey;
        int32_t subSpriteIndex = -1;
        if (Assets::TryParseSubSpriteAssetKey(entry.PrimaryAssetKey, subSpriteTextureKey, subSpriteIndex))
        {
            if (ImGui::MenuItem("Open Parent Texture"))
            {
                state.MultiSelectedAssetKeys = { subSpriteTextureKey };
                state.SelectionAnchorAssetKey = subSpriteTextureKey;
                state.MultiSelectedSubSpriteKeys.clear();
                state.SubSpriteSelectionAnchorKey.clear();
                ClearPrimaryAssetSelection(selection);
                selection.SelectedTextureAssetKey = subSpriteTextureKey;
            }
            ImGui::EndPopup();
            return;
        }

        if (entry.IsNativeScriptFile || entry.IsManagedScriptFile)
        {
            if (ImGui::MenuItem("Open Script") && callbacks.OnNativeScriptAssetActivated)
                callbacks.OnNativeScriptAssetActivated(entry.PrimaryAssetKey);
            if (entry.HasPairedScriptFile)
                ImGui::Separator();
        }

        if (entry.IsScene)
        {
            if (ImGui::MenuItem("Open Scene") && callbacks.OnSceneActivated)
                callbacks.OnSceneActivated(entry.PrimaryAssetKey);
            if (ImGui::MenuItem("Set As Default Scene") && callbacks.OnSetDefaultSceneRequested)
                callbacks.OnSetDefaultSceneRequested(entry.PrimaryAssetKey);
            ImGui::Separator();
        }

        if (entry.IsPrefab)
        {
            if (ImGui::MenuItem("Open Prefab") && callbacks.OnPrefabOpened)
                callbacks.OnPrefabOpened(entry.PrimaryAssetKey);
            if (ImGui::MenuItem("Instantiate Prefab") && callbacks.OnPrefabInstantiated)
                callbacks.OnPrefabInstantiated(entry.PrimaryAssetKey);
            ImGui::Separator();
        }

        if (ImGui::MenuItem(entry.HasPairedScriptFile ? "Rename Script Pair" : "Rename"))
        {
            state.RenameAssetRelativePath = entry.Entry.RelativePath;
            if (entry.HasPairedScriptFile)
                CopyTextToBuffer(state.RenameAssetBuffer, entry.Entry.AbsolutePath.stem().string().c_str());
            else if (entry.IsNativeScriptFile || entry.IsManagedScriptFile)
                CopyTextToBuffer(state.RenameAssetBuffer, entry.Entry.AbsolutePath.stem().string().c_str());
            else
                CopyTextToBuffer(state.RenameAssetBuffer, entry.DisplayName.c_str());
            state.RenameAssetAsNativeScriptPair = entry.HasPairedScriptFile;
            state.RenameAssetPopupPending = true;
        }

        if (ImGui::MenuItem(entry.HasPairedScriptFile ? "Delete Script Pair" : "Delete"))
        {
            const bool deleteMultiSelection =
                state.MultiSelectedAssetKeys.size() > 1 &&
                std::find(state.MultiSelectedAssetKeys.begin(), state.MultiSelectedAssetKeys.end(), entry.PrimaryAssetKey) != state.MultiSelectedAssetKeys.end();
            const bool removed = deleteMultiSelection
                ? DeleteAssetKeysWithSceneHandling(state, assetsDirectory, state.MultiSelectedAssetKeys, callbacks.OnDeleteSceneAssetsRequested)
                : DeleteAssetKeysWithSceneHandling(state, assetsDirectory, { entry.PrimaryAssetKey }, callbacks.OnDeleteSceneAssetsRequested);
            if (removed)
            {
                state.MultiSelectedAssetKeys.clear();
                state.SelectionAnchorAssetKey.clear();
                state.MultiSelectedSubSpriteKeys.clear();
                state.SubSpriteSelectionAnchorKey.clear();
                ClearPrimaryAssetSelection(selection);
                InvalidateProjectDirectoryCache(state);
            }
        }

        ImGui::EndPopup();
    }

    void DrawProjectGridItemDragDropSource(EditorProjectPanelState& state,
                                           const ProjectGridEntry& entry,
                                           const ProjectPanelCallbacks& callbacks)
    {
        if (!ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
            return;

        std::string subSpriteTextureKey;
        int32_t subSpriteIndex = -1;
        const bool entryIsSubSprite =
            Assets::TryParseSubSpriteAssetKey(entry.PrimaryAssetKey, subSpriteTextureKey, subSpriteIndex);
        (void)subSpriteTextureKey;
        (void)subSpriteIndex;

        const bool draggingMultiSelection =
            state.MultiSelectedAssetKeys.size() > 1 &&
            std::find(state.MultiSelectedAssetKeys.begin(), state.MultiSelectedAssetKeys.end(), entry.PrimaryAssetKey) != state.MultiSelectedAssetKeys.end();
        if (draggingMultiSelection)
        {
            const std::string payloadText = EncodeAssetKeyListPayload(state.MultiSelectedAssetKeys);
            if (!payloadText.empty())
            {
                ImGui::SetDragDropPayload(
                    kAssetMultiSelectionPayload,
                    payloadText.c_str(),
                    static_cast<uint32_t>(payloadText.size() + 1),
                    ImGuiCond_Once);
            }
            ImGui::Text("%zu assets", state.MultiSelectedAssetKeys.size());
        }
        else
        {
            if (entryIsSubSprite)
                ImGui::SetDragDropPayload(kSubSpritePayloadId, entry.PrimaryAssetKey.c_str(), static_cast<uint32_t>(entry.PrimaryAssetKey.size() + 1), ImGuiCond_Once);
            else if (entry.IsTexture)
                ImGui::SetDragDropPayload(callbacks.TexturePayloadId, entry.PrimaryAssetKey.c_str(), static_cast<uint32_t>(entry.PrimaryAssetKey.size() + 1), ImGuiCond_Once);
            else if (entry.IsScene)
                ImGui::SetDragDropPayload(callbacks.ScenePayloadId, entry.PrimaryAssetKey.c_str(), static_cast<uint32_t>(entry.PrimaryAssetKey.size() + 1), ImGuiCond_Once);
            else if (entry.IsPrefab)
                ImGui::SetDragDropPayload(callbacks.PrefabPayloadId, entry.PrimaryAssetKey.c_str(), static_cast<uint32_t>(entry.PrimaryAssetKey.size() + 1), ImGuiCond_Once);
            else if (entry.IsMaterial)
                ImGui::SetDragDropPayload(callbacks.MaterialPayloadId, entry.PrimaryAssetKey.c_str(), static_cast<uint32_t>(entry.PrimaryAssetKey.size() + 1), ImGuiCond_Once);
            else if (entry.IsShader)
                ImGui::SetDragDropPayload(callbacks.ShaderPayloadId, entry.PrimaryAssetKey.c_str(), static_cast<uint32_t>(entry.PrimaryAssetKey.size() + 1), ImGuiCond_Once);
            else if (entry.IsAudio)
                ImGui::SetDragDropPayload(callbacks.AudioPayloadId, entry.PrimaryAssetKey.c_str(), static_cast<uint32_t>(entry.PrimaryAssetKey.size() + 1), ImGuiCond_Once);
            else if (entry.IsFont)
                ImGui::SetDragDropPayload(callbacks.FontPayloadId, entry.PrimaryAssetKey.c_str(), static_cast<uint32_t>(entry.PrimaryAssetKey.size() + 1), ImGuiCond_Once);
            else
                ImGui::SetDragDropPayload(callbacks.AssetMovePayloadId, entry.PrimaryAssetKey.c_str(), static_cast<uint32_t>(entry.PrimaryAssetKey.size() + 1), ImGuiCond_Once);
            ImGui::Text("%s", entry.DisplayName.c_str());
        }

        ImGui::EndDragDropSource();
    }
}
