#include "Application.h"
#include "Platform/Window.h"
#include "Platform/SDL/SDLManager.h"
#include "PerformanceMonitor.h"

namespace Limitless
{
	Application::Application()
	{
		// Constructor implementation
	}

	Application::~Application()
	{
		// Destructor implementation
	}

	void Application::Run()
	{
		LT_PERF_SCOPE("Application::Run");
		
		if (!InternalInitialize())
		{
			LT_ERROR("Failed to initialize application!");
			return;
		}

		LT_INFO("Starting application main loop");
		
		while(m_isRunning)
		{
			m_Window->OnUpdate();
		}

		LT_INFO("Application main loop ended");
		InternalShutdown();
	}

	bool Application::InternalInitialize()
	{
		LT_PERF_SCOPE("Application::InternalInitialize");
		LT_INFO("Application initializing...");

		// Initialize logging system
		LogManager::GetInstance().Initialize("Limitless");
		// Get the default logger - it was already created during Initialize
		m_Logger = LogManager::GetInstance().GetLogger("default");
		
		// Enable file logging
		LogManager::GetInstance().EnableGlobalFileLogging("logs");
		
		// Log system information
		LogManager::GetInstance().LogSystemInfo();

		// Initialize SDL
		if (!SDLManager::GetInstance().Initialize())
		{
			LT_ERROR("Failed to initialize SDL!");
			return false;
		}

		// Create window
		m_Window = Window::Create();
		if (!m_Window)
		{
			LT_ERROR("Failed to create window!");
			return false;
		}

		// Set up close callback
		m_Window->SetCloseCallback([this]() {
			LT_INFO("Window close requested");
			m_isRunning = false;
		});

		if (!Initialize())
		{
			LT_ERROR("Application initialization failed!");
			return false;
		}

		LT_INFO("Application fully initialized!");

		return true;
	}

	void Application::InternalShutdown()
	{
		// Don't use LT_PERF_SCOPE here as the logger gets destroyed during shutdown
		LT_INFO("Application shutting down...");
		
		Shutdown();
		
		// Clean up window
		m_Window.reset();
		
		// Shutdown SDL
		SDLManager::GetInstance().Shutdown();
		
		// Shutdown logging system
		LogManager::GetInstance().Shutdown();
		
		// Don't log after shutdown as the logger is now destroyed
	}
}