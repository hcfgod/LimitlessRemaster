#pragma once

#include "Graphics/Texture.h"
#include "imgui/imgui.h"

#include <memory>
#include <string>

namespace Limitless::EditorAssetPreview
{
    struct MaterialPreviewData
    {
        std::shared_ptr<Texture2D> PreviewTexture;
        ImVec2 UvMin = ImVec2(0.0f, 1.0f);
        ImVec2 UvMax = ImVec2(1.0f, 0.0f);
        float SourceWidth = 1.0f;
        float SourceHeight = 1.0f;
        bool HasPreview = false;
    };

    const MaterialPreviewData* GetCachedMaterialPreview(const std::string& materialAssetKey);
    void InvalidateCachedMaterialPreview(const std::string& materialAssetKey);
}
