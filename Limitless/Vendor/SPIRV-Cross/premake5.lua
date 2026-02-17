project "VendorSpirvCross"
    location "."
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    -- Upstream vendor library: keep warnings in engine code paths only.
    warnings "Off"
    staticruntime "off"

    -- Keep vendor build outputs alongside the rest of the workspace.
    targetdir ("%{wks.location}/Build/%{cfg.shortname}-%{cfg.system}-%{cfg.platform}/%{prj.name}")
    objdir ("%{wks.location}/Build/Intermediates/%{cfg.shortname}-%{cfg.system}-%{cfg.platform}/%{prj.name}")

    files
    {
        -- Core + GLSL backend + reflection. We intentionally do not compile the
        -- HLSL/MSL backends yet, but it's easy to add later.
        "spirv_cross.cpp",
        "spirv_cross_parsed_ir.cpp",
        "spirv_cross_util.cpp",
        "spirv_parser.cpp",
        "spirv_cfg.cpp",
        "spirv_glsl.cpp",
        "spirv_reflect.cpp",

        "spirv*.hpp",
        "spirv*.h"
    }

    includedirs
    {
        "."
    }

    filter "system:windows"
        systemversion "latest"
        buildoptions
        {
            "/utf-8",
            "/FS"
        }

    -- SPIRV-Cross is vendored third-party code. In C++20, implicit `this`
    -- capture via `[=]` is deprecated and emits warning C4855 on MSVC.
    -- Keep vendor output clean without modifying upstream source files.
    filter "toolset:msc"
        disablewarnings { "4855" }

    filter "toolset:clang"
        buildoptions { "-Wno-deprecated-this-capture" }

    filter {}
