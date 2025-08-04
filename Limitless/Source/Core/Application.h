#pragma once

#include <memory>
#include "Error.h"
#include "LayerStack.h"

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
        LayerStack& GetLayerStack() { return m_LayerStack; }

        // Layer management convenience methods
        void PushLayer(LayerRef layer) { m_LayerStack.PushLayer(layer); }
        void PushOverlay(LayerRef overlay) { m_LayerStack.PushOverlay(overlay); }
        void PopLayer(LayerRef layer) { m_LayerStack.PopLayer(layer); }
        void PopOverlay(LayerRef overlay) { m_LayerStack.PopOverlay(overlay); }

	private:
		bool m_isRunning = true;
		std::unique_ptr<Window> m_Window;
		LayerStack m_LayerStack;
		
		bool InternalInitialize();
		void InternalShutdown();
	};

	// To be defined by the client application
	Application* CreateApplication();
}