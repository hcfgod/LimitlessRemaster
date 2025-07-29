#pragma once
#include "Application.h"
#include "ConfigManager.h"
#include "Debug/Log.h"
#include "HotReloadManager.h"

// This function must be defined by the client application
extern Limitless::Application* CreateApplication();

int main(int argc, char** argv)
{
	// Default logging
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