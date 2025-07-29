#pragma once
#include "Application.h"
#include "ConfigManager.h"
#include "Debug/Log.h"

// This function must be defined by the client application
extern Limitless::Application* CreateApplication();

int main(int argc, char** argv)
{
	// Initialize configuration system first
	auto& configManager = Limitless::ConfigManager::GetInstance();
	configManager.Initialize("config.json");
	
	// Load configuration from command line arguments
	configManager.LoadFromCommandLine(argc, argv);

	// Initialize logging system with configuration settings
	Limitless::Log::InitFromConfig();

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
	
	// Shutdown logging system
	Limitless::Log::Shutdown();
	
	// Shutdown configuration system last
	configManager.Shutdown();

	return 0;
}