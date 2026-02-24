# LimitlessRemaster

LimitlessRemaster is a C++20 game engine and editor focused on 2D workflows.  
It includes a Unity-style editor, Scene/ECS runtime (EnTT), Renderer2D on OpenGL, asset import/versioning, native C++ scripting (ScriptCore), audio, input actions, and cross-platform build/test pipelines.

## Current Status

- Editor-first workflow with Play/Simulate, scene editing, inspector, hierarchy, project panel, and diagnostics.
- Rendering backend in production is OpenGL.
- 2D gameplay stack is implemented: scene/ECS, sprites, tilemaps, animation, UI, audio, input actions, and physics.
- Physics2D supports multiple worlds per scene (`WorldCount` + per-body `PhysicsWorldSlot`).
- Asset pipeline supports `.meta` GUIDs, import/reimport, hot reload, and serialization version migration.
- Native script runtime is C++ via ScriptCore DLL loading and host bridge callbacks.

## Build

Use the provided scripts unless you specifically need direct Premake/MSBuild/Make invocation.

### Windows

```bat
Scripts\build-windows.bat [Debug|Release|Dist] [x64|ARM64]
```

Examples:

```bat
Scripts\build-windows.bat Debug x64
Scripts\build-windows.bat Dist x64
```

Typical output directory format:

```text
Build/<cfg.shortname>-windows-<platform>/
```

Examples:

- `Build/debug_x64-windows-x64/`
- `Build/dist_x64-windows-x64/`

### Linux / macOS

```bash
Scripts/build-unix.sh --config Debug --compiler gcc
Scripts/build-unix.sh --config Release --compiler clang
```

Typical output directory format:

```text
Build/<cfg.shortname>-<system>-<platform>/
```

Examples:

- `Build/debug_x64-linux-x64/`
- `Build/debug_arm64-macosx-ARM64/`

The Unix build script checks dependencies and can install/build missing packages (SDL3, Box2D, and related system libs) based on platform/package manager availability.

### Premake Direct (Advanced)

```bash
# From repo root
Vendor/Premake/premake5 vs2022   # Windows solution
Vendor/Premake/premake5 gmake2   # Linux/macOS makefiles
```

## Run

### Editor

Start project in Premake/workspace is `Editor`.

- Windows: `Build/debug_x64-windows-x64/Editor/Editor.exe`
- Linux: `./Build/debug_x64-linux-x64/Editor/Editor`
- macOS: `./Build/debug_x64-macosx-x64/Editor/Editor` (or ARM64 folder on Apple Silicon)

### Runtime Sample

- Windows: `Build/debug_x64-windows-x64/Runtime/Runtime.exe`
- Linux/macOS: `./Build/debug_x64-<system>-x64/Runtime/Runtime`

### Tests

- Windows: `Build\debug_x64-windows-x64\Test\Test.exe --success`
- Linux/macOS: `./Build/debug_x64-<system>-x64/Test/Test --success`

## ScriptCore Workflow

- `ScriptCore` builds as a shared library and is output beside the Editor binary.
- The editor also supports project-side ScriptCore loading for gameplay scripts compiled in the target project.
- If component layouts or script bridge APIs change, rebuild ScriptCore to avoid ABI/layout mismatch issues.

## Documentation Map

### Core

- `Docs/CONFIGURATION_GUIDE.md`
- `Docs/EVENT_GUIDE.md`
- `Docs/ERROR_HANDLING_GUIDE.md`
- `Docs/LOGGING_GUIDE.md`
- `Docs/CONCURRENCY_GUIDE.md`
- `Docs/TIME_GUIDE.md`
- `Docs/HOT_RELOAD_GUIDE.md`

### Graphics and Rendering

- `Docs/README_RenderCommandSystem.md`
- `Docs/RENDERER2D_GUIDE.md`
- `Docs/RENDERING_ROADMAP.md`
- `Docs/GRAPHICS_API_DETECTION_GUIDE.md`
- `Docs/LIGHTING2D_SYSTEM_GUIDE.md`
- `Docs/FRAMEBUFFER_GUIDE.md`

### Scene, Physics, and Gameplay

- `Docs/SCENE_ECS_GUIDE.md`
- `Docs/PHYSICS2D_BUILD_GUIDE.md`
- `Docs/PHYSICS2D_DESIGN.md`
- `Docs/NATIVE_CPP_SCRIPTING_GUIDE.md`
- `Docs/INPUT_GUIDE.md`
- `Docs/AUDIO_SYSTEM_GUIDE.md`
- `Docs/ANIMATION_2D_SYSTEM_GUIDE.md`

### Assets and Project

- `Docs/ASSET_SYSTEM_GUIDE.md`
- `Docs/ASSET_IMPORT_PIPELINE_GUIDE.md`
- `Docs/ASSET_HOT_RELOAD_GUIDE.md`
- `Docs/ASSET_VERSIONING_AND_MIGRATION.md`
- `Docs/PROJECT_SYSTEM_GUIDE.md`
- `Docs/BUILD_CONFIGURATION_GUIDE.md`

### Editor

- `Docs/EDITOR_PLAY_MODE_GUIDE.md`
- `Docs/EDITOR_CAMERA_CONTROLLER_GUIDE.md`
- `Docs/README_EditorArchitecture.md`
- `Docs/IMGUI_GUIDE.md`

## Known Limitations

- Rendering backend is OpenGL in production; Vulkan/DirectX/Metal are not implemented end-to-end yet.
- Scene loading model is currently single active scene per context (no first-class additive/streaming runtime model yet).
- GPU telemetry in performance monitor remains limited without vendor-specific integration.
- Some optional subsystems (for example FFmpeg integration on Windows) depend on local vendor/binary availability.

## License

This project is proprietary and closed-source. Public/open contributions are not accepted at this time.
See `LICENSE` for terms.
