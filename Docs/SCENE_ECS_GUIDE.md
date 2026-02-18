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
| `NativeScriptComponent` | List of native C++ script entries (class, asset path, exposed properties); see `Docs/NATIVE_CPP_SCRIPTING_GUIDE.md` |

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
3. If the component should be cloned when entering Play Mode, add copying logic in `Scene::Clone()` (`Limitless/Source/Scene/Scene.cpp`).
4. Add Inspector UI in `EditorInspectorPanel::Draw` (or a helper used by it) so the component can be edited in the editor.

## Extending

- **Hierarchy**: Use `HierarchyComponent` with a parent `entt::entity`. Recursively build world matrices for child transforms.
- **Serialization**: Use EnTT's snapshot/continuous loader or custom JSON serialization for save/load.
- **Systems**: Add update loops that iterate views (e.g. physics, AI) in `OnUpdate` or a dedicated system scheduler.
