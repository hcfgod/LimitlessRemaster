#pragma once

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
                  Assets::TextureAsset::Ptr& cachedTextureAsset);
    }
}
