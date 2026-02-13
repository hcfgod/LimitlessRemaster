#pragma once

#include "Assets/MaterialAsset.h"
#include "Assets/TextureAsset.h"
#include "Limitless.h"

#include <string>

namespace Limitless
{
    class Scene;

    namespace EditorInspectorPanel
    {
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
                  Assets::MaterialAsset::Ptr& cachedMaterialAsset);
    }
}
