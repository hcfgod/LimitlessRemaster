project "Limitless"
    location "."
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    staticruntime "on"

    targetdir ("../Build/%{cfg.buildcfg}-%{cfg.system}-x64/%{prj.name}")
    objdir ("../Build/Intermediates/%{cfg.buildcfg}-%{cfg.system}-x64/%{prj.name}")

    files
    {
        "Source/**.h",
        "Source/**.cpp"
    }

    includedirs
    {
        "Source",
        "Vendor/spdlog",
        "Vendor/doctest",
        "Vendor/nlohmann"
    }

    filter "system:windows"
        cppdialect "C++20"
        staticruntime "On"
        systemversion "latest"

        defines
        {
            "LT_PLATFORM_WINDOWS"
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