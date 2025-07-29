#pragma once

#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <filesystem>
#include <chrono>

namespace Limitless
{
    class FileWatcher
    {
    public:
        using FileChangeCallback = std::function<void(const std::string&)>;
        
        FileWatcher();
        ~FileWatcher();
        
        // Start watching a file for changes
        void StartWatching(const std::string& filepath, FileChangeCallback callback);
        
        // Stop watching
        void StopWatching();
        
        // Check if currently watching
        bool IsWatching() const { return m_IsWatching; }
        
        // Get the currently watched file path
        const std::string& GetWatchedFile() const { return m_FilePath; }

    private:
        void WatchLoop();
        std::filesystem::file_time_type GetLastWriteTime(const std::string& filepath);
        
    private:
        std::string m_FilePath;
        FileChangeCallback m_Callback;
        std::thread m_WatchThread;
        std::atomic<bool> m_IsWatching;
        std::atomic<bool> m_ShouldStop;
        std::filesystem::file_time_type m_LastWriteTime;
        std::chrono::milliseconds m_PollInterval;
    };
}