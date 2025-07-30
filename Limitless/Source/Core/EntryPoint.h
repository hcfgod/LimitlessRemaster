#pragma once
#include "Application.h"
#include "ConfigManager.h"
#include "Debug/Log.h"
#include "HotReloadManager.h"

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
    // Default logging initialization so we can logs errors before the config and other systems are set up
	Limitless::Log::Init();

	// Initialize configuration system first
	auto& configManager = Limitless::ConfigManager::GetInstance();
	configManager.Initialize("config.json");
	
	// Load configuration from command line arguments
	configManager.LoadFromCommandLine(argc, argv);

	// Try to initialize logging system with configuration settings
	Limitless::Log::InitFromConfig();

	// Initialize hot reload manager
	auto& hotReloadManager = Limitless::HotReloadManager::GetInstance();
	hotReloadManager.Initialize();
	hotReloadManager.EnableHotReload(true);

	Limitless::Application* app = CreateApplication();

	if (app)
	{
		app->Run();
		
		delete app;
	}
	else
	{
		return -1;
	}
	
	// Shutdown hot reload manager
	hotReloadManager.Shutdown();
	
	// Shutdown logging system
	Limitless::Log::Shutdown();
	
	// Shutdown configuration system last
	configManager.Shutdown();

	return 0;
}