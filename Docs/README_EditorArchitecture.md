# Editor Architecture Overview

This document describes how editor responsibilities are currently split across `Editor/Source` and how state flows through the modern editor.

## Goals

- Keep `EditorLayer` focused on orchestration, not panel implementation details.
- Split large editor subsystems into feature-oriented translation units.
- Persist editor UX state across restarts without coupling it to transient runtime state.
- Keep asset browser, play mode, prefab mode, and project/runtime bootstrapping isolated enough to evolve independently.

## High-Level Flow

- `EditorLayer` owns top-level editor state, open-scene identity, selection, play-mode state, layout/session state, and cross-panel callbacks.
- `EditorLayer::OnAttach()` / `OnDetach()` handle editor bootstrapping and shutdown.
- `EditorLayer::OnUpdate()` advances editor/runtime state, scene loading, asset prewarm, and pending play-mode transitions.
- `EditorLayer::OnRender()` draws the menu bar and active panels in a fixed order.

## Top-Level `EditorLayer` Split

The editor is no longer centered around one monolithic `EditorLayer.cpp`. Current orchestration is split into focused modules under `Editor/Source/Layer/`:

- `EditorLayer.cpp`
  - Core state definitions, session-state serialization helpers, and cross-feature glue.

- `EditorLayerMenu.cpp`
  - Main menu callbacks.
  - Layout menu integration.
  - Play / Simulate / Stop / Pause requests.

- `EditorLayerSceneLifecycle.cpp`
  - New/open/save/load scene flow.
  - Play mode and Simulate mode transitions.
  - Prefab-mode return handling.
  - Runtime scene activation / unloading while playing.

- `EditorLayerProjectPanel.cpp`
  - Bridges `EditorProjectPanel` callbacks into scene loading, prefab editing, asset rename propagation, and build-settings maintenance.

- `EditorLayerProjectRuntime.cpp`
  - Open-project bootstrapping and scene loading progress/state updates.
  - Runtime/editor project synchronization helpers.

- `EditorLayerPrefabAssets.cpp`
  - Prefab editing workflow, prefab stage transitions, and apply-to-instances behavior.

- `EditorLayerSceneUtilities.cpp`
  - Shared scene/layout helpers and scene utility functions reused across lifecycle/orchestration code.

## Core Editor Modules

- `EditorMenuBar`
  - Draws `File`, `Edit`, `Assets`, `Tools`, `View`, `Window`, `Layouts`, and `Help`.
  - Exposes `Play`, `Simulate`, `Stop`, and `Pause/Resume`.
  - Hosts layout save/load/delete/reset entry points.

- `EditorProjectDialog`
  - Handles project open/create dialogs.

- `EditorLayoutManager`
  - Owns saved layout discovery, save/load/delete, and layout metadata persistence.
  - Default working layout is copied into `Project/Settings/EditorLayout.current.ini`.
  - Saved custom layouts live under the user data root in `Editor/Layouts/Saved/<layout>/`.

- `EditorPanelStyle`
  - Provides the shared dark rounded ImGui styling used by editor panels.
  - Viewports are intentionally excluded because rendered content uses different padding/spacing requirements.

## Panel Responsibilities

- `EditorScenePanel`
  - Hierarchy tree, entity selection, multi-selection, rename/delete, and draw-order views.

- `EditorInspectorPanel`
  - Selected-entity inspector and asset inspectors.
  - Script attachment/editing surfaces.
  - Component-specific authoring UIs.
  - Foldout persistence keyed by scene/entity/component path.

- `EditorProjectPanel`
  - Unity-style project browser with:
    - folder tree pane
    - right-side grid browser
    - compact list mode at grid scale `0.0`
    - search/filtering
    - drag/drop moves
    - context menus and background create menus
    - native and managed script asset handling

- `EditorViewportPanel`
  - Scene View / Game View image presentation and overlays.
  - Viewport resize handling and drag/drop scene activation from the Project browser.

- `EditorBuildSettingsPanel`
  - Ordered build-scene list, target platform/backend/execution settings, and asynchronous build execution UI.

- Animation / authoring panels
  - `EditorAnimationTimelinePanel`
  - `EditorAnimatorGraphPanel`
  - `EditorTilePalettePanel`
  - `EditorSpriteEditor`

- Diagnostics / tooling panels
  - asset diagnostics
  - console
  - physics diagnostics
  - performance overlay/panel

## `EditorProjectPanel` Internal Split

The Project browser is itself split into focused files under `Editor/Source/Panels/`:

- `EditorProjectPanel.cpp`
  - Top-level orchestration and popup dispatch.

- `EditorProjectPanelCache.cpp`
  - Directory scanning, asset classification, cache invalidation, thumbnail/material preview helpers.

- `EditorProjectPanelSearch.cpp`
  - Search filter state and recursive match caching.

- `EditorProjectPanelNavigation.cpp`
  - Folder tree, breadcrumbs, browser-region layout, create menus, and grid/list assembly.

- `EditorProjectPanelSelection.cpp`
  - Multi-selection, activation, drag/drop payloads, delete handling, and context-menu actions.

- `EditorProjectPanelPopups.cpp`
  - Create/rename modal popups for folders, assets, native scripts, managed scripts, and authored asset types.

- `EditorProjectPanelShared.h`
  - Shared internal structs/constants/helpers.

## Play Mode and Runtime Authoring Model

- Edit mode still centers on one authoring scene at a time.
- Entering `Play` or `Simulate` clones the edit scene into runtime state.
- While playing, the editor now uses `SceneCollection`-style runtime scene ownership so gameplay scene loads can activate, unload, or additively load more than one runtime scene.
- Exiting play mode destroys runtime scenes and restores the stored edit scene identity and selection.

See `Docs/EDITOR_PLAY_MODE_GUIDE.md` for details.

## Persistence

Per-project editor session state lives in:

- `Project/Settings/EditorSessionState.json`

This stores, among other things:

- last opened scene asset key
- active layout name
- panel/window visibility
- Project browser active folder / grid scale / folder expansion
- inspector foldout state
- native script editor session state

The current working ImGui layout lives in:

- `Project/Settings/EditorLayout.current.ini`

Saved named layouts are user-level data, not project files.

## Extension Guidance

- New user-facing editor features should usually add a focused module or panel file instead of expanding `EditorLayer`.
- Keep file-system mutation code out of draw functions when possible.
- Prefer callback wiring from `EditorLayer` into focused modules rather than letting panels reach into global editor state directly.
- If a feature needs persistence, decide explicitly whether it is:
  - project-scoped (`Project/Settings/...`)
  - user-scoped (editor user data)
  - transient runtime/editor state only
