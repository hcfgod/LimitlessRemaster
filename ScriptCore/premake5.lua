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
        "../Limitless/Source/Scene/SceneManager.cpp",
        "../Limitless/Source/Scripting/Input.cpp",
        "../Limitless/Source/Scripting/InputActions.cpp",
        "../Limitless/Source/Scripting/Physics2D.cpp",
        "../Limitless/Source/Scripting/Debug.cpp",
        "../Limitless/Source/Scripting/Coroutine.cpp",
        "../Limitless/Source/Scripting/Random.cpp",
        "../Limitless/Source/Scripting/ScriptableEntity.cpp",
        "../Limitless/Source/Scene/ParticleEmitterSystem.cpp",
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

    -- Keep static script registrar constructors alive in optimized builds.
    filter "configurations:Release or Dist"
        linkoptions
        {
            "/OPT:NOREF",
            "/OPT:NOICF"
        }
