# Asset Bundle Guide (Built Game Assets)

This project supports **Unity-style assets** during development (the source `Assets/` tree with `.meta` GUIDs), and also supports building a **runtime `AssetBundle`** so a shipped game can run **without shipping the source `Assets/` files**.

## What is an AssetBundle in Limitless?

- **Manifest**: `AssetBundleManifest.json`
  - Contains a list of entries: `{ guid, key, type, offset, size }`
  - Points to the data file name (`AssetBundle.bin`)
- **Data file**: `AssetBundle.bin`
  - A flat concatenation of raw asset bytes (textures, shaders, JSON assets, etc.)

This is a *packaging layer* (raw bytes), not a fully cooked/optimized format yet. It’s designed to be simple, robust, and easy to iterate on.

## Building the bundle

The builder scans the project `Assets/` directory, imports known asset types into `AssetDatabase`, and writes the bundle to:

- `Build/AssetBundle/AssetBundleManifest.json`
- `Build/AssetBundle/AssetBundle.bin`

The build entry point is:

- `Limitless::Assets::AssetBundleBuilder::BuildProjectAssetBundle()`

## Runtime loading

At runtime:

- `Limitless::Assets::AssetBundle::LoadFromProjectBuildOutput()` loads the bundle from the default project build output path.
- `Limitless::Assets::AssetBundle::LoadFromExecutableDirectory()` loads the bundle from the packaged layout next to the executable.
- `Limitless::Assets::AssetBundle::Enable(true)` enables bundle-first loading.

When enabled, supported asset loaders will attempt to read from the bundle **before** reading from disk.

## Sandbox test (button press)

In Sandbox:

- Press **B** to:
  - Build the bundle
  - Load it (from the executable directory layout)
  - Toggle bundle loading **on/off**
  - Reinitialize the demo material + input actions to prove it works

You should see logs like:

- `AssetBundleBuilder: built bundle ...`
- `AssetBundle: loaded ... entries ...`
- `AssetBundle enabled (loading assets from bundle).`

## Shipping layout (recommended)

For a packaged game, copy the bundle folder next to the executable, for example:

- `Game.exe`
- `AssetBundle/AssetBundleManifest.json`
- `AssetBundle/AssetBundle.bin`

Sandbox will also **auto-enable** the bundle at startup if it finds this layout next to the executable.

## Current limitations / next steps

- No compression (yet).
- No per-type cooked formats (yet) — textures are still decoded at runtime, shaders still compile at runtime.
- Asset discovery currently targets a known set of extensions/types.
- Future: incremental builds (hashing), compression, cooked formats, and platform-specific shader/texture cooking.

