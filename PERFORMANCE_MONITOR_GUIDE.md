# Performance Monitoring System Guide

This guide covers the comprehensive performance monitoring system added to the Limitless Engine, providing real-time metrics, frame timing, memory tracking, CPU/GPU monitoring, and performance counters.

## Table of Contents

1. [Overview](#overview)
2. [Core Components](#core-components)
3. [Basic Usage](#basic-usage)
4. [Advanced Features](#advanced-features)
5. [Integration Examples](#integration-examples)
6. [Platform Support](#platform-support)
7. [Best Practices](#best-practices)
8. [API Reference](#api-reference)

## Overview

The Performance Monitoring System provides comprehensive real-time performance tracking for the Limitless Engine, including:

- **Frame Timing**: Precise frame time measurements and FPS calculation
- **Memory Tracking**: Allocation tracking, peak memory usage, and memory leaks detection
- **CPU Monitoring**: Real-time CPU usage with platform-specific implementations
- **GPU Monitoring**: GPU usage, memory, and temperature (extensible)
- **Performance Counters**: Custom timing for specific code sections
- **Metrics Collection**: Comprehensive performance data aggregation
- **Logging and Reporting**: Built-in logging and file export capabilities

### Key Features

- **Thread-Safe**: All operations are thread-safe for multi-threaded applications
- **Low Overhead**: Minimal performance impact when disabled
- **Cross-Platform**: Works on Windows, Linux, and macOS
- **Extensible**: Easy to add custom metrics and monitoring
- **Real-Time**: Live performance data with configurable update intervals
- **Callback Support**: Custom callbacks for metrics processing

## Core Components

### PerformanceMonitor

The main singleton class that orchestrates all performance monitoring:

```cpp
// Get the singleton instance
auto& monitor = Limitless::PerformanceMonitor::GetInstance();

// Initialize the system
monitor.Initialize();

// Enable/disable monitoring
monitor.SetEnabled(true);
monitor.SetLoggingEnabled(true);
```

### PerformanceCounter

Individual performance counters for timing specific operations:

```cpp
// Create a counter
auto* counter = monitor.CreateCounter("Rendering");

// Use the counter
counter->Start();
// ... perform operation ...
counter->Stop();

// Get statistics
double lastValue = counter->GetLastValue();
double avgValue = counter->GetAverageValue();
double minValue = counter->GetMinValue();
double maxValue = counter->GetMaxValue();
```

### PerformanceTimer

High-resolution timer for precise measurements:

```cpp
Limitless::PerformanceTimer timer;
timer.Start();
// ... perform operation ...
timer.Stop();

double ms = timer.GetElapsedMilliseconds();
double us = timer.GetElapsedMicroseconds();
double ns = timer.GetElapsedNanoseconds();
```

### MemoryTracker

Tracks memory allocations and deallocations:

```cpp
auto* tracker = monitor.GetMemoryTracker();

// Track allocations (usually done automatically)
tracker->TrackAllocation(1024); // 1KB allocation
tracker->TrackDeallocation(1024); // 1KB deallocation

// Get memory statistics
uint64_t current = tracker->GetCurrentMemory();
uint64_t peak = tracker->GetPeakMemory();
uint32_t count = tracker->GetAllocationCount();
```

### CPUMonitor

Real-time CPU usage monitoring:

```cpp
// CPU monitoring is automatic, but you can configure it
monitor.m_cpuMonitor.SetUpdateInterval(0.5); // Update every 500ms

// Get CPU statistics
double usage = monitor.m_cpuMonitor.GetCurrentUsage();
double avgUsage = monitor.m_cpuMonitor.GetAverageUsage();
uint32_t cores = monitor.m_cpuMonitor.GetCoreCount();
```

## Basic Usage

### Initialization

```cpp
#include "Core/PerformanceMonitor.h"

int main() {
    // Initialize the performance monitor
    auto& monitor = Limitless::PerformanceMonitor::GetInstance();
    monitor.Initialize();
    
    // Enable logging for debugging
    monitor.SetLoggingEnabled(true);
    
    // Your application code here...
    
    // Shutdown when done
    monitor.Shutdown();
    return 0;
}
```

### Frame Timing

```cpp
// In your main game loop
while (running) {
    // Begin frame timing
    LT_PERF_BEGIN_FRAME();
    
    // Your frame processing code...
    UpdateGame();
    RenderFrame();
    
    // End frame timing
    LT_PERF_END_FRAME();
    
    // Get frame statistics
    double frameTime = monitor.GetFrameTime();
    double fps = monitor.GetFPS();
    double avgFps = monitor.GetAverageFPS();
}
```

### Performance Counters

```cpp
// Method 1: Using the convenience macro
{
    LT_PERF_COUNTER("UpdatePhysics");
    UpdatePhysics();
} // Counter automatically stops here

// Method 2: Manual counter usage
auto* counter = monitor.CreateCounter("Rendering");
counter->Start();
RenderScene();
counter->Stop();

// Method 3: Using PerformanceTimer for custom timing
{
    LT_PERF_SCOPE("CustomOperation");
    PerformCustomOperation();
} // Timer automatically stops here
```

### Memory Tracking

```cpp
// Memory tracking is automatic when using the engine's allocators
// For custom allocations, you can track manually:

void* ptr = malloc(1024);
LT_PERF_TRACK_MEMORY(1024);

// ... use memory ...

free(ptr);
LT_PERF_UNTrack_MEMORY(1024);
```

### Metrics Collection

```cpp
// Collect comprehensive metrics
auto metrics = monitor.CollectMetrics();

// Access individual metrics
double frameTime = metrics.frameTime;
double fps = metrics.fps;
uint64_t memory = metrics.currentMemory;
double cpuUsage = metrics.cpuUsage;

// Use metrics in your application
if (metrics.fps < 30.0) {
    // Performance warning
    LT_LOG_WARN("Low FPS detected: {}", metrics.fps);
}
```

## Advanced Features

### Custom Metrics Callbacks

```cpp
// Set up a callback for metrics processing
monitor.SetMetricsCallback([](const Limitless::PerformanceMetrics& metrics) {
    // Process metrics every collection interval
    if (metrics.fps < 30.0) {
        // Trigger performance optimization
        OptimizePerformance();
    }
    
    // Send metrics to external monitoring system
    SendToMonitoringService(metrics);
});

// Set collection interval (default is 1 second)
monitor.SetMetricsCollectionInterval(0.5); // Every 500ms
```

### Performance Profiling

```cpp
// Create multiple counters for detailed profiling
auto* updateCounter = monitor.CreateCounter("Update");
auto* renderCounter = monitor.CreateCounter("Render");
auto* physicsCounter = monitor.CreateCounter("Physics");

// Profile different systems
updateCounter->Start();
UpdateGameSystems();
updateCounter->Stop();

renderCounter->Start();
RenderFrame();
renderCounter->Stop();

physicsCounter->Start();
UpdatePhysics();
physicsCounter->Stop();

// Get detailed profiling data
auto metrics = monitor.CollectMetrics();
for (const auto& pair : metrics.counters) {
    LT_LOG_INFO("{}: {:.2f}ms (avg: {:.2f}ms)", 
                pair.first, 
                pair.second,
                monitor.GetCounter(pair.first)->GetAverageValue());
}
```

### Memory Leak Detection

```cpp
// Enable memory tracking
auto* tracker = monitor.GetMemoryTracker();

// At the start of a test
tracker->Reset();

// Run your test
RunMemoryTest();

// Check for memory leaks
uint64_t currentMemory = tracker->GetCurrentMemory();
if (currentMemory > 0) {
    LT_LOG_ERROR("Potential memory leak detected: {} bytes", currentMemory);
    LT_LOG_ERROR("Allocation count: {}", tracker->GetAllocationCount());
}
```

### Performance Reporting

```cpp
// Log current metrics
monitor.LogMetrics();

// Get formatted metrics string
std::string metricsStr = monitor.GetMetricsString();
std::cout << metricsStr << std::endl;

// Save detailed report to file
monitor.SaveMetricsToFile("performance_report.txt");
```

## Integration Examples

### Game Engine Integration

```cpp
class GameEngine {
private:
    Limitless::PerformanceMonitor& m_monitor;
    Limitless::PerformanceCounter* m_updateCounter;
    Limitless::PerformanceCounter* m_renderCounter;
    
public:
    GameEngine() : m_monitor(Limitless::PerformanceMonitor::GetInstance()) {
        // Initialize performance monitoring
        m_monitor.Initialize();
        m_monitor.SetLoggingEnabled(true);
        
        // Create performance counters
        m_updateCounter = m_monitor.CreateCounter("GameUpdate");
        m_renderCounter = m_monitor.CreateCounter("GameRender");
        
        // Set up metrics callback
        m_monitor.SetMetricsCallback([this](const Limitless::PerformanceMetrics& metrics) {
            OnPerformanceMetrics(metrics);
        });
    }
    
    void Run() {
        while (m_running) {
            LT_PERF_BEGIN_FRAME();
            
            // Update phase
            m_updateCounter->Start();
            Update();
            m_updateCounter->Stop();
            
            // Render phase
            m_renderCounter->Start();
            Render();
            m_renderCounter->Stop();
            
            LT_PERF_END_FRAME();
        }
    }
    
private:
    void OnPerformanceMetrics(const Limitless::PerformanceMetrics& metrics) {
        // Handle performance issues
        if (metrics.fps < 30.0) {
            // Reduce quality settings
            ReduceQualitySettings();
        }
        
        // Log performance data
        if (metrics.frameCount % 300 == 0) { // Every 300 frames
            m_monitor.LogMetrics();
        }
    }
};
```

### Rendering System Integration

```cpp
class Renderer {
private:
    Limitless::PerformanceCounter* m_geometryCounter;
    Limitless::PerformanceCounter* m_lightingCounter;
    Limitless::PerformanceCounter* m_postProcessCounter;
    
public:
    Renderer() {
        auto& monitor = Limitless::PerformanceMonitor::GetInstance();
        m_geometryCounter = monitor.CreateCounter("GeometryPass");
        m_lightingCounter = monitor.CreateCounter("LightingPass");
        m_postProcessCounter = monitor.CreateCounter("PostProcess");
    }
    
    void Render() {
        // Geometry pass
        m_geometryCounter->Start();
        RenderGeometry();
        m_geometryCounter->Stop();
        
        // Lighting pass
        m_lightingCounter->Start();
        RenderLighting();
        m_lightingCounter->Stop();
        
        // Post-processing
        m_postProcessCounter->Start();
        RenderPostProcess();
        m_postProcessCounter->Stop();
    }
};
```

### Physics System Integration

```cpp
class PhysicsSystem {
private:
    Limitless::PerformanceCounter* m_broadphaseCounter;
    Limitless::PerformanceCounter* m_narrowphaseCounter;
    Limitless::PerformanceCounter* m_solverCounter;
    
public:
    PhysicsSystem() {
        auto& monitor = Limitless::PerformanceMonitor::GetInstance();
        m_broadphaseCounter = monitor.CreateCounter("PhysicsBroadphase");
        m_narrowphaseCounter = monitor.CreateCounter("PhysicsNarrowphase");
        m_solverCounter = monitor.CreateCounter("PhysicsSolver");
    }
    
    void Update() {
        // Broad phase collision detection
        m_broadphaseCounter->Start();
        BroadPhaseCollision();
        m_broadphaseCounter->Stop();
        
        // Narrow phase collision detection
        m_narrowphaseCounter->Start();
        NarrowPhaseCollision();
        m_narrowphaseCounter->Stop();
        
        // Physics solver
        m_solverCounter->Start();
        SolveConstraints();
        m_solverCounter->Stop();
    }
};
```

### Memory Management Integration

```cpp
// Custom allocator with performance tracking
class PerformanceTrackedAllocator {
public:
    void* Allocate(size_t size) {
        void* ptr = malloc(size);
        if (ptr) {
            Limitless::PerformanceMonitor::GetInstance().TrackMemoryAllocation(size);
        }
        return ptr;
    }
    
    void Deallocate(void* ptr, size_t size) {
        if (ptr) {
            Limitless::PerformanceMonitor::GetInstance().TrackMemoryDeallocation(size);
            free(ptr);
        }
    }
};

// Usage with STL containers
std::vector<int, PerformanceTrackedAllocator> trackedVector;
```

## Platform Support

### Windows

- **CPU Monitoring**: Uses Performance Data Helper (PDH) API
- **Memory Tracking**: Uses PSAPI for process memory information
- **High-Resolution Timer**: Uses QueryPerformanceCounter

### Linux

- **CPU Monitoring**: Reads from `/proc/stat`
- **Memory Tracking**: Uses system calls for memory information
- **High-Resolution Timer**: Uses clock_gettime with CLOCK_MONOTONIC

### macOS

- **CPU Monitoring**: Uses Mach host statistics
- **Memory Tracking**: Uses Mach task information
- **High-Resolution Timer**: Uses mach_absolute_time

### GPU Monitoring

GPU monitoring is currently a placeholder that can be extended with:

- **NVIDIA**: NVML (NVIDIA Management Library)
- **AMD**: ADL (AMD Display Library)
- **Intel**: Intel Graphics Performance Analyzers
- **Generic**: OpenGL/Vulkan extensions for GPU metrics

## Best Practices

### Performance Impact

1. **Disable in Release**: Consider disabling performance monitoring in release builds
2. **Use Counters Sparingly**: Don't create too many counters as they have overhead
3. **Batch Operations**: Group related operations under single counters
4. **Update Intervals**: Use appropriate update intervals for different metrics

### Memory Management

1. **Track All Allocations**: Ensure all memory allocations are tracked
2. **Check for Leaks**: Regularly check for memory leaks during development
3. **Monitor Peak Usage**: Watch peak memory usage for optimization opportunities
4. **Reset Counters**: Reset memory tracking between tests

### Frame Timing

1. **Consistent Placement**: Always place BeginFrame/EndFrame at the same points
2. **Minimize Overhead**: Keep frame timing code minimal
3. **Monitor Spikes**: Watch for frame time spikes that indicate performance issues
4. **Target FPS**: Set appropriate FPS targets for your application

### Metrics Collection

1. **Regular Logging**: Log metrics regularly for trend analysis
2. **Threshold Monitoring**: Set up thresholds for performance warnings
3. **Historical Data**: Consider storing historical metrics for analysis
4. **External Integration**: Send metrics to external monitoring systems

### Thread Safety

1. **Avoid Nested Counters**: Don't nest counters on the same thread
2. **Cross-Thread Usage**: Be careful when using counters across threads
3. **Synchronization**: Use appropriate synchronization for multi-threaded access
4. **Performance Impact**: Consider the overhead of thread synchronization

## API Reference

### PerformanceMonitor

#### Static Methods

- `static PerformanceMonitor& GetInstance()` - Get singleton instance
- `void Initialize()` - Initialize the performance monitor
- `void Shutdown()` - Shutdown the performance monitor
- `bool IsInitialized()` - Check if initialized

#### Frame Timing

- `void BeginFrame()` - Start frame timing
- `void EndFrame()` - End frame timing
- `double GetFrameTime()` - Get current frame time in milliseconds
- `double GetAverageFrameTime()` - Get average frame time
- `double GetFPS()` - Get current FPS
- `double GetAverageFPS()` - Get average FPS
- `uint32_t GetFrameCount()` - Get total frame count

#### Performance Counters

- `PerformanceCounter* CreateCounter(const std::string& name)` - Create a new counter
- `PerformanceCounter* GetCounter(const std::string& name)` - Get existing counter
- `void RemoveCounter(const std::string& name)` - Remove a counter
- `void ResetAllCounters()` - Reset all counters

#### Memory Tracking

- `void TrackMemoryAllocation(size_t size)` - Track memory allocation
- `void TrackMemoryDeallocation(size_t size)` - Track memory deallocation
- `MemoryTracker* GetMemoryTracker()` - Get memory tracker

#### Metrics Collection

- `PerformanceMetrics CollectMetrics()` - Collect all metrics
- `void SetMetricsCollectionInterval(double intervalSeconds)` - Set collection interval
- `void SetMetricsCallback(MetricsCallback callback)` - Set metrics callback

#### Configuration

- `void SetEnabled(bool enabled)` - Enable/disable monitoring
- `bool IsEnabled()` - Check if enabled
- `void SetLoggingEnabled(bool enabled)` - Enable/disable logging
- `bool IsLoggingEnabled()` - Check if logging enabled

#### Utility Methods

- `void LogMetrics()` - Log current metrics
- `std::string GetMetricsString()` - Get formatted metrics string
- `void SaveMetricsToFile(const std::string& filename)` - Save metrics to file

### PerformanceCounter

#### Methods

- `void Start()` - Start timing
- `void Stop()` - Stop timing
- `void Reset()` - Reset counter
- `double GetLastValue()` - Get last measured value
- `double GetAverageValue()` - Get average value
- `double GetMinValue()` - Get minimum value
- `double GetMaxValue()` - Get maximum value
- `uint64_t GetSampleCount()` - Get number of samples
- `const std::string& GetName()` - Get counter name

### PerformanceTimer

#### Methods

- `void Start()` - Start timer
- `void Stop()` - Stop timer
- `void Reset()` - Reset timer
- `double GetElapsedMilliseconds()` - Get elapsed time in milliseconds
- `double GetElapsedMicroseconds()` - Get elapsed time in microseconds
- `double GetElapsedNanoseconds()` - Get elapsed time in nanoseconds
- `bool IsRunning()` - Check if timer is running

### MemoryTracker

#### Methods

- `void TrackAllocation(size_t size)` - Track allocation
- `void TrackDeallocation(size_t size)` - Track deallocation
- `void Reset()` - Reset tracker
- `uint64_t GetCurrentMemory()` - Get current memory usage
- `uint64_t GetPeakMemory()` - Get peak memory usage
- `uint64_t GetTotalMemory()` - Get total allocated memory
- `uint32_t GetAllocationCount()` - Get allocation count
- `void UpdateSystemMemory()` - Update system memory info

### PerformanceMetrics

#### Members

- `double frameTime` - Current frame time
- `double frameTimeAvg` - Average frame time
- `double fps` - Current FPS
- `double fpsAvg` - Average FPS
- `uint64_t currentMemory` - Current memory usage
- `uint64_t peakMemory` - Peak memory usage
- `uint64_t totalMemory` - Total allocated memory
- `uint32_t allocationCount` - Number of allocations
- `double cpuUsage` - Current CPU usage
- `double cpuUsageAvg` - Average CPU usage
- `uint32_t cpuCoreCount` - CPU core count
- `double gpuUsage` - GPU usage
- `double gpuMemoryUsage` - GPU memory usage
- `double gpuTemperature` - GPU temperature
- `std::unordered_map<std::string, double> counters` - Performance counters
- `uint64_t timestamp` - Timestamp
- `uint32_t frameCount` - Frame count

### Macros

- `LT_PERF_BEGIN_FRAME()` - Begin frame timing
- `LT_PERF_END_FRAME()` - End frame timing
- `LT_PERF_COUNTER(name)` - Create and use a performance counter
- `LT_PERF_SCOPE(name)` - Create a scoped timer
- `LT_PERF_TRACK_MEMORY(size)` - Track memory allocation
- `LT_PERF_UNTrack_MEMORY(size)` - Track memory deallocation

This comprehensive performance monitoring system provides the tools needed to optimize and maintain high-performance applications built with the Limitless Engine. 