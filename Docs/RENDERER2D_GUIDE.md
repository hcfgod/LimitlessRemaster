# Renderer2D Guide (MVP)

`Renderer2D` is the first "game-code friendly" renderer API in Limitless. It is intentionally small and pragmatic:

- A stable scene API: `BeginScene(camera)`, `DrawQuad(...)`, `EndScene()`
- A real batching path (minimizing draw calls under common 2D workloads)
- Basic renderer statistics surfaced for profiling

This is **OpenGL-first** today because the engine's render-command execution is OpenGL-backed.

## Public API

Header: `Limitless/Source/Graphics/Renderer2D.h`

Core calls:

- `Renderer2D::Initialize()` / `Renderer2D::Shutdown()`
- `Renderer2D::BeginScene(const Camera& camera)` (also `BeginScene(viewProjection)`)
- `Renderer2D::DrawQuad(...)` (position+size, or transform; color or texture+tint)
- `Renderer2D::DrawText(transform, text, font, fontSize, color)` (MSDF text)
- `Renderer2D::EndScene()`

Readiness and assets:

- `Renderer2D::IsShaderReady()` — true when the default material is loaded; otherwise `DrawQuad` is dropped.
- `Renderer2D::GetDefaultShaderKey()` — asset key for the default shader (e.g. for `AssetLoadProgress`).

Statistics:

- `Renderer2D::GetStatistics()` returns `{ DrawCalls, Batches, QuadCount }`
- `Renderer2D::ResetStatistics()`

## Batching model (current MVP)

The MVP batches quads by **texture**:

- Quads using the same `Texture2D` are appended into a single CPU vertex staging buffer
- At flush time, `Renderer2D` uploads the vertex data once and issues **one** `DrawIndexedCommand`
- If the texture changes (or the batch reaches capacity), the current batch is flushed and a new one begins

This yields "one draw call per texture per scene" for typical 2D usage, without requiring instancing or per-draw uniforms.

## Renderer state defaults

`BeginScene()` submits common 2D defaults:

- Blending enabled: \( SrcAlpha \times OneMinusSrcAlpha \)
- Depth test disabled
- Face culling disabled

## Asset usage

The default `Renderer2D` material is an asset:

- `Assets/Materials/Renderer2D_TexturedQuad.material.json`
- `Assets/Shaders/Renderer2D_TexturedQuad.glsl`

This keeps the shader pipeline editor-friendly and consistent with the engine's existing asset workflow.

## Example usage (Runtime)

The runtime layer uses `Renderer2D` here:

- `Runtime/Source/Renderer2DDemo.cpp`

It draws a grid of textured quads and logs `Renderer2D` statistics once per second.

