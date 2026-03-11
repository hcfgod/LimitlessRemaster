# Prefab System Guide

This guide documents the current prefab workflow in the editor and runtime.

## What a Prefab Is

A prefab is an asset file with the `.prefab.json` suffix, for example:

- `Assets/Prefabs/Player.prefab.json`

Prefab assets store an entity hierarchy template using the same general serialization model as scenes.

Prefab instance linkage is tracked on the instance root through:

- `PrefabInstanceComponent`

## Core Editor Workflow

### Create a Prefab

In the **Scene** panel:

- right-click an entity
- choose `Create Prefab`

The editor writes a prefab asset and marks the created/linked root as a prefab instance root.

### Open a Prefab for Editing

In the **Project** panel:

- single-click a prefab asset to inspect/select it
- double-click a prefab asset to open it for editing
- or use the context menu action `Open Prefab`

Opening a prefab asset loads it into **Prefab Mode**.

## Prefab Mode

When a prefab asset is open:

- the menu bar shows a `Prefab Mode` indicator
- the editor remembers the scene you came from
- `Return` takes you back to that previous scene
- `Apply To Instances` saves the prefab asset, returns to the previous scene, and reapplies that prefab asset to matching instances in that scene

If you request `Play` while editing a prefab asset, the editor first returns to the previous scene and then enters runtime.

Current note:

- the prefab-mode return handoff is currently implemented for `Play`
- `Simulate` does not currently run that same prefab-return flow

## Instantiating Prefabs

Current instantiation paths include:

- Project panel context menu: `Instantiate Prefab`
- drag/drop from the Project panel into the Scene hierarchy
- editor/runtime helper flows that instantiate prefabs under a chosen parent or at scene root

When instantiated under a parent, the prefab system preserves the authored root world transform as closely as possible instead of blindly inheriting a skewed local transform.

## Scene Panel Instance Actions

Right-clicking a prefab instance root in the **Scene** panel exposes:

- `Apply Prefab`
- `Revert Prefab`
- `Unpack Prefab`

Behavior:

- `Apply Prefab`
  - writes the current instance root subtree back to the prefab asset

- `Revert Prefab`
  - destroys the current instance root
  - re-instantiates the prefab asset under the same parent

- `Unpack Prefab`
  - removes `PrefabInstanceComponent`
  - keeps the scene objects in place as normal scene entities

## Apply To Instances Behavior

`Apply To Instances` is the prefab-stage workflow used after editing a prefab asset directly.

Current behavior:

- the prefab asset is saved first
- the editor returns to the originating scene
- every matching prefab instance root in that scene is found by prefab asset key
- old instances are destroyed
- new instances are copied from the prefab asset
- stored root parent, sibling order, transform, and tag are restored onto the replacement root

## Script and Entity Reference Copy Behavior

Prefab copy/instantiate flows now handle more than simple component duplication.

Current behavior includes:

- native script entries are copied
- managed script entries are copied
- runtime-only script state is reset on copied instances
- script-exposed `ScriptEntityReference` values are remapped when the referenced entity is part of the copied subtree

If a scene-specific entity reference does not exist inside the copied prefab subtree, scene-specific IDs are not blindly preserved across the copy.

## Undo / Redo

Scene mutations are integrated into editor undo/redo for actions such as:

- `Instantiate Prefab`
- `Revert Prefab`
- `Unpack Prefab`
- `Apply Prefab To Instances`

File-only prefab asset writes are not the same as scene undo entries.

## Current Scope

- prefab assets are edited as loaded prefab scenes
- instance linkage is root-based through `PrefabInstanceComponent`
- prefab editing and apply flows are integrated with the scene/editor workflow
- prefab subtree copies reset runtime-only component/script state

The guide does not assume nested prefab authoring or per-property override UI beyond what is explicitly implemented today.

## Related Files

- `Editor/Source/Layer/EditorLayerPrefabAssets.cpp`
- `Editor/Source/Systems/EditorPrefabSystem.{h,cpp}`
- `Limitless/Source/Scene/ScenePrefab.cpp`
