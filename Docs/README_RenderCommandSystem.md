# Render Command System

The Render Command System is a **command submission + execution framework** intended to make rendering work explicit and schedulable. Today it is best understood as **infrastructure/scaffolding**: the queueing and prioritization exist, and the OpenGL backend now implements a small but useful subset of commands (enough to bind shaders/VAOs/buffers/textures and draw basic geometry). Many state-setting and advanced draw commands are still stubbed.

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

#### Avoiding stalls (large uploads)

`Renderer::SubmitResourceAndWait()` is correct but **blocking**: the calling thread will wait until the render thread executes the work.

For large uploads (big textures, mesh streaming), prefer:

- `Renderer::SubmitResource(...)` (fire-and-forget)
- `Renderer::SubmitResourceAsync(...)` (returns a `std::future`, non-blocking)

Then poll or wait at a safe point (loading screens, frame boundaries, etc.).

### Current limitations (important)

- Some commands (especially state-setting, framebuffer, instanced drawing, and debug marker commands) are still **placeholders**: they log intent instead of issuing real graphics API calls.
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
  - `BindVertexArrayCommand` (`glBindVertexArray` via `VertexArray::Bind()`)
  - `BindVertexBufferCommand` (`glBindBuffer(GL_ARRAY_BUFFER)` via `VertexBuffer::Bind()`)
  - `BindIndexBufferCommand` (`glBindBuffer(GL_ELEMENT_ARRAY_BUFFER)` via `IndexBuffer::Bind()`)
  - `BindTextureCommand` (`glActiveTexture`, `glBindTexture` via `Texture::Bind(slot)`)
  - `DrawArraysCommand` (`glDrawArrays`)
  - `DrawIndexedCommand` (`glDrawElements` / `glDrawElementsBaseVertex`)
  - `CustomCommand` (invokes user function; still requires non-null context)
- **Stubbed (logs intent, no actual GL state change yet)**:
  - `BindFramebufferCommand`
  - `SetBlendModeCommand`, `SetDepthTestCommand`, `SetCullFaceCommand`, `SetPolygonModeCommand`, `SetLineWidthCommand`, `SetPointSizeCommand`
  - `PushDebugGroupCommand`, `PopDebugGroupCommand`, `InsertDebugMarkerCommand`
- **Not implemented yet (TODO / no behavior)**:
  - `DrawInstancedCommand`, `DrawIndexedInstancedCommand`

For a milestone-by-milestone plan, see `Docs/RENDERING_ROADMAP.md`.

## Architecture

### Core Components

1. **RenderCommand**: Base interface for all render commands
2. **RenderCommandQueue**: Thread-safe queue using lock-free implementation
3. **RenderCommandExecutor**: Multi-threaded command executor
4. **RenderCommandBatch**: Efficient command batching utility

### Command Types

The system includes a comprehensive set of pre-defined render commands:

- **ClearCommand**: Clear color, depth, and stencil buffers
- **SetViewportCommand**: Set viewport dimensions
- **SetScissorCommand**: Set scissor test region
- **BindShaderCommand**: Bind/unbind shaders
- **BindVertexArrayCommand**: Bind/unbind vertex arrays
- **BindIndexBufferCommand**: Bind/unbind index buffers
- **BindVertexBufferCommand**: Bind/unbind vertex buffers
- **BindTextureCommand**: Bind/unbind textures
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
    auto drawCommand = std::make_unique<DrawArraysCommand>(GL_TRIANGLES, i * 3, 3);
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

### Multi-Threaded Rendering

```cpp
// Create a multi-threaded executor
RenderCommandExecutor executor(graphicsContext, 2); // 2 worker threads

// Start the executor
executor.Start();

// Submit commands
std::vector<std::unique_ptr<RenderCommand>> commands;
// ... add commands ...

executor.SubmitCommands(std::move(commands));

// Wait for completion
executor.WaitForCompletion();

// Stop the executor
executor.Stop();
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
commands.push_back(std::make_unique<DrawArraysCommand>(GL_TRIANGLES, 0, 3));
commands.push_back(std::make_unique<PopDebugGroupCommand>());
```

## Configuration Options

### RenderQueueConfig

- `maxQueueSize`: Maximum number of commands allowed in the queue (**must be power of 2**). Note: the underlying ring buffer is currently a **fixed-capacity** queue (see Contracts below).
- `maxCommandsPerFrame`: Maximum commands to process per frame
- `maxExecutionTimePerFrame`: Maximum execution time per frame (microseconds)
- `enableBatching`: Enable command batching
- `enablePrioritySorting`: Enable priority-based sorting
- `enableStatistics`: Enable performance statistics
- `enableDebugMarkers`: Enable debug markers
- `workerThreadCount`: Number of worker threads for multi-threaded execution

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

`RenderCommandExecutor` is experimental scaffolding. It is **not safe** to execute OpenGL commands from multiple threads against a single OpenGL context without explicit context sharing/ownership rules. Treat multi-threaded GPU execution as **not implemented** for OpenGL as of right now.

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