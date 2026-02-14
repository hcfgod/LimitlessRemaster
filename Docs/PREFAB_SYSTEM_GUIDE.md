# Prefab System Guide (V1)

This guide documents the first Unity-style prefab workflow added to the editor.

## What a Prefab Is

A prefab is an asset file with the `.prefab.json` suffix (for example `Assets/Prefabs/Player.prefab.json`) that stores an entity hierarchy template.

## Editor Workflow

- In the **Scene** panel, right-click an entity and choose `Create Prefab`.
  - The editor writes a prefab asset under `Assets/Prefabs/`.
  - The entity is marked as a prefab instance root.
- In the **Project** panel, double-click a prefab file to instantiate it into the active scene.
- Drag-and-drop a prefab from **Project** to **Scene** hierarchy:
  - Drop on a specific entity to instantiate as its child.
  - Drop on scene root/empty root zone to instantiate at the root level.
- Right-click a prefab instance root in **Scene** for:
  - `Apply Prefab` (write current instance state back to asset)
  - `Revert Prefab` (replace instance with asset state)
  - `Unpack Prefab` (remove prefab linkage, keep scene objects)

## Undo/Redo Integration

- Scene mutations are integrated into the editor undo stack:
  - Create Prefab (instance tagging)
  - Instantiate Prefab
  - Revert Prefab
  - Unpack Prefab
- File-only operations (`Apply Prefab` writing the prefab asset) are intentionally not part of scene undo.

## Current V1 Scope

- Prefab instance linkage is tracked by `PrefabInstanceComponent` on the instance root entity.
- Prefab assets use the same component serialization model as scenes.
- Nested prefab authoring and fine-grained per-property override UI are not part of V1 yet.
