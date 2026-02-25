project "Editor"
    location "."
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    staticruntime "off"

    targetdir ("../Build/%{cfg.shortname}-%{cfg.system}-%{cfg.platform}/%{prj.name}")
    objdir ("../Build/Intermediates/%{cfg.shortname}-%{cfg.system}-%{cfg.platform}/%{prj.name}")

    pchheader "PrecompiledHeader.h"
    pchsource "Source/PrecompiledHeader.cpp"

    filter "files:Source/**.cpp"
        forceincludes { "PrecompiledHeader.h" }
    filter {}

    files
    {
        "Source/**.h",
        "Source/**.cpp"
    }

    filter "system:windows"
        files
        {
            "../Resources/LimitlessExecutableIcon.rc",
            "../Resources/LimitlessLogo.ico"
        }
    filter {}

    removefiles
    {
        "Source/Scripting/UserScripts/**.h",
        "Source/Scripting/UserScripts/**.cpp"
    }

    includedirs
    {
        "Source",
        "Source/Panels",
        "Source/Layer",
        "Source/Core",
        "Source/Systems",
        "Source/Operations",
        "Source/Dialogs",
        "Source/Utilities",
        "Source/Scripting",
        "Source/Undo",
        "../Limitless/Vendor",
        "../Limitless/Vendor/box2d/include",
        "../Limitless/Source",
        "../Limitless/Vendor/spdlog",
        "../Limitless/Vendor/nlohmann",
        "../Limitless/Vendor/SDL3",
        "../Limitless/Vendor/ffmpeg/include",
        "../Limitless/Vendor/imgui",
    }

    links
    {
        "Limitless",
        "VendorZstd",
        -- NOTE: Static library dependencies from Limitless are not reliably transitive
        -- on Unix toolchains; link text rendering stack explicitly.
        "msdf-atlas-gen",
        "msdfgen",
        "freetype",
    }

    postbuildcommands
    {
        "{COPY} \"%{wks.location}/Editor/config.json\" \"%{cfg.targetdir}\"",
        "{COPY} \"%{wks.location}/Resources/LimitlessLogo.ico\" \"%{cfg.targetdir}\"",
        "{COPY} \"%{wks.location}/Editor/imgui-default.ini\" \"%{cfg.targetdir}\"",
        "{COPY} \"%{wks.location}/Editor/imgui.ini\" \"%{cfg.targetdir}\""
    }

    filter "system:windows"
        cppdialect "C++20"
        staticruntime "Off"
        systemversion "latest"
        -- Match Runtime behavior: prefer dynamic CRT and ignore static CRT defaults
        -- from prebuilt third-party libraries.
        ignoredefaultlibraries { "LIBCMT", "LIBCMTD" }

        defines
        {
            "LT_PLATFORM_WINDOWS",
            "_CRT_SECURE_NO_WARNINGS",
            "_CRT_NONSTDC_NO_WARNINGS",
            "LT_ENABLE_PHYSICS2D"
        }

        libdirs
        {
            "../Limitless/Vendor/SDL3/SDL3Libs",
            "../Limitless/Vendor/box2d/libs",
            "../Limitless/Vendor/shaderc/libs",
            "../Limitless/Vendor/VulkanSDK/lib"
        }

        local ffmpegLibDir = "../Limitless/Vendor/ffmpeg/libs"
        local ffmpegLibs = os.matchfiles(ffmpegLibDir .. "/*.lib")
        if #ffmpegLibs > 0 then
            defines { "LT_ENABLE_FFMPEG" }
            libdirs { ffmpegLibDir }
            links { "avcodec", "avformat", "avutil", "swresample" }
        end

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

        filter { "system:windows", "configurations:Debug" }
            links
            {
                "shaderc_sharedd",
                "box2DD"
            }
            -- box2DD prebuilt libs may ship without matching PDBs.
            -- Keep debug link output clean while retaining symbols for our code.
            linkoptions { "/ignore:4099" }

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
            "/FS"
        }

        -- Copy shaderc runtime DLLs
        local shadercDllDir = "../Limitless/Vendor/shaderc/dlls"
        if os.isdir(shadercDllDir) then
            postbuildcommands { "{COPYDIR} \"" .. shadercDllDir .. "\" \"%{cfg.targetdir}\"" }
        end

        -- Copy FFmpeg runtime DLLs
        local ffmpegDllDir = "../Limitless/Vendor/ffmpeg/dlls"
        if os.isdir(ffmpegDllDir) then
            postbuildcommands { "{COPYDIR} \"" .. ffmpegDllDir .. "\" \"%{cfg.targetdir}\"" }
        end

    filter "system:macosx"
        cppdialect "C++20"
        staticruntime "Off"

        defines
        {
            "LT_PLATFORM_MACOS",
            "LT_ENABLE_PHYSICS2D"
        }

        libdirs
        {
            "/opt/homebrew/lib"
        }

        links
        {
            "box2d",
            "SDL3",
            "z",
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
            "LT_PLATFORM_LINUX",
            "LT_ENABLE_PHYSICS2D",
            "LT_ENABLE_FFMPEG"
        }

        libdirs
        {
            "/usr/local/lib",
            "../Limitless/Vendor/box2d/libs/linux"
        }

        links
        {
            "box2d",
            "SDL3",
            "avcodec",
            "avformat",
            "avutil",
            "swresample",
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
