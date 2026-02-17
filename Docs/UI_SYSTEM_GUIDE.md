# UI System Guide

## Overview

The runtime now supports a Canvas-driven UI workflow:

- `CanvasComponent` defines UI space (`ScreenSpace` or `WorldSpace`), sort order, and reference resolution.
- `RectTransformComponent` defines layout (`AnchorMin`, `AnchorMax`, `Pivot`, `SizeDelta`, `AnchoredPosition`).
- UI elements render in a dedicated UI pass after world rendering.

## Core Components

Minimum UI component set currently available:

- `UIImageComponent`
- `UITextComponent`
- `UIButtonComponent`
- `UISliderComponent`

These components are authoring and scripting hooks and are intended to be composed with:

- `RectTransformComponent`
- `SpriteComponent` (for images)
- `TextComponent` (for text)

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

`TextComponent` no longer owns screen/world render mode or screen-anchor settings.
UI placement is now exclusively driven by `CanvasComponent` + `RectTransformComponent`.

## Recommended Authoring Pattern

1. Add `CanvasComponent` on a root entity.
2. Add `RectTransformComponent` on each child UI entity.
3. Add `SpriteComponent` + `UIImageComponent` for image elements.
4. Add `TextComponent` + `UITextComponent` for text elements.
5. Add `UIButtonComponent` or `UISliderComponent` for interaction/state metadata.

## Current Scope

This phase establishes the core data model, serialization, inspector integration, and rendering pass separation.
Advanced layout groups, automatic event routing, and full runtime UI input dispatch are intended for future iterations.
