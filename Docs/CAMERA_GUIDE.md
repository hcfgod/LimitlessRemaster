# Camera Guide

This document describes the **camera system** used by Limitless, how to manage multiple cameras, and recommended patterns for **gameplay** vs **editor** cameras.

## Goals and Guarantees

- Support **2D** and **3D** camera types.
- Make it easy to create and manage **multiple cameras**.
- Provide a clear concept of an **active camera** (the one you use for rendering).
- Keep the camera system **renderer-agnostic** (it only produces view/projection matrices).

## Threading Model

- **Camera objects and `CameraManager` are not thread-safe.**
- Use them from your main/game thread (or provide your own external synchronization).

## Types

- **`OrthographicCamera2D`**: 2D orthographic camera with position (XY), Z-rotation, zoom, and near/far planes.
- **`PerspectiveCamera3D`**: 3D perspective camera with position and yaw/pitch orientation, plus FOV/near/far.

Both camera types provide:

- `GetViewMatrix()`
- `GetProjectionMatrix()`
- `GetViewProjectionMatrix()`

## Managing Multiple Cameras (`CameraManager`)

`CameraManager` owns cameras and hands out stable `CameraId` handles.

Common operations:

- Create cameras: `CreateOrthographic2D`, `CreatePerspective3D`
- Set/get active camera: `SetActiveCamera`, `GetActiveCamera`
- Find by name: `FindByName`
- Destroy: `DestroyCamera`

### Active Camera Behavior

- The **first created camera** becomes active automatically.
- If the active camera is destroyed, the manager selects another remaining camera (or clears active if none remain).

## Gameplay Camera vs Editor Camera

The engine distinguishes intent via `CameraUsage`:

- `CameraUsage::Gameplay`
- `CameraUsage::Editor`

This is a lightweight tag you can use to drive behavior:

- Editor: free-look/orbit camera controllers, gizmo camera, preview cameras.
- Gameplay: player-follow cameras, cutscene cameras, split-screen cameras.

## Example

```cpp
Limitless::CameraManager cameras;

Limitless::CameraManager::Perspective3DCreateInfo gameplayInfo{};
gameplayInfo.Name = "GameplayCamera";
gameplayInfo.Usage = Limitless::CameraUsage::Gameplay;
gameplayInfo.ViewportWidthPixels = 1280;
gameplayInfo.ViewportHeightPixels = 720;

Limitless::CameraId gameplayCamera = cameras.CreatePerspective3D(gameplayInfo);

Limitless::CameraManager::Orthographic2DCreateInfo editorInfo{};
editorInfo.Name = "EditorOrtho";
editorInfo.Usage = Limitless::CameraUsage::Editor;
editorInfo.ViewportWidthPixels = 1280;
editorInfo.ViewportHeightPixels = 720;
editorInfo.Zoom = 1.0f;

Limitless::CameraId editorCamera = cameras.CreateOrthographic2D(editorInfo);

// Switch active camera:
cameras.SetActiveCamera(editorCamera);

const Limitless::Camera* active = cameras.GetActiveCamera();
// active->GetViewProjectionMatrix() is ready for rendering.
```

