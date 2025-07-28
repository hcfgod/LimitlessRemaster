#pragma once

#include <memory>
#include "Logger.h"
#include "LogManager.h"
#include "Error.h"

namespace Limitless
{
    class Window;

	class Application
	{
	public:
		Application();
		virtual ~Application();

		void Run();

		// Virtual methods to be overridden by the client application
		virtual bool Initialize() = 0; // Called to initialize the application
		virtual void Shutdown() = 0;  // Called to clean up resources before exiting

		bool IsRunning() const { return m_isRunning; }
		void SetRunning(bool running) { m_isRunning = running; }

        Window& GetWindow() { return *m_Window; }

        // Logging access
        Logger* GetLogger() const { return m_Logger; }
        LogManager& GetLogManager() { return LogManager::GetInstance(); }

	private:
		bool m_isRunning = true;
		std::unique_ptr<Window> m_Window;
		Logger* m_Logger = nullptr;
		
		bool InternalInitialize();
		void InternalShutdown();
	};

	// To be defined by the client application
	Application* CreateApplication();
}