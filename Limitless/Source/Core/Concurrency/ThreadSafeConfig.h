#pragma once

#include "Core/ConfigManager.h"
#include "AsyncIO.h"
#include "LockFreeQueue.h"
#include <shared_mutex>
#include <atomic>
#include <future>

namespace Limitless
{
    namespace Concurrency
    {
        // Thread-safe configuration manager with async I/O support
        class ThreadSafeConfigManager
        {
        public:
            static ThreadSafeConfigManager& GetInstance();

            // Disable copy and assignment
            ThreadSafeConfigManager(const ThreadSafeConfigManager&) = delete;
            ThreadSafeConfigManager& operator=(const ThreadSafeConfigManager&) = delete;

            // Initialize with async I/O support
            void Initialize(const std::string& configFile = "config.json");
            void Shutdown();

            // Thread-safe configuration access
            template<typename T>
            void SetValue(const std::string& key, const T& value);
            
            template<typename T>
            T GetValue(const std::string& key, const T& defaultValue = T{}) const;
            
            template<typename T>
            std::optional<T> GetValueOptional(const std::string& key) const;

            bool HasValue(const std::string& key) const;
            void RemoveValue(const std::string& key);

            // Async I/O operations
            Async::Task<void> LoadFromFileAsync(const std::string& filename);
            Async::Task<void> SaveToFileAsync(const std::string& filename = "");
            Async::Task<void> ReloadFromFileAsync();

            // Hot reloading with async support
            void EnableAsyncHotReload(bool enable);
            bool IsAsyncHotReloadEnabled() const { return m_AsyncHotReloadEnabled.load(); }

            // Configuration validation
            bool ValidateConfiguration() const;
            void RegisterSchema(const std::string& key, ConfigValidator validator);

            // Change callbacks with async support
            void RegisterAsyncChangeCallback(const std::string& key, 
                                           std::function<void(const std::string&, const ConfigValue&)> callback);
            void UnregisterAsyncChangeCallback(const std::string& key);

            // Batch operations for performance
            void BeginBatchUpdate();
            void EndBatchUpdate();
            bool IsBatchUpdateActive() const { return m_BatchUpdateActive.load(); }

            // Statistics
            struct ConfigStats
            {
                size_t totalReads;
                size_t totalWrites;
                size_t totalAsyncOperations;
                size_t totalHotReloads;
                double averageReadTime;
                double averageWriteTime;
            };

            ConfigStats GetStats() const;

        private:
            ThreadSafeConfigManager() = default;
            ~ThreadSafeConfigManager() = default;

            // Internal helper methods
            void NotifyAsyncChangeCallbacks(const std::string& key, const ConfigValue& value);
            void ProcessAsyncCallbacks();
            void UpdateStats(bool isRead, double duration) const;

            // Thread-safe data structures
            mutable std::shared_mutex m_ConfigMutex;
            std::unordered_map<std::string, ConfigValue> m_Config;
            std::unordered_map<std::string, ConfigValidator> m_Validators;
            std::unordered_map<std::string, std::vector<std::function<void(const std::string&, const ConfigValue&)>>> m_AsyncCallbacks;

            // Async I/O integration
            std::string m_ConfigFile;
            std::atomic<bool> m_AsyncHotReloadEnabled{false};
            std::atomic<bool> m_BatchUpdateActive{false};
            std::vector<std::function<void()>> m_PendingCallbacks;

            // Lock-free queue for async callbacks
            LockFreeMPMCQueue<std::function<void()>, 1024> m_AsyncCallbackQueue;

            // Statistics (atomic for thread safety)
            mutable std::atomic<size_t> m_TotalReads{0};
            mutable std::atomic<size_t> m_TotalWrites{0};
            mutable std::atomic<size_t> m_TotalAsyncOperations{0};
            mutable std::atomic<size_t> m_TotalHotReloads{0};
            mutable std::atomic<double> m_TotalReadTime{0.0};
            mutable std::atomic<double> m_TotalWriteTime{0.0};

            // Background processing
            std::thread m_AsyncCallbackThread;
            std::atomic<bool> m_Shutdown{false};
        };

        // Convenience function
        inline ThreadSafeConfigManager& GetThreadSafeConfig() 
        { 
            return ThreadSafeConfigManager::GetInstance(); 
        }

        // Template implementations
        template<typename T>
        void ThreadSafeConfigManager::SetValue(const std::string& key, const T& value)
        {
            auto startTime = std::chrono::high_resolution_clock::now();

            {
                std::unique_lock<std::shared_mutex> lock(m_ConfigMutex);
                
                // Validate if schema exists
                auto validatorIt = m_Validators.find(key);
                if (validatorIt != m_Validators.end())
                {
                    ConfigValue configValue = value;
                    if (!validatorIt->second(configValue))
                    {
                        LT_WARN("Configuration value validation failed for key: {}", key);
                        return;
                    }
                }

                m_Config[key] = value;
            }

            // Update statistics
            auto endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration<double, std::micro>(endTime - startTime).count();
            UpdateStats(false, duration);

            // Notify async callbacks
            if (!m_BatchUpdateActive.load())
            {
                NotifyAsyncChangeCallbacks(key, value);
            }
            else
            {
                // Queue callback for batch processing
                m_PendingCallbacks.emplace_back([this, key, value]() {
                    NotifyAsyncChangeCallbacks(key, value);
                });
            }
        }

        template<typename T>
        T ThreadSafeConfigManager::GetValue(const std::string& key, const T& defaultValue) const
        {
            auto startTime = std::chrono::high_resolution_clock::now();

            std::shared_lock<std::shared_mutex> lock(m_ConfigMutex);
            
            auto it = m_Config.find(key);
            if (it != m_Config.end())
            {
                try
                {
                    T result = std::get<T>(it->second);
                    
                    // Update statistics
                    auto endTime = std::chrono::high_resolution_clock::now();
                    auto duration = std::chrono::duration<double, std::micro>(endTime - startTime).count();
                    UpdateStats(true, duration);
                    
                    return result;
                }
                catch (const std::bad_variant_access&)
                {
                    LT_WARN("Configuration value type mismatch for key: {}", key);
                }
            }

            // Update statistics for default value access
            auto endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration<double, std::micro>(endTime - startTime).count();
            UpdateStats(true, duration);

            return defaultValue;
        }

        template<typename T>
        std::optional<T> ThreadSafeConfigManager::GetValueOptional(const std::string& key) const
        {
            std::shared_lock<std::shared_mutex> lock(m_ConfigMutex);
            
            auto it = m_Config.find(key);
            if (it != m_Config.end())
            {
                try
                {
                    return std::get<T>(it->second);
                }
                catch (const std::bad_variant_access&)
                {
                    LT_WARN("Configuration value type mismatch for key: {}", key);
                }
            }

            return std::nullopt;
        }
    }
} 