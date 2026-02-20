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

        // Resolve to an absolute path so file I/O and hot reload are deterministic even when the
        // process working directory differs between IDEs, scripts, and packaged builds.
        std::filesystem::path resolved = configFile.empty() ? std::filesystem::path("config.json") : std::filesystem::path(configFile);
        std::error_code ec;
        if (resolved.is_relative())
        {
            resolved = std::filesystem::absolute(resolved, ec);
            if (ec)
                resolved = std::filesystem::absolute(configFile);
        }
        resolved = std::filesystem::weakly_canonical(resolved, ec);
        if (ec)
        {
            // If canonicalization fails (e.g. file doesn't exist yet), keep the absolute form.
            resolved = std::filesystem::absolute(resolved);
        }

        m_ConfigFile = resolved.string();
        m_Shutdown.store(false);
        m_AsyncCallbackThreadStarted.store(false, std::memory_order_relaxed);

        LT_CORE_INFO("ConfigManager::Initialize: Config file set to: {}", m_ConfigFile);

        // Load defaults first
        LoadDefaults();

        // Async callback processing thread is started lazily on-demand:
        // - It is only needed if async change callbacks are registered.
        // - Avoiding a background thread by default improves determinism in tests/standalone tools.

        // Try to load from file if it exists
        if (std::filesystem::exists(m_ConfigFile))
        {
            LT_CORE_INFO("ConfigManager::Initialize: Config file exists, loading...");
            try
            {
                LoadFromFileAsync(m_ConfigFile).Get();
                LT_CORE_INFO("ConfigManager::Initialize: Config file loaded successfully");
            }
            catch (const std::exception& e)
            {
                LT_CORE_ERROR("Failed to load configuration from file: {} - {}", m_ConfigFile, e.what());
            }
        }
        else
        {
            LT_CORE_INFO("Configuration file not found, using defaults: {}", m_ConfigFile);
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
        m_AsyncCallbackCondition.notify_all();

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
        m_AsyncCallbackThreadStarted.store(false, std::memory_order_relaxed);

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
            auto configTask = Async::GetAsyncIO().LoadConfigAsyncResult(filename);
            const auto configResult = configTask.Get();
            if (configResult.IsFailure())
            {
                LT_CORE_ERROR("Failed to load configuration from file: {} - {}", filename, configResult.GetError().GetErrorMessage());
                return;
            }

            const auto& config = configResult.GetValue();

            std::unique_lock<std::shared_mutex> lock(m_ConfigMutex);
            m_Config.clear();

            // Flatten nested JSON of arbitrary depth into "dot" keys.
            // This keeps hot reload stable for nested structures (e.g. window.position.x/y).
            ProcessJsonObject(config, "");

            m_TotalAsyncOperations.fetch_add(1, std::memory_order_relaxed);
            LT_CORE_INFO("Configuration loaded from file: {} ({} entries)", filename, m_Config.size());
        });
    }

    Async::Task<void> ConfigManager::SaveToFileAsync(const std::string& filename)
    {
        return Async::Task<void>([this, filename]() -> void {
            // Take a snapshot of the flat map under a shared lock, then build JSON without holding the lock.
            // This avoids long lock holds and ensures multi-dot keys like "window.position.x" are serialized properly.
            std::unordered_map<std::string, ConfigValue> snapshot;
            snapshot.reserve(m_Config.size());
            {
                std::shared_lock<std::shared_mutex> lock(m_ConfigMutex);
                snapshot = m_Config;
            }

            nlohmann::json config = nlohmann::json::object();

            auto SetJsonAtPath = [&config](const std::string& dottedKey, const ConfigValue& value)
            {
                std::vector<std::string> parts;
                parts.reserve(4);

                std::string current;
                current.reserve(dottedKey.size());
                for (char c : dottedKey)
                {
                    if (c == '.')
                    {
                        if (!current.empty())
                        {
                            parts.push_back(std::move(current));
                            current = {};
                        }
                    }
                    else
                    {
                        current.push_back(c);
                    }
                }
                if (!current.empty())
                    parts.push_back(std::move(current));

                if (parts.empty())
                    return;

                nlohmann::json* node = &config;
                for (size_t i = 0; i + 1 < parts.size(); ++i)
                {
                    const auto& p = parts[i];
                    if (!node->contains(p) || !(*node)[p].is_object())
                    {
                        (*node)[p] = nlohmann::json::object();
                    }
                    node = &(*node)[p];
                }

                const std::string& leaf = parts.back();
                std::visit([node, &leaf](const auto& v)
                {
                    (*node)[leaf] = v;
                }, value);
            };

            for (const auto& [key, value] : snapshot)
            {
                SetJsonAtPath(key, value);
            }

            auto saveTask = Async::GetAsyncIO().SaveConfigAsyncResult(filename.empty() ? m_ConfigFile : filename, config);
            const auto saveResult = saveTask.Get();
            if (saveResult.IsFailure())
            {
                LT_CORE_ERROR("Failed to save configuration to file: {} - {}",
                    filename.empty() ? m_ConfigFile : filename, saveResult.GetError().GetErrorMessage());
                return;
            }

            m_TotalAsyncOperations.fetch_add(1, std::memory_order_relaxed);
            LT_CORE_INFO("Configuration saved to file: {} ({} entries)",
                   filename.empty() ? m_ConfigFile : filename, snapshot.size());
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

        // Ensure the worker exists once async callbacks are used.
        EnsureAsyncCallbackThreadRunning();
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
                        if (fullKey == Config::Window::POSITION_X || fullKey == Config::Window::POSITION_Y)
                        {
                            m_Config[fullKey] = intValue;
                            LT_CORE_DEBUG("Loaded config {} = {} (as int)", fullKey, intValue);
                        }
                        else if (fullKey.find("max_file_size") != std::string::npos || 
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
        m_Config[Config::Logging::CORE_LEVEL] = std::string("info");
        m_Config[Config::Logging::APP_LEVEL] = std::string("info");
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
            EnsureAsyncCallbackThreadRunning();
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
                else
                {
                    // Wake the async callback thread without spinning.
                    m_AsyncCallbackCondition.notify_one();
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
        std::unique_lock<std::mutex> waitLock(m_AsyncCallbackMutex);

        while (!m_Shutdown.load())
        {
            m_AsyncCallbackCondition.wait(waitLock, [this]() {
                return m_Shutdown.load() || !m_AsyncCallbackQueue.IsEmpty();
            });

            if (m_Shutdown.load())
                break;

            waitLock.unlock();

            while (auto callbackResult = m_AsyncCallbackQueue.TryPop())
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

            waitLock.lock();
        }
    }

    void ConfigManager::EnsureAsyncCallbackThreadRunning()
    {
        if (m_Shutdown.load(std::memory_order_relaxed))
        {
            return;
        }

        bool expected = false;
        if (!m_AsyncCallbackThreadStarted.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        {
            return; // already started
        }

        // If we're re-initializing after a shutdown, the old thread was joined and no longer joinable.
        // If a thread is still joinable here, do not replace it.
        if (m_AsyncCallbackThread.joinable())
        {
            return;
        }

        m_AsyncCallbackThread = std::thread(&ConfigManager::ProcessAsyncCallbacks, this);
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