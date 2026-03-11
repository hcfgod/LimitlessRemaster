# Audio System Guide (Unity-Style)

## Goals

This engine uses a **Unity-style** runtime audio model:

- **`AudioClip`**: decoded PCM sample data (runtime format the mixer can consume).
- **`AudioClipAsset`**: Unity-style asset wrapper around an `AudioClip` with a persistent `.meta` GUID.
- **`AudioSource`**: lightweight “component-like” object that plays an `AudioClipAsset`.
- **`AudioEngine`**: global mixer that owns the SDL playback device and mixes all active voices.

## Current Implementation (What Ships Today)

### Mixer Format (Engine Contract)

The mixer runs in a fixed format to keep the audio callback simple and fast:

- **Sample format**: float32
- **Channels**: stereo (2)
- **Sample rate**: 48,000 Hz
- **Layout**: interleaved (L, R, L, R, ...)

Decoders are expected to **resample/remix** into this format at import/load time.

### Playback Backend

- **SDL3** is used for device playback (`SDL_OpenAudioDeviceStream`).
- The engine provides data through SDL’s audio stream callback.
- The callback mixes active voices into a preallocated scratch buffer and queues it to SDL.
- Public API calls enqueue lock-free audio commands; the callback thread drains and applies commands before each mix pass.

### Spatial Audio (Implemented)

Scene-authored spatial audio is supported through ECS components:

- **`AudioListener2DComponent`**
  - `Enabled`
  - `UsePrimaryCameraPosition`
- **`AudioListener3DComponent`**
  - `Enabled`
  - `UsePrimaryCameraTransform`
- **`AudioSourceComponent`** (extended)
  - `Pitch`
  - `Space`: `Global`, `Spatial2D`, or `Spatial3D`
  - `SpatialMinDistance`, `SpatialMaxDistance`
  - `SpatialRolloffExponent`
  - `SpatialRolloffMode`: `SmoothStep`, `Linear`, `Inverse`
  - `StereoPanStrength`
  - `DopplerFactor`
  - `EnableDirectionalAttenuation`
  - `DirectionalInnerAngleDegrees`
  - `DirectionalOuterAngleDegrees`
  - `DirectionalOuterVolume`
  - `AttenuationCurveKey` (reserved string key for future authored attenuation curve assets)

#### Spatial 2D

For `Space = Spatial2D`:

- runtime computes source/listener world positions in 2D
- applies distance attenuation
- applies stereo pan from listener-relative X position

2D listener resolution uses **multi-listener nearest selection**:

- all enabled `AudioListener2DComponent` instances are considered
- the nearest listener to each source is selected per-source
- if no listener is authored, a primary-camera fallback listener keeps scenes audible

#### Spatial 3D

For `Space = Spatial3D`:

- runtime computes 3D source/listener world positions
- applies distance attenuation
- applies stereo pan from source direction relative to listener forward/right vectors
- applies optional directional attenuation from the source forward vector
- applies Doppler-style pitch adjustment from listener/source relative velocity

3D listener resolution also uses nearest-listener selection with primary-camera fallback when no authored listener is present.

### Mixer Groups (Implemented)

`AudioEngine` now supports per-voice mixer routing and group faders:

- Per-voice routing: `PlayClip(..., mixerGroup, pan, pitch)`
- Runtime voice updates: `SetVoiceMixParameters(..., pitch)`
- Group faders: `SetMixerGroupVolume(...)`, `GetMixerGroupVolume(...)`
- Group reverb sends: `SetMixerGroupReverbSend(...)`, `GetMixerGroupReverbSend(...)`

Default groups initialized by the engine:

- `Master`
- `SFX`
- `Music`
- `UI`

Each `AudioSourceComponent` stores a `MixerGroup` name for scene-authored routing.

Project audio settings can also point to an audio mixer asset key, and runtime bootstrap applies that mixer definition to initialize mixer-group volume and reverb-send values.

### Built-in Reverb Send/Return (Implemented)

The mixer includes a minimal built-in reverb path:

- Per-group **send amount** (`ReverbSend`, 0..1) authored in audio mixer assets.
- A single global feedback delay return mixed back into stereo output.
- Zero send preserves prior level-only behavior.

## FFmpeg Integration (Decoding)

When `LT_ENABLE_FFMPEG` is defined, audio files are decoded through FFmpeg:

- **libavformat**: container demux
- **libavcodec**: codec decode
- **libswresample**: resample/remix → float32 stereo @ 48 kHz

If FFmpeg is not enabled for the build, `AudioClipAsset` loading will fail with a clear error.

## Vendor Layout (Windows)

This repository vendors FFmpeg headers at:

- `Limitless/Vendor/ffmpeg/include`

For Windows builds, you must also provide:

- **Import libraries**: `Limitless/Vendor/ffmpeg/libs/*.lib`
- **Runtime DLLs**: `Limitless/Vendor/ffmpeg/dlls/*.dll`

Premake will automatically:

- Define `LT_ENABLE_FFMPEG`
- Link `avcodec`, `avformat`, `avutil`, `swresample`
- Copy DLLs next to the built executables (e.g. Editor, Runtime) in a post-build step where applicable

After adding FFmpeg binaries, **regenerate project files** (Premake) so the new link/copy rules apply.

## Using Audio in Game Code

### Loading an AudioClip Asset

Include the importer trait header (this provides the `AssetImporter<AudioClipAsset>` specialization):

- `Assets/AudioClipAssetImporter.h`

Then load like any other Unity-style asset:

- `Assets::AssetManager::LoadBlocking<Assets::AudioClipAsset>("Assets/Audio/Example.wav");`

### Playing Through an AudioSource

Create an `AudioSource`, assign the clip (via `AssetHandle`), then call `Play()`.

## Managed Scripting Surface

Managed gameplay code currently exposes:

- `AudioSource`
- `AudioListener2D`
- `AudioListener3D`
- `AudioPlaybackSpace`
- `AudioRolloffMode`

`AudioSource` includes wrappers for authored/runtime properties such as:

- clip key
- volume
- pitch
- loop / mute
- playback space
- mixer group
- spatial distance settings
- rolloff mode / exponent
- stereo pan strength
- Doppler factor
- directional attenuation settings
- attenuation curve key
- `IsPlaying`
- `Play()`
- `Stop()`

`AudioListener2D` and `AudioListener3D` expose the authored listener toggles (`Enabled`, primary-camera binding options), so managed code can participate in the current scene-authored audio workflow without going through native-only gameplay code.

## Quick Validation Workflow

To validate the current audio path:

- assign an `AudioClipKey` to an `AudioSourceComponent`
- choose a `MixerGroup`
- set `PlayOnStart = true` or request playback from code
- author an `AudioListener2DComponent` / `AudioListener3DComponent`, or rely on the primary-camera fallback
- enter Play Mode and confirm the source starts, spatializes, and routes through the expected mixer group

If AssetBundle is enabled, rebuild the bundle so the clip asset is packaged.

## Next Steps (Planned)

- **Streaming clips**: large music tracks via streaming ring buffers instead of “decode whole file”.
- **Authored attenuation curves**: resolve `AttenuationCurveKey` to real curve assets.
- **Advanced effect chains**: multiple effects per-group/per-voice (filters, dynamics, richer reverb).

