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
    LT_PERF_SCOPE("SandboxApp::Initialize");
    
    // Initialize sandbox-specific resources
    LT_INFO("SandboxApp has been successfully initialized!");
    LT_INFO("Window created with size: {}x{}", GetWindow().GetWidth(), GetWindow().GetHeight());
    
    // Demonstrate different logging levels
    LT_TRACE("This is a trace message - very detailed debugging info");
    LT_DEBUG("This is a debug message - useful for development");
    LT_INFO("This is an info message - general information");
    LT_WARN("This is a warning message - something to be aware of");
    
    // Demonstrate performance logging
    LT_PERF_FUNCTION("Sample Operation", []() {
        // Simulate some work
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    });
    
    // Demonstrate memory logging
    LT_LOG_MEMORY("After SandboxApp initialization");
    
    // Demonstrate error handling
    try
    {
        // Simulate a potential error condition
        bool someCondition = false;
        LT_ASSERT(someCondition, "This assertion will fail to demonstrate error handling");
    }
    catch (const Limitless::Error& error)
    {
        LT_ERROR("Caught Limitless error: {}", error.ToString());
    }
    
    // Demonstrate multiple loggers
    auto graphicsLogger = GetLogManager().GetLogger("Graphics");
    auto audioLogger = GetLogManager().GetLogger("Audio");
    
    graphicsLogger->Info("Graphics system initialized");
    audioLogger->Info("Audio system initialized");
    
    return true;
}

void SandboxApp::Shutdown()
{
    // Don't use LT_PERF_SCOPE here as the logger gets destroyed during shutdown
    LT_INFO("SandboxApp is shutting down...");
    
    // Demonstrate conditional logging
    bool hasResources = true;
    LT_INFO_IF(hasResources, "Cleaning up {} resources", 5);
    
    // Don't use LT_LOG_MEMORY here as the logger gets destroyed during shutdown
    
    // Clean up sandbox-specific resources
    LT_INFO("SandboxApp shutdown complete");
} 