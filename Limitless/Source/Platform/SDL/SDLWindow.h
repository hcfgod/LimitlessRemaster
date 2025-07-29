#pragma once

#include "Platform/Window.h"
#include "Core/EventSystem.h"
#include <SDL3/SDL.h>
#include <vector>

namespace Limitless
{
    class SDLWindow : public Window
    {
    public:
        SDLWindow(const WindowProps& props);
        virtual ~SDLWindow();

        // Basic window operations
        void OnUpdate() override;
        uint32_t GetWidth() const override { return m_Data.Width; }
        uint32_t GetHeight() const override { return m_Data.Height; }
        void GetSize(uint32_t& width, uint32_t& height) const override;
        void SetSize(uint32_t width, uint32_t height) override;

        // Window position
        void GetPosition(int& x, int& y) const override;
        void SetPosition(int x, int y) override;
        void CenterOnScreen() override;

        // Window title and properties
        std::string GetTitle() const override { return m_Data.Title; }
        void SetTitle(const std::string& title) override;
        void SetIcon(const std::string& iconPath) override;

        // Window state management
        WindowState GetState() const override;
        void SetState(WindowState state) override;
        void Minimize() override;
        void Maximize() override;
        void Restore() override;
        void Show() override;
        void Hide() override;
        bool IsVisible() const override;
        bool IsMinimized() const override;
        bool IsMaximized() const override;

        // Fullscreen management
        void SetFullscreen(bool fullscreen) override;
        void SetFullscreenDesktop(bool fullscreen) override;
        bool IsFullscreen() const override { return m_Data.Fullscreen; }
        bool IsFullscreenDesktop() const override;
        void ToggleFullscreen() override;

        // Window flags and properties
        void SetFlags(WindowFlags flags) override;
        WindowFlags GetFlags() const override { return m_Data.Flags; }
        void SetResizable(bool resizable) override;
        bool IsResizable() const override { return m_Data.Resizable; }
        void SetBorderless(bool borderless) override;
        bool IsBorderless() const override;
        void SetAlwaysOnTop(bool alwaysOnTop) override;
        bool IsAlwaysOnTop() const override;

        // Size constraints
        void SetMinimumSize(uint32_t width, uint32_t height) override;
        void SetMaximumSize(uint32_t width, uint32_t height) override;
        void GetMinimumSize(uint32_t& width, uint32_t& height) const override;
        void GetMaximumSize(uint32_t& width, uint32_t& height) const override;

        // VSync and rendering
        void SetVSync(bool enabled) override;
        bool IsVSync() const override { return m_Data.VSync; }
        void SetHighDPI(bool enabled) override;
        bool IsHighDPI() const override;

        // Event handling
        void SubscribeToEvents();
        void UnsubscribeFromEvents();

        // Input focus and capture
        void SetInputFocus() override;
        bool HasInputFocus() const override;
        void SetMouseCapture(bool capture) override;
        bool IsMouseCaptured() const override;
        void SetInputGrabbed(bool grabbed) override;
        bool IsInputGrabbed() const override;

        // Display information
        int GetDisplayIndex() const override;
        DisplayMode GetDisplayMode() const override;
        std::vector<DisplayMode> GetAvailableDisplayModes() const override;
        void SetDisplayMode(const DisplayMode& mode) override;
        float GetDisplayScale() const override;
        void GetDrawableSize(uint32_t& width, uint32_t& height) const override;

        // Native window access
        void* GetNativeWindow() const override { return m_Window; }
        void* GetNativeWindowHandle() const override { return m_Window; }
        void GetWindowSize(int* width, int* height) const override;

        // Event callbacks
        void SetCloseCallback(std::function<void()> callback) override { m_CloseCallback = callback; }
        void SetResizeCallback(std::function<void(uint32_t, uint32_t)> callback) override { m_ResizeCallback = callback; }
        void SetMoveCallback(std::function<void(int, int)> callback) override { m_MoveCallback = callback; }
        void SetFocusCallback(std::function<void(bool)> callback) override { m_FocusCallback = callback; }
        void SetStateChangeCallback(std::function<void(WindowState)> callback) override { m_StateChangeCallback = callback; }

        // Window events
        void SetEventCallback(std::function<void(WindowEventType)> callback) override { m_EventCallback = callback; }

        // Utility methods
        void Flash() override;
        void RequestAttention() override;
        void SetOpacity(float opacity) override;
        float GetOpacity() const override;
        void SetBrightness(float brightness) override;
        float GetBrightness() const override;

        // Window information
        std::string GetWindowID() const override;
        bool IsForeign() const override;

        // Clipboard operations
        void SetClipboardText(const std::string& text) override;
        std::string GetClipboardText() const override;
        bool HasClipboardText() const override;

        // Cursor management
        void SetCursor(void* cursor) override;
        void SetCursorVisible(bool visible) override;
        bool IsCursorVisible() const override;
        void SetCursorPosition(int x, int y) override;
        void GetCursorPosition(int& x, int& y) const override;

        // Window hints (platform-specific)
        void SetHint(const std::string& name, const std::string& value) override;
        std::string GetHint(const std::string& name) const override;

    private:
        virtual void Init(const WindowProps& props);
        virtual void Shutdown();
        void HandleWindowEvent(const SDL_Event& event);
        void OnWindowConfigChanged(Events::WindowConfigChangedEvent& event);
        void OnWindowConfigChangedCallback(Event& event);
        void UpdateWindowFlags();
        void ApplyWindowFlags();
        SDL_WindowFlags ConvertToSDLFags(WindowFlags flags) const;
        WindowFlags ConvertFromSDLFags(SDL_WindowFlags flags) const;

    private:
        SDL_Window* m_Window;

        struct WindowData
        {
            std::string Title;
            uint32_t Width, Height;
            bool VSync;
            bool Fullscreen;
            bool Resizable;
            WindowFlags Flags;
            int PositionX, PositionY;
            uint32_t MinWidth, MinHeight, MaxWidth, MaxHeight;
            float Opacity;
            float Brightness;
            bool HighDPI;
            bool Borderless;
            bool AlwaysOnTop;
            std::string IconPath;

            WindowData()
                : Title("Limitless Engine"), Width(1280), Height(720), VSync(false), 
                  Fullscreen(false), Resizable(true), Flags(WindowFlags::Resizable),
                  PositionX(0), PositionY(0), MinWidth(0), MinHeight(0), MaxWidth(0), MaxHeight(0),
                  Opacity(1.0f), Brightness(1.0f), HighDPI(true), Borderless(false), AlwaysOnTop(false)
            {
            }
        };

        WindowData m_Data;
        std::function<void()> m_CloseCallback;
        std::function<void(uint32_t, uint32_t)> m_ResizeCallback;
        std::function<void(int, int)> m_MoveCallback;
        std::function<void(bool)> m_FocusCallback;
        std::function<void(WindowState)> m_StateChangeCallback;
        std::function<void(WindowEventType)> m_EventCallback;
    };
} 