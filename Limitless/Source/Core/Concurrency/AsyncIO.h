#pragma once
#include "Core/Error.h"
#include "Core/Debug/Log.h"
#include <atomic>
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
#include <type_traits>
#include <utility>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <nlohmann/json.hpp>

// NOTE:
// This module provides a real asynchronous I/O scheduler:
// - Work is enqueued into a lock-free queue
// - A dedicated worker thread pool executes the work
// - Callers receive a future-backed Task that can be waited/queried

namespace Limitless
{
    namespace Async
    {
        // Future-backed task wrapper for asynchronous operations.
        template<typename T>
        class Task
        {
        public:
            Task() = default;
            explicit Task(std::shared_future<T> future) : m_Future(std::move(future)) {}

            // Convenience: create a standalone asynchronous task executed by std::async.
            // Prefer AsyncIO methods for I/O work so tasks run on the engine's thread pool.
            template<typename Func, typename = std::enable_if_t<std::is_invocable_r_v<T, Func>>>
            explicit Task(Func&& func)
                : m_Future(std::async(std::launch::async, std::forward<Func>(func)).share())
            {
            }

            bool IsValid() const noexcept { return m_Future.valid(); }

            // Check if task is done (ready).
            bool IsDone() const
            {
                if (!m_Future.valid())
                    return false;
                return m_Future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
            }

            void Wait() const
            {
                if (!m_Future.valid())
                    throw std::runtime_error("Task is not valid");
                m_Future.wait();
            }

            // Get the result (blocks until ready). Exceptions propagate from the worker thread.
            T Get() const
            {
                if (!m_Future.valid())
                    throw std::runtime_error("Task is not valid");
                return T(m_Future.get());
            }

        private:
            std::shared_future<T> m_Future;
        };

        template<>
        class Task<void>
        {
        public:
            Task() = default;
            explicit Task(std::shared_future<void> future) : m_Future(std::move(future)) {}

            template<typename Func, typename = std::enable_if_t<std::is_invocable_r_v<void, Func>>>
            explicit Task(Func&& func)
                : m_Future(std::async(std::launch::async, std::forward<Func>(func)).share())
            {
            }

            bool IsValid() const noexcept { return m_Future.valid(); }

            bool IsDone() const
            {
                if (!m_Future.valid())
                    return false;
                return m_Future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
            }

            void Wait() const
            {
                if (!m_Future.valid())
                    throw std::runtime_error("Task is not valid");
                m_Future.wait();
            }

            void Get() const
            {
                if (!m_Future.valid())
                    throw std::runtime_error("Task is not valid");
                m_Future.get();
            }

        private:
            std::shared_future<void> m_Future;
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

            // Result-returning variants (preferred for engine code).
            // These never throw across the async boundary; failures are returned as Result<T>.
            Task<Result<std::string>> ReadFileAsyncResult(const std::string& path);
            Task<Result<void>> WriteFileAsyncResult(const std::string& path, const std::string& content);
            Task<Result<void>> AppendFileAsyncResult(const std::string& path, const std::string& content);

            // Directory operations
            Task<std::vector<std::string>> ListDirectoryAsync(const std::string& path);
            Task<bool> CreateDirectoryAsync(const std::string& path);
            Task<bool> DeleteFileAsync(const std::string& path);
            Task<bool> DeleteDirectoryAsync(const std::string& path);

            // Configuration operations
            Task<void> SaveConfigAsync(const std::string& path, const nlohmann::json& config);
            Task<nlohmann::json> LoadConfigAsync(const std::string& path);

            Task<Result<void>> SaveConfigAsyncResult(const std::string& path, const nlohmann::json& config);
            Task<Result<nlohmann::json>> LoadConfigAsyncResult(const std::string& path);

            // Utility operations
            Task<size_t> GetFileSizeAsync(const std::string& path);
            Task<std::filesystem::file_time_type> GetFileModifiedTimeAsync(const std::string& path);

            // General-purpose async execution on the AsyncIO worker pool.
            // This is useful for CPU-side asset processing (decode, parsing) where you want
            // the engine-managed worker threads (not std::async).
            template<typename Func>
            auto RunAsync(Func&& func) -> Task<std::invoke_result_t<Func>>
            {
                return Submit(std::forward<Func>(func));
            }

            // Thread pool management
            size_t GetThreadCount() const { return m_Threads.size(); }
            bool IsInitialized() const { return m_Initialized.load(); }

        private:
            AsyncIO() = default;
            ~AsyncIO()
            {
                // Ensure we never hit std::terminate due to joinable threads during
                // static deinitialization / process exit.
                Shutdown();
            }

            void WorkerThread();
            void EnqueueTask(std::function<void()> task);

            template<typename Func>
            auto Submit(Func&& func) -> Task<std::invoke_result_t<Func>>
            {
                using ReturnType = std::invoke_result_t<Func>;

                auto packagedTask = std::make_shared<std::packaged_task<ReturnType()>>(std::forward<Func>(func));
                std::shared_future<ReturnType> future = packagedTask->get_future().share();

                // If AsyncIO is not initialized (or is shutting down), execute inline.
                // This is critical for shutdown paths (ex: ConfigManager final save) where
                // re-initializing a worker pool can create joinable threads that outlive main().
                if (!m_Initialized.load() || m_Shutdown.load() || !m_AcceptingTasks.load())
                {
                    (*packagedTask)();
                    return Task<ReturnType>(std::move(future));
                }

                EnqueueTask([packagedTask]() mutable { (*packagedTask)(); });
                return Task<ReturnType>(std::move(future));
            }

            std::vector<std::thread> m_Threads;

            // IMPORTANT:
            // The project contains experimental lock-free queues. The current MPMC queue implementation
            // is not safe for a work-stealing style scheduler because it does not protect against a
            // consumer observing the tail reservation before the producer stores the element.
            //
            // AsyncIO correctness is higher priority than lock-free behavior, so we use a bounded,
            // blocking MPMC queue here.
            std::mutex m_TaskMutex;
            std::condition_variable m_TaskCv;
            std::deque<std::function<void()>> m_TaskQueue;
            size_t m_MaxQueueSize = 8192;
            std::atomic<bool> m_Shutdown{false};
            std::atomic<bool> m_Initialized{false};
            std::atomic<bool> m_AcceptingTasks{false};
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
        inline Task<std::filesystem::file_time_type> GetFileModifiedTimeAsync(const std::string& path)
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
                    LT_CORE_ERROR("Async operation failed: {}", e.what());
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

// Template implementations are header-only by design.  