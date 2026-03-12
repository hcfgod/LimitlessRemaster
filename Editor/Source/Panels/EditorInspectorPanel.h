#pragma once

#include "EditorAssetPreview.h"
#include "Assets/MaterialAsset.h"
#include "Assets/TextureAsset.h"
#include "Limitless.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "EnTT/entt.hpp"

namespace Limitless
{
    class Scene;
    class EditorUndoService;

    /// Per-instance mutable state for an Inspector panel (supports lock + multi-instance).
    struct EditorInspectorInstanceState
    {
        bool IsOpen = true;
        bool IsLocked = false;

        // Snapshotted selection used when locked.
        entt::entity LockedEntity = entt::null;
        std::string LockedTextureAssetKey;
        std::string LockedMaterialAssetKey;
        std::string LockedNativeScriptAssetKey;
        std::string LockedPrefabAssetKey;
        std::string LockedTilesetAssetKey;
        std::string LockedAudioMixerAssetKey;
        std::string LockedInputActionsAssetKey;
        std::string LockedAnimationClipAssetKey;
        std::string LockedAnimatorControllerAssetKey;

        Assets::TextureAsset::Ptr LockedCachedTextureAsset;
        Assets::MaterialAsset::Ptr LockedCachedMaterialAsset;
    };

    namespace EditorInspectorPanel
    {
        struct NativeScriptEditorSessionState
        {
            bool IsOpen = false;
            std::string LastEditedScriptClassName;
            std::string LastEditedScriptAssetRelativePath;
            bool ShowDebugInfo = false;
        };

        void GetNativeScriptEditorSessionState(NativeScriptEditorSessionState& outState);
        void ApplyNativeScriptEditorSessionState(const NativeScriptEditorSessionState& state);
        void GetPersistentFoldoutState(std::unordered_map<std::string, bool>& outState);
        void ApplyPersistentFoldoutState(const std::unordered_map<std::string, bool>& state);
        void GetPersistentSectionOrderState(std::unordered_map<std::string, std::vector<std::string>>& outState);
        void ApplyPersistentSectionOrderState(const std::unordered_map<std::string, std::vector<std::string>>& state);
        std::vector<std::string> GetOrderedSectionKeys(const std::vector<std::string>& availableSectionKeys);
        bool MovePersistentSectionKeyToIndex(std::string_view sectionKey, const std::vector<std::string>& orderedSectionKeys, size_t destinationIndex);
        bool HandleSectionDragDrop(std::string_view sectionKey,
                                   const std::vector<std::string>& orderedSectionKeys,
                                   const char* dragLabel = nullptr);
        bool BeginPersistentTreeNode(const char* stateKeySuffix, const char* label, int treeNodeFlags = 0);
        bool OpenNativeScriptEditorForAssetKey(const std::string& assetKey);
        bool BuildProjectNativeScripts(std::string* outStatusMessage = nullptr);
        bool GetLastNativeScriptBuildFailure(std::string* outStatusMessage = nullptr);
        void OnNativeScriptAssetRenamed(const std::string& oldAssetKey, const std::string& newAssetKey);

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
                  EditorUndoService* undoService);

        /// Overload that supports lock toggle and custom window name for multi-instance panels.
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
                          EditorUndoService* undoService);
    }
}
