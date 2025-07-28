#pragma once

#include "Platform/Window.h"
#include <SDL3/SDL.h>

namespace Limitless
{
    class SDLWindow : public Window
    {
    public:
        SDLWindow(const WindowProps& props);
        virtual ~SDLWindow();

        void OnUpdate() override;

        uint32_t GetWidth() const override { return m_Data.Width; }
        uint32_t GetHeight() const override { return m_Data.Height; }

        void SetVSync(bool enabled) override;
        bool IsVSync() const override { return m_Data.VSync; }

        void* GetNativeWindow() const override { return m_Window; }

        void SetCloseCallback(std::function<void()> callback) override { m_CloseCallback = callback; }

        void* GetNativeWindowHandle() const override { return m_Window; }
        void GetWindowSize(int* width, int* height) const override;

    private:
        virtual void Init(const WindowProps& props);
        virtual void Shutdown();

    private:
        SDL_Window* m_Window;

        struct WindowData
        {
            std::string Title;
            uint32_t Width, Height;
            bool VSync;

            WindowData()
                : Title("Limitless Engine"), Width(1280), Height(720), VSync(false)
            {
            }
        };

        WindowData m_Data;
        std::function<void()> m_CloseCallback;
    };
} 