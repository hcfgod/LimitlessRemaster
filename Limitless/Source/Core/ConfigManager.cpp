#include "ConfigManager.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <iomanip>

// Disable warning about getenv being unsafe (we're using it safely)
#ifdef _MSC_VER
#pragma warning(disable: 4996)
#endif

namespace Limitless
{
    ConfigManager& ConfigManager::GetInstance()
    {
        static ConfigManager instance;
        return instance;
    }

    void ConfigManager::Initialize(const std::string& configFile)
    {
        LT_CORE_INFO("ConfigManager::Initialize called with configFile: {}", configFile);
        
        m_ConfigFile = configFile;
        m_Shutdown.store(false);

        LT_CORE_INFO("ConfigManager::Initialize: Config file set to: {}", m_ConfigFile);

        // Load defaults first
        LoadDefaults();

        // Start async callback processing thread (only in non-test mode)
        m_AsyncCallbackThread = std::thread(&ConfigManager::ProcessAsyncCallbacks, this);

        // Try to load from file if it exists
        if (std::filesystem::exists(configFile))
        {
            LT_CORE_INFO("ConfigManager::Initialize: Config file exists, loading...");
            try
            {
                LoadFromFileAsync(configFile).Get();
                LT_CORE_INFO("ConfigManager::Initialize: Config file loaded successfully");
            }
            catch (const std::exception& e)
            {
                LT_CORE_ERROR("Failed to load configuration from file: {} - {}", configFile, e.what());
            }
        }
        else
        {
            LT_CORE_INFO("Configuration file not found, using defaults: {}", configFile);
        }

        // Load from environment variables
        LoadFromEnvironment();

        LT_CORE_INFO("ConfigManager initialized with config file: {}", configFile);
    }

    void ConfigManager::Shutdown()
    {
        if (m_Shutdown.load())
            return;

        LT_CORE_INFO("Shutting down ConfigManager...");

        m_Shutdown.store(true);

        // Stop hot reload if enabled
        if (m_AsyncHotReloadEnabled.load() && m_FileWatcher)
        {
            m_FileWatcher->StopWatching();
            m_FileWatcher.reset();
        }

        // Wait for async callback thread to finish
        if (m_AsyncCallbackThread.joinable())
        {
            m_AsyncCallbackThread.join();
        }

        // Save current configuration
        if (!m_ConfigFile.empty())
        {
            try
            {
                SaveToFileAsync(m_ConfigFile).Get();
            }
            catch (const std::exception& e)
            {
                LT_CORE_ERROR("Failed to save configuration to file: {} - {}", m_ConfigFile, e.what());
            }
        }

        LT_CORE_INFO("ConfigManager shutdown complete");
    }

    bool ConfigManager::HasValue(const std::string& key) const
    {
        std::shared_lock<std::shared_mutex> lock(m_ConfigMutex);
        return m_Config.find(key) != m_Config.end();
    }

    void ConfigManager::RemoveValue(const std::string& key)
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

    Async::Task<void> ConfigManager::LoadFromFileAsync(const std::string& filename)
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
                            {
                                int intValue = nestedIt.value().get<int>();
                                std::string fullKey = std::string(it.key()) + "." + std::string(nestedIt.key());
                                
                                // Apply type-specific logic
                                if (fullKey.find("max_threads") != std::string::npos)
                                {
                                    value = static_cast<size_t>(intValue);
                                }
                                else if (fullKey.find("width") != std::string::npos || 
                                         fullKey.find("height") != std::string::npos ||
                                         fullKey.find("max_width") != std::string::npos ||
                                         fullKey.find("max_height") != std::string::npos ||
                                         fullKey.find("min_width") != std::string::npos ||
                                         fullKey.find("min_height") != std::string::npos)
                                {
                                    value = static_cast<uint32_t>(intValue);
                                }
                                else
                                {
                                    value = intValue;
                                }
                            }
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
                        {
                            int intValue = it.value().get<int>();
                            std::string key = it.key();
                            
                            // Apply type-specific logic for flat keys
                            if (key.find("max_threads") != std::string::npos)
                            {
                                value = static_cast<size_t>(intValue);
                            }
                            else if (key.find("width") != std::string::npos || 
                                     key.find("height") != std::string::npos ||
                                     key.find("max_width") != std::string::npos ||
                                     key.find("max_height") != std::string::npos ||
                                     key.find("min_width") != std::string::npos ||
                                     key.find("min_height") != std::string::npos)
                            {
                                value = static_cast<uint32_t>(intValue);
                            }
                            else
                            {
                                value = intValue;
                            }
                        }
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
                LT_CORE_INFO("Configuration loaded from file: {} ({} entries)", filename, m_Config.size());
            }
            catch (const std::exception& e)
            {
                LT_CORE_ERROR("Failed to load configuration from file: {} - {}", filename, e.what());
                throw;
            }
        });
    }

    Async::Task<void> ConfigManager::SaveToFileAsync(const std::string& filename)
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
                LT_CORE_INFO("Configuration saved to file: {} ({} entries)", 
                       filename.empty() ? m_ConfigFile : filename, m_Config.size());
            }
            catch (const std::exception& e)
            {
                LT_CORE_ERROR("Failed to save configuration to file: {} - {}", 
                        filename.empty() ? m_ConfigFile : filename, e.what());
                throw;
            }
        });
    }

    Async::Task<void> ConfigManager::ReloadFromFileAsync()
    {
        return Async::Task<void>([this]() -> void {
            try
            {
                auto loadTask = LoadFromFileAsync(m_ConfigFile);
                loadTask.Get();

                m_TotalHotReloads.fetch_add(1, std::memory_order_relaxed);
                LT_CORE_INFO("Configuration hot reloaded from file: {}", m_ConfigFile);
            }
            catch (const std::exception& e)
            {
                LT_CORE_ERROR("Failed to hot reload configuration: {}", e.what());
                throw;
            }
        });
    }

    // Sync wrapper methods
    bool ConfigManager::LoadFromFile(const std::string& filename)
    {
        try
        {
            LoadFromFileAsync(filename).Get();
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool ConfigManager::SaveToFile(const std::string& filename) const
    {
        try
        {
            const_cast<ConfigManager*>(this)->SaveToFileAsync(filename).Get();
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    void ConfigManager::ReloadFromFile()
    {
        LT_CORE_INFO("ConfigManager::ReloadFromFile called");
        
        if (m_ConfigFile.empty())
        {
            LT_CORE_WARN("ConfigManager::ReloadFromFile: Config file is empty, cannot reload");
            return;
        }

        LT_CORE_INFO("ConfigManager: Reloading configuration from {}", m_ConfigFile);

        // Store current values to detect changes
        std::unordered_map<std::string, ConfigValue> oldValues;
        {
            std::shared_lock<std::shared_mutex> lock(m_ConfigMutex);
            oldValues = m_Config;
            LT_CORE_INFO("ConfigManager::ReloadFromFile: Stored {} current values", oldValues.size());
        }

        // Reload from file
        try
        {
            LT_CORE_INFO("ConfigManager::ReloadFromFile: Starting async load");
            LoadFromFileAsync(m_ConfigFile).Get();
            LT_CORE_INFO("ConfigManager::ReloadFromFile: Async load completed");
            
            // Notify change callbacks for any changed values
            std::shared_lock<std::shared_mutex> lock(m_ConfigMutex);
            int changedCount = 0;
            for (const auto& [key, newValue] : m_Config)
            {
                auto it = oldValues.find(key);
                if (it == oldValues.end() || it->second != newValue)
                {
                    LT_CORE_INFO("ConfigManager: Value changed for key '{}'", key);
                    NotifyAsyncChangeCallbacks(key, newValue);
                    changedCount++;
                }
            }

            // Also check for removed values
            int removedCount = 0;
            for (const auto& [key, oldValue] : oldValues)
            {
                if (m_Config.find(key) == m_Config.end())
                {
                    LT_CORE_INFO("ConfigManager: Key removed '{}'", key);
                    removedCount++;
                    // You could add a special "removed" callback here if needed
                }
            }

            LT_CORE_INFO("ConfigManager: Configuration reloaded successfully - {} changed, {} removed", changedCount, removedCount);
        }
        catch (const std::exception& e)
        {
            LT_CORE_ERROR("ConfigManager: Failed to reload configuration from {} - {}", m_ConfigFile, e.what());
        }
    }

    void ConfigManager::EnableAsyncHotReload(bool enable)
    {
        LT_CORE_INFO("ConfigManager::EnableAsyncHotReload called with enable={}", enable);
        
        if (m_AsyncHotReloadEnabled.load() == enable)
        {
            LT_CORE_INFO("ConfigManager::EnableAsyncHotReload: Already in desired state, returning");
            return;
        }

        m_AsyncHotReloadEnabled.store(enable, std::memory_order_relaxed);

        if (enable && !m_ConfigFile.empty())
        {
            LT_CORE_INFO("ConfigManager::EnableAsyncHotReload: Enabling hot reload for file: {}", m_ConfigFile);
            
            // Create file watcher if it doesn't exist
            if (!m_FileWatcher)
            {
                LT_CORE_INFO("ConfigManager::EnableAsyncHotReload: Creating new FileWatcher");
                m_FileWatcher = std::make_unique<FileWatcher>();
            }

            // Start watching the config file
            m_FileWatcher->StartWatching(m_ConfigFile, [this](const std::string& filepath) {
                LT_CORE_INFO("ConfigManager: Hot reload triggered for {}", filepath);
                ReloadFromFile();
            });

            LT_CORE_INFO("ConfigManager: Async hot reload enabled for {}", m_ConfigFile);
        }
        else if (!enable && m_FileWatcher)
        {
            LT_CORE_INFO("ConfigManager::EnableAsyncHotReload: Disabling hot reload");
            // Stop watching
            m_FileWatcher->StopWatching();
            LT_CORE_INFO("ConfigManager: Async hot reload disabled");
        }
        else
        {
            LT_CORE_WARN("ConfigManager::EnableAsyncHotReload: Cannot enable hot reload - config file is empty or FileWatcher not available");
        }
    }

    bool ConfigManager::ValidateConfiguration() const
    {
        std::shared_lock<std::shared_mutex> lock(m_ConfigMutex);
        
        for (const auto& [key, value] : m_Config)
        {
            auto validatorIt = m_Validators.find(key);
            if (validatorIt != m_Validators.end())
            {
                if (!validatorIt->second(value))
                {
                    LT_CORE_ERROR("Configuration validation failed for key: {}", key);
                    return false;
                }
            }
        }
        
        return true;
    }

    void ConfigManager::RegisterSchema(const std::string& key, ConfigValidator validator)
    {
        std::unique_lock<std::shared_mutex> lock(m_ConfigMutex);
        m_Validators[key] = std::move(validator);
    }

    void ConfigManager::RegisterAsyncChangeCallback(const std::string& key, 
                                                   std::function<void(const std::string&, const ConfigValue&)> callback)
    {
        std::unique_lock<std::shared_mutex> lock(m_ConfigMutex);
        m_AsyncCallbacks[key].push_back(std::move(callback));
    }

    void ConfigManager::UnregisterAsyncChangeCallback(const std::string& key)
    {
        std::unique_lock<std::shared_mutex> lock(m_ConfigMutex);
        m_AsyncCallbacks.erase(key);
    }

    void ConfigManager::RegisterChangeCallback(const std::string& key, ConfigChangeCallback callback)
    {
        std::unique_lock<std::shared_mutex> lock(m_ConfigMutex);
        m_LegacyCallbacks[key].push_back(std::move(callback));
    }

    void ConfigManager::UnregisterChangeCallback(const std::string& key)
    {
        std::unique_lock<std::shared_mutex> lock(m_ConfigMutex);
        m_LegacyCallbacks.erase(key);
    }

    void ConfigManager::BeginBatchUpdate()
    {
        m_BatchUpdateActive.store(true, std::memory_order_relaxed);
        m_PendingCallbacks.clear();
    }

    void ConfigManager::EndBatchUpdate()
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

    void ConfigManager::LoadFromEnvironment(const std::string& prefix)
    {
        // This is a simplified implementation
        // In a full implementation, you'd iterate through all environment variables
        // and look for ones starting with the prefix
        
        const char* envVars[] = 
        {
            "LIMITLESS_LOG_LEVEL",
            "LIMITLESS_WINDOW_WIDTH",
            "LIMITLESS_WINDOW_HEIGHT",
            "LIMITLESS_FULLSCREEN"
        };
        
        for (const char* envVar : envVars)
        {
            const char* value = std::getenv(envVar);
            if (value)
            {
                std::string key = envVar;
                std::transform(key.begin(), key.end(), key.begin(), ::tolower);
                std::replace(key.begin(), key.end(), '_', '.');
                
                // Try to convert to appropriate type
                std::string strValue(value);
                if (strValue == "true" || strValue == "false")
                {
                    SetValue(key, strValue == "true");
                }
                else if (std::all_of(strValue.begin(), strValue.end(), ::isdigit))
                {
                    SetValue(key, std::stoi(strValue));
                }
                else
                {
                    SetValue(key, strValue);
                }  
            }
        }
    }

    void ConfigManager::LoadFromCommandLine(int argc, char* argv[], const std::string& prefix)
    {
        for (int i = 1; i < argc; ++i)
        {
            std::string arg(argv[i]);
            
            if (arg.substr(0, prefix.length()) == prefix)
            {
                std::string keyValue = arg.substr(prefix.length());
                size_t equalPos = keyValue.find('=');
                
                if (equalPos != std::string::npos)
                {
                    std::string key = keyValue.substr(0, equalPos);
                    std::string value = keyValue.substr(equalPos + 1);
                    
                    // Convert key format (e.g., "window-width" -> "window.width")
                    std::replace(key.begin(), key.end(), '-', '.');
                    
                    // Try to convert to appropriate type
                    if (value == "true" || value == "false")
                    {
                        SetValue(key, value == "true");
                    }
                    else if (std::all_of(value.begin(), value.end(), ::isdigit))
                    {
                        SetValue(key, std::stoi(value));
                    }
                    else
                    {
                        SetValue(key, value);
                    }  
                }
            }
        }
    }

    void ConfigManager::ResetToDefaults()
    {
        LoadDefaults();
    }

    std::vector<std::string> ConfigManager::GetKeys() const
    {
        std::shared_lock<std::shared_mutex> lock(m_ConfigMutex);
        
        std::vector<std::string> keys;
        keys.reserve(m_Config.size());
        
        for (const auto& [key, _] : m_Config)
        {
            keys.push_back(key);
        }
        
        return keys;
    }

    nlohmann::json ConfigManager::ToJson() const
    {
        std::shared_lock<std::shared_mutex> lock(m_ConfigMutex);
        
        nlohmann::json json;
        
        for (const auto& [key, value] : m_Config)
        {
            // Split the key by dots to reconstruct nested structure
            std::vector<std::string> keyParts;
            std::string currentKey;
            for (char c : key) {
                if (c == '.') {
                    keyParts.push_back(currentKey);
                    currentKey.clear();
                } else {
                    currentKey += c;
                }
            }
            keyParts.push_back(currentKey);
            
            // Navigate to the correct nested location
            nlohmann::json* current = &json;
            for (size_t i = 0; i < keyParts.size() - 1; ++i) {
                if (!current->contains(keyParts[i])) {
                    (*current)[keyParts[i]] = nlohmann::json::object();
                }
                current = &(*current)[keyParts[i]];
            }
            
            // Set the value at the final location
            std::visit([current, &keyParts](const auto& v) 
            {
                (*current)[keyParts.back()] = v;
            }, value);
        }
        
        return json;
    }

    void ConfigManager::FromJson(const nlohmann::json& json)
    {
        std::unique_lock<std::shared_mutex> lock(m_ConfigMutex);
        
        LT_CORE_INFO("Loading configuration from JSON with {} items", json.size());
        
        ProcessJsonObject(json, "");
        
        LT_CORE_INFO("Configuration loading complete. Total values: {}", m_Config.size());
    }

    void ConfigManager::ProcessJsonObject(const nlohmann::json& json, const std::string& prefix)
    {
        for (const auto& [key, value] : json.items())
        {
            std::string fullKey = prefix.empty() ? key : prefix + "." + key;
            
            try
            {
                if (value.is_object())
                {
                    // Recursively process nested objects
                    ProcessJsonObject(value, fullKey);
                }
                else if (value.is_boolean())
                {
                    m_Config[fullKey] = value.get<bool>();
                    LT_CORE_DEBUG("Loaded config {} = {}", fullKey, (value.get<bool>() ? "true" : "false"));
                }
                else if (value.is_number_integer())
                {
                    int intValue = value.get<int>();
                    if (intValue >= 0)
                    {
                        // Determine the appropriate type based on the key
                        if (fullKey.find("max_file_size") != std::string::npos || 
                            fullKey.find("max_files") != std::string::npos)
                        {
                            m_Config[fullKey] = static_cast<size_t>(intValue);
                            LT_CORE_DEBUG("Loaded config {} = {} (as size_t)", fullKey, intValue);
                        }
                        else if (fullKey.find("width") != std::string::npos || 
                                 fullKey.find("height") != std::string::npos ||
                                 fullKey.find("max_width") != std::string::npos ||
                                 fullKey.find("max_height") != std::string::npos ||
                                 fullKey.find("min_width") != std::string::npos ||
                                 fullKey.find("min_height") != std::string::npos)
                        {
                            m_Config[fullKey] = static_cast<uint32_t>(intValue);
                            LT_CORE_DEBUG("Loaded config {} = {} (as uint32_t)", fullKey, intValue);
                        }
                        else if (fullKey.find("max_threads") != std::string::npos)
                        {
                            m_Config[fullKey] = static_cast<size_t>(intValue);
                            LT_CORE_DEBUG("Loaded config {} = {} (as size_t)", fullKey, intValue);
                        }
                        else
                        {
                            // Default to size_t for other positive integers
                            m_Config[fullKey] = static_cast<size_t>(intValue);
                            LT_CORE_DEBUG("Loaded config {} = {} (as size_t)", fullKey, intValue);
                        }
                    }
                    else
                    {
                        m_Config[fullKey] = intValue;
                        LT_CORE_DEBUG("Loaded config {} = {} (as int)", fullKey, intValue);
                    }
                }
                else if (value.is_number_float())
                {
                    m_Config[fullKey] = value.get<double>();
                    LT_CORE_DEBUG("Loaded config {} = {}", fullKey, value.get<double>());
                }
                else if (value.is_string())
                {
                    m_Config[fullKey] = value.get<std::string>();
                    LT_CORE_DEBUG("Loaded config {} = {}", fullKey, value.get<std::string>());
                }
                else
                {
                    LT_CORE_WARN("Unknown config value type for key: {}", fullKey);
                }
            }
            catch (const std::exception& e)
            {
                LT_CORE_ERROR("Error parsing key {}: {}", fullKey, e.what());
                continue; // Skip invalid entries
            }
        }
    }

    void ConfigManager::LoadDefaults()
    {
        std::unique_lock<std::shared_mutex> lock(m_ConfigMutex);
        
        // Window defaults
        m_Config[Config::Window::WIDTH] = static_cast<uint32_t>(1280);
        m_Config[Config::Window::HEIGHT] = static_cast<uint32_t>(720);
        m_Config[Config::Window::TITLE] = std::string("Limitless Engine");
        m_Config[Config::Window::FULLSCREEN] = false;
        m_Config[Config::Window::VSYNC] = false;
        m_Config[Config::Window::RESIZABLE] = true;

        // Logging defaults
        m_Config[Config::Logging::LEVEL] = std::string("info");
        m_Config[Config::Logging::FILE_ENABLED] = true;
        m_Config[Config::Logging::CONSOLE_ENABLED] = true;
        m_Config[Config::Logging::PATTERN] = std::string("[%T] [%l] %n: %v");
        m_Config[Config::Logging::DIRECTORY] = std::string("logs");
        m_Config[Config::Logging::MAX_FILE_SIZE] = static_cast<size_t>(50 * 1024 * 1024); // 50MB
        m_Config[Config::Logging::MAX_FILES] = static_cast<size_t>(10);

        // System defaults
        m_Config[Config::System::MAX_THREADS] = static_cast<size_t>(std::thread::hardware_concurrency());
        m_Config[Config::System::WORKING_DIRECTORY] = std::string(".");
        m_Config[Config::System::TEMP_DIRECTORY] = std::string("temp");
        m_Config[Config::System::LOG_DIRECTORY] = std::string("logs");
    }

    ConfigManager::ConfigStats ConfigManager::GetStats() const
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

    void ConfigManager::NotifyAsyncChangeCallbacks(const std::string& key, const ConfigValue& value)
    {
        std::shared_lock<std::shared_mutex> lock(m_ConfigMutex);
        
        // Notify async callbacks
        auto asyncIt = m_AsyncCallbacks.find(key);
        if (asyncIt != m_AsyncCallbacks.end())
        {
            for (const auto& callback : asyncIt->second)
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
                        LT_CORE_ERROR("Exception in async config callback: {}", e.what());
                    }
                }))
                {
                    LT_CORE_WARN("Async callback queue is full, dropping callback for key: {}", key);
                }
            }
        }

        // Notify legacy callbacks immediately
        auto legacyIt = m_LegacyCallbacks.find(key);
        if (legacyIt != m_LegacyCallbacks.end())
        {
            for (const auto& callback : legacyIt->second)
            {
                try
                {
                    if (callback)
                        callback(key, value);
                }
                catch (const std::exception& e)
                {
                    LT_CORE_ERROR("Exception in legacy config callback: {}", e.what());
                }
            }
        }
    }

    void ConfigManager::ProcessAsyncCallbacks()
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
                    LT_CORE_ERROR("Exception in async callback processing: {}", e.what());
                }
            }
            else
            {
                std::this_thread::yield();
            }
        }
    }

    void ConfigManager::UpdateStats(bool isRead, double duration) const
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