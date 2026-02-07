#include "Assets/AssetManager.h"

namespace Limitless::Assets
{
    std::unordered_map<std::string, std::weak_ptr<Asset>> AssetManager::s_KeyCache;
    std::unordered_map<std::string, std::weak_ptr<Asset>> AssetManager::s_GuidCache;
    std::shared_mutex AssetManager::s_Mutex;

    void AssetManager::GarbageCollect()
    {
        std::unique_lock<std::shared_mutex> lock(s_Mutex);

        for (auto it = s_KeyCache.begin(); it != s_KeyCache.end();)
        {
            if (it->second.expired())
            {
                it = s_KeyCache.erase(it);
            }
            else
            {
                ++it;
            }
        }

        for (auto it = s_GuidCache.begin(); it != s_GuidCache.end();)
        {
            if (it->second.expired())
            {
                it = s_GuidCache.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void AssetManager::GetCacheStats(size_t& keyCacheSize, size_t& guidCacheSize)
    {
        std::shared_lock<std::shared_mutex> lock(s_Mutex);
        keyCacheSize = s_KeyCache.size();
        guidCacheSize = s_GuidCache.size();
    }
}

