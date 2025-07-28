# Limitless Engine - Logging and Error Handling Guide

## Overview

The Limitless engine provides a robust, modular, and cross-platform logging and error handling system built on top of spdlog. This system offers:

- **Multiple logging levels** (Trace, Debug, Info, Warn, Error, Critical)
- **File and console logging** with rotation and daily logs
- **Performance monitoring** with automatic timing
- **Memory usage tracking** across platforms
- **Comprehensive error handling** with custom exception types
- **Thread-safe logging** with multiple logger instances
- **Cross-platform compatibility** (Windows, macOS, Linux)

## Quick Start

### Basic Logging

```cpp
#include "Limitless.h"

// Simple logging with the default logger
LT_INFO("Application started");
LT_WARN("This is a warning message");
LT_ERROR("An error occurred: {}", errorMessage);

// Conditional logging
bool debugMode = true;
LT_DEBUG_IF(debugMode, "Debug info: {}", debugData);
```

### Performance Monitoring

```cpp
// Automatic timing with scope
{
    LT_PERF_SCOPE("Expensive Operation");
    // ... your code here ...
} // Automatically logs timing when scope ends

// Function timing
auto result = LT_PERF_FUNCTION("Data Processing", [&]() {
    return ProcessData(input);
});

// Manual timing
auto start = Limitless::PerformanceMonitor::Now();
// ... your code ...
auto end = Limitless::PerformanceMonitor::Now();
double ms = Limitless::PerformanceMonitor::DurationMs(start, end);
```

### Memory Tracking

```cpp
// Log current memory usage
LT_LOG_MEMORY("After loading textures");

// Get memory usage programmatically
size_t memoryUsage = Limitless::PerformanceMonitor::GetCurrentMemoryUsage();
```

## Detailed Usage

### Logger Class

The `Logger` class provides the core logging functionality:

```cpp
// Create a logger
auto logger = std::make_unique<Limitless::Logger>("MyLogger");

// Configure logging level
logger->SetLevel(Limitless::LogLevel::Debug);

// Configure pattern
logger->SetPattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v");

// Enable file logging
logger->EnableFileLogging("logs");

// Log messages
logger->Info("Application started");
logger->Error("Failed to load file: {}", filename);
logger->LogPerformance("Operation", 15.5); // 15.5ms
logger->LogMemoryUsage(1024 * 1024, "Texture loading"); // 1MB
```

### LogManager Singleton

The `LogManager` provides centralized control over multiple loggers:

```cpp
auto& logManager = Limitless::LogManager::GetInstance();

// Initialize with application name
logManager.Initialize("MyGame");

// Get or create loggers
auto graphicsLogger = logManager.GetLogger("Graphics");
auto audioLogger = logManager.GetLogger("Audio");
auto defaultLogger = logManager.GetDefaultLogger();

// Global configuration
logManager.SetGlobalLevel(Limitless::LogLevel::Info);
logManager.EnableGlobalFileLogging("logs");
logManager.EnablePerformanceLogging(true);
logManager.EnableMemoryLogging(true);

// Log system information
logManager.LogSystemInfo();

// Get statistics
auto stats = logManager.GetLoggerStats();
for (const auto& stat : stats) {
    std::cout << "Logger: " << stat.name << ", Level: " << (int)stat.level << std::endl;
}

// Cleanup
logManager.Shutdown();
```

### Error Handling

The error handling system provides structured error management:

```cpp
// Custom error types
try {
    if (!fileExists) {
        throw Limitless::SystemError("Configuration file not found");
    }
    
    if (!textureLoaded) {
        throw Limitless::ResourceError("Failed to load texture: {}", texturePath);
    }
    
    if (!shaderCompiled) {
        throw Limitless::GraphicsError("Shader compilation failed");
    }
} catch (const Limitless::Error& error) {
    LT_ERROR("Caught error: {}", error.ToString());
    // Error automatically logged through error handler
}

// Result class for error handling
auto result = Limitless::ErrorHandling::Try([]() {
    return LoadResource("texture.png");
});

if (result.IsSuccess()) {
    auto resource = result.GetValue();
    // Use resource
} else {
    LT_ERROR("Failed to load resource: {}", result.GetError().GetMessage());
}

// Assertions
LT_ASSERT(pointer != nullptr, "Pointer must not be null");
LT_ASSERT(index < arraySize, "Index out of bounds: {} >= {}", index, arraySize);
```

### Performance Monitor

Advanced performance monitoring capabilities:

```cpp
// Scoped timing
{
    auto scope = Limitless::PerformanceMonitor::CreateScope("Rendering", graphicsLogger);
    RenderFrame();
    // Automatically logs timing when scope is destroyed
}

// Function timing with return value
auto result = Limitless::PerformanceMonitor::TimeFunction("Data Processing", 
    [&]() { return ProcessData(input); }, logger);

// Manual timing
auto start = Limitless::PerformanceMonitor::Now();
// ... your code ...
auto end = Limitless::PerformanceMonitor::Now();
double ms = Limitless::PerformanceMonitor::DurationMs(start, end);
logger->LogPerformance("Manual Operation", ms);
```

## Configuration

### Log Levels

- **Trace**: Very detailed debugging information
- **Debug**: Development debugging information
- **Info**: General information messages
- **Warn**: Warning messages
- **Error**: Error messages
- **Critical**: Critical error messages
- **Off**: Disable logging

### Log Patterns

Customize log output format:

```cpp
// Default pattern
"[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v"

// Simple pattern
"[%H:%M:%S] [%l] %v"

// Detailed pattern with thread ID
"[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] [%n] %v"
```

Pattern placeholders:
- `%Y-%m-%d`: Date
- `%H:%M:%S`: Time
- `%e`: Milliseconds
- `%l`: Log level
- `%n`: Logger name
- `%t`: Thread ID
- `%v`: Message
- `%^` and `%$`: Color markers

### File Logging

```cpp
// Enable file logging with custom directory
logManager.EnableGlobalFileLogging("logs");

// This creates:
// - logs/limitless_daily.log (daily rotation)
// - logs/limitless_rotating.log (5MB files, keep 3)
```

## Integration with Application

The logging system is automatically integrated into the `Application` class:

```cpp
class MyApp : public Limitless::Application
{
public:
    bool Initialize() override
    {
        // Logging is already initialized
        LT_INFO("MyApp initializing...");
        
        // Access loggers
        auto logger = GetLogger();
        auto& logManager = GetLogManager();
        
        // Your initialization code...
        return true;
    }
    
    void Shutdown() override
    {
        LT_INFO("MyApp shutting down...");
        // Your cleanup code...
    }
};
```

## Best Practices

### 1. Use Appropriate Log Levels

```cpp
// Use TRACE for very detailed debugging
LT_TRACE("Entering function with params: x={}, y={}", x, y);

// Use DEBUG for development debugging
LT_DEBUG("Processing {} items", itemCount);

// Use INFO for general information
LT_INFO("Level {} loaded successfully", levelName);

// Use WARN for potential issues
LT_WARN("Texture {} not found, using fallback", textureName);

// Use ERROR for actual errors
LT_ERROR("Failed to connect to server: {}", errorMessage);

// Use CRITICAL for severe errors
LT_CRITICAL("Application cannot continue: {}", criticalError);
```

### 2. Performance Monitoring

```cpp
// Use scoped timing for automatic cleanup
{
    LT_PERF_SCOPE("Frame Rendering");
    RenderScene();
    RenderUI();
} // Automatically logs timing

// Use function timing for simple operations
auto result = LT_PERF_FUNCTION("Data Loading", [&]() {
    return LoadDataFromFile(filename);
});
```

### 3. Error Handling

```cpp
// Use Result class for operations that can fail
auto result = LoadResource("texture.png");
if (result.IsSuccess()) {
    UseResource(result.GetValue());
} else {
    LT_ERROR("Failed to load resource: {}", result.GetError().GetMessage());
}

// Use assertions for invariants
LT_ASSERT(pointer != nullptr, "Pointer must not be null");
LT_ASSERT(index < size, "Index {} out of bounds [0, {})", index, size);
```

### 4. Multiple Loggers

```cpp
// Create specialized loggers for different systems
auto graphicsLogger = GetLogManager().GetLogger("Graphics");
auto audioLogger = GetLogManager().GetLogger("Audio");
auto networkLogger = GetLogManager().GetLogger("Network");

graphicsLogger->Info("Rendering frame {}", frameNumber);
audioLogger->Info("Playing sound {}", soundName);
networkLogger->Info("Sending packet to {}", serverAddress);
```

### 5. Memory Tracking

```cpp
// Track memory usage at key points
LT_LOG_MEMORY("After loading level");
LT_LOG_MEMORY("After creating entities");
LT_LOG_MEMORY("Before rendering frame");
```

## Platform-Specific Features

The logging system automatically detects the platform and provides appropriate functionality:

- **Windows**: Uses Windows API for memory tracking
- **macOS**: Uses Mach API for memory tracking  
- **Linux**: Uses /proc filesystem for memory tracking

## Troubleshooting

### Common Issues

1. **Logs not appearing**: Check log level configuration
2. **File logging not working**: Ensure directory permissions
3. **Performance impact**: Use appropriate log levels in release builds
4. **Memory leaks**: Ensure proper cleanup in destructors

### Debug Configuration

For development, use:

```cpp
logManager.SetGlobalLevel(Limitless::LogLevel::Trace);
logManager.EnablePerformanceLogging(true);
logManager.EnableMemoryLogging(true);
```

For release builds, use:

```cpp
logManager.SetGlobalLevel(Limitless::LogLevel::Info);
logManager.EnablePerformanceLogging(false);
logManager.EnableMemoryLogging(false);
```

## Examples

See the `SandboxApp` implementation for a complete example of using the logging and error handling system in a real application.