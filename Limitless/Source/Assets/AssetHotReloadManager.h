#pragma once

#include "Assets/AssetDatabase.h"

#include "Assets/AssetTreeWatcher.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <thread>

namespace Limitless::Assets
{
    // -----------------------------------------------------------------------------
    // AssetHotReloadManager (P1)
    // Watches imported/loaded assets for file changes and triggers:
    // - re-import metadata (AssetDatabase)
    // - reload of cached runtime assets (Asset::Reload)
    // - best-effort dependent propagation via AssetDatabase deps
    //
    // Implementation uses:
    // - one AssetTreeWatcher for the whole Assets/ tree
    // - a debounced/coalesced reload worker to avoid recompiling on every keystroke save
    // -----------------------------------------------------------------------------
    class AssetHotReloadManager final
    {
    public:
        static AssetHotReloadManager& GetInstance();

        void Enable(bool enable);
        bool IsEnabled() const { return m_Enabled.load(std::memory_order_relaxed); }

        // Start watching an asset by key. No-op if already watching or disabled.
        void WatchKey(const std::string& key);

        // Stop watching all assets.
        void Shutdown();

    private:
        AssetHotReloadManager() = default;

        void OnFileChanged(const std::filesystem::path& changedPath);
        void EnsureWatcherRunningLocked();

        void ReloadThreadMain();
        void EnqueueReloadLocked(const std::string& key, const std::string& guid);

    private:
        std::atomic<bool> m_Enabled{true};

        struct WatchEntry
        {
            std::string key;
            std::string guid;
            std::string resolvedPath;
            AssetType type = AssetType::Unknown;
            nlohmann::json importerSettings = nlohmann::json::object();
        };

        struct PendingReload
        {
            std::string key;
            std::string guid;
            uint64_t generation = 0; // coalescing: only reload latest generation
            std::chrono::steady_clock::time_point dueTime{};
        };

        mutable std::mutex m_Mutex;
        std::unordered_map<std::string, WatchEntry> m_ByResolvedPath; // resolvedPath -> entry

        // Single tree watcher.
        std::unique_ptr<AssetTreeWatcher> m_TreeWatcher;

        // Coalesced reload queue.
        std::condition_variable m_ReloadCv;
        std::thread m_ReloadThread;
        bool m_ReloadThreadRunning = false;
        bool m_ReloadThreadStop = false;
        std::unordered_map<std::string, PendingReload> m_PendingByKey;

        std::chrono::milliseconds m_DebounceWindow{250};
    };
}

