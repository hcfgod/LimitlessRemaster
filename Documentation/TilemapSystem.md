# Tilemap System

## Overview

The tilemap feature adds a grid-based world authoring workflow with runtime rendering and editor painting tools. It is designed to match existing scene/entity workflows:

- Tilemaps are regular ECS components (`TilemapComponent`) on entities.
- Tilemap content serializes directly in scene files and prefab files.
- Tile painting operations are undoable with editor command history.
- Tilemap entities are selectable in Scene hierarchy and editable in Inspector.

## Runtime Data Model

`TilemapComponent` stores:

- `GridSize`: number of cells (`x`, `y`)
- `CellSize`: world-space dimensions of each cell
- `TilesetAssetKey`: optional `.tileset.json` asset reference
- `TilesetTextureKey`: texture asset key used as tile atlas
- `TilesetTileSizePixels`: tile dimensions in the atlas texture
- `AutoTileEnabled`: optional bitmask autotile remapping
- `Layers`: ordered tile layers (`Background`, `Collision`, `Foreground` by default)

Each `TilemapLayer` stores:

- `Name`
- `Visible`
- `CollisionEnabled`
- `RenderOrder`
- `Tiles` (`0` means empty, non-zero means tile id)
- `PerTileData` (optional user data per cell)

## Rendering

Tilemaps render in `SceneRenderer::Render` using the existing `Renderer2D` pipeline.

- Negative `RenderOrder` tilemap layers draw before sprites.
- Zero or positive `RenderOrder` tilemap layers draw after sprites.
- Tilemap cells use UVs derived from `TilesetTileSizePixels`.
- Autotile mode (optional) applies a 4-neighbor bitmask offset (`+0..+15`) per painted base tile.

## Scene Serialization

Scene save/load includes full tilemap payload:

- Grid size
- Cell size
- Tileset texture asset reference
- Tileset tile size
- Auto tile toggle
- Per-layer tile arrays and per-cell user data

Scene format version was incremented to `9` for this addition.

## Editor Workflow

### Inspector

When a selected entity has `TilemapComponent`, Inspector exposes:

- Tileset texture assignment (drag/drop from Project panel)
- Tileset asset assignment (`.tileset.json`) with texture and tile-size defaults
- Grid size / cell size / tile pixel size
- Auto tile toggle
- Layer list (name, visibility, collision flag, render order, add/remove)

### Tilemap Panel

`Tilemap` panel provides paint tool controls:

- Paint enable
- Grid overlay toggle
- Paint mode: `Single`, `Rectangle`, `Fill`, `Erase`
- Brush size
- Active tile id
- Optional custom per-tile data painting
- Active layer selection
- Tile palette preview and tile selection from tileset texture

### Viewport Painting

When the selected entity has a tilemap and painting is enabled:

- Viewport shows tile grid overlay and brush preview
- Painting is grid-aligned using world-to-local conversion
- Modes supported: single brush, rectangle paint, flood fill, erase

Undo/redo is command-based for tile painting:

- One stroke/fill/rectangle = one undo command
- Commands store changed cells (layer, coordinates, previous/new tile and data)

## Prefab Support

Tilemap components are copied with prefab instantiation/apply/revert flows through `EditorPrefabSystem`.

Runtime-only tilemap cache state is reset when cloning/loading.

## Tilemap Physics (2D)

`TilemapCollider2DComponent` can be added to the same entity as `TilemapComponent`.

- Generates static physics colliders from painted solid tiles.
- Uses either:
  - all layers marked `CollisionEnabled`, or
  - one specific layer index.
- `MergeAdjacentTiles` combines neighbors into larger boxes (composite-like optimization).
- Material/filter properties are exposed (`Friction`, `Restitution`, `IsSensor`, `Layer Bits`, `Mask Bits`).

Collider geometry is rebuilt automatically when tile content, grid/cell sizing, layer collision flags, or relevant collider settings change.
