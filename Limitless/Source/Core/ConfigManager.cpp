#include "ConfigManager.h"
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>

namespace Limitless
{
    ConfigManager& ConfigManager::GetInstance()
    {
        static ConfigManager instance;
        return instance;
    }

    void ConfigManager::Initialize(const std::string& configFile)
    {
        if (m_Initialized)
        {
            return;
        }

        m_ConfigFile = configFile;
        LoadDefaults();
        
        // Try to load from file if it exists
        if (std::filesystem::exists(configFile))
        {
            if (!LoadFromFile(configFile))
            {
                // Use std::cerr instead of logging since logging isn't initialized yet
                std::cerr << "Failed to load configuration from file: " << configFile << std::endl;
            }
        }
        else
        {
            // Use std::cout instead of logging since logging isn't initialized yet
            std::cout << "Configuration file not found, using defaults: " << configFile << std::endl;
        }

        // Load from environment variables
        LoadFromEnvironment();
        
        m_Initialized = true;
    }

    void ConfigManager::Shutdown()
    {
        if (!m_Initialized) return;

        // Save current configuration
        if (!m_ConfigFile.empty())
        {
            if (!SaveToFile(m_ConfigFile)) 
            {
                std::cerr << "Failed to save configuration to file: " << m_ConfigFile << std::endl;
            }
        }
        
        // Clear all data
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Values.clear();
        m_Validators.clear();
        m_ChangeCallbacks.clear();
        m_Defaults.clear();
        
        m_Initialized = false;
    }

    bool ConfigManager::LoadFromFile(const std::string& filename)
    {
        try
        {
            std::ifstream file(filename);
            if (!file.is_open())
            {
                std::cerr << "Could not open configuration file: " << filename << std::endl;
                return false;
            }

            nlohmann::json json;
            file >> json;
            
            FromJson(json);

            return true;
        }
        catch (const std::exception& e) 
        {
            std::cerr << "Error loading configuration from file " << filename << ": " << e.what() << std::endl;
            return false;
        }
    }

    bool ConfigManager::SaveToFile(const std::string& filename) const
    {
        try
        {
            std::string saveFile = filename.empty() ? m_ConfigFile : filename;
            if (saveFile.empty())
            {
                std::cerr << "Cannot save configuration: no filename provided" << std::endl;
                return false;
            }

            // Create directory if it doesn't exist
            std::filesystem::path path(saveFile);
            if (path.has_parent_path())
            {
                std::filesystem::create_directories(path.parent_path());
            }

            std::ofstream file(saveFile);
            if (!file.is_open())
            {
                std::cerr << "Could not open configuration file for writing: " << saveFile << std::endl;
                return false;
            }

            nlohmann::json json = ToJson();
            file << std::setw(4) << json << std::endl;
            
            return true;
        }
        catch (const std::exception& e)
        {
            std::cerr << "Error saving configuration to file " << filename << ": " << e.what() << std::endl;
            return false;
        }
    }

    template<typename T>
    void ConfigManager::SetValue(const std::string& key, const T& value)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        
        ConfigValue configValue = ConfigValue(value);
        
        // Validate if validator exists
        if (!ValidateValue(key, configValue))
        {
            std::cerr << "Validation failed for key: " << key << " with value: " << value << std::endl;
            return;
        }
        
        // Check if value actually changed
        auto it = m_Values.find(key);
        if (it != m_Values.end() && it->second == configValue)
        {
            return; // No change
        }
        
        m_Values[key] = configValue;
        
        // Notify change callbacks
        NotifyChangeCallbacks(key, configValue);
    }

    template<typename T>
    T ConfigManager::GetValue(const std::string& key, const T& defaultValue) const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        
        auto it = m_Values.find(key);
        if (it != m_Values.end())
        {
            try
            {
                // Attempt to convert the value to the requested type
                return ConvertValue<T>(it->second);
            }
            catch (const std::exception& e)
            {
                std::cerr << "Error converting value for key " << key << ": " << e.what() << std::endl;
                return defaultValue;
            }
        }
        
        return defaultValue;
    }

    template<typename T>
    std::optional<T> ConfigManager::GetValueOptional(const std::string& key) const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        
        auto it = m_Values.find(key);
        if (it != m_Values.end())
        {
            try
            {
                // Attempt to convert the value to the requested type
                return ConvertValue<T>(it->second);
            }
            catch (const std::exception& e)
            {
                std::cerr << "Error converting value for key " << key << ": " << e.what() << std::endl;
                return std::nullopt;
            }
        }
        
        return std::nullopt;
    }

    bool ConfigManager::HasValue(const std::string& key) const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_Values.find(key) != m_Values.end();
    }

    void ConfigManager::RemoveValue(const std::string& key)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        
        auto it = m_Values.find(key);
        if (it != m_Values.end())
        {
            m_Values.erase(it);
        }
    }

    void ConfigManager::RegisterSchema(const std::string& key, ConfigValidator validator)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Validators[key] = validator;
    }

    bool ConfigManager::ValidateConfiguration() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        
        bool valid = true;
        for (const auto& [key, value] : m_Values)
        {
            if (!ValidateValue(key, value))
            {
                valid = false;
            }
        }
        
        return valid;
    }

    std::vector<std::string> ConfigManager::GetValidationErrors() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        
        std::vector<std::string> errors;
        for (const auto& [key, value] : m_Values)
        {
            if (!ValidateValue(key, value))
            {
                errors.push_back("Validation failed for key: " + key);
            }
        }
        
        return errors;
    }

    void ConfigManager::RegisterChangeCallback(const std::string& key, ConfigChangeCallback callback)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_ChangeCallbacks[key].push_back(callback);
    }

    void ConfigManager::UnregisterChangeCallback(const std::string& key)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_ChangeCallbacks.erase(key);
    }

    void ConfigManager::ResetToDefaults()
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Values = m_Defaults;
    }

    std::vector<std::string> ConfigManager::GetKeys() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        
        std::vector<std::string> keys;
        keys.reserve(m_Values.size());
        
        for (const auto& [key, _] : m_Values)
        {
            keys.push_back(key);
        }
        
        return keys;
    }

    nlohmann::json ConfigManager::ToJson() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        
        nlohmann::json json;
        
        for (const auto& [key, value] : m_Values)
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
        std::lock_guard<std::mutex> lock(m_Mutex);
        
        std::cout << "Loading configuration from JSON with " << json.size() << " items" << std::endl;
        
        ProcessJsonObject(json, "");
        
        std::cout << "Configuration loading complete. Total values: " << m_Values.size() << std::endl;
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
                    m_Values[fullKey] = value.get<bool>();
                    std::cout << "Loaded config " << fullKey << " = " << (value.get<bool>() ? "true" : "false") << std::endl;
                }
                else if (value.is_number_integer())
                {
                    m_Values[fullKey] = value.get<int>();
                    std::cout << "Loaded config " << fullKey << " = " << value.get<int>() << std::endl;
                }
                else if (value.is_number_float())
                {
                    m_Values[fullKey] = value.get<double>();
                    std::cout << "Loaded config " << fullKey << " = " << value.get<double>() << std::endl;
                }
                else if (value.is_string())
                {
                    m_Values[fullKey] = value.get<std::string>();
                    std::cout << "Loaded config " << fullKey << " = " << value.get<std::string>() << std::endl;
                }
                else
                {
                    std::cout << "Unknown config value type for key: " << fullKey << std::endl;
                }
            }
            catch (const std::exception& e)
            {
                std::cerr << "Error parsing key " << fullKey << ": " << e.what() << std::endl;
                continue; // Skip invalid entries
            }
        }
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

    void ConfigManager::EnableHotReload(bool enable)
    {
        m_HotReloadEnabled = enable;
    }

    void ConfigManager::NotifyChangeCallbacks(const std::string& key, const ConfigValue& value)
    {
        auto it = m_ChangeCallbacks.find(key);
        if (it != m_ChangeCallbacks.end())
        {
            for (const auto& callback : it->second)
            {
                try
                {
                    callback(key, value);
                }
                catch (const std::exception& e)
                {

                }
            }
        }
    }

    bool ConfigManager::ValidateValue(const std::string& key, const ConfigValue& value) const
    {
        auto it = m_Validators.find(key);
        if (it != m_Validators.end())
        {
            return it->second(value);
        }
        return true; // No validator means always valid
    }

    void ConfigManager::LoadDefaults()
    {
        // Window defaults
        m_Defaults[Config::Window::WIDTH] = 1280;
        m_Defaults[Config::Window::HEIGHT] = 720;
        m_Defaults[Config::Window::TITLE] = std::string("Limitless Engine");
        m_Defaults[Config::Window::FULLSCREEN] = false;
        m_Defaults[Config::Window::VSYNC] = false;
        m_Defaults[Config::Window::RESIZABLE] = true;

        // Logging defaults
        m_Defaults[Config::Logging::LEVEL] = std::string("info");
        m_Defaults[Config::Logging::FILE_ENABLED] = true;
        m_Defaults[Config::Logging::CONSOLE_ENABLED] = true;
        m_Defaults[Config::Logging::PATTERN] = std::string("[%T] [%l] %n: %v");
        m_Defaults[Config::Logging::DIRECTORY] = std::string("logs");
        m_Defaults[Config::Logging::MAX_FILE_SIZE] = static_cast<size_t>(50 * 1024 * 1024); // 50MB
        m_Defaults[Config::Logging::MAX_FILES] = static_cast<size_t>(10);

        // System defaults
        m_Defaults[Config::System::MAX_THREADS] = static_cast<int>(std::thread::hardware_concurrency());
        m_Defaults[Config::System::WORKING_DIRECTORY] = std::string(".");
        m_Defaults[Config::System::TEMP_DIRECTORY] = std::string("temp");
        m_Defaults[Config::System::LOG_DIRECTORY] = std::string("logs");

        // Copy defaults to current values
        m_Values = m_Defaults;
    }

    template<typename T>
    T ConfigManager::ConvertValue(const ConfigValue& value) const
    {
        return std::visit([](const auto& v) -> T {
            using V = std::decay_t<decltype(v)>;
            
            // Direct type match
            if constexpr (std::is_same_v<T, V>)
            {
                return v;
            }
            // String conversions
            else if constexpr (std::is_same_v<T, std::string>)
            {
                if constexpr (std::is_same_v<V, std::string>)
                {
                    return v;
                }
                else if constexpr (std::is_same_v<V, bool>)
                {
                    return v ? "true" : "false";
                }
                else if constexpr (std::is_same_v<V, int> || std::is_same_v<V, float> || std::is_same_v<V, double>)
                {
                    return std::to_string(v);
                }
                else if constexpr (std::is_same_v<V, size_t> || std::is_same_v<V, uint32_t>)
                {
                    return std::to_string(v);
                }
                else
                {
                    static_assert(std::is_same_v<V, void>, "Unsupported type for string conversion");
                }
            }
            // Numeric conversions
            else if constexpr (std::is_same_v<T, int>)
            {
                if constexpr (std::is_same_v<V, std::string>)
                {
                    return std::stoi(v);
                }
                else if constexpr (std::is_same_v<V, bool>)
                {
                    return static_cast<int>(v);
                }
                else if constexpr (std::is_same_v<V, float> || std::is_same_v<V, double>)
                {
                    return static_cast<int>(v);
                }
                else if constexpr (std::is_same_v<V, size_t> || std::is_same_v<V, uint32_t>)
                {
                    return static_cast<int>(v);
                }
                else
                {
                    static_assert(std::is_same_v<V, void>, "Unsupported type for int conversion");
                }
            }
            else if constexpr (std::is_same_v<T, float>)
            {
                if constexpr (std::is_same_v<V, std::string>)
                {
                    return std::stof(v);
                }
                else if constexpr (std::is_same_v<V, bool>)
                {
                    return static_cast<float>(v);
                }
                else if constexpr (std::is_same_v<V, int> || std::is_same_v<V, double>)
                {
                    return static_cast<float>(v);
                }
                else if constexpr (std::is_same_v<V, size_t> || std::is_same_v<V, uint32_t>)
                {
                    return static_cast<float>(v);
                }
                else
                {
                    static_assert(std::is_same_v<V, void>, "Unsupported type for float conversion");
                }
            }
            else if constexpr (std::is_same_v<T, double>)
            {
                if constexpr (std::is_same_v<V, std::string>)
                {
                    return std::stod(v);
                }
                else if constexpr (std::is_same_v<V, bool>)
                {
                    return static_cast<double>(v);
                }
                else if constexpr (std::is_same_v<V, int> || std::is_same_v<V, float>)
                {
                    return static_cast<double>(v);
                }
                else if constexpr (std::is_same_v<V, size_t> || std::is_same_v<V, uint32_t>)
                {
                    return static_cast<double>(v);
                }
                else
                {
                    static_assert(std::is_same_v<V, void>, "Unsupported type for double conversion");
                }
            }
            else if constexpr (std::is_same_v<T, size_t>)
            {
                if constexpr (std::is_same_v<V, std::string>)
                {
                    return std::stoull(v);
                }
                else if constexpr (std::is_same_v<V, bool>)
                {
                    return static_cast<size_t>(v);
                }
                else if constexpr (std::is_same_v<V, int> || std::is_same_v<V, float> || std::is_same_v<V, double>)
                {
                    return static_cast<size_t>(v);
                }
                else if constexpr (std::is_same_v<V, size_t> || std::is_same_v<V, uint32_t>)
                {
                    return static_cast<size_t>(v);
                }
                else
                {
                    static_assert(std::is_same_v<V, void>, "Unsupported type for size_t conversion");
                }
            }
            else if constexpr (std::is_same_v<T, uint32_t>)
            {
                if constexpr (std::is_same_v<V, std::string>)
                {
                    return static_cast<uint32_t>(std::stoul(v));
                }
                else if constexpr (std::is_same_v<V, bool>)
                {
                    return static_cast<uint32_t>(v);
                }
                else if constexpr (std::is_same_v<V, int> || std::is_same_v<V, float> || std::is_same_v<V, double>)
                {
                    return static_cast<uint32_t>(v);
                }
                else if constexpr (std::is_same_v<V, size_t> || std::is_same_v<V, uint32_t>)
                {
                    return static_cast<uint32_t>(v);
                }
                else
                {
                    static_assert(std::is_same_v<V, void>, "Unsupported type for uint32_t conversion");
                }
            }
            else if constexpr (std::is_same_v<T, bool>)
            {
                if constexpr (std::is_same_v<V, std::string>)
                {
                    std::string lower = v;
                    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                    return (lower == "true" || lower == "1" || lower == "yes");
                }
                else if constexpr (std::is_same_v<V, int> || std::is_same_v<V, float> || std::is_same_v<V, double>)
                {
                    return static_cast<bool>(v);
                }
                else if constexpr (std::is_same_v<V, size_t> || std::is_same_v<V, uint32_t>)
                {
                    return static_cast<bool>(v);
                }
                else
                {
                    static_assert(std::is_same_v<V, void>, "Unsupported type for bool conversion");
                }
            }
            else
            {
                static_assert(std::is_same_v<T, void>, "Unsupported target type for conversion");
            }
        }, value);
    }

    template<typename T>
    bool ConfigManager::CanConvert(const ConfigValue& value) const
    {
        try
        {
            ConvertValue<T>(value);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    // Explicit template instantiations
    template void ConfigManager::SetValue<bool>(const std::string&, const bool&);
    template void ConfigManager::SetValue<int>(const std::string&, const int&);
    template void ConfigManager::SetValue<float>(const std::string&, const float&);
    template void ConfigManager::SetValue<double>(const std::string&, const double&);
    template void ConfigManager::SetValue<std::string>(const std::string&, const std::string&);
    template void ConfigManager::SetValue<size_t>(const std::string&, const size_t&);
    template void ConfigManager::SetValue<uint32_t>(const std::string&, const uint32_t&);

    template bool ConfigManager::GetValue<bool>(const std::string&, const bool&) const;
    template int ConfigManager::GetValue<int>(const std::string&, const int&) const;
    template float ConfigManager::GetValue<float>(const std::string&, const float&) const;
    template double ConfigManager::GetValue<double>(const std::string&, const double&) const;
    template std::string ConfigManager::GetValue<std::string>(const std::string&, const std::string&) const;
    template size_t ConfigManager::GetValue<size_t>(const std::string&, const size_t&) const;
    template uint32_t ConfigManager::GetValue<uint32_t>(const std::string&, const uint32_t&) const;

    template std::optional<bool> ConfigManager::GetValueOptional<bool>(const std::string&) const;
    template std::optional<int> ConfigManager::GetValueOptional<int>(const std::string&) const;
    template std::optional<float> ConfigManager::GetValueOptional<float>(const std::string&) const;
    template std::optional<double> ConfigManager::GetValueOptional<double>(const std::string&) const;
    template std::optional<std::string> ConfigManager::GetValueOptional<std::string>(const std::string&) const;
    template std::optional<size_t> ConfigManager::GetValueOptional<size_t>(const std::string&) const;
    template std::optional<uint32_t> ConfigManager::GetValueOptional<uint32_t>(const std::string&) const;
}