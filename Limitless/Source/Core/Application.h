#pragma once

namespace Limitless
{
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
	private:
		bool m_isRunning = true;
		bool InternalInitialize();
		void InternalShutdown();
	};

	// To be defined by the client application
	Application* CreateApplication();
}