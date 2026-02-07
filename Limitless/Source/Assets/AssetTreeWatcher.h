#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace Limitless::Assets
{
    // -----------------------------------------------------------------------------
    // AssetTreeWatcher
    // One watcher for the whole Assets/ directory tree.
    //
    // Implementation: polling scan using std::filesystem::recursive_directory_iterator.
    // This is portable and keeps the codebase simple. For AAA-grade performance later,
    // we can swap the backend with platform native file notifications.
    //
    // Callback: called on the watch thread when a file is added/modified/removed.
    // -----------------------------------------------------------------------------
    class AssetTreeWatcher final
    {
    public:
        using ChangeCallback = std::function<void(const std::filesystem::path& changedPath)>;

        AssetTreeWatcher();
        ~AssetTreeWatcher();

        AssetTreeWatcher(const AssetTreeWatcher&) = delete;
        AssetTreeWatcher& operator=(const AssetTreeWatcher&) = delete;

        void Start(const std::filesystem::path& rootDirectory, ChangeCallback callback);
        void Stop();

        bool IsRunning() const { return m_Running.load(std::memory_order_relaxed); }

        void SetPollInterval(std::chrono::milliseconds interval) { m_PollInterval = interval; }

    private:
        void ThreadMain();
        void ScanOnce();

    private:
        std::filesystem::path m_Root;
        ChangeCallback m_Callback;

        std::chrono::milliseconds m_PollInterval{250};

        std::atomic<bool> m_Running{false};
        std::atomic<bool> m_StopRequested{false};
        std::thread m_Thread;

        // path -> last_write_time (or missing)
        std::mutex m_StateMutex;
        std::unordered_map<std::string, std::filesystem::file_time_type> m_LastWriteTimes;
    };
}

