#include "HotReloadManager.h"
#include "Core/Debug/Log.h"
#include "Core/ConfigManager.h"
#include "Core/FileWatcher.h"
#include "EventSystem.h"
#include <iostream>

namespace Limitless
{
    HotReloadManager& HotReloadManager::GetInstance()
    {
        static HotReloadManager instance;
        return instance;
    }

    void HotReloadManager::Initialize()
    {
        // Subscribe to configuration changes
        auto& config = Limitless::ConfigManager::GetInstance();
        
        // Set up callbacks for configuration changes
        config.RegisterChangeCallback("logging.level", [this](const std::string& key, const ConfigValue& value) { OnLoggingConfigChanged(key, value); });
        config.RegisterChangeCallback("logging.file_enabled", [this](const std::string& key, const ConfigValue& value) { OnLoggingConfigChanged(key, value); });
        config.RegisterChangeCallback("logging.console_enabled", [this](const std::string& key, const ConfigValue& value) { OnLoggingConfigChanged(key, value); });
        config.RegisterChangeCallback("logging.pattern", [this](const std::string& key, const ConfigValue& value) { OnLoggingConfigChanged(key, value); });

        config.RegisterChangeCallback("window.width", [this](const std::string& key, const ConfigValue& value) { OnWindowConfigChanged(key, value); });
        config.RegisterChangeCallback("window.height", [this](const std::string& key, const ConfigValue& value) { OnWindowConfigChanged(key, value); });
        config.RegisterChangeCallback("window.title", [this](const std::string& key, const ConfigValue& value) { OnWindowConfigChanged(key, value); });
        config.RegisterChangeCallback("window.fullscreen", [this](const std::string& key, const ConfigValue& value) { OnWindowConfigChanged(key, value); });
        config.RegisterChangeCallback("window.resizable", [this](const std::string& key, const ConfigValue& value) { OnWindowConfigChanged(key, value); });
        config.RegisterChangeCallback("window.vsync", [this](const std::string& key, const ConfigValue& value) { OnWindowConfigChanged(key, value); });
        
        LT_CORE_INFO("HotReloadManager: Initialized with configuration change callbacks");
    }

    void HotReloadManager::Shutdown()
    {
        if (m_Enabled) { EnableHotReload(false); }
        LT_CORE_INFO("HotReloadManager: Shutdown complete");
    }

    void HotReloadManager::EnableHotReload(bool enable)
    {
        if (m_Enabled == enable) return;
        m_Enabled = enable;
        auto& config = ConfigManager::GetInstance();
        config.EnableHotReload(enable);
        LT_CORE_INFO("HotReloadManager: Hot reload {}", (enable ? "enabled" : "disabled"));
    }

    void HotReloadManager::OnLoggingConfigChanged(const std::string& key, const ConfigValue& value)
    {
        LT_CORE_INFO("HotReloadManager: Logging configuration changed - {}", key);
        
        // Dispatch event for logging config change
        Events::LoggingConfigChangedEvent event(key, value);
        GetEventSystem().Dispatch(event);
        
        // Also reinitialize logging immediately
        ReinitializeLogging();
    }

    void HotReloadManager::OnWindowConfigChanged(const std::string& key, const ConfigValue& value)
    {
        LT_CORE_INFO("HotReloadManager: Window configuration changed - {}", key);
        
        // Dispatch event for window config change
        Events::WindowConfigChangedEvent event(key, value);
        GetEventSystem().Dispatch(event);
        
        // Note: Window reinitialization is now handled by the window class via event subscription
    }

    void HotReloadManager::ReinitializeLogging()
    {
        LT_CORE_INFO("HotReloadManager: Reinitializing logging system...");
        Log::Shutdown();
        Log::InitFromConfig();
        LT_CORE_INFO("HotReloadManager: Logging system reinitialized");
    }

    void HotReloadManager::SetWindow(Window* window)
    {
        m_Window = window;
    }
}