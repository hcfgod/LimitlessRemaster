# Editor Architecture Overview

This document describes how editor responsibilities are split across the `Editor/Source` modules.

## Goals

- Keep `EditorLayer` small and easy to reason about.
- Isolate panel-specific behavior in dedicated files.
- Make Play Mode and runtime lifecycle reusable and testable.

## High-Level Flow

- `EditorLayer` owns top-level editor state.
- `EditorLayer::OnRender()` calls panel draw modules in a fixed order.
- `EditorLayer::OnAttach()` and `EditorLayer::OnDetach()` delegate lifecycle operations.

## Module Responsibilities

- `EditorLayer`
  - Coordinates frame order and shared state ownership.
  - Routes callbacks into Play Mode and runtime modules.

- `EditorMenuBar`
  - Draws main menu and Play/Stop/Pause controls.
  - Uses callbacks from `EditorLayer` for state transitions.

- `EditorViewportPanel`
  - Draws viewport image and overlays.
  - Triggers viewport framebuffer creation/resizing through a callback.

- `EditorScenePanel`
  - Draws scene hierarchy entity tree.
  - Handles entity selection changes.

- `EditorInspectorPanel`
  - Draws selected entity inspector and texture inspector.
  - Applies texture settings and invalidates sprite texture caches.

- `EditorProjectPanel`
  - Draws asset tree and project folder actions.
  - Handles drag/drop moves and create/rename/delete folder popups.
  - Stores popup UI state in `EditorProjectPanelState`.

- `EditorPlayMode`
  - Handles Enter/Exit/TogglePause Play Mode state transitions.
  - Clones edit scene for runtime simulation and manages active camera switching.

- `EditorRuntimeOperations`
  - Handles attach/detach lifecycle bootstrapping and teardown.
  - Handles per-frame editor camera input update.
  - Handles window resize and viewport framebuffer synchronization.

- `ProjectAssetOperations`
  - Provides file-system operations used by the Project panel.
  - Keeps disk-side asset move/create/rename/delete code outside UI files.

## Extension Guidance

- New panel UI should get its own `Editor<PanelName>` module.
- Shared editor logic should live in a focused helper module, not in `EditorLayer`.
- Keep `EditorLayer` focused on orchestration and cross-module state wiring.
