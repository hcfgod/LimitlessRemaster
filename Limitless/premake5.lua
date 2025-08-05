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
            "LT_PLATFORM_WINDOWS"
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
            "/std:c++20"
        }

    filter "system:macosx"
        cppdialect "C++20"
        staticruntime "On"

        defines
        {
            "LT_PLATFORM_MACOS",
            "LT_PLATFORM_MAC"
        }

        buildoptions
        {
            "-std=c++20"
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

        filter "architecture:ARM64"
            defines
            {
                "LT_ARCHITECTURE_ARM64",
                "LT_PLATFORM_MAC_ARM64"
            }
            
            -- Handle C files specifically for macOS ARM64
            filter "files:**.c"
                buildoptions { "-std=c11" }

        filter "architecture:x64"
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

        buildoptions
        {
            "-std=c++20"
        }

        libdirs
        {
            "/usr/local/lib"
        }

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

        filter "architecture:ARM64"
            defines
            {
                "LT_ARCHITECTURE_ARM64"
            }

        filter "architecture:x64"
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

    -- Configuration-specific settings (inherited from workspace)
    filter "configurations:Debug"
        defines { "LIMITLESS_DEBUG" }

    filter "configurations:Release"
        defines { "LIMITLESS_RELEASE" }

    filter "configurations:Dist"
        defines { "LIMITLESS_DIST" } 