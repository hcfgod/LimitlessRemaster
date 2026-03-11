# Build Configuration Guide

This guide covers the current build/export model for the Limitless workspace and editor.

## Build System Overview

Limitless uses **Premake5** to generate platform build files and provides helper scripts for the common workflows.

Current goals:

- Windows, macOS, and Linux desktop support
- multiple compilers/toolchains where applicable
- x64 and ARM64 targets
- source-workspace and install-relative/internal-toolchain build flows
- managed payload staging for C# scripting

## Standard Build Scripts

### Windows

```bat
Scripts\build-windows.bat [Debug|Release|Dist] [x64|ARM64]
```

Typical output examples:

- `Build/debug_x64-windows-x64/`
- `Build/dist_x64-windows-x64/`

### Linux / macOS

```bash
Scripts/build-unix.sh --config Debug --compiler gcc
Scripts/build-unix.sh --config Release --compiler clang
```

Typical output examples:

- `Build/debug_x64-linux-x64/`
- `Build/debug_arm64-macosx-ARM64/`

## Premake Direct

Advanced users can still generate directly:

```bash
Vendor/Premake/premake5 vs2022
Vendor/Premake/premake5 gmake2
```

## Build Configurations

### `Debug`

- full symbols
- minimal optimization
- highest logging/detail level

### `Release`

- optimized build
- reduced logging

### `Dist`

- shipping-oriented build
- release runtime
- minimized logging
- used by the editor/game export pipeline as the shipped build configuration

Current editor/export reality:

- the Build Settings panel currently forces `BuildConfiguration` to `Dist`
- load/save sanitization in `BuildSettings` also normalizes the stored build configuration to `Dist`
- `Debug` and `Release` still exist for direct workspace/toolchain builds, but the editor's shipped game build flow currently exports `Dist`

## Render Thread / OpenGL Resource Model

The production renderer is OpenGL-based and supports a dedicated render thread.

Important runtime rules:

- render commands are submitted from the main thread
- GPU resource work may execute on the render/resource thread
- GPU resources must be destroyed before renderer shutdown tears down the context

Leaking a resource during shutdown is preferred over calling unsafe `glDelete*` after the context is gone.

## Editor Build Settings Model

Per-project export settings live in:

- `Project/Settings/BuildSettings.json`

The build settings file currently includes:

- ordered build-scene list
- persisted settings version
- build configuration
- build backend
- target OS / architecture
- execution mode
- remote build settings
- compression settings
- last output directory
- game icon override
- engine root override field
- script editor mode
- native script compile failure policy

The first enabled build scene is treated as the startup scene.

Current editor behavior:

- the editor auto-detects the engine workspace root or internal toolchain root at build time
- `EngineRootOverride` exists in the schema, but the current Build Settings panel clears persisted manual overrides and relies on auto-detection
- the panel may auto-resolve the backend to `InternalToolchain` when an internal toolchain layout is detected without a matching legacy workspace root

## Build Backends

Current backend options:

- `LegacySdk`
  - source-workspace oriented
  - uses the existing engine workspace/build scripts directly

- `InternalToolchain`
  - install-relative/toolchain-root oriented
  - intended for packaged/internal distribution workflows

The editor may normalize a legacy selection into `InternalToolchain` when running from an internal-toolchain install layout.

## Execution Modes

Current editor build execution modes:

- `Auto`
- `Local`
- `Remote`

Current `Auto` behavior:

- host/target match -> local build
- Windows host + Linux target + WSL ready -> local Linux build through WSL
- otherwise -> remote build when configured

On Windows, the Build Settings panel also exposes a WSL setup/status helper for the local Windows->Linux path.

## Remote Build Flow

Remote builds use:

- `Scripts/remote_build_worker.py`
- `Scripts/remote_build_client.py`

The editor can route requests through:

- fallback endpoint
- target-specific endpoints
- optional auth token / pool labels / retry settings

For API details see:

- `Docs/REMOTE_BUILD_API_GUIDE.md`

## Managed C# Payload Staging

Managed scripting is now part of the build/staging model.

Primary helper scripts:

- Windows: `Scripts/build-managed-runtime-windows.bat`
- Unix: `Scripts/build-managed-runtime-unix.sh`

These scripts currently:

- validate `dotnet` availability
- build/publish Coral-managed runtime pieces
- build `Managed/Limitless.Managed`
- build `Managed/Limitless.Managed.TestScripts`
- optionally generate and build a project-authored managed scripts `.csproj`
- stage everything into a `Managed/` payload directory
- emit `Limitless.Managed.payload.json`
- reuse a managed runtime cache
- coordinate concurrent builds with scoped lock directories

The resulting payload is copied beside editor/runtime/shipping outputs as:

- `<Output>/Managed/`

## Managed Payload Manifest

The managed payload manifest is:

- `Managed/Limitless.Managed.payload.json`

It records:

- payload format version
- host API version
- Coral managed assembly/runtimeconfig names
- `Limitless.Managed` contract assembly/runtimeconfig names
- discovered script assembly list
- target OS / architecture / build configuration

## Application Icon Packaging

Shared icon assets:

- `Resources/LimitlessExecutableIcon.rc`
- `Resources/LimitlessLogo.ico`

Current behavior:

- the build request resolves `GameWindowIconPath` from either an absolute path or a project-relative path
- the shipped runtime config writes `window.icon` to the copied icon filename in the output directory
- packaged game builds copy either the configured icon or the default runtime `LimitlessLogo.ico` into the output directory
- on Windows, configured executable icon embedding requires `.ico` data; if the configured runtime icon is not `.ico`, a same-stem companion `.ico` file is required for executable metadata embedding

## Troubleshooting

Common build issues:

- missing platform SDK/toolchain
- missing `dotnet` SDK for managed payload builds
- missing WSL for local Windows->Linux builds
- configured game icon path missing or invalid
- Windows executable icon embedding requested with a non-`.ico` icon and no companion `.ico`
- missing FFmpeg binaries when audio decoding support is enabled
- stale ScriptCore or managed payload after bridge/API changes

## Related Files

- `Limitless/Source/Project/BuildSettings.{h,cpp}`
- `Limitless/Source/Project/GameBuilder.{h,cpp}`
- `Scripts/build-windows.bat`
- `Scripts/build-unix.sh`
- `Scripts/build-managed-runtime-windows.bat`
- `Scripts/build-managed-runtime-unix.sh`
