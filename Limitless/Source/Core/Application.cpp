#include "Application.h"
#include "Platform/Window.h"
#include "Platform/SDL/SDLManager.h"
#include <iostream>

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
			std::cerr << "Failed to initialize application!" << std::endl;
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
		std::cout << "Application initializing..." << std::endl;

		// Initialize SDL
		if (!SDLManager::GetInstance().Initialize())
		{
			std::cerr << "Failed to initialize SDL!" << std::endl;
			return false;
		}

		// Create window
		m_Window = Window::Create();
		if (!m_Window)
		{
			std::cerr << "Failed to create window!" << std::endl;
			return false;
		}

		// Set up close callback
		m_Window->SetCloseCallback([this]() {
			m_isRunning = false;
		});

		if (!Initialize())
		{
			std::cerr << "Application initialization failed!" << std::endl;
			return false;
		}

		std::cout << "Application fully initialized!" << std::endl;

		return true;
	}

	void Application::InternalShutdown()
	{
		Shutdown();
		
		// Clean up window
		m_Window.reset();
		
		// Shutdown SDL
		SDLManager::GetInstance().Shutdown();
		
		std::cout << "Application has been successfully shutdown." << std::endl;
	}
}