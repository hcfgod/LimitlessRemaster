# Lighting2D Guide

`Lighting2D` adds deferred-style 2D lighting to the scene renderer with:

- Directional lights
- Point lights
- Normal mapped sprites
- Dynamic shadows from authored occluders
- Soft shadow controls per light and per project

It is fully integrated with ECS, scene save/load, scene clone, editor undo/redo, inspector authoring, and viewport gizmos.

## Supported Targets

- World sprites (`SpriteComponent`) using lit materials
- World-space text is not currently supported in the UI-only text pipeline
- Canvas UI text (`UITextComponent`) renders in the UI pass after world lighting

## Rendering Path

Header: `Limitless/Source/Graphics/Lighting2DRenderer.h`  
Implementation: `Limitless/Source/Graphics/Lighting2DRenderer.cpp`

Pipeline overview:

1. **GBuffer Albedo pass**: world render callback fills albedo target.
2. **GBuffer Normal pass**: sprites are redrawn with `Lighting2D_GBufferNormalPass.glsl`.
3. **Light accumulation**:
   - `Lighting2D_DirectionalLight.glsl`
   - `Lighting2D_PointLight.glsl`
4. **Composite pass**: `Lighting2D_Composite.glsl` combines albedo and accumulated light.

When lighting is disabled or resources are unavailable, renderer falls back to the legacy scene path.

## Authoring Workflow

### 1) Project Lighting Settings

Open **Project Settings -> Lighting 2D** and configure:

- `Enabled`
- `EnableNormalMaps`
- `EnableShadows`
- `AmbientColor`, `AmbientIntensity`
- `ShadowQualityLevel`
- `MaxDirectionalLights`, `MaxPointLights`
- `MaxShadowSegments`
- `ShadowSoftnessScale`
- `MaxShadowSamplesPerLight`

Settings are stored in the project settings file via `Project::SaveLighting2DSettings(...)`.

### 2) Lit Materials

Use or duplicate:

- `Assets/Materials/Lighting2D_DefaultLit.material.json`

Recommended material fields:

- Albedo texture in base texture slot
- Normal texture in normal slot
- `normalStrength` tuned per asset
- `roughness` and `specularIntensity` reserved for extended shading controls

### 3) Lights

Add components in Inspector:

- `DirectionalLight2DComponent`
  - Optional `UseEntityRotation` for direction from transform rotation
  - Color, intensity, shadow strength, softness, samples, distance
- `PointLight2DComponent`
  - Color, intensity, radius, falloff
  - Shadow strength, softness, samples, bias

### 4) Shadow Casters

Add `ShadowOccluder2DComponent` and choose source mode:

- `ManualPolygon`: hand-authored points
- `PhysicsCollider`: auto-build from same-entity collider
  - Uses `BoxCollider2DComponent` rectangle
  - Uses `CircleCollider2DComponent` approximated polygon

Optional:

- `Closed` loop handling for segment generation
- `Extrusion` to grow occluder shape and reduce light leaks

## Editor Integration

- Add/remove lighting components from Inspector
- Full undo/redo tracking for inspector edits
- Full undo/redo tracking for viewport light/occluder gizmo drags
- Scene save/load and scene clone preserve authored lighting data

Viewport gizmos support:

- Directional light direction drag
- Point light radius drag
- Shadow occluder polygon point drag

## Diagnostics

Lighting runtime diagnostics are exposed through `Lighting2DRenderer::GetDiagnostics()` and shown in the editor diagnostics panel:

- Lighting path active state
- Directional and point light counts rendered this frame
- Occluder count and shadow segment count
- CPU build time and submit time

Use diagnostics to separate quality tuning from performance bottlenecks.

## Quality Tuning Guidance

Start from these defaults for gameplay scenes:

- `ShadowQualityLevel = 1`
- `MaxDirectionalLights = 2-4`
- `MaxPointLights = 16-32`
- `MaxShadowSegments = 96-160`
- `MaxShadowSamplesPerLight = 8-12`

For heavier scenes:

- Lower `MaxPointLights` first
- Lower `MaxShadowSegments` second
- Lower `MaxShadowSamplesPerLight` third
- Reduce per-light `ShadowSoftness` before disabling shadows entirely

For cleaner contact edges and fewer leaks:

- Increase occluder `Extrusion` slightly (`0.05` to `0.25`)
- Increase point light `ShadowBias` only as needed
- Keep polygon points convex and non-self-intersecting when possible

## Validation Checklist

- Add each lighting component and verify undo/redo in Inspector
- Drag each lighting gizmo and verify undo/redo in viewport
- Save, reload, and confirm light/occluder data round-trips
- Verify UI text overlays render correctly over lit world content
- Validate fallback path by disabling `Enabled` in project settings
- Check diagnostics while scaling lights and occluder complexity
