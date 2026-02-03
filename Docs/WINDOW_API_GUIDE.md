# Window API Guide

## Overview

The Limitless window layer is designed to be:

- Cross-platform (SDL-backed platform implementation)
- Config-driven (`Window::CreateFromConfig()`)
- Feature-rich (fullscreen modes, High DPI, cursor and clipboard helpers, window hints)

The primary interfaces are `Limitless::WindowProps` and `Limitless::Window`.

## Creating a Window

### Create with explicit properties

```cpp
#include "Platform/Window.h"

Limitless::WindowProps props;
props.Title = "Limitless Engine";
props.Width = 1920;
props.Height = 1080;
props.Resizable = true;
props.Fullscreen = false;
props.HighDPI = true;
props.Borderless = false;
props.AlwaysOnTop = false;

auto window = Limitless::Window::Create(props);
```

### Create from configuration

```cpp
auto window = Limitless::Window::CreateFromConfig();
```

This reads values such as `window.width`, `window.height`, `window.title`, and other supported keys.

## Common Operations

### Size and position

```cpp
uint32_t w = window->GetWidth();
uint32_t h = window->GetHeight();

window->SetSize(1280, 720);
window->SetPosition(100, 100);
window->CenterOnScreen();
```

### State and fullscreen

```cpp
window->Minimize();
window->Maximize();
window->Restore();

window->SetFullscreen(true);
window->SetFullscreenDesktop(true);
window->ToggleFullscreen();
```

### Appearance helpers

```cpp
window->SetOpacity(0.9f);
window->SetBrightness(1.1f);

window->Flash();
window->RequestAttention();
```

### Clipboard

```cpp
window->SetClipboardText("Hello, World!");
std::string text = window->GetClipboardText();
bool hasText = window->HasClipboardText();
```

## Callbacks

The window layer exposes callbacks for common window events:

```cpp
window->SetCloseCallback([&]() {
    LT_INFO("Window closing");
});

window->SetResizeCallback([&](uint32_t width, uint32_t height) {
    LT_INFO("Resize: {}x{}", width, height);
});

window->SetMoveCallback([&](int x, int y) {
    LT_INFO("Move: {}, {}", x, y);
});

window->SetFocusCallback([&](bool focused) {
    LT_INFO("Focus: {}", focused);
});
```

There is also a generic `SetEventCallback` for platform-level window events (`WindowEventType`).

## Display Information

```cpp
int displayIndex = window->GetDisplayIndex();
auto currentMode = window->GetDisplayMode();
auto modes = window->GetAvailableDisplayModes();
float scale = window->GetDisplayScale();
```

## Flags and Hints

Flags can be combined with the provided operators:

```cpp
using Limitless::WindowFlags;

window->SetFlags(WindowFlags::Resizable | WindowFlags::AllowHighDPI);
```

Hints provide platform-specific customization:

```cpp
window->SetHint("SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS", "0");
std::string value = window->GetHint("SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS");
```

## Best Practices

- Use `Window::CreateFromConfig()` for tooling and iteration-friendly development.
- Use callbacks only to update state; dispatch gameplay/application behavior through the event system.
- Prefer High DPI (`HighDPI = true`) for modern displays and UI clarity.
- Avoid toggling fullscreen repeatedly during the same frame; queue and apply once.

## Troubleshooting

- **Window fails to create**: Confirm SDL is initialized and the requested graphics API is available.
- **High DPI scaling looks incorrect**: Query `GetDisplayScale()` and consider using drawable size (`GetDrawableSize`) for rendering.
- **Fullscreen behaves differently across platforms**: Prefer `FullscreenDesktop` for consistent behavior.

