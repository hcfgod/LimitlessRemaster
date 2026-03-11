# Performance Monitoring System Guide

This guide describes the current `PerformanceMonitor` API used by Limitless for frame timing, counters, memory tracking, metrics snapshots, logging, and report export.

## Overview

The current system provides:

- Frame timing and FPS
- Named performance counters
- Manual memory tracking hooks
- Periodic metrics collection callbacks
- CPU and GPU metrics snapshots when platform providers are available
- Logging and file export helpers

The authoritative public surface lives in:

- `Limitless/Source/Core/PerformanceMonitor.h`
- `Limitless/Source/Core/PerformanceMonitor.cpp`

## Engine Integration

When you use `Limitless::Application`, performance monitoring is already wired up:

- `PerformanceMonitor::Initialize()` is called during application initialization
- `PerformanceMonitor::BeginFrame()` is called once per frame near the start of `Application::Run()`
- `PerformanceMonitor::EndFrame()` is called once per frame near the end of `Application::Run()`
- `PerformanceMonitor::Shutdown()` is called during application shutdown

That means most engine/editor/runtime code should **not** manually bracket the frame with `LT_PERF_BEGIN_FRAME()` / `LT_PERF_END_FRAME()` unless you are building your own loop outside `Application`.

## Core Types

### `PerformanceMonitor`

Singleton entry point:

```cpp
auto& monitor = Limitless::PerformanceMonitor::GetInstance();
```

Main responsibilities:

- frame timing
- counter lifetime/lookup
- memory tracking aggregation
- metrics snapshots
- periodic metrics callbacks
- logging and file export

### `PerformanceCounter`

Use `PerformanceCounter` when you want named, persistent timing samples that show up in `PerformanceMetrics::counters`.

```cpp
auto& monitor = Limitless::PerformanceMonitor::GetInstance();
auto* renderCounter = monitor.CreateCounter("Rendering");

renderCounter->Start();
RenderScene();
renderCounter->Stop();

double lastMs = renderCounter->GetLastValue();
double avgMs = renderCounter->GetAverageValue();
```

Important behavior:

- `CreateCounter(name)` returns the existing counter if one already exists
- counter values are stored in milliseconds
- `CollectMetrics()` exposes the latest `GetLastValue()` per counter in `metrics.counters`

### `PerformanceTimer`

`PerformanceTimer` is a standalone high-resolution timer. It does **not** register itself with `PerformanceMonitor`.

```cpp
Limitless::PerformanceTimer timer;
timer.Start();
DoWork();
timer.Stop();

double ms = timer.GetElapsedMilliseconds();
double us = timer.GetElapsedMicroseconds();
double ns = timer.GetElapsedNanoseconds();
```

### `MemoryTracker`

`PerformanceMonitor` owns a `MemoryTracker` instance and exposes it through `GetMemoryTracker()`.

```cpp
auto& monitor = Limitless::PerformanceMonitor::GetInstance();
auto* tracker = monitor.GetMemoryTracker();

tracker->Reset();
monitor.TrackMemoryAllocation(1024);
monitor.TrackMemoryDeallocation(1024);
```

Tracked values:

- current tracked bytes
- peak tracked bytes
- total tracked allocated bytes
- tracked allocation count

## Basic Usage

### Manual initialization outside `Application`

If you are not using `Limitless::Application`, initialize and shut down the monitor yourself:

```cpp
auto& monitor = Limitless::PerformanceMonitor::GetInstance();
monitor.Initialize();
monitor.SetLoggingEnabled(true);

// your loop / tests / tool code

monitor.Shutdown();
```

### Reading frame statistics

```cpp
auto& monitor = Limitless::PerformanceMonitor::GetInstance();

double frameTimeMs = monitor.GetFrameTime();
double frameTimeAvgMs = monitor.GetAverageFrameTime();
double fps = monitor.GetFPS();
double fpsAvg = monitor.GetAverageFPS();
uint32_t frameCount = monitor.GetFrameCount();
```

### Using named counters

```cpp
auto& monitor = Limitless::PerformanceMonitor::GetInstance();
auto* updateCounter = monitor.CreateCounter("Update");
auto* renderCounter = monitor.CreateCounter("Render");

updateCounter->Start();
UpdateGame();
updateCounter->Stop();

renderCounter->Start();
RenderGame();
renderCounter->Stop();
```

### Using `LT_PERF_COUNTER(name)`

The convenience macro does **not** create a counter. It looks up an existing counter with `GetCounter(name)` and becomes a no-op if the counter has not already been created.

```cpp
auto& monitor = Limitless::PerformanceMonitor::GetInstance();
monitor.CreateCounter("Physics");

{
    LT_PERF_COUNTER("Physics");
    StepPhysics();
}
```

### Using `LT_PERF_SCOPE(name)`

Current behavior is minimal: the macro creates a local `PerformanceTimer`, starts it, and stops it at scope exit. It does **not** automatically log anything or publish a named metric to `PerformanceMonitor`.

Use `PerformanceCounter` if you want the measurement to appear in collected metrics.

### Tracking memory manually

```cpp
void* ptr = malloc(2048);
LT_PERF_TRACK_MEMORY(2048);

free(ptr);
LT_PERF_UNTrack_MEMORY(2048);
```

The deallocation macro name is currently spelled exactly as:

- `LT_PERF_UNTrack_MEMORY(size)`

## Metrics Collection

Use `CollectMetrics()` to get a snapshot of the current state:

```cpp
auto& monitor = Limitless::PerformanceMonitor::GetInstance();
Limitless::PerformanceMetrics metrics = monitor.CollectMetrics();

double fps = metrics.fps;
double fpsAvg = metrics.fpsAvg;
double frameTimeMs = metrics.frameTime;
uint64_t currentMemory = metrics.currentMemory;
double cpuUsage = metrics.cpuUsage;
double gpuUsage = metrics.gpuUsage;
```

Important `PerformanceMetrics` fields include:

- `frameTime`
- `frameTimeAvg`
- `fps`
- `fpsAvg`
- `currentMemory`
- `peakMemory`
- `totalMemory`
- `allocationCount`
- `cpuUsage`
- `cpuUsageAvg`
- `cpuCoreCount`
- `gpuUsage`
- `gpuMemoryUsage`
- `gpuTemperature`
- `gpuMemoryUsedBytes`
- `gpuMemoryTotalBytes`
- `counters`
- `timestamp`
- `frameCount`

### Metrics callback

The monitor can invoke a callback on the collection interval:

```cpp
auto& monitor = Limitless::PerformanceMonitor::GetInstance();

monitor.SetMetricsCollectionInterval(0.5);
monitor.SetMetricsCallback([](const Limitless::PerformanceMetrics& metrics) {
    if (metrics.fps < 30.0)
    {
        // react to low-FPS conditions
    }
});
```

The callback receives the same data structure returned by `CollectMetrics()`.

## Logging and Reports

Available helpers:

- `SetLoggingEnabled(bool)`
- `LogMetrics()`
- `GetMetricsString()`
- `SaveMetricsToFile(filename)`

Example:

```cpp
auto& monitor = Limitless::PerformanceMonitor::GetInstance();
monitor.SetLoggingEnabled(true);

monitor.LogMetrics();
std::string summary = monitor.GetMetricsString();
monitor.SaveMetricsToFile("performance_report.txt");
```

Current behavior notes:

- `LogMetrics()` returns immediately unless logging is enabled
- `EndFrame()` emits per-frame debug logging when logging is enabled
- `Shutdown()` logs a final snapshot when logging is enabled
- `SaveMetricsToFile(...)` writes a snapshot at the time of the call

## CPU and GPU Monitoring

CPU and GPU metrics are collected through platform/provider implementations behind `PerformanceMonitor`.

Use the snapshot API:

- `CollectMetrics().cpuUsage`
- `CollectMetrics().cpuUsageAvg`
- `CollectMetrics().cpuCoreCount`
- `CollectMetrics().gpuUsage`
- `CollectMetrics().gpuMemoryUsage`
- `CollectMetrics().gpuTemperature`
- `CollectMetrics().gpuMemoryUsedBytes`
- `CollectMetrics().gpuMemoryTotalBytes`

There is **no supported public API** to access an internal `m_cpuMonitor` or `m_gpuMonitor` member directly.

Availability notes:

- CPU metrics depend on the active platform provider
- GPU metrics may be unavailable on some systems and may report zero-valued fields in that case
- VRAM byte counters are populated through the GPU metrics provider path when available

## Best Practices

- Create long-lived counters once and reuse them
- Prefer `PerformanceCounter` over `LT_PERF_SCOPE(...)` for named, collected measurements
- Use `CollectMetrics()` for CPU/GPU data instead of reaching into implementation details
- Reset `MemoryTracker` between targeted tests if you are using it for leak/regression checks
- Keep manual memory tracking balanced so `currentMemory` stays meaningful
- If you use `Application`, do not add a second manual frame-timing bracket around the main loop

## API Summary

### `PerformanceMonitor`

- `GetInstance()`
- `Initialize()`
- `Shutdown()`
- `IsInitialized()`
- `BeginFrame()`
- `EndFrame()`
- `GetFrameTime()`
- `GetAverageFrameTime()`
- `GetFPS()`
- `GetAverageFPS()`
- `GetFrameCount()`
- `CreateCounter(name)`
- `GetCounter(name)`
- `RemoveCounter(name)`
- `ResetAllCounters()`
- `TrackMemoryAllocation(size)`
- `TrackMemoryDeallocation(size)`
- `GetMemoryTracker()`
- `CollectMetrics()`
- `SetMetricsCollectionInterval(seconds)`
- `SetMetricsCallback(callback)`
- `SetEnabled(bool)`
- `IsEnabled()`
- `SetLoggingEnabled(bool)`
- `IsLoggingEnabled()`
- `LogMetrics()`
- `GetMetricsString()`
- `SaveMetricsToFile(filename)`

### Macros

- `LT_PERF_BEGIN_FRAME()`
- `LT_PERF_END_FRAME()`
- `LT_PERF_COUNTER(name)`
- `LT_PERF_SCOPE(name)`
- `LT_PERF_TRACK_MEMORY(size)`
- `LT_PERF_UNTrack_MEMORY(size)`