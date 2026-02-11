# ImGui Integration Guide

This guide explains how to use the Limitless ImGui integration for editor UIs, debug overlays, and tools.

## Overview

The engine integrates [Dear ImGui](https://github.com/ocornut/imgui) with SDL3 and OpenGL3 backends. Use the `ImGuiLayer` to enable ImGui in your application.

## Quick Start

1. **Push ImGuiLayer as an overlay** so it renders on top of all other layers:

```cpp
#include "ImGui/ImGuiLayer.h"

bool MyApp::Initialize()
{
    PushOverlay(CreateLayer<ImGuiLayer>());
    return true;
}
```

2. **Draw ImGui content** in any layer's `OnRender()`:

```cpp
void MyLayer::OnRender()
{
    ImGui::Begin("My Panel");
    ImGui::Text("Hello, world!");
    ImGui::End();
}
```

## ImGuiLayer

- **Lifecycle**: Registers with `Application` for correct frame ordering (NewFrame before layer updates, Render after layer render).
- **Events**: SDL events are forwarded to ImGui via `SetSdlEventCallback` on the window.
- **Demo window**: Enabled by default (`m_ShowDemoWindow = true`). Toggle or set to `false` in production.

## Include Paths

When using ImGui in app code, include:

```cpp
#include "imgui/imgui.h"
// For ImGui::ShowDemoWindow:
// (already included if you use ImGuiLayer's demo)
```

## Framebuffer Viewport

To render the game into an ImGui panel, use a `Framebuffer` and bind it before drawing your scene. See [FRAMEBUFFER_GUIDE.md](FRAMEBUFFER_GUIDE.md) for details.

```cpp
ImGui::Begin("Viewport");
ImGui::Image(
    reinterpret_cast<ImTextureID>(framebuffer->GetColorAttachment()->GetRendererID()),
    ImVec2(width, height), ImVec2(0, 1), ImVec2(1, 0));
ImGui::End();
```

## Configuration

- **imconfig.h**: `IMGUI_IMPL_OPENGL_LOADER_CUSTOM` is set so ImGui uses the engine's GLAD loader.
- **GLSL version**: `#version 130` is used for OpenGL 3.3+ compatibility.

## See Also

- [FRAMEBUFFER_GUIDE.md](FRAMEBUFFER_GUIDE.md) – Render-to-texture for viewports
- [ImGui Manual](https://github.com/ocornut/imgui/wiki) – Full ImGui documentation
