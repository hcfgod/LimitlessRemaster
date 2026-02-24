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

- **Project-wide**: `InputSystem::SetProjectActionAsset(asset)` or `InputSystem::SetProjectActionAssetFromKey(assetKey)` to load and set by asset key (e.g. `"Assets/InputActions/Runtime.inputactions.json"`).
- **Override**: `InputSystem::PushOverrideActionAsset(asset)` / `PopOverrideActionAsset(...)`
- **Evaluation**: `InputSystem::UpdateActions()` uses the **top override** if present, otherwise the project default.

`Runtime/Source/TestLayer.cpp` sets the **project** input actions via `SetProjectActionAssetFromKey("Assets/InputActions/Runtime.inputactions.json")`.
The engine-owned `EditorCameraController` pushes its own override so editor controls do not affect gameplay actions.

## Project Settings InputActions (Default + Aliases)

Project Settings now supports Unity-style multi-asset input setup:

- **Default InputActions**: one project-wide asset used by `InputSystem::GetActiveActionAsset()` when no override stack is active.
- **Additional InputActions Assets**: multiple assets registered with **logical aliases** (for example: `Gameplay`, `Vehicle`, `UiNavigation`).

### Runtime helpers

Use `Project::ProjectSettings` helpers to resolve aliases:

- `Project::ResolveInputActionsAssetKeyByAlias(inputSettings, alias)`
- `Project::ResolveInputActionsAssetKeyByAlias(projectRoot, alias)`
- `Project::CollectAdditionalInputActionsAssetKeys(inputSettings)`

Built-in alias behavior:

- Alias `"Default"` resolves `InputSettings.ProjectInputActionsKey`.

### Example

```cpp
const std::filesystem::path projectRoot = Project::ProjectManager::GetInstance().GetProjectRoot();
const auto gameplayKeyResult = Project::ResolveInputActionsAssetKeyByAlias(projectRoot, "Gameplay");
if (gameplayKeyResult.IsSuccess())
{
    const std::string gameplayAssetKey = gameplayKeyResult.GetValue();
    // Load or activate the gameplay input action asset by key.
}
```

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

Gamepad support uses **explicit player/device indexing** for multi-player:

- Up to **`InputSystem::kMaxGamepads`** (4) gamepads are tracked; the first connected uses player index 0, the next 1, and so on.
- **Polling API**: `HasGamepad(playerIndex)`, `IsGamepadButtonDown(playerIndex, button)`, `WasGamepadButtonPressedThisFrame(playerIndex, button)`, `WasGamepadButtonReleasedThisFrame(playerIndex, button)`, `GetGamepadAxis(playerIndex, axis)`. Overloads without `playerIndex` default to player 0 (primary).
- **Count**: `GetGamepadCount()` returns how many gamepads are currently connected.
- **Bindings**: Each gamepad binding has an optional **`PlayerIndex`** (default 0). Use it in action assets or JSON (`"player_index": 1`) to bind actions to a specific player's gamepad.
- Existing single-player code and assets remain valid; player index 0 is the default everywhere.

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

This is exactly what `Runtime/Source/TestLayer.cpp` does now:

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

This is a low-level API intended for editor/UI code to build on top (no UI is provided by the engine). Rebinding currently captures input from the **primary gamepad (player 0)** only.

## Touch and gestures (future)

Touch and gesture input are **not implemented** in the current input API. SDL3 supports touch via `SDL_touch.h` (touch device IDs, finger IDs, normalized coordinates) and can be integrated later for mobile and touch-capable platforms. Gestures (e.g. pinch, rotate) would build on top of touch or platform-specific APIs.

## Files

- `Limitless/Source/Core/Input/InputSystem.{h,cpp}`
- `Limitless/Source/Core/Input/InputAction.{h,cpp}`
- `Limitless/Source/Core/Input/InputActionAssetSerializer.{h,cpp}`
- `Limitless/Source/Core/Input/InputRebinding.{h,cpp}`
- `Limitless/Source/Editor/EditorCameraController.{h,cpp}` (engine module; Editor and Runtime both use it)

