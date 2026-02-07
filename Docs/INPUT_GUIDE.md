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

## Key Identifiers

Bindings currently use **SDL scancodes** (`SDL_Scancode`) for keyboard keys (physical keys).
This keeps controls stable across keyboard layouts.

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

- Map: `"Editor"`
- Actions:
  - `"Move"`: `Axis2D` with `KeyboardAxis2DBinding` (WASD)
  - `"Look"`: `Axis2D` with `MouseDeltaBinding`
  - `"Boost"`: `Button` with `KeyboardButtonBinding` (Shift)

At runtime:

- `Move.ReadAxis2D()` drives camera translation
- `Look.ReadAxis2D()` drives yaw/pitch
- `Boost.ReadButton()` increases speed

## Files

- `Limitless/Source/Core/Input/InputSystem.{h,cpp}`
- `Limitless/Source/Core/Input/InputAction.{h,cpp}`

