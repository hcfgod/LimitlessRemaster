# Rendering Roadmap (OpenGL-first)

This document is a short, practical roadmap for turning the current render-command scaffolding into a “real renderer” with clear milestones. It is intentionally **OpenGL-first**, because that is the only backend with any real command execution today.

## Definitions

- **Implemented**: issues real OpenGL calls and produces observable GPU state changes.
- **Stubbed**: validates inputs and/or logs intent, but does not issue meaningful OpenGL work yet.
- **Not implemented**: placeholder with TODO/no behavior.

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
| `BindFramebufferCommand` | Implemented | `glBindFramebuffer`, `Framebuffer::Bind()` |
| `DrawArraysCommand` | Implemented | `glDrawArrays` |
| `DrawIndexedCommand` | Implemented | `glDrawElements` / `glDrawElementsBaseVertex` |
| `DrawInstancedCommand` | Not implemented | TODO |
| `DrawIndexedInstancedCommand` | Not implemented | TODO |
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
- Add a minimal “triangle example” in `Sandbox` (or a dedicated sample target) that exercises the command system end-to-end

Acceptance criteria:
- A triangle renders on Windows with OpenGL backend.
- Command submission uses the queue (not only immediate execution).

### Milestone 2 — “Textured quad”

Goal: draw a quad with a texture and basic blending.

Deliverables:
- Implement `BindTextureCommand` (DONE)
- Implement `SetBlendModeCommand` and `SetDepthTestCommand` (DONE — real GL state changes)
- Implement `DrawIndexedCommand` + `glDrawElements` (DONE)

Acceptance criteria:
- Textured quad renders with deterministic output (basic pixel test optional).

### Milestone 3 — “Batching that matters”

Goal: reduce per-frame overhead with a real batching strategy.

Deliverables:
- Define a “batch key” (pipeline/shader/material/VAO) and sort commands by it
- Add a small command buffer allocator/pool to reduce per-frame heap churn
- Add renderer stats that show batch count and draw-call count

Acceptance criteria:
- Demonstrable reduction in command count / state changes under a simple scene.

### Milestone 4 — “Renderer API surface”

Goal: move from raw command lists to a stable renderer-facing API.

Deliverables:
- `Renderer2D` or `Renderer` helpers that build command sequences safely
- Clear ownership rules for GPU resources referenced by queued commands
- Decide how multi-threaded *GPU execution* will be handled (OpenGL context ownership vs future Vulkan/Metal)

## Notes on multi-threading

Multi-thread **submission** is supported. Multi-thread **OpenGL execution** is not a goal until explicit context ownership / sharing rules are designed and enforced.

