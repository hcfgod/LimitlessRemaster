@echo off
setlocal enabledelayedexpansion

REM Parse command line arguments
set CONFIGURATION=Debug
set PLATFORM=x64

if "%1"=="Release" set CONFIGURATION=Release
if "%1"=="Dist" set CONFIGURATION=Dist
if "%2"=="ARM64" set PLATFORM=ARM64

echo Building LimitlessRemaster in %CONFIGURATION% configuration for %PLATFORM%...

REM Change to the project root directory
cd /d "%~dp0.."

REM Generate Visual Studio solution with fallback
echo Generating Visual Studio solution...
set VS_ACTION=vs2022
Vendor\Premake\premake5.exe %VS_ACTION%
if errorlevel 1 (
    echo vs2022 failed, trying vs2019...
    set VS_ACTION=vs2019
    Vendor\Premake\premake5.exe %VS_ACTION%
    if errorlevel 1 (
        echo vs2019 failed, trying vs2017...
        set VS_ACTION=vs2017
        Vendor\Premake\premake5.exe %VS_ACTION%
        if errorlevel 1 (
            echo Error: Failed to generate Visual Studio solution with any supported action
            exit /b 1
        )
    )
)
echo Generated solution with action: %VS_ACTION%

REM Build the solution
echo Building solution...
msbuild LimitlessRemaster.sln /p:Configuration=%CONFIGURATION% /p:Platform=%PLATFORM% /m
if errorlevel 1 (
    echo Error: Build failed
    exit /b 1
)

echo Build completed successfully!
echo Output directory: Build\%CONFIGURATION%-windows-x86_64\

REM Run tests if they exist
if exist "Build\%CONFIGURATION%-windows-x86_64%\Test\Test.exe" (
    echo Running tests...
    Build\%CONFIGURATION%-windows-x86_64%\Test\Test.exe --success
    if errorlevel 1 (
        echo Warning: Some tests failed
    ) else (
        echo All tests passed!
    )
)

endlocal 