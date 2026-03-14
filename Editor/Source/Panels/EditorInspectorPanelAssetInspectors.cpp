#include "EditorInspectorPanelAssetInspectorsShared.h"

namespace Limitless::EditorInspectorPanel
{
    const std::string& GetPendingSpriteEditorRequest()
    {
        return Internal::GetPendingSpriteEditorRequestState();
    }

    void ClearPendingSpriteEditorRequest()
    {
        Internal::ClearPendingSpriteEditorRequestState();
    }

    void DrawTextureInspector(Scene* scene,
                              std::string& selectedTextureAssetKey,
                              Assets::TextureAsset::Ptr& cachedTextureAsset)
    {
        Internal::DrawTextureInspectorInternal(scene, selectedTextureAssetKey, cachedTextureAsset);
    }

    void DrawMaterialInspector(const char* texturePayloadId,
                               const char* shaderPayloadId,
                               EditorAssetPreview::MaterialPreviewCache& materialPreviewCache,
                               std::string& selectedMaterialAssetKey,
                               Assets::MaterialAsset::Ptr& cachedMaterialAsset)
    {
        Internal::DrawMaterialInspectorInternal(
            texturePayloadId,
            shaderPayloadId,
            materialPreviewCache,
            selectedMaterialAssetKey,
            cachedMaterialAsset);
    }

    void DrawNativeScriptAssetInspector(std::string& selectedNativeScriptAssetKey)
    {
        Internal::DrawNativeScriptAssetInspectorInternal(selectedNativeScriptAssetKey);
    }

    void DrawPrefabAssetInspector(std::string& selectedPrefabAssetKey)
    {
        Internal::DrawPrefabAssetInspectorInternal(selectedPrefabAssetKey);
    }

    void DrawTilesetAssetInspector(Scene* scene, std::string& selectedTilesetAssetKey)
    {
        Internal::DrawTilesetAssetInspectorInternal(scene, selectedTilesetAssetKey);
    }

    void DrawInputActionsAssetInspector(std::string& selectedInputActionsAssetKey)
    {
        Internal::DrawInputActionsAssetInspectorInternal(selectedInputActionsAssetKey);
    }

    void DrawAudioMixerAssetInspector(std::string& selectedAudioMixerAssetKey)
    {
        Internal::DrawAudioMixerAssetInspectorInternal(selectedAudioMixerAssetKey);
    }

    void DrawAnimationClipAssetInspector(std::string& selectedAnimationClipAssetKey)
    {
        Internal::DrawAnimationClipAssetInspectorInternal(selectedAnimationClipAssetKey);
    }

    void DrawAnimatorControllerAssetInspector(std::string& selectedAnimatorControllerAssetKey)
    {
        Internal::DrawAnimatorControllerAssetInspectorInternal(selectedAnimatorControllerAssetKey);
    }
}
