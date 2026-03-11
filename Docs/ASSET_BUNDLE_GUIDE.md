# Asset Bundle Guide (Built Game Assets)

This project supports **Unity-style assets** during development (the source `Assets/` tree with `.meta` GUIDs), and also supports building a **runtime `AssetBundle`** so a shipped game can run **without shipping the source `Assets/` files**.

## What is an AssetBundle in Limitless?

- **Manifest**: `AssetBundleManifest.json`
  - Contains a list of entries with:
    - `guid`
    - `key`
    - `type`
    - `payloadFormat`
    - `compression`
    - `uncompressedSize`
    - `contentHash64`
    - `offset`
    - `size`
  - Points to the data file name (`AssetBundle.bin`)
- **Data file**: `AssetBundle.bin`
  - A flat concatenation of stored payload bytes
  - entries may be raw or cooked payloads
  - entries may be uncompressed or compressed

This is still intentionally a simple runtime bundle format, but it is no longer raw-only.

## Building the bundle

The builder scans the project `Assets/` directory, imports known asset types into `AssetDatabase`, and writes the bundle to:

- `Build/AssetBundle/AssetBundleManifest.json`
- `Build/AssetBundle/AssetBundle.bin`

The build entry point is:

- `Limitless::Assets::AssetBundleBuilder::BuildProjectAssetBundle()`

There is also an explicit output-directory path:

- `Limitless::Assets::AssetBundleBuilder::BuildAssetBundleToDirectory(outputDirectory, settings)`

## Current Build Features

### Known asset discovery

The builder currently bundles a known set of asset types, including:

- textures
- shaders
- materials
- scenes
- prefabs
- tilemaps / tilesets / tiles / tile palettes
- animation clips
- animator controllers
- audio mixers
- input actions
- audio clips

### Cooked payload support

The bundle is no longer limited to raw source bytes.

Current cooked payload formats include:

- `CookedTexture2D`
- `CookedShaderStages`

Current behavior:

- textures attempt decode + cook first, then fall back to raw source bytes if cooking fails
- shaders attempt parse + cook first, then fall back to raw source bytes if cooking fails
- other asset types currently remain raw payloads

### Compression

The builder currently supports:

- `None`
- `Zstd`

If Zstd is requested but not available in the current build, the builder falls back to uncompressed payloads.

### Incremental build cache

The builder now uses an incremental cache under the bundle output directory.

Current behavior:

- content hashes are computed from source bytes, importer settings, and cooking version
- cached stored blobs are reused when content/settings/compression still match
- cache hits and misses are logged after the build

### Extra bundled data

The builder also includes supporting files needed by shipped/runtime builds:

- texture `.meta` files for bundled textures
- project settings JSON files under `Project/Settings/`, currently:
  - `RenderSettings.json`
  - `AudioSettings.json`
  - `InputSettings.json`
  - `Layers.json`
  - `Physics2DSettings.json`
  - `Lighting2DSettings.json`

## Runtime loading

At runtime:

- `Limitless::Assets::AssetBundle::LoadFromProjectBuildOutput()` loads the bundle from the default project build output path.
- `Limitless::Assets::AssetBundle::LoadFromExecutableDirectory()` loads the bundle from the packaged layout next to the executable.
- `Limitless::Assets::AssetBundle::Enable(true)` enables bundle-first loading.

When enabled, supported asset loaders will attempt to read from the bundle **before** reading from disk.

At application startup, the runtime also attempts auto-load in this order:

1. `<exeDir>/AssetBundle`
2. `<workingDir>/AssetBundle`

If auto-load succeeds:

- bundle loading is enabled automatically
- asset hot reload is disabled for that session

## Runtime startup behavior

At startup:

- `Application::InternalInitialize()` probes for `AssetBundle/AssetBundleManifest.json`
- the probe order is executable directory first, then working directory
- if a bundle is found, bundle loading is enabled automatically
- asset hot reload is disabled for that session

When the game layer boots, runtime project settings can then be resolved from bundled JSON such as:

- `Project/Settings/RenderSettings.json`
- `Project/Settings/AudioSettings.json`
- `Project/Settings/InputSettings.json`
- `Project/Settings/Lighting2DSettings.json`

Typical logs include:

- `AssetBundle: loaded ... entries ...`
- `AssetBundle: enabled (auto-loaded).`

## Shipping layout (recommended)

For a packaged game, copy the bundle folder next to the executable, for example:

- `Game.exe`
- `AssetBundle/AssetBundleManifest.json`
- `AssetBundle/AssetBundle.bin`
- `AssetBundle/Cache/...` is optional build/cache data and is not part of the required runtime load contract

Runtime will also **auto-enable** the bundle at startup if it finds this layout next to the executable.

## Manifest / Entry Notes

Important current manifest details:

- manifest `version` is currently `2`
- `dataFile` is currently `AssetBundle.bin`
- entry `size` is the stored payload size
- entry `uncompressedSize` is the original payload size before compression
- `payloadFormat` tells runtime loaders whether the payload is raw or cooked
- `compression` tells runtime readers whether decompression is required

## Current limitations / future work

- Asset discovery currently targets a known set of extensions/types.
- Only some asset types currently use cooked payload formats.
- Texture/shader cooking still falls back to raw payloads when cooking fails.
- There is not yet full per-type cooked coverage across all asset classes.
- Platform-specific cooking coverage is still limited compared with a full offline content pipeline.

Still-future areas include:

- broader cooked-format coverage for more asset types
- more advanced platform-specific cooking/optimization
- further bundle-format evolution beyond the current manifest/data-file model

