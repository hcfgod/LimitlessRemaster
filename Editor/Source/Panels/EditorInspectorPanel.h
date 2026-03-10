#pragma once

#include "EditorAssetPreview.h"
#include "Assets/MaterialAsset.h"
#include "Assets/TextureAsset.h"
#include "Limitless.h"

#include <string>
#include <unordered_map>

namespace Limitless
{
    class Scene;
    class EditorUndoService;

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
    }
}
