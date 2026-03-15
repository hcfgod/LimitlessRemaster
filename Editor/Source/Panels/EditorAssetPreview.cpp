#include "EditorAssetPreview.h"

#include "Assets/AssetManager.h"
#include "Assets/MaterialAsset.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace Limitless::EditorAssetPreview
{
    namespace
    {
        constexpr std::chrono::milliseconds kMaterialPreviewCacheLifetime(30000);

        void PopulateMaterialPreviewEntry(MaterialPreviewCacheEntry& entry, const Assets::MaterialAsset::Ptr& materialAsset)
        {
            entry.HasPreview = false;
            entry.PreviewTexture.reset();
            entry.UvMin = ImVec2(0.0f, 1.0f);
            entry.UvMax = ImVec2(1.0f, 0.0f);
            entry.SourceWidth = 1.0f;
            entry.SourceHeight = 1.0f;

            if (!materialAsset)
                return;

            std::shared_ptr<Texture2D> previewTexture = materialAsset->GetMainTexture();
            if (!previewTexture)
                return;

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

    const MaterialPreviewData* GetCachedMaterialPreview(MaterialPreviewCache& cache, const std::string& materialAssetKey)
    {
        if (materialAssetKey.empty())
            return nullptr;

        constexpr int32_t kMaxRetries = 3;
        constexpr auto kRetryCooldown = std::chrono::seconds(3);

        const auto now = std::chrono::steady_clock::now();
        auto it = cache.Entries.find(materialAssetKey);
        if (it != cache.Entries.end())
        {
            // Already have a preview — return it.
            if (it->second.HasPreview)
                return &it->second;

            // Permanently failed — don't retry.
            if (it->second.Failed)
                return nullptr;

            // Async load in flight — poll the Task for completion.
            if (it->second.ReloadInFlight)
            {
                if (it->second.PendingTask.IsValid() && it->second.PendingTask.IsDone())
                {
                    auto result = it->second.PendingTask.Get();
                    it->second.PendingTask = {};
                    it->second.ReloadInFlight = false;

                    if (result)
                    {
                        it->second.CachedMaterialAsset = result;
                        it->second.LoadTime = now;
                        PopulateMaterialPreviewEntry(it->second, result);
                        return it->second.HasPreview ? &it->second : nullptr;
                    }

                    // Load failed.
                    it->second.RetryCount++;
                    it->second.LoadTime = now;
                    if (it->second.RetryCount >= kMaxRetries)
                        it->second.Failed = true;
                    return nullptr;
                }

                // Still in flight — keep waiting.
                return nullptr;
            }

            // Entry exists but no preview and not in flight — respect retry cooldown.
            if (it->second.RetryCount > 0 && (now - it->second.LoadTime) < kRetryCooldown)
                return nullptr;
        }

        // Check if the AssetManager already has this material cached.
        if (auto existing = Assets::AssetManager::GetCachedByKey(materialAssetKey))
        {
            auto asMaterial = std::dynamic_pointer_cast<Assets::MaterialAsset>(existing);
            if (asMaterial)
            {
                MaterialPreviewCacheEntry entry;
                entry.CachedMaterialAsset = asMaterial;
                entry.LoadTime = now;
                PopulateMaterialPreviewEntry(entry, asMaterial);
                auto [insertedIt, _] = cache.Entries.insert_or_assign(materialAssetKey, std::move(entry));
                return insertedIt->second.HasPreview ? &insertedIt->second : nullptr;
            }
        }

        const int32_t prevRetryCount = (it != cache.Entries.end()) ? it->second.RetryCount : 0;
        MaterialPreviewCacheEntry entry;
        entry.LoadTime = now;
        entry.ReloadInFlight = true;
        entry.RetryCount = prevRetryCount;
        entry.PendingTask = Assets::MaterialAsset::LoadAsync(materialAssetKey);
        cache.Entries.insert_or_assign(materialAssetKey, std::move(entry));
        return nullptr;
    }

    void InvalidateCachedMaterialPreview(MaterialPreviewCache& cache, const std::string& materialAssetKey)
    {
        if (materialAssetKey.empty())
            return;

        cache.Entries.erase(materialAssetKey);
    }
}
