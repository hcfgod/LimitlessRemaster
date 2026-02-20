#pragma once

#include "Assets/MaterialAsset.h"
#include "Assets/TextureAsset.h"

#include <string>

namespace Limitless
{
    class Scene;

    namespace EditorInspectorPanel
    {
        void DrawTextureInspector(Scene* scene,
                                  std::string& selectedTextureAssetKey,
                                  Assets::TextureAsset::Ptr& cachedTextureAsset);

        void DrawMaterialInspector(const char* texturePayloadId,
                                   const char* shaderPayloadId,
                                   std::string& selectedMaterialAssetKey,
                                   Assets::MaterialAsset::Ptr& cachedMaterialAsset);

        void DrawNativeScriptAssetInspector(std::string& selectedNativeScriptAssetKey);
        void DrawPrefabAssetInspector(std::string& selectedPrefabAssetKey);
        void DrawTilesetAssetInspector(Scene* scene, std::string& selectedTilesetAssetKey);
        void DrawInputActionsAssetInspector(std::string& selectedInputActionsAssetKey);
        void DrawAudioMixerAssetInspector(std::string& selectedAudioMixerAssetKey);
        void DrawAnimationClipAssetInspector(std::string& selectedAnimationClipAssetKey);
        void DrawAnimatorControllerAssetInspector(std::string& selectedAnimatorControllerAssetKey);

        /// Returns the texture asset key that the user requested to open in the Sprite Editor.
        /// Empty string means no request pending. Consuming code should call ClearPendingSpriteEditorRequest().
        const std::string& GetPendingSpriteEditorRequest();
        void ClearPendingSpriteEditorRequest();
    }
}
