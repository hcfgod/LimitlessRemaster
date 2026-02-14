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

## Exposed Variables in Inspector

Each `Native Script` component automatically exposes supported `public` fields from the script header.

- Declare fields in your script class (not in the inspector UI).
- Inspector values are serialized with the scene.
- Supported field types: `float`, `int`/`int32_t`, `bool`, `glm::vec3`, `std::string`.

From script code, read/write these values with the field name:

- `GetExposedFloat`, `GetExposedInteger`, `GetExposedBoolean`, `GetExposedVector3`, `GetExposedString`
- `SetExposedFloat`, `SetExposedInteger`, `SetExposedBoolean`, `SetExposedVector3`, `SetExposedString`
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

Important:

- New scripts are compiled through the `ScriptCore` DLL project.
- The editor can trigger script-only builds with:
  - Windows: `Scripts/build-scriptcore-windows.bat`
  - Linux/macOS: `Scripts/build-scriptcore-unix.sh`
- Build configuration/platform are read from project build target settings when available.
- Script classes are loaded from the platform-native ScriptCore module and hot-reloaded in Edit mode when its timestamp changes (`ScriptCore.dll` on Windows, `libScriptCore.so` on Linux, `libScriptCore.dylib` on macOS).

## Current Scope

- Runtime execution is active in Play Mode.
- Scene save/load and clone preserve `NativeScriptComponent` class name and enabled state.
- Script runtime instances are transient and are not serialized.
- Script authoring source-of-truth is the opened project's `Assets` folder.
- Script build inputs are generated mirrors under `Build/Generated/ScriptCore`.
