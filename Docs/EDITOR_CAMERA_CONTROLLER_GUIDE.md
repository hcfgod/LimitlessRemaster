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

## Override Stack Behavior

When `Settings::UseOverrideActionAsset == true`:

- `Initialize(...)` pushes the editor action asset using `InputSystem::PushOverrideActionAsset(...)`
- `Shutdown()` pops that asset using `InputSystem::PopOverrideActionAsset(...)`

This ensures editor navigation does not change the project-wide gameplay input action asset.

## Cursor Behavior

Unity/editor style:

- While `LookEnable` is held, the controller locks + hides the cursor.
- When released, cursor is unlocked + visible.

This is implemented using the engine window API (`Window::SetCursorLocked`, `Window::SetCursorVisible`).

## Typical Usage

Runtime (or an editor app) typically:

- Creates a `PerspectiveCamera3D` via `CameraManager`
- Initializes `EditorCameraController` with that camera id
- Calls `Update(deltaTime)` each frame
- Forwards window resize events to `OnWindowResize(...)`

## File References

- Module: `Limitless/Source/Editor/EditorCameraController.{h,cpp}`
- Input: `Limitless/Source/Core/Input/*`
- Default actions asset: `Assets/InputActions/EditorCamera.inputactions.json`

