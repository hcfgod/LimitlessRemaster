#pragma once

#include "Error.h"
#include "FileWatcher.h"
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <string>
#include <variant>
#include <memory>
#include <functional>
#include <optional>
#include <mutex>

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

    class ConfigManager
    {
    public:
        static ConfigManager& GetInstance();
        
        // Disable copy and assignment
        ConfigManager(const ConfigManager&) = delete;
        ConfigManager& operator=(const ConfigManager&) = delete;

        // Initialize configuration system
        void Initialize(const std::string& configFile = "config.json");
        
        // Shutdown configuration system
        void Shutdown();

        // Check if initialized
        bool IsInitialized() const { return m_Initialized; }

        // Load configuration from file
        bool LoadFromFile(const std::string& filename);
        
        // Save configuration to file
        bool SaveToFile(const std::string& filename = "") const;

        // Set configuration value
        template<typename T>
        void SetValue(const std::string& key, const T& value);
        
        // Get configuration value with type safety
        template<typename T>
        T GetValue(const std::string& key, const T& defaultValue = T{}) const;
        
        // Get configuration value as optional
        template<typename T>
        std::optional<T> GetValueOptional(const std::string& key) const;

        // Check if key exists
        bool HasValue(const std::string& key) const;
        
        // Remove configuration value
        void RemoveValue(const std::string& key);

        // Register configuration schema (validation rules)
        void RegisterSchema(const std::string& key, ConfigValidator validator);
        
        // Validate all configuration values
        bool ValidateConfiguration() const;
        
        // Get validation errors
        std::vector<std::string> GetValidationErrors() const;

        // Register change callback
        void RegisterChangeCallback(const std::string& key, ConfigChangeCallback callback);
        
        // Unregister change callback
        void UnregisterChangeCallback(const std::string& key);

        // Reset to defaults
        void ResetToDefaults();
        
        // Get all configuration keys
        std::vector<std::string> GetKeys() const;
        
        // Get configuration as JSON
        nlohmann::json ToJson() const;
        
        // Load from JSON
        void FromJson(const nlohmann::json& json);

        // Environment variable integration
        void LoadFromEnvironment(const std::string& prefix = "LIMITLESS_");
        
        // Command line argument integration
        void LoadFromCommandLine(int argc, char* argv[], const std::string& prefix = "--");

        // Hot reload support
        bool IsHotReloadEnabled() const { return m_HotReloadEnabled; }
        void EnableHotReload(bool enable);
        void ReloadFromFile();

    private:
        ConfigManager() = default;
        ~ConfigManager() = default;

        std::unordered_map<std::string, ConfigValue> m_Values;
        std::unordered_map<std::string, ConfigValidator> m_Validators;
        std::unordered_map<std::string, std::vector<ConfigChangeCallback>> m_ChangeCallbacks;
        std::unordered_map<std::string, ConfigValue> m_Defaults;
        std::string m_ConfigFile;
        bool m_Initialized = false;
        bool m_HotReloadEnabled = false;
        std::unique_ptr<FileWatcher> m_FileWatcher;
        mutable std::mutex m_Mutex;

        void NotifyChangeCallbacks(const std::string& key, const ConfigValue& value);
        bool ValidateValue(const std::string& key, const ConfigValue& value) const;
        void LoadDefaults();
        void ProcessJsonObject(const nlohmann::json& json, const std::string& prefix);
        
        // Type conversion helpers
        template<typename T>
        T ConvertValue(const ConfigValue& value) const;
        
        template<typename T>
        bool CanConvert(const ConfigValue& value) const;
    };

    // Convenience function
    inline ConfigManager& GetConfigManager() { return ConfigManager::GetInstance(); }
}