#include "AsyncIO.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

namespace Limitless
{
    namespace Async
    {
        AsyncIO& AsyncIO::GetInstance()
        {
            static AsyncIO instance;
            return instance;
        }

        void AsyncIO::Initialize(size_t threadCount)
        {
            bool expectedInitialized = false;
            if (!m_Initialized.compare_exchange_strong(expectedInitialized, true))
                return;

            if (threadCount == 0)
                threadCount = std::thread::hardware_concurrency();
            if (threadCount == 0)
                threadCount = 4;

            LT_CORE_INFO("Initializing AsyncIO with {} threads", threadCount);

            m_Shutdown.store(false);
            m_Threads.reserve(threadCount);

            for (size_t i = 0; i < threadCount; ++i)
            {
                m_Threads.emplace_back(&AsyncIO::WorkerThread, this);
            }

            m_AcceptingTasks.store(true);
            LT_CORE_INFO("AsyncIO initialized successfully");
        }

        void AsyncIO::Shutdown()
        {
            if (!m_Initialized.load())
                return;

            LT_CORE_INFO("Shutting down AsyncIO...");

            // Stop accepting new tasks, then request worker shutdown. Workers drain the queue before exiting.
            m_AcceptingTasks.store(false);
            m_Shutdown.store(true);
            m_TaskCv.notify_all();

            // Wait for all threads to finish
            for (auto& thread : m_Threads)
            {
                if (thread.joinable())
                    thread.join();
            }

            m_Threads.clear();
            m_Initialized.store(false);

            LT_CORE_INFO("AsyncIO shutdown complete");
        }

        void AsyncIO::WorkerThread()
        {
            for (;;)
            {
                std::function<void()> task;
                bool shouldExit = false;
                {
                    std::unique_lock<std::mutex> lock(m_TaskMutex);
                    m_TaskCv.wait(lock, [this]() {
                        return m_Shutdown.load() || !m_TaskQueue.empty();
                    });

                    shouldExit = m_Shutdown.load() && m_TaskQueue.empty();
                    if (!shouldExit)
                    {
                        task = std::move(m_TaskQueue.front());
                        m_TaskQueue.pop_front();
                    }
                }

                if (shouldExit)
                    break;

                try
                {
                    if (task)
                        task();
                }
                catch (const std::exception& e)
                {
                    LT_CORE_ERROR("Exception in async worker thread: {}", e.what());
                }
            }
        }

        void AsyncIO::EnqueueTask(std::function<void()> task)
        {
            if (!task)
                return;

            // If the queue is saturated, execute inline so futures always complete.
            // This avoids deadlocking callers that are waiting on futures during startup.
            {
                std::lock_guard<std::mutex> lock(m_TaskMutex);
                if (!m_Shutdown.load() && m_TaskQueue.size() < m_MaxQueueSize)
                {
                    m_TaskQueue.emplace_back(std::move(task));
                    m_TaskCv.notify_one();
                    return;
                }
            }

            if (m_Shutdown.load())
            {
                // During shutdown, don't queue new work; execute inline to complete any waiting futures.
                task();
                return;
            }

            LT_CORE_WARN("Async task queue is full; executing task inline");
            task();
        }

        // File operations implementation
        Task<std::string> AsyncIO::ReadFileAsync(const std::string& path)
        {
            return Submit([path]() -> std::string {
                std::ifstream file(path, std::ios::binary);
                if (!file.is_open())
                {
                    throw std::runtime_error("Failed to open file: " + path);
                }

                std::stringstream buffer;
                buffer << file.rdbuf();
                return buffer.str();
            });
        }

        Task<Result<std::string>> AsyncIO::ReadFileAsyncResult(const std::string& path)
        {
            return Submit([path]() -> Result<std::string> {
                std::ifstream file(path, std::ios::binary);
                if (!file.is_open())
                {
                    return Result<std::string>(ErrorCode::FileNotFound, "Failed to open file: " + path);
                }

                std::stringstream buffer;
                buffer << file.rdbuf();
                return buffer.str();
            });
        }

        Task<void> AsyncIO::WriteFileAsync(const std::string& path, const std::string& content)
        {
            return Submit([path, content]() -> void {
                std::ofstream file(path, std::ios::binary);
                if (!file.is_open())
                {
                    throw std::runtime_error("Failed to create file: " + path);
                }

                file.write(content.data(), static_cast<std::streamsize>(content.size()));
                if (!file.good())
                {
                    throw std::runtime_error("Failed to write to file: " + path);
                }
            });
        }

        Task<Result<void>> AsyncIO::WriteFileAsyncResult(const std::string& path, const std::string& content)
        {
            return Submit([path, content]() -> Result<void> {
                std::ofstream file(path, std::ios::binary);
                if (!file.is_open())
                {
                    return Result<void>(ErrorCode::FileAccessDenied, "Failed to create file: " + path);
                }

                file.write(content.data(), static_cast<std::streamsize>(content.size()));
                if (!file.good())
                {
                    return Result<void>(ErrorCode::FileAccessDenied, "Failed to write to file: " + path);
                }

                return Result<void>();
            });
        }

        Task<bool> AsyncIO::FileExistsAsync(const std::string& path)
        {
            return Submit([path]() -> bool {
                return std::filesystem::exists(path);
            });
        }

        Task<std::vector<std::string>> AsyncIO::ReadLinesAsync(const std::string& path)
        {
            return Submit([path]() -> std::vector<std::string> {
                std::ifstream file(path);
                if (!file.is_open())
                {
                    throw std::runtime_error("Failed to open file: " + path);
                }

                std::vector<std::string> lines;
                std::string line;
                while (std::getline(file, line))
                {
                    lines.push_back(std::move(line));
                }

                return lines;
            });
        }

        Task<void> AsyncIO::AppendFileAsync(const std::string& path, const std::string& content)
        {
            return Submit([path, content]() -> void {
                std::ofstream file(path, std::ios::app | std::ios::binary);
                if (!file.is_open())
                {
                    throw std::runtime_error("Failed to open file for appending: " + path);
                }

                file.write(content.data(), static_cast<std::streamsize>(content.size()));
                if (!file.good())
                {
                    throw std::runtime_error("Failed to append to file: " + path);
                }
            });
        }

        Task<Result<void>> AsyncIO::AppendFileAsyncResult(const std::string& path, const std::string& content)
        {
            return Submit([path, content]() -> Result<void> {
                std::ofstream file(path, std::ios::app | std::ios::binary);
                if (!file.is_open())
                {
                    return Result<void>(ErrorCode::FileAccessDenied, "Failed to open file for appending: " + path);
                }

                file.write(content.data(), static_cast<std::streamsize>(content.size()));
                if (!file.good())
                {
                    return Result<void>(ErrorCode::FileAccessDenied, "Failed to append to file: " + path);
                }

                return Result<void>();
            });
        }

        Task<std::vector<std::string>> AsyncIO::ListDirectoryAsync(const std::string& path)
        {
            return Submit([path]() -> std::vector<std::string> {
                std::vector<std::string> entries;
                
                try
                {
                    for (const auto& entry : std::filesystem::directory_iterator(path))
                    {
                        entries.push_back(entry.path().string());
                    }
                }
                catch (const std::filesystem::filesystem_error& e)
                {
                    throw std::runtime_error("Failed to list directory: " + std::string(e.what()));
                }

                return entries;
            });
        }

        Task<bool> AsyncIO::CreateDirectoryAsync(const std::string& path)
        {
            return Submit([path]() -> bool {
                try
                {
                    return std::filesystem::create_directories(path);
                }
                catch (const std::filesystem::filesystem_error& e)
                {
                    throw std::runtime_error("Failed to create directory: " + std::string(e.what()));
                }
            });
        }

        Task<bool> AsyncIO::DeleteFileAsync(const std::string& path)
        {
            return Submit([path]() -> bool {
                try
                {
                    return std::filesystem::remove(path);
                }
                catch (const std::filesystem::filesystem_error& e)
                {
                    throw std::runtime_error("Failed to delete file: " + std::string(e.what()));
                }
            });
        }

        Task<bool> AsyncIO::DeleteDirectoryAsync(const std::string& path)
        {
            return Submit([path]() -> bool {
                try
                {
                    return std::filesystem::remove_all(path) > 0;
                }
                catch (const std::filesystem::filesystem_error& e)
                {
                    throw std::runtime_error("Failed to delete directory: " + std::string(e.what()));
                }
            });
        }

        Task<void> AsyncIO::SaveConfigAsync(const std::string& path, const nlohmann::json& config)
        {
            return Submit([path, config]() -> void {
                std::ofstream file(path);
                if (!file.is_open())
                {
                    throw std::runtime_error("Failed to create config file: " + path);
                }

                file << config.dump(4);
                if (!file.good())
                {
                    throw std::runtime_error("Failed to write config file: " + path);
                }
            });
        }

        Task<Result<void>> AsyncIO::SaveConfigAsyncResult(const std::string& path, const nlohmann::json& config)
        {
            return Submit([path, config]() -> Result<void> {
                std::ofstream file(path);
                if (!file.is_open())
                {
                    return Result<void>(ErrorCode::FileAccessDenied, "Failed to create config file: " + path);
                }

                file << config.dump(4);
                if (!file.good())
                {
                    return Result<void>(ErrorCode::FileAccessDenied, "Failed to write config file: " + path);
                }

                return Result<void>();
            });
        }

        Task<nlohmann::json> AsyncIO::LoadConfigAsync(const std::string& path)
        {
            return Submit([path]() -> nlohmann::json {
                std::ifstream file(path);
                if (!file.is_open())
                {
                    throw std::runtime_error("Failed to open config file: " + path);
                }

                nlohmann::json config;
                file >> config;
                return config;
            });
        }

        Task<Result<nlohmann::json>> AsyncIO::LoadConfigAsyncResult(const std::string& path)
        {
            return Submit([path]() -> Result<nlohmann::json> {
                std::ifstream file(path);
                if (!file.is_open())
                {
                    return Result<nlohmann::json>(ErrorCode::FileNotFound, "Failed to open config file: " + path);
                }

                try
                {
                    nlohmann::json config;
                    file >> config;
                    return config;
                }
                catch (const std::exception& e)
                {
                    return Result<nlohmann::json>(ErrorCode::ConfigParseError, std::string("Failed to parse config file: ") + e.what());
                }
            });
        }

        Task<size_t> AsyncIO::GetFileSizeAsync(const std::string& path)
        {
            return Submit([path]() -> size_t {
                try
                {
                    return std::filesystem::file_size(path);
                }
                catch (const std::filesystem::filesystem_error& e)
                {
                    throw std::runtime_error("Failed to get file size: " + std::string(e.what()));
                }
            });
        }

        Task<std::filesystem::file_time_type> AsyncIO::GetFileModifiedTimeAsync(const std::string& path)
        {
            return Submit([path]() -> std::filesystem::file_time_type {
                try
                {
                    return std::filesystem::last_write_time(path);
                }
                catch (const std::filesystem::filesystem_error& e)
                {
                    throw std::runtime_error("Failed to get file modification time: " + std::string(e.what()));
                }
            });
        }
    }
} 