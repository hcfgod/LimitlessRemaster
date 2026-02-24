# Asset Versioning and Migration

This document describes how **asset format versioning** and **migration** work in LimitlessRemaster, and how to extend them beyond the built-in scene version.

## Scene version (built-in)

Scenes use a **serialization version** to support format evolution and migrations:

- **Constant**: `Limitless::kSceneSerializationVersion` (in `Scene.h`) is the current format version.
- **Save**: The scene JSON includes `"Version": <current>` (see `SceneSerializationSave.cpp`).
- **Load**: `Scene::LoadFromFile` reads the `"Version"` field from JSON (default `0` if missing).
- **Migration**: When `loadedVersion < kSceneSerializationVersion`, the loader runs **migrations** (e.g. `RunPostLoadSliderMigration`) to bring old data up to the current format. New scenes are saved at the current version and skip migrations.

Adding a new migration when you change the scene format:

1. Bump `kSceneSerializationVersion` in `Scene.h`.
2. In `SceneSerializationLoad.cpp`, add a migration function (e.g. `RunPostLoadXxxMigration(Scene* scene)`) that fixes data for older versions.
3. In `Scene::LoadFromFile`, call the new migration when `loadedVersion < kSceneSerializationVersion` (or gate it by version range if the migration applies only to a specific old version).

## Other JSON assets (manual pattern)

There is no global asset-version registry. For **other JSON-backed assets** (prefabs, input actions, materials, etc.) you can follow the same pattern:

1. **Define a version constant** for the asset type (e.g. `kPrefabSerializationVersion`, `kInputActionsVersion`).
2. **Write a version field** when saving (e.g. `root["version"] = kXxxVersion`).
3. **Read the version** when loading; if missing, treat as an old format (e.g. version `0`).
4. **Run migrations** when `loadedVersion < currentVersion`: update the in-memory structure or the parsed JSON before building the runtime asset. Optionally **re-save** the asset after migration so the file is updated (editor workflow).

Example (pseudocode) for a hypothetical asset:

```cpp
constexpr int kMyAssetVersion = 2;

void LoadMyAsset(const nlohmann::json& root) {
    int loaded = root.value("version", 0);
    nlohmann::json data = root;
    if (loaded < 2) MigrateV1ToV2(data);
    if (loaded < 1) MigrateV0ToV1(data);
    // ... build asset from data
}
```

## Loading progress and LoadingScreen

Async loading and load progress are centralized for a **single "loading screen" API**:

- **AssetLoadProgress** (`Assets/AssetLoadProgress.h`): Loaders call `SetProgress(key, progress, status)` during async work; call `ClearProgress(key)` when done. UI (or the LoadingScreen) queries `GetProgress(key)` and `GetActiveKeys()`.
- **LoadingScreen** (`Assets/LoadingScreen.h`): Aggregates:
  - All active `AssetLoadProgress` entries
  - Optional **scene load state** (scene loading, scene objects init, physics init)
  - Optional **default shader** readiness (e.g. `Renderer2D::IsShaderReady()` and progress for `Renderer2D::GetDefaultShaderKey()`)

Usage:

- **BuildContext**: `LoadingScreen::BuildContext(scene, Renderer2D::IsShaderReady(), Renderer2D::GetDefaultShaderKey())` (or pass `nullptr` for scene/key if not applicable).
- **GetState**: `LoadingScreen::State state = LoadingScreen::GetState(ctx);` then use `state.IsLoading`, `state.Progress` (0–1), and `state.StatusText` to drive a loading overlay or blocking screen.

The editor viewport uses this to draw a single loading overlay for both scene loading and shader compilation. Game code can use the same API for a unified loading screen.

## Files

- `Limitless/Source/Scene/Scene.h` — `kSceneSerializationVersion`
- `Limitless/Source/Scene/SceneSerializationLoad.cpp` — scene load, version read, migrations
- `Limitless/Source/Scene/SceneSerializationSave.cpp` — scene save, version write
- `Limitless/Source/Assets/AssetLoadProgress.{h,cpp}` — per-asset progress registry
- `Limitless/Source/Assets/LoadingScreen.{h,cpp}` — aggregated loading state API
