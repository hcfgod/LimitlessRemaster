#include "Assets/AssetTreeWatcher.h"

#include "Core/Debug/Log.h"

namespace Limitless::Assets
{
    AssetTreeWatcher::AssetTreeWatcher() = default;

    AssetTreeWatcher::~AssetTreeWatcher()
    {
        Stop();
    }

    void AssetTreeWatcher::Start(const std::filesystem::path& rootDirectory, ChangeCallback callback)
    {
        Stop();

        m_Root = rootDirectory;
        m_Callback = std::move(callback);
        m_StopRequested.store(false, std::memory_order_relaxed);
        m_Running.store(true, std::memory_order_relaxed);

        {
            std::lock_guard<std::mutex> lock(m_StateMutex);
            m_LastWriteTimes.clear();
        }

        // Seed initial state WITHOUT firing callbacks. Otherwise, startup would emit "changes"
        // for every file in the tree and can also deadlock if the caller holds locks.
        {
            std::unordered_map<std::string, std::filesystem::file_time_type> snapshot;
            snapshot.reserve(4096);

            std::error_code ec;
            for (auto it = std::filesystem::recursive_directory_iterator(m_Root, ec);
                 it != std::filesystem::recursive_directory_iterator();
                 it.increment(ec))
            {
                if (ec)
                {
                    ec.clear();
                    continue;
                }

                if (!it->is_regular_file(ec))
                {
                    ec.clear();
                    continue;
                }

                const auto& path = it->path();
                if (path.extension() == ".meta")
                {
                    continue;
                }

                const auto time = std::filesystem::last_write_time(path, ec);
                if (ec)
                {
                    ec.clear();
                    continue;
                }

                snapshot[path.string()] = time;
            }

            std::lock_guard<std::mutex> lock(m_StateMutex);
            m_LastWriteTimes = std::move(snapshot);
        }

        m_Thread = std::thread(&AssetTreeWatcher::ThreadMain, this);
        LT_CORE_INFO("AssetTreeWatcher: started for '{}'", m_Root.string());
    }

    void AssetTreeWatcher::Stop()
    {
        if (!m_Running.exchange(false, std::memory_order_relaxed))
        {
            return;
        }

        m_StopRequested.store(true, std::memory_order_relaxed);
        if (m_Thread.joinable())
        {
            m_Thread.join();
        }

        LT_CORE_INFO("AssetTreeWatcher: stopped");
    }

    void AssetTreeWatcher::ThreadMain()
    {
        while (!m_StopRequested.load(std::memory_order_relaxed))
        {
            try
            {
                ScanOnce();
            }
            catch (const std::exception& e)
            {
                LT_CORE_ERROR("AssetTreeWatcher: scan error: {}", e.what());
            }
            catch (...)
            {
                LT_CORE_ERROR("AssetTreeWatcher: scan error (unknown)");
            }

            std::this_thread::sleep_for(m_PollInterval);
        }
    }

    void AssetTreeWatcher::ScanOnce()
    {
        if (m_Root.empty() || !std::filesystem::exists(m_Root))
        {
            return;
        }

        // Build a new snapshot.
        std::unordered_map<std::string, std::filesystem::file_time_type> current;
        current.reserve(4096);

        std::error_code ec;
        for (auto it = std::filesystem::recursive_directory_iterator(m_Root, ec);
             it != std::filesystem::recursive_directory_iterator();
             it.increment(ec))
        {
            if (ec)
            {
                ec.clear();
                continue;
            }

            if (!it->is_regular_file(ec))
            {
                ec.clear();
                continue;
            }

            const auto& path = it->path();
            // Skip meta changes from triggering asset rebuilds directly; we generate these ourselves.
            if (path.extension() == ".meta")
            {
                continue;
            }

            const auto time = std::filesystem::last_write_time(path, ec);
            if (ec)
            {
                ec.clear();
                continue;
            }

            current[path.string()] = time;
        }

        // Compare with previous snapshot and emit changes.
        std::unordered_map<std::string, std::filesystem::file_time_type> previous;
        {
            std::lock_guard<std::mutex> lock(m_StateMutex);
            previous = m_LastWriteTimes;
            m_LastWriteTimes = current;
        }

        if (!m_Callback)
        {
            return;
        }

        // Modified/added.
        for (const auto& [pathString, time] : current)
        {
            const auto itPrev = previous.find(pathString);
            if (itPrev == previous.end() || itPrev->second != time)
            {
                m_Callback(std::filesystem::path(pathString));
            }
        }

        // Removed.
        for (const auto& [pathString, _] : previous)
        {
            if (current.find(pathString) == current.end())
            {
                m_Callback(std::filesystem::path(pathString));
            }
        }
    }
}

