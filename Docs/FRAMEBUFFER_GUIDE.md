# Framebuffer Guide

This guide explains how to use the Limitless framebuffer system for render-to-texture, off-screen rendering, and editor viewports.

## Overview

Framebuffers allow you to render to a texture instead of the default backbuffer. Use cases include:

- **Editor viewports**: Render the game view into a texture for display in an ImGui panel
- **Post-processing**: Render scene to texture, then apply effects
- **Minimaps**: Render a top-down view to a small texture
- **Reflections**: Render reflected geometry to a texture

## Creating a Framebuffer

Framebuffer GPU objects are created on the **primary render context**, but callers can use the factory methods from normal engine code:

```cpp
#include "Graphics/Framebuffer.h"

// Blocking (use during initialization or when you can wait)
FramebufferSpecification spec;
spec.Width = 1920;
spec.Height = 1080;
spec.Samples = 1;
spec.DepthAttachment = true;
spec.StencilAttachment = false;

auto framebuffer = Framebuffer::Create(spec);

// Async (schedule creation, continue elsewhere)
auto future = Framebuffer::CreateAsync(spec);
// ... do other work ...
auto framebuffer = future.get();
```

Current behavior:

- `Framebuffer::Create(...)` blocks until creation completes on the primary render context
- `Framebuffer::CreateAsync(...)` schedules creation on the primary render context and returns a `std::future`
- both APIs validate that width/height are non-zero and that at least one color attachment is requested

## FramebufferSpecification

| Field | Default | Description |
|-------|--------|-------------|
| `Width` | 0 | Framebuffer width (required) |
| `Height` | 0 | Framebuffer height (required) |
| `Samples` | 1 | MSAA sample count. 0 or 1 = no MSAA |
| `ColorAttachmentCount` | 1 | Number of color attachments to create |
| `DepthAttachment` | true | Create 24-bit depth renderbuffer |
| `StencilAttachment` | false | Create stencil (combined with depth when true) |
| `SwapChainTarget` | false | Reserved for future swap-chain binding |

## Rendering to a Framebuffer

Submit a `BindFramebufferCommand` before your draw commands:

```cpp
#include "Graphics/RenderCommand.h"

// Bind our framebuffer
renderer.SubmitCommand(std::make_unique<BindFramebufferCommand>(framebuffer));

// Set viewport to match framebuffer size
renderer.SubmitCommand(std::make_unique<SetViewportCommand>(0, 0, 
    framebuffer->GetWidth(), framebuffer->GetHeight()));

// Clear and draw your scene
renderer.SubmitCommand(std::make_unique<ClearCommand>(...));
// ... draw commands ...

// Unbind (bind default framebuffer)
renderer.SubmitCommand(std::make_unique<BindFramebufferCommand>(nullptr));
```

## Getting the Color Texture

Use `GetColorAttachment()` to obtain color attachment slot `0` for display (e.g. in ImGui):

```cpp
auto colorTexture = framebuffer->GetColorAttachment();
// colorTexture is std::shared_ptr<Texture2D>

// For ImGui::Image:
ImGui::Image(
    reinterpret_cast<ImTextureID>(colorTexture->GetRendererID()),
    ImVec2(static_cast<float>(framebuffer->GetWidth()), 
           static_cast<float>(framebuffer->GetHeight())));
```

If you create multiple color attachments, use `GetColorAttachment(index)` and `GetColorAttachmentCount()`.

## Resizing

Framebuffers can be resized; attachments are recreated:

```cpp
framebuffer->Resize(2560, 1440);
```

Current behavior:

- `Resize()` recreates the attachments and invalidates the underlying framebuffer
- when the renderer is initialized, `Resize()` marshals work to the **primary render context** internally and blocks until completion
- before full renderer initialization, it falls back to a direct single-thread path
- sizes are clamped against `GL_MAX_RENDERBUFFER_SIZE` in the current OpenGL backend

In normal engine/editor code, you usually call `framebuffer->Resize(...)` directly rather than manually submitting a separate render-thread task around it.

## Thread Safety

- **Create** / **CreateAsync**:
  - safe to call from normal engine threads
  - creation runs on the primary render context
- **Bind** / **Unbind**:
  - raw calls are render-context operations and should only execute on the render thread / primary context
  - in higher-level code, prefer issuing bind/unbind through render commands or render-pass flow
- **GetColorAttachment** / `GetColorAttachment(index)` / **GetWidth** / **GetHeight**:
  - read-only accessors
  - do not race these calls against concurrent `Resize()` or destruction
- **Resize**:
  - callable from normal engine code
  - internally marshals to the primary render context in the common renderer-initialized path

## Example: Editor Viewport

```cpp
// In editor layer initialization
m_ViewportFramebuffer = Framebuffer::Create({
    .Width = 1920,
    .Height = 1080,
    .Samples = 1,
    .DepthAttachment = true,
});

// Each frame: render scene to framebuffer
renderer.SubmitCommand(std::make_unique<BindFramebufferCommand>(m_ViewportFramebuffer));
renderer.SubmitCommand(std::make_unique<SetViewportCommand>(0, 0, 
    m_ViewportFramebuffer->GetWidth(), m_ViewportFramebuffer->GetHeight()));
// ... clear, draw scene ...
renderer.SubmitCommand(std::make_unique<BindFramebufferCommand>(nullptr));

// In ImGui panel
ImGui::Begin("Viewport");
ImVec2 size = ImGui::GetContentRegionAvail();
ImGui::Image(
    reinterpret_cast<ImTextureID>(m_ViewportFramebuffer->GetColorAttachment()->GetRendererID()),
    size, ImVec2(0, 1), ImVec2(1, 0));
ImGui::End();
```

## See Also

- [Renderer2D Guide](RENDERER2D_GUIDE.md) for draw APIs
- [README_RenderCommandSystem.md](README_RenderCommandSystem.md) for command submission
- [RENDERING_ROADMAP.md](RENDERING_ROADMAP.md) for implementation status
