# Rendering Roadmap (OpenGL-first)

This document is a short, practical roadmap for turning the current render-command scaffolding into a “real renderer” with clear milestones. It is intentionally **OpenGL-first**, because that is the only backend with any real command execution today.

## Definitions

- **Implemented**: issues real OpenGL calls and produces observable GPU state changes.
- **Stubbed**: validates inputs and/or logs intent, but does not issue meaningful OpenGL work yet.
- **Not implemented**: placeholder with TODO/no behavior.

**Summary**: All commands in the table below are **Implemented** (real OpenGL work). There are currently no stubbed or not-implemented commands in the OpenGL backend.

## Current Command Implementation Status (OpenGL)

Source of truth: `Limitless/Source/Graphics/OpenGL/OpenGLRenderCommand.cpp`

| Command | Status | Notes |
|--------|--------|------|
| `ClearCommand` | Implemented | `glClearColor`, `glClear` |
| `SetViewportCommand` | Implemented | `glViewport` |
| `SetScissorCommand` | Implemented | `glEnable/Disable(GL_SCISSOR_TEST)`, `glScissor` |
| `CustomCommand` | Implemented | Calls user function; still requires a non-null context |
| `BindShaderCommand` | Implemented | `Shader::Bind()` (OpenGL `glUseProgram`) |
| `SetShaderMat4Command` | Implemented | Uniform update via `Shader::SetMat4()` (transitional) |
| `BindVertexArrayCommand` | Implemented | `VertexArray::Bind()` (OpenGL VAO bind) |
| `BindIndexBufferCommand` | Implemented | `IndexBuffer::Bind()` |
| `BindVertexBufferCommand` | Implemented | `VertexBuffer::Bind()` |
| `SetVertexBufferDataCommand` | Implemented | Dynamic streaming uploads via `VertexBuffer::SetData()` |
| `BindTextureCommand` | Implemented | `Texture::Bind(slot)` (`glActiveTexture` + `glBindTexture`) |
| `SetTextureSpecificationCommand` | Implemented | Sampler-like state via `Texture::ApplySpecification()` |
| `BindRenderPipelineCommand` | Implemented | Binds the current render pipeline / fixed-function fallback state |
| `BindFramebufferCommand` | Implemented | `glBindFramebuffer`, `Framebuffer::Bind()` |
| `BeginRenderPassCommand` | Implemented | Binds target framebuffer, viewport/scissor, and clear/load behavior |
| `EndRenderPassCommand` | Implemented | Ends the pass and restores pass-local state as needed |
| `ApplyRenderBindingsCommand` | Implemented | Applies bound textures/samplers/uniform-style parameter sets |
| `DrawArraysCommand` | Implemented | `glDrawArrays` |
| `DrawIndexedCommand` | Implemented | `glDrawElements` / `glDrawElementsBaseVertex` |
| `DrawInstancedCommand` | Implemented | `glDrawArraysInstanced` |
| `DrawIndexedInstancedCommand` | Implemented | `glDrawElementsInstanced` / `glDrawElementsInstancedBaseVertex` |
| `SetBlendModeCommand` | Implemented | `glEnable/Disable(GL_BLEND)`, `glBlendFunc` |
| `SetDepthTestCommand` | Implemented | `glEnable/Disable(GL_DEPTH_TEST)`, `glDepthFunc` |
| `SetCullFaceCommand` | Implemented | `glEnable/Disable(GL_CULL_FACE)`, `glCullFace` |
| `SetPolygonModeCommand` | Implemented | `glPolygonMode` |
| `SetLineWidthCommand` | Implemented | `glLineWidth` |
| `SetPointSizeCommand` | Implemented | `glPointSize` |
| `PushDebugGroupCommand` | Implemented | `glPushDebugGroup` / `glPushDebugGroupKHR` when available |
| `PopDebugGroupCommand` | Implemented | `glPopDebugGroup` / `glPopDebugGroupKHR` when available |
| `InsertDebugMarkerCommand` | Implemented | `glInsertDebugMarker` / `glInsertDebugMarkerKHR` when available |

## Milestones (Product-Focused)

### Milestone 0 — “Clear screen and survive” (DONE)

- Clear color/depth/stencil works.
- Viewport/scissor work.
- RenderCommandQueue supports multi-thread submission, single-thread execution, bounded overflow behavior.

### Milestone 1 — “Triangle on screen” (DONE)

Goal: submit a command sequence and reliably draw a triangle.

Deliverables:
- Implement `BindShaderCommand` (real shader bind/unbind)
- Implement `BindVertexArrayCommand` / `BindVertexBufferCommand` / `BindIndexBufferCommand` as needed
- Implement `DrawArraysCommand` with a real `glDrawArrays`
- Add a minimal “triangle example” in `Runtime` (or a dedicated sample target) that exercises the command system end-to-end

Acceptance criteria:
- A triangle renders on Windows with OpenGL backend.
- Command submission uses the queue (not only immediate execution).

### Milestone 2 — “Textured quad” (DONE)

Goal: draw a quad with a texture and basic blending.

Deliverables:
- Implement `BindTextureCommand` (DONE)
- Implement `SetBlendModeCommand` and `SetDepthTestCommand` (DONE — real GL state changes)
- Implement `DrawIndexedCommand` + `glDrawElements` (DONE)

Acceptance criteria:
- Textured quad renders with deterministic output (basic pixel test optional).

### Milestone 3 — “Batching that matters” (PARTIALLY DONE)

Goal: reduce per-frame overhead with a real batching strategy.

Deliverables:
- Define a safe batching strategy that preserves command correctness
- ~~Add a small command buffer allocator/pool to reduce per-frame heap churn~~ **Done**: the renderer now uses frame-local upload allocation plus `FrameCommandArena` for per-frame command storage
- ~~Add renderer stats that show batch count and draw-call count~~ **Partially done**: `Renderer2D` exposes batch/draw-call stats and `RenderCommandQueue` tracks queue statistics

Current status notes:

- Generic command-level reordering by “batch key” is **not** enabled today because render commands are stateful and order-dependent.
- The queue only does stable priority sorting, and its batching stage intentionally preserves original submission order.

Acceptance criteria:
- Demonstrable reduction in command count / state changes under a simple scene.

### Milestone 4 — “Renderer API surface” (PARTIALLY DONE)

Goal: move from raw command lists to a stable renderer-facing API.

Deliverables:
- ~~`Renderer2D` or `Renderer` helpers that build command sequences safely~~ **Done**: `Renderer2D` exists and is used by `SceneRenderer` and Runtime (BeginScene/DrawQuad/DrawText/EndScene, batching, stats). See `Docs/RENDERER2D_GUIDE.md`.
- Clear ownership rules for GPU resources referenced by queued commands
- ~~Decide how multi-threaded *GPU execution* will be handled~~ **Partially done**: current OpenGL execution uses an optional dedicated render thread plus an optional shared-context resource thread, with primary-context-only work kept on the render thread

## What’s next

- **Milestone 3 — Remaining**: Reduce per-frame overhead further without violating command ordering. Renderer2D already batches by texture; the remaining work is around higher-level submission patterns rather than unsafe global command reordering.
- **Milestone 4 — Remaining**: Ownership rules for queued commands and continued cleanup of the renderer-facing API surface.
- **Not yet on roadmap**: 2D lighting, 3D mesh pipeline.

## Notes on multi-threading

Multi-thread **submission** is supported.

Current OpenGL execution model:

- A dedicated render thread can own frame execution and presentation when `graphics.render_thread_enabled` is enabled.
- An optional shared-context resource thread can process shareable GPU resource work in parallel when shared-context support is available.
- Primary-context-only work (for example VAO/FBO-related work) is kept on the render thread via the primary resource queue.
- Submission helpers also have an inline fast path when the calling thread already owns the current graphics context, which avoids unnecessary cross-thread round-trips.

