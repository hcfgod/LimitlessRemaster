#include "FileWatcher.h"
#include "Core/Debug/Log.h"
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

        if (!std::filesystem::exists(filepath))
        {
            LT_CORE_ERROR("FileWatcher: Cannot watch non-existent file: {}", filepath);
            return;
        }

        m_FilePath = filepath;
        m_Callback = callback;
        m_LastWriteTime = std::filesystem::last_write_time(filepath);
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
        while (m_IsWatching)
        {
            try
            {
                if (std::filesystem::exists(m_FilePath))
                {
                    auto currentWriteTime = std::filesystem::last_write_time(m_FilePath);
                    
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
            return std::filesystem::last_write_time(filepath);
        }
        catch (const std::exception& e)
        {
            LT_CORE_ERROR("FileWatcher: Error getting last write time for {}: {}", filepath, e.what());
            return std::filesystem::file_time_type::min();
        }
    }
}