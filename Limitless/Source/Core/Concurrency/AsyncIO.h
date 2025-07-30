#pragma once
#include "Core/Debug/Log.h"
#include "LockFreeQueue.h"
#include <future>
#include <thread>
#include <vector>
#include <functional>
#include <memory>
#include <string>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <nlohmann/json.hpp>

// Remove coroutine dependencies for now - using std::function approach

namespace Limitless
{
    namespace Async
    {
        // Forward declarations
        template<typename T>
        class Task;
        class AsyncIO;

        // Task promise type for async operations (simplified)
        template<typename T>
        struct TaskPromise
        {
            Task<T> get_return_object() noexcept { return Task<T>(); }
            void return_value(T value) noexcept { m_Value = std::move(value); }
            void unhandled_exception() noexcept { m_Exception = std::current_exception(); }

            T m_Value;
            std::exception_ptr m_Exception;
        };

        // Simple task wrapper for async operations
        template<typename T>
        class Task
        {
        public:
            Task() = default;
            Task(std::function<T()> func) : m_Function(std::move(func)) {}

            // Wait for completion and get result
            T Get()
            {
                if (!m_Function)
                    throw std::runtime_error("Task is not valid");

                try
                {
                    return m_Function();
                }
                catch (...)
                {
                    m_Exception = std::current_exception();
                    std::rethrow_exception(m_Exception);
                }
            }

            // Check if task is done
            bool IsDone() const
            {
                return m_Function == nullptr;
            }

            // Wait for completion
            void Wait()
            {
                if (m_Function)
                {
                    Get();
                }
            }

        private:
            std::function<T()> m_Function;
            std::exception_ptr m_Exception;
        };

        // Specialization for void tasks
        template<>
        class Task<void>
        {
        public:
            Task() = default;
            Task(std::function<void()> func) : m_Function(std::move(func)) {}

            void Get()
            {
                if (!m_Function)
                    throw std::runtime_error("Task is not valid");

                try
                {
                    m_Function();
                }
                catch (...)
                {
                    m_Exception = std::current_exception();
                    std::rethrow_exception(m_Exception);
                }
            }

            bool IsDone() const
            {
                return m_Function == nullptr;
            }

            void Wait()
            {
                if (m_Function)
                {
                    Get();
                }
            }

        private:
            std::function<void()> m_Function;
            std::exception_ptr m_Exception;
        };

        // Async I/O manager
        class AsyncIO
        {
        public:
            static AsyncIO& GetInstance();

            // Initialize the async I/O system
            void Initialize(size_t threadCount = 0);
            void Shutdown();

            // File operations
            Task<std::string> ReadFileAsync(const std::string& path);
            Task<void> WriteFileAsync(const std::string& path, const std::string& content);
            Task<bool> FileExistsAsync(const std::string& path);
            Task<std::vector<std::string>> ReadLinesAsync(const std::string& path);
            Task<void> AppendFileAsync(const std::string& path, const std::string& content);

            // Directory operations
            Task<std::vector<std::string>> ListDirectoryAsync(const std::string& path);
            Task<bool> CreateDirectoryAsync(const std::string& path);
            Task<bool> DeleteFileAsync(const std::string& path);
            Task<bool> DeleteDirectoryAsync(const std::string& path);

            // Configuration operations
            Task<void> SaveConfigAsync(const std::string& path, const nlohmann::json& config);
            Task<nlohmann::json> LoadConfigAsync(const std::string& path);

            // Utility operations
            Task<size_t> GetFileSizeAsync(const std::string& path);
            Task<std::chrono::system_clock::time_point> GetFileModifiedTimeAsync(const std::string& path);

            // Thread pool management
            size_t GetThreadCount() const { return m_Threads.size(); }
            bool IsInitialized() const { return m_Initialized; }

        private:
            AsyncIO() = default;
            ~AsyncIO() = default;

            void WorkerThread();
            void EnqueueTask(std::function<void()> task);

            std::vector<std::thread> m_Threads;
            Concurrency::LockFreeMPMCQueue<std::function<void()>, 8192> m_TaskQueue;
            std::atomic<bool> m_Shutdown{false};
            std::atomic<bool> m_Initialized{false};
        };

        // Convenience functions
        inline AsyncIO& GetAsyncIO() { return AsyncIO::GetInstance(); }

        // Async file reading
        inline Task<std::string> ReadFileAsync(const std::string& path)
        {
            return GetAsyncIO().ReadFileAsync(path);
        }

        // Async file writing
        inline Task<void> WriteFileAsync(const std::string& path, const std::string& content)
        {
            return GetAsyncIO().WriteFileAsync(path, content);
        }

        // Async configuration loading
        inline Task<nlohmann::json> LoadConfigAsync(const std::string& path)
        {
            return GetAsyncIO().LoadConfigAsync(path);
        }

        // Async configuration saving
        inline Task<void> SaveConfigAsync(const std::string& path, const nlohmann::json& config)
        {
            return GetAsyncIO().SaveConfigAsync(path, config);
        }

        // Async directory listing
        inline Task<std::vector<std::string>> ListDirectoryAsync(const std::string& path)
        {
            return GetAsyncIO().ListDirectoryAsync(path);
        }

        // Async file existence check
        inline Task<bool> FileExistsAsync(const std::string& path)
        {
            return GetAsyncIO().FileExistsAsync(path);
        }

        // Async file size
        inline Task<size_t> GetFileSizeAsync(const std::string& path)
        {
            return GetAsyncIO().GetFileSizeAsync(path);
        }

        // Async file modification time
        inline Task<std::chrono::system_clock::time_point> GetFileModifiedTimeAsync(const std::string& path)
        {
            return GetAsyncIO().GetFileModifiedTimeAsync(path);
        }

        // Utility function to run async operations synchronously (for compatibility)
        template<typename T>
        T RunSync(Task<T>&& task)
        {
            task.Wait();
            return task.Get();
        }

        // Utility function to run async operations with callback
        template<typename T>
        void RunAsync(Task<T>&& task, std::function<void(T)> callback)
        {
            std::thread([task = std::move(task), callback = std::move(callback)]() mutable {
                try
                {
                    T result = task.Get();
                    callback(std::move(result));
                }
                catch (const std::exception& e)
                {
                    LT_ERROR("Async operation failed: {}", e.what());
                }
            }).detach();
        }

        // Utility function to run async operations with error handling
        template<typename T>
        void RunAsyncWithError(Task<T>&& task, 
                              std::function<void(T)> successCallback,
                              std::function<void(const std::exception&)> errorCallback)
        {
            std::thread([task = std::move(task), 
                        successCallback = std::move(successCallback),
                        errorCallback = std::move(errorCallback)]() mutable {
                try
                {
                    T result = task.Get();
                    successCallback(std::move(result));
                }
                catch (const std::exception& e)
                {
                    errorCallback(e);
                }
            }).detach();
        }
    }
}

// Template implementations removed - using std::function approach 