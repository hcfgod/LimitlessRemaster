project "Test"
    location "."
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    staticruntime "off"

    targetdir ("../Build/%{cfg.shortname}-%{cfg.system}-%{cfg.platform}/%{prj.name}")
    objdir ("../Build/Intermediates/%{cfg.shortname}-%{cfg.system}-%{cfg.platform}/%{prj.name}")

    -- Use the engine precompiled header for faster iteration builds.
    -- Note: The header lives in ../Limitless/Source, which is already in includedirs.
    pchheader "PrecompiledHeader.h"
    pchsource "Source/PrecompiledHeader.cpp"

    forceincludes { "PrecompiledHeader.h" }

    files
    {
        "Source/**.h",
        "Source/**.cpp"
    }

    includedirs
    {
        "../Limitless/Source",
        "../Limitless/Vendor/",
        "../Limitless/Vendor/spdlog",
        "../Limitless/Vendor/doctest",
        "../Limitless/Vendor/nlohmann",
        "../Limitless/Vendor/SDL3"
    }

    links
    {
        "Limitless",
        "LimitlessVendor"
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
            "../Limitless/Vendor/SDL3/SDL3Libs",

            -- Shader toolchain libraries (vendored).
            "../Limitless/Vendor/shaderc/libs",
            "../Limitless/Vendor/VulkanSDK/lib"
        }

        links
        {
            "SDL3-static",
            "VendorSpirvCross",
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

        -- Copy shaderc runtime DLLs next to the built executable.
        -- Users can drop the required binaries into `Limitless/Vendor/shaderc/dlls`.
        local shadercDllDir = "../Limitless/Vendor/shaderc/dlls"
        if os.isdir(shadercDllDir) then
            postbuildcommands
            {
                "{COPYDIR} \"" .. shadercDllDir .. "\" \"%{cfg.targetdir}\""
            }
        end

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
 