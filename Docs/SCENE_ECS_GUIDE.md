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
SceneRenderer::Render(*scene, camera);
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

1. Define a struct in `Source/Scene/Components.h`.
2. Register usage in `SceneRenderer::Render` or custom systems.
3. Add Inspector UI in `EditorLayer::DrawInspectorPanel` if needed.

## Extending

- **Hierarchy**: Use `HierarchyComponent` with a parent `entt::entity`. Recursively build world matrices for child transforms.
- **Serialization**: Use EnTT's snapshot/continuous loader or custom JSON serialization for save/load.
- **Systems**: Add update loops that iterate views (e.g. physics, AI) in `OnUpdate` or a dedicated system scheduler.
