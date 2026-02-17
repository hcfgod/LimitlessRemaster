# Native C++ Scripting Guide

This guide explains how to write and run native C++ scripts on scene entities.

## Overview

Native scripting is component-driven:

- Add `NativeScriptComponent` to an entity in the Inspector.
- Register script classes with `NativeScriptRegistry`.
- Select the registered class name from the `Native Script` inspector section.
- Enter Play Mode to run script lifecycle methods.

## Script Lifecycle

Derive from `ScriptableEntity` and override any of:

- `OnCreate()` called once when the script instance starts.
- `OnUpdate(float deltaTime)` called each runtime frame in Play Mode.
- `OnDestroy()` called when the entity is destroyed, script is disabled/removed, or scene shuts down.

## Entity and Component API (Unity-Style)

`ScriptableEntity` now supports scene creation/destruction and component operations directly from scripts through a Unity-style `Limitless::Entity` wrapper.

- `Entity CreateEntity(const std::string& name = "Entity")`
- `void DestroyEntity(Entity entity)` or `entity.Destroy()`
- `bool entity.HasComponent<T>()`
- `T& entity.GetComponent<T>()`
- `T& entity.AddComponent<T>(...)`
- `void entity.RemoveComponent<T>()`

Behavior:

- `CreateEntity` returns an `Entity` value object (no manual allocation/deallocation).
- `AddComponent<T>` is idempotent for scripting convenience: if the component already exists, the existing component is returned.
- `RemoveComponent<T>` is safe to call even if the component is missing.
- Advanced users can still work with raw handles through `entity.GetHandle()` and low-level overloads.

Example:

```cpp
Limitless::Entity spawned = CreateEntity("RuntimeEnemy");
auto& transform = spawned.GetComponent<Limitless::TransformComponent>();
transform.Position = glm::vec3(4.0f, 2.0f, 0.0f);

auto& sprite = spawned.AddComponent<Limitless::SpriteComponent>();
sprite.Color = glm::vec4(1.0f, 0.3f, 0.3f, 1.0f);

auto& rigidbody = spawned.AddComponent<Limitless::Rigidbody2DComponent>();
rigidbody.Type = Limitless::Rigidbody2DComponent::BodyType::Dynamic;

spawned.AddComponent<Limitless::BoxCollider2DComponent>();
```

## Scene Management API (Unity-Style)

Scripts can request scene transitions at runtime:

- `Limitless::SceneManager::LoadScene("Level02")`
- `Limitless::SceneManager::LoadScene("Scenes/Level02")`
- `Limitless::SceneManager::LoadScene("Assets/Scenes/Level02")`
- `Limitless::SceneManager::LoadScene("Assets/Scenes/Level02.scene.json")`
- `Limitless::SceneManager::ReloadCurrentScene()`

Important behavior:

- Scene transitions are deferred until a safe point after script updates in the current frame.
- In Editor Play Mode, scene transitions stay in Play Mode (they do not force-exit to Edit Mode).
- Scene keys must be valid asset keys under `Assets/...`.
- `.scene.json` is optional when calling `LoadScene`; it is appended automatically if omitted.
- Scene name-only loading (for example `"Level02"`) resolves through project scene records tracked by `AssetDatabase`.
- If multiple scene assets share the same scene name, loading by name is considered ambiguous and will fail with a warning.

## Physics2D API (Unity-Style)

Scripts can raycast through the scripting API:

- `Limitless::Physics2D::Raycast(origin, direction, maxDistance, collisionMask)`

The call returns a `Limitless::RaycastHit2D` with:

- `HasHit`
- `Entity`
- `Point`
- `Normal`
- `Fraction`

Example grounded probe:

```cpp
const glm::vec2 origin(transform.Position.x, transform.Position.y - 0.45f);
const Limitless::RaycastHit2D hit = Limitless::Physics2D::Raycast(
    origin,
    glm::vec2(0.0f, -1.0f),
    0.2f);
const bool grounded = hit.HasHit && hit.Entity != GetEntityHandle();
```

Rigidbody 2D constraints are available in the Inspector (Unity-style):

- `Freeze Position X`
- `Freeze Position Y`
- `Freeze Rotation`

These constraints are serialized with the scene/prefab and enforced at runtime by the physics world.

## Debug Logging API (Unity-Style)

Native scripts can now emit runtime logs directly:

- `Limitless::Debug::Log("message")`
- `Limitless::Debug::LogWarning("message")`
- `Limitless::Debug::LogError("message")`
- `LT_INFO("message")`, `LT_WARN("message")`, `LT_ERROR("message")` are also supported in ScriptCore and route to the same bridge.

These calls are forwarded from `ScriptCore` into the host editor logger, so you can debug game logic without adding temporary engine-side prints.

Example:

```cpp
if (jumpPressed && !grounded)
{
    Limitless::Debug::LogWarning("Jump pressed while grounded check returned false.");
}
```

## Editor Console

The editor now includes a **Console** window for runtime debugging:

- Open it from `Window -> Console`.
- It shows engine and script logs in one stream.
- Script-origin logs are tagged with `[Script]`.
- Source filters are available (`Scripts`, `Engine`), and default to script-only.
- Script severity counters (`I`, `W`, `E`) are shown for quick debugging context.
- Use filters (`Info`, `Warnings`, `Errors`) and search text to isolate issues quickly.
- `Copy Visible` copies the currently filtered console lines to your clipboard.
- `Clear` removes buffered messages from the panel.

## Exposed Variables in Inspector

Each `Native Script` component automatically exposes supported `public` fields from the script header.

- Declare fields in your script class (not in the inspector UI).
- Inspector values are serialized with the scene.
- Supported field types: `float`, `int`/`int32_t`, `bool`, `glm::vec3`, `std::string`, `Limitless::Entity`.
- `Limitless::Entity` fields are Unity-style object slots in Inspector:
  - Choose from a dropdown of scene entities
  - Drag an entity from the Scene panel directly into the slot
  - Clear with `X`
  - Runtime resolution is tag-based, so tags should be unique.

From script code, read/write these values with the field name:

- `GetExposedFloat`, `GetExposedInteger`, `GetExposedBoolean`, `GetExposedVector3`, `GetExposedString`, `GetExposedEntity`
- `SetExposedFloat`, `SetExposedInteger`, `SetExposedBoolean`, `SetExposedVector3`, `SetExposedString`, `SetExposedEntity`
- `LT_SYNC_EXPOSED_FIELD(FieldName)` for manual sync when needed
- `LT_BEGIN_AUTO_EXPOSED_FIELD_SYNC()` / `LT_AUTO_EXPOSED_FIELD(FieldName)` / `LT_END_AUTO_EXPOSED_FIELD_SYNC()` to auto-sync fields in the background (recommended)

Example:

```cpp
class DoorRotateScript final : public Limitless::ScriptableEntity
{
public:
    float RotationSpeed = 90.0f;

protected:
    LT_BEGIN_AUTO_EXPOSED_FIELD_SYNC()
        LT_AUTO_EXPOSED_FIELD(RotationSpeed)
    LT_END_AUTO_EXPOSED_FIELD_SYNC()

    void OnUpdate(float deltaTime) override
    {
        auto& transform = GetComponent<Limitless::TransformComponent>();
        transform.Rotation.y += RotationSpeed * deltaTime;
    }
};
```

Scene transition example:

```cpp
#include "Limitless.h"

class PortalScript final : public Limitless::ScriptableEntity
{
public:
    std::string TargetScene = "Level02";

protected:
    LT_BEGIN_AUTO_EXPOSED_FIELD_SYNC()
        LT_AUTO_EXPOSED_FIELD(TargetScene)
    LT_END_AUTO_EXPOSED_FIELD_SYNC()

    void OnUpdate(float /*deltaTime*/) override
    {
        if (Limitless::InputSystem::IsKeyDown(Limitless::KeyCode::Enter))
        {
            // Queue scene change to be applied safely after script updates finish.
            Limitless::SceneManager::LoadScene(TargetScene);
        }
    }
};
```

Legacy note:

- If a script still uses `GetExposed*("Name", fallback)` calls and has no public fields yet, the inspector can still discover those names for backward compatibility.

## Authoring a Script

```cpp
#include "Limitless.h"

class DoorRotateScript final : public Limitless::ScriptableEntity
{
protected:
    void OnUpdate(float deltaTime) override
    {
        auto& transform = GetComponent<Limitless::TransformComponent>();
        transform.Rotation.y += 45.0f * deltaTime;
    }
};
```

## Registering Scripts

Register scripts during application startup (before Play Mode):

```cpp
Limitless::NativeScriptRegistry::RegisterScript<DoorRotateScript>("DoorRotateScript");
```

Notes:

- `NativeScriptComponent.ScriptClassName` (and each script entry’s class name) must match the registered class name exactly.
- Scripts are native C++ only (no C# bridge in this system).
- **Editor Play Mode**: The Editor builds and loads the **ScriptCore** DLL (`ScriptCore` project). Scripts authored under the project’s `Assets/` are compiled into ScriptCore; when you enter Play Mode, the Editor loads that DLL and registers its scripts so they run on entities with `NativeScriptComponent`.

## Editor Usage

1. Select an entity.
2. Click `Add Component` and add `Native Script`.
3. In the `Native Script` section:
   - Toggle `Enabled`.
   - Pick a script from the `Class` dropdown.
4. Enter Play Mode and observe behavior.

If the class list is empty, no scripts were registered in the running executable.

## In-Editor Script Authoring

The inspector now supports native script authoring:

- `Create New Native Script` generates template files in:
  - `<OpenedProjectRoot>/Assets/<AnyFolderYouChoose>`
- `Edit Current Native Script` opens a built-in text editor window for `.h` and `.cpp`.
- `Save Files` writes edited buffers back to disk.
- Saved scripts are mirrored into `<EngineWorkspace>/Build/Generated/ScriptCore` for compilation by the `ScriptCore` build.
- Mirror preserves your relative folder structure under `Assets` (for example `Assets/Gameplay/Player` mirrors to `.../Build/Generated/ScriptCore/Gameplay/Player`).
- Build sync mirrors all paired `*.h`/`*.cpp` native scripts found under project `Assets` before compiling `ScriptCore`.
- `Auto Build On Save` can trigger ScriptCore-only builds automatically after saving.
- Script editor quality-of-life includes:
  - Unsaved tab indicators (`*`) for header/source buffers.
  - `Ctrl+S` (save), `Ctrl+B` (save + build), and `Ctrl+R` (reload from disk).
  - `Save + Build` performs an explicit save/mirror before compilation.

Important:

- New scripts are compiled through the `ScriptCore` DLL project.
- The editor can trigger script-only builds with:
  - Windows: `Scripts/build-scriptcore-windows.bat`
  - Linux/macOS: `Scripts/build-scriptcore-unix.sh`
- Build configuration/platform are read from project build target settings when available.
- Script classes are loaded from the platform-native ScriptCore module and hot-reloaded in Edit mode when its timestamp changes (`ScriptCore.dll` on Windows, `libScriptCore.so` on Linux, `libScriptCore.dylib` on macOS).

## Stress Testing Large Entity Counts

A sample stress script class can be authored in your opened project under `Assets/Scripts`:

- `PhysicsStressSpawnerScript`

What it does:

- Spawns a grid of entities on `OnCreate()`.
- Adds `SpriteComponent`, `Rigidbody2DComponent`, and `BoxCollider2DComponent` to each spawned entity.
- Logs the spawned total to the script console.

Key exposed fields:

- `Columns` (default `120`)
- `Rows` (default `80`)
- `Spacing` (default `1.1`)
- `ColliderSize` (default `0.9`)
- `SpawnOnCreate` (default `true`)
- `Spawned` (runtime guard to prevent duplicate spawn)

Suggested workflow:

1. Create `PhysicsStressSpawnerScript.h/.cpp` in your project `Assets/Scripts` folder.
2. Build `ScriptCore` from the editor or with `Scripts/build-scriptcore-windows.bat`.
3. Add a single empty entity in your scene.
4. Add `Native Script` component.
5. Select class `PhysicsStressSpawnerScript`.
6. Tune `Rows`/`Columns` for your target stress level.
7. Enter Play Mode and monitor the FPS/performance overlay.

## Current Scope

- Runtime execution is active in Play Mode.
- Scene save/load and clone preserve `NativeScriptComponent` class name and enabled state.
- Script runtime instances are transient and are not serialized.
- Script authoring source-of-truth is the opened project's `Assets` folder.
- Script build inputs are generated mirrors under `Build/Generated/ScriptCore`.
