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
    }
}
