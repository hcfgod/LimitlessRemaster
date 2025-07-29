#include "Platform/SDL/SDLWindow.h"
#include <SDL3/SDL.h>
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
        m_Data.Fullscreen = props.Fullscreen;
        m_Data.Resizable = props.Resizable;

        std::cout << "Creating window " << props.Title << " (" << props.Width << ", " << props.Height << ")" << std::endl;
        if (props.Fullscreen) {
            std::cout << "Window will be fullscreen" << std::endl;
        }
        if (props.Resizable) {
            std::cout << "Window will be resizable" << std::endl;
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