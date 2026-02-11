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

**Option A: Raw callback (manual enum + cast)**

```cpp
Limitless::EventCallbackToken token = events.AddCallback(Limitless::EventType::KeyPressed,
    [](Limitless::Event& event)
    {
        LT_INFO("Key pressed: {}", event.ToString());
    },
    Limitless::EventPriority::High);
```

**Option B: Typed callback via `AddEventCallback` (no cast needed)**

```cpp
Limitless::EventCallbackToken token = Limitless::AddEventCallback<Limitless::Events::KeyPressedEvent>(
    [](Limitless::Events::KeyPressedEvent& event)
    {
        LT_INFO("Key pressed: {}", event.GetKeyCode());
    },
    Limitless::EventPriority::High);
```

`AddEventCallback<EventClass>` registers a callback that receives the strongly-typed event directly. Each event class must provide `static EventType GetStaticType()` for this to work (all built-in events do).

### Remove a callback (identity-safe)

Callbacks must be removed by token (AAA-grade identity safety):

```cpp
events.RemoveCallback(Limitless::EventType::KeyPressed, token);
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
- `static EventType GetStaticType()` (required if you want to use `AddEventCallback<YourEvent>`)

```cpp
class PlayerDiedEvent final : public Limitless::Event
{
public:
    explicit PlayerDiedEvent(int playerId)
        : Event(Limitless::EventType::Custom, Limitless::EventPriority::High)
        , m_PlayerId(playerId)
    {
    }

    static Limitless::EventType GetStaticType() { return Limitless::EventType::Custom; }

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

## Correctness Contracts (Threading + Lifetime)

This section is the “hard rules” version of the event system behavior.

### Threading model

- **Handler registration (`AddListener`, `AddCallback`, remove variants)**:
  - Thread-safe with respect to concurrent dispatch: dispatch takes a snapshot of handlers under a mutex.
  - Best practice is still to mutate handlers from a single “game thread” to keep mental models simple.
- **Immediate dispatch (`Dispatch`, `DispatchImmediate`)**:
  - The event is delivered **synchronously on the calling thread**.
  - User callbacks/listeners must be thread-safe if you dispatch from multiple threads.
- **Deferred dispatch (`DispatchDeferred`)**:
  - Safe for **multiple producer threads** (MPMC enqueue).
  - Events are delivered when `ProcessEvents()` runs.
- **Processing (`ProcessEvents`)**:
  - Intended to be called from **one thread** (the main/game thread) as part of the frame loop.
  - Do not run `ProcessEvents()` concurrently from multiple threads.

### Ownership / lifetime

- **Immediate dispatch**: the caller retains ownership of the event object; it must remain alive for the duration of the call.
- **Deferred dispatch**: ownership transfers into the event queue via `std::unique_ptr<Event>`.
- **Shutdown safety**:
  - `EventSystem::Shutdown()` is **race-safe** with other threads calling `Dispatch*()` / `ProcessEvents()`:
    - Shutdown prevents new operations from starting.
    - Shutdown **waits for in-flight operations to complete**, so **no callbacks/listeners will execute after `Shutdown()` returns**.
  - Calls made after shutdown are treated as safe no-ops (with warnings).

## Troubleshooting

- **No events are received**: Confirm `EventSystem::Initialize()` was called and `ProcessEvents()` is running each frame.
- **Deferred events never run**: Ensure `ProcessEvents()` is called (or call `ProcessEvents(maxEvents)`).
- **Performance issues**: Add coarse filtering, reduce callback counts, and prefer deferred processing for noisy event sources.

