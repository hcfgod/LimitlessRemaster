# Platform Truth Table (Implemented vs Future)

This document is the **single source of truth** for what the engine actually supports **today** on:

- Windows
- macOS
- Linux

Anything not marked as implemented should be treated as **future work**, even if an API surface exists.

## Status Legend

- **Implemented**: Used by the runtime today.
- **Partially Implemented**: Works, but has known limitations or placeholder behavior.
- **Future**: Planned API surface or stub exists, but not implemented end-to-end.
- **Not Supported**: Not applicable for that platform (or intentionally not supported).

## Notes (how to interpret this)

- This truth table is about **engine code paths**, not about whether a machine has a driver installed.
- “Detection” means **real probing** (for example: loading a loader library, enumerating devices, validating feature levels). A platform-default boolean is **not** treated as detection.
- When a system is SDL-backed (windowing, input), “Implemented” means the engine uses the SDL path across all three platforms.

## Truth Table

| Area | Windows | macOS | Linux | Notes |
|------|---------|-------|-------|------|
| Build generation (Premake) | Implemented | Implemented | Implemented | `premake5.lua` targets Windows / macOS / Linux. |
| Build scripts | Implemented | Implemented | Implemented | Windows: `Scripts/build-windows.bat`. Unix: `Scripts/build-unix.sh`. |
| Continuous Integration workflows | Implemented | Implemented | Implemented | Workflows exist for Windows/Linux/macOS. |
| Core systems (logging, configuration, events, error handling) | Implemented | Implemented | Implemented | Core systems are platform-agnostic C++20. |
| Concurrency (Async I/O thread pool, tasks) | Implemented | Implemented | Implemented | Uses `std::thread`, `std::future`, `std::filesystem`. |
| Hot reload (configuration-driven updates) | Implemented | Implemented | Implemented | File watching is polling-based (portable) and applies window/logging diffs on the main thread. |
| Window layer (SDL-backed) | Implemented | Implemented | Implemented | SDL3-based window implementation with extended window operations. |
| OpenGL context creation | Implemented | Implemented | Implemented | Implemented via SDL + `OpenGLContext` (GLAD loader). |
| Vulkan context creation | Future | Not Supported | Future | Not implemented. On macOS, native Vulkan is not targeted (would require a translation layer). |
| DirectX context creation | Future | Not Supported | Not Supported | Not implemented. Windows-only target when implemented. |
| Metal context creation | Not Supported | Future | Not Supported | Not implemented. macOS-only target when implemented. |
| Graphics API selection framework | Partial | Partial | Partial | Framework exists; real non-OpenGL backends are not implemented yet. |
| Graphics API detection (real probing) | Partial | Partial | Partial | OpenGL details become accurate after context creation; Vulkan/DirectX/Metal probing is future work. |
| Platform detection (runtime info, paths, CPU features) | Implemented | Implemented | Implemented | Implemented in `PlatformDetection` with platform-specific code paths. |
| Performance monitoring (CPU usage) | Implemented | Partial | Implemented | macOS ARM64 uses safe defaults in some environments to avoid instability. |
| Performance monitoring (system memory / process memory) | Implemented | Implemented | Implemented | Platform-specific system backends exist for all three. |
| Performance monitoring (GPU usage, temperature, VRAM) | Future | Future | Future | Stubbed as unavailable; requires external vendor libraries. |

## Explicit “Future” Items (Commonly Asked)

- Vulkan / DirectX / Metal backends (contexts, swapchains, renderer integration)
- Real Vulkan / DirectX / Metal detection (probing + capability enumeration)
- Criteria-based selection (performance / features / stability) and real comparisons
- GPU performance monitoring (vendor library integration)

