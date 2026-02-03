# Graphics API Detection and Selection Guide

## Overview

The Limitless engine includes a comprehensive graphics API detection and selection system that automatically detects which graphics APIs are available on the current platform and selects the best one based on various criteria. The system is now **production-ready** for OpenGL with improved error handling, thread safety, and configuration options.

**Key Design Principles:**
- **Lightweight Detection**: The detector doesn't require SDL video subsystem or window creation
- **Separation of Concerns**: Detection is separate from context creation
- **Progressive Enhancement**: Basic detection first, detailed info after context creation
- **Thread Safety**: All operations are thread-safe and idempotent

## Features

- **Lightweight Detection**: Detects OpenGL availability without requiring SDL video subsystem
- **Progressive Enhancement**: Gets basic info during detection, detailed info after context creation
- **Platform-Specific Priority**: Different API priority lists for each platform
- **Version Detection**: Detects actual OpenGL versions and capabilities after context creation
- **Capability Analysis**: Analyzes hardware capabilities and feature support
- **Smart Selection**: Chooses the best API based on performance, compatibility, features, stability, or power efficiency
- **Fallback Support**: Graceful fallback to OpenGL if preferred APIs are not available
- **Thread Safety**: Thread-safe initialization and configuration
- **Configuration Override**: Users can override automatic API selection
- **Comprehensive Error Recovery**: Robust fallback logic for context creation
- **Debugging Support**: Detailed detection reports for troubleshooting

## Supported Graphics APIs

| API | Platform Support | Minimum Version | Status |
|-----|-----------------|-----------------|---------|
| OpenGL | All Platforms | 3.3+ | ✅ Production Ready |
| Vulkan | Windows, Linux, Android | 1.0+ | 🚧 Detection Only |
| DirectX | Windows | 12+ | 🚧 Detection Only |
| Metal | macOS, iOS | 2.0+ | 🚧 Detection Only |

## Quick Start

### Basic Usage

```cpp
#include "Graphics/GraphicsAPIDetector.h"

// Initialize the detection system (call once at application startup)
GraphicsAPIDetector::Initialize();

// Get the best available graphics API
auto bestAPI = GraphicsAPIDetector::GetBestAPI();
if (bestAPI) {
    GraphicsAPI selectedAPI = bestAPI.value();
    std::cout << "Selected API: " << GraphicsAPIToString(selectedAPI) << std::endl;
}
```

### Advanced Usage

```cpp
// Set a preferred graphics API (overrides automatic selection)
GraphicsAPIDetector::SetPreferredAPI(GraphicsAPI::OpenGL);

// Get detailed information about all detected APIs
auto detectionResults = GraphicsAPIDetector::GetDetectionResults();
for (const auto& caps : detectionResults) {
    std::cout << "API: " << GraphicsAPIToString(caps.api) << std::endl;
    std::cout << "Version: " << caps.version.ToString() << std::endl;
    std::cout << "Supported: " << (caps.isSupported ? "Yes" : "No") << std::endl;
}

// Get comprehensive detection report for debugging
std::string report = GraphicsAPIDetector::GetDetectionReport();
std::cout << report << std::endl;
```

## Initialization

The detection system must be initialized once at application startup:

```cpp
// In Application::InternalInitialize() or similar startup code
GraphicsAPIDetector::Initialize();
```

**Important**: The system is thread-safe and idempotent, so calling `Initialize()` multiple times is safe but unnecessary.

## Configuration

### Setting Preferred API

You can override the automatic API selection:

```cpp
// Force the use of OpenGL
GraphicsAPIDetector::SetPreferredAPI(GraphicsAPI::OpenGL);

// Check if a preferred API is set
auto preferred = GraphicsAPIDetector::GetPreferredAPI();
if (preferred) {
    std::cout << "Preferred API: " << GraphicsAPIToString(preferred.value()) << std::endl;
}

// Clear preferred API setting
GraphicsAPIDetector::ClearPreferredAPI();
```

### Error Recovery

The system includes robust error recovery:

```cpp
// Check if detection system is initialized
if (!GraphicsAPIDetector::IsInitialized()) {
    std::cerr << "Graphics API Detection not initialized!" << std::endl;
    return;
}

// Validate API selection
std::string errorMessage;
if (!GraphicsAPISelector::ValidateSelection(GraphicsAPI::OpenGL, errorMessage)) {
    std::cerr << "OpenGL validation failed: " << errorMessage << std::endl;
}
```

## Integration with Graphics Context

The graphics context creation automatically uses the detection system:

```cpp
// This will automatically select the best available graphics API
std::unique_ptr<GraphicsContext> context = CreateGraphicsContext();
```

The system will:
1. Check if detection system is initialized
2. Find the best available graphics API (respecting preferred API setting)
3. Create the appropriate graphics context
4. Fall back to OpenGL if no other APIs are available
5. Provide detailed logging about the selection process

## OpenGL Context Creation

The OpenGL context creation now includes robust fallback logic:

```cpp
// The system will try multiple OpenGL versions in order:
// 1. Requested version (from detection)
// 2. OpenGL 4.5, 4.4, 4.3, 4.2, 4.1, 4.0
// 3. OpenGL 3.3, 3.2, 3.1, 3.0
// 4. Throw exception if no version works
```

## Debugging and Diagnostics

### Detection Report

Get comprehensive information about the detection system:

```cpp
std::string report = GraphicsAPIDetector::GetDetectionReport();
std::cout << report << std::endl;
```

This will output:
- Initialization status
- Preferred API setting
- Detected APIs with versions and capabilities
- Best API selection
- Platform-specific priority list

### Error Information

Get detailed error information:

```cpp
// Check if an API is supported
if (!GraphicsAPIDetector::IsAPISupported(GraphicsAPI::Vulkan)) {
    std::string reason = GraphicsAPIDetector::GetUnsupportedReason(GraphicsAPI::Vulkan);
    std::cout << "Vulkan not supported: " << reason << std::endl;
}
```

## Performance Considerations

- The detection system is designed to be fast and lightweight
- Results are cached after initialization
- Detection only occurs once unless explicitly refreshed
- Temporary windows are created and destroyed quickly for OpenGL testing
- Thread-safe operations use minimal locking

## Production Readiness

### ✅ Production Ready Features

1. **Thread Safety**: All operations are thread-safe
2. **Error Recovery**: Robust fallback logic for context creation
3. **Configuration**: User can override automatic selection
4. **Debugging**: Comprehensive diagnostic information
5. **OpenGL Support**: Full production-ready OpenGL detection and context creation
6. **Initialization**: Proper initialization at application startup

### 🚧 Future Enhancements

1. **Vulkan Implementation**: Full Vulkan context implementation
2. **DirectX Implementation**: Full DirectX 12 context implementation
3. **Metal Implementation**: Full Metal context implementation
4. **Performance Benchmarking**: Runtime performance comparison between APIs
5. **Dynamic Switching**: Ability to switch graphics APIs at runtime
6. **Configuration Persistence**: Save and load API preferences

## Troubleshooting

### Common Issues

1. **Detection Not Initialized**: Ensure `GraphicsAPIDetector::Initialize()` is called at application startup
2. **OpenGL Detection Fails**: Check if OpenGL drivers are installed and up to date
3. **Context Creation Fails**: The system will automatically try lower OpenGL versions
4. **Preferred API Not Used**: Check if the preferred API is actually supported on the system

### Debug Information

Enable debug logging to get detailed information about the detection process:

```cpp
// The system will log detection results automatically
// Check the console output for detailed information
// Use GetDetectionReport() for comprehensive diagnostics
```

## API Reference

### GraphicsAPIDetector

- `Initialize()`: Initialize the detection system (thread-safe, idempotent)
- `SetPreferredAPI(GraphicsAPI)`: Set preferred graphics API
- `GetPreferredAPI()`: Get preferred graphics API setting
- `ClearPreferredAPI()`: Clear preferred API setting
- `DetectAvailableAPIs()`: Detect all available APIs
- `GetBestAPI()`: Get the best available API (respects preferred setting)
- `GetAPI(GraphicsAPI)`: Get specific API information
- `IsAPISupported(GraphicsAPI)`: Check if API is supported
- `GetRecommendedAPI()`: Get platform-recommended API
- `GetFallbackAPI()`: Get fallback API
- `GetAPIPriorityList()`: Get platform-specific priority list
- `ValidateAPISelection(GraphicsAPI)`: Validate API selection
- `GetAPIInfo(GraphicsAPI)`: Get detailed API information
- `GetSystemRequirements(GraphicsAPI)`: Get system requirements
- `MeetsRequirements(GraphicsAPI)`: Check if system meets requirements
- `GetUnsupportedReason(GraphicsAPI)`: Get unsupported reason
- `Refresh()`: Refresh detection results
- `IsInitialized()`: Check if detection system is initialized
- `GetDetectionResults()`: Get all detection results
- `GetDetectionReport()`: Get comprehensive detection report

### GraphicsAPISelector

- `SelectAPI(SelectionCriteria)`: Select API based on criteria
- `SelectAPI(priorityList)`: Select API from custom priority list
- `GetRecommendation(SelectionCriteria)`: Get detailed recommendation
- `ValidateSelection(GraphicsAPI, errorMessage)`: Validate selection
- `GetPerformanceComparison()`: Get performance comparison
- `GetFeatureComparison()`: Get feature comparison

## Conclusion

The graphics API detection and selection system is now **production-ready** for OpenGL with improved error handling, thread safety, and configuration options. It provides a robust foundation for cross-platform graphics development while maintaining the flexibility to add support for other graphics APIs in the future. 