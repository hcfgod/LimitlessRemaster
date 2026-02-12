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

`CloneSceneForPlayMode` currently copies the built-in components used by the viewport renderer:

- `TagComponent`
- `TransformComponent`
- `SpriteComponent` (runtime cache is cleared)

As new gameplay/runtime components are added, extend the clone function to include them.

