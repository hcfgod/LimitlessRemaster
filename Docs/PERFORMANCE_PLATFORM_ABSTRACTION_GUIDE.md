# Performance Monitoring Platform Abstraction Guide

This guide explains the new platform abstraction architecture for the performance monitoring system in the Limitless Engine.

## Overview

The performance monitoring system has been refactored to use a clean platform abstraction layer, separating platform-specific code from the main performance monitoring logic. This follows the same pattern as the existing platform detection system.

## Architecture

### Core Components

1. **PerformancePlatform.h** - Defines the platform abstraction interfaces
2. **PerformancePlatformFactory** - Factory for creating platform-specific implementations
3. **Platform-specific implementations** - Windows, Linux, and macOS specific code

### Interface Design

The system provides three main interfaces:

#### ICPUPlatform
- `Initialize()` - Initialize CPU monitoring
- `Shutdown()` - Clean up resources
- `Update()` - Update CPU metrics
- `Reset()` - Reset metrics
- `GetCurrentUsage()` - Get current CPU usage percentage
- `GetAverageUsage()` - Get average CPU usage
- `GetCoreCount()` - Get number of CPU cores
- `SetUpdateInterval()` - Set update frequency

#### IGPUPlatform
- `Initialize()` - Initialize GPU monitoring
- `Shutdown()` - Clean up resources
- `Update()` - Update GPU metrics
- `Reset()` - Reset metrics
- `GetUsage()` - Get GPU usage percentage
- `GetMemoryUsage()` - Get GPU memory usage
- `GetTemperature()` - Get GPU temperature
- `IsAvailable()` - Check if GPU monitoring is available
- `SetUpdateInterval()` - Set update frequency

#### ISystemPlatform
- `Initialize()` - Initialize system monitoring
- `Shutdown()` - Clean up resources
- `Update()` - Update system metrics
- `GetTotalMemory()` - Get total system memory
- `GetAvailableMemory()` - Get available system memory
- `GetProcessMemory()` - Get current process memory usage
- `GetProcessId()` - Get current process ID
- `GetThreadId()` - Get current thread ID

### Platform-Specific Implementations

#### Windows
- **WindowsCPUPlatform** - Uses PDH (Performance Data Helper) for CPU monitoring
- **WindowsGPUPlatform** - Placeholder for GPU monitoring (requires NVML/AMD ADL)
- **WindowsSystemPlatform** - Uses Windows API for system information

#### Linux
- **LinuxCPUPlatform** - Reads `/proc/stat` for CPU monitoring
- **LinuxGPUPlatform** - Placeholder for GPU monitoring (requires NVML)
- **LinuxSystemPlatform** - Uses `/proc` filesystem for system information

#### macOS
- **macOSCPUPlatform** - Uses Mach APIs for CPU monitoring
- **macOSGPUPlatform** - Placeholder for GPU monitoring
- **macOSSystemPlatform** - Uses sysctl and Mach APIs for system information

## Benefits of This Architecture

### 1. **Separation of Concerns**
- Platform-specific code is isolated in separate files
- Main performance monitoring logic is platform-agnostic
- Easy to add new platforms without modifying core code

### 2. **Maintainability**
- Each platform implementation is self-contained
- Changes to one platform don't affect others
- Clear interfaces make testing easier

### 3. **Extensibility**
- Easy to add new monitoring capabilities
- Simple to integrate new GPU monitoring libraries
- Can add platform-specific optimizations

### 4. **Consistency**
- Follows the same pattern as the existing platform detection system
- Consistent API across all platforms
- Uniform error handling and logging

## Usage Example

```cpp
// The PerformanceMonitor now automatically uses the appropriate platform implementation
auto& monitor = Limitless::PerformanceMonitor::GetInstance();
monitor.Initialize();

// All platform-specific details are hidden behind the abstraction
auto metrics = monitor.CollectMetrics();
std::cout << "CPU Usage: " << metrics.cpuUsage << "%" << std::endl;
std::cout << "GPU Usage: " << metrics.gpuUsage << "%" << std::endl;
```

## Adding New Platforms

To add support for a new platform:

1. **Create platform-specific header** (`NewPlatformPerformancePlatform.h`)
2. **Implement the interfaces** (`NewPlatformPerformancePlatform.cpp`)
3. **Update the factory** (`PerformancePlatform.cpp`)
4. **Add platform detection** (if needed)

Example:
```cpp
// In PerformancePlatform.cpp
#ifdef LT_PLATFORM_NEWPLATFORM
    #include "Platform/NewPlatform/NewPlatformPerformancePlatform.h"
#endif

std::unique_ptr<ICPUPlatform> PerformancePlatformFactory::CreateCPUPlatform() {
#ifdef LT_PLATFORM_NEWPLATFORM
    return std::make_unique<NewPlatformCPUPlatform>();
#elif defined(LT_PLATFORM_WINDOWS)
    return std::make_unique<WindowsCPUPlatform>();
// ... other platforms
#endif
}
```

## Migration from Old System

The old system had platform-specific code scattered throughout `PerformanceMonitor.cpp`:

```cpp
// OLD - Platform-specific code mixed with core logic
#ifdef LT_PLATFORM_WINDOWS
    PDH_FMT_COUNTERVALUE value;
    if (PdhCollectQueryData(m_platformData->query) == ERROR_SUCCESS) {
        // Windows-specific CPU monitoring
    }
#elif defined(LT_PLATFORM_LINUX)
    // Linux-specific CPU monitoring
#elif defined(LT_PLATFORM_MACOS)
    // macOS-specific CPU monitoring
#endif
```

The new system separates this:

```cpp
// NEW - Clean abstraction
void CPUMonitor::Update() {
    if (!m_platform) {
        return;
    }
    m_platform->Update(); // Platform-specific implementation
    m_currentUsage = m_platform->GetCurrentUsage();
}
```

## Future Enhancements

1. **GPU Monitoring Integration**
   - Add NVML support for NVIDIA GPUs
   - Add AMD ADL support for AMD GPUs
   - Add Metal Performance Shaders for macOS

2. **Additional Metrics**
   - Network I/O monitoring
   - Disk I/O monitoring
   - Power consumption (where available)

3. **Advanced Features**
   - Historical data tracking
   - Performance alerts
   - Automated optimization suggestions

## Best Practices

1. **Always check for null platform implementations**
2. **Use the factory pattern for platform creation**
3. **Keep platform-specific code isolated**
4. **Provide fallback implementations for unsupported features**
5. **Use consistent error handling across platforms**
6. **Document platform-specific limitations**

This architecture provides a solid foundation for cross-platform performance monitoring while maintaining clean, maintainable code. 