# Project System Guide

This document describes how Limitless projects are discovered, opened, configured, and persisted.

## Goals

- Deterministic project root discovery.
- Unity-style project layout centered around `Assets/`.
- Project-scoped settings that are portable and source-controlled.
- Clear separation between project files, generated build output, and user-level editor data.

## Project Layout

Typical project root:

```text
<ProjectRoot>/
  Assets/
  Build/
  Project/
    Project.json
    Settings/
      AudioSettings.json
      BuildSettings.json
      BuildTargets.json
      EditorLayout.current.ini
      EditorSessionState.json
      InputSettings.json
      Layers.json
      RenderSettings.json
```

Notes:

- `Assets/` is the authored content root used for asset keys such as `Assets/Scenes/Main.scene.json`.
- `Build/` is generated output and staging for project/runtime build workflows.
- `Project/Settings/BuildTargets.json` still exists for native script tooling compatibility.
- `Project/Settings/BuildSettings.json` is now the main per-project build/export settings file used by the editor build workflow.

## Project Marker File

The project identity marker is:

- `Project/Project.json`

It stores the project definition, including:

- stable project GUID
- project name
- asset/build root conventions
- optional default scene metadata

Implementation:

- `Limitless/Source/Project/ProjectDefinition.{h,cpp}`
- `Limitless/Source/Project/ProjectManager.{h,cpp}`

## Project Root Discovery Order

Asset key resolution for `Assets/...` depends on project root discovery in `AssetPaths`.

Current order:

- explicit override via `Assets::SetAssetRootDirectory(...)`
- environment override via `LIMITLESS_ASSET_ROOT`
- upward search for `Project/Project.json`
- fallback upward search for an `Assets/` directory

Notes:

- `LIMITLESS_ASSET_ROOT` may point either to the project root or directly to the `Assets/` directory
- discovery results are cached until the override/project context changes

Implementation:

- `Limitless/Source/Assets/AssetPaths.{h,cpp}`

## Build Settings vs Build Targets

### `BuildSettings.json`

Primary editor/game export settings live in:

- `Project/Settings/BuildSettings.json`

This file currently stores:

- ordered `Scenes In Build`
- stable scene GUIDs for rename safety
- persisted settings version
- build configuration
- last output directory
- compression settings
- build backend (`LegacySdk` or `InternalToolchain`)
- target OS / architecture
- execution mode (`Auto`, `Local`, `Remote`)
- remote build routing/auth settings
- game icon override
- engine root override field
- script editor mode
- native script compile failure policy

Important behavior:

- the first enabled build scene is the startup scene
- scene asset renames update matching build-scene entries
- scene deletions from the Project browser remove matching build-scene entries

### `BuildTargets.json`

`Project/Settings/BuildTargets.json` remains present for native script tooling and older build-target settings flows, but it is no longer the main export/build-settings source for the editor build workflow.

## Editor Session State

Per-project editor session state lives in:

- `Project/Settings/EditorSessionState.json`

This persists editor-facing state such as:

- last opened scene asset key
- active layout name
- visibility of major panels/windows
- Project browser active folder
- Project browser grid scale
- Project browser folder expansion state
- inspector foldout state
- native script editor session state

## Editor Layout State

The current working ImGui layout for a project is stored in:

- `Project/Settings/EditorLayout.current.ini`

Saved named layouts are user-level data, not project files. They are managed by `EditorLayoutManager`.

## Project Browser Behavior

The Project browser is now more than a simple folder tree. It supports:

- folder tree navigation
- a right-side browser region with grid and compact list modes
- search/filtering
- drag/drop moves
- multi-selection
- background context creation menus
- scene activation
- prefab open/instantiate actions
- native C++ script asset pairs
- managed C# script assets

Scene-aware delete behavior is routed through editor callbacks so build settings and undo state can be kept in sync.

## Editor UX

### Open / Create Project

Use:

- `File -> Open Project...`
- `File -> Create Project...`

Implementation:

- `Editor/Source/Dialogs/EditorProjectDialog.{h,cpp}`

### Recent Projects

Recent projects are stored in user data, not inside the project.

Current behavior:

- the editor stores them in `Editor/RecentProjects.json` under the user data directory
- roots are normalized before persistence
- opening an existing recent project moves it to the front
- the list is capped at 20 entries

Implementation:

- `Editor/Source/Utilities/EditorRecentProjects.{h,cpp}`

## Open / Close Project Side Effects

When a project is opened through `ProjectManager::OpenProjectRoot(...)`, the editor/runtime currently:

- loads and stores `Project/Project.json`
- clears cross-project asset caches
- clears the native script registry
- sets the global `AssetPaths` root override to the opened project root
- resets the asset database so it reindexes against the new project

When a project is closed through `ProjectManager::CloseProject()`, the manager currently:

- clears cross-project asset caches
- clears the native script registry
- resets the asset database
- clears the `AssetPaths` root override

### Project Settings

Project settings authoring is surfaced through:

- `File -> Project Settings...`

Implementation:

- `Limitless/Source/Project/ProjectSettings.{h,cpp}`
- editor project settings panel code under `Editor/Source/...`

## Related Files

- `Limitless/Source/Project/ProjectManager.{h,cpp}`
- `Limitless/Source/Project/ProjectDefinition.{h,cpp}`
- `Limitless/Source/Project/ProjectSettings.{h,cpp}`
- `Limitless/Source/Project/BuildSettings.{h,cpp}`
- `Limitless/Source/Project/BuildTargetsSettings.{h,cpp}`

