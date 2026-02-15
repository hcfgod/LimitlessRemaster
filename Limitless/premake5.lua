project "Limitless"
    location "."
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    staticruntime "off"

    targetdir ("../Build/%{cfg.shortname}-%{cfg.system}-%{cfg.platform}/%{prj.name}")
    objdir ("../Build/Intermediates/%{cfg.shortname}-%{cfg.system}-%{cfg.platform}/%{prj.name}")

    -- Place the compiler PDB in the intermediate directory.
    -- This avoids failures when the output directory does not exist yet (common on clean builds)
    -- and reduces contention with other processes that may touch output folders.
    symbolspath ("%{cfg.objdir}/%{prj.name}.pdb")

    -- -----------------------------------------------------------------------------
    -- Precompiled Header (PCH)
    -- - Enabled for engine C++ translation units for faster iteration
    -- - Disabled for vendor code and all C files (e.g. glad.c)
    -- -----------------------------------------------------------------------------
    pchheader "PrecompiledHeader.h"
    pchsource "Source/PrecompiledHeader.cpp"

    -- Force-include PCH only for engine translation units.
    filter "files:Source/**.cpp"
        forceincludes { "PrecompiledHeader.h" }
    filter {}

    files
    {
        "Source/**.h",
        "Source/**.cpp",
        "Vendor/stb/stb_image/stb_image.cpp",
        "Vendor/glad/glad/glad.c",
        "Vendor/imgui/imgui.cpp",
        "Vendor/imgui/imgui_demo.cpp",
        "Vendor/imgui/imgui_draw.cpp",
        "Vendor/imgui/imgui_tables.cpp",
        "Vendor/imgui/imgui_widgets.cpp",
        "Vendor/imgui/backends/imgui_impl_sdl3.cpp"
        -- imgui_impl_opengl3.cpp is included via Source/ImGui/ImGuiOpenGL3Backend.cpp
        -- which includes GLAD first, ensuring OpenGL symbols are available on all platforms
        -- (Linux/gmake2 forceincludes can be unreliable for vendor files).
    }

    filter "files:Vendor/imgui/**"
        flags { "NoPCH" }

    filter "files:Source/ImGui/ImGuiOpenGL3Backend.cpp"
        flags { "NoPCH" }

    filter "files:Vendor/**"
        flags { "NoPCH" }
    filter "files:**.c"
        flags { "NoPCH" }
        language "C"
        cdialect "C11"
    filter {}

    includedirs
    {
        "Source",
        "Vendor/",
        "Vendor/box2d/include",
        "Vendor/Zstd/include",
        "Vendor/glad",
        "Vendor/spdlog",
        "Vendor/doctest",
        "Vendor/SDL3",
        "Vendor/ffmpeg/include",
        "Vendor/imgui",

        -- Shader toolchain (vendored). These are used by the shader system for
        -- compilation (shaderc) and reflection/transpilation (SPIRV-Cross).
        "Vendor/shaderc/libshaderc/include",
        "Vendor/SPIRV-Cross",
        "Vendor/msdf-atlas-gen/msdf-atlas-gen",
        "Vendor/msdf-atlas-gen/msdfgen",
        "Vendor/msdf-atlas-gen/msdfgen/include",
        "Vendor/msdf-atlas-gen/msdfgen/freetype/include",

        -- Vulkan headers + loader import library (vendored).
        "Vendor/VulkanSDK/include"
    }

    links
    {
        -- Build SPIRV-Cross from source as a normal static library project.
        "VendorSpirvCross",
        -- Build Zstd from source as a normal static library project.
        "VendorZstd",
        "msdf-atlas-gen",
        "msdfgen",
        "freetype",
    }

    filter "system:windows"
        cppdialect "C++20"
        staticruntime "Off"
        systemversion "latest"

        defines
        {
            "LT_PLATFORM_WINDOWS",
            -- Enable shaderc-based compilation on Windows where we vendor prebuilt libs.
            "LT_ENABLE_SHADERC",
            "LT_ENABLE_ZSTD",
            "LT_ENABLE_PHYSICS2D"
        }

        libdirs
        {
            "Vendor/SDL3/SDL3Libs",
            "Vendor/box2d/libs",

            -- shaderc ships prebuilt .lib files in this folder in our vendor drop.
            "Vendor/shaderc/libs",

            -- Vulkan loader import library.
            "Vendor/VulkanSDK/lib"
        }

        -- FFmpeg (optional): drop import libs into `Vendor/ffmpeg/libs` and DLLs into `Vendor/ffmpeg/dlls`.
        local ffmpegLibDir = "Vendor/ffmpeg/libs"
        local ffmpegLibs = os.matchfiles(ffmpegLibDir .. "/*.lib")
        if #ffmpegLibs > 0 then
            defines { "LT_ENABLE_FFMPEG" }
            libdirs { ffmpegLibDir }
            links { "avcodec", "avformat", "avutil", "swresample" }
        end

        -- Zstd is built from source via VendorZstd.

        links
        {
            "SDL3-static",
            "vulkan-1",
            "user32",
            "gdi32",
            "winmm",
            "imm32",
            "ole32",
            "oleaut32",
            "uuid",
            "version",
            "advapi32",
            "setupapi",
            "shell32",
            "psapi"
        }

        -- Select the correct shaderc library for each configuration.
        filter { "system:windows", "configurations:Debug" }
            links
            {
                "shaderc_sharedd",
                "box2DD"
            }

        filter { "system:windows", "configurations:Release or Dist" }
            links
            {
                "shaderc_shared",
                "box2D"
            }

        filter "system:windows"
            buildoptions
            {
                "/utf-8",
                "/FS" -- Prevent PDB contention in parallel builds
            }

    filter "system:macosx"
        cppdialect "C++20"
        staticruntime "Off"

        defines
        {
            "LT_PLATFORM_MACOS",
            "LT_ENABLE_ZSTD",
            "LT_ENABLE_PHYSICS2D"
        }

        libdirs
        {
            "/opt/homebrew/lib",
            "/usr/local/lib"
        }

        links
        {
            "box2d",
            "SDL3",
            "z",
            "Cocoa.framework",
            "CoreServices.framework",
            "IOKit.framework",
            "CoreAudio.framework",
            "AudioToolbox.framework",
            "ForceFeedback.framework",
            "Carbon.framework",
            "CoreVideo.framework",
            "AVFoundation.framework",
            "Metal.framework",
            "QuartzCore.framework"
        }

        filter { "system:macosx", "architecture:ARM64" }
            defines
            {
                "LT_ARCHITECTURE_ARM64",
                "LT_PLATFORM_MAC_ARM64"
            }

        filter { "system:macosx", "architecture:x64" }
            defines
            {
                "LT_ARCHITECTURE_X64",
                "LT_PLATFORM_MAC_X64"
            }

    filter "system:linux"
        cppdialect "C++20"
        staticruntime "Off"

        defines
        {
            "LT_PLATFORM_LINUX",
            "LT_ENABLE_ZSTD",
            "LT_ENABLE_PHYSICS2D"
        }

        libdirs
        {
            "/usr/local/lib"
        }

    -- Ensure C sources are treated as C (not C++).
    filter "files:**.c"
        language "C"
        cdialect "C11"

    filter {}
    filter "system:linux"
        links
        {
            "box2d",
            "SDL3",
            "z",
            "X11",
            "Xext",
            "Xcursor",
            "Xinerama",
            "Xi",
            "Xrandr",
            "Xss",
            "Xxf86vm",
            "asound",
            "dbus-1",
            "ibus-1.0",
            "udev",
            "pthread",
            "dl",
            "m",
            "atomic"
        }

        filter { "system:linux", "architecture:ARM64" }
            defines
            {
                "LT_ARCHITECTURE_ARM64"
            }

        filter { "system:linux", "architecture:x64" }
            defines
            {
                "LT_ARCHITECTURE_X64"
            }

    -- Compiler-specific defines
    filter "toolset:msc"
        defines { "LT_COMPILER_MSVC" }

    filter "toolset:gcc"
        defines { "LT_COMPILER_GCC" }

    filter "toolset:clang"
        defines { "LT_COMPILER_CLANG" }

    -- Configuration-specific defines are provided by the workspace `premake5.lua`

group "Dependencies"
    include "Vendor/SPIRV-Cross"
    include "Vendor/Zstd"
    include "Vendor/msdf-atlas-gen"
group ""