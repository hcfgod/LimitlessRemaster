#include "Platform/SDL/SDLWindow.h"
#include <iostream>

namespace Limitless
{
    SDLWindow::SDLWindow(const WindowProps& props)
    {
        Init(props);
    }

    SDLWindow::~SDLWindow()
    {
        Shutdown();
    }

    void SDLWindow::Init(const WindowProps& props)
    {
        m_Data.Title = props.Title;
        m_Data.Width = props.Width;
        m_Data.Height = props.Height;

        std::cout << "Creating window " << props.Title << " (" << props.Width << ", " << props.Height << ")" << std::endl;

        // Create window
        m_Window = SDL_CreateWindow(
            props.Title.c_str(),
            props.Width,
            props.Height,
            SDL_WINDOW_RESIZABLE
        );

        if (!m_Window)
        {
            std::cerr << "Error creating SDL window: " << SDL_GetError() << std::endl;
            return;
        }

        // Note: No SDL renderer - using custom rendering system
        std::cout << "Window created successfully - ready for custom rendering" << std::endl;
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
        std::cout << "VSync " << (enabled ? "enabled" : "disabled") << " - handled by custom renderer" << std::endl;
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
} 