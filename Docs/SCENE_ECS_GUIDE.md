# Scene / Entity-Component System (ECS)

The Limitless engine uses the [EnTT](https://github.com/skypjack/entt) library for its Entity-Component System, similar to Unity's GameObject/Component model but data-oriented.

## Overview

- **Scene** – Owns an `entt::registry` and manages entity lifecycle.
- **Entity** – Lightweight handle (`entt::entity`) referring to a logical object.
- **Components** – Plain data structs attached to entities (e.g. `TransformComponent`, `SpriteComponent`).

## Core Components

| Component | Purpose |
|-----------|---------|
| `TagComponent` | Display name for the hierarchy (required for all entities) |
| `TransformComponent` | Position, rotation (euler degrees), scale (added to all entities by default) |
| `HierarchyComponent` | Parent entity for hierarchy (optional; `entt::null` = root) |
| `SpriteComponent` | Renders a 2D sprite (color + optional texture); size from TransformComponent::Scale |
| `MaterialComponent` | Optional material asset; overrides Sprite defaults when set |
| `UIPanelComponent` | Canvas UI panel background (solid color, optional sprite-backed fill, raycast metadata) |
| `UITextComponent` | Canvas UI text payload (text, font path, size, color, raycast target) |
| `CameraComponent` | Gameplay camera (orthographic 2D or perspective 3D; primary flag for active camera) |
| `AudioSourceComponent` | Plays an AudioClip asset (volume, pitch, play-on-start, loop, mute) |
| `NativeScriptComponent` | List of native C++ script entries (class, asset path, exposed properties, parallel execution policy/access masks); see `Docs/NATIVE_CPP_SCRIPTING_GUIDE.md` |

## Runtime Phases (Jobified Frame Model)

Scene runtime work is staged into explicit phases to keep structural operations deterministic while still allowing worker parallelism:

- `Structural`
- `ScriptMainThread`
- `ScriptParallel`
- `Simulation`
- `Transform`
- `RenderBuild`
- `Idle`

High-level flow during runtime:

1. Flush deferred structural mutations in `Structural`.
2. Run scripts (main-thread first, then compatible `ParallelSafe` batches in `ScriptParallel`).
3. Run simulation systems (scheduler-compatible barriers).
4. Solve transforms in depth batches.
5. Build render commands.

## Structural Mutation Safety

Structural operations include create/destroy entity, component add/remove, and hierarchy edits (`SetParent`, sibling order changes).

In parallel/runtime-sensitive contexts these are deferred through a lock-free MPMC enqueue path and applied in the single-threaded structural phase. This avoids registry structural races while preserving authored script ergonomics.

See config rollout keys in `Docs/CONFIGURATION_GUIDE.md`:

- `ecs.mt.defer_structural_mutations`
- `ecs.mt.validate_structural_phase`

## Parallel Script Compatibility

`NativeScriptEntry` supports opt-in parallel execution and compatibility metadata:

- `ExecutionPolicy` (`MainThread` or `ParallelSafe`)
- `DeclaredReadAccessMask`
- `DeclaredWriteAccessMask`

Parallel script slots are batched by access compatibility (read/write hazards force barriers). Missing declarations can be configured to either:

- fall back to main-thread execution (strict mode), or
- run with conservative barriers.

See:

- `ecs.mt.enable_parallel_scripts`
- `ecs.mt.require_parallel_script_access_declarations`
- `ecs.mt.warn_implicit_parallel_script_access`
- `ecs.mt.validate_parallel_script_access_masks`
- `ecs.mt.warn_parallel_script_access_mismatch`
- `ecs.mt.enable_system_scheduler`

When `ecs.mt.validate_parallel_script_access_masks` is enabled, runtime validation currently tracks observed writes for:

- `Transform`
- `Hierarchy`
- `Rigidbody2D`
- `BoxCollider2D`
- `CircleCollider2D`
- `Joint2D`
- `Animator` (including `AnimationEventReceiver`)
- `ParticleEmitter`
- `Rendering2D` domain (`Sprite`, `Material`)
- `Lighting2D` domain (`DirectionalLight2D`, `PointLight2D`, `ShadowOccluder2D`)
- `UI` domain (`Canvas`, `RectTransform`, `UIImage`, `UIPanel`, `UIText`, `UIButton`, `UISlider`)
- `Audio` domain (`AudioListener2D`, `AudioSource`)
- `Camera` domain (`Camera`)
- `Tilemap` domain (`Grid2D`, `TilemapLayer`)
- `Metadata` domain (`Tag`, `PrefabInstance`)

## Parallel Physics World Stepping

Physics stepping uses split phases:

1. `PrepareForStep` (sequential, may touch ECS registry)
2. `StepWorldOnly` (`b2World_Step`, parallel across independent worlds)
3. `SyncAfterStep` (sequential ECS sync)

See:

- `ecs.mt.enable_parallel_physics_world_step`

## Depth-Batched Transform Solve

Hierarchy updates are solved using cached depth values (`HierarchyComponent::HierarchyDepth`) and depth barriers:

- Roots at depth 0 are solved first.
- Children at depth `n+1` run only after depth `n` completes.
- Within a depth, transform updates can run in parallel.

This preserves parent-before-child correctness while scaling wide hierarchies.

See:

- `ecs.mt.enable_parallel_transforms`

## Explicit Non-Goals (Current Milestone)

- No arbitrary concurrent EnTT structural mutation.
- No concurrent in-step mutation within the same Box2D world.
- No per-component transform mutexes or atomic matrix writes.

## Usage

### Creating a Scene

```cpp
auto scene = std::make_unique<Scene>();
```

### Creating Entities

```cpp
entt::entity entity = scene->CreateEntity("My Sprite");
auto& transform = scene->GetRegistry().get<TransformComponent>(entity);
transform.Position = glm::vec3(0.0f, 0.0f, 0.0f);
transform.Scale = glm::vec3(1.0f, 1.0f, 1.0f);
auto& sprite = scene->GetRegistry().emplace<SpriteComponent>(entity);
sprite.Color = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
```

### Rendering

```cpp
// Draw to the currently bound framebuffer
SceneRenderer::Render(*scene, camera);

// Or draw to a specific viewport framebuffer (e.g. editor viewport)
SceneRenderer::RenderToViewport(*scene, camera, framebuffer, width, height);
```

### Querying Entities

```cpp
auto view = scene->GetRegistry().view<TransformComponent, SpriteComponent>();
for (entt::entity entity : view)
{
    const auto& transform = view.get<TransformComponent>(entity);
    const auto& sprite = view.get<SpriteComponent>(entity);
    // ...
}
```

## Adding New Components

1. Define a struct in `Limitless/Source/Scene/Components.h`.
2. Register usage in `SceneRenderer::Render` (or custom systems) so the component is drawn or updated.
3. If the component should be cloned when entering Play Mode, add copying logic in `Scene::Clone()` (`Limitless/Source/Scene/SceneClone.cpp`).
4. Add Inspector UI in `EditorInspectorPanel::Draw` (or a helper used by it) so the component can be edited in the editor.

## Scene model and loading

- **One active scene per context.** The editor has a single open scene (optionally with an edit-time copy for Play Mode). The runtime (e.g. `GameLayer`) has a single active scene. There is no first-class **scene streaming** or **additive loading**: you cannot load a second scene “on top of” the current one or stream in chunks while keeping multiple scenes live.
- **Transitions** are **replace**: `SceneManager::LoadScene(sceneIdentifier)` queues a request to replace the current scene with the requested one; the host consumes the request and loads the new scene (see `SceneManager.h` and `Docs/NATIVE_CPP_SCRIPTING_GUIDE.md`). `ReloadCurrentScene()` replaces the current scene with a fresh load of the same asset.
- **Future: additive loading and streaming.** A possible extension would be to support multiple loaded scenes (e.g. a “main” scene plus additively loaded sub-scenes) or streaming (load/unload scene chunks or sectors while one logical level is active). That would require defining how entities from multiple scenes are merged or kept separate, how physics worlds and cameras are assigned, and how scripts refer to “the other” scene. Not implemented today.

## Extending

- **Hierarchy**: Use `HierarchyComponent` with a parent `entt::entity`. Recursively build world matrices for child transforms.
- **Serialization**: Use EnTT's snapshot/continuous loader or custom JSON serialization for save/load.
- **Systems**: Add update loops that iterate views (e.g. physics, AI) in `OnUpdate` or a dedicated system scheduler.
