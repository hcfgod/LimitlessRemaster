# Asset Hot Reload Guide (Assets/)

This document describes **asset hot reload** for source content under the `Assets/` folder.

> Note: This is separate from configuration hot reload (`config.json`). Configuration hot reload is documented in `Docs/HOT_RELOAD_GUIDE.md`.

## Goals

- Watch the whole `Assets/` tree (single watcher).
- Debounce/coalesce rapid saves (avoid import storms).
- Reload in-place where possible (keep strong asset references alive).
- Cascade reloads through dependencies (Unity-style).
- Allow cancellation when “context changes” (scene change, shutdown, etc.).

## Key Components

### `AssetTreeWatcher`

- Watches the `Assets/` directory recursively.
- Emits file-change events.

Files:

- `Limitless/Source/Assets/AssetTreeWatcher.{h,cpp}`

### `AssetHotReloadManager`

- Owns the watcher and a small debounce/coalesce queue.
- Maps file changes → asset keys → reload calls.
- Uses the dependency graph to cascade reloads.

Files:

- `Limitless/Source/Assets/AssetHotReloadManager.{h,cpp}`

### Dependency Graph (`AssetDatabase` + `.meta`)

Dependencies are tracked in:

- `Build/AssetDatabase.json` (persistent manifest)
- `<asset>.meta` (GUID + `deps` array)

This allows “edit shader → reload material” style cascades.

Files:

- `Limitless/Source/Assets/AssetDatabase.{h,cpp}`
- `Limitless/Source/Assets/AssetUtils.{h,cpp}`

### Cancellation (`AssetLoadCoordinator`)

Async loads are generation-based:

- New “generation” cancels in-flight tasks (they return null / no-op).
- Used during shutdown and can be used during scene changes.

Files:

- `Limitless/Source/Assets/AssetLoadCoordinator.{h,cpp}`

## Correctness Contract (Important)

- CPU work (file IO, parsing, decoding) runs on `AsyncIO` threads.
- GPU work (texture upload, shader compile) runs on the render thread.
- `Reload()` implementations should prefer **in-place rebuild** (swap internal GPU pointers) so clients holding `shared_ptr<Asset>` keep working.

## Troubleshooting

### “Logs say reloaded but visuals don’t change”

Usually means the caller holds raw GPU objects instead of asset refs.

Correct pattern:

- Keep strong refs to `ShaderAsset` / `TextureAsset` / `MaterialAsset`.
- Ask the asset for the current GPU resource each frame (or detect internal pointer changes).

