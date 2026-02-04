#pragma once

#include "ConfigManager.h"
#include "Debug/Log.h"
#include "Platform/Window.h"
#include "EventSystem.h"
#include <memory>
#include <mutex>
#include <vector>

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

        // Apply pending hot reload changes on the main thread.
        // This must be called from the application's main loop to keep window and logging updates thread-safe.
        void Update();
        
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
        void ApplyWindowConfigChange(const std::string& key, const ConfigValue& value);

    private:
        bool m_Enabled = false;
        Window* m_Window = nullptr; // Raw pointer to avoid ownership issues

        struct PendingConfigChange
        {
            std::string key;
            ConfigValue value;
        };

        std::mutex m_PendingMutex;
        std::vector<PendingConfigChange> m_PendingWindowChanges;
        bool m_PendingLoggingReinitialize = false;
    };
}