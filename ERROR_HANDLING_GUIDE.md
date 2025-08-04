# Error Handling Guide for Limitless Engine

This guide explains how to use the comprehensive error handling system in the Limitless engine, including assertions, verifications, error throwing, and result-based error handling.

## Table of Contents

1. [Overview](#overview)
2. [Error Class](#error-class)
3. [Assertion and Verification Macros](#assertion-and-verification-macros)
4. [Error Throwing Macros](#error-throwing-macros)
5. [Result Class](#result-class)
6. [Try-Catch Wrappers](#try-catch-wrappers)
7. [Error Propagation](#error-propagation)
8. [Best Practices](#best-practices)
9. [Examples](#examples)

## Overview

The Limitless engine provides a comprehensive error handling system that includes:

- **Error Class**: A robust error representation with context, severity levels, and system integration
- **LT_ASSERT/LT_VERIFY**: Assertion and verification macros for runtime checks
- **LT_THROW_* Macros**: Convenient macros for throwing specific error types
- **Result<T> Class**: A template class for error handling without exceptions
- **LT_TRY Macros**: Wrappers for exception-safe function calls
- **Error Propagation**: Macros for clean error propagation

## Error Class

The `Error` class is the foundation of the error handling system:

```cpp
#include "Core/Error.h"

// Basic error creation
Limitless::Error error(Limitless::ErrorCode::FileNotFound, "File not found", std::source_location::current());

// Error with severity
Limitless::Error criticalError(Limitless::ErrorCode::OutOfMemory, "Memory allocation failed", 
                              std::source_location::current(), Limitless::ErrorSeverity::Critical);

// Adding context
error.SetFunctionName("MyFunction");
error.SetClassName("MyClass");
error.SetModuleName("MyModule");
error.AddContext("file_path", "/path/to/file.txt");
error.AddContext("user_id", "12345");
```

### Error Severity Levels

- `Info`: Informational messages
- `Warning`: Warning conditions
- `Error`: Error conditions (default)
- `Critical`: Critical errors that may affect system stability
- `Fatal`: Fatal errors that require immediate termination

### Error Codes

The engine provides comprehensive error codes for different scenarios:

```cpp
// General errors
Limitless::ErrorCode::Success
Limitless::ErrorCode::InvalidArgument
Limitless::ErrorCode::OutOfMemory
Limitless::ErrorCode::Timeout

// System errors
Limitless::ErrorCode::FileNotFound
Limitless::ErrorCode::FileAccessDenied
Limitless::ErrorCode::NetworkError

// Platform errors
Limitless::ErrorCode::PlatformError
Limitless::ErrorCode::PlatformNotSupported

// Graphics errors
Limitless::ErrorCode::GraphicsError
Limitless::ErrorCode::WindowCreationFailed
Limitless::ErrorCode::ShaderCompilationFailed

// And many more...
```

## Assertion and Verification Macros

### LT_ASSERT

Use `LT_ASSERT` for critical conditions that should never be false in correct code:

```cpp
// Basic assertion
LT_ASSERT(pointer != nullptr, "Pointer cannot be null");

// Complex conditions
LT_ASSERT(value >= 0 && value <= 100, "Value must be between 0 and 100");

// With context
LT_ASSERT(fileSize > 0, "File size must be positive");
```

**When to use**: For programming errors, invariant violations, and conditions that indicate bugs.

### LT_VERIFY

Use `LT_VERIFY` for conditions that should be true but might fail due to external factors:

```cpp
// Basic verification
LT_VERIFY(window != nullptr, "Window must be initialized");

// API call verification
LT_VERIFY(SDL_Init(SDL_INIT_VIDEO) == 0, "SDL initialization failed");

// State verification
LT_VERIFY(m_Initialized, "Component must be initialized before use");
```

**When to use**: For runtime conditions, API call results, and state validation.

## Error Throwing Macros

The engine provides convenient macros for throwing specific error types:

### LT_THROW_ERROR

```cpp
LT_THROW_ERROR(Limitless::ErrorCode::InvalidArgument, "Invalid parameter provided");
LT_THROW_ERROR(Limitless::ErrorCode::Timeout, "Operation timed out");
```

### Specific Error Type Macros

```cpp
// System errors
LT_THROW_SYSTEM_ERROR("Failed to open system file");

// Platform errors
LT_THROW_PLATFORM_ERROR("Platform-specific operation failed");

// Graphics errors
LT_THROW_GRAPHICS_ERROR("Failed to create graphics context");

// Resource errors
LT_THROW_RESOURCE_ERROR("Failed to load texture");

// Configuration errors
LT_THROW_CONFIG_ERROR("Invalid configuration format");

// Memory errors
LT_THROW_MEMORY_ERROR("Memory allocation failed");

// Thread errors
LT_THROW_THREAD_ERROR("Thread creation failed");
```

## Result Class

The `Result<T>` class provides exception-safe error handling:

### Basic Usage

```cpp
#include "Core/Error.h"

// Success case
Limitless::Result<int> successResult(42);
if (successResult.IsSuccess()) {
    int value = successResult.GetValue();
}

// Error case
Limitless::Result<int> errorResult(Limitless::ErrorCode::FileNotFound, "File not found");
if (errorResult.IsFailure()) {
    const auto& error = errorResult.GetError();
    // Handle error
}
```

### Advanced Usage

```cpp
// Safe value access
if (int* value = result.GetValuePtr()) {
    // Use *value safely
}

// Value or default
int value = result.GetValueOr(0);

// Value or throw
try {
    int value = result.GetValueOrThrow();
} catch (const Limitless::Error& error) {
    // Handle error
}
```

### Result with Complex Types

```cpp
struct MyStruct {
    int value;
    std::string name;
};

Limitless::Result<MyStruct> structResult(MyStruct{42, "test"});
if (structResult.IsSuccess()) {
    const auto& data = structResult.GetValue();
    // Use data.value and data.name
}
```

## Try-Catch Wrappers

### LT_TRY

Use `LT_TRY` to wrap functions that might throw exceptions:

```cpp
// Basic usage
auto result = LT_TRY(riskyFunction());

// With lambda
auto result = LT_TRY([]() -> int {
    // Risky operation
    return 42;
}());

// Error handling
auto result = LT_TRY(riskyFunction());
if (result.IsFailure()) {
    const auto& error = result.GetError();
    // Handle error
}
```

### LT_TRY_VOID

For functions that return void:

```cpp
auto result = LT_TRY_VOID(riskyVoidFunction());
if (result.IsFailure()) {
    // Handle error
}
```

## Error Propagation

### LT_RETURN_IF_ERROR

Use `LT_RETURN_IF_ERROR` for clean error propagation:

```cpp
Limitless::Result<int> level3Function(bool fail) {
    if (fail) {
        return Limitless::Result<int>(Limitless::ErrorCode::FileNotFound, "File not found");
    }
    return Limitless::Result<int>(3);
}

Limitless::Result<int> level2Function(bool fail) {
    auto result = level3Function(fail);
    LT_RETURN_IF_ERROR(result);  // Returns early if error
    return Limitless::Result<int>(result.GetValue() + 2);
}

Limitless::Result<int> level1Function(bool fail) {
    auto result = level2Function(fail);
    LT_RETURN_IF_ERROR(result);  // Returns early if error
    return Limitless::Result<int>(result.GetValue() + 1);
}
```

### LT_RETURN_IF_ERROR_VOID

For void functions:

```cpp
Limitless::Result<void> myFunction() {
    auto result = someOperation();
    LT_RETURN_IF_ERROR_VOID(result);  // Returns early if error
    // Continue with success case
    return Limitless::Result<void>();
}
```

## Best Practices

### 1. Choose the Right Tool

- **LT_ASSERT**: For programming errors and invariants
- **LT_VERIFY**: For runtime conditions and API calls
- **LT_THROW_***: For exceptional conditions that require immediate handling
- **Result<T>**: For functions that can fail as part of normal operation

### 2. Provide Context

Always add relevant context to errors:

```cpp
Limitless::Error error(Limitless::ErrorCode::FileNotFound, "File not found", std::source_location::current());
error.SetFunctionName("LoadTexture");
error.SetClassName("TextureManager");
error.AddContext("file_path", filePath);
error.AddContext("texture_id", std::to_string(textureId));
```

### 3. Use Appropriate Severity

```cpp
// Info: Normal operation information
Limitless::Error(Limitless::ErrorCode::Cancelled, "Operation cancelled", 
                std::source_location::current(), Limitless::ErrorSeverity::Info);

// Warning: Something unexpected but recoverable
Limitless::Error(Limitless::ErrorCode::FileExists, "File already exists", 
                std::source_location::current(), Limitless::ErrorSeverity::Warning);

// Error: Something went wrong (default)
Limitless::Error(Limitless::ErrorCode::FileNotFound, "File not found", 
                std::source_location::current(), Limitless::ErrorSeverity::Error);

// Critical: System stability at risk
Limitless::Error(Limitless::ErrorCode::OutOfMemory, "Memory allocation failed", 
                std::source_location::current(), Limitless::ErrorSeverity::Critical);

// Fatal: Immediate termination required
Limitless::Error(Limitless::ErrorCode::MemoryCorruption, "Memory corruption detected", 
                std::source_location::current(), Limitless::ErrorSeverity::Fatal);
```

### 4. Handle Errors Appropriately

```cpp
// For critical errors, log and potentially terminate
if (error.IsCritical()) {
    Limitless::Error::LogError(error);
    // Consider terminating or taking drastic action
}

// For fatal errors, log and terminate
if (error.IsFatal()) {
    Limitless::Error::LogError(error);
    std::terminate();  // Or your preferred termination method
}
```

### 5. Use Result for Public APIs

```cpp
// Good: Public API returns Result
Limitless::Result<Texture> LoadTexture(const std::string& path);

// Usage
auto textureResult = LoadTexture("texture.png");
if (textureResult.IsFailure()) {
    // Handle error gracefully
    return;
}
auto texture = textureResult.GetValue();
```

## Examples

### Example 1: Window Creation

```cpp
void SDLWindow::Init(const WindowProps& props)
{
    // Validate input parameters
    LT_VERIFY(!props.Title.empty(), "Window title cannot be empty");
    LT_VERIFY(props.Width > 0, "Window width must be greater than 0");
    LT_VERIFY(props.Height > 0, "Window height must be greater than 0");
    
    // Create window
    m_Window = SDL_CreateWindow(props.Title.c_str(), props.Width, props.Height, windowFlags);
    
    if (!m_Window) {
        std::string errorMsg = fmt::format("SDL_CreateWindow failed: {}", SDL_GetError());
        GraphicsError error(errorMsg, std::source_location::current());
        error.SetFunctionName("SDLWindow::Init");
        error.SetClassName("SDLWindow");
        error.AddContext("title", props.Title);
        error.AddContext("width", std::to_string(props.Width));
        error.AddContext("height", std::to_string(props.Height));
        
        LT_CORE_ERROR("{}", errorMsg);
        Error::LogError(error);
        LT_THROW_GRAPHICS_ERROR(errorMsg);
    }
}
```

### Example 2: File Loading with Result

```cpp
Limitless::Result<std::vector<uint8_t>> LoadFile(const std::string& path)
{
    LT_VERIFY(!path.empty(), "File path cannot be empty");
    
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        SystemError error("Failed to open file", std::source_location::current());
        error.SetFunctionName("LoadFile");
        error.AddContext("file_path", path);
        error.SetSystemErrorCode(Limitless::ErrorHandling::GetLastSystemError());
        
        return Limitless::Result<std::vector<uint8_t>>(error);
    }
    
    file.seekg(0, std::ios::end);
    auto size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> data(size);
    file.read(reinterpret_cast<char*>(data.data()), size);
    
    if (file.fail()) {
        SystemError error("Failed to read file", std::source_location::current());
        error.SetFunctionName("LoadFile");
        error.AddContext("file_path", path);
        error.AddContext("file_size", std::to_string(size));
        
        return Limitless::Result<std::vector<uint8_t>>(error);
    }
    
    return Limitless::Result<std::vector<uint8_t>>(std::move(data));
}
```

### Example 3: Error Propagation Chain

```cpp
Limitless::Result<Texture> LoadTexture(const std::string& path)
{
    // Load file data
    auto fileData = LoadFile(path);
    LT_RETURN_IF_ERROR(fileData);
    
    // Parse texture format
    auto textureInfo = ParseTextureFormat(fileData.GetValue());
    LT_RETURN_IF_ERROR(textureInfo);
    
    // Create GPU texture
    auto gpuTexture = CreateGPUTexture(textureInfo.GetValue());
    LT_RETURN_IF_ERROR(gpuTexture);
    
    return Limitless::Result<Texture>(gpuTexture.GetValue());
}
```

### Example 4: Platform-Specific Error Handling

```cpp
bool WindowsCPUPlatform::Initialize()
{
    LT_VERIFY(!m_initialized, "Windows CPU Platform already initialized");
    
    // Get CPU core count
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    m_coreCount = sysInfo.dwNumberOfProcessors;
    
    LT_VERIFY(m_coreCount > 0, "Invalid CPU core count");
    
    // Initialize PDH for CPU monitoring
    if (PdhOpenQuery(nullptr, 0, &m_query) != ERROR_SUCCESS) {
        std::string errorMsg = "Failed to initialize Windows CPU Platform - PDH initialization failed";
        PlatformError error(errorMsg, std::source_location::current());
        error.SetFunctionName("WindowsCPUPlatform::Initialize");
        error.SetClassName("WindowsCPUPlatform");
        error.AddContext("core_count", std::to_string(m_coreCount));
        
        LT_CORE_ERROR("{}", errorMsg);
        Error::LogError(error);
        LT_THROW_PLATFORM_ERROR(errorMsg);
    }
    
    m_initialized = true;
    return true;
}
```

## Conclusion

The Limitless engine's error handling system provides comprehensive tools for robust error management. By using the appropriate tools for each situation and following the best practices outlined in this guide, you can create reliable and maintainable code that handles errors gracefully.

Remember:
- Use `LT_ASSERT` for programming errors
- Use `LT_VERIFY` for runtime conditions
- Use `LT_THROW_*` for exceptional conditions
- Use `Result<T>` for functions that can fail normally
- Always provide context and appropriate severity
- Handle errors at the appropriate level in your application 