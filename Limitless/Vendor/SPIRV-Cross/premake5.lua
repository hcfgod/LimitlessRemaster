project "VendorSpirvCross"
    location "."
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
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

