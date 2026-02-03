# Render Command System

The Render Command System is a high-performance, thread-safe rendering architecture that uses lock-free queues for efficient command submission and execution. It provides a flexible and extensible framework for managing graphics rendering operations.

## Features

- **Lock-Free Queue**: Uses the existing lock-free queue system for thread-safe command submission
- **Priority-Based Execution**: Commands can be submitted with different priority levels
- **Command Batching**: Efficient batching of similar commands for better performance
- **Multi-Threaded Rendering**: Support for multi-threaded command execution
- **Error Handling**: Comprehensive error handling using the existing error system
- **Performance Monitoring**: Built-in statistics and performance metrics
- **Debug Support**: Debug markers and groups for profiling
- **Extensible**: Easy to add new command types

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
commands.push_back(std::make_unique<DrawArraysCommand>(GL_TRIANGLES, 0, 3));

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

- `maxQueueSize`: Maximum number of commands in the queue (must be power of 2)
- `maxCommandsPerFrame`: Maximum commands to process per frame
- `maxExecutionTimePerFrame`: Maximum execution time per frame (microseconds)
- `enableBatching`: Enable command batching
- `enablePrioritySorting`: Enable priority-based sorting
- `enableStatistics`: Enable performance statistics
- `enableDebugMarkers`: Enable debug markers
- `workerThreadCount`: Number of worker threads for multi-threaded execution

## Thread Safety

The render command system is fully thread-safe:

- **Multiple producers**: Multiple threads can submit commands simultaneously
- **Single consumer**: Commands are processed by a single thread (or multiple worker threads)
- **Lock-free operations**: Uses lock-free queues for maximum performance
- **Atomic statistics**: Performance statistics are updated atomically

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

See `RenderCommandExample.cpp` for comprehensive examples of how to use the render command system.

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