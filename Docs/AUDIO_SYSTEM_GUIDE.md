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

### Spatial 2D Audio (Implemented)

Scene-authored 2D spatial audio is now supported through ECS components:

- **`AudioListener2DComponent`**:
  - `Enabled`
  - `UsePrimaryCameraPosition` (listener can follow the scene primary camera)
- **`AudioSourceComponent`** (extended):
  - `Pitch` (playback rate multiplier; `1.0` = authored speed)
  - `PlaybackSpace`: `Global` or `Spatial2D`
  - `SpatialMinDistance`, `SpatialMaxDistance`
  - `SpatialRolloffExponent`
  - `StereoPanStrength`
  - `AttenuationCurveKey` (reserved string key for future authored attenuation curve assets)

At runtime, play mode computes listener/source world positions, applies attenuation based on distance, then applies stereo pan from listener-relative X position.

### Mixer Groups (Implemented)

`AudioEngine` now supports per-voice mixer routing and group faders:

- Per-voice routing: `PlayClip(..., mixerGroup, pan, pitch)`
- Runtime voice updates: `SetVoiceMixParameters(..., pitch)`
- Group faders: `SetMixerGroupVolume(...)`, `GetMixerGroupVolume(...)`

Default groups initialized by the engine:

- `Master`
- `SFX`
- `Music`
- `UI`

Each `AudioSourceComponent` stores a `MixerGroup` name for scene-authored routing.

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

## Runtime Demo

Runtime includes a minimal audio validation demo:

- Default clip key: `Assets/Audio/Example.wav`
- Controls:
  - **P**: play
  - **O**: stop

Drop an audio file at that path and run Runtime. If AssetBundle is enabled, rebuild the bundle so the file is packaged.

## Next Steps (Planned)

- **Streaming clips**: large music tracks via streaming ring buffers instead of “decode whole file”.
- **Authored attenuation curves**: resolve `AttenuationCurveKey` to real curve assets.
- **Effect buses/sends**: reverb and other per-group or per-voice effects.
- **Lock-free audio command queue**: remove mutex usage in the audio callback.

