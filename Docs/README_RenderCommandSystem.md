# Render Command System

The Render Command System is a **command submission + execution framework** intended to make rendering work explicit and schedulable. Today it is best understood as **infrastructure/scaffolding**: the queueing and prioritization exist, and the OpenGL backend now implements a small but useful subset of commands (enough to bind shaders/VAOs/buffers/textures and draw basic geometry). Several common state commands (blend/depth/cull/polygon/line/point) are now implemented in the OpenGL backend, but framebuffer and debug-marker commands are still stubbed.

## Features

- **Thread-safe submission**: Multiple threads can submit commands safely.
- **Priority-based ordering**: Commands can be submitted with different priority levels.
- **Basic batching pass**: A conservative reorder pass exists (clear commands are moved to the front; other commands keep their relative order).
- **Error handling hooks**: Command execution catches `Limitless::Error` and logs/report errors.
- **Statistics**: Basic execution counters and timing stats exist.
- **Extensible**: It’s straightforward to add new command types.

### GPU resource operations (RenderResourceCommandQueue)

GPU resource operations (shader compile/link, buffer/texture creation, VAO attribute setup, and OpenGL deletes) are now routed through a dedicated queue that executes on the render thread:

- **Type**: `Limitless::RenderResourceCommandQueue`
- **Execution**: drained by the render thread before processing frame render commands
- **Why**: OpenGL context affinity + avoiding “context thrash” across threads

#### OpenGL shared-context resource thread (advanced, optional)

The engine can optionally start a dedicated **OpenGL resource thread** that owns a **shared OpenGL context** and drains a resource queue in parallel with frame rendering.

Important correctness rule (OpenGL):

- **Shareable across contexts**: buffers, textures, shader/program objects
- **Not shareable across contexts**: **VAOs** (and generally FBO-related state)

Because VAOs are not shared, the engine maintains **two resource queues**:

- **Primary resource queue**: executed only on the render thread with the primary context current (for VAOs, etc.)
- **Shared resource queue**: may be executed on the resource thread (buffers/textures/programs)

This avoids “black screen / VAO=0” failures when enabling the shared context path.

#### Avoiding stalls (large uploads)

`Renderer::SubmitResourceAndWait()` is correct but **blocking**: the calling thread will wait until the render thread executes the work.

For large uploads (big textures, mesh streaming), prefer:

- `Renderer::SubmitResource(...)` (fire-and-forget)
- `Renderer::SubmitResourceAsync(...)` (returns a `std::future`, non-blocking)

Then poll or wait at a safe point (loading screens, frame boundaries, etc.).

#### Uniform updates (current vs future)

`SetShaderMat4Command` exists today as a **transitional** mechanism for demos and early engine bring-up.

Long-term, the engine should move toward a **Material/Pipeline** model where per-frame/per-draw parameters are owned by a pipeline state object (or material instance), rather than calling `Shader::Set*` during render command execution.

Example (async texture creation):

```cpp
Limitless::TextureSpecification spec{};
spec.GenerateMipmaps = true;

auto textureFuture = Limitless::Texture2D::CreateFromFileAsync("Assets/Textures/Albedo.png", spec);

// Later (e.g. in Update):
if (textureFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
{
    std::shared_ptr<Limitless::Texture2D> texture = textureFuture.get();
    // safe to submit draw that binds this texture
}
```

### Current limitations (important)

- Some commands (especially framebuffer, instanced drawing, and debug marker commands) are still **placeholders**: they log intent instead of issuing real graphics API calls.
- The “multi-threaded executor” type exists but is currently **disabled** for the OpenGL-first backend. True multi-threaded GPU execution requires an explicit context ownership/sharing model that is not implemented yet.

## Guarantees (Current Behavior)

This table describes what the system **guarantees today** (not future intent).

| Area | Guarantee | Notes |
|------|-----------|-------|
| Submission threading | `SubmitCommand*()` is safe for **multiple producer threads** | Uses a bounded lock-free MPMC ring + an atomic size gate for `maxQueueSize`. |
| Execution threading | With the render thread enabled, **execution + present happen on the render thread** | The main thread typically only submits commands. `Renderer::EndFrame()` signals the render thread and `Renderer::SwapBuffers()` waits for completion. OpenGL context ownership is still serialized and correct. |
| Resource threading | GPU resource operations are executed on the render thread | `Renderer::SubmitResourceAndWait()` schedules work on `RenderResourceCommandQueue` and blocks the caller until complete. |
| Ownership | Commands are transferred via `std::unique_ptr` into the queue | If submission fails, the command is destroyed on the submitting thread. |
| Queue bounds | `maxQueueSize` is enforced | Underlying fixed capacity is `RenderCommandQueue::kQueueCapacity` (currently 16384). `maxQueueSize` must be <= that. |
| Error behavior | Command execution catches `Limitless::Error` and `std::exception` | Errors are logged and optionally forwarded via the debug callback. |
| Statistics | Stats are **thread-safe** | Stats are protected by an internal mutex (not “atomic struct writes”). |

## Implementation Status (OpenGL backend)

The OpenGL backend currently implements only a subset of commands “for real”. The rest are scaffolding and generally only log intent.

- **Implemented (real OpenGL calls)**:
  - `ClearCommand` (`glClearColor`, `glClear`)
  - `SetViewportCommand` (`glViewport`)
  - `SetScissorCommand` (`glEnable/glDisable(GL_SCISSOR_TEST)`, `glScissor`)
  - `BindShaderCommand` (`glUseProgram` via `Shader::Bind()`)
  - `SetShaderMat4Command` (uniform update via `Shader::SetMat4()`; transitional)
  - `BindVertexArrayCommand` (`glBindVertexArray` via `VertexArray::Bind()`)
  - `BindVertexBufferCommand` (`glBindBuffer(GL_ARRAY_BUFFER)` via `VertexBuffer::Bind()`)
  - `BindIndexBufferCommand` (`glBindBuffer(GL_ELEMENT_ARRAY_BUFFER)` via `IndexBuffer::Bind()`)
  - `SetVertexBufferDataCommand` (`glBufferSubData` via `VertexBuffer::SetData()`; dynamic streaming uploads)
  - `BindTextureCommand` (`glActiveTexture`, `glBindTexture` via `Texture::Bind(slot)`)
  - `SetTextureSpecificationCommand` (sampler-like state via `Texture::ApplySpecification()`)
  - `DrawArraysCommand` (`glDrawArrays`)
  - `DrawIndexedCommand` (`glDrawElements` / `glDrawElementsBaseVertex`)
  - `SetBlendModeCommand` (`glEnable/Disable(GL_BLEND)`, `glBlendFunc`)
  - `SetDepthTestCommand` (`glEnable/Disable(GL_DEPTH_TEST)`, `glDepthFunc`)
  - `SetCullFaceCommand` (`glEnable/Disable(GL_CULL_FACE)`, `glCullFace`)
  - `SetPolygonModeCommand` (`glPolygonMode`)
  - `SetLineWidthCommand` (`glLineWidth`)
  - `SetPointSizeCommand` (`glPointSize`)
  - `CustomCommand` (invokes user function; still requires non-null context)
- **Stubbed (logs intent, no actual GL state change yet)**:
  - `BindFramebufferCommand`
  - `PushDebugGroupCommand`, `PopDebugGroupCommand`, `InsertDebugMarkerCommand`
- **Not implemented yet (TODO / no behavior)**:
  - `DrawInstancedCommand`, `DrawIndexedInstancedCommand`

For a milestone-by-milestone plan, see `Docs/RENDERING_ROADMAP.md`.

## Viewport updates (window resize)

For OpenGL correctness, the viewport must match the window's **drawable pixel size**. The SDL window layer keeps this in sync:

- `SDL_EVENT_WINDOW_RESIZED` / `SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED`
  - queries `SDL_GetWindowSizeInPixels`
  - submits `SetViewportCommand(0,0,width,height)`
  - dispatches `WindowResizeEvent(width,height)` so cameras/UI can react

## Architecture

### Core Components

1. **RenderCommand**: Base interface for all render commands
2. **RenderCommandQueue**: Thread-safe queue using lock-free implementation
3. **RenderCommandExecutor**: Multi-threaded command executor (**currently disabled for OpenGL**; future-facing scaffolding only)
4. **RenderCommandBatch**: Efficient command batching utility

### Command Types

The system includes a comprehensive set of pre-defined render commands:

- **ClearCommand**: Clear color, depth, and stencil buffers
- **SetViewportCommand**: Set viewport dimensions
- **SetScissorCommand**: Set scissor test region
- **BindShaderCommand**: Bind/unbind shaders
- **SetShaderMat4Command**: Set a `mat4` uniform on a bound shader (transitional)
- **BindVertexArrayCommand**: Bind/unbind vertex arrays
- **BindIndexBufferCommand**: Bind/unbind index buffers
- **BindVertexBufferCommand**: Bind/unbind vertex buffers
- **SetVertexBufferDataCommand**: Upload bytes into a vertex buffer (dynamic streaming)
- **BindTextureCommand**: Bind/unbind textures
- **SetTextureSpecificationCommand**: Apply sampler-like filtering/wrap/mip settings to a texture
- **BindFramebufferCommand**: Bind/unbind framebuffers
- **DrawArraysCommand**: Draw arrays of vertices
- **DrawIndexedCommand**: Draw indexed vertices
- **DrawInstancedCommand**: Draw instanced arrays
- **DrawIndexedInstancedCommand**: Draw indexed instanced arrays
- **SetBlendModeCommand**: Configure blending
- **SetDepthTestCommand**: Configure depth testing
- **SetCullFaceCommand**: Configure face culling
- **SetPolygonModeCommand**: Set polygon rendering mode
- **SetLineWidthCommand**: Set line width
- **SetPointSizeCommand**: Set point size
- **PushDebugGroupCommand**: Push debug group
- **PopDebugGroupCommand**: Pop debug group
- **InsertDebugMarkerCommand**: Insert debug marker
- **CustomCommand**: User-defined custom commands

## Usage

### Basic Setup

In the engine runtime, you typically interact with the render queue through `Renderer` (which owns the queue and coordinates the render thread).
Constructing a `RenderCommandQueue` directly is a lower-level path that is still useful for tests, prototypes, or standalone tooling.

```cpp
#include "Graphics/RenderCommandQueue.h"

// Create a render command queue with custom configuration
RenderQueueConfig config;
config.maxQueueSize = 8192; // Must be power of 2
config.maxCommandsPerFrame = 5000;
config.enableBatching = true;
config.enablePrioritySorting = true;
config.enableStatistics = true;

auto renderQueue = std::make_unique<RenderCommandQueue>(config);
```

### Submitting Commands

```cpp
// Submit a single command
auto clearCommand = std::make_unique<ClearCommand>(
    ClearCommand::ClearFlags{true, true, false}, // Clear color and depth
    0.2f, 0.3f, 0.8f, 1.0f // Blue background
);

if (renderQueue->SubmitCommand(std::move(clearCommand)))
{
    // Command submitted successfully
}

// Submit multiple commands at once
std::vector<std::unique_ptr<RenderCommand>> commands;
commands.push_back(std::make_unique<SetViewportCommand>(0, 0, 1920, 1080));
commands.push_back(std::make_unique<DrawArraysCommand>(Limitless::DrawMode::Triangles, 0, 3));

renderQueue->SubmitCommands(std::move(commands));
```

### Priority-Based Submission

```cpp
// Submit commands with different priorities
auto uiCommand = std::make_unique<CustomCommand>(
    [](GraphicsContext* context) {
        // Render UI elements
    },
    "RenderUI"
);

renderQueue->SubmitCommandWithPriority(std::move(uiCommand), RenderCommandPriority::High);

auto backgroundCommand = std::make_unique<CustomCommand>(
    [](GraphicsContext* context) {
        // Render background effects
    },
    "RenderBackground"
);

renderQueue->SubmitCommandWithPriority(std::move(backgroundCommand), RenderCommandPriority::Low);
```

### Command Batching

```cpp
// Use command batching for efficiency
RenderCommandBatch batch(*renderQueue);

// Add multiple similar commands to the batch
for (int i = 0; i < 10; ++i)
{
    auto drawCommand = std::make_unique<DrawArraysCommand>(Limitless::DrawMode::Triangles, i * 3, 3);
    batch.AddCommand(std::move(drawCommand));
}

// Submit the entire batch at once
batch.Submit();
```

### Processing Commands

```cpp
// Process all commands in the queue
renderQueue->ProcessCommands(graphicsContext);

// Process commands in batches
renderQueue->ProcessCommandsBatch(graphicsContext, 100);

// Process commands with a time limit
renderQueue->ProcessCommandsWithTimeLimit(graphicsContext, 16000); // 16ms limit
```

### Dedicated render thread (implemented)

Limitless supports **multi-threaded submission**: multiple producer threads may submit commands concurrently.
Execution is **single-threaded** against the graphics context (OpenGL context affinity), and can run either:

- on the **main thread** (render thread disabled), or
- on a dedicated **render thread** (render thread enabled) that owns “process commands + present” for each frame.

The render thread is coordinated by `Renderer` (see `Limitless/Source/Graphics/Renderer.cpp`), not by `RenderCommandExecutor`.

```cpp
// Typical engine usage: submit from any thread.
// Execution + SwapBuffers happen on the render thread when enabled.
// (Frame boundaries are typically owned by Application: BeginFrame → EndFrame → SwapBuffers.)
#include "Graphics/Renderer.h"
#include "Graphics/RenderCommand.h"

auto& renderer = Limitless::Renderer::GetInstance();

renderer.SubmitCommand(std::make_unique<Limitless::ClearCommand>(
    Limitless::ClearCommand::ClearFlags{true, true, false},
    0.1f, 0.1f, 0.12f, 1.0f));
```

### Performance Monitoring

```cpp
// Enable statistics
RenderQueueConfig config = renderQueue->GetConfig();
config.enableStatistics = true;
renderQueue->SetConfig(config);

// Begin and end frames for timing
renderQueue->BeginFrame();
renderQueue->ProcessCommands(graphicsContext);
renderQueue->EndFrame();

// Get performance statistics
auto stats = renderQueue->GetStats();
std::cout << "Commands executed: " << stats.totalCommandsExecuted << std::endl;
std::cout << "Average frame time: " << stats.averageFrameTime / 1000.0 << "ms" << std::endl;
```

### Error Handling

The system integrates with the existing error handling system:

```cpp
// Commands automatically handle errors
auto errorCommand = std::make_unique<CustomCommand>(
    [](GraphicsContext* context) {
        if (context == nullptr)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context is null", std::source_location::current());
        }
    },
    "ErrorTest"
);

renderQueue->SubmitCommand(std::move(errorCommand));
// Errors are automatically caught and logged by the queue
```

### Debug Support

```cpp
// Enable debug markers
renderQueue->EnableDebugMarkers(true);

// Set debug callback
renderQueue->SetDebugCallback([](const std::string& message) {
    std::cout << "Render Debug: " << message << std::endl;
});

// Use debug groups in commands
std::vector<std::unique_ptr<RenderCommand>> commands;
commands.push_back(std::make_unique<PushDebugGroupCommand>("DrawTriangle"));
commands.push_back(std::make_unique<DrawArraysCommand>(Limitless::DrawMode::Triangles, 0, 3));
commands.push_back(std::make_unique<PopDebugGroupCommand>());
```

Note: debug-marker commands (`PushDebugGroupCommand`, `PopDebugGroupCommand`, `InsertDebugMarkerCommand`) are currently **stubbed** in the OpenGL backend and only log intent (see `Docs/RENDERING_ROADMAP.md`).

## Configuration Options

### RenderQueueConfig

- `maxQueueSize`: Maximum number of commands allowed in the queue (**must be power of 2**). Note: the underlying ring buffer is currently a **fixed-capacity** queue (see Contracts below).
- `maxCommandsPerFrame`: Maximum commands to process per frame
- `maxExecutionTimePerFrame`: Maximum execution time per frame (microseconds)
- `enableBatching`: Enable command batching
- `enablePrioritySorting`: Enable priority-based sorting
- `enableStatistics`: Enable performance statistics
- `enableDebugMarkers`: Enable debug markers
- `workerThreadCount`: Number of worker threads for multi-threaded execution (**reserved**; `RenderCommandExecutor` is disabled for OpenGL today)

## Thread Safety

The render command system is thread-safe for *submission*, with strict rules for execution:

### Contracts (Hard invariants)

- **Multiple producers**: `SubmitCommand*()` may be called concurrently from multiple threads (MPMC submit).
- **Single consumer execution**: `ProcessCommands*()` must be called from a **single thread that owns the `GraphicsContext`** (OpenGL contexts are thread-affine).
- **Bounded queue**:
  - The underlying ring buffer has a fixed capacity (`RenderCommandQueue::kQueueCapacity`, currently **16384**).
  - `maxQueueSize` can be set to a value **<=** that capacity to enforce a tighter bound; submissions beyond it return `false` and the commands are destroyed on the submitting thread.
- **Ownership**:
  - Submitting transfers ownership of the command via `std::unique_ptr` into the queue.
  - If submission fails (queue full), the command is destroyed immediately on the submitting thread.
- **Lifetime rule (critical)**:
  - Any resources referenced by a queued command must remain valid until the command is executed on the consumer thread.

### Notes about `RenderCommandExecutor`

`RenderCommandExecutor` is experimental scaffolding and is currently **disabled** for the OpenGL-first backend (it will throw `PlatformNotSupported` if used).

Multi-thread **submission** is supported. Multi-thread **OpenGL execution** is not a goal until explicit context ownership / sharing rules are designed and enforced.

## Performance Considerations

1. **Queue Size**: Choose an appropriate queue size based on your workload
2. **Command Batching**: Use batching for similar commands to reduce overhead
3. **Priority Sorting**: Use priority sorting for important commands
4. **Time Limits**: Use time limits to prevent frame drops
5. **Statistics**: Disable statistics in release builds for better performance

## Integration with Existing Systems

The render command system integrates seamlessly with existing Limitless engine components:

- **Error System**: Uses the existing error handling system
- **Logging System**: Uses the existing logging system
- **Lock-Free Queues**: Uses the existing lock-free queue implementation
- **Graphics Context**: Works with the existing graphics context system

## Example Usage

The best practical examples right now are in:

- `Limitless/Source/Graphics/Renderer.cpp` (queue setup + frame boundaries)
- `Limitless/Source/Graphics/RenderCommandQueue.cpp` (submission, ordering, execution loop)

## Best Practices

1. **Command Design**: Keep commands simple and focused
2. **Batch Similar Commands**: Group similar commands together
3. **Use Priorities Wisely**: Don't overuse high priority
4. **Monitor Performance**: Use statistics to identify bottlenecks
5. **Handle Errors**: Always handle potential errors in custom commands
6. **Debug Support**: Use debug markers for profiling
7. **Thread Safety**: Ensure custom commands are thread-safe

## Future Enhancements

Potential future improvements:

- **Command Validation**: Validate commands before execution
- **Command Caching**: Cache frequently used command sequences
- **GPU Command Buffers**: Direct GPU command buffer generation
- **Command Compression**: Compress command data for memory efficiency
- **Predictive Batching**: Predict and batch commands automatically
- **Command Replay**: Record and replay command sequences
- **Cross-Platform Support**: Support for multiple graphics APIs 