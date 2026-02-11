#pragma once

#include <memory>
#include "Error.h"
#include "LayerStack.h"

namespace Limitless
{
    class Window;
    class EventSystem;
    class InputSystem;

	class Application
	{
	public:
        static Application& GetInstance();
        static bool HasInstance();

		Application();
		virtual ~Application();

		void Run();

		// Virtual methods to be overridden by the client application
		virtual bool Initialize() = 0; // Called to initialize the application
		virtual void Shutdown() = 0;  // Called to clean up resources before exiting

		bool IsRunning() const { return m_IsRunning; }
		void SetRunning(bool running) { m_IsRunning = running; }

        Window& GetWindow() { return *m_Window; }
        EventSystem& GetEventSystem();
        InputSystem& GetInputSystem();
        LayerStack& GetLayerStack() { return m_LayerStack; }

        // Layer management convenience methods
        void PushLayer(LayerRef layer) { m_LayerStack.PushLayer(layer); }
        void PushOverlay(LayerRef overlay) { m_LayerStack.PushOverlay(overlay); }
        void PopLayer(LayerRef layer) { m_LayerStack.PopLayer(layer); }
        void PopOverlay(LayerRef overlay) { m_LayerStack.PopOverlay(overlay); }

	private:
		bool m_IsRunning = true;
		std::unique_ptr<Window> m_Window;
		LayerStack m_LayerStack;
 
		bool InternalInitialize();
		void InternalShutdown();
	};

	// To be defined by the client application.
	// Returns unique_ptr so main() can take ownership and avoid leaks on exceptions.
	std::unique_ptr<Application> CreateApplication();
}