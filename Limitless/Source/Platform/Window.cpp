#include "Window.h"
#include "Platform/SDL/SDLWindow.h"
#include "Core/ConfigManager.h"
#include "Core/Debug/Log.h"

namespace Limitless
{
    std::unique_ptr<Window> Window::Create(const WindowProps& props)
    {
        LT_VERIFY(!props.Title.empty(), "Window title cannot be empty");
        LT_VERIFY(props.Width > 0, "Window width must be greater than 0");
        LT_VERIFY(props.Height > 0, "Window height must be greater than 0");
        
        try
        {
            auto window = std::make_unique<SDLWindow>(props);
            LT_CORE_INFO("Window created successfully: {} ({}x{})", props.Title, props.Width, props.Height);
            return window;
        }
        catch (const Error& error)
        {
            LT_CORE_ERROR("Failed to create window: {}", error.GetErrorMessage());
            throw; // Re-throw the error
        }
        catch (const std::exception& e)
        {
            std::string errorMsg = fmt::format("Unexpected error creating window: {}", e.what());
            PlatformError error(errorMsg, std::source_location::current());
            error.SetFunctionName("Window::Create");
            error.SetClassName("Window");
            error.SetModuleName("Platform");
            error.AddContext("title", props.Title);
            error.AddContext("width", std::to_string(props.Width));
            error.AddContext("height", std::to_string(props.Height));
            
            LT_CORE_ERROR("{}", errorMsg);
            Error::LogError(error);
            LT_THROW_PLATFORM_ERROR(errorMsg);
        }
    }
    
    std::unique_ptr<Window> Window::CreateFromConfig()
    {
        try
        {
            auto& config = GetConfigManager();
            
            WindowProps props;
            
            // Basic window properties
            props.Title = config.GetValue<std::string>(Config::Window::TITLE, "Limitless Engine");
            props.Width = config.GetValue<uint32_t>(Config::Window::WIDTH, 1280);
            props.Height = config.GetValue<uint32_t>(Config::Window::HEIGHT, 720);
            props.Fullscreen = config.GetValue<bool>(Config::Window::FULLSCREEN, false);
            props.Resizable = config.GetValue<bool>(Config::Window::RESIZABLE, true);
            
            // Position
            props.PositionX = config.GetValue<int>("window.position.x", 0);
            props.PositionY = config.GetValue<int>("window.position.y", 0);
            
            // Advanced properties
            props.VSync = config.GetValue<bool>(Config::Window::VSYNC, true);
            props.HighDPI = config.GetValue<bool>("window.high_dpi", true);
            props.Borderless = config.GetValue<bool>("window.borderless", false);
            props.AlwaysOnTop = config.GetValue<bool>("window.always_on_top", false);
            
            // Size constraints
            props.MinWidth = config.GetValue<uint32_t>("window.min_width", 0);
            props.MinHeight = config.GetValue<uint32_t>("window.min_height", 0);
            props.MaxWidth = config.GetValue<uint32_t>("window.max_width", 0);
            props.MaxHeight = config.GetValue<uint32_t>("window.max_height", 0);
            
            // Icon path
            props.IconPath = config.GetValue<std::string>("window.icon", "");
            
            // Window flags
            WindowFlags flags = WindowFlags::Resizable;
            if (props.Resizable) flags |= WindowFlags::Resizable;
            if (props.Fullscreen) flags |= WindowFlags::Fullscreen;
            if (props.Borderless) flags |= WindowFlags::Borderless;
            if (props.AlwaysOnTop) flags |= WindowFlags::AlwaysOnTop;
            if (props.HighDPI) flags |= WindowFlags::AllowHighDPI;
            
            props.Flags = flags;
            
            LT_CORE_INFO("Window configuration from config:");
            LT_CORE_INFO("  Title: {}", props.Title);
            LT_CORE_INFO("  Size: {}x{}", props.Width, props.Height);
            LT_CORE_INFO("  Position: ({}, {})", props.PositionX, props.PositionY);
            LT_CORE_INFO("  Fullscreen: {}", (props.Fullscreen ? "true" : "false"));
            LT_CORE_INFO("  Resizable: {}", (props.Resizable ? "true" : "false"));
            LT_CORE_INFO("  Borderless: {}", (props.Borderless ? "true" : "false"));
            LT_CORE_INFO("  Always on top: {}", (props.AlwaysOnTop ? "true" : "false"));
            LT_CORE_INFO("  High DPI: {}", (props.HighDPI ? "true" : "false"));
            LT_CORE_INFO("  VSync: {}", (props.VSync ? "true" : "false"));
            
            if (props.MinWidth > 0 || props.MinHeight > 0) {
                LT_CORE_INFO("  Min size: {}x{}", props.MinWidth, props.MinHeight);
            }
            if (props.MaxWidth > 0 || props.MaxHeight > 0) {
                LT_CORE_INFO("  Max size: {}x{}", props.MaxWidth, props.MaxHeight);
            }
            if (!props.IconPath.empty()) {
                LT_CORE_INFO("  Icon: {}", props.IconPath);
            }
            
            auto window = std::make_unique<SDLWindow>(props);
            
            // Apply additional window settings that can't be set during creation
            window->SetVSync(props.VSync);
            
            return window;
        }
        catch (const Error& error)
        {
            LT_CORE_ERROR("Failed to create window from config: {}", error.GetErrorMessage());
            throw; // Re-throw the error
        }
        catch (const std::exception& e)
        {
            std::string errorMsg = fmt::format("Unexpected error creating window from config: {}", e.what());
            ConfigError error(errorMsg, std::source_location::current());
            error.SetFunctionName("Window::CreateFromConfig");
            error.SetClassName("Window");
            error.SetModuleName("Platform");
            
            LT_CORE_ERROR("{}", errorMsg);
            Error::LogError(error);
            LT_THROW_CONFIG_ERROR(errorMsg);
        }
    }
} 