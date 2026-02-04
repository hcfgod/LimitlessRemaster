# Event System Guide

## Overview

The Limitless event system provides:

- Strongly-typed event categories (`EventType`)
- Prioritized dispatch (critical → background)
- Listener interfaces (`EventListener`) and simple callbacks (`AddCallback`)
- Deferred queuing (`DispatchDeferred`) for frame-safe processing

Core types live in `Limitless::EventSystem` and can be accessed via `Limitless::GetEventSystem()`.

## Quick Start

### Initialize

`Limitless::Application` initializes the event system during startup. If you are using the event system standalone:

```cpp
#include "Core/EventSystem.h"

auto& events = Limitless::GetEventSystem();
events.Initialize();
```

### Register a callback

```cpp
events.AddCallback(Limitless::EventType::KeyPressed,
    [](Limitless::Event& event)
    {
        LT_INFO("Key pressed: {}", event.ToString());
    },
    Limitless::EventPriority::High);
```

### Dispatch events

Immediate dispatch:

```cpp
Limitless::Events::WindowResizeEvent resize(1920, 1080);
events.DispatchImmediate(resize);
```

Deferred dispatch (recommended when emitting from systems that should not re-enter other systems mid-frame):

```cpp
auto e = std::make_unique<Limitless::Events::WindowCloseEvent>();
events.DispatchDeferred(std::move(e));
```

Then, once per frame:

```cpp
events.ProcessEvents();
```

## Listener Model

Listeners implement `Limitless::EventListener`:

```cpp
class MyListener : public Limitless::EventListener
{
public:
    void OnEvent(Limitless::Event& event) override
    {
        LT_INFO("Listener received: {}", event.ToString());
    }

    bool ShouldReceiveEvent(const Limitless::Event& event) const override
    {
        // Add filtering here (category/type checks, game state, etc.)
        return true;
    }

    Limitless::EventPriority GetPriority() const override
    {
        return Limitless::EventPriority::Normal;
    }
};

auto listener = std::make_shared<MyListener>();
events.AddListener(listener);
```

## Priorities

Both callbacks and listeners can have priorities:

- `Critical`
- `High`
- `Normal`
- `Low`
- `Background`

Use higher priorities sparingly. Prefer `Normal` for most gameplay and UI events.

## Filters

Global filtering is supported:

```cpp
events.SetEventFilter([](const Limitless::Event& event) {
    // Example: ignore background events while in a critical loading screen
    return true;
});
```

## Extending with Custom Events

Create a derived event type that implements:

- `GetCategory()`
- `GetName()`
- `Clone()` for safe queuing/copying

```cpp
class PlayerDiedEvent final : public Limitless::Event
{
public:
    explicit PlayerDiedEvent(int playerId)
        : Event(Limitless::EventType::Custom, Limitless::EventPriority::High)
        , m_PlayerId(playerId)
    {
    }

    int GetPlayerId() const { return m_PlayerId; }

    std::string GetCategory() const override { return "Gameplay"; }
    std::string GetName() const override { return "PlayerDied"; }

    std::unique_ptr<Event> Clone() const override
    {
        return std::make_unique<PlayerDiedEvent>(m_PlayerId);
    }

private:
    int m_PlayerId = 0;
};
```

Then dispatch it:

```cpp
PlayerDiedEvent e(7);
events.Dispatch(e);
```

## Best Practices

- Prefer `DispatchDeferred` when emitting events from deep inside update loops.
- Keep callbacks small; hand off heavy work to jobs/tasks.
- Use `ShouldReceiveEvent` for cheap filtering instead of doing it inside `OnEvent`.
- Avoid allocating per-frame in event handlers; if you must, pool or reuse.
- If an event is marked handled (`event.SetHandled(true)`), dispatch will stop propagating it to lower-priority handlers.

## Troubleshooting

- **No events are received**: Confirm `EventSystem::Initialize()` was called and `ProcessEvents()` is running each frame.
- **Deferred events never run**: Ensure `ProcessEvents()` is called (or call `ProcessEvents(maxEvents)`).
- **Performance issues**: Add coarse filtering, reduce callback counts, and prefer deferred processing for noisy event sources.

