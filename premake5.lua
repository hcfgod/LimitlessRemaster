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

    -- Output directory format matches CI/CD expectations
    -- Format: Build/Debug-windows-x64/, Build/Release-linux-x64/, etc.
    outputdir = "Build/%{cfg.shortname}-%{cfg.system}-%{cfg.platform}"

    -- Global configuration settings
    filter "configurations:Debug"
        defines 
        { 
            "LT_CONFIG_DEBUG",
            "LT_LOG_LEVEL_TRACE_ENABLED",
            "LT_LOG_LEVEL_DEBUG_ENABLED",
            "LT_LOG_LEVEL_INFO_ENABLED",
            "LT_LOG_LEVEL_WARN_ENABLED",
            "LT_LOG_LEVEL_ERROR_ENABLED",
            "LT_LOG_LEVEL_CRITICAL_ENABLED",
            "LT_LOG_CONSOLE_ENABLED",
            "LT_LOG_FILE_ENABLED",
            "LT_LOG_CORE_ENABLED"
        }
        runtime "Debug"
        symbols "on"
        optimize "off"

    filter "configurations:Release"
        defines 
        { 
            "LT_CONFIG_RELEASE",
            "LT_LOG_LEVEL_INFO_ENABLED",
            "LT_LOG_LEVEL_WARN_ENABLED",
            "LT_LOG_LEVEL_ERROR_ENABLED",
            "LT_LOG_LEVEL_CRITICAL_ENABLED",
            "LT_LOG_CONSOLE_ENABLED",
            "LT_LOG_FILE_ENABLED",
            "LT_LOG_CORE_DISABLED"
        }
        runtime "Release"
        optimize "speed"
        symbols "off"

    filter "configurations:Dist"
        defines 
        { 
            "LT_CONFIG_DIST",
            "LT_LOG_LEVEL_WARN_ENABLED",
            "LT_LOG_LEVEL_ERROR_ENABLED",
            "LT_LOG_LEVEL_CRITICAL_ENABLED",
            "LT_LOG_CONSOLE_DISABLED",
            "LT_LOG_FILE_ENABLED",
            "LT_LOG_CORE_DISABLED"
        }
        runtime "Release"
        optimize "speed"
        symbols "off"
        systemversion "latest"

-- Include sub-projects
include "Limitless"
include "Sandbox"
include "Test"