#include "Application.h"
#include "Core/EventSystem.h"
#include "Core/Input/InputSystem.h"
#include "Platform/Window.h"
#include "Platform/SDL/SDLWindow.h"
#include "Platform/SDL/SDLManager.h"
#include "Platform/Platform.h"
#include "Core/ConfigManager.h"
#include "Core/Debug/Log.h"
#include "Core/HotReloadManager.h"
#include "Core/EventSystem.h"
#include "Core/Concurrency/AsyncIO.h"
#include "Assets/AssetHotReloadManager.h"
#include "Assets/AssetLoadCoordinator.h"
#include "Assets/AssetBundle.h"
#include "Audio/AudioEngine.h"
#include "Graphics/GraphicsAPIDetector.h"
#include "Graphics/Renderer.h"
#include "Core/Time.h"
#include "Core/Input/InputSystem.h"
#include <chrono>

namespace Limitless
{
    static Application* s_ApplicationInstance = nullptr;

    Application& Application::GetInstance()
    {
        LT_VERIFY(s_ApplicationInstance != nullptr, "Application instance not available");
        return *s_ApplicationInstance;
    }

    bool Application::HasInstance()
    {
        return s_ApplicationInstance != nullptr;
    }

	Application::Application()
	{
        LT_VERIFY(s_ApplicationInstance == nullptr, "Only one Application instance is supported");
        s_ApplicationInstance = this;

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

	EventSystem& Application::GetEventSystem()
	{
		return ::Limitless::GetEventSystem();
	}

	InputSystem& Application::GetInputSystem()
	{
		return ::Limitless::GetInputSystem();
	}

	void Application::SetImGuiCallbacks(std::function<void()> beginFrame, std::function<void()> endFrame)
	{
		m_ImGuiBeginFrame = std::move(beginFrame);
		m_ImGuiEndFrame = std::move(endFrame);
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

        m_ImGuiBeginFrame = nullptr;
        m_ImGuiEndFrame = nullptr;

        if (s_ApplicationInstance == this)
        {
            s_ApplicationInstance = nullptr;
        }
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

		while(m_IsRunning)
		{
			// Update engine time once per frame (Unity-style)
			Time::Update();
			const float deltaTime = Time::GetDeltaTimeSeconds();
			const float fixedDeltaTime = Time::GetFixedDeltaTimeSeconds();

            // Begin input frame: clears per-frame deltas (mouse, wheel) and pressed/released flags.
            GetInputSystem().BeginFrame();

			// Apply any pending hot reload diffs on the main thread
            Limitless::HotReloadManager::GetInstance().Update();

            m_Window->OnUpdate();

            // Update input actions after pumping events so Layers can poll action values during OnUpdate.
            GetInputSystem().UpdateActions();

            // ImGui begin frame (before any layer may call ImGui::Begin).
            if (m_ImGuiBeginFrame)
                m_ImGuiBeginFrame();

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

			// ImGui end frame (render ImGui on top of all layers).
			if (m_ImGuiEndFrame)
				m_ImGuiEndFrame();
			
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

        // -----------------------------------------------------------------------------
        // AssetBundle auto-load (shipping/bundle-only mode)
        // Try to load a packaged bundle before any user layers attempt to load assets.
        // Layouts checked (in order):
        // - <exeDir>/AssetBundle/AssetBundleManifest.json
        // - <workingDir>/AssetBundle/AssetBundleManifest.json
        // -----------------------------------------------------------------------------
        {
            auto& bundle = Limitless::Assets::AssetBundle::GetInstance();

            Result<void> loadResult(ErrorCode::ResourceNotFound, "AssetBundle not probed yet");
            loadResult = bundle.LoadFromExecutableDirectory();
            if (loadResult.IsFailure())
            {
                const std::filesystem::path workingDir = std::filesystem::path(PlatformDetection::GetWorkingDirectory());
                if (!workingDir.empty())
                {
                    loadResult = bundle.LoadFromDirectory(workingDir / "AssetBundle");
                }
            }

            if (loadResult.IsSuccess())
            {
                bundle.Enable(true);
                Limitless::Assets::AssetHotReloadManager::GetInstance().Enable(false);
                LT_CORE_INFO("AssetBundle: enabled (auto-loaded).");
            }
            else
            {
                // Dev default: source assets on disk, hot reload allowed if the app enables it.
                LT_CORE_INFO("AssetBundle: not enabled (auto-load failed): {}", loadResult.GetError().GetErrorMessage());
            }
        }

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

        // Initialize global audio engine (Unity-style).
        // This requires SDL_INIT_AUDIO (enabled in SDLManager::Initialize).
        if (!Audio::AudioEngine::GetInstance().Initialize())
        {
            LT_CORE_WARN("AudioEngine failed to initialize. Audio playback will be disabled for this session.");
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
			LT_CORE_INFO("Window close callback triggered, setting m_IsRunning = false");
			m_IsRunning = false;
		});

		// Register LayerStack with event system (non-owned: we own m_LayerStack)
		GetEventSystem().AddListenerNonOwned(&m_LayerStack);

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

        // Stop asset hot reload + cancel in-flight loads before tearing down renderer/window.
        // This prevents background asset threads from running during CRT/static teardown.
        Limitless::Assets::AssetLoadCoordinator::CancelAllInFlightLoads();
        Limitless::Assets::AssetHotReloadManager::GetInstance().Enable(false);
		
		// Shutdown the renderer
		Renderer::GetInstance().Shutdown();
		
		// Clean up window (this will unsubscribe from events)
		m_Window.reset();
		
		// Shutdown SDL
        Audio::AudioEngine::GetInstance().Shutdown();
		SDLManager::GetInstance().Shutdown();
		
		// Shutdown event system AFTER window is destroyed
		GetEventSystem().Shutdown();

		// Shutdown time system
		Time::Shutdown();
		
		// Note: Logging shutdown is handled in main() after this returns
		LT_CORE_INFO("Application::InternalShutdown() completed");
	}
}