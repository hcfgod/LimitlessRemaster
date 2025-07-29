#include "Platform/Window.h"
#include "Platform/SDL/SDLWindow.h"
#include "Core/ConfigManager.h"
#include <iostream>

namespace Limitless
{
    std::unique_ptr<Window> Window::Create(const WindowProps& props)
    {
        return std::make_unique<SDLWindow>(props);
    }
    
    std::unique_ptr<Window> Window::CreateFromConfig()
    {
        auto& config = GetConfigManager();
        
        WindowProps props;
        props.Title = config.GetValue<std::string>(Config::Window::TITLE, "Limitless Engine");
        props.Width = config.GetValue<uint32_t>(Config::Window::WIDTH, 1280);
        props.Height = config.GetValue<uint32_t>(Config::Window::HEIGHT, 720);
        props.Fullscreen = config.GetValue<bool>(Config::Window::FULLSCREEN, false);
        props.Resizable = config.GetValue<bool>(Config::Window::RESIZABLE, true);
        
        // Debug output to show configuration values being used
        std::cout << "Window configuration from config:" << std::endl;
        std::cout << "  Title: " << props.Title << std::endl;
        std::cout << "  Size: " << props.Width << "x" << props.Height << std::endl;
        std::cout << "  Fullscreen: " << (props.Fullscreen ? "true" : "false") << std::endl;
        std::cout << "  Resizable: " << (props.Resizable ? "true" : "false") << std::endl;
        
        auto window = std::make_unique<SDLWindow>(props);
        
        // Apply additional window settings
        bool vsync = config.GetValue<bool>(Config::Window::VSYNC, true);
        window->SetVSync(vsync);
        std::cout << "  VSync: " << (vsync ? "true" : "false") << std::endl;
        
        return window;
    }
} 