# Asset System Guide (Unity-style)

This document describes the **Unity-style asset system** in LimitlessRemaster:

- `Assets/` project folder contains source assets (textures, shaders, scenes, etc.)
- Each asset has a sidecar **`.meta`** file containing a stable **GUID**
- Runtime code references assets using **GUID-based handles** (`AssetHandle<T>`)

## Goals

- Stable identity (GUID) independent of file paths.
- Central cache with weak ownership (unused assets can be reclaimed naturally).
- Async loading that does **CPU work off-thread** and **GPU work on the render thread**.

## Core Concepts

### Asset Key vs GUID

- **Key**: typically the asset’s project-relative path (example: `Assets/Textures/Checker.ppm`)
- **GUID**: stable identifier stored in `Assets/Textures/Checker.ppm.meta`

The `.meta` file is created automatically when you first load an asset through the asset system.

### Working directory independence

Unity-style keys like `Assets/...` are resolved by walking up from the current working directory until a directory containing an `Assets/` folder is found.

Implementation:

- `Limitless/Source/Assets/AssetPaths.{h,cpp}`

### `Asset`

Base class for runtime assets.

- File: `Limitless/Source/Assets/Asset.h`
- Properties: `GetKey()`, `GetGuid()`

### `AssetHandle<T>`

Lightweight reference that stores only:

- the GUID (persistent)
- a weak cached pointer (non-owning)

File: `Limitless/Source/Assets/AssetHandle.h`

#### Serialization

`AssetHandle<T>` serializes to JSON as:

- `{ "guid": "..." }`

This is the format we’ll use for scenes/prefabs so references survive file moves/renames.

### `AssetManager`

Global cache that stores weak references:

- key → weak_ptr
- guid → weak_ptr

File: `Limitless/Source/Assets/AssetManager.{h,cpp}`

#### Generic Load API

Instead of calling `TextureAsset::LoadAsync(...)` directly, game/editor code should call:

- `Assets::AssetManager::LoadAsync<Assets::TextureAsset>("Assets/...")`

Important properties:

- Loading happens **outside locks**
- Commit happens under a **write lock**
- GUID de-duplication prevents duplicate GPU resources for the same asset identity

### `AssetDatabase` (P0)

`AssetDatabase` is the persistent manifest that tracks GUID ↔ key/path ↔ type ↔ importer settings ↔ dependencies.

- File: `Limitless/Source/Assets/AssetDatabase.{h,cpp}`
- Storage: `Build/AssetDatabase.json` (under project root)

## Asset Hot Reload (Unity-style)

Assets support hot reload for **source file changes** under `Assets/`.

High-level behavior:

- The engine watches the **entire** `Assets/` tree (single watcher) instead of one watcher per file.
- Change events are **debounced/coalesced** to avoid re-import storms during rapid saves.
- Reload cascades through dependencies tracked in `AssetDatabase` and `.meta` files (dependency graph).
- Async loads can be cancelled via `AssetLoadCoordinator` (generation-based cancellation).

Key files:

- `Limitless/Source/Assets/AssetTreeWatcher.{h,cpp}`
- `Limitless/Source/Assets/AssetHotReloadManager.{h,cpp}`
- `Limitless/Source/Assets/AssetLoadCoordinator.{h,cpp}`

## P1 Assets

### `MaterialAsset`

Minimal Unity-style material stored as JSON:

- File: `Limitless/Source/Assets/MaterialAsset.{h,cpp}`
- Importer: `Limitless/Source/Assets/MaterialAssetImporter.h`
- Example: `Assets/Materials/TexturedTriangle.material.json`

The material references other assets via `{ "guid": "..." }` (preferred) or `{ "key": "Assets/..." }` (convenience). When loaded, it writes dependency GUIDs into the database and `.meta`, enabling cascading hot reload.

Materials can also specify runtime texture sampling overrides (e.g., nearest filtering for crisp pixel art).
This is applied at bind time using a render command (`SetTextureSpecificationCommand`) so it is safe for the render thread.

### `InputActionsAssetResource`

Unity-style input actions as a first-class Asset:

- File: `Limitless/Source/Assets/InputActionsAssetResource.{h,cpp}`
- Importer: `Limitless/Source/Assets/InputActionsAssetImporter.h`
- Example: `Assets/InputActions/Sandbox.inputactions.json`

To set project-wide actions from a key:

- `InputSystem::SetProjectActionAssetFromKey("Assets/InputActions/Sandbox.inputactions.json")`

### `.meta` utilities (`AssetUtils`)

File: `Limitless/Source/Assets/AssetUtils.{h,cpp}`

- `LoadOrCreateGuid(assetPath)` creates/reads `<assetPath>.meta`
- `WriteDependencies(assetPath, deps)` writes a `deps` array in the meta for future hot-reload cascades

## Async Loading Model (Correctness Contract)

### CPU work vs GPU work

For large assets, decoding/parsing should not run on the render thread.

The recommended pattern is:

- **CPU stage**: run on AsyncIO worker threads (`Async::GetAsyncIO().RunAsync(...)`)
- **GPU stage**: enqueue onto render thread (`Texture2D::CreateFromRGBA8Async(...)`, resource queue)

### `TextureAsset` example

File: `Limitless/Source/Assets/TextureAsset.{h,cpp}`

`TextureAsset::LoadAsync()` does:

- Decode image to RGBA8 on an AsyncIO worker thread (stb_image)
- Upload RGBA8 to GPU on render thread via `Texture2D::CreateFromRGBA8Async`

## Sandbox Proof

The Sandbox demo now loads a **material asset** (and the material pulls in its shader + texture deps):

- `Assets/Materials/TexturedTriangle.material.json`

The material references:

- `Assets/Shaders/TexturedTriangle.glsl`
- `Assets/Textures/Checker.ppm`

and the asset system will create/update `.meta` files and dependency entries so shader/texture edits can cascade and hot reload correctly.

Related files:

- `Assets/Textures/Checker.ppm`
- `Assets/Materials/TexturedTriangle.material.json`
- `Sandbox/Source/TexturedTriangleDemo.{h,cpp}`

