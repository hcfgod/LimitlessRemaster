workspace "LimitlessRemaster"
    architecture "x64"
    startproject "Sandbox"

    configurations
    {
        "Debug",
        "Release",
        "Dist"
    }

outputdir = "Build/%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

-- Include sub-projects
include "Limitless"
include "Sandbox"
include "Test"