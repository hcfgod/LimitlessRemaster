#pragma once

#include "Assets/MaterialAsset.h"
#include "Graphics/Texture.h"
#include "imgui/imgui.h"

#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>

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

    struct MaterialPreviewCacheEntry : MaterialPreviewData
    {
        Assets::MaterialAsset::Ptr CachedMaterialAsset;
        Async::Task<Assets::MaterialAsset::Ptr> PendingTask;
        std::chrono::steady_clock::time_point LoadTime = {};
        bool ReloadInFlight = false;
        int32_t RetryCount = 0;
        bool Failed = false;
    };

    struct MaterialPreviewCache
    {
        std::unordered_map<std::string, MaterialPreviewCacheEntry> Entries;
    };

    const MaterialPreviewData* GetCachedMaterialPreview(MaterialPreviewCache& cache, const std::string& materialAssetKey);
    void InvalidateCachedMaterialPreview(MaterialPreviewCache& cache, const std::string& materialAssetKey);
}
