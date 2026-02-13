## Editor Play Mode (Play / Pause / Stop)

This editor supports a Unity-style runtime loop control:

- **Play**: Enter Play Mode by cloning the current edit scene.
- **Pause**: Freeze the Play Mode state (rendering continues).
- **Stop**: Exit Play Mode and restore the original edit-scene instance.

### Goals

- **Non-destructive editing**: Changes made while playing should not permanently modify the edit scene.
- **Fast iteration**: Switching modes should be instant and avoid disk I/O.
- **Deterministic restore**: Stop returns you to *exactly* the edit scene you had before Play.

### Current Implementation

The toolbar lives in `EditorLayer::DrawMenuBar()` and is driven by an internal state:

- `Edit`: normal editor mode (editable scene)
- `Play`: runtime clone is active
- `Pause`: runtime clone remains active but is paused

When entering Play Mode, the editor:

1. Moves the edit-scene instance into storage (`m_EditSceneStored`)
2. Creates a Play Mode clone (`CloneSceneForPlayMode`)
3. Uses the clone as the active scene for rendering/inspection

When stopping, the editor restores the stored edit-scene instance.

### Scene Cloning Coverage

Play Mode uses `Scene::Clone()` (see `Limitless/Source/Scene/Scene.cpp`). The clone copies all of the following so that the runtime scene matches the edit scene:

**Components copied (per entity):**

- **TagComponent** — entity name
- **TransformComponent** — position, rotation, scale
- **SpriteComponent** — texture key, color; runtime cache is cleared so textures reload in Play Mode
- **MaterialComponent** — material key; runtime cache cleared
- **TextComponent** — text, font path, size, color; font cache cleared
- **CameraComponent** — projection type, primary flag, zoom, planes, FOV (full copy)
- **AudioSourceComponent** — clip key, volume, play-on-start, loop, muted; runtime voice state reset
- **NativeScriptComponent** — all script entries (class name, asset path, enabled, exposed properties); runtime instances are not copied (scripts are re-instantiated when the clone runs)

**Hierarchy:**

- **HierarchyComponent** — parent and sibling order are copied; parent references are remapped to the cloned entities so the hierarchy structure is preserved in Play Mode.

**Scene-level:**

- **Editor camera bookmark** — stored so returning to the scene can restore the same editor view.

When you add new component types that should exist at runtime, extend `Scene::Clone()` to copy them (and clear or re-init any runtime-only state).

