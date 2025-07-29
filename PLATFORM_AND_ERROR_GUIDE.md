# Platform Detection and Error Handling Guide

This guide covers the comprehensive platform detection system and enhanced error handling capabilities added to the Limitless Engine.

## Table of Contents

1. [Platform Detection System](#platform-detection-system)
2. [Enhanced Error Handling](#enhanced-error-handling)
3. [Integration Examples](#integration-examples)
4. [Best Practices](#best-practices)
5. [API Reference](#api-reference)

## Platform Detection System

The platform detection system provides comprehensive information about the current platform, architecture, compiler, and system capabilities at both compile-time and runtime.

### Key Features

- **Cross-Platform Support**: Windows, macOS, Linux, Android, iOS, Web
- **Architecture Detection**: x86, x64, ARM32, ARM64, RISC-V
- **Compiler Detection**: MSVC, GCC, Clang, AppleClang
- **System Capabilities**: CPU features, memory, graphics APIs
- **Platform Utilities**: Path handling, environment variables, system calls

### Basic Usage

```cpp
#include "Platform/Platform.h"

// Initialize platform detection (done automatically in Application)
Limitless::PlatformDetection::Initialize();

// Get platform information
const auto& platformInfo = Limitless::PlatformDetection::GetPlatformInfo();

// Platform checks
if (Limitless::PlatformDetection::IsWindows()) {
    // Windows-specific code
}

if (Limitless::PlatformDetection::IsX64()) {
    // x64-specific optimizations
}

// System capabilities
uint32_t cpuCount = Limitless::PlatformDetection::GetCPUCount();
uint64_t totalMemory = Limitless::PlatformDetection::GetTotalMemory();

// CPU feature checks
if (Limitless::PlatformDetection::HasAVX2()) {
    // Use AVX2 instructions
}
```

### Platform-Specific Macros

The system provides compile-time macros for platform detection:

```cpp
#ifdef LT_PLATFORM_WINDOWS
    // Windows-specific code
#elif defined(LT_PLATFORM_MACOS)
    // macOS-specific code
#elif defined(LT_PLATFORM_LINUX)
    // Linux-specific code
#endif

#ifdef LT_ARCHITECTURE_X64
    // x64-specific code
#elif defined(LT_ARCHITECTURE_ARM64)
    // ARM64-specific code
#endif

#ifdef LT_COMPILER_MSVC
    // MSVC-specific code
#elif defined(LT_COMPILER_GCC)
    // GCC-specific code
#endif
```

### Platform Utilities

```cpp
// Path utilities
std::string separator = Limitless::PlatformUtils::GetPathSeparator();
std::string path = Limitless::PlatformUtils::JoinPath("dir", "file.txt");
std::string dir = Limitless::PlatformUtils::GetDirectoryName(path);
std::string file = Limitless::PlatformUtils::GetFileName(path);

// Environment variables
auto value = Limitless::PlatformUtils::GetEnvironmentVariable("PATH");
Limitless::PlatformUtils::SetEnvironmentVariable("CUSTOM_VAR", "value");

// Process information
uint32_t pid = Limitless::PlatformUtils::GetCurrentProcessId();
uint32_t tid = Limitless::PlatformUtils::GetCurrentThreadId();

// Time utilities
uint64_t highResTime = Limitless::PlatformUtils::GetHighResolutionTime();
uint64_t systemTime = Limitless::PlatformUtils::GetSystemTime();

// Memory utilities
void* alignedPtr = Limitless::PlatformUtils::AllocateAligned(1024, 16);
Limitless::PlatformUtils::FreeAligned(alignedPtr);

// Debug utilities
Limitless::PlatformUtils::BreakIntoDebugger();
Limitless::PlatformUtils::OutputDebugString("Debug message");
```

## Enhanced Error Handling

The enhanced error handling system provides comprehensive error management with severity levels, context information, and platform integration.

### Key Features

- **Error Severity Levels**: Info, Warning, Error, Critical, Fatal
- **Rich Context Information**: Function, class, module, thread, platform info
- **Platform Integration**: System error codes, platform-specific errors
- **Result Pattern**: Type-safe error handling with `Result<T>` class
- **Error Categories**: System, Platform, Graphics, Audio, Input, Resource, etc.

### Basic Error Creation

```cpp
#include "Core/Error.h"

// Basic error
Limitless::Error error(Limitless::ErrorCode::FileNotFound, "Configuration file not found");

// Error with severity
Limitless::Error criticalError(
    Limitless::ErrorCode::OutOfMemory, 
    "Memory allocation failed",
    std::source_location::current(),
    Limitless::ErrorSeverity::Critical
);

// Error with context
error.SetFunctionName("LoadConfiguration");
error.SetClassName("ConfigManager");
error.AddContext("FileName", "config.json");
error.AddContext("Attempt", "1");
```

### Specific Error Types

```cpp
// System errors
Limitless::SystemError systemError("Failed to open system file");
systemError.SetSystemErrorCode(Limitless::ErrorHandling::GetLastSystemError());

// Platform errors
Limitless::PlatformError platformError("Platform-specific operation failed");

// Graphics errors
Limitless::GraphicsError graphicsError("Failed to create graphics context");

// Resource errors
Limitless::ResourceError resourceError("Failed to load texture");

// Configuration errors
Limitless::ConfigError configError("Invalid configuration format");

// Memory errors
Limitless::MemoryError memoryError("Memory allocation failed");

// Thread errors
Limitless::ThreadError threadError("Thread creation failed");
```

### Result Pattern

The `Result<T>` class provides type-safe error handling:

```cpp
// Success result
Limitless::Result<int> successResult(42);
if (successResult.IsSuccess()) {
    int value = successResult.GetValue();
}

// Error result
Limitless::Result<int> errorResult(Limitless::ErrorCode::FileNotFound, "File not found");
if (errorResult.IsFailure()) {
    const auto& error = errorResult.GetError();
    // Handle error
}

// Safe value access
if (int* value = successResult.GetValuePtr()) {
    // Use value
}

// Value or default
int value = errorResult.GetValueOr(0);

// Try wrapper
auto result = Limitless::ErrorHandling::Try([]() -> int {
    // Potentially throwing operation
    return 123;
});

if (result.IsSuccess()) {
    int value = result.GetValue();
}
```

### Error Handling Utilities

```cpp
// Assertions and verifications
Limitless::ErrorHandling::Assert(condition, "Assertion failed");
Limitless::ErrorHandling::Verify(condition, "Verification failed");

// Error code utilities
std::string codeString = Limitless::ErrorHandling::GetErrorCodeString(Limitless::ErrorCode::FileNotFound);
std::string description = Limitless::ErrorHandling::GetErrorCodeDescription(Limitless::ErrorCode::FileNotFound);
Limitless::ErrorSeverity severity = Limitless::ErrorHandling::GetErrorCodeSeverity(Limitless::ErrorCode::FileNotFound);

// System error utilities
int lastError = Limitless::ErrorHandling::GetLastSystemError();
std::string errorString = Limitless::ErrorHandling::GetSystemErrorString(lastError);
Limitless::ErrorCode convertedError = Limitless::ErrorHandling::ConvertSystemError(lastError);
```

### Error Logging

```cpp
// Log error with appropriate severity
Limitless::Error::LogError(error);

// Get detailed error information
std::string details = error.ToDetailedString();
std::cout << details << std::endl;

// Custom error handler
Limitless::ErrorHandling::SetErrorHandler([](const Limitless::Error& error) {
    if (error.IsCritical()) {
        // Handle critical errors
        Limitless::PlatformUtils::BreakIntoDebugger();
    }
    Limitless::Error::LogError(error);
});
```

## Integration Examples

### Platform-Aware Error Handling

```cpp
Limitless::Result<std::string> LoadPlatformSpecificFile(const std::string& filename)
{
    try {
        std::string path;
        
        if (Limitless::PlatformDetection::IsWindows()) {
            path = Limitless::PlatformUtils::JoinPath(
                Limitless::PlatformDetection::GetSystemPath(), 
                filename
            );
        } else if (Limitless::PlatformDetection::IsMacOS()) {
            path = Limitless::PlatformUtils::JoinPath(
                Limitless::PlatformDetection::GetUserDataPath(), 
                filename
            );
        } else {
            path = Limitless::PlatformUtils::JoinPath(
                Limitless::PlatformDetection::GetWorkingDirectory(), 
                filename
            );
        }
        
        // Attempt to load file
        std::ifstream file(path);
        if (!file.is_open()) {
            Limitless::SystemError error("Failed to open file: " + path);
            error.SetSystemErrorCode(Limitless::ErrorHandling::GetLastSystemError());
            error.AddContext("Platform", Limitless::PlatformDetection::GetPlatformString());
            error.AddContext("Path", path);
            return Limitless::Result<std::string>(error);
        }
        
        std::string content((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
        return Limitless::Result<std::string>(content);
        
    } catch (const std::exception& e) {
        return Limitless::Result<std::string>(
            Limitless::ErrorCode::SystemError, 
            "Exception during file loading: " + std::string(e.what())
        );
    }
}
```

### Capability-Based Feature Selection

```cpp
class GraphicsRenderer
{
public:
    bool Initialize()
    {
        // Check platform capabilities
        if (Limitless::PlatformDetection::IsWindows()) {
            if (Limitless::PlatformDetection::HasDirectX()) {
                return InitializeDirectX();
            } else if (Limitless::PlatformDetection::HasVulkan()) {
                return InitializeVulkan();
            } else if (Limitless::PlatformDetection::HasOpenGL()) {
                return InitializeOpenGL();
            }
        } else if (Limitless::PlatformDetection::IsMacOS()) {
            if (Limitless::PlatformDetection::HasMetal()) {
                return InitializeMetal();
            } else if (Limitless::PlatformDetection::HasOpenGL()) {
                return InitializeOpenGL();
            }
        } else if (Limitless::PlatformDetection::IsLinux()) {
            if (Limitless::PlatformDetection::HasVulkan()) {
                return InitializeVulkan();
            } else if (Limitless::PlatformDetection::HasOpenGL()) {
                return InitializeOpenGL();
            }
        }
        
        throw Limitless::PlatformError("No supported graphics API found on this platform");
    }
    
private:
    bool InitializeDirectX() { /* ... */ }
    bool InitializeVulkan() { /* ... */ }
    bool InitializeOpenGL() { /* ... */ }
    bool InitializeMetal() { /* ... */ }
};
```

### Performance-Optimized Code Paths

```cpp
class MathLibrary
{
public:
    void VectorMultiply(const float* a, const float* b, float* result, size_t count)
    {
        if (Limitless::PlatformDetection::HasAVX512()) {
            VectorMultiplyAVX512(a, b, result, count);
        } else if (Limitless::PlatformDetection::HasAVX2()) {
            VectorMultiplyAVX2(a, b, result, count);
        } else if (Limitless::PlatformDetection::HasAVX()) {
            VectorMultiplyAVX(a, b, result, count);
        } else if (Limitless::PlatformDetection::HasSSE4_2()) {
            VectorMultiplySSE42(a, b, result, count);
        } else if (Limitless::PlatformDetection::HasSSE2()) {
            VectorMultiplySSE2(a, b, result, count);
        } else {
            VectorMultiplyScalar(a, b, result, count);
        }
    }
    
private:
    void VectorMultiplyAVX512(const float* a, const float* b, float* result, size_t count);
    void VectorMultiplyAVX2(const float* a, const float* b, float* result, size_t count);
    void VectorMultiplyAVX(const float* a, const float* b, float* result, size_t count);
    void VectorMultiplySSE42(const float* a, const float* b, float* result, size_t count);
    void VectorMultiplySSE2(const float* a, const float* b, float* result, size_t count);
    void VectorMultiplyScalar(const float* a, const float* b, float* result, size_t count);
};
```

## Best Practices

### Platform Detection

1. **Initialize Early**: Call `PlatformDetection::Initialize()` as early as possible in your application
2. **Use Compile-Time Checks**: Prefer compile-time macros for platform-specific code paths
3. **Check Capabilities**: Always verify system capabilities before using platform-specific features
4. **Graceful Degradation**: Provide fallback implementations for missing capabilities
5. **Cache Results**: Store platform information to avoid repeated detection calls

### Error Handling

1. **Use Appropriate Severity**: Choose the right severity level for your errors
2. **Provide Context**: Always add relevant context information to errors
3. **Use Result Pattern**: Prefer `Result<T>` over exceptions for expected error conditions
4. **Handle System Errors**: Always capture and convert system error codes
5. **Log Appropriately**: Use the logging system for error reporting
6. **Fail Fast**: Use assertions for programming errors, verifications for runtime checks

### Error Categories

1. **System Errors**: Use for OS-level errors (file I/O, network, etc.)
2. **Platform Errors**: Use for platform-specific issues
3. **Resource Errors**: Use for resource loading/management issues
4. **Configuration Errors**: Use for configuration parsing/validation issues
5. **Memory Errors**: Use for memory allocation/deallocation issues
6. **Thread Errors**: Use for threading-related issues

### Performance Considerations

1. **Lazy Initialization**: Platform detection is initialized once and cached
2. **Minimal Overhead**: Error creation has minimal runtime overhead
3. **Efficient Logging**: Error logging is optimized and can be disabled in release builds
4. **Memory Safety**: All error objects use RAII and are exception-safe

## API Reference

### PlatformDetection

#### Static Methods

- `static void Initialize()` - Initialize platform detection
- `static const PlatformInfo& GetPlatformInfo()` - Get platform information
- `static void RefreshCapabilities()` - Refresh system capabilities

#### Platform Checks

- `static bool IsWindows()` - Check if running on Windows
- `static bool IsMacOS()` - Check if running on macOS
- `static bool IsLinux()` - Check if running on Linux
- `static bool IsAndroid()` - Check if running on Android
- `static bool IsIOS()` - Check if running on iOS
- `static bool IsWeb()` - Check if running on Web

#### Architecture Checks

- `static bool IsX86()` - Check if x86 architecture
- `static bool IsX64()` - Check if x64 architecture
- `static bool IsARM32()` - Check if ARM32 architecture
- `static bool IsARM64()` - Check if ARM64 architecture
- `static bool IsRISC_V()` - Check if RISC-V architecture

#### Compiler Checks

- `static bool IsMSVC()` - Check if MSVC compiler
- `static bool IsGCC()` - Check if GCC compiler
- `static bool IsClang()` - Check if Clang compiler
- `static bool IsAppleClang()` - Check if AppleClang compiler

#### System Capabilities

- `static bool HasSSE2()` - Check for SSE2 support
- `static bool HasSSE3()` - Check for SSE3 support
- `static bool HasSSE4_1()` - Check for SSE4.1 support
- `static bool HasSSE4_2()` - Check for SSE4.2 support
- `static bool HasAVX()` - Check for AVX support
- `static bool HasAVX2()` - Check for AVX2 support
- `static bool HasAVX512()` - Check for AVX512 support
- `static bool HasNEON()` - Check for NEON support
- `static bool HasAltiVec()` - Check for AltiVec support

#### Graphics API Checks

- `static bool HasOpenGL()` - Check for OpenGL support
- `static bool HasVulkan()` - Check for Vulkan support
- `static bool HasMetal()` - Check for Metal support
- `static bool HasDirectX()` - Check for DirectX support

#### System Information

- `static uint32_t GetCPUCount()` - Get CPU core count
- `static uint64_t GetTotalMemory()` - Get total system memory
- `static uint64_t GetAvailableMemory()` - Get available system memory

#### Path Utilities

- `static std::string GetExecutablePath()` - Get executable path
- `static std::string GetWorkingDirectory()` - Get working directory
- `static std::string GetUserDataPath()` - Get user data path
- `static std::string GetTempPath()` - Get temporary directory path
- `static std::string GetSystemPath()` - Get system directory path

### PlatformUtils

#### File System Utilities

- `GetPathSeparator()` - Get platform path separator
- `NormalizePath(const std::string& path)` - Normalize path
- `JoinPath(const std::string& path1, const std::string& path2)` - Join paths
- `GetDirectoryName(const std::string& path)` - Get directory name
- `GetFileName(const std::string& path)` - Get file name
- `GetFileExtension(const std::string& path)` - Get file extension

#### Environment Utilities

- `GetEnvironmentVariable(const std::string& name)` - Get environment variable
- `SetEnvironmentVariable(const std::string& name, const std::string& value)` - Set environment variable

#### Process Utilities

- `GetCurrentProcessId()` - Get current process ID
- `GetCurrentThreadId()` - Get current thread ID
- `Sleep(uint32_t milliseconds)` - Sleep for specified milliseconds

#### System Utilities

- `LoadLibrary(const std::string& path)` - Load dynamic library
- `GetProcAddress(void* library, const std::string& name)` - Get function address
- `FreeLibrary(void* library)` - Free dynamic library

#### Memory Utilities

- `AllocateAligned(size_t size, size_t alignment)` - Allocate aligned memory
- `FreeAligned(void* ptr)` - Free aligned memory

#### Time Utilities

- `GetHighResolutionTime()` - Get high-resolution time
- `GetSystemTime()` - Get system time

#### Console Utilities

- `SetConsoleColor(uint32_t color)` - Set console color
- `ResetConsoleColor()` - Reset console color
- `IsConsoleAvailable()` - Check if console is available

#### Debug Utilities

- `BreakIntoDebugger()` - Break into debugger
- `OutputDebugString(const std::string& message)` - Output debug string

### Error Handling

#### Error Class

- `Error(ErrorCode code, const std::string& message, const std::source_location& location, ErrorSeverity severity)` - Constructor
- `GetCode()` - Get error code
- `GetMessage()` - Get error message
- `GetLocation()` - Get error location
- `GetSeverity()` - Get error severity
- `GetContext()` - Get error context
- `IsSuccess()` - Check if error is success
- `IsFailure()` - Check if error is failure
- `IsCritical()` - Check if error is critical
- `IsFatal()` - Check if error is fatal
- `ToString()` - Convert to string
- `ToDetailedString()` - Convert to detailed string
- `LogError(const Error& error)` - Log error

#### Context Methods

- `AddContext(const std::string& key, const std::string& value)` - Add context
- `SetFunctionName(const std::string& functionName)` - Set function name
- `SetClassName(const std::string& className)` - Set class name
- `SetModuleName(const std::string& moduleName)` - Set module name
- `SetPlatformInfo(const PlatformInfo& platformInfo)` - Set platform info
- `SetSystemErrorCode(int systemErrorCode)` - Set system error code
- `GetSystemErrorCode()` - Get system error code

#### Result Class

- `Result(const T& value)` - Success constructor
- `Result(const Error& error)` - Error constructor
- `IsSuccess()` - Check if successful
- `IsFailure()` - Check if failed
- `GetValue()` - Get value (throws if error)
- `GetError()` - Get error
- `GetValuePtr()` - Get value pointer (safe)
- `GetValueOr(const T& defaultValue)` - Get value or default
- `GetValueOrThrow()` - Get value or throw

#### ErrorHandling Namespace

- `SetErrorHandler(ErrorHandler handler)` - Set error handler
- `GetErrorHandler()` - Get error handler
- `DefaultErrorHandler(const Error& error)` - Default error handler
- `Assert(bool condition, const std::string& message, const std::source_location& location)` - Assert
- `Verify(bool condition, const std::string& message, const std::source_location& location)` - Verify
- `Try(Func&& func)` - Try wrapper
- `GetErrorCodeString(ErrorCode code)` - Get error code string
- `GetErrorCodeDescription(ErrorCode code)` - Get error code description
- `GetErrorCodeSeverity(ErrorCode code)` - Get error code severity
- `GetLastSystemError()` - Get last system error
- `GetSystemErrorString(int errorCode)` - Get system error string
- `ConvertSystemError(int systemErrorCode)` - Convert system error

### Macros

#### Platform Macros

- `LT_PLATFORM_WINDOWS` - Windows platform
- `LT_PLATFORM_MACOS` - macOS platform
- `LT_PLATFORM_LINUX` - Linux platform
- `LT_PLATFORM_NAME` - Platform name string

#### Architecture Macros

- `LT_ARCHITECTURE_X64` - x64 architecture
- `LT_ARCHITECTURE_X86` - x86 architecture
- `LT_ARCHITECTURE_ARM64` - ARM64 architecture
- `LT_ARCHITECTURE_ARM32` - ARM32 architecture
- `LT_ARCHITECTURE_NAME` - Architecture name string

#### Compiler Macros

- `LT_COMPILER_MSVC` - MSVC compiler
- `LT_COMPILER_GCC` - GCC compiler
- `LT_COMPILER_CLANG` - Clang compiler
- `LT_COMPILER_APPLE_CLANG` - AppleClang compiler
- `LT_COMPILER_NAME` - Compiler name string

#### Error Macros

- `LT_ASSERT(condition, message)` - Assert macro
- `LT_VERIFY(condition, message)` - Verify macro
- `LT_THROW_ERROR(code, message)` - Throw error macro
- `LT_THROW_SYSTEM_ERROR(message)` - Throw system error macro
- `LT_THROW_PLATFORM_ERROR(message)` - Throw platform error macro
- `LT_THROW_GRAPHICS_ERROR(message)` - Throw graphics error macro
- `LT_THROW_RESOURCE_ERROR(message)` - Throw resource error macro
- `LT_THROW_CONFIG_ERROR(message)` - Throw config error macro
- `LT_THROW_MEMORY_ERROR(message)` - Throw memory error macro
- `LT_THROW_THREAD_ERROR(message)` - Throw thread error macro
- `LT_TRY(expr)` - Try wrapper macro
- `LT_TRY_VOID(expr)` - Try wrapper macro for void
- `LT_RETURN_IF_ERROR(result)` - Return if error macro
- `LT_RETURN_IF_ERROR_VOID(result)` - Return if error macro for void

This comprehensive system provides robust platform detection and error handling capabilities that integrate seamlessly with the existing engine architecture. 