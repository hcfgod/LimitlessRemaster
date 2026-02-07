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

The Sandbox demo now loads a texture from:

- `Assets/Textures/Checker.ppm`

and logs the key + GUID when ready.

Related files:

- `Assets/Textures/Checker.ppm`
- `Sandbox/Source/TexturedTriangleDemo.{h,cpp}`

