#include "Window.h"
#include "Platform/SDL/SDLWindow.h"
#include "Core/ConfigManager.h"
#include "Core/Debug/Log.h"

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
        
        LT_CORE_INFO("Window configuration from config:");
        LT_CORE_INFO("  Title: {}", props.Title);
        LT_CORE_INFO("  Size: {}x{}", props.Width, props.Height);
        LT_CORE_INFO("  Fullscreen: {}", (props.Fullscreen ? "true" : "false"));
        LT_CORE_INFO("  Resizable: {}", (props.Resizable ? "true" : "false"));
        
        auto window = std::make_unique<SDLWindow>(props);
        
        // Apply additional window settings
        bool vsync = config.GetValue<bool>(Config::Window::VSYNC, true);
        window->SetVSync(vsync);
        LT_CORE_INFO("  VSync: {}", (vsync ? "true" : "false"));
        
        return window;
    }
} 