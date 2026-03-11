# 2D Animation System Guide

## Overview

The 2D animation stack now includes:

- `AnimationClip` assets (`.animationclip.json`, `.animation.json`, `.anim.json`)
- `AnimatorController` assets (`.animcontroller.json`, `.animatorcontroller.json`)
- Runtime `AnimatorComponent` state-machine playback with parameterized transitions
- Runtime `AnimationEventReceiverComponent` event collection
- Editor timeline and graph panels with command-based undo/redo integration

This guide describes data model expectations, runtime behavior, editor workflows, and scripting access.

## Animation Clip Asset

An animation clip stores:

- Clip metadata: `Name`, `Loop`, `DurationSeconds`, `SamplesPerSecond`
- Sprite tracks:
  - `SpriteSubRectTrack` for atlas sub-rect UV animation
  - `SpriteTextureTrack` for texture-swap animation
- Transform tracks:
  - `PositionTrack`
  - `ScaleTrack`
  - `RotationTrack`
- `EventTrack` for timed animation events

Interpolation values are `Step` or `Linear` on supported tracks.

## Animator Controller Asset

An animator controller stores:

- `Parameters` (`Bool`, `Float`, `Integer`, `Trigger`)
- `States` (clip key, speed multiplier, optional loop override)
- `Transitions` between states with:
  - Exit time settings
  - Duration
  - Self-transition policy
  - Condition list (`If`, `IfNot`, `Greater`, `Less`, `Equals`, `NotEquals`, `Triggered`)

## Runtime Components

### AnimatorComponent

Authoring fields:

- `ControllerKey`, `DefaultClipKey`
- `PlaybackSpeed`
- `Enabled`, `AutoPlay`
- `ApplyToSprite`, `ApplyToTransform`
- Parameter override maps (`BoolParameters`, `FloatParameters`, `IntegerParameters`, `TriggerParameters`)

Runtime fields cache controller/clip assets and sampled outputs for sprite UV/texture and transform channels.

### AnimationEventReceiverComponent

- Receives animation events generated during the animation runtime update.
- Runtime list `RuntimeDispatchedEvents` is refreshed each frame.

## Editor Workflow

### Project Panel

- Create assets:
  - **Create Animation Clip**
  - **Create Animator Controller**
- Select clip/controller assets to inspect and edit.

### Inspector Integration

- Clip and controller asset summaries are displayed in Inspector.
- Entity Inspector supports:
  - `Animator` component authoring and parameter overrides
  - `Animation Event Receiver` component

### Dedicated Editors

- **Animation Timeline** panel edits clip tracks and events.
- **Animator Graph** panel edits controller parameters, states, transitions, and conditions.
- Both panels support local edits with:
  - `Apply Changes` (persist + push undo command)
  - `Revert Changes` (discard local edits)

## Undo/Redo Model

Animation asset edits are command-based through `EditorTextAssetCommand`:

- Undo rewrites previous JSON text
- Redo rewrites edited JSON text
- Asset database/import pipeline are refreshed after each apply/undo/redo

## Scripting API

### Native C++ scripting

`ScriptableEntity` exposes animator runtime controls:

- `HasAnimator()`
- `PlayAnimatorState(stateName, restartIfSameState)`
- `PlayAnimatorClip(clipKey, restartIfSameClip)`
- Parameter access:
  - `SetAnimatorBool`, `GetAnimatorBool`
  - `SetAnimatorFloat`, `GetAnimatorFloat`
  - `SetAnimatorInteger`, `GetAnimatorInteger`
  - `SetAnimatorTrigger`, `ResetAnimatorTrigger`
- Runtime queries:
  - `GetAnimatorCurrentStateName()`
  - `GetAnimatorStateTimeSeconds()`

### Managed C# scripting

The managed contract assembly also exposes animation wrappers such as:

- `Animator`
- `AnimationEventReceiver`
- `AnimationEvent`

Current managed `Animator` members include:

- authoring/runtime properties:
  - `ControllerKey`
  - `DefaultClipKey`
  - `PlaybackSpeed`
  - `Enabled`
  - `ApplyToSprite`
  - `ApplyToTransform`
  - `AutoPlay`
  - `CurrentStateName`
  - `CurrentClipKey`
  - `StateTimeSeconds`
  - `CurrentStateDurationSeconds`
- playback control:
  - `PlayState(...)`
  - `PlayClip(...)`
- parameter access:
  - `SetBool`, `GetBool`
  - `SetFloat`, `GetFloat`
  - `SetInteger`, `GetInteger`
  - `SetTrigger`, `ResetTrigger`

Current managed `AnimationEventReceiver` members include:

- `Enabled`
- `EventCount`
- `GetEvent(index)`
- `GetEvents()`

## Test Coverage

Added automated tests validate:

- Animator component clone/serialization behavior
- Runtime-only animation state reset expectations
- Command-based undo/redo behavior for text asset edits
