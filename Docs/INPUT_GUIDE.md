# Input Guide (Action Maps + Actions + Bindings)

This engine provides a **Unity-style** input layer built around:

- **Input Action Assets** (`InputActionAsset`)
- **Action Maps** (`InputActionMap`)
- **Actions** (`InputAction`)
- **Bindings** (`InputBinding` variants)

The goal is to keep gameplay/editor code **data-driven** and easy to scale to multiple control schemes.

## Threading Model

- `InputSystem`, `InputActionAsset`, maps, and actions are **not thread-safe**.
- Use them on the main/game thread.

## Frame Lifecycle

The engine runs input in a Unity-like frame order:

- `InputSystem::BeginFrame()` resets per-frame state (mouse delta, wheel delta, pressed/released).
- SDL events are pumped and forwarded to `InputSystem` (same-frame).
- `InputSystem::UpdateActions()` evaluates enabled action maps.

This means Layers can **poll actions** during `Layer::OnUpdate()`.

## Project-wide asset vs overrides (Unity-style)

The engine supports two common patterns:

- **Project-wide default action asset**: set once for the game/project and used everywhere.
- **Override action asset (stack)**: temporary override (e.g., editor viewport, menu, modal UI) without changing the project default.

### APIs

- **Project-wide**: `InputSystem::SetProjectActionAsset(asset)`
- **Override**: `InputSystem::PushOverrideActionAsset(asset)` / `PopOverrideActionAsset(...)`
- **Evaluation**: `InputSystem::UpdateActions()` uses the **top override** if present, otherwise the project default.

`Sandbox/TestLayer` sets the **project** input actions from `Assets/InputActions/Sandbox.inputactions.json`.
The engine-owned `EditorCameraController` pushes its own override so editor controls do not affect gameplay actions.

## Key Identifiers

Bindings currently use **SDL scancodes** (`SDL_Scancode`) for keyboard keys (physical keys).
This keeps controls stable across keyboard layouts.

## Control Schemes (Keyboard+Mouse + Gamepad)

Actions support **multiple bindings** at the same time. This is the intended approach for multiple control schemes:

- Keyboard+mouse bindings and gamepad bindings can both feed the same `InputAction`.
- `InputAction::EvaluateValue(...)` combines contributions from all bindings for that action.

### Supported Binding Types

The `InputBinding` variant currently supports:

- `KeyboardButtonBinding`
- `MouseButtonBinding`
- `KeyboardAxis1DBinding`
- `KeyboardAxis2DBinding`
- `MouseDeltaBinding`
- `GamepadButtonBinding`
- `GamepadAxis1DBinding`
- `GamepadAxis2DBinding`

Gamepad support is currently “single primary gamepad” (first connected gamepad wins). This is intentionally minimal and can be extended later to support player indices.

## Action Value Types

- **Button**: boolean (pressed/not pressed)
- **Axis1D**: float (e.g. -1..1)
- **Axis2D**: `glm::vec2` (e.g. WASD, mouse delta)

## Phases

Actions expose simplified Unity-like phases:

- `Started` (false → true)
- `Performed` (true while actuated)
- `Canceled` (true → false)
- `Waiting` / `Disabled`

## Example: Editor Camera (WASD + Mouse Look)

This is exactly what `Sandbox/Source/TestLayer.cpp` does now:

- Editor input is defined in `Assets/InputActions/EditorCamera.inputactions.json`
- Map: `"Editor"`
- Actions:
  - `"Move"`: `Axis2D` with `KeyboardAxis2DBinding` and `GamepadAxis2DBinding` (left stick)
  - `"Look"`: `Axis2D` with `MouseDeltaBinding` and `GamepadAxis2DBinding` (right stick)
  - `"Boost"`: `Button` with `KeyboardButtonBinding` (Shift) and `GamepadButtonBinding` (shoulder)
  - `"LookEnable"`: `Button` with `MouseButtonBinding` (RMB) and `GamepadButtonBinding` (shoulder)

At runtime:

- `Move.ReadAxis2D()` drives camera translation
- `Look.ReadAxis2D()` drives yaw/pitch
- `Boost.ReadButton()` increases speed

## Runtime Rebinding (Capture Next Input + Save JSON)

The engine provides a minimal runtime rebinding helper:

- `Limitless/Source/Core/Input/InputRebinding.{h,cpp}`

How it works:

- You create an `InputRebinding` session.
- You register it with `InputSystem` using `InputSystem::SetRebindingSession(...)`.
- While active, the session can **consume the next SDL input event** and replace a specific binding slot.
- If enabled, it persists the updated asset back to JSON via `InputActionAssetSerializer::SaveToFile(...)`.

Important shipping note:

- Saving back to `Assets/...` requires the source `Assets/` tree to exist (development/editor workflow).
- In bundle-only/shipping builds, rebinding should typically persist to a **user-writable override file** (example: under the platform user data directory) rather than attempting to write into `Assets/`.

Current implementation:

- Overrides are stored under `PlatformDetection::GetUserDataPath()/InputActionsOverrides/<AssetKey>`.
- If an override file exists, `InputActionsAssetResource` will load it **instead of** the bundled/source asset for that key.

This is a low-level API intended for editor/UI code to build on top (no UI is provided by the engine).

## Files

- `Limitless/Source/Core/Input/InputSystem.{h,cpp}`
- `Limitless/Source/Core/Input/InputAction.{h,cpp}`
- `Limitless/Source/Core/Input/InputActionAssetSerializer.{h,cpp}`
- `Limitless/Source/Core/Input/InputRebinding.{h,cpp}`
- `Limitless/Source/Editor/EditorCameraController.{h,cpp}`

