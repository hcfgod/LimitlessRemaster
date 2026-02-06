project "Limitless"
    location "."
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    staticruntime "on"

    targetdir ("../Build/%{cfg.shortname}-%{cfg.system}-%{cfg.platform}/%{prj.name}")
    objdir ("../Build/Intermediates/%{cfg.shortname}-%{cfg.system}-%{cfg.platform}/%{prj.name}")

    files
    {
        "Source/**.h",
        "Source/**.cpp",
        "Vendor/stb/stb_image/stb_image.cpp",
        "Vendor/glad/glad/glad.c"
    }

    includedirs
    {
        "Source",
        "Vendor/",
        "Vendor/glad",
        "Vendor/spdlog",
        "Vendor/doctest",
        "Vendor/SDL3"
    }

    filter "system:windows"
        cppdialect "C++20"
        staticruntime "On"
        systemversion "latest"

        defines
        {
            "LT_PLATFORM_WINDOWS",
        }

        libdirs
        {
            "Vendor/SDL3/SDL3Libs"
        }

        links
        {
            "SDL3-static",
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

        buildoptions
        {
            "/utf-8",
            "/FS" -- Prevent PDB contention in parallel builds
        }

    filter "system:macosx"
        cppdialect "C++20"
        staticruntime "On"

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
        staticruntime "On"

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