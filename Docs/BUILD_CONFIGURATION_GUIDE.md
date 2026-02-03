# Build Configuration Guide

This guide covers the build system configuration, platform-specific settings, and C++20 coroutine support in the Limitless Engine.

## Table of Contents

1. [Build System Overview](#build-system-overview)
2. [Platform-Specific Build Options](#platform-specific-build-options)
3. [C++20 Coroutine Support](#c20-coroutine-support)
4. [Build Configurations](#build-configurations)
5. [Cross-Platform Building](#cross-platform-building)
6. [Best Practices](#best-practices)

## Build System Overview

The Limitless Engine uses **Premake5** as its build system generator, providing:
- **Cross-platform support** for Windows, macOS, and Linux
- **Multiple compiler support** (MSVC, GCC, Clang)
- **Multiple architecture support** (x64, ARM64)
- **C++20 coroutine support** with platform-specific flags
- **Automated project generation** with proper dependencies

### Project Structure
```
LimitlessRemaster/
├── premake5.lua              # Main workspace configuration
├── Limitless/premake5.lua    # Core engine library
├── Sandbox/premake5.lua      # Example application
├── Test/premake5.lua         # Unit tests
└── Scripts/                  # Build scripts
    ├── build-windows.bat     # Windows build script
    └── build-unix.sh         # Unix/Linux/macOS build script
```

## Platform-Specific Build Options

### Windows (MSVC)

**Build Options:**
```lua
filter "system:windows"
    cppdialect "C++20"
    staticruntime "On"
    systemversion "latest"
    
    buildoptions
    {
        "/utf-8",
        "/std:c++20",
    }
```

**Key Features:**
- **C++20 Standard**: Full C++20 language support
- **UTF-8 Support**: Proper Unicode handling
- **Coroutine Support**: `/await` flag enables C++20 coroutines
- **Static Runtime**: Self-contained executables
- **Latest SDK**: Uses the latest Windows SDK

### macOS (GCC/Clang)

**Build Options:**
```lua
filter "system:macosx"
    cppdialect "C++20"
    staticruntime "On"
    
    buildoptions
    {
        "-std=c++20",
    }
```

**Key Features:**
- **C++20 Standard**: Full C++20 language support
- **Coroutine Support**: `-fcoroutines` flag enables C++20 coroutines
- **Framework Integration**: Native macOS framework support
- **ARM64 Support**: Native Apple Silicon support

### Linux (GCC/Clang)

**Build Options:**
```lua
filter "system:linux"
    cppdialect "C++20"
    staticruntime "On"
    
    buildoptions
    {
        "-std=c++20",
    }
```

**Key Features:**
- **C++20 Standard**: Full C++20 language support
- **Coroutine Support** Full coroutine support
- **System Libraries**: Native Linux library integration
- **Multi-architecture**: x64 and ARM64 support

## C++20 Coroutine Support

### Overview

The engine fully supports C++20 coroutines across all platforms with appropriate compiler flags:

- **Windows (MSVC)**: `/await` flag

### Usage Examples

```cpp
#include <coroutine>
#include "Core/Concurrency/AsyncIO.h"

using namespace Limitless::Async;

// Coroutine-based async function
Async::Task<std::string> LoadFileAsync(const std::string& path) {
    // This runs in a background thread
    auto content = co_await ReadFileAsync(path);
    co_return content;
}

// Using coroutines in your application
void ExampleUsage() {
    auto task = LoadFileAsync("config.json");
    auto content = task.Get(); // Wait for completion
}
```

### Platform-Specific Implementation

The coroutine support is implemented differently per platform:

**Windows (MSVC):**
```cpp
// MSVC automatically supports coroutines with /await flag
#include <coroutine>

template<typename T>
struct Task {
    struct promise_type {
        T value;
        Task get_return_object() { return Task{}; }
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_value(T v) { value = v; }
        void unhandled_exception() {}
    };
};
```

**macOS/Linux (GCC/Clang):**
```cpp
// GCC/Clang requires -fcoroutines flag
#include <coroutine>

template<typename T>
struct Task {
    struct promise_type {
        T value;
        Task get_return_object() { return Task{}; }
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_value(T v) { value = v; }
        void unhandled_exception() {}
    };
};
```

## Build Configurations

### Debug Configuration
```lua
filter "configurations:Debug"
    defines 
    { 
        "LT_CONFIG_DEBUG",
        "LT_LOG_LEVEL_TRACE_ENABLED",
        "LT_LOG_LEVEL_DEBUG_ENABLED",
        "LT_LOG_LEVEL_INFO_ENABLED",
        "LT_LOG_LEVEL_WARN_ENABLED",
        "LT_LOG_LEVEL_ERROR_ENABLED",
        "LT_LOG_LEVEL_CRITICAL_ENABLED",
        "LT_LOG_CONSOLE_ENABLED",
        "LT_LOG_FILE_ENABLED",
        "LT_LOG_CORE_ENABLED"
    }
    runtime "Debug"
    symbols "on"
    optimize "off"
```

**Features:**
- **Full Debugging**: Complete debug information
- **All Log Levels**: Maximum logging output
- **No Optimization**: Easier debugging
- **Symbols**: Full symbol information

### Release Configuration
```lua
filter "configurations:Release"
    defines 
    { 
        "LT_CONFIG_RELEASE",
        "LT_LOG_LEVEL_INFO_ENABLED",
        "LT_LOG_LEVEL_WARN_ENABLED",
        "LT_LOG_LEVEL_ERROR_ENABLED",
        "LT_LOG_LEVEL_CRITICAL_ENABLED",
        "LT_LOG_CONSOLE_ENABLED",
        "LT_LOG_FILE_ENABLED",
        "LT_LOG_CORE_DISABLED"
    }
    runtime "Release"
    optimize "speed"
    symbols "off"
```

**Features:**
- **Speed Optimization**: Maximum performance
- **Reduced Logging**: Only essential log levels
- **No Debug Symbols**: Smaller executable size
- **Release Runtime**: Optimized runtime libraries

### Distribution Configuration
```lua
filter "configurations:Dist"
    defines 
    { 
        "LT_CONFIG_DIST",
        "LT_LOG_LEVEL_WARN_ENABLED",
        "LT_LOG_LEVEL_ERROR_ENABLED",
        "LT_LOG_LEVEL_CRITICAL_ENABLED",
        "LT_LOG_CONSOLE_DISABLED",
        "LT_LOG_FILE_ENABLED",
        "LT_LOG_CORE_DISABLED"
    }
    runtime "Release"
    optimize "speed"
    symbols "off"
    systemversion "latest"
```

**Features:**
- **Maximum Performance**: Full optimization
- **Minimal Logging**: Only critical errors
- **No Console Output**: Clean user experience
- **Distribution Ready**: Production-ready build

## Cross-Platform Building

### Windows Building

**Using Build Script:**
```batch
# Build Debug x64
Scripts\build-windows.bat Debug x64

# Build Release ARM64
Scripts\build-windows.bat Release ARM64

# Build Distribution x64
Scripts\build-windows.bat Dist x64
```

**Using Premake Directly:**
```batch
# Generate Visual Studio solution
Vendor/Premake/premake5 vs2022

# Build with MSBuild
msbuild LimitlessRemaster.sln /p:Configuration=Debug /p:Platform=x64
```

### macOS Building

**Using Build Script:**
```bash
# Build Debug with GCC
Scripts/build-unix.sh --config Debug --compiler gcc

# Build Release with Clang
Scripts/build-unix.sh --config Release --compiler clang

# Build Distribution
Scripts/build-unix.sh --config Dist --compiler clang
```

**Using Premake Directly:**
```bash
# Generate Makefiles
Vendor/Premake/premake5 gmake2

# Build with make
make -j$(sysctl -n hw.ncpu) config=Debug_x64
```

### Linux Building

**Using Build Script:**
```bash
# Build Debug with GCC
Scripts/build-unix.sh --config Debug --compiler gcc

# Build Release with Clang
Scripts/build-unix.sh --config Release --compiler clang

# Build Distribution
Scripts/build-unix.sh --config Dist --compiler gcc
```

**Using Premake Directly:**
```bash
# Generate Makefiles
Vendor/Premake/premake5 gmake2

# Build with make
make -j$(nproc) config=Debug_x64
```

## Best Practices

### Compiler Selection

1. **Windows**: Use MSVC for best Windows integration
2. **macOS**: Use Clang for best Apple ecosystem integration
3. **Linux**: Use GCC for best Linux compatibility

### Configuration Selection

1. **Development**: Use Debug configuration for development
2. **Testing**: Use Release configuration for performance testing
3. **Distribution**: Use Dist configuration for final builds

### Coroutine Usage

1. **Platform Awareness**: Always check coroutine support
2. **Error Handling**: Use proper exception handling in coroutines
3. **Memory Management**: Be aware of coroutine memory allocation
4. **Performance**: Profile coroutine performance in your use case

### Build Optimization

1. **Parallel Building**: Use `-j` flag for parallel compilation
2. **Incremental Builds**: Use proper dependency management
3. **Clean Builds**: Clean build directory for major changes
4. **Cache Management**: Use build caching when available

### Platform-Specific Considerations

1. **Windows**: Ensure UTF-8 encoding for source files
2. **macOS**: Consider framework vs static library usage
3. **Linux**: Ensure proper library linking and dependencies

## Troubleshooting

### Common Build Issues

1. **Coroutine Support**: Ensure `/await` or `-fcoroutines` flags are set
2. **C++20 Support**: Verify compiler supports C++20 standard
3. **Library Dependencies**: Check all required libraries are available
4. **Platform SDK**: Ensure latest platform SDK is installed

### Performance Issues

1. **Build Time**: Use parallel compilation and build caching
2. **Binary Size**: Use appropriate optimization levels
3. **Runtime Performance**: Profile with Release configuration

### Platform-Specific Issues

1. **Windows**: Check Windows SDK version compatibility
2. **macOS**: Verify Xcode command line tools installation
3. **Linux**: Ensure development libraries are installed

This comprehensive build configuration ensures optimal performance and compatibility across all supported platforms while providing full C++20 coroutine support. 