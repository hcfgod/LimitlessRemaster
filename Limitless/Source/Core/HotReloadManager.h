#pragma once

#include "ConfigManager.h"
#include "Debug/Log.h"
#include "Platform/Window.h"
#include "EventSystem.h"
#include <memory>

namespace Limitless
{
    class HotReloadManager
    {
    public:
        static HotReloadManager& GetInstance();
        
        // Initialize hot reloading for all systems
        void Initialize();
        
        // Shutdown hot reloading
        void Shutdown();
        
        // Enable/disable hot reloading
        void EnableHotReload(bool enable = true);
        bool IsHotReloadEnabled() const { return m_Enabled; }
        
        // Set window reference for hot reloading
        void SetWindow(Window* window);

    private:
        HotReloadManager() = default;
        ~HotReloadManager() = default;
        
        // Disable copy and assignment
        HotReloadManager(const HotReloadManager&) = delete;
        HotReloadManager& operator=(const HotReloadManager&) = delete;

        // Hot reload callbacks for different systems
        void OnLoggingConfigChanged(const std::string& key, const ConfigValue& value);
        void OnWindowConfigChanged(const std::string& key, const ConfigValue& value);
        
        // Reinitialize systems with new configuration
        void ReinitializeLogging();

    private:
        bool m_Enabled = false;
        Window* m_Window = nullptr; // Raw pointer to avoid ownership issues
    };
}