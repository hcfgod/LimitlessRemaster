#pragma once

#include "Core/Debug/Log.h"
#include "Core/Concurrency/LockFreeQueue.h"
#include "Core/Concurrency/AsyncIO.h"
#include "Core/FileWatcher.h"
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <string>
#include <variant>
#include <memory>
#include <functional>
#include <optional>
#include <shared_mutex>
#include <atomic>
#include <future>
#include <thread>

// Forward declarations
namespace Limitless
{
    namespace Async
    {
        template<typename T>
        class Task;
    }
}

namespace Limitless
{
    // Configuration value types
    using ConfigValue = std::variant<bool, int, float, double, std::string, size_t, uint32_t>;
    
    // Configuration validation function type
    using ConfigValidator = std::function<bool(const ConfigValue&)>;
    
    // Configuration change callback
    using ConfigChangeCallback = std::function<void(const std::string&, const ConfigValue&)>;

    // Configuration constants namespace
    namespace Config
    {
        namespace Window
        {
            constexpr const char* WIDTH = "window.width";
            constexpr const char* HEIGHT = "window.height";
            constexpr const char* TITLE = "window.title";
            constexpr const char* FULLSCREEN = "window.fullscreen";
            constexpr const char* RESIZABLE = "window.resizable";
            constexpr const char* VSYNC = "window.vsync";
        }
        
        namespace Logging
        {
            constexpr const char* LEVEL = "logging.level";
            constexpr const char* FILE_ENABLED = "logging.file_enabled";
            constexpr const char* CONSOLE_ENABLED = "logging.console_enabled";
            constexpr const char* PATTERN = "logging.pattern";
            constexpr const char* DIRECTORY = "logging.directory";
            constexpr const char* MAX_FILE_SIZE = "logging.max_file_size";
            constexpr const char* MAX_FILES = "logging.max_files";
        }
        
        namespace System
        {
            constexpr const char* MAX_THREADS = "system.max_threads";
            constexpr const char* WORKING_DIRECTORY = "system.working_directory";
            constexpr const char* TEMP_DIRECTORY = "system.temp_directory";
            constexpr const char* LOG_DIRECTORY = "system.log_directory";
        }
    }

    // Thread-safe configuration manager with async I/O support
    class ConfigManager
    {
    public:
        static ConfigManager& GetInstance();

        // Disable copy and assignment
        ConfigManager(const ConfigManager&) = delete;
        ConfigManager& operator=(const ConfigManager&) = delete;

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

        // Sync wrapper methods for backward compatibility
        bool LoadFromFile(const std::string& filename);
        bool SaveToFile(const std::string& filename = "") const;
        void ReloadFromFile();

        // Hot reloading with async support
        void EnableAsyncHotReload(bool enable);
        bool IsAsyncHotReloadEnabled() const { return m_AsyncHotReloadEnabled.load(); }

        // Legacy hot reload support
        void EnableHotReload(bool enable) { EnableAsyncHotReload(enable); }
        bool IsHotReloadEnabled() const { return IsAsyncHotReloadEnabled(); }

        // Configuration validation
        bool ValidateConfiguration() const;
        void RegisterSchema(const std::string& key, ConfigValidator validator);

        // Change callbacks with async support
        void RegisterAsyncChangeCallback(const std::string& key, 
                                       std::function<void(const std::string&, const ConfigValue&)> callback);
        void UnregisterAsyncChangeCallback(const std::string& key);

        // Legacy sync callback support
        void RegisterChangeCallback(const std::string& key, ConfigChangeCallback callback);
        void UnregisterChangeCallback(const std::string& key);

        // Batch operations for performance
        void BeginBatchUpdate();
        void EndBatchUpdate();
        bool IsBatchUpdateActive() const { return m_BatchUpdateActive.load(); }

        // Legacy methods for backward compatibility
        void LoadFromEnvironment(const std::string& prefix = "LIMITLESS_");
        void LoadFromCommandLine(int argc, char* argv[], const std::string& prefix = "--");
        void ResetToDefaults();
        std::vector<std::string> GetKeys() const;
        nlohmann::json ToJson() const;
        void FromJson(const nlohmann::json& json);
        bool IsInitialized() const { return !m_Shutdown.load(); }

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
        ConfigManager() = default;
        ~ConfigManager() = default;

        // Internal helper methods
        void NotifyAsyncChangeCallbacks(const std::string& key, const ConfigValue& value);
        void ProcessAsyncCallbacks();
        void UpdateStats(bool isRead, double duration) const;
        void LoadDefaults();
        void ProcessJsonObject(const nlohmann::json& json, const std::string& prefix);

        // Thread-safe data structures
        mutable std::shared_mutex m_ConfigMutex;
        std::unordered_map<std::string, ConfigValue> m_Config;
        std::unordered_map<std::string, ConfigValidator> m_Validators;
        std::unordered_map<std::string, std::vector<std::function<void(const std::string&, const ConfigValue&)>>> m_AsyncCallbacks;
        std::unordered_map<std::string, std::vector<ConfigChangeCallback>> m_LegacyCallbacks;

        // Async I/O integration
        std::string m_ConfigFile;
        std::atomic<bool> m_AsyncHotReloadEnabled{false};
        std::atomic<bool> m_BatchUpdateActive{false};
        std::vector<std::function<void()>> m_PendingCallbacks;

        // Lock-free queue for async callbacks
        Concurrency::LockFreeMPMCQueue<std::function<void()>, 1024> m_AsyncCallbackQueue;

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
        
        // File watching for hot reload
        std::unique_ptr<FileWatcher> m_FileWatcher;
    };

    // Convenience function
    inline ConfigManager& GetConfigManager() 
    { 
        return ConfigManager::GetInstance(); 
    }

    // Template implementations
    template<typename T>
    void ConfigManager::SetValue(const std::string& key, const T& value)
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
    T ConfigManager::GetValue(const std::string& key, const T& defaultValue) const
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
                LT_CORE_WARN("Configuration value type mismatch for key: {}", key);
            }
        }

        // Update statistics for default value access
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration<double, std::micro>(endTime - startTime).count();
        UpdateStats(true, duration);

        return defaultValue;
    }

    template<typename T>
    std::optional<T> ConfigManager::GetValueOptional(const std::string& key) const
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
                LT_CORE_WARN("Configuration value type mismatch for key: {}", key);
            }
        }

        return std::nullopt;
    }
}