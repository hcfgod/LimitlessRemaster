# Physics2D Build Guide

This project uses Box2D for 2D physics (`LT_ENABLE_PHYSICS2D`).

## Windows

- Box2D is vendored under `Limitless/Vendor/box2d`.
- Premake links:
  - Debug: `box2DD.lib`
  - Release/Dist: `box2D.lib`
- Build normally:
  - `Scripts/build-windows.bat`

## Linux

- Install Box2D development package:
  - Ubuntu/Debian: `sudo apt-get install libbox2d-dev`
- `Scripts/build-unix.sh` now checks this dependency automatically.
- Build:
  - `bash Scripts/build-unix.sh --config Debug --compiler gcc`

## macOS

- Install Box2D via Homebrew:
  - `brew install box2d`
- Ensure `/opt/homebrew/lib` is visible to linker (already configured in Premake).
- Build:
  - `bash Scripts/build-unix.sh --config Debug --compiler clang`

## Notes

- Physics2D is enabled on all desktop platforms with `LT_ENABLE_PHYSICS2D`.
- If Box2D is missing on macOS/Linux, linker errors referencing `box2d` are expected.
