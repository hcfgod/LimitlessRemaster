#pragma once

#include <string>
#include <memory>
#include <functional>

struct SDL_Window;

namespace Limitless
{
    struct WindowProps
    {
        std::string Title;
        uint32_t Width;
        uint32_t Height;

        WindowProps(const std::string& title = "Limitless Engine",
                   uint32_t width = 1280,
                   uint32_t height = 720)
            : Title(title), Width(width), Height(height)
        {
        }
    };

    class Window
    {
    public:
        virtual ~Window() = default;

        virtual void OnUpdate() = 0;
        virtual uint32_t GetWidth() const = 0;
        virtual uint32_t GetHeight() const = 0;

        virtual void SetVSync(bool enabled) = 0;
        virtual bool IsVSync() const = 0;

        virtual void* GetNativeWindow() const = 0;

        // Callback for when the window should close
        virtual void SetCloseCallback(std::function<void()> callback) = 0;

        // Get window handle for custom rendering systems
        virtual void* GetNativeWindowHandle() const = 0;
        
        // Get window surface info for custom rendering
        virtual void GetWindowSize(int* width, int* height) const = 0;

        static std::unique_ptr<Window> Create(const WindowProps& props = WindowProps());
    };
} 