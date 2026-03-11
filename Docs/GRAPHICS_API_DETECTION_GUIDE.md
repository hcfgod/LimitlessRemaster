# Graphics API Detection and Selection Guide

This guide documents the current graphics API detection and selection code as it exists today.

## Current Reality

The engine currently ships with:

- OpenGL rendering and context creation
- a graphics API detector/selector framework
- stubbed future-facing entries for Vulkan, DirectX, and Metal

What that means in practice:

- the actual runtime/backend path is **OpenGL-only**
- non-OpenGL APIs can appear in priority lists and selection helpers
- but non-OpenGL detection is not implemented today
- and non-OpenGL context creation falls back to `OpenGLContext`

## What Is Implemented Today

### OpenGL

Implemented today:

- `OpenGLContext`
- SDL-based OpenGL context creation
- fallback OpenGL version attempts during context creation
- updating detector state with the actual OpenGL vendor/renderer/version after context creation

`GraphicsAPIDetector::DetectOpenGL()` currently:

- assumes OpenGL support
- seeds conservative default version data
- marks OpenGL as supported/available
- computes default capability flags from that assumed version

That means OpenGL detection is not a real probe before context creation. The real verification step is whether the OpenGL context can actually be created.

### Detector / Selector Framework

Implemented today:

- `GraphicsAPIDetector::Initialize()`
- preferred API storage via `SetPreferredAPI()` / `GetPreferredAPI()` / `ClearPreferredAPI()`
- platform priority lists
- best-API lookup based on supported entries in the current detection results
- detection reporting helpers

Startup/config reality:

- `Application` initializes `GraphicsAPIDetector` before window creation
- `Window::CreateFromConfig()` reads `graphics.api` from config into `WindowProps::Api`
- current startup does **not** automatically forward that config value into `GraphicsAPIDetector::SetPreferredAPI()`
- `CreateGraphicsContext()` still chooses from detector state, not directly from `WindowProps::Api`

### Context Factory Behavior

`CreateGraphicsContext()` currently:

- checks whether the detector has been initialized
- asks `GraphicsAPIDetector::GetBestAPI()`
- logs the selected API
- always returns an `OpenGLContext` today

Current outcomes:

- if the detector is not initialized, the factory logs and returns `OpenGLContext`
- if no supported API is found, the factory logs and returns `OpenGLContext`
- if `OpenGL` is selected, it returns `OpenGLContext`
- if `Vulkan`, `DirectX`, or `Metal` are selected, it still logs a warning and returns `OpenGLContext`

## What Is Only Partial Today

### API Selection

The framework for selection exists, but it is still conservative/placeholder:

- Windows priority list: `DirectX`, `Vulkan`, `OpenGL`
- macOS priority list: `Metal`, `OpenGL`
- Linux priority list: `Vulkan`, `OpenGL`

Because only OpenGL is currently marked supported, `GetBestAPI()` effectively resolves to `OpenGL` today.

### `GraphicsAPISelector`

`GraphicsAPISelector` exists, but it is not a full decision engine yet.

Current behavior:

- `SelectAPI(criteria)` returns the first supported API
- `GetRecommendation(criteria)` produces placeholder reasoning text
- `GetPerformanceComparison()` returns `"Performance comparison not yet implemented"`
- `GetFeatureComparison()` returns `"Feature comparison not yet implemented"`

## What Is Not Implemented Today

The following are present as framework surface area, but not implemented end-to-end:

- real Vulkan detection
- real DirectX detection
- real Metal detection
- Vulkan context creation
- DirectX context creation
- Metal context creation
- criteria-based scoring for performance / compatibility / features / stability
- runtime benchmarking/comparison output
- runtime backend switching

## Supported Graphics APIs

| API | Current Detection Status | Current Context Status | Current Runtime Status |
|-----|--------------------------|------------------------|------------------------|
| OpenGL | Assumed supported with conservative defaults until a real context exists | Implemented | Implemented |
| Vulkan | Stubbed as unsupported with explicit error text | Not implemented | Not implemented |
| DirectX | Stubbed as unsupported with explicit error text | Not implemented | Not implemented |
| Metal | Stubbed as unsupported with explicit error text | Not implemented | Not implemented |

## Initialization Flow

The current startup flow is:

1. initialize `GraphicsAPIDetector`
2. load config and parse `graphics.api` into `WindowProps::Api`
3. create the SDL window / graphics context objects
4. let `CreateGraphicsContext()` choose an API from detector state
5. create the actual OpenGL context
6. update the detector with real OpenGL version/vendor/renderer strings

`GraphicsAPIDetector::Initialize()` is:

- thread-safe
- idempotent

Important implication:

- the config key `graphics.api` is parsed and stored on the window side
- but it does not currently become a true detector preference or enable a non-OpenGL backend
- in practice, the runtime still resolves to OpenGL today

## OpenGL Context Version Behavior

`OpenGLContext::SetupAttributes()` currently:

- asks `GraphicsAPIDetector::GetBestSupportedOpenGLVersion()`
- stores that requested version
- sets SDL GL attributes

`GetBestSupportedOpenGLVersion()` currently returns a conservative result:

- `3.3`

During `OpenGLContext::Init()`:

- the engine first tries the requested version
- if that fails, it retries lower fallback versions
- current fallback list is:
  - `3.3`
  - `3.2`
  - `3.1`
  - `3.0`
  - `2.1`
  - `2.0`

After successful context creation, the engine reads:

- `GL_VENDOR`
- `GL_RENDERER`
- `GL_VERSION`

and pushes that information back into `GraphicsAPIDetector::UpdateOpenGLInfo()`.

## Usage Notes

### Preferred API

You can set a preferred API with:

- `GraphicsAPIDetector::SetPreferredAPI(...)`

But today:

- if the preferred API is unsupported, `GetBestAPI()` clears that preference
- and the system falls back to the normal supported-API search
- which still resolves to OpenGL with the current implementation
- the `graphics.api` config key is **not** currently wired to call `SetPreferredAPI(...)` automatically

### Validation Helpers

These helpers are real:

- `IsAPISupported()`
- `ValidateAPISelection()`
- `MeetsRequirements()`
- `GetUnsupportedReason()`

But their answers are only as good as the current detector data.

For non-OpenGL APIs, the result is currently based on stubbed unsupported entries.
For OpenGL, the pre-context answer is conservative until a real context has been created.

## Diagnostics

Useful current helpers:

- `GetDetectionResults()`
- `GetDetectionReport()`
- `GetAPIInfo(api)`
- `GetUnsupportedReason(api)`

`GetDetectionReport()` currently includes:

- initialization status
- preferred API
- detected API entries
- best API result
- current platform priority list

## Troubleshooting

### Preferred API is ignored

Check whether the preferred API is actually reported as supported.

Today, non-OpenGL APIs are reported unsupported, so they will not survive `GetBestAPI()`.

Also note:

- changing `graphics.api` in config changes the parsed window property
- but it does not currently create a real Vulkan/DirectX/Metal path or set detector preference automatically
- so config changes alone still do not bypass the current OpenGL-only runtime/backend

### Detection says OpenGL is supported but context creation fails

That can happen.

The detector currently assumes OpenGL support before a real context exists. Actual support is confirmed only when SDL/OpenGL context creation succeeds.

### Detection report shows placeholder OpenGL vendor/renderer

That is expected before a real OpenGL context is created.

The detector starts with placeholder values and only receives real vendor/renderer/version strings after successful context initialization.

## Current Scope Summary

- graphics API framework exists
- OpenGL runtime/backend path exists
- non-OpenGL APIs are framework placeholders today
- selector criteria/comparison output is still placeholder logic
- context factory always returns `OpenGLContext` today

## Related Files

- `Limitless/Source/Graphics/GraphicsAPIDetector.{h,cpp}`
- `Limitless/Source/Graphics/GraphicsContext.{h,cpp}`
- `Limitless/Source/Graphics/OpenGL/OpenGLContext.cpp`
- `Limitless/Source/Graphics/Renderer.cpp`