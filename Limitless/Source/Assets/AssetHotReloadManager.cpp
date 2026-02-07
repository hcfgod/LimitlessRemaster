#include "Assets/AssetHotReloadManager.h"

#include "Assets/AssetManager.h"
#include "Assets/AssetPaths.h"

#include "Core/Debug/Log.h"

#include <vector>

namespace Limitless::Assets
{
    AssetHotReloadManager& AssetHotReloadManager::GetInstance()
    {
        static AssetHotReloadManager s_Instance;
        return s_Instance;
    }

    void AssetHotReloadManager::Enable(bool enable)
    {
        m_Enabled.store(enable, std::memory_order_relaxed);
        if (!enable)
        {
            Shutdown();
        }
    }

    void AssetHotReloadManager::WatchKey(const std::string& key)
    {
        if (!IsEnabled() || key.empty())
        {
            return;
        }

        auto recordResult = AssetDatabase::GetInstance().FindByKey(key);
        if (recordResult.IsFailure())
        {
            // Importers typically call this right after ImportOrUpdate; if they don't, just ignore.
            return;
        }

        const auto record = recordResult.GetValue();

        // IMPORTANT:
        // Do NOT start the tree watcher while holding `m_Mutex` because the watcher can invoke
        // callbacks (ScanOnce) that call back into AssetHotReloadManager and try to lock `m_Mutex`.
        bool shouldStartWatcher = false;
        bool shouldStartReloadThread = false;
        std::filesystem::path assetsRoot;

        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            if (!m_TreeWatcher)
            {
                const auto rootResult = FindProjectRootFromWorkingDirectory();
                if (rootResult.IsFailure())
                {
                    LT_CORE_ERROR("AssetHotReload: cannot start tree watcher: {}", rootResult.GetError().GetErrorMessage());
                }
                else
                {
                    assetsRoot = rootResult.GetValue() / "Assets";
                    m_TreeWatcher = std::make_unique<AssetTreeWatcher>();
                    shouldStartWatcher = true;
                }
            }

            if (!m_ReloadThreadRunning)
            {
                m_ReloadThreadRunning = true;
                shouldStartReloadThread = true;
            }

            if (m_ByResolvedPath.find(record.ResolvedPath) == m_ByResolvedPath.end())
            {
                WatchEntry entry;
                entry.key = record.Key;
                entry.guid = record.Guid;
                entry.resolvedPath = record.ResolvedPath;
                entry.type = record.Type;
                entry.importerSettings = record.ImporterSettings;
                m_ByResolvedPath.emplace(entry.resolvedPath, std::move(entry));
            }
        }

        if (shouldStartWatcher && m_TreeWatcher)
        {
            m_TreeWatcher->Start(assetsRoot, [this](const std::filesystem::path& changed) {
                OnFileChanged(changed);
            });
        }

        if (shouldStartReloadThread)
        {
            m_ReloadThread = std::thread(&AssetHotReloadManager::ReloadThreadMain, this);
        }
    }

    void AssetHotReloadManager::Shutdown()
    {
        // IMPORTANT:
        // Do not join threads while holding `m_Mutex`. The reload thread needs `m_Mutex` to wake,
        // observe the stop flag, and exit cleanly.
        std::unique_ptr<AssetTreeWatcher> watcherToStop;
        std::thread reloadThreadToJoin;

        {
            std::lock_guard<std::mutex> lock(m_Mutex);

            m_ByResolvedPath.clear();
            m_PendingByKey.clear();

            watcherToStop = std::move(m_TreeWatcher);

            if (m_ReloadThreadRunning)
            {
                m_ReloadThreadStop = true;
                m_ReloadCv.notify_all();
                reloadThreadToJoin = std::move(m_ReloadThread);
                m_ReloadThreadRunning = false;
            }
        }

        if (watcherToStop)
        {
            watcherToStop->Stop();
        }

        if (reloadThreadToJoin.joinable())
        {
            reloadThreadToJoin.join();
        }

        // Ready for future use.
        m_ReloadThreadStop = false;
    }

    void AssetHotReloadManager::EnsureWatcherRunningLocked()
    {
        // Deprecated: watcher startup must occur outside locks to avoid deadlocks.
        // Keep for compatibility, but do nothing.
    }

    void AssetHotReloadManager::OnFileChanged(const std::filesystem::path& changedPath)
    {
        if (!IsEnabled())
        {
            return;
        }

        std::string key;
        std::string guid;
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            const auto it = m_ByResolvedPath.find(changedPath.string());
            if (it == m_ByResolvedPath.end())
            {
                return;
            }
            key = it->second.key;
            guid = it->second.guid;

            EnqueueReloadLocked(key, guid);
        }

        LT_CORE_INFO("AssetHotReload: change detected '{}', queued reload key='{}'", changedPath.string(), key);
    }

    void AssetHotReloadManager::EnqueueReloadLocked(const std::string& key, const std::string& guid)
    {
        auto& pending = m_PendingByKey[key];
        pending.key = key;
        pending.guid = guid;
        pending.generation++;
        pending.dueTime = std::chrono::steady_clock::now() + m_DebounceWindow;
        m_ReloadCv.notify_all();
    }

    void AssetHotReloadManager::ReloadThreadMain()
    {
        while (true)
        {
            std::vector<PendingReload> ready;

            {
                std::unique_lock<std::mutex> lock(m_Mutex);
                m_ReloadCv.wait(lock, [this] {
                    return m_ReloadThreadStop || !m_PendingByKey.empty();
                });

                if (m_ReloadThreadStop)
                {
                    return;
                }

                auto now = std::chrono::steady_clock::now();
                auto nextDue = now + std::chrono::hours(24);
                for (const auto& [key, p] : m_PendingByKey)
                {
                    if (p.dueTime < nextDue)
                    {
                        nextDue = p.dueTime;
                    }
                }

                if (nextDue > now)
                {
                    m_ReloadCv.wait_until(lock, nextDue, [this] { return m_ReloadThreadStop; });
                    if (m_ReloadThreadStop)
                    {
                        return;
                    }
                }

                now = std::chrono::steady_clock::now();
                for (auto it = m_PendingByKey.begin(); it != m_PendingByKey.end();)
                {
                    if (it->second.dueTime <= now)
                    {
                        ready.push_back(it->second);
                        it = m_PendingByKey.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }
            }

            for (const auto& job : ready)
            {
                auto recordResult = AssetDatabase::GetInstance().FindByKey(job.key);
                if (recordResult.IsFailure())
                {
                    continue;
                }

                const auto record = recordResult.GetValue();
                LT_CORE_INFO("AssetHotReload: reimporting key='{}'", record.Key);

                const auto reimportResult = AssetDatabase::GetInstance().ImportOrUpdate(record.Key, record.Type, record.ImporterSettings);
                if (reimportResult.IsFailure())
                {
                    LT_CORE_WARN("AssetHotReload: reimport failed for '{}': {}", record.Key, reimportResult.GetError().GetErrorMessage());
                }

                const std::shared_ptr<Asset> cached = AssetManager::GetCachedByKey(record.Key);
                if (cached)
                {
                    const bool ok = cached->Reload();
                    LT_CORE_INFO("AssetHotReload: reload key='{}' result={}", record.Key, ok ? "success" : "no-op");
                }
                else
                {
                    LT_CORE_INFO("AssetHotReload: key='{}' not currently loaded (no cached instance to reload)", record.Key);
                }

                const auto dependents = AssetDatabase::GetInstance().GetDependentsOf(record.Guid);
                for (const auto& dep : dependents)
                {
                    std::shared_ptr<Asset> depCached = AssetManager::GetCachedByKey(dep.Key);
                    if (depCached)
                    {
                        (void)depCached->Reload();
                    }
                }
            }
        }
    }
}

