#pragma once

#include "Assets/Asset.h"
#include "Assets/AssetImporter.h"
#include "Assets/AssetLoadCoordinator.h"
#include "Core/Debug/Log.h"

#include "Core/Concurrency/AsyncIO.h"

#include <memory>
#include <shared_mutex>
#include <string>
#include <type_traits>
#include <chrono>
#include <unordered_map>
#include <vector>

namespace Limitless::Assets
{
    // -----------------------------------------------------------------------------
    // AssetManager
    // Unity-style global weak cache:
    // - Key cache: key/path -> weak asset
    // - GUID cache: guid -> weak asset
    //
    // Loading occurs outside locks; commit occurs under a write lock.
    // -----------------------------------------------------------------------------
    class AssetManager final
    {
    public:
        /// Configures the cooldown duration before retrying a failed asset load.
        /// Default is 1 second.
        static void SetFailedLoadRetryCooldown(std::chrono::milliseconds cooldown) { s_RetryCooldown = cooldown; }
        static std::chrono::milliseconds GetFailedLoadRetryCooldown() { return s_RetryCooldown; }

        // Generic Unity-style load API.
        // Users call: `AssetManager::LoadAsync<TextureAsset>("Assets/...")`
        template<typename TAsset>
        static auto LoadAsync(const std::string& key, const typename AssetImporter<TAsset>::Settings& settings = typename AssetImporter<TAsset>::Settings{})
            -> Async::Task<typename AssetImporter<TAsset>::Ptr>
        {
            const uint64_t generation = AssetLoadCoordinator::GetGeneration();
            return AssetImporter<TAsset>::LoadAsync(key, settings, generation);
        }

        template<typename TAsset>
        static auto LoadBlocking(const std::string& key, const typename AssetImporter<TAsset>::Settings& settings = typename AssetImporter<TAsset>::Settings{})
            -> typename AssetImporter<TAsset>::Ptr
        {
            auto task = LoadAsync<TAsset>(key, settings);
            task.Wait();
            return task.Get();
        }

        template<typename T, typename LoaderFn>
        static std::shared_ptr<T> GetOrLoad(const std::string& key, LoaderFn loader)
        {
            static_assert(std::is_base_of_v<Asset, T>, "T must derive from Asset");

            if (key.empty())
            {
                LT_CORE_WARN("AssetManager::GetOrLoad called with empty key");
                return nullptr;
            }

            // 1) Shared lock lookup.
            std::shared_ptr<Asset> cached;
            {
                std::shared_lock<std::shared_mutex> rlock(s_Mutex);
                const auto it = s_KeyCache.find(key);
                if (it != s_KeyCache.end())
                {
                    cached = it->second.lock();
                }
            }

            if (cached)
            {
                // Ensure GUID cache has an entry (may have been GC'd).
                const std::string guid = cached->GetGuid();
                {
                    std::unique_lock<std::shared_mutex> wlock(s_Mutex);
                    s_GuidCache[guid] = cached;
                    s_FailedLoadRetryByKey.erase(key);
                }
                return std::static_pointer_cast<T>(cached);
            }

            // 2) Respect failure retry cooldown to avoid per-frame load spam for missing assets.
            const auto now = std::chrono::steady_clock::now();
            {
                std::shared_lock<std::shared_mutex> rlock(s_Mutex);
                const auto failedIt = s_FailedLoadRetryByKey.find(key);
                if (failedIt != s_FailedLoadRetryByKey.end() && now < failedIt->second)
                {
                    return nullptr;
                }
            }

            // 2) Load outside locks.
            auto loaded = loader();
            if (!loaded)
            {
                bool shouldLog = false;
                {
                    std::unique_lock<std::shared_mutex> wlock(s_Mutex);
                    auto [it, inserted] = s_FailedLoadRetryByKey.insert_or_assign(
                        key, now + s_RetryCooldown);
                    shouldLog = inserted;
                    (void)it;
                }

                if (shouldLog)
                {
                    LT_CORE_ERROR("AssetManager::GetOrLoad failed to load '{}'", key);
                }
                return nullptr;
            }

            std::shared_ptr<Asset> loadedBase = loaded;
            const std::string loadedGuid = loadedBase->GetGuid();

            // 3) Commit under write lock with dedup.
            {
                std::unique_lock<std::shared_mutex> wlock(s_Mutex);

                // a) Another thread might have inserted while we loaded.
                if (auto it = s_KeyCache.find(key); it != s_KeyCache.end())
                {
                    if (auto existing = it->second.lock())
                    {
                        s_GuidCache[existing->GetGuid()] = existing;
                        s_FailedLoadRetryByKey.erase(key);
                        return std::static_pointer_cast<T>(existing);
                    }
                    s_KeyCache.erase(it);
                }

                // b) GUID dedup: prefer existing instance if same GUID already exists.
                if (auto git = s_GuidCache.find(loadedGuid); git != s_GuidCache.end())
                {
                    if (auto existing = git->second.lock())
                    {
                        s_KeyCache[key] = existing;
                        s_FailedLoadRetryByKey.erase(key);
                        return std::static_pointer_cast<T>(existing);
                    }
                    s_GuidCache.erase(git);
                }

                s_KeyCache[key] = loadedBase;
                s_GuidCache[loadedGuid] = loadedBase;
                s_FailedLoadRetryByKey.erase(key);
            }

            return loaded;
        }

        // Non-loading cache lookup helpers (no logs, no allocations).
        static std::shared_ptr<Asset> GetCachedByKey(const std::string& key)
        {
            if (key.empty())
            {
                return nullptr;
            }

            std::shared_lock<std::shared_mutex> rlock(s_Mutex);
            const auto it = s_KeyCache.find(key);
            if (it == s_KeyCache.end())
            {
                return nullptr;
            }
            return it->second.lock();
        }

        static std::shared_ptr<Asset> GetCachedByGuid(const std::string& guid)
        {
            if (guid.empty())
            {
                return nullptr;
            }

            std::shared_lock<std::shared_mutex> rlock(s_Mutex);
            const auto it = s_GuidCache.find(guid);
            if (it == s_GuidCache.end())
            {
                return nullptr;
            }
            return it->second.lock();
        }

        template<typename T>
        static std::shared_ptr<T> GetByGuid(const std::string& guid)
        {
            static_assert(std::is_base_of_v<Asset, T>, "T must derive from Asset");
            if (guid.empty())
            {
                return nullptr;
            }

            // Shared lock first.
            {
                std::shared_lock<std::shared_mutex> rlock(s_Mutex);
                const auto it = s_GuidCache.find(guid);
                if (it != s_GuidCache.end())
                {
                    if (auto sp = it->second.lock())
                    {
                        return std::static_pointer_cast<T>(sp);
                    }
                }
            }

            // Cleanup expired under write lock.
            std::unique_lock<std::shared_mutex> wlock(s_Mutex);
            const auto it = s_GuidCache.find(guid);
            if (it != s_GuidCache.end() && it->second.expired())
            {
                s_GuidCache.erase(it);
            }

            return nullptr;
        }

        static void GarbageCollect();

        static void GetCacheStats(size_t& keyCacheSize, size_t& guidCacheSize);

        // Clears key+GUID weak caches. This does not destroy assets (weak references only),
        // but it forces future loads to resolve through importers instead of returning
        // previously-cached instances.
        static void ClearCaches();

    private:
        static std::unordered_map<std::string, std::weak_ptr<Asset>> s_KeyCache;
        static std::unordered_map<std::string, std::weak_ptr<Asset>> s_GuidCache;
        static std::unordered_map<std::string, std::chrono::steady_clock::time_point> s_FailedLoadRetryByKey;
        static std::shared_mutex s_Mutex;
        static std::chrono::milliseconds s_RetryCooldown;
    };
}

