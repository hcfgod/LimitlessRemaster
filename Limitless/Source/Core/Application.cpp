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
#include "Graphics/GraphicsAPIDetector.h"
#include "Graphics/Renderer.h"
#include "Core/Time.h"
#include <chrono>

namespace Limitless
{
	Application::Application()
	{
		LT_CORE_INFO("Application constructor starting...");
		
		// Initialize AsyncIO system with thread count from config
		auto& asyncIO = Limitless::Async::GetAsyncIO();
		auto& configManager = Limitless::ConfigManager::GetInstance();
		size_t threadCount = configManager.GetValue<size_t>("system.max_threads", 0);
		asyncIO.Initialize(threadCount);

		// Initialize hot reload manager
		auto& hotReloadManager = Limitless::HotReloadManager::GetInstance();
		hotReloadManager.Initialize();
		hotReloadManager.EnableHotReload(true);

		LT_CORE_INFO("Application constructor completed successfully");
	}

	Application::~Application()
	{
		LT_CORE_INFO("Application destructor starting...");
		
        // Shutdown hot reload manager
        auto& hotReloadManager = Limitless::HotReloadManager::GetInstance();
        hotReloadManager.Shutdown();

        // Shutdown AsyncIO system
        auto& asyncIO = Limitless::Async::GetAsyncIO();
        asyncIO.Shutdown();
        
        LT_CORE_INFO("Application destructor completed");
	}

	void Application::Run()
	{
		LT_CORE_INFO("Application::Run() starting...");
		
		if (!InternalInitialize())
		{
			LT_CORE_ERROR("Application internal initialization failed!");
			return;
		}

		LT_CORE_INFO("Application internal initialization completed, entering main loop...");

		while(m_isRunning)
		{
			// Update engine time once per frame (Unity-style)
			Time::Update();
			const float deltaTime = Time::GetDeltaTimeSeconds();
			const float fixedDeltaTime = Time::GetFixedDeltaTimeSeconds();

			// Apply any pending hot reload diffs on the main thread
            Limitless::HotReloadManager::GetInstance().Update();

            m_Window->OnUpdate();

            // Update layers
            // FixedUpdate-style steps (deterministic simulation).
            while (Time::TryConsumeFixedStep()) 
			{
                m_LayerStack.OnFixedUpdate(fixedDeltaTime);
            }

            m_LayerStack.OnUpdate(deltaTime);

			// Begin frame
			Renderer::GetInstance().BeginFrame();
			
			// Process events (this will also dispatch to layers)
			GetEventSystem().ProcessEvents();
			
			// Render layers
			m_LayerStack.OnRender();
			
			// End frame and swap buffers
			Renderer::GetInstance().EndFrame();
			Renderer::GetInstance().SwapBuffers();
		}

		LT_CORE_INFO("Main loop ended, beginning shutdown...");
		InternalShutdown();
		LT_CORE_INFO("Application::Run() completed");
	}

	bool Application::InternalInitialize()
	{
		LT_CORE_INFO("Application::InternalInitialize() starting...");
		
		// Initialize platform detection first
		PlatformDetection::Initialize();

		// Initialize time system (must happen before the first frame)
		Time::Initialize();
		
		// Initialize event system
		GetEventSystem().Initialize();

		// Initialize SDL
		auto sdlInitResult = SDLManager::GetInstance().Initialize();
		if (sdlInitResult.IsFailure())
		{
			return false;
		}

		// Initialize graphics API detection system
		GraphicsAPIDetector::Initialize();

		// Create window using configuration
		m_Window = Window::CreateFromConfig();
		if (!m_Window)
		{
			LT_CORE_ERROR("Window creation failed!");
			return false;
		}

		// Initialize the global renderer with the graphics context from the window
		auto graphicsContext = m_Window->GetGraphicsContext();
		if (graphicsContext)
		{
			Renderer::GetInstance().Initialize(graphicsContext);
		}
		else
		{
			LT_CORE_ERROR("Window does not have a graphics context!");
			return false;
		}

		// Register window with hot reload manager
		auto& hotReloadManager = HotReloadManager::GetInstance();
		hotReloadManager.SetWindow(m_Window.get());

		// Set up close callback
		m_Window->SetCloseCallback([this]() 
		{
			LT_CORE_INFO("Window close callback triggered, setting m_isRunning = false");
			m_isRunning = false;
		});

		// Register LayerStack with event system
		auto& eventSystem = GetEventSystem();
		eventSystem.AddListener(std::shared_ptr<EventListener>(&m_LayerStack, [](EventListener*){}));

		if (!Initialize())
		{
			LT_CORE_ERROR("User-defined Initialize() method failed!");
			return false;
		}

		return true;
	}

	void Application::InternalShutdown()
	{
		LT_CORE_INFO("Application::InternalShutdown() starting...");
		
		Shutdown();
		
		// Clear LayerStack (this will detach all layers)
		m_LayerStack.Clear();
		
		// Shutdown the renderer
		Renderer::GetInstance().Shutdown();
		
		// Clean up window (this will unsubscribe from events)
		m_Window.reset();
		
		// Shutdown SDL
		SDLManager::GetInstance().Shutdown();
		
		// Shutdown event system AFTER window is destroyed
		GetEventSystem().Shutdown();

		// Shutdown time system
		Time::Shutdown();
		
		// Note: Logging shutdown is handled in main() after this returns
		LT_CORE_INFO("Application::InternalShutdown() completed");
	}
}