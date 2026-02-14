# Editor Shared Assets

The editor now supports engine-level shared assets that are available across projects.

## Resolution Rules

For Unity-style keys like `Assets/...`, resolution order is:

1. Active project `Assets/` (project assets always win)
2. Editor shared `Assets/` directory (engine-level fallback)
3. Built-in aliases (currently `Assets/Fonts/Default.ttf` on Windows)

This ensures project content is never overridden by shared defaults.

## Shared Assets Root

- Default: discovered by walking upward from the editor executable path
- Optional override: `LIMITLESS_SHARED_ASSET_ROOT`
  - Value must be a directory that contains `Assets/`

## Default Font

- New Text components are initialized with:
  - `Assets/Fonts/Default.ttf`
- If this key is not present in project/shared assets, a Windows system font fallback is used.

## Built-In Shared Materials

The shared editor assets should include:

- `Assets/Materials/Renderer2D_TexturedQuad.material.json`
- `Assets/Materials/Renderer2D_MSDFText.material.json`

These are used by `Renderer2D` as engine defaults and are now resolved through the same project-first/shared-fallback path.

## Default Scene Name

- New projects now create `Assets/Scenes/SampleScene.scene.json` by default.
