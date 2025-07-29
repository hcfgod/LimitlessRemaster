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
        "x64",
        "ARM64"
    }

    filter "platforms:x64"
        architecture "x64"

    filter "platforms:ARM64"
        architecture "ARM64"

    -- TODO:Make sure output directory format matches CI/CD expectations
    outputdir = "Build/%{cfg.shortname}-%{cfg.system}-%{cfg.platform}"

-- Include sub-projects
include "Limitless"
include "Sandbox"
include "Test"