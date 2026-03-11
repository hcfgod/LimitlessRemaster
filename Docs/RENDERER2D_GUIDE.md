# Renderer2D Guide

`Renderer2D` is the main game-code-friendly 2D submission API in Limitless. It stays intentionally small:

- `BeginScene(...)`
- `DrawQuad(...)`
- `DrawText(...)`
- `EndScene()`

It is still **OpenGL-first** today because the render-command execution path is currently OpenGL-backed.

## Public API

Header: `Limitless/Source/Graphics/Renderer2D.h`

Core calls:

- `Renderer2D::Initialize()` / `Renderer2D::Shutdown()`
- `Renderer2D::BeginScene(const Camera& camera)`
- `Renderer2D::BeginScene(const glm::mat4& viewProjection)`
- `Renderer2D::BeginScene(const glm::mat4& viewProjection, bool enableDepthTest)`
- `Renderer2D::DrawQuad(...)`
- `Renderer2D::DrawText(transform, text, font, fontSize, color)`
- `Renderer2D::EndScene()`

Notes:

- `Renderer2D` is instantiable; `Renderer2D::Default()` returns the shared default instance used by the scene renderer.
- Quad overloads support position/size or full-transform submission, with color-only, textured, or textured+UV variants.

Readiness and assets:

- `Renderer2D::IsShaderReady()` returns true once the default quad shader/material path has resolved successfully.
- `Renderer2D::GetDefaultShaderKey()` returns the default textured-quad shader key used by loading/progress systems.

Statistics:

- `Renderer2D::GetStatistics()` returns `{ DrawCalls, Batches, QuadCount }`
- `Renderer2D::ResetStatistics()`

`QuadCount` includes both regular quad submissions and text glyph quads.

## Batching model

`Renderer2D` keeps separate batch state for:

- textured/color quads
- MSDF text quads

Within each batch, submissions are grouped by texture slots:

- vertices are staged on the CPU
- textures are assigned to the current texture-slot table
- a batch flush happens when index capacity is reached or the texture-slot table is full
- flush uploads one packed vertex buffer region and submits one indexed draw for that batch

This still gives the common “few draw calls for many sprites/text glyphs” behavior, but it is not literally “one draw call per texture per scene” once capacity or texture-slot limits are hit.

The runtime texture-slot limit is clamped by the graphics context and capped in code at 32 slots.

## Renderer state defaults

`BeginScene()` configures the current scene view-projection and prepares the quad/text pipelines.

Current pipeline defaults:

- alpha blending enabled (`SrcAlpha`, `OneMinusSrcAlpha`)
- face culling disabled
- depth test/write follow the `enableDepthTest` argument

Important default behavior:

- `BeginScene(const Camera&)` enables depth testing
- `BeginScene(const glm::mat4& viewProjection)` also enables depth testing
- screen-space UI paths explicitly call `BeginScene(..., false)` when they want depth disabled

## Asset usage

The default quad and text paths are both asset-driven:

- textured quads:
  - `Assets/Materials/Renderer2D_TexturedQuad.material.json`
  - `Assets/Shaders/Renderer2D_TexturedQuad.glsl`
- MSDF text:
  - `Assets/Materials/Renderer2D_MSDFText.material.json`
  - `Assets/Shaders/Renderer2D_MSDFText.glsl`

If the material asset is missing, `Renderer2D` falls back to loading the shader asset directly. If the text shader path is unavailable, text rendering is disabled while quad rendering can still continue.

## Current scene integration

The main runtime/editor scene submission path uses `Renderer2D::Default()` from:

- `Limitless/Source/Scene/SceneRendererRuntime.cpp`

Current uses include:

- sprite submission
- MSDF text submission
- screen-space and world-space UI submission
- particle-emitter billboard submission

The scene renderer chooses the view-projection and whether depth testing is enabled before beginning each 2D pass.

