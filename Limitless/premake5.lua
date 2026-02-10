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
    -- - Third-party compilation units are built in a separate project (`LimitlessVendor`)
    -- -----------------------------------------------------------------------------
    pchheader "PrecompiledHeader.h"
    pchsource "Source/PrecompiledHeader.cpp"

    forceincludes { "PrecompiledHeader.h" }

    files
    {
        "Source/**.h",
        "Source/**.cpp",
    }

    includedirs
    {
        "Source",
        "Vendor/",
        "Vendor/glad",
        "Vendor/spdlog",
        "Vendor/doctest",
        "Vendor/SDL3",

        -- Shader toolchain (vendored). These are used by the shader system for
        -- compilation (shaderc) and reflection/transpilation (SPIRV-Cross).
        "Vendor/shaderc/libshaderc/include",
        "Vendor/SPIRV-Cross",

        -- Vulkan headers + loader import library (vendored).
        "Vendor/VulkanSDK/include"
    }

    links
    {
        -- Build SPIRV-Cross from source as a normal static library project.
        "VendorSpirvCross",
        -- Compile stb_image + glad in a separate project without PCH.
        "LimitlessVendor"
    }

    filter "system:windows"
        cppdialect "C++20"
        staticruntime "Off"
        systemversion "latest"

        defines
        {
            "LT_PLATFORM_WINDOWS",
            -- Enable shaderc-based compilation on Windows where we vendor prebuilt libs.
            "LT_ENABLE_SHADERC"
        }

        libdirs
        {
            "Vendor/SDL3/SDL3Libs",

            -- shaderc ships prebuilt .lib files in this folder in our vendor drop.
            "Vendor/shaderc/libs",

            -- Vulkan loader import library.
            "Vendor/VulkanSDK/lib"
        }

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
            links { "shaderc_sharedd" }

        filter { "system:windows", "configurations:Release or Dist" }
            links { "shaderc_shared" }

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
            "LT_PLATFORM_MACOS"
        }

        libdirs
        {
            "/opt/homebrew/lib"
        }

        links
        {
            "SDL3",
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
            "LT_PLATFORM_LINUX"
        }

        libdirs
        {
            "/usr/local/lib"
        }

    -- Ensure C sources are treated as C (not C++).
    filter "files:**.c"
        language "C"
        cdialect "C11"

        links
        {
            "SDL3",
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
    project "LimitlessVendor"
        location "Vendor/LimitlessVendor"
        kind "StaticLib"
        language "C++"
        cppdialect "C++20"
        staticruntime "off"

        targetdir ("%{wks.location}/Build/%{cfg.shortname}-%{cfg.system}-%{cfg.platform}/%{prj.name}")
        objdir ("%{wks.location}/Build/Intermediates/%{cfg.shortname}-%{cfg.system}-%{cfg.platform}/%{prj.name}")

        -- Vendor project must never use the engine PCH.
        flags { "NoPCH" }

        files
        {
            "Vendor/stb/stb_image/stb_image.cpp",
            "Vendor/glad/glad/glad.c"
        }

        includedirs
        {
            "Vendor/",
            "Vendor/glad",
            "Vendor/stb"
        }

        -- Ensure C sources are treated as C (not C++).
        filter "files:**.c"
            language "C"
            cdialect "C11"
        filter {}

    include "Vendor/SPIRV-Cross"
group ""