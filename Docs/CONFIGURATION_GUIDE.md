# Configuration Guide

## Overview

`Limitless::ConfigManager` is a **thread-safe**, **type-safe** configuration system built around `nlohmann::json` and a `std::variant` value store (`Limitless::ConfigValue`). It supports:

- Synchronous `LoadFromFile` / `SaveToFile` for simple tooling
- Asynchronous `LoadFromFileAsync` / `SaveToFileAsync` via `AsyncIO`
- Optional hot reload via `FileWatcher`
- Key-level schema validation and change callbacks

The default configuration file name is `config.json` in the working directory (customizable via `Initialize()`).

## Guarantees (Current Behavior)

| Area | Guarantee | Notes |
|------|-----------|-------|
| Thread safety | Reads are concurrent; writes are exclusive | Implemented with `std::shared_mutex`. |
| Source precedence | Defaults → file → environment → command line → runtime writes | `EntryPoint.h` loads file first, then command line; `ConfigManager` loads environment during `Initialize()`. |
| Key format | Keys are stored as dot-delimited strings | JSON nesting is flattened into dotted keys on load. |
| Command-line format | `--key=value` overrides are supported | Hyphens in the key become dots. Value parsing is basic (bool / digits-only int / else string). |
| Async I/O | Async load/save use the AsyncIO worker pool | Async operations return `Async::Task<void>`. |
| Hot reload | File watcher callbacks run on a background thread; application of sensitive changes is main-threaded | Example: `HotReloadManager` applies window/logging changes on the main thread. |

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
    "working_directory": "."
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

## Troubleshooting

- **Value type mismatch warnings**: Your requested template type does not match the stored variant type. Confirm the JSON value type and the template parameter.
- **Hot reload does not trigger**: Confirm the watched file path and that the file is actually being written to (some tools write via temp + rename).
- **Async save/load not completing**: Ensure `AsyncIO` is initialized (or use `Limitless::Application` which initializes it during startup).

