# UI System Guide

## Overview

The runtime now supports a Canvas-driven UI workflow:

- `CanvasComponent` defines UI space (`ScreenSpace` or `WorldSpace`), sort order, and reference resolution.
- `RectTransformComponent` defines layout (`AnchorMin`, `AnchorMax`, `Pivot`, `SizeDelta`, `AnchoredPosition`).
- UI elements render in a dedicated UI pass after world rendering.

## Core Components

Minimum UI component set currently available:

- `UIImageComponent`
- `UIPanelComponent`
- `UITextComponent`
- `UIButtonComponent`
- `UISliderComponent`

These components are intended to be composed with:

- `RectTransformComponent`
- `SpriteComponent` (for images)
- `UIPanelComponent` (for solid-color or optional sprite-backed backgrounds)
- `UITextComponent` (for text payload and styling)

## Rendering Model

### Screen Space Canvas

- Uses centered canvas coordinates (`0,0` at center).
- Uses `CanvasComponent::ReferenceResolution` as the root layout area.
- Renders after world content in a dedicated pass.

### World Space Canvas

- Uses the scene camera projection.
- Applies canvas world transform before UI element local layout transform.
- Renders after world content in the same UI stage.

## Important Behavior Change

UI text is now authored through `UITextComponent` only.
UI placement is exclusively driven by `CanvasComponent` + `RectTransformComponent`.

## Recommended Authoring Pattern

1. Add `CanvasComponent` on a root entity.
2. Add `RectTransformComponent` on each child UI entity.
3. Add `UIPanelComponent` for background blocks and container visuals.
4. Add `SpriteComponent` + `UIImageComponent` for image elements.
5. Add `UITextComponent` for text elements.
6. Add `UIButtonComponent` or `UISliderComponent` for interaction/state metadata.

`UIPanelComponent` supports:

- `BackgroundColor` for solid fills.
- `UseSpriteTexture` to optionally use `SpriteComponent::TextureKey` as the panel background.
- `RaycastTarget` metadata for UI tooling parity.

Button and slider visuals now follow Unity-style defaults:

- `UIButtonComponent` exposes state color fields (`NormalColor`, `HoveredColor`, `PressedColor`, `DisabledColor`)
  and `UseStateColors` to tint based on runtime interaction state.
- `UISliderComponent` uses dedicated child entities by default:
  - `Slider Background`
  - `Slider Fill`
  - `Slider Handle`

Each child can have its own `SpriteComponent`/texture assignment for Unity-like customization.
`Slider Fill` and `Slider Handle` are auto-driven from slider value at runtime.

When adding UI components from the inspector (`Canvas`, `UI Image`, `UI Panel`, `UI Text`, `UI Button`, `UI Slider`),
the editor automatically ensures a `RectTransformComponent` exists on the target entity.

## Current Scope

This phase establishes the core data model, serialization, inspector integration, rendering pass separation,
and runtime pointer interaction for screen-space UI (`UIButtonComponent` hover/press/click and `UISliderComponent` drag/value updates).

Interaction state is exposed on the components through runtime flags:

- `UIButtonComponent`: `RuntimeHoverEnteredThisFrame`, `RuntimeHoverExitedThisFrame`, `RuntimePressedThisFrame`, `RuntimeClickedThisFrame`
- `UISliderComponent`: `RuntimeDragging`, `RuntimeValueChangedThisFrame`

These flags are transient gameplay state and are reset as part of runtime update/clone/load safety paths.
