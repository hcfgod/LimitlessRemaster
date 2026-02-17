project "VendorZstd"
    location "."
    kind "StaticLib"
    language "C"
    cdialect "C11"
    -- Upstream vendor library: keep warnings in engine code paths only.
    warnings "Off"
    staticruntime "off"

    -- Keep vendor build outputs alongside the rest of the workspace.
    targetdir ("%{wks.location}/Build/%{cfg.shortname}-%{cfg.system}-%{cfg.platform}/%{prj.name}")
    objdir ("%{wks.location}/Build/Intermediates/%{cfg.shortname}-%{cfg.system}-%{cfg.platform}/%{prj.name}")

    -- Vendor code: no engine PCH.
    flags { "NoPCH" }

    files
    {
        -- Core library (compression + decompression).
        "lib/common/**.h",
        "lib/common/**.c",
        "lib/compress/**.h",
        "lib/compress/**.c",
        "lib/decompress/**.h",
        "lib/decompress/**.c",
        "lib/dictBuilder/**.h",
        "lib/dictBuilder/**.c",

        -- Public headers (copied from upstream for include path stability).
        "include/**.h"
    }

    -- Exclude optional legacy/deprecated paths to keep build time smaller and avoid
    -- compiling formats we don't intend to support in shipping bundles yet.
    removefiles
    {
        "lib/legacy/**",
        "lib/deprecated/**",
        "lib/dll/**"
    }

    includedirs
    {
        "include",
        "lib",
        "lib/common"
    }

    filter "system:windows"
        systemversion "latest"
        buildoptions
        {
            "/utf-8",
            "/FS"
        }

    -- Linux: we do not build the HUF decompress assembly (huf_decompress_amd64.S),
    -- so disable ASM so the C code does not reference those symbols.
    filter "system:linux"
        defines { "ZSTD_DISABLE_ASM" }

