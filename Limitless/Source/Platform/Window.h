#pragma once

#include <string>
#include <memory>
#include <functional>
#include <vector>
#include <optional>

struct SDL_Window;

namespace Limitless
{
    // Display mode structure
    struct DisplayMode
    {
        uint32_t width;
        uint32_t height;
        uint32_t refreshRate;
        uint32_t format;
        
        DisplayMode(uint32_t w = 0, uint32_t h = 0, uint32_t refresh = 0, uint32_t fmt = 0)
            : width(w), height(h), refreshRate(refresh), format(fmt) {}
    };

    // Window flags for advanced configuration
    enum class WindowFlags : uint32_t
    {
        None = 0,
        Fullscreen = 1 << 0,
        FullscreenDesktop = 1 << 1,
        Resizable = 1 << 2,
        Minimizable = 1 << 3,
        Maximizable = 1 << 4,
        Hidden = 1 << 5,
        Borderless = 1 << 6,
        AlwaysOnTop = 1 << 7,
        SkipTaskbar = 1 << 8,
        Utility = 1 << 9,
        Tooltip = 1 << 10,
        PopupMenu = 1 << 11,
        InputGrabbed = 1 << 12,
        InputFocus = 1 << 13,
        MouseFocus = 1 << 14,
        Foreign = 1 << 15,
        AllowHighDPI = 1 << 16,
        MouseCapture = 1 << 17,
        AlwaysOnTopHint = 1 << 18,
        BypassWindowManager = 1 << 19
    };

    // Window state
    enum class WindowState
    {
        Normal,
        Minimized,
        Maximized,
        Fullscreen,
        FullscreenDesktop
    };

    // Window events
    enum class WindowEventType
    {
        Shown,
        Hidden,
        Exposed,
        Moved,
        Resized,
        SizeChanged,
        Minimized,
        Maximized,
        Restored,
        Enter,
        Leave,
        FocusGained,
        FocusLost,
        Close,
        TakeFocus,
        HitTest
    };

    struct WindowProps
    {
        std::string Title;
        uint32_t Width;
        uint32_t Height;
        bool Fullscreen;
        bool Resizable;
        int PositionX;
        int PositionY;
        WindowFlags Flags;
        std::string IconPath;
        bool VSync;
        bool HighDPI;
        bool Borderless;
        bool AlwaysOnTop;
        uint32_t MinWidth;
        uint32_t MinHeight;
        uint32_t MaxWidth;
        uint32_t MaxHeight;

        WindowProps(const std::string& title = "Limitless Engine",
                   uint32_t width = 1280,
                   uint32_t height = 720,
                   bool fullscreen = false,
                   bool resizable = true)
            : Title(title), Width(width), Height(height), Fullscreen(fullscreen), Resizable(resizable),
              PositionX(0), PositionY(0), Flags(WindowFlags::Resizable), VSync(true), HighDPI(true),
              Borderless(false), AlwaysOnTop(false), MinWidth(0), MinHeight(0), MaxWidth(0), MaxHeight(0)
        {
        }
    };

    class Window
    {
    public:
        virtual ~Window() = default;

        // Basic window operations
        virtual void OnUpdate() = 0;
        virtual uint32_t GetWidth() const = 0;
        virtual uint32_t GetHeight() const = 0;
        virtual void GetSize(uint32_t& width, uint32_t& height) const = 0;
        virtual void SetSize(uint32_t width, uint32_t height) = 0;

        // Window position
        virtual void GetPosition(int& x, int& y) const = 0;
        virtual void SetPosition(int x, int y) = 0;
        virtual void CenterOnScreen() = 0;

        // Window title and properties
        virtual std::string GetTitle() const = 0;
        virtual void SetTitle(const std::string& title) = 0;
        virtual void SetIcon(const std::string& iconPath) = 0;

        // Window state management
        virtual WindowState GetState() const = 0;
        virtual void SetState(WindowState state) = 0;
        virtual void Minimize() = 0;
        virtual void Maximize() = 0;
        virtual void Restore() = 0;
        virtual void Show() = 0;
        virtual void Hide() = 0;
        virtual bool IsVisible() const = 0;
        virtual bool IsMinimized() const = 0;
        virtual bool IsMaximized() const = 0;

        // Fullscreen management
        virtual void SetFullscreen(bool fullscreen) = 0;
        virtual void SetFullscreenDesktop(bool fullscreen) = 0;
        virtual bool IsFullscreen() const = 0;
        virtual bool IsFullscreenDesktop() const = 0;
        virtual void ToggleFullscreen() = 0;

        // Window flags and properties
        virtual void SetFlags(WindowFlags flags) = 0;
        virtual WindowFlags GetFlags() const = 0;
        virtual void SetResizable(bool resizable) = 0;
        virtual bool IsResizable() const = 0;
        virtual void SetBorderless(bool borderless) = 0;
        virtual bool IsBorderless() const = 0;
        virtual void SetAlwaysOnTop(bool alwaysOnTop) = 0;
        virtual bool IsAlwaysOnTop() const = 0;

        // Size constraints
        virtual void SetMinimumSize(uint32_t width, uint32_t height) = 0;
        virtual void SetMaximumSize(uint32_t width, uint32_t height) = 0;
        virtual void GetMinimumSize(uint32_t& width, uint32_t& height) const = 0;
        virtual void GetMaximumSize(uint32_t& width, uint32_t& height) const = 0;

        // VSync and rendering
        virtual void SetVSync(bool enabled) = 0;
        virtual bool IsVSync() const = 0;
        virtual void SetHighDPI(bool enabled) = 0;
        virtual bool IsHighDPI() const = 0;

        // Input focus and capture
        virtual void SetInputFocus() = 0;
        virtual bool HasInputFocus() const = 0;
        virtual void SetMouseCapture(bool capture) = 0;
        virtual bool IsMouseCaptured() const = 0;
        virtual void SetInputGrabbed(bool grabbed) = 0;
        virtual bool IsInputGrabbed() const = 0;

        // Display information
        virtual int GetDisplayIndex() const = 0;
        virtual DisplayMode GetDisplayMode() const = 0;
        virtual std::vector<DisplayMode> GetAvailableDisplayModes() const = 0;
        virtual void SetDisplayMode(const DisplayMode& mode) = 0;
        virtual float GetDisplayScale() const = 0;
        virtual void GetDrawableSize(uint32_t& width, uint32_t& height) const = 0;

        // Native window access
        virtual void* GetNativeWindow() const = 0;
        virtual void* GetNativeWindowHandle() const = 0;
        virtual void GetWindowSize(int* width, int* height) const = 0;

        // Event callbacks
        virtual void SetCloseCallback(std::function<void()> callback) = 0;
        virtual void SetResizeCallback(std::function<void(uint32_t, uint32_t)> callback) = 0;
        virtual void SetMoveCallback(std::function<void(int, int)> callback) = 0;
        virtual void SetFocusCallback(std::function<void(bool)> callback) = 0;
        virtual void SetStateChangeCallback(std::function<void(WindowState)> callback) = 0;

        // Window events
        virtual void SetEventCallback(std::function<void(WindowEventType)> callback) = 0;

        // Utility methods
        virtual void Flash() = 0;
        virtual void RequestAttention() = 0;
        virtual void SetOpacity(float opacity) = 0;
        virtual float GetOpacity() const = 0;
        virtual void SetBrightness(float brightness) = 0;
        virtual float GetBrightness() const = 0;

        // Window information
        virtual std::string GetWindowID() const = 0;
        virtual bool IsForeign() const = 0;

        // Clipboard operations
        virtual void SetClipboardText(const std::string& text) = 0;
        virtual std::string GetClipboardText() const = 0;
        virtual bool HasClipboardText() const = 0;

        // Cursor management
        virtual void SetCursor(void* cursor) = 0;
        virtual void SetCursorVisible(bool visible) = 0;
        virtual bool IsCursorVisible() const = 0;
        virtual void SetCursorPosition(int x, int y) = 0;
        virtual void GetCursorPosition(int& x, int& y) const = 0;

        // Window hints (platform-specific)
        virtual void SetHint(const std::string& name, const std::string& value) = 0;
        virtual std::string GetHint(const std::string& name) const = 0;

        // Factory methods
        static std::unique_ptr<Window> Create(const WindowProps& props = WindowProps());
        static std::unique_ptr<Window> CreateFromConfig();
    };

    // Window flags operators
    inline WindowFlags operator|(WindowFlags a, WindowFlags b) {
        return static_cast<WindowFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    inline WindowFlags operator&(WindowFlags a, WindowFlags b) {
        return static_cast<WindowFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }

    inline WindowFlags operator~(WindowFlags a) {
        return static_cast<WindowFlags>(~static_cast<uint32_t>(a));
    }

    inline WindowFlags& operator|=(WindowFlags& a, WindowFlags b) {
        a = a | b;
        return a;
    }

    inline WindowFlags& operator&=(WindowFlags& a, WindowFlags b) {
        a = a & b;
        return a;
    }
} 