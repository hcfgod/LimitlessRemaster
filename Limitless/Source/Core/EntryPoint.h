#pragma once
#include "Application.h"
#include "ConfigManager.h"
#include "Debug/Log.h"
#include <iostream>

// Prevent console window from appearing in Dist builds on Windows
#if defined(LT_PLATFORM_WINDOWS) && defined(LT_CONFIG_DIST)
    /*
     * Ensure distribution / release builds on Windows do not spawn a console
     * window by switching the subsystem to WINDOWS and setting the CRT entry
     * point appropriately.  This pragma is MSVC-specific and is ignored by
     * other compilers.
     */
    #if defined(_MSC_VER)
        #pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")
    #endif
#endif

// This function must be defined by the client application
extern Limitless::Application* CreateApplication();

int main(int argc, char** argv)
{
	// Initialize configuration system first (before logging)
	auto& configManager = Limitless::ConfigManager::GetInstance();
	configManager.Initialize("config.json");
	
	// Load configuration from command line arguments
	if (argc > 1) {
		configManager.LoadFromCommandLine(argc, argv);
	}

	// Initialize logging system with configuration settings
	Limitless::Log::InitFromConfig();	
	
	// Now we can start logging
	LT_CORE_INFO("=== Limitless Engine Startup ===");
	LT_CORE_INFO("Starting application with {} command line arguments", argc);
	LT_CORE_INFO("ConfigManager initialized successfully");
	if (argc > 1) {
		LT_CORE_INFO("Command line configuration loaded");
	}
	LT_CORE_INFO("Logging system initialized successfully");

	LT_CORE_INFO("Creating application instance...");
	Limitless::Application* app = CreateApplication();

	if (app)
	{
		LT_CORE_INFO("Application created successfully, starting main loop...");
		app->Run();
		LT_CORE_INFO("Application main loop completed");
		
		LT_CORE_INFO("Destroying application instance...");
		delete app;
		LT_CORE_INFO("Application destroyed successfully");
	}
	else
	{
		LT_CORE_ERROR("Failed to create application instance!");
		return -1;
	}

	// ---------------------------------------------------------------------------------
	// Shutdown ordering matters.
	//
	// Many engine systems (ConfigManager, hot reload, async tasks) may still be running
	// background threads and/or performing final file I/O during shutdown.
	//
	// Keep logging alive until *after* those systems are fully torn down. This avoids
	// shutdown-time races where another thread attempts to log while spdlog is tearing
	// down its async thread pool.
	// ---------------------------------------------------------------------------------

	LT_CORE_INFO("Shutting down configuration system...");
	configManager.Shutdown();
	LT_CORE_INFO("ConfigManager shutdown complete");

	LT_CORE_INFO("Shutting down logging system...");
	Limitless::Log::Shutdown();

	// Don't use LT_CORE_* after Log::Shutdown().
	#ifdef LT_CONSOLE_LOGGING_ENABLED
	std::cout << "=== Limitless Engine Shutdown Complete ===" << std::endl;
	#endif

	return 0;
}