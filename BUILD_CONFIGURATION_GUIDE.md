# Build Configuration Guide

This document explains the build configuration system for the Limitless Engine, including logging levels, optimization settings, and platform-specific configurations.

## Build Configurations

The project supports three main build configurations:

### 1. Debug Configuration
- **Purpose**: Development and debugging
- **Optimization**: Disabled (`optimize "off"`)
- **Symbols**: Enabled (`symbols "on"`)
- **Runtime**: Debug

**Logging Settings:**
- **Log Level**: TRACE (all levels enabled)
- **Console Logging**: Enabled
- **File Logging**: Enabled
- **Core Logging**: Enabled (engine internal logs)

**Defines:**
```cpp
LT_CONFIG_DEBUG
LT_LOG_LEVEL_TRACE_ENABLED
LT_LOG_LEVEL_DEBUG_ENABLED
LT_LOG_LEVEL_INFO_ENABLED
LT_LOG_LEVEL_WARN_ENABLED
LT_LOG_LEVEL_ERROR_ENABLED
LT_LOG_LEVEL_CRITICAL_ENABLED
LT_LOG_CONSOLE_ENABLED
LT_LOG_FILE_ENABLED
LT_LOG_CORE_ENABLED
```

### 2. Release Configuration
- **Purpose**: Testing and performance validation
- **Optimization**: Speed optimization (`optimize "speed"`)
- **Symbols**: Disabled (`symbols "off"`)
- **Runtime**: Release

**Logging Settings:**
- **Log Level**: INFO (info, warn, error, critical)
- **Console Logging**: Enabled
- **File Logging**: Enabled
- **Core Logging**: Disabled (only client logs)

**Defines:**
```cpp
LT_CONFIG_RELEASE
LT_LOG_LEVEL_INFO_ENABLED
LT_LOG_LEVEL_WARN_ENABLED
LT_LOG_LEVEL_ERROR_ENABLED
LT_LOG_LEVEL_CRITICAL_ENABLED
LT_LOG_CONSOLE_ENABLED
LT_LOG_FILE_ENABLED
LT_LOG_CORE_DISABLED
```

### 3. Dist Configuration
- **Purpose**: Production deployment
- **Optimization**: Speed optimization (`optimize "speed"`)
- **Symbols**: Disabled (`symbols "off"`)
- **Runtime**: Release
- **Subsystem**: Windows (GUI) - No console window

**Logging Settings:**
- **Log Level**: WARN (warn, error, critical only)
- **Console Logging**: Disabled
- **File Logging**: Enabled
- **Core Logging**: Disabled (only client logs)

**Defines:**
```cpp
LT_CONFIG_DIST
LT_LOG_LEVEL_WARN_ENABLED
LT_LOG_LEVEL_ERROR_ENABLED
LT_LOG_LEVEL_CRITICAL_ENABLED
LT_LOG_CONSOLE_DISABLED
LT_LOG_FILE_ENABLED
LT_LOG_CORE_DISABLED
```

**Windows-Specific:**
- Sets subsystem to Windows (GUI) to prevent console window from appearing
- Uses `mainCRTStartup` entry point for proper GUI application behavior

## Platform-Specific Defines

### Windows
```cpp
LT_PLATFORM_WINDOWS
LT_COMPILER_MSVC (when using MSVC)
```

### macOS
```cpp
LT_PLATFORM_MACOS
LT_PLATFORM_MAC
LT_ARCHITECTURE_ARM64 (on Apple Silicon)
LT_ARCHITECTURE_X64 (on Intel)
LT_PLATFORM_MAC_ARM64 (on Apple Silicon)
LT_PLATFORM_MAC_X64 (on Intel)
LT_COMPILER_CLANG (when using Clang)
LT_COMPILER_APPLE_CLANG (when using Apple Clang)
```

### Linux
```cpp
LT_PLATFORM_LINUX
LT_ARCHITECTURE_ARM64 (on ARM64)
LT_ARCHITECTURE_X64 (on x64)
LT_COMPILER_GCC (when using GCC)
LT_COMPILER_CLANG (when using Clang)
```

## Logging System

### Log Levels
The logging system supports the following levels (from lowest to highest priority):
1. **TRACE** - Detailed trace information
2. **DEBUG** - Debug information
3. **INFO** - General information
4. **WARN** - Warning messages
5. **ERROR** - Error messages
6. **CRITICAL** - Critical error messages

### Logging Macros

#### Client Logging (Application Layer)
```cpp
LT_TRACE("Trace message");
LT_DBG("Debug message");
LT_INFO("Info message");
LT_WARN("Warning message");
LT_ERROR("Error message");
LT_CRITICAL("Critical message");

// Conditional logging
LT_TRACE_IF(condition, "Conditional trace");
LT_DBG_IF(condition, "Conditional debug");
LT_INFO_IF(condition, "Conditional info");
LT_WARN_IF(condition, "Conditional warning");
LT_ERROR_IF(condition, "Conditional error");
LT_CRITICAL_IF(condition, "Conditional critical");
```

#### Core Logging (Engine Internal)
```cpp
LT_CORE_TRACE("Core trace message");
LT_CORE_DEBUG("Core debug message");
LT_CORE_INFO("Core info message");
LT_CORE_WARN("Core warning message");
LT_CORE_ERROR("Core error message");
LT_CORE_CRITICAL("Core critical message");

// Conditional core logging
LT_CORE_TRACE_IF(condition, "Conditional core trace");
LT_CORE_DEBUG_IF(condition, "Conditional core debug");
LT_CORE_INFO_IF(condition, "Conditional core info");
LT_CORE_WARN_IF(condition, "Conditional core warning");
LT_CORE_ERROR_IF(condition, "Conditional core error");
LT_CORE_CRITICAL_IF(condition, "Conditional core critical");
```

### Build Configuration Impact on Logging

#### Debug Build
- All logging levels are enabled
- Both console and file logging are active
- Core logging is enabled for engine debugging
- Logging macros compile to actual logging calls

#### Release Build
- Only INFO, WARN, ERROR, and CRITICAL levels are enabled
- Console and file logging are active
- Core logging is disabled (compiles to no-ops)
- TRACE and DEBUG macros compile to no-ops

#### Dist Build
- Only WARN, ERROR, and CRITICAL levels are enabled
- Console logging is disabled (compiles to no-ops)
- File logging is active
- Core logging is disabled (compiles to no-ops)
- TRACE, DEBUG, and INFO macros compile to no-ops

## Optimization Settings

### Debug Configuration
- **Optimization**: Disabled for faster compilation and better debugging
- **Symbols**: Enabled for stack traces and debugging
- **Runtime**: Debug runtime libraries

### Release Configuration
- **Optimization**: Speed optimization for performance
- **Symbols**: Disabled to reduce binary size
- **Runtime**: Release runtime libraries

### Dist Configuration
- **Optimization**: Speed optimization for maximum performance
- **Symbols**: Disabled for minimal binary size
- **Runtime**: Release runtime libraries

## Console Window Prevention (Windows)

In the Dist build configuration, the application is configured to run as a Windows GUI application, which prevents a console window from appearing. This is achieved through:

1. **Premake5 Configuration**: Sets the subsystem to "windows" for Dist builds
2. **Linker Pragma**: Uses `#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")` to ensure proper GUI application behavior

This configuration only affects Windows builds and is ignored on other platforms (macOS, Linux).

## Usage Examples

### Conditional Compilation
```cpp
#ifdef LT_CONFIG_DEBUG
    // Debug-only code
    LT_CORE_TRACE("Debug information");
#endif

#ifdef LT_CONFIG_RELEASE
    // Release-only code
    LT_INFO("Release build active");
#endif

#ifdef LT_CONFIG_DIST
    // Dist-only code
    LT_WARN("Production build");
#endif
```

### Platform-Specific Code
```cpp
#ifdef LT_PLATFORM_WINDOWS
    // Windows-specific code
#elif defined(LT_PLATFORM_MACOS)
    // macOS-specific code
#elif defined(LT_PLATFORM_LINUX)
    // Linux-specific code
#endif
```

### Architecture-Specific Code
```cpp
#ifdef LT_ARCHITECTURE_ARM64
    // ARM64-specific optimizations
#elif defined(LT_ARCHITECTURE_X64)
    // x64-specific optimizations
#endif
```

## Building

### Generate Project Files
```bash
# Windows
premake5 vs2022

# macOS/Linux
premake5 gmake2
```

### Build Commands
```bash
# Debug build
make config=debug

# Release build
make config=release

# Dist build
make config=dist
```

### Platform-Specific Builds
```bash
# Windows x64 Debug
make config=debug platform=x64

# macOS ARM64 Release
make config=release platform=ARM64

# Linux x64 Dist
make config=dist platform=x64
```

## Best Practices

1. **Use appropriate logging levels**: Use TRACE for detailed debugging, INFO for general information, WARN for warnings, and ERROR/CRITICAL for actual problems.

2. **Conditional compilation**: Use build configuration defines to include/exclude code based on the build type.

3. **Performance considerations**: In Release and Dist builds, logging calls that are disabled compile to no-ops, so there's no runtime overhead.

4. **Console vs File logging**: In Dist builds, console logging is disabled to avoid cluttering the terminal, but file logging remains active for debugging production issues.

5. **Core logging**: Use core logging macros for engine internal messages, and client logging macros for application-level messages. 