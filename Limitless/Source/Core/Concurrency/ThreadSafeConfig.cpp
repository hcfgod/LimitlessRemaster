#include "ThreadSafeConfig.h"
#include "Core/Debug/Log.h"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace Limitless
{
    namespace Concurrency
    {
        ThreadSafeConfigManager& ThreadSafeConfigManager::GetInstance()
        {
            static ThreadSafeConfigManager instance;
            return instance;
        }

        void ThreadSafeConfigManager::Initialize(const std::string& configFile)
        {
            m_ConfigFile = configFile;
            m_Shutdown.store(false);

            // Start async callback processing thread
            m_AsyncCallbackThread = std::thread(&ThreadSafeConfigManager::ProcessAsyncCallbacks, this);

            LT_INFO("ThreadSafeConfigManager initialized with config file: {}", configFile);
        }

        void ThreadSafeConfigManager::Shutdown()
        {
            if (m_Shutdown.load())
                return;

            LT_INFO("Shutting down ThreadSafeConfigManager...");

            m_Shutdown.store(true);

            // Wait for async callback thread to finish
            if (m_AsyncCallbackThread.joinable())
            {
                m_AsyncCallbackThread.join();
            }

            LT_INFO("ThreadSafeConfigManager shutdown complete");
        }

        bool ThreadSafeConfigManager::HasValue(const std::string& key) const
        {
            std::shared_lock<std::shared_mutex> lock(m_ConfigMutex);
            return m_Config.find(key) != m_Config.end();
        }

        void ThreadSafeConfigManager::RemoveValue(const std::string& key)
        {
            std::unique_lock<std::shared_mutex> lock(m_ConfigMutex);
            auto it = m_Config.find(key);
            if (it != m_Config.end())
            {
                ConfigValue oldValue = it->second;
                m_Config.erase(it);
                
                // Notify callbacks about removal
                NotifyAsyncChangeCallbacks(key, oldValue);
            }
        }

        Async::Task<void> ThreadSafeConfigManager::LoadFromFileAsync(const std::string& filename)
        {
            return Async::Task<void>([this, filename]() -> void {
                try
                {
                    auto configTask = Async::LoadConfigAsync(filename);
                    auto config = configTask.Get();

                    std::unique_lock<std::shared_mutex> lock(m_ConfigMutex);
                    m_Config.clear();

                    // Process nested JSON structure
                    for (auto it = config.begin(); it != config.end(); ++it)
                    {
                        if (it.value().is_object())
                        {
                            // Handle nested objects
                            for (auto nestedIt = it.value().begin(); nestedIt != it.value().end(); ++nestedIt)
                            {
                                std::string key = std::string(it.key()) + "." + std::string(nestedIt.key());
                                ConfigValue value;

                                if (nestedIt.value().is_string())
                                    value = nestedIt.value().get<std::string>();
                                else if (nestedIt.value().is_number_integer())
                                    value = nestedIt.value().get<int>();
                                else if (nestedIt.value().is_number_float())
                                    value = nestedIt.value().get<float>();
                                else if (nestedIt.value().is_boolean())
                                    value = nestedIt.value().get<bool>();
                                else if (nestedIt.value().is_number_unsigned())
                                    value = nestedIt.value().get<size_t>();

                                m_Config[key] = value;
                            }
                        }
                        else
                        {
                            // Handle flat values
                            std::string key = it.key();
                            ConfigValue value;

                            if (it.value().is_string())
                                value = it.value().get<std::string>();
                            else if (it.value().is_number_integer())
                                value = it.value().get<int>();
                            else if (it.value().is_number_float())
                                value = it.value().get<float>();
                            else if (it.value().is_boolean())
                                value = it.value().get<bool>();
                            else if (it.value().is_number_unsigned())
                                value = it.value().get<size_t>();

                            m_Config[key] = value;
                        }
                    }

                    m_TotalAsyncOperations.fetch_add(1, std::memory_order_relaxed);
                    LT_INFO("Configuration loaded from file: {} ({} entries)", filename, m_Config.size());
                }
                catch (const std::exception& e)
                {
                    LT_ERROR("Failed to load configuration from file: {} - {}", filename, e.what());
                    throw;
                }
            });
        }

        Async::Task<void> ThreadSafeConfigManager::SaveToFileAsync(const std::string& filename)
        {
            return Async::Task<void>([this, filename]() -> void {
                try
                {
                    std::shared_lock<std::shared_mutex> lock(m_ConfigMutex);

                    nlohmann::json config;
                    
                    // Convert flat configuration back to nested structure
                    for (const auto& [key, value] : m_Config)
                    {
                        size_t dotPos = key.find('.');
                        if (dotPos != std::string::npos)
                        {
                            // Nested key
                            std::string parentKey = key.substr(0, dotPos);
                            std::string childKey = key.substr(dotPos + 1);
                            
                            if (!config.contains(parentKey))
                                config[parentKey] = nlohmann::json::object();
                            
                            std::visit([&config, &parentKey, &childKey](const auto& v) {
                                config[parentKey][childKey] = v;
                            }, value);
                        }
                        else
                        {
                            // Flat key
                            std::visit([&config, &key](const auto& v) {
                                config[key] = v;
                            }, value);
                        }
                    }

                    lock.unlock();

                    auto saveTask = Async::SaveConfigAsync(filename.empty() ? m_ConfigFile : filename, config);
                    saveTask.Get();

                    m_TotalAsyncOperations.fetch_add(1, std::memory_order_relaxed);
                    LT_INFO("Configuration saved to file: {} ({} entries)", 
                           filename.empty() ? m_ConfigFile : filename, m_Config.size());
                }
                catch (const std::exception& e)
                {
                    LT_ERROR("Failed to save configuration to file: {} - {}", 
                            filename.empty() ? m_ConfigFile : filename, e.what());
                    throw;
                }
            });
        }

        Async::Task<void> ThreadSafeConfigManager::ReloadFromFileAsync()
        {
            return Async::Task<void>([this]() -> void {
                try
                {
                    auto loadTask = LoadFromFileAsync(m_ConfigFile);
                    loadTask.Get();

                    m_TotalHotReloads.fetch_add(1, std::memory_order_relaxed);
                    LT_INFO("Configuration hot reloaded from file: {}", m_ConfigFile);
                }
                catch (const std::exception& e)
                {
                    LT_ERROR("Failed to hot reload configuration: {}", e.what());
                    throw;
                }
            });
        }

        void ThreadSafeConfigManager::EnableAsyncHotReload(bool enable)
        {
            m_AsyncHotReloadEnabled.store(enable, std::memory_order_relaxed);
            LT_INFO("Async hot reload {}", enable ? "enabled" : "disabled");
        }

        bool ThreadSafeConfigManager::ValidateConfiguration() const
        {
            std::shared_lock<std::shared_mutex> lock(m_ConfigMutex);
            
            for (const auto& [key, value] : m_Config)
            {
                auto validatorIt = m_Validators.find(key);
                if (validatorIt != m_Validators.end())
                {
                    if (!validatorIt->second(value))
                    {
                        LT_ERROR("Configuration validation failed for key: {}", key);
                        return false;
                    }
                }
            }
            
            return true;
        }

        void ThreadSafeConfigManager::RegisterSchema(const std::string& key, ConfigValidator validator)
        {
            std::unique_lock<std::shared_mutex> lock(m_ConfigMutex);
            m_Validators[key] = std::move(validator);
        }

        void ThreadSafeConfigManager::RegisterAsyncChangeCallback(const std::string& key, 
                                                                 std::function<void(const std::string&, const ConfigValue&)> callback)
        {
            std::unique_lock<std::shared_mutex> lock(m_ConfigMutex);
            m_AsyncCallbacks[key].push_back(std::move(callback));
        }

        void ThreadSafeConfigManager::UnregisterAsyncChangeCallback(const std::string& key)
        {
            std::unique_lock<std::shared_mutex> lock(m_ConfigMutex);
            m_AsyncCallbacks.erase(key);
        }

        void ThreadSafeConfigManager::BeginBatchUpdate()
        {
            m_BatchUpdateActive.store(true, std::memory_order_relaxed);
            m_PendingCallbacks.clear();
        }

        void ThreadSafeConfigManager::EndBatchUpdate()
        {
            m_BatchUpdateActive.store(false, std::memory_order_relaxed);
            
            // Process all pending callbacks
            for (const auto& callback : m_PendingCallbacks)
            {
                if (callback)
                    callback();
            }
            m_PendingCallbacks.clear();
        }

        ThreadSafeConfigManager::ConfigStats ThreadSafeConfigManager::GetStats() const
        {
            ConfigStats stats;
            stats.totalReads = m_TotalReads.load(std::memory_order_relaxed);
            stats.totalWrites = m_TotalWrites.load(std::memory_order_relaxed);
            stats.totalAsyncOperations = m_TotalAsyncOperations.load(std::memory_order_relaxed);
            stats.totalHotReloads = m_TotalHotReloads.load(std::memory_order_relaxed);
            
            double totalReadTime = m_TotalReadTime.load(std::memory_order_relaxed);
            double totalWriteTime = m_TotalWriteTime.load(std::memory_order_relaxed);
            
            stats.averageReadTime = stats.totalReads > 0 ? totalReadTime / stats.totalReads : 0.0;
            stats.averageWriteTime = stats.totalWrites > 0 ? totalWriteTime / stats.totalWrites : 0.0;
            
            return stats;
        }

        void ThreadSafeConfigManager::NotifyAsyncChangeCallbacks(const std::string& key, const ConfigValue& value)
        {
            std::shared_lock<std::shared_mutex> lock(m_ConfigMutex);
            
            auto it = m_AsyncCallbacks.find(key);
            if (it != m_AsyncCallbacks.end())
            {
                for (const auto& callback : it->second)
                {
                                // Queue callback for async execution
            if (!m_AsyncCallbackQueue.TryPush([callback, key, value]() {
                try
                {
                    if (callback)
                        callback(key, value);
                }
                catch (const std::exception& e)
                {
                    LT_ERROR("Exception in async config callback: {}", e.what());
                }
            }))
                    {
                        LT_WARN("Async callback queue is full, dropping callback for key: {}", key);
                    }
                }
            }
        }

        void ThreadSafeConfigManager::ProcessAsyncCallbacks()
        {
            while (!m_Shutdown.load())
            {
                auto callbackResult = m_AsyncCallbackQueue.TryPop();
                if (callbackResult.has_value())
                {
                    try
                    {
                        auto& callback = callbackResult.value();
                        if (callback)
                            callback();
                    }
                    catch (const std::exception& e)
                    {
                        LT_ERROR("Exception in async callback processing: {}", e.what());
                    }
                }
                else
                {
                    std::this_thread::yield();
                }
            }
        }

        void ThreadSafeConfigManager::UpdateStats(bool isRead, double duration) const
        {
            if (isRead)
            {
                m_TotalReads.fetch_add(1, std::memory_order_relaxed);
                double currentReadTime = m_TotalReadTime.load(std::memory_order_relaxed);
                m_TotalReadTime.store(currentReadTime + duration, std::memory_order_relaxed);
            }
            else
            {
                m_TotalWrites.fetch_add(1, std::memory_order_relaxed);
                double currentWriteTime = m_TotalWriteTime.load(std::memory_order_relaxed);
                m_TotalWriteTime.store(currentWriteTime + duration, std::memory_order_relaxed);
            }
        }
    }
} 