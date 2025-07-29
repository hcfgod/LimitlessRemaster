#include "Application.h"
#include "Platform/Window.h"
#include "Platform/SDL/SDLManager.h"
#include "Core/ConfigManager.h"
#include "Core/Debug/Log.h"

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
		if (!InternalInitialize())
		{
			return;
		}

		while(m_isRunning)
		{
			m_Window->OnUpdate();
		}

		InternalShutdown();
	}

	bool Application::InternalInitialize()
	{
		// Initialize SDL
		if (!SDLManager::GetInstance().Initialize())
		{
			return false;
		}

		// Create window using configuration
		m_Window = Window::CreateFromConfig();
		if (!m_Window)
		{
			return false;
		}

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
		
		// Clean up window
		m_Window.reset();
		
		// Shutdown SDL
		SDLManager::GetInstance().Shutdown();
		
		// Shutdown logging
		Log::Shutdown();
	}
}