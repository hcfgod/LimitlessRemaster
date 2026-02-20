# Tilemap System

## Overview

The engine uses the new Unity-style tile workflow only:

- `Grid2DComponent` defines grid layout on a parent entity.
- `TilemapLayerComponent` stores painted tile data on child layer entities.
- `TileAsset` (`.tile.json`) represents individual paintable tiles.
- `TilePaletteAsset` (`.tilepalette.json`) stores palette layout and selection source.

The legacy `TilemapComponent` workflow has been removed.

## Runtime Data Model

### `Grid2DComponent`

- `CellSize`: world-space cell size.
- `CellGap`: optional spacing between cells.

### `TilemapLayerComponent`

- `GridSize`: number of cells.
- `RenderOrder`: draw order relative to sprites/layers.
- `CollisionEnabled`: layer collision flag for future collision baking/queries.
- `TileTable`: tile-id-to-`TileAsset` key mapping, where index `0` is always empty.
- `Tiles`: per-cell tile ids (row-major).

## Rendering

`SceneRenderer::Render` draws `Grid2DComponent` hierarchies by iterating child `TilemapLayerComponent` entities:

- Layers are sorted by `RenderOrder`.
- Each painted cell resolves a `TileAsset` and sub-sprite UVs.
- UV orientation matches texture import flip behavior.
- Layer render caches are rebuilt lazily when dirty.

## Scene Serialization

Scene save/load persists:

- `Grid2D` component data.
- `TilemapLayer` component data (`GridSize`, `RenderOrder`, `CollisionEnabled`, `TileTable`, `Tiles`).

For large sparse maps, all-zero `Tiles` arrays are omitted to keep scene files smaller.

## Editor Workflow

### Tile Palette Panel

The `Tile Palette` panel is the single authoring surface for tile painting:

- Select a `TilePaletteAsset`.
- Drag a sliced sprite sheet into the drop zone to populate the palette.
- Single-click to select one tile.
- Drag-select a rectangular stamp for multi-tile painting.
- Choose active grid and layer targets.
- Paint directly in Scene viewport with undo/redo support.

### Viewport Painting

Painting uses `Grid2DComponent` + `TilemapLayerComponent` targets only.

- Supports single/stamp painting and erase.
- Uses stable world-to-grid projection under camera movement/rotation.
- Each mouse stroke is recorded as one undoable command.

## Notes

- Tile Palette assets are standard project assets and persist across editor restarts.
- Legacy tilemap panel/components are no longer part of the supported workflow.
