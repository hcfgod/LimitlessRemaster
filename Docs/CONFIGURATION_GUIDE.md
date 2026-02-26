# Configuration Guide

## Overview

`Limitless::ConfigManager` is a **thread-safe**, **type-safe** configuration system built around `nlohmann::json` and a `std::variant` value store (`Limitless::ConfigValue`). It supports:

- Synchronous `LoadFromFile` / `SaveToFile` for simple tooling
- Asynchronous `LoadFromFileAsync` / `SaveToFileAsync` via `AsyncIO`
- Optional hot reload via `FileWatcher`
- Key-level schema validation and change callbacks

When using the engine’s entry point (`Limitless/Source/Core/EntryPoint.h`), the engine resolves and loads `config.json` (current working directory first, then executable directory) and initializes `ConfigManager` before calling your `CreateApplication()`. In that case you only need `GetInstance()` and Get/Set; you do not call `Initialize()` yourself. For custom or tooling use without the engine main, call `Initialize(path)` as below.

## Guarantees (Current Behavior)

| Area | Guarantee | Notes |
|------|-----------|-------|
| Thread safety | Reads are concurrent; writes are exclusive | Implemented with `std::shared_mutex`. |
| Source precedence | Defaults → file → environment → command line → runtime writes | `EntryPoint.h` loads file first, then command line; `ConfigManager` loads environment during `Initialize()`. |
| Key format | Keys are stored as dot-delimited strings | JSON nesting is flattened into dotted keys on load. |
| Command-line format | `--key=value` overrides are supported | Hyphens in the key become dots. Value parsing is basic (bool / digits-only int / else string). |
| Async I/O | Async load/save use the AsyncIO worker pool | Async operations return `Async::Task<void>`. |
| Hot reload | File watcher callbacks run on a background thread; application of sensitive changes is main-threaded | Example: `HotReloadManager` applies window/logging changes on the main thread. |

## Editor Build Settings (Separate File)

Game export/build preferences are not stored in `config.json`.

- File: `Project/Settings/BuildSettings.json`
- Owner: editor build workflow (`EditorBuildSettingsPanel`)
- Typical keys:
  - `targetOS`, `targetArchitecture`, `executionMode` (`Auto`/`Local`/`Remote`)
  - `remoteBuildEndpoint`, `remoteBuildEndpointWindows`, `remoteBuildEndpointMacOS`, `remoteBuildEndpointLinux`
  - `useTargetEndpointRouting`, `remoteBuildPool`, `remoteBuildAuthToken`
  - `remoteBuildTimeoutSeconds`, `remoteBuildPollIntervalSeconds`, `remoteBuildMaxRetries`
  - `allowLocalBuildFallback`

This split keeps runtime configuration (`ConfigManager`) separate from per-project build orchestration.

## Quick Start

### Initialize

```cpp
#include "Core/ConfigManager.h"

auto& config = Limitless::ConfigManager::GetInstance();
config.Initialize("config.json");
```

### Read and write values

```cpp
// Write
config.SetValue(Limitless::Config::Window::WIDTH, 1920u);
config.SetValue(Limitless::Config::Window::HEIGHT, 1080u);
config.SetValue("graphics.vsync", true);

// Read with defaults
uint32_t width  = config.GetValue<uint32_t>(Limitless::Config::Window::WIDTH, 1280u);
uint32_t height = config.GetValue<uint32_t>(Limitless::Config::Window::HEIGHT, 720u);
bool vsync      = config.GetValue<bool>("graphics.vsync", true);
```

## Configuration File Layout

The engine uses **dot-delimited keys** (for example `system.max_threads`, `window.width`) even if your JSON is nested.

Example `config.json`:

```json
{
  "system": {
    "max_threads": 0,
    "simulation_threads": 0,
    "working_directory": "."
  },
  "ecs": {
    "mt": {
      "defer_structural_mutations": true,
      "validate_structural_phase": true,
      "enable_system_scheduler": true,
      "enable_parallel_scripts": true,
      "require_parallel_script_access_declarations": true,
      "warn_implicit_parallel_script_access": true,
      "enable_parallel_transforms": true
    }
  },
  "window": {
    "title": "Limitless Engine",
    "width": 1280,
    "height": 720,
    "fullscreen": false,
    "resizable": true,
    "vsync": true
  },
  "logging": {
    "level": "info",
    "console_enabled": true,
    "file_enabled": true
  }
}
```

## Thread Safety Model

`ConfigManager` uses a `std::shared_mutex`:

- Reads (`GetValue`, `HasValue`) take a shared lock
- Writes (`SetValue`, `RemoveValue`) take an exclusive lock

This allows high-frequency reads on the main thread while background systems update configuration values safely.

## Async I/O Integration

Async operations return `Limitless::Async::Task<void>` and execute on the `AsyncIO` worker pool:

```cpp
auto loadTask = config.LoadFromFileAsync("config.json");
loadTask.Wait();

auto saveTask = config.SaveToFileAsync("config.json");
saveTask.Wait();
```

If you are using `Limitless::Application`, `AsyncIO` is initialized early using `system.max_threads`.

## Environment Variables (Current Status)

The `LoadFromEnvironment()` hook exists, but the current implementation is intentionally minimal and only reads a small fixed set of variables. Treat environment overrides as **experimental** until the variable → key mapping is expanded and documented.

## Hot Reload

Hot reload watches the config file and reloads it when it changes:

```cpp
config.EnableAsyncHotReload(true);
```

When hot reload triggers, the manager updates stored values and can dispatch change callbacks. (See the hot reload guide for end-to-end integration details.)

## Validation (Schema)

You can register key-level validation:

```cpp
config.RegisterSchema("window.width", [](const Limitless::ConfigValue& value) {
    if (const auto* width = std::get_if<uint32_t>(&value))
        return (*width >= 640u) && (*width <= 7680u);
    return false;
});
```

If a write fails validation, the value is not updated and a warning is logged.

## Change Callbacks

### Async callbacks

Async callbacks are queued and processed safely:

```cpp
config.RegisterAsyncChangeCallback("window.width",
    [](const std::string& key, const Limitless::ConfigValue& value)
    {
        const auto width = std::get<uint32_t>(value);
        LT_INFO("Config changed: {} = {}", key, width);
    });
```

### Legacy (sync) callbacks

Synchronous callbacks are supported for backwards compatibility:

```cpp
config.RegisterChangeCallback("logging.level",
    [](const std::string& key, const Limitless::ConfigValue& value)
    {
        LT_INFO("Legacy callback: {}", key);
    });
```

## Recommended Keys

Common keys exposed as constants in `Limitless::Config`:

- `Limitless::Config::System::MAX_THREADS` (`system.max_threads`)
- `Limitless::Config::Window::WIDTH` / `HEIGHT` / `TITLE`
- `Limitless::Config::Logging::LEVEL` and file/console toggles

## ECS Multithreading Rollout Keys

The ECS runtime now has dedicated multithreading rollout switches. These keys are read directly by scene/runtime systems and are designed for staged enablement and debugging.

| Key | Type | Default | Purpose |
|-----|------|---------|---------|
| `system.simulation_threads` | integer | `0` | Worker count for the dedicated simulation `JobSystem` (`0` = engine-selected default). |
| `ecs.mt.defer_structural_mutations` | bool | `true` | Defers structural changes (create/destroy/add/remove/set-parent) into the structural phase when called from parallel contexts. |
| `ecs.mt.validate_structural_phase` | bool | `true` | Emits warnings when structural operations happen outside the expected structural phase in runtime/parallel contexts. |
| `ecs.mt.enable_system_scheduler` | bool | `true` | Enables compatibility-based scene system scheduling barriers for runtime simulation systems. |
| `ecs.mt.enable_parallel_scripts` | bool | `true` | Enables execution of `ParallelSafe` native scripts in worker jobs. |
| `ecs.mt.require_parallel_script_access_declarations` | bool | `true` | Requires explicit script read/write masks before a `ParallelSafe` script can run in parallel; otherwise it falls back to main-thread execution. |
| `ecs.mt.warn_implicit_parallel_script_access` | bool | `true` | Warns once per script slot when `ParallelSafe` is selected but access masks are missing. |
| `ecs.mt.enable_parallel_transforms` | bool | `true` | Enables depth-batched parallel transform solve (with depth barriers). |

Suggested rollout order:

1. Enable `defer_structural_mutations` + `validate_structural_phase`.
2. Enable `enable_system_scheduler`.
3. Enable `enable_parallel_scripts` while keeping `require_parallel_script_access_declarations=true`.
4. Enable `enable_parallel_transforms`.
5. Tune `system.simulation_threads` for target CPU topology.

## Troubleshooting

- **Value type mismatch warnings**: Your requested template type does not match the stored variant type. Confirm the JSON value type and the template parameter.
- **Hot reload does not trigger**: Confirm the watched file path and that the file is actually being written to (some tools write via temp + rename).
- **Async save/load not completing**: Ensure `AsyncIO` is initialized (or use `Limitless::Application` which initializes it during startup).

