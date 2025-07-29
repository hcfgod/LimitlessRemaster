#include "SandboxApp.h"
#include "Limitless.h"
#include <thread>

// Define CreateApplication in global namespace as expected by EntryPoint.h
Limitless::Application* CreateApplication()
{
    return new SandboxApp();
}

bool SandboxApp::Initialize()
{
    LT_INFO("SandboxApp initialized successfully!");
    LT_INFO("Logging system is now configured from config.json");
    LT_INFO("Hot reloading is enabled - try changing config.json while the app is running!");
    
    // Demonstrate different log levels - these should all be visible with debug level
    LT_TRACE("This is a trace message - only visible if log level is set to 'trace'");
    LT_DBG("This is a debug message - only visible if log level is set to 'debug' or lower");
    LT_INFO("This is an info message - visible with 'info' level or lower");
    LT_WARN("This is a warning message - visible with 'warn' level or lower");
    LT_ERROR("This is an error message - visible with 'error' level or lower");
    
    // Log some configuration values to demonstrate integration
    auto& config = Limitless::ConfigManager::GetInstance();
    LT_INFO("Window configuration: {}x{}", 
            config.GetValue<int>(Limitless::Config::Window::WIDTH, 1280),
            config.GetValue<int>(Limitless::Config::Window::HEIGHT, 720));
    
    LT_INFO("Window title: {}", config.GetValue<std::string>(Limitless::Config::Window::TITLE, "Default"));
    LT_INFO("Window fullscreen: {}", config.GetValue<bool>(Limitless::Config::Window::FULLSCREEN, false) ? "enabled" : "disabled");
    LT_INFO("Window resizable: {}", config.GetValue<bool>(Limitless::Config::Window::RESIZABLE, true) ? "enabled" : "disabled");
    LT_INFO("Window VSync: {}", config.GetValue<bool>(Limitless::Config::Window::VSYNC, true) ? "enabled" : "disabled");
    
    LT_INFO("Logging configuration loaded from config.json");
    LT_INFO("Log level: {}", config.GetValue<std::string>(Limitless::Config::Logging::LEVEL, "info"));
    LT_INFO("File logging: {}", config.GetValue<bool>(Limitless::Config::Logging::FILE_ENABLED, true) ? "enabled" : "disabled");
    LT_INFO("Console logging: {}", config.GetValue<bool>(Limitless::Config::Logging::CONSOLE_ENABLED, true) ? "enabled" : "disabled");
    
    // Test configuration changes
    LT_DBG("Testing configuration access...");
    LT_DBG("Window title: {}", config.GetValue<std::string>(Limitless::Config::Window::TITLE, "Default"));
    LT_DBG("Max threads: {}", config.GetValue<int>(Limitless::Config::System::MAX_THREADS, 4));
    
    // Demonstrate conditional logging
    bool debugMode = config.GetValue<std::string>(Limitless::Config::Logging::LEVEL) == "debug";
    LT_DBG_IF(debugMode, "Debug mode is enabled - showing detailed information");
    LT_INFO_IF(!debugMode, "Debug mode is disabled - showing basic information only");
    
    // Test conditional logging with different conditions
    int maxThreads = config.GetValue<int>(Limitless::Config::System::MAX_THREADS, 4);
    LT_WARN_IF(maxThreads > 8, "High thread count detected: {} threads", maxThreads);
    LT_ERROR_IF(maxThreads == 0, "Invalid thread count: 0 threads");
    
    // Test conditional logging with complex conditions
    bool isFullscreen = config.GetValue<bool>(Limitless::Config::Window::FULLSCREEN, false);
    uint32_t width = config.GetValue<uint32_t>(Limitless::Config::Window::WIDTH, 800);
    uint32_t height = config.GetValue<uint32_t>(Limitless::Config::Window::HEIGHT, 600);
    
    LT_INFO_IF(isFullscreen && (width < 1920 || height < 1080), 
               "Fullscreen mode with low resolution: {}x{}", width, height);
    LT_DBG_IF(!isFullscreen, "Windowed mode: {}x{}", width, height);
    
    return true;
}

void SandboxApp::Shutdown()
{
    LT_INFO("SandboxApp shutting down...");
}