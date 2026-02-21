# No-Source Editor Distribution

This project supports a shipped-editor workflow where users do not need a local `LimitlessRemaster` source checkout.

## Build Backend Modes

- `InternalToolchain`: Uses install-relative bundled toolchain assets from `Toolchain/`.
- `LegacySdk`: Uses the original source-workspace build scripts and paths.

## Internal Toolchain Contract

When `Build Backend` is set to `Internal Toolchain`, the configured root must contain:

- `Scripts/build-scriptcore-*.{bat,sh}`
- `Scripts/build-project-scriptcore-*.{bat,sh}`
- `RuntimeTemplates/<config-platform>/...` (prebuilt Runtime template files)
- `Build/Generated/ScriptCore/` (mirror source output root)
- `SDK/include/` (public scripting headers only, no engine `.cpp`)
- `SDK/vendor/` (third-party headers required by scripting API)
- `SDK/lib/<config-platform>/` (includes `Limitless.lib` and `ScriptCoreHostGlue.lib`)

## Packaging

Use:

- `Scripts/package-editor-windows.ps1`
- or `Scripts/package-editor-windows.bat`

The packaging script emits:

- `LimitlessEditor/` editor binaries
- `LimitlessEditor/Toolchain/` internal compile/build assets
- `LimitlessEditor/Toolchain/RuntimeTemplates/<config-platform>/...`
- `LimitlessEditor/Toolchain/SDK/` headers + libraries for project script compilation

## Versioning Rule

Package editor binaries and toolchain assets from the same commit. If script build scripts, ScriptCore layout, or runtime template layout change, regenerate shipped packages.
