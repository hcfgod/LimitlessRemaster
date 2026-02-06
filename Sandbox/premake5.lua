project "Sandbox"
    location "."
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    staticruntime "off"

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

    -- ----------------------------------------------------------------------------------
    -- Runtime data
    --
    -- Visual Studio often runs Sandbox with working directory set to `Sandbox/`, while
    -- double-clicking the built .exe runs with working directory set to the output folder.
    --
    -- To avoid "editing the wrong config.json", we copy the source-of-truth config next to
    -- the built executable on every build.
    -- ----------------------------------------------------------------------------------
    postbuildcommands
    {
        -- NOTE:
        -- `%{wks.location}` is relative to the project location for some generators (e.g. gmake2),
        -- so we must include an explicit path separator. Without it, the path becomes `..Sandbox/...`
        -- on Linux/macOS and the CI build fails.
        "{COPY} \"%{wks.location}/Sandbox/config.json\" \"%{cfg.targetdir}\""
    }

    filter "system:windows"
        cppdialect "C++20"
        staticruntime "Off"
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

    -- Configuration-specific defines are provided by the workspace `premake5.lua`
 