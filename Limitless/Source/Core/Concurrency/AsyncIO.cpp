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
            if (m_Initialized.load())
                return;

            if (threadCount == 0)
                threadCount = std::thread::hardware_concurrency();

            LT_INFO("Initializing AsyncIO with {} threads", threadCount);

            m_Shutdown.store(false);
            m_Threads.reserve(threadCount);

            for (size_t i = 0; i < threadCount; ++i)
            {
                m_Threads.emplace_back(&AsyncIO::WorkerThread, this);
            }

            m_Initialized.store(true);
            LT_INFO("AsyncIO initialized successfully");
        }

        void AsyncIO::Shutdown()
        {
            if (!m_Initialized.load())
                return;

            LT_INFO("Shutting down AsyncIO...");

            m_Shutdown.store(true);

            // Wait for all threads to finish
            for (auto& thread : m_Threads)
            {
                if (thread.joinable())
                    thread.join();
            }

            m_Threads.clear();
            m_Initialized.store(false);

            LT_INFO("AsyncIO shutdown complete");
        }

        void AsyncIO::WorkerThread()
        {
            while (!m_Shutdown.load())
            {
                auto taskResult = m_TaskQueue.TryPop();
                if (taskResult.has_value())
                {
                    try
                    {
                        auto& task = taskResult.value();
                        if (task)
                            task();
                    }
                    catch (const std::exception& e)
                    {
                        LT_ERROR("Exception in async worker thread: {}", e.what());
                    }
                }
                else
                {
                    std::this_thread::yield();
                }
            }
        }

        void AsyncIO::EnqueueTask(std::function<void()> task)
        {
            if (!m_TaskQueue.TryPush(std::move(task)))
            {
                LT_WARN("Async task queue is full, dropping task");
            }
        }

        // File operations implementation
        Task<std::string> AsyncIO::ReadFileAsync(const std::string& path)
        {
            return Task<std::string>([path]() -> std::string {
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

        Task<void> AsyncIO::WriteFileAsync(const std::string& path, const std::string& content)
        {
            return Task<void>([path, content]() -> void {
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

        Task<bool> AsyncIO::FileExistsAsync(const std::string& path)
        {
            return Task<bool>([path]() -> bool {
                return std::filesystem::exists(path);
            });
        }

        Task<std::vector<std::string>> AsyncIO::ReadLinesAsync(const std::string& path)
        {
            return Task<std::vector<std::string>>([path]() -> std::vector<std::string> {
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
            return Task<void>([path, content]() -> void {
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

        Task<std::vector<std::string>> AsyncIO::ListDirectoryAsync(const std::string& path)
        {
            return Task<std::vector<std::string>>([path]() -> std::vector<std::string> {
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
            return Task<bool>([path]() -> bool {
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
            return Task<bool>([path]() -> bool {
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
            return Task<bool>([path]() -> bool {
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
            return Task<void>([path, config]() -> void {
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

        Task<nlohmann::json> AsyncIO::LoadConfigAsync(const std::string& path)
        {
            return Task<nlohmann::json>([path]() -> nlohmann::json {
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

        Task<size_t> AsyncIO::GetFileSizeAsync(const std::string& path)
        {
            return Task<size_t>([path]() -> size_t {
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

        Task<std::chrono::system_clock::time_point> AsyncIO::GetFileModifiedTimeAsync(const std::string& path)
        {
            return Task<std::chrono::system_clock::time_point>([path]() -> std::chrono::system_clock::time_point {
                try
                {
                    auto ftime = std::filesystem::last_write_time(path);
                    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                        ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
                    return sctp;
                }
                catch (const std::filesystem::filesystem_error& e)
                {
                    throw std::runtime_error("Failed to get file modification time: " + std::string(e.what()));
                }
            });
        }
    }
} 