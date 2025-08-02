#include "FileWatcher.h"
#include "Core/Debug/Log.h"
#include "Core/Concurrency/AsyncIO.h"
#include <filesystem>
#include <chrono>
#include <thread>

namespace Limitless
{
    FileWatcher::FileWatcher()
        : m_IsWatching(false)
        , m_ShouldStop(false)
        , m_PollInterval(std::chrono::milliseconds(500)) // Check every 500ms
    {
    }

    FileWatcher::~FileWatcher()
    {
        StopWatching();
    }

    void FileWatcher::StartWatching(const std::string& filepath, FileChangeCallback callback)
    {
        if (m_IsWatching)
        {
            StopWatching();
        }

        // Use AsyncIO to check if file exists
        auto& asyncIO = Limitless::Async::GetAsyncIO();
        auto existsTask = asyncIO.FileExistsAsync(filepath);
        bool exists = existsTask.Get();
        
        if (!exists)
        {
            LT_CORE_ERROR("FileWatcher: Cannot watch non-existent file: {}", filepath);
            return;
        }

        m_FilePath = filepath;
        m_Callback = callback;
        
        // Use AsyncIO to get initial file modification time
        auto timeTask = asyncIO.GetFileModifiedTimeAsync(filepath);
        m_LastWriteTime = timeTask.Get();
        
        m_IsWatching = true;
        m_ShouldStop = false;

        LT_CORE_INFO("FileWatcher: Started watching {}", filepath);

        // Start the watch thread
        m_WatchThread = std::thread(&FileWatcher::WatchLoop, this);
    }

    void FileWatcher::StopWatching()
    {
        if (!m_IsWatching)
            return;

        m_ShouldStop = true;
        m_IsWatching = false;

        if (m_WatchThread.joinable())
        {
            m_WatchThread.join();
        }

        LT_CORE_INFO("FileWatcher: Stopped watching {}", m_FilePath);
    }

    void FileWatcher::WatchLoop()
    {
        auto& asyncIO = Limitless::Async::GetAsyncIO();
        
        while (m_IsWatching)
        {
            try
            {
                // Use AsyncIO to check if file exists
                auto existsTask = asyncIO.FileExistsAsync(m_FilePath);
                bool exists = existsTask.Get();
                
                if (exists)
                {
                    // Use AsyncIO to get current modification time
                    auto timeTask = asyncIO.GetFileModifiedTimeAsync(m_FilePath);
                    auto currentWriteTime = timeTask.Get();
                    
                    if (currentWriteTime != m_LastWriteTime)
                    {
                        LT_CORE_INFO("FileWatcher: Detected change in {}", m_FilePath);
                        
                        // Update the last write time
                        m_LastWriteTime = currentWriteTime;
                        
                        // Call the callback
                        if (m_Callback)
                        {
                            m_Callback(m_FilePath);
                        }
                    }
                }
                else
                {
                    // File no longer exists
                    LT_CORE_INFO("FileWatcher: Watched file no longer exists: {}", m_FilePath);
                    break;
                }
            }
            catch (const std::exception& e)
            {
                LT_CORE_ERROR("FileWatcher: Error in watch loop: {}", e.what());
            }
            
            // Sleep for a short interval before checking again
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    std::filesystem::file_time_type FileWatcher::GetLastWriteTime(const std::string& filepath)
    {
        try
        {
            auto& asyncIO = Limitless::Async::GetAsyncIO();
            auto timeTask = asyncIO.GetFileModifiedTimeAsync(filepath);
            return timeTask.Get();
        }
        catch (const std::exception& e)
        {
            LT_CORE_ERROR("FileWatcher: Error getting last write time for {}: {}", filepath, e.what());
            return std::filesystem::file_time_type::min();
        }
    }
}