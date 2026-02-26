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

    filter "files:Source/**.cpp"
        forceincludes { "PrecompiledHeader.h" }
    filter {}

    files
    {
        "Source/**.h",
        "Source/**.cpp",
        "../Editor/Source/Undo/**.h",
        "../Editor/Source/Undo/**.cpp"
    }

    filter "system:windows"
        files
        {
            "../Resources/LimitlessExecutableIcon.rc",
            "../Resources/LimitlessLogo.ico"
        }
    filter {}

    includedirs
    {
        "../Limitless/Source",
        "../Editor/Source",
        "../Limitless/Vendor/",
        "../Limitless/Vendor/box2d/include",
        "../Limitless/Vendor/spdlog",
        "../Limitless/Vendor/doctest",
        "../Limitless/Vendor/nlohmann",
        "../Limitless/Vendor/SDL3",
        "../Limitless/Vendor/ffmpeg/include"
    }

    links
    {
        "Limitless",
        -- NOTE: `Limitless` is a static lib; link dependencies explicitly for non-MSVC toolchains.
        "VendorZstd",
        "msdf-atlas-gen",
        "msdfgen",
        "freetype",
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

            -- Shader toolchain libraries (vendored).
            "../Limitless/Vendor/shaderc/libs",
            "../Limitless/Vendor/VulkanSDK/lib"
        }

        -- FFmpeg (optional): drop import libs into `Limitless/Vendor/ffmpeg/libs` and DLLs into `Limitless/Vendor/ffmpeg/dlls`.
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

        -- Select the correct shaderc library for each configuration.
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

        -- Copy FFmpeg runtime DLLs next to the built executable.
        local ffmpegDllDir = "../Limitless/Vendor/ffmpeg/dlls"
        if os.isdir(ffmpegDllDir) then
            postbuildcommands
            {
                "{COPYDIR} \"" .. ffmpegDllDir .. "\" \"%{cfg.targetdir}\""
            }
        end

    filter "system:macosx"
        cppdialect "C++20"
        staticruntime "Off"

        defines
        {
            "LT_PLATFORM_MACOS",
            "LT_ENABLE_PHYSICS2D"
        }

        -- Enable FFmpeg decode on macOS when Homebrew FFmpeg is available.
        local macFfmpegLibDir = nil
        local macFfmpegProbeDirs =
        {
            "/opt/homebrew/lib",
            "/usr/local/lib",
            "/opt/homebrew/opt/ffmpeg/lib",
            "/usr/local/opt/ffmpeg/lib"
        }
        for _, candidate in ipairs(macFfmpegProbeDirs) do
            if #os.matchfiles(candidate .. "/libavcodec*.dylib") > 0 then
                macFfmpegLibDir = candidate
                break
            end
        end
        if macFfmpegLibDir ~= nil then
            defines { "LT_ENABLE_FFMPEG" }
            libdirs { macFfmpegLibDir }
            links { "avcodec", "avformat", "avutil", "swresample" }
        end

        libdirs
        {
            "/opt/homebrew/lib",
            "/usr/local/lib",
            "/opt/homebrew/opt/ffmpeg/lib",
            "/usr/local/opt/ffmpeg/lib"
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
            "LT_ENABLE_PHYSICS2D"
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

    -- Configuration-specific defines are provided by the workspace `premake5.lua`
 