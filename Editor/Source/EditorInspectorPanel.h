#pragma once

#include "Assets/MaterialAsset.h"
#include "Assets/TextureAsset.h"
#include "Limitless.h"

#include <string>

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
        bool OpenNativeScriptEditorForAssetKey(const std::string& assetKey);
        void OnNativeScriptAssetRenamed(const std::string& oldAssetKey, const std::string& newAssetKey);

        void Draw(Scene* scene,
                  entt::entity selectedEntity,
                  const char* texturePayloadId,
                  std::string& selectedTextureAssetKey,
                  Assets::TextureAsset::Ptr& cachedTextureAsset,
                  const char* audioPayloadId,
                  const char* materialPayloadId,
                  const char* shaderPayloadId,
                  const char* fontPayloadId,
                  std::string& selectedMaterialAssetKey,
                  Assets::MaterialAsset::Ptr& cachedMaterialAsset,
                  std::string& selectedNativeScriptAssetKey,
                  EditorUndoService* undoService);
    }
}
