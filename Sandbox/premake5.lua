project "Sandbox"
    location "."
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    staticruntime "on"

    targetdir ("../Build/%{cfg.shortname}-%{cfg.system}-%{cfg.platform}/%{prj.name}")
    objdir ("../Build/Intermediates/%{cfg.shortname}-%{cfg.system}-%{cfg.platform}/%{prj.name}")

    files
    {
        "Source/**.h",
        "Source/**.cpp"
    }

    includedirs
    {
        "../Limitless/Vendor",
        "../Limitless/Source",
        "../Limitless/Vendor/spdlog",
        "../Limitless/Vendor/doctest",
        "../Limitless/Vendor/nlohmann",
        "../Limitless/Vendor/SDL3",
    }

    links
    {
        "Limitless"
    }

    filter "system:windows"
        cppdialect "C++20"
        staticruntime "On"
        systemversion "latest"

        defines
        {
            "LT_PLATFORM_WINDOWS"
        }

        libdirs
        {
            "../Limitless/Vendor/SDL3/SDL3Libs"
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
            "/utf-8"
        }

    filter "system:macosx"
        cppdialect "C++20"
        staticruntime "On"

        defines
        {
            "LT_PLATFORM_MAC"
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
                "LT_PLATFORM_MAC_ARM64"
            }

        filter "architecture:x64"
            defines
            {
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
            "m"
        }

    filter "configurations:Debug"
        defines "LT_DEBUG"
        runtime "Debug"
        symbols "on"

    filter "configurations:Release"
        defines "LT_RELEASE"
        runtime "Release"
        optimize "on"

    filter "configurations:Dist"
        defines "LT_DIST"
        runtime "Release"
        optimize "on" 