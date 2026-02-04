# Time Guide (Unity-style)

The engine provides a Unity-style `Limitless::Time` API that acts as the **single authoritative source** of timing data for gameplay and simulation.

## What `Time` is for

- **Delta time** (`GetDeltaTimeSeconds()`): per-frame time step for gameplay updates (**scaled** by `TimeScale`)
- **Unscaled delta** (`GetUnscaledDeltaTimeSeconds()`): per-frame time step for UI/tools and real-time systems (**not scaled**)
- **Time since startup**: scaled and unscaled variants
- **Fixed-step accumulator**: simple `FixedUpdate`-style loop using `TryConsumeFixedStep()`

## What `Time` is not for

- `Time` is **not** a performance profiler. Use `PerformanceMonitor` for FPS reporting, counters, memory stats, and platform metrics.

## Engine integration (already wired)

The engine calls:

- `Time::Initialize()` during application startup
- `Time::Update()` **once per frame** in `Application::Run()`
- `Time::Shutdown()` during shutdown

That means your layers can safely read `Time` without needing to own timers.

## Common usage

### Per-frame update (scaled)

```cpp
void MyLayer::OnUpdate(float /*deltaTime*/)
{
    const float dt = Limitless::Time::GetDeltaTimeSeconds();
    // Move gameplay using dt...
}
```

### FixedUpdate-style step (simulation)

```cpp
void MyLayer::OnFixedUpdate(float fixedDeltaTime)
{
    // Deterministic step (ex: physics, movement integration).
    // fixedDeltaTime typically equals Limitless::Time::GetFixedDeltaTimeSeconds().
}
```

### UI/tools update (unscaled)

```cpp
const float realDt = Limitless::Time::GetUnscaledDeltaTimeSeconds();
```

### Pause / slow motion

```cpp
Limitless::Time::SetTimeScale(0.0f);  // pause gameplay time
Limitless::Time::SetTimeScale(0.25f); // quarter-speed
Limitless::Time::SetTimeScale(1.0f);  // normal speed
```

### Fixed-step loop (deterministic simulation)

```cpp
while (Limitless::Time::TryConsumeFixedStep())
{
    // FixedUpdate-style simulation step.
    // Use Limitless::Time::GetFixedDeltaTimeSeconds() as your step size.
}
```

## Recommended conventions

- Use **scaled delta** for gameplay, animation, and simulation that should pause/slow down.
- Use **unscaled delta** for debugging UI, editors, telemetry, and file watchers.
- Keep long stalls safe by clamping `MaximumDeltaTimeSeconds` (default is 0.25s).

