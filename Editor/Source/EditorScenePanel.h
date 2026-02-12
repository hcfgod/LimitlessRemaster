#pragma once

#include "Assets/TextureAsset.h"
#include "Limitless.h"

#include <string>

namespace Limitless
{
    class Scene;

    namespace EditorScenePanel
    {
        void Draw(Scene* scene,
                  entt::entity& selectedEntity,
                  std::string& selectedTextureAssetKey,
                  Assets::TextureAsset::Ptr& cachedTextureAsset);
    }
}
