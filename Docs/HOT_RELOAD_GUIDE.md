# Hot Reload Configuration Guide

This guide documents the current configuration hot-reload path.

## Overview

Limitless supports hot reloading for the active `config.json` file through:

- `ConfigManager`
- `FileWatcher`
- `HotReloadManager`

The current implementation is configuration-driven and focused on:

- logging settings
- window settings

## Current Runtime Flow

The hot-reload pipeline works like this:

1. `Application` initializes `HotReloadManager` and enables config hot reload.
2. `ConfigManager` starts a `FileWatcher` for the active config file.
3. `FileWatcher` polls the file modification time.
4. When the file changes, `ConfigManager` reloads the file and compares old vs new values.
5. Changed keys trigger registered callbacks.
6. `HotReloadManager` queues logging/window work.
7. `HotReloadManager::Update()` applies the queued work on the main thread during the application loop.

This main-thread application step is important for window safety and deterministic logging reinitialization.

## Supported Hot-Reloaded Keys

### Logging

The currently registered logging keys are:

- `logging.level`
- `logging.file_enabled`
- `logging.console_enabled`
- `logging.pattern`

Behavior:

- logging changes do not reconfigure immediately on the file-watcher thread
- instead, the manager queues a logging reinitialization
- the logging system is reinitialized on the next main-thread update

### Window

The currently registered window keys are:

- `window.width`
- `window.height`
- `window.title`
- `window.fullscreen`
- `window.resizable`
- `window.vsync`
- `window.position.x`
- `window.position.y`
- `window.borderless`
- `window.always_on_top`
- `window.min_width`
- `window.min_height`
- `window.max_width`
- `window.max_height`
- `window.high_dpi`
- `window.icon`

Behavior:

- window changes are queued when config values change
- they are applied on the main thread in `HotReloadManager::Update()`

## What Is Not Hot Reloaded Today

These are **not** currently registered in `HotReloadManager`:

- audio settings hot reload
- graphics quality/settings hot reload
- input settings hot reload
- arbitrary project/editor settings hot reload

Those may still be valid configuration values, but they are not part of the current hot-reload callback set.

## File Watching Details

`FileWatcher` currently:

- watches one file path at a time
- uses polling-based file timestamp checks
- defaults to a `500ms` polling interval
- uses `AsyncIO` helpers for file existence and modified-time queries

If the watched file disappears, the watch loop logs the condition and stops.

## Config Reload Behavior

`ConfigManager::ReloadFromFile()` currently:

- reloads the active config file
- stores the old flat key/value map
- compares old and new values
- invokes change callbacks only for keys whose values actually changed
- logs removed keys, but does not currently dispatch a dedicated removal callback

## Typical Usage

1. Run the editor or runtime normally.
2. Edit the active `config.json`.
3. Save the file.
4. Wait for the watcher to detect the change.
5. Observe the effect next frame for window/logging changes.

Example:

```json
{
  "logging": {
    "level": "trace",
    "file_enabled": false,
    "console_enabled": true,
    "pattern": "[%T] [%l] %n: %v"
  },
  "window": {
    "width": 1920,
    "height": 1080,
    "title": "Hot Reload Test",
    "fullscreen": false,
    "borderless": false
  }
}
```

## Where the Active Config Comes From

The engine logs the active config path at startup.

In practice:

- Visual Studio/debug runs often use `Runtime/config.json`
- built executables usually use the `config.json` beside the executable in the output directory

The runtime build flow copies `Runtime/config.json` into the build output so the output-local config stays available.

## Troubleshooting

### Changes do not apply

Check:

- the edited file is the actual active `config.json`
- the JSON syntax is valid
- hot reload was enabled during startup
- the changed key is part of the supported key set above

### Logging changes seem delayed

That is expected.

Logging changes are queued and then applied on the next main-thread `HotReloadManager::Update()` call.

### Window changes do not apply

Check:

- the value type matches the expected key type
- a window has been registered with `HotReloadManager`
- the application is still pumping frames so `Update()` continues to run

## Current Scope Summary

- config hot reload is implemented
- file watching is polling-based
- change detection is value-aware
- logging and window hot reload are implemented
- audio / graphics / input hot reload are still future work

## Related Files

- `Limitless/Source/Core/ConfigManager.{h,cpp}`
- `Limitless/Source/Core/FileWatcher.{h,cpp}`
- `Limitless/Source/Core/HotReloadManager.{h,cpp}`
- `Limitless/Source/Core/Application.cpp`