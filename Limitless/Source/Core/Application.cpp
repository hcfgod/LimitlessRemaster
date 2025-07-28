#include "Application.h"
#include "iostream"

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
		InternalInitialize();

		while(m_isRunning)
		{
			break; // Placeholder for the main loop logic
		}

		InternalShutdown();
	}

	bool Application::InternalInitialize()
	{
		std::cout << "Application initializing..." << std::endl;

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
		std::cout << "Application has been successfully shutdown." << std::endl;
	}
}