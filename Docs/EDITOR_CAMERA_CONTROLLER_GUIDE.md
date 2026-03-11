# Editor Camera Controller Guide

This document describes the **engine-owned** editor camera module:

- `Limitless/Source/Editor/EditorCameraController.{h,cpp}`

The goal is to provide a reusable, “engine product” quality controller for tools/editor-style camera navigation that is **not Runtime-specific**.

## Design Goals

- Reusable engine module (Runtime is only a consumer).
- Data-driven input using Unity-style **Input Action Assets** (`InputActionAsset`).
- Does not overwrite project gameplay input: uses the **override action asset stack**.

## Input Actions Contract

The controller expects an action map named `"Editor"` with:

- **Move** (`Axis2D`): WASD / left stick
- **Look** (`Axis2D`): mouse delta / right stick
- **Boost** (`Button`): Shift / shoulder
- **LookEnable** (`Button`): RMB (hold-to-look) / shoulder

Default asset key:

- `Assets/InputActions/EditorCamera.inputactions.json`

## Fallback Input Behavior

If the configured input actions asset cannot be loaded, the controller falls back to an internal keyboard/mouse action asset so it remains usable.

Current fallback bindings are:

- `Move`: `W`, `A`, `S`, `D`
- `Look`: mouse delta
- `Boost`: `Left Shift`
- `LookEnable`: right mouse button

## Override Stack Behavior

When `Settings::UseOverrideActionAsset == true`:

- `Initialize(...)` pushes the editor action asset using `InputSystem::PushOverrideActionAsset(...)`
- `SetInputEnabled(false)` pops the override and ignores controller input
- `SetInputEnabled(true)` pushes the override again
- `Shutdown()` pops the asset if it is still the active override

This ensures editor navigation does not change the project-wide gameplay input action asset.

## Cursor Behavior

Unity/editor style:

- The controller restores cursor visibility/unlock state when input is disabled or during shutdown.
- In the current editor path, viewport capture and cursor lock policy are orchestrated by `EditorRuntimeOperations`, not by `EditorCameraController` alone.
- `EditorRuntimeOperations` decides whether the Scene view or Game view owns right-mouse capture and then applies `Window::SetCursorLocked(...)` / `Window::SetCursorVisible(...)`.
- Inside `Update(...)`, the controller prefers raw RMB state, with the action as fallback for non-mouse bindings, to avoid docked-viewport flicker while relative mouse lock engages.

## Asset Hot Reload

When the controller successfully loads `Assets/InputActions/EditorCamera.inputactions.json` through `InputActionsAssetResource`, it tracks the resource revision and refreshes cached action pointers if the asset hot reloads in place.

## Typical Usage

Editor/runtime host code typically:

- Creates a `PerspectiveCamera3D` via `CameraManager`
- Initializes `EditorCameraController` with that camera id
- Chooses when controller input is enabled (for example, Scene view hover/capture rules)
- Calls `Update(deltaTime)` each frame
- Forwards window resize events to `OnWindowResize(...)`

## File References

- Module: `Limitless/Source/Editor/EditorCameraController.{h,cpp}`
- Input: `Limitless/Source/Core/Input/*`
- Host/editor integration: `Editor/Source/Operations/EditorRuntimeOperations.cpp`
- Default actions asset: `Assets/InputActions/EditorCamera.inputactions.json`

