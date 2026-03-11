# LimitlessRemaster

LimitlessRemaster is a C++20 game engine and editor focused on 2D-first workflows with an expanding Unity-style authoring model.

Current major areas include:

- Unity-style editor workflow
- scene/ECS runtime on EnTT
- OpenGL-based 2D renderer
- asset import/versioning with `.meta` GUIDs
- native C++ scripting through `ScriptCore`
- managed C# scripting through Coral/CoreCLR payload staging
- animation, UI, audio, tilemaps, input actions, and 2D physics
- cross-platform build/test/export tooling

## Current Status

- Editor-first workflow with scene hierarchy, inspector, project browser, diagnostics, layouts, and session persistence.
- Play, Simulate, Pause, prefab editing, and runtime scene transitions are implemented.
- The Project browser supports grid browsing plus compact list mode at scale `0.0`.
- Physics2D supports multiple worlds per scene via `WorldCount` and per-body `PhysicsWorldSlot`.
- Audio supports global, 2D spatial, and 3D spatial playback.
- Native and managed scripting coexist; managed scripting does not replace native scripting.
- Rendering backend in production remains OpenGL.

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

The Unix build script can validate/install required dependencies depending on host platform and package-manager availability.

### Premake Direct

```bash
Vendor/Premake/premake5 vs2022
Vendor/Premake/premake5 gmake2
```

## Run

### Editor

- Windows: `Build/debug_x64-windows-x64/Editor/Editor.exe`
- Linux: `./Build/debug_x64-linux-x64/Editor/Editor`
- macOS: `./Build/debug_x64-macosx-x64/Editor/Editor`

### Runtime Sample

- Windows: `Build/debug_x64-windows-x64/Runtime/Runtime.exe`
- Linux/macOS: `./Build/debug_x64-<system>-x64/Runtime/Runtime`

### Tests

- Windows: `Build\debug_x64-windows-x64\Test\Test.exe --success`
- Linux/macOS: `./Build/debug_x64-<system>-x64/Test/Test --success`

## Scripting Overview

### Native C++

- Built through `ScriptCore`
- Project-authored native sources live under project `Assets/`
- Editor supports creation/edit/build/hot-reload flows

### Managed C#

- Contract/API assembly lives under `Managed/Limitless.Managed`
- Runtime payload is staged into `Managed/`
- Project-authored `.cs` files under `Assets/` can be generated/built/discovered through the editor workflow

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
- `Docs/MANAGED_CSHARP_SCRIPTING_GUIDE.md`
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

- OpenGL is still the only production-ready rendering backend.
- Editor authoring is still centered on one open scene at a time, even though runtime scene ownership is broader while playing.
- GPU telemetry remains limited without deeper vendor-specific integration.
- Some optional dependencies, such as FFmpeg on Windows, still depend on local binary/vendor availability.

## License

This project is proprietary and closed-source. Public/open contributions are not accepted at this time.

See `LICENSE` for terms.
