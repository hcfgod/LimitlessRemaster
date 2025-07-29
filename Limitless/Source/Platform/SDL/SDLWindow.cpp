#include "SDLWindow.h"
#include "Core/Debug/Log.h"
#include "Core/ConfigManager.h"
#include <SDL3/SDL.h>

namespace Limitless
{
    SDLWindow::SDLWindow(const WindowProps& props)
    {
        Init(props);
    }

    SDLWindow::~SDLWindow()
    {
        UnsubscribeFromEvents();
        Shutdown();
    }

    void SDLWindow::SubscribeToEvents()
    {
        // Subscribe to window configuration change events (only if EventSystem is initialized)
        try
        {
            if (GetEventSystem().IsInitialized())
            {
                // Use callbacks instead of listeners to avoid shared_ptr issues
                GetEventSystem().AddCallback(EventType::WindowConfigChanged, 
                    [this](Event& event) { OnWindowConfigChangedCallback(event); });
            }
        }
        catch (...)
        {
            // Ignore any exceptions during initialization
            LT_CORE_WARN("SDLWindow: Warning - Could not subscribe to events during initialization");
        }
    }

    void SDLWindow::UnsubscribeFromEvents()
    {
        // Unsubscribe from events (only if EventSystem is still initialized)
        try
        {
            if (GetEventSystem().IsInitialized())
            {
                // Note: EventSystem doesn't have a way to remove callbacks by function
                // This is a limitation, but it's safer than the shared_ptr approach
            }
        }
        catch (...)
        {
            // Ignore any exceptions during shutdown
            LT_CORE_WARN("SDLWindow: Warning - Could not unsubscribe from events during shutdown");
        }
    }

    void SDLWindow::OnWindowConfigChangedCallback(Event& event)
    {
        if (event.GetType() == EventType::WindowConfigChanged)
        {
            if (auto* windowEvent = dynamic_cast<Events::WindowConfigChangedEvent*>(&event))
            {
                OnWindowConfigChanged(*windowEvent);
            }
        }
    }

    void SDLWindow::Init(const WindowProps& props)
    {
        m_Data.Title = props.Title;
        m_Data.Width = props.Width;
        m_Data.Height = props.Height;
        m_Data.Fullscreen = props.Fullscreen;
        m_Data.Resizable = props.Resizable;

        LT_CORE_INFO("Creating window {} ({}, {})", props.Title, props.Width, props.Height);
        if (props.Fullscreen) {
            LT_CORE_INFO("Window will be fullscreen");
        }
        if (props.Resizable) {
            LT_CORE_INFO("Window will be resizable");
        }

        // Set up SDL window flags
        uint32_t windowFlags = 0;
        if (props.Resizable) {
            windowFlags |= SDL_WINDOW_RESIZABLE;
        }
        if (props.Fullscreen) {
            windowFlags |= SDL_WINDOW_FULLSCREEN;
        }

        // Create window
        m_Window = SDL_CreateWindow(
            props.Title.c_str(),
            props.Width,
            props.Height,
            windowFlags
        );

        if (!m_Window)
        {
            LT_CORE_ERROR("Error creating SDL window: {}", SDL_GetError());
            return;
        }

        // Note: No SDL renderer - using custom rendering system
        LT_CORE_INFO("Window created successfully - ready for custom rendering");
    }

    void SDLWindow::Shutdown()
    {
        if (m_Window)
        {
            SDL_DestroyWindow(m_Window);
            m_Window = nullptr;
        }
    }

    void SDLWindow::OnUpdate()
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
                case SDL_EVENT_QUIT:
                    // Handle window close
                    if (m_CloseCallback)
                    {
                        m_CloseCallback();
                    }
                    break;
                case SDL_EVENT_WINDOW_RESIZED:
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                    m_Data.Width = event.window.data1;
                    m_Data.Height = event.window.data2;
                    break;
            }
        }
    }

    void SDLWindow::SetVSync(bool enabled)
    {
        m_Data.VSync = enabled;
        // VSync will be handled by your custom rendering system
        LT_CORE_INFO("VSync {} - handled by custom renderer", (enabled ? "enabled" : "disabled"));
    }

    void SDLWindow::GetWindowSize(int* width, int* height) const
    {
        if (m_Window)
        {
            SDL_GetWindowSize(m_Window, width, height);
        }
        else
        {
            *width = m_Data.Width;
            *height = m_Data.Height;
        }
    }

    // Hot reload event handlers
    void SDLWindow::OnWindowConfigChanged(Events::WindowConfigChangedEvent& event)
    {
        const std::string& key = event.GetChangedKey();
        const ConfigValue& value = event.GetNewValue();
        
        LT_CORE_INFO("SDLWindow: Handling window config change - {}", key);
        
        if (key == "window.title")
        {
            if (std::holds_alternative<std::string>(value))
            {
                std::string newTitle = std::get<std::string>(value);
                if (SDL_SetWindowTitle(m_Window, newTitle.c_str()))
                {
                    m_Data.Title = newTitle;
                    LT_CORE_INFO("SDLWindow: Window title updated to: {}", newTitle);
                }
                else
                {
                    LT_CORE_ERROR("SDLWindow: Failed to update window title: {}", SDL_GetError());
                }
            }
        }
        else if (key == "window.width" || key == "window.height")
        {
            if (std::holds_alternative<int>(value) || std::holds_alternative<uint32_t>(value))
            {
                int newWidth = m_Data.Width;
                int newHeight = m_Data.Height;
                
                if (key == "window.width")
                {
                    if (std::holds_alternative<int>(value))
                        newWidth = std::get<int>(value);
                    else if (std::holds_alternative<uint32_t>(value))
                        newWidth = static_cast<int>(std::get<uint32_t>(value));
                }
                else if (key == "window.height")
                {
                    if (std::holds_alternative<int>(value))
                        newHeight = std::get<int>(value);
                    else if (std::holds_alternative<uint32_t>(value))
                        newHeight = static_cast<int>(std::get<uint32_t>(value));
                }
                
                SDL_SetWindowSize(m_Window, newWidth, newHeight);
                m_Data.Width = newWidth;
                m_Data.Height = newHeight;
                LT_CORE_INFO("SDLWindow: Window size updated to: {}x{}", newWidth, newHeight);
            }
        }
        else if (key == "window.fullscreen")
        {
            if (std::holds_alternative<bool>(value))
            {
                bool fullscreen = std::get<bool>(value);
                if (SDL_SetWindowFullscreen(m_Window, fullscreen ? SDL_WINDOW_FULLSCREEN : 0))
                {
                    m_Data.Fullscreen = fullscreen;
                    LT_CORE_INFO("SDLWindow: Fullscreen {}", (fullscreen ? "enabled" : "disabled"));
                }
                else
                {
                    LT_CORE_ERROR("SDLWindow: Failed to set fullscreen: {}", SDL_GetError());
                }
            }
        }
        else if (key == "window.resizable")
        {
            if (std::holds_alternative<bool>(value))
            {
                bool resizable = std::get<bool>(value);
                SDL_SetWindowResizable(m_Window, resizable ? 1 : 0);
                m_Data.Resizable = resizable;
                LT_CORE_INFO("SDLWindow: Resizable {}", (resizable ? "enabled" : "disabled"));
            }
        }
        else if (key == "window.vsync")
        {
            if (std::holds_alternative<bool>(value))
            {
                bool vsync = std::get<bool>(value);
                m_Data.VSync = vsync;
                LT_CORE_INFO("SDLWindow: VSync {} - handled by custom renderer", (vsync ? "enabled" : "disabled"));
            }
        }
    }
} 