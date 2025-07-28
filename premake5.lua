workspace "LimitlessRemaster"
    startproject "Sandbox"

    configurations
    {
        "Debug",
        "Release",
        "Dist"
    }

    platforms
    {
        "x86_64",
        "ARM64"
    }

outputdir = "Build/%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

-- Include sub-projects
include "Limitless"
include "Sandbox"
include "Test"