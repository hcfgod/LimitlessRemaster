#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace Limitless::Assets
{
    struct AssetTreeWatcherBackend;

    // -----------------------------------------------------------------------------
    // AssetTreeWatcher
    // One watcher for the whole Assets/ directory tree.
    //
    // Implementation: platform-native directory notifications when available:
    // - Windows: ReadDirectoryChangesW
    // - Linux: inotify
    // - macOS: FSEvents
    //
    // Fallback: portable polling scan using std::filesystem::recursive_directory_iterator.
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
        void EmitPathIfRelevant(const std::filesystem::path& path);

    private:
        std::filesystem::path m_Root;
        ChangeCallback m_Callback;

        std::chrono::milliseconds m_PollInterval{250};

        std::atomic<bool> m_Running{false};
        std::atomic<bool> m_StopRequested{false};
        std::thread m_Thread;

        std::unique_ptr<AssetTreeWatcherBackend> m_Backend;
    };
}

