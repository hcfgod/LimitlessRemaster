# Physics2D Test Plan

## Runtime Validation

- Create entities with `Rigidbody2DComponent` + `BoxCollider2DComponent`.
- Create entities with `Rigidbody2DComponent` + `CircleCollider2DComponent`.
- Verify static, dynamic, and kinematic body behavior.
- Verify gravity, damping, sleep, and bullet flags change simulation output.
- Verify distance, revolute, and prismatic joints constrain correctly.

## Editor Validation

- Add/remove all Physics2D components through Inspector Add Component menu.
- Edit properties and verify Undo/Redo for:
  - Rigidbody 2D
  - Box Collider 2D
  - Circle Collider 2D
  - Joint 2D
- Verify viewport overlays render selected colliders and joint links.
- Verify Play mode runs scripts + physics.
- Verify Simulate mode runs physics without script updates.

## Persistence Validation

- Save scene with full Physics2D data.
- Reload scene and verify all component values persist:
  - Rigidbody body type and tuning
  - Collider geometry and filter bits
  - Joint configuration and connected entity references
- Enter Play/Simulate from loaded scene and verify behavior is consistent.

## Scripting Validation

- Use `ScriptableEntity::Raycast2D(...)` in a native script.
- Use `ScriptableEntity::HasContactWith(...)` and `GetContactCount(...)`.
- Verify returned entity ids match expected colliders in scene.

## Cross-Platform Build Validation

- Windows:
  - Run `Scripts/build-windows.bat Debug x64`
  - Confirm Box2D links against vendored libs.
- Linux:
  - Install `libbox2d-dev`
  - Run `bash Scripts/build-unix.sh --config Debug --compiler gcc`
- macOS:
  - Install `box2d` via Homebrew
  - Run `bash Scripts/build-unix.sh --config Debug --compiler clang`
