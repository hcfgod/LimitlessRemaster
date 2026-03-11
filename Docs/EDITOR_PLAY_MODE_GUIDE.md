## Editor Play Mode Guide

The editor supports a Unity-style runtime loop with four visible states:

- `Edit`
- `Play`
- `Simulate`
- `Pause`

## Goals

- Non-destructive authoring: runtime changes should not overwrite the edit scene.
- Fast iteration: entering and exiting runtime should stay in-memory whenever possible.
- Deterministic restore: leaving runtime should restore the stored edit scene identity, selection, and editor-facing state.
- Safe transitions: scene switches, prefab mode, pending animation edits, and script build failures are resolved before runtime starts.

## Current Runtime Model

### Edit Mode

- The editor works on one authoring scene at a time.
- Scene save/load/new operations are only allowed from `Edit`.
- The currently open authoring scene is tracked by asset key and persisted in session state.

### Play Mode

- Entering `Play` clones the current edit scene into runtime state.
- Native/managed runtime systems, gameplay camera selection, per-frame runtime update, fixed update, physics, and script execution become active.
- Runtime scene loads can add or replace scenes while staying in play mode.

### Simulate Mode

- `Simulate` is a runtime clone like `Play`, but keeps the editor-centric workflow for simulation/debugging scenarios.
- It uses the same scene-clone foundation and many of the same preflight checks as Play Mode.
- Current behavior differs from full `Play`: fixed-step updates / physics still run, but the editor's normal per-frame runtime `scene->Update(deltaTime)` path is only executed in `Play`.

### Pause

- `Pause` stops the editor's runtime update/fixed-update progression while keeping the runtime scene(s) available for inspection/rendering.
- Audio playback state is still kept live while paused.

## Preflight Before Entering Runtime

Before entering `Play` or `Simulate`, the editor currently:

- applies pending Animation Clip edits
- applies pending Animator Controller edits
- clears preview/game-view camera state as needed
- checks the last script build result

Before entering `Play`, the editor also resolves prefab-mode return behavior before runtime starts.

## Script Build Failure Policy

The build settings file controls what happens when native script compilation has failed:

- `SafeMode`
  - runtime starts
  - native script execution is blocked for that session
  - the editor logs a persistent warning

- `BlockPlay`
  - the editor refuses to enter `Play` / `Simulate` until scripts build successfully

This policy is read from:

- `Project/Settings/BuildSettings.json`

## Scene Cloning

Runtime entry is still based on cloning the edit scene rather than mutating the authoring instance in place.

Important behavior:

- edit-scene audio playback is stopped before runtime begins
- the edit scene is stored off to the side
- the runtime clone becomes the active scene view target
- on exit, runtime scenes are destroyed and the stored edit scene is restored

The clone path must continue copying any component that should exist during runtime, while resetting runtime-only state such as active voices, runtime script instances, or cached handles that should not leak across sessions.

## Runtime Scene Ownership While Playing

The editor now has a higher-level runtime scene ownership model while in `Play` / `Simulate`:

- one runtime scene is marked active for gameplay-primary/script-query behavior
- additional scenes can be loaded additively
- loaded runtime scenes can be activated or unloaded by asset key
- exiting play mode clears all runtime scenes and restores the authoring scene

This means gameplay code can call runtime scene APIs such as:

- single-scene load
- additive load
- set active scene
- unload scene

without forcing the editor back to `Edit`.

## Scene Identifier Handling

Scene loads accept author-friendly identifiers and normalize them at runtime:

- `Level01`
- `Scenes/Level01`
- `Assets/Scenes/Level01`
- `Assets/Scenes/Level01.scene.json`

Normalization handles slash differences and `.scene` / `.scene.json` suffixes.
Normalization is also case-insensitive when matching against runtime/build scene keys.

## Prefab Mode Interaction

If the editor is currently viewing a prefab asset when `Play` is requested:

- it first returns to the previous scene when possible
- then enters `Play`

This keeps play mode centered on scene execution instead of prefab-stage authoring.

Current note:

- the dedicated prefab-mode return handoff is implemented in the `EnterPlayMode()` path
- `EnterSimulateMode()` does not currently run that same prefab-mode redirect flow

## Scene Switching Safety

Before destructive scene switches in edit mode, the editor checks whether there are unsaved changes and may defer the switch behind confirmation flow.

Beginning a scene switch also clears state that must not leak across scenes, including:

- selected entity / multi-selection
- scene panel rename/delete pending state
- tilemap edit targets
- transform gizmo drag state
- runtime preview camera state

## Session and Selection Restore

When leaving runtime:

- native script blocking state is cleared
- runtime audio is stopped
- runtime scenes are removed
- the stored edit scene asset key is restored
- edit-mode entity selection is restored when still valid
- Scene panel selection anchors and multi-selection are rebuilt

## Related Files

- `Editor/Source/Layer/EditorLayerSceneLifecycle.cpp`
- `Editor/Source/Core/EditorMenuBar.cpp`
- `Editor/Source/Core/EditorPlayMode.*`
- `Limitless/Source/Scene/SceneClone.cpp`

## Known Boundaries

- Edit mode still authors one scene at a time.
- Additive runtime scene handling exists for runtime/play workflows, but broader editor-side multi-scene authoring and streaming UX is still evolving.

