# Managed C# Scripting Guide

This guide documents the current managed scripting workflow in Limitless.

## Overview

Managed scripting currently uses:

- Coral / CoreCLR hosting
- `Managed/Limitless.Managed` as the public managed contract/API assembly
- a staged `Managed/` runtime payload beside editor/runtime/shipping outputs
- project-authored `.cs` source files under the opened project's `Assets/`

Managed scripting exists **alongside** native C++ scripting. It does not replace the native scripting backend or `ScriptCore`.

Important distinction:

- project `.cs` files are the **authoring source**
- the host discovers and executes **compiled managed assemblies** from the staged `Managed/` payload

Raw `.cs` files by themselves are not enough for runtime discovery until they have been built and staged into a valid payload.

## Runtime Payload

The managed payload is staged into:

- `<Output>/Managed/`

The payload manifest is:

- `Managed/Limitless.Managed.payload.json`

It records:

- payload format version
- host API version
- Coral managed assembly/runtimeconfig
- `Limitless.Managed` contract assembly/runtimeconfig
- `scriptAssemblies`
- target OS / architecture / build configuration

At startup, the host validates the payload directory before initialization. On success, the payload is **shadow-copied** to a loaded directory and Coral is initialized from that loaded shadow copy rather than directly from the source directory.

`ManagedScriptHost::GetSnapshot()` exposes the current host/discovery state, including:

- source managed directory
- loaded shadow directory
- payload API version
- discovered managed classes
- reflected field metadata for those classes

## Build / Staging Scripts

Primary helpers:

- Windows: `Scripts/build-managed-runtime-windows.bat`
- Unix: `Scripts/build-managed-runtime-unix.sh`

These scripts build/publish:

- Coral managed runtime pieces
- `Managed/Limitless.Managed`
- `Managed/Limitless.Managed.TestScripts`
- optional project-authored managed script assemblies

The build scripts also emit `Limitless.Managed.payload.json` with the current `scriptAssemblies` list.

## Editor and Runtime Probing

Both editor/runtime flows probe candidate `Managed/` directories and initialize from the first valid managed payload they find.

Current runtime probing includes locations such as:

- `Managed/` beside the executable
- `Managed/` under the current working directory

The editor also tracks the discovered payload manifest and can reload the managed host when the active payload changes.

## Project Authoring Workflow

Project-authored managed scripts live under:

- `<ProjectRoot>/Assets/**/*.cs`

Current editor workflow supports:

- `Create C# Script` from the Project browser
- script asset discovery in the Project browser
- project script attachment from the inspector `Add Component` flow
- building/staging project scripts so newly authored types can be discovered by the host

The Project browser treats `.cs` files as first-class script assets, but managed class discovery still happens from the compiled assemblies present in the staged payload.

## Default Script Template

Creating a new C# script currently generates a basic template like:

- `using Limitless.Managed;`
- `namespace GameScripts;`
- `public sealed class MyScript : ScriptableEntity`

with `OnCreate()` and `OnUpdate(float deltaTime)` overrides.

## Discovery and Attachment

Managed classes discovered by the host are exposed through `ManagedScriptHost`.

Current host initialization/discovery flow:

- validate the payload manifest and required files
- shadow-copy the managed payload
- initialize Coral from the loaded shadow directory
- load `Limitless.Managed.dll`
- register internal calls against the contract assembly
- enumerate remaining `.dll` files in the payload
- discover subclasses of `Limitless.Managed.ScriptableEntity`
- reflect supported managed fields for exposed-property synchronization

In scene data, managed script entries currently serialize runtime authoring data such as:

- class name
- source asset path
- exposed properties
- enabled state

If a newly authored project script has not been built into the staged payload yet, it will not appear as a discovered managed class until that build/staging step completes.

## Script Lifecycle

The main base type is:

- `Limitless.Managed.ScriptableEntity`

Current overridable lifecycle members include:

- `OnCreate()`
- `OnFixedUpdate(float fixedDeltaTime)`
- `OnUpdate(float deltaTime)`
- `OnCollisionEnter(Entity other)`
- `OnCollisionStay(Entity other)`
- `OnCollisionExit(Entity other)`
- `OnTriggerEnter(Entity other)`
- `OnTriggerStay(Entity other)`
- `OnTriggerExit(Entity other)`
- `OnDestroy()`

At runtime, the host creates a managed object instance, assigns its entity handle, and dispatches these methods through internal bridge calls.

## Public API Surface

`ScriptableEntity` currently exposes helpers such as:

- `EntityHandle`
- `Entity`
- `Transform`
- `Camera`
- `IsEntityAlive`
- `HasComponent<T>()`
- `GetComponent<T>()`
- `TryGetComponent<T>(out T component)`
- `FindEntityByTag(string tag)`
- `CreateEntity(string name = "Entity")`
- `DestroyEntity(Entity entity)`
- parent/child hierarchy helpers
- coroutine helpers

Related types include:

- `Entity`
- `Transform`
- `SceneManager`
- `DebugApi`
- `RandomApi`
- component wrapper types in `Managed/Limitless.Managed/*.cs`

## Exposed Properties and Reflection

Managed discovery records reflected field metadata per discovered script class.

That reflected metadata is used by the native/runtime bridge to:

- synchronize editor-authored exposed property values into runtime instances
- read back supported runtime field changes into serialized/editor-facing property state

The authoritative discovery/runtime interfaces for this are in:

- `ManagedScriptHost::GetSnapshot()`
- `ManagedScriptHost::SynchronizeScriptExposedProperties(...)`
- `ManagedScriptHost::ReadBackScriptExposedProperties(...)`

## Logging API

`ScriptableEntity` exposes:

- `Debug`

Current logging/assert helpers include:

- `Debug.Log(...)`
- `Debug.LogWarning(...)`
- `Debug.LogError(...)`
- `Debug.LogException(...)`
- `Debug.Assert(...)`
- `Debug.AssertFormat(...)`

Formatting behavior currently supports:

- `string.Format(...)`-style indexed placeholders
- anonymous `{}` placeholder replacement

## Random API

`ScriptableEntity` also exposes:

- `Random`

Current members include:

- `Random.Value`
- `Random.value`
- `Random.InitState(int seed)`
- `Random.Range(int minInclusive, int maxExclusive)`
- `Random.Range(float minInclusive, float maxInclusive)`

## Scene Management API

Managed scripts can request runtime scene transitions through:

- `SceneManager.LoadScene(string sceneIdentifier, LoadSceneMode loadMode = LoadSceneMode.Single)`
- `SceneManager.ReloadCurrentScene()`
- `SceneManager.SetActiveScene(string sceneIdentifier)`
- `SceneManager.UnloadScene(string sceneIdentifier)`

Accepted identifiers are normalized similarly to the runtime scene-loading path:

- scene name
- relative scene path
- `Assets/...`
- optional `.scene` / `.scene.json` suffixes
- slash/case variations that resolve to the same build-scene key

## Entity API

`Limitless.Managed.Entity` currently supports:

- `Handle`
- `HasHandle`
- `IsAlive`
- `Enabled`
- `IsEnabledInHierarchy`
- `Parent`
- `ChildCount`
- `Tag`
- `CompareTag(...)`
- `SetParent(...)`
- `GetChild(...)`
- `GetChildren()`
- `Destroy()`
- `HasComponent<T>()`
- `GetComponent<T>()`
- `TryGetComponent<T>(out T component)`
- `Transform`
- `Entity.Create(...)`
- `Entity.FindEntityByTag(...)`
- `Entity.Null`

## Coroutines

Managed `ScriptableEntity` includes built-in coroutine support.

Common members:

- `StartCoroutine(IEnumerator routine)`
- `StopCoroutine(...)`
- `StopAllCoroutines()`
- `IsCoroutineRunning(...)`

Supported yield values include:

- `null` for next frame
- `WaitForSeconds`
- `WaitForFrames`
- child `Coroutine` handles
- nested enumerators
- numeric frame/seconds shortcuts handled by the coroutine runtime

## Component Wrappers

The managed contract assembly already exposes a broad set of wrapper/component types, including examples such as:

- `Camera`
- `Sprite`
- `Material`
- `Rigidbody2D`
- `BoxCollider2D`
- `CircleCollider2D`
- `CapsuleCollider2D`
- `PolygonCollider2D`
- `EdgeCollider2D`
- `Joint2D`
- `Animator`
- `AnimationEventReceiver`
- `AudioSource`
- `AudioListener2D`
- `AudioListener3D`
- `Canvas`
- `RectTransform`
- `UIImage`
- `UIPanel`
- `UIText`
- `UIButton`
- `UISlider`
- `DirectionalLight2D`
- `PointLight2D`
- `Grid2D`
- `TilemapLayer`
- `ParticleEmitter`

The exact public managed surface is defined by the files in:

- `Managed/Limitless.Managed/`

## Current Constraints

- Managed scripting is still evolving and its public API surface is actively expanding.
- Source authoring is project-driven through `.cs` assets in `Assets/`.
- Runtime discovery is payload/assembly-driven, not raw-source-driven.
- Discovery/build/staging must be current for new project script classes to appear in the editor/runtime host.

## Related Files

- `Managed/Limitless.Managed/`
- `Limitless/Source/Scripting/ManagedScriptHost.*`
- `Limitless/Source/Scripting/ManagedScriptPayload.*`
- `Limitless/Source/Scripting/ManagedScriptHostLifecycle.cpp`
- `Runtime/Source/GameLayerScripting.cpp`
- `Limitless/Source/Scripting/ScriptCoreModuleRuntime.cpp`
- `Scripts/build-managed-runtime-windows.bat`
- `Scripts/build-managed-runtime-unix.sh`
