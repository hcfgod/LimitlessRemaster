include "msdfgen"

project "msdf-atlas-gen"
	kind "StaticLib"
	language "C++"
	cppdialect "C++20"
    staticruntime "on" -- default, overridden per-config

	targetdir ("Binaries/" .. OutputDir .. "/%{prj.name}")
    objdir ("Binaries/Intermediates/" .. OutputDir .. "/%{prj.name}")

	files
	{
		"msdf-atlas-gen/**.h",
    	"msdf-atlas-gen/**.hpp",
    	"msdf-atlas-gen/**.cpp"
	}

	includedirs
	{
		"msdf-atlas-gen",
		"msdfgen",
		"msdfgen/include"
	}

	defines
	{
		"_CRT_SECURE_NO_WARNINGS"
	}

	links
	{
		"msdfgen"
	}

	filter "system:windows"
		systemversion "latest"

	filter "configurations:Debug"
		runtime "Debug"
		staticruntime "off" -- Use /MDd (Multi-threaded Debug DLL)
		symbols "on"

	filter "configurations:Release"
		runtime "Release"
		staticruntime "on" -- Use /MT (Multi-threaded static)
		optimize "on"

	filter "configurations:Dist"
		runtime "Release"
		staticruntime "on" -- Use /MT (Multi-threaded static)
		optimize "on"
        symbols "off"