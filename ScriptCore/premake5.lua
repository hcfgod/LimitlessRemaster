project "ScriptCore"
    location "."
    kind "SharedLib"
    language "C++"
    cppdialect "C++20"
    staticruntime "off"

    targetdir ("../Build/%{cfg.shortname}-%{cfg.system}-%{cfg.platform}/Editor")
    objdir ("../Build/Intermediates/%{cfg.shortname}-%{cfg.system}-%{cfg.platform}/%{prj.name}")
    symbolspath ("%{cfg.objdir}/%{prj.name}.pdb")

    local generatedScriptCoreDirectory = "../Build/Generated/ScriptCore"

    files
    {
        "Source/**.h",
        "Source/**.cpp",
        "../Limitless/Source/Scripting/ScriptableEntity.cpp",
        generatedScriptCoreDirectory .. "/**.h",
        generatedScriptCoreDirectory .. "/**.cpp"
    }

    includedirs
    {
        "../Limitless/Source",
        "../Limitless/Vendor",
        "../Limitless/Vendor/spdlog",
        "../Limitless/Vendor/nlohmann",
        "../Limitless/Vendor/SDL3",
        "Source",
        generatedScriptCoreDirectory
    }

    filter "system:windows"
        cppdialect "C++20"
        staticruntime "Off"
        systemversion "latest"
        defines
        {
            "LT_PLATFORM_WINDOWS",
            "SCRIPTCORE_EXPORTS"
        }
        buildoptions
        {
            "/utf-8",
            "/FS"
        }

    filter "system:linux or system:macosx"
        defines
        {
            "SCRIPTCORE_EXPORTS"
        }

    -- ScriptCore is rebuilt while Editor is running.
    -- Disable linker debug-info file emission in Debug to avoid PDB lock contention.
    filter "configurations:Debug"
        symbols "off"
