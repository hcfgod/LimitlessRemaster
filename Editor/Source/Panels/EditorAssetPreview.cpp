#include "EditorAssetPreview.h"

#include "Assets/MaterialAsset.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <unordered_map>

namespace Limitless::EditorAssetPreview
{
    namespace
    {
        struct MaterialPreviewCacheEntry : MaterialPreviewData
        {
            std::chrono::steady_clock::time_point LoadTime = {};
        };

        std::unordered_map<std::string, MaterialPreviewCacheEntry> gMaterialPreviewCache;
        constexpr std::chrono::milliseconds kMaterialPreviewCacheLifetime(2000);
    }

    const MaterialPreviewData* GetCachedMaterialPreview(const std::string& materialAssetKey)
    {
        if (materialAssetKey.empty())
            return nullptr;

        const auto now = std::chrono::steady_clock::now();
        if (auto it = gMaterialPreviewCache.find(materialAssetKey); it != gMaterialPreviewCache.end())
        {
            if ((now - it->second.LoadTime) < kMaterialPreviewCacheLifetime)
                return it->second.HasPreview ? &it->second : nullptr;
        }

        MaterialPreviewCacheEntry entry;
        entry.LoadTime = now;

        if (Assets::MaterialAsset::Ptr materialAsset = Assets::MaterialAsset::LoadBlocking(materialAssetKey))
        {
            std::shared_ptr<Texture2D> previewTexture = materialAsset->GetMainTexture();
            if (previewTexture)
            {
                entry.PreviewTexture = previewTexture;
                if (materialAsset->HasMainTextureSubRect())
                {
                    const glm::vec2 uvMin = materialAsset->GetMainTextureUvMin();
                    const glm::vec2 uvMax = materialAsset->GetMainTextureUvMax();
                    const glm::vec2 span(std::abs(uvMax.x - uvMin.x), std::abs(uvMax.y - uvMin.y));
                    entry.UvMin = ImVec2(uvMin.x, 1.0f - uvMin.y);
                    entry.UvMax = ImVec2(uvMax.x, 1.0f - uvMax.y);
                    entry.SourceWidth = std::max(1.0f, static_cast<float>(previewTexture->GetWidth()) * span.x);
                    entry.SourceHeight = std::max(1.0f, static_cast<float>(previewTexture->GetHeight()) * span.y);
                }
                else
                {
                    entry.UvMin = ImVec2(0.0f, 1.0f);
                    entry.UvMax = ImVec2(1.0f, 0.0f);
                    entry.SourceWidth = static_cast<float>(std::max(1u, previewTexture->GetWidth()));
                    entry.SourceHeight = static_cast<float>(std::max(1u, previewTexture->GetHeight()));
                }
                entry.HasPreview = true;
            }
        }

        auto [insertedIt, _] = gMaterialPreviewCache.insert_or_assign(materialAssetKey, std::move(entry));
        return insertedIt->second.HasPreview ? &insertedIt->second : nullptr;
    }

    void InvalidateCachedMaterialPreview(const std::string& materialAssetKey)
    {
        if (materialAssetKey.empty())
            return;

        gMaterialPreviewCache.erase(materialAssetKey);
    }
}
