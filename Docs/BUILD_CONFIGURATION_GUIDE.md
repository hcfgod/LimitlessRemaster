# Build Configuration Guide

This guide covers the build system configuration, platform-specific settings, and C++20 coroutine support in the Limitless Engine.

## Table of Contents

1. [Build System Overview](#build-system-overview)
2. [Platform-Specific Build Options](#platform-specific-build-options)
3. [C++20 Coroutine Support](#c20-coroutine-support)
4. [Build Configurations](#build-configurations)
5. [Cross-Platform Building](#cross-platform-building)
6. [Application Icon Setup](#application-icon-setup)
7. [Best Practices](#best-practices)

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
├── premake5.lua              # Main workspace (start project: Editor)
├── Limitless/premake5.lua    # Core engine library
├── Editor/premake5.lua      # Unity-style editor (default start project)
├── Runtime/premake5.lua      # Example runtime application
├── ScriptCore/premake5.lua   # Native C++ script DLL (loaded by Editor)
├── Test/premake5.lua         # Unit tests
└── Scripts/                  # Build scripts
    ├── build-windows.bat     # Windows: bootstrap Premake, generate solution, build, run tests
    ├── build-unix.sh         # Unix/Linux/macOS: dependencies, Premake, make, tests
    ├── BootstrapPremake.bat  # Download Premake5 into Vendor/Premake (Windows)
    ├── build-scriptcore-windows.bat  # Windows ScriptCore-only build
    └── build-scriptcore-unix.sh      # Linux/macOS ScriptCore-only build
```
Output directory: `Build/<Config>-<system>-<platform>/` (e.g. `Build/Debug-windows-x64/`).

## Platform-Specific Build Options

### Windows (MSVC)

**Build Options:**
```lua
filter "system:windows"
    cppdialect "C++20"
    staticruntime "Off"
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
- **Coroutine Support**: C++20 coroutines are available when compiling in C++20 mode (no special MSVC flag required for standard C++20 coroutines).
- **Runtime Library**: `staticruntime "Off"` (dynamic runtime). Binaries are smaller and link consistently across projects; you may need the MSVC runtime redistributable on target machines.
- **Latest SDK**: Uses the latest Windows SDK

### macOS (GCC/Clang)

**Build Options:**
```lua
filter "system:macosx"
    cppdialect "C++20"
    staticruntime "Off"
    
    buildoptions
    {
        "-std=c++20",
    }
```

**Key Features:**
- **C++20 Standard**: Full C++20 language support
- **Coroutine Support**: Standard C++20 coroutines are available in C++20 mode on modern Clang.
- **Framework Integration**: Native macOS framework support
- **ARM64 Support**: Native Apple Silicon support

### Linux (GCC/Clang)

**Build Options:**
```lua
filter "system:linux"
    cppdialect "C++20"
    staticruntime "Off"
    
    buildoptions
    {
        "-std=c++20",
    }
```

**Key Features:**
- **C++20 Standard**: Full C++20 language support
- **Coroutine Support**: Standard C++20 coroutines are available in C++20 mode on modern GCC/Clang.
- **System Libraries**: Native Linux library integration
- **Multi-architecture**: x64 and ARM64 support

## C++20 Coroutine Status (Engine Reality)

The project is compiled in **C++20 mode**, so client code may use standard C++20 coroutines where supported by the chosen compiler.

However:

- The engine’s current async primitives (`Limitless::Async::Task<T>`) are **future-backed** and are **not** coroutine-awaitable by default.
- If you want coroutine-friendly tasks, you’ll need either:
  - an awaiter wrapper around `std::shared_future`, or
  - a dedicated coroutine `Task` type (future work).

## Render Thread (OpenGL)

The engine supports a dedicated render thread for OpenGL execution/present.

- **Config key**: `graphics.render_thread_enabled` (boolean)
- **Default**: `true`
- **Behavior**:
  - Main thread builds/submits render commands (MPMC submission).
  - `Renderer::EndFrame()` signals the render thread that a frame is ready.
  - `Renderer::SwapBuffers()` waits until the render thread completes the frame (process commands + present).

This improves correctness and simplifies thread-affinity rules for OpenGL contexts. It does not make OpenGL GPU execution parallel; it ensures context ownership is explicit and safe.

### GPU Resource Operations (OpenGL)

When the render thread is enabled, GPU resource operations are executed on the render thread via an internal resource queue.

- **Examples**:
  - Shader compile/link
  - Buffer/texture creation
  - VAO attribute setup
  - OpenGL deletes during teardown
- **Behavior**: these operations may block the calling thread briefly (they are submitted and waited on).

### GPU Resource Lifetime Rule (Must-have)

**Rule**: GPU resources must be destroyed **before** `Renderer::Shutdown()` tears down the graphics context.

- The engine attempts to delete OpenGL objects on the render thread via the resource queue.
- If a GPU resource is destroyed *after* the renderer/context are already gone, the engine will **not** call `glDelete*` (unsafe) and will instead **warn and leak** the GL handle.

This is a deliberate correctness choice: leaking during shutdown is preferable to undefined behavior or driver crashes.

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

## Application Icon Setup

The workspace uses a shared logo file at `Resources/LimitlessLogo.ico`.

- **Windows executable icon metadata**: Embedded through `Resources/LimitlessExecutableIcon.rc` so the generated `.exe` files show the project icon in Explorer.
- **Runtime window icon (Windows/macOS/Linux)**: `window.icon` in `Editor/config.json` and `Runtime/config.json` is set to `LimitlessLogo.ico`.
- **Output layout**: Premake post-build copy rules place `LimitlessLogo.ico` next to each built executable so config-based icon loading works consistently.
- **Packaged game builds**: `GameBuilder` now copies `LimitlessLogo.ico` into the output directory alongside `config.json` and the game executable.

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

1. **Coroutine Support (C++20)**: Ensure you are compiling in **C++20 mode** and your compiler version supports standard coroutines.
   - MSVC: no special `/await` flag is required for standard C++20 coroutines.
   - GCC/Clang: standard C++20 coroutines work in C++20 mode on modern toolchains. (Older toolchains may require extra flags; upgrade if possible.)
2. **C++20 Support**: Verify your compiler supports the C++20 standard library features you use (not just the language mode).
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

This build configuration targets the engine's **implemented platforms today** (Windows/macOS/Linux) while keeping future platform expansion in mind, and provides C++20 build settings across those targets.
