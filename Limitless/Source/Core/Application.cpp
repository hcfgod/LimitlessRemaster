#include "Application.h"
#include "Platform/Window.h"
#include "Platform/SDL/SDLWindow.h"
#include "Platform/SDL/SDLManager.h"
#include "Platform/Platform.h"
#include "Core/ConfigManager.h"
#include "Core/Debug/Log.h"
#include "Core/HotReloadManager.h"
#include "Core/EventSystem.h"
#include "Core/Concurrency/AsyncIO.h"

namespace Limitless
{
	Application::Application()
	{
		// Initialize AsyncIO system with thread count from config
		auto& asyncIO = Limitless::Async::GetAsyncIO();
		auto& configManager = Limitless::ConfigManager::GetInstance();
		size_t threadCount = configManager.GetValue<size_t>("system.max_threads", 0);
		asyncIO.Initialize(threadCount);

		// Initialize hot reload manager
		auto& hotReloadManager = Limitless::HotReloadManager::GetInstance();
		hotReloadManager.Initialize();
		hotReloadManager.EnableHotReload(true);
	}

	Application::~Application()
	{
        // Shutdown hot reload manager
        auto& hotReloadManager = Limitless::HotReloadManager::GetInstance();
        hotReloadManager.Shutdown();

        // Shutdown AsyncIO system
        auto& asyncIO = Limitless::Async::GetAsyncIO();
        asyncIO.Shutdown();
	}

	void Application::Run()
	{
		if (!InternalInitialize())
		{
			return;
		}

		while(m_isRunning)
		{
			m_Window->OnUpdate();
			
			// Process events
			GetEventSystem().ProcessEvents();
		}

		InternalShutdown();
	}

	bool Application::InternalInitialize()
	{
		// Initialize platform detection first
		PlatformDetection::Initialize();
		
		// Initialize event system
		GetEventSystem().Initialize();

		// Initialize SDL
		auto sdlInitResult = SDLManager::GetInstance().Initialize();
		if (sdlInitResult.IsFailure())
		{
			return false;
		}

		// Create window using configuration
		m_Window = Window::CreateFromConfig();
		if (!m_Window)
		{
			return false;
		}

		// Subscribe to events (if it's an SDLWindow)
		if (auto* sdlWindow = dynamic_cast<SDLWindow*>(m_Window.get()))
		{
			sdlWindow->SubscribeToEvents();
		}

		// Register window with hot reload manager
		auto& hotReloadManager = HotReloadManager::GetInstance();
		hotReloadManager.SetWindow(m_Window.get());

		// Set up close callback
		m_Window->SetCloseCallback([this]() 
		{
			m_isRunning = false;
		});

		if (!Initialize())
		{
			return false;
		}

		return true;
	}

	void Application::InternalShutdown()
	{
		Shutdown();
		
		// Clean up window (this will unsubscribe from events)
		m_Window.reset();
		
		// Shutdown SDL
		SDLManager::GetInstance().Shutdown();
		
		// Shutdown event system AFTER window is destroyed
		GetEventSystem().Shutdown();
		
		// Shutdown logging
		Log::Shutdown();
	}
}