# Shader Toolchain Guide (shaderc + SPIRV-Cross)

This project vendors a shader toolchain so the engine can:

- Compile GLSL into SPIR-V using **shaderc**
- Transpile / reflect SPIR-V using **SPIRV-Cross**

Today, Limitless runs an OpenGL backend. Even so, we route shader sources through SPIR-V in order to:

- Validate shaders consistently on the CPU (off the render thread)
- Prepare for a Vulkan backend where SPIR-V is the native input
- Enable reflection and future tooling (pipeline layout generation, resource binding validation, etc.)

## Where the vendor code lives

- **shaderc**: `Limitless/Vendor/shaderc/`
  - Prebuilt libraries (Windows): `Limitless/Vendor/shaderc/libs/`
    - `shaderc_shared.lib` (Release/Dist)
    - `shaderc_sharedd.lib` (Debug)
- **SPIRV-Cross**: `Limitless/Vendor/SPIRV-Cross/`
  - Built from source as a normal Premake project (`VendorSpirvCross`)
- **Vulkan headers/loader import lib**: `Limitless/Vendor/VulkanSDK/`
  - Headers: `Limitless/Vendor/VulkanSDK/include/`
  - Windows import library: `Limitless/Vendor/VulkanSDK/lib/vulkan-1.lib`

## Build system integration (Premake)

The `Limitless` static library is configured to:

- Add include paths for shaderc and SPIRV-Cross
- Link the SPIRV-Cross static library project (`VendorSpirvCross`)
- On Windows, link:
  - `shaderc_shared` / `shaderc_sharedd` (depending on configuration)
  - `vulkan-1` (Vulkan loader import library)

On non-Windows platforms we currently do **not** enable shaderc linking by default, because the repository only includes
Windows prebuilt shaderc libraries. The engine code is guarded by `LT_ENABLE_SHADERC` so other platforms can still build.

## Runtime notes about `vulkan-1.dll`

This repository vendors `vulkan-1.lib` (the import library), but does not currently contain `vulkan-1.dll`.

On Windows, `vulkan-1.dll` is normally provided by the **installed Vulkan Runtime** (GPU driver / Vulkan redistributable).
If you want to ship Vulkan with the game without requiring the runtime to be installed, you will need to bundle the
redistributable loader in your installer/package.

## Shader pipeline behavior

When `LT_ENABLE_SHADERC` is enabled (Windows):

1. `Assets/*.glsl` is parsed into per-stage GLSL (`#type vertex` / `#type fragment`)
2. Each stage is compiled into SPIR-V using shaderc (CPU worker thread)
3. SPIR-V is reflected using SPIRV-Cross (CPU worker thread) to prove the toolchain and enable future layout generation
4. The original authored GLSL is sent to the render thread and compiled by the OpenGL backend

**Why we do not transpile back to GLSL yet**:
SPIRV-Cross output can restructure/rename uniforms (UBOs, flattened structs, etc.). Our current OpenGL material path binds
uniforms by authored names (e.g. `u_ViewProjection`), so transpiled GLSL can cause “uniform not found” behavior and break rendering.

When `LT_ENABLE_SHADERC` is **not** enabled, shaders follow the legacy path: parsed GLSL is compiled directly by OpenGL.

