# Project System Guide (Unity-grade)

This document describes the **Project System** in LimitlessRemaster: how projects are discovered, opened, created, and configured.

## Goals

- Deterministic project root discovery (no more “which Assets folder did it find?” confusion).
- Unity-style project layout (`Assets/` + per-asset `.meta` GUIDs).
- Project-scoped settings that are source-controlled and portable.
- Editor UX for opening/creating projects and configuring settings.

## Project layout

At the project root:

```
<ProjectRoot>/
  Assets/
  Build/
  Project/
    Project.json
    Settings/
      RenderSettings.json
      AudioSettings.json
      InputSettings.json
      Layers.json
      BuildTargets.json
```

## Project marker file: `Project/Project.json`

`Project/Project.json` is the project **solution marker** (Unity-style). If it exists, the engine prefers it for root discovery.

- **Path**: `<ProjectRoot>/Project/Project.json`
- **Purpose**:
  - Stable project identity (`projectGuid`)
  - Project name
  - Default roots (`Assets`, `Build`)
  - Default scene reference (optional)

Implementation:

- `Limitless/Source/Project/ProjectDefinition.{h,cpp}`
- `Limitless/Source/Project/ProjectManager.{h,cpp}`

## Project root discovery order

Asset key resolution for `Assets/...` relies on project root discovery (`Assets::FindProjectRootFromWorkingDirectory`).

Order:

- Explicit override: `Assets::SetAssetRootDirectory(<ProjectRoot>)`
- Environment override: `LIMITLESS_ASSET_ROOT`
- Marker discovery: walk up for `Project/Project.json`
- Fallback discovery: walk up for an `Assets/` directory

Implementation:

- `Limitless/Source/Assets/AssetPaths.{h,cpp}`

## Editor UX

### Open / Create Project

Use the editor menu:

- **File → Open Project…**
- **File → Create Project…**

Implementation:

- `Editor/Source/EditorProjectDialog.{h,cpp}`

### Recent Projects

The editor stores recent projects in the **user data directory** (never inside the project).

- **Windows path** (example): `%APPDATA%\\Limitless\\Editor\\RecentProjects.json`

Implementation:

- `Editor/Source/EditorRecentProjects.{h,cpp}`

## Project Settings

Project settings are stored under `Project/Settings/` and are intended to be committed to source control.

Editor window:

- **File → Project Settings…**

Implementation:

- `Limitless/Source/Project/ProjectSettings.{h,cpp}`
- `Editor/Source/EditorProjectSettingsPanel.{h,cpp}`

## Build And Run

Build target configuration is stored in:

- `Project/Settings/BuildTargets.json`

Editor window:

- **Window → Build And Run**

Implementation:

- `Limitless/Source/Project/BuildTargetsSettings.{h,cpp}`
- `Editor/Source/EditorBuildAndRunPanel.{h,cpp}`

