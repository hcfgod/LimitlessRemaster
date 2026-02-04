@echo off
setlocal enabledelayedexpansion

REM Parse command line arguments
set CONFIGURATION=Debug
set PLATFORM=x64

if "%1"=="Release" set CONFIGURATION=Release
if "%1"=="Dist" set CONFIGURATION=Dist
if "%2"=="ARM64" set PLATFORM=ARM64

echo Building LimitlessRemaster in %CONFIGURATION% configuration for %PLATFORM%...

REM Ensure Premake5 exists (download if missing)
call "%~dp0BootstrapPremake.bat"
if errorlevel 1 (
    echo Error: Premake bootstrap failed.
    exit /b 1
)

REM Change to the project root directory
cd /d "%~dp0.."

REM --------------------------------------------------------------------------------------
REM Locate MSBuild (works without a Developer Command Prompt)
REM --------------------------------------------------------------------------------------
set "MSBUILD_EXE="

REM 1) Prefer msbuild already on PATH
for /f "delims=" %%i in ('where msbuild 2^>nul') do (
    REM `where` prints an INFO line on failure; only accept real file paths.
    if exist "%%i" (
        if not defined MSBUILD_EXE set "MSBUILD_EXE=%%i"
    )
)

REM 2) Use vswhere (installed with Visual Studio / Build Tools)
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\Current\Bin\MSBuild.exe`) do (
        if exist "%%i" (
            if not defined MSBUILD_EXE set "MSBUILD_EXE=%%i"
        )
    )
    for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do (
        if exist "%%i" (
            if not defined MSBUILD_EXE set "MSBUILD_EXE=%%i"
        )
    )
)

REM 3) Fallback to common install locations
if not defined MSBUILD_EXE if exist "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" set "MSBUILD_EXE=%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
if not defined MSBUILD_EXE if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" set "MSBUILD_EXE=%ProgramFiles%\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
if not defined MSBUILD_EXE if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe" set "MSBUILD_EXE=%ProgramFiles%\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"
if not defined MSBUILD_EXE if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe" set "MSBUILD_EXE=%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe"

if "%MSBUILD_EXE%"=="" (
    echo Error: MSBuild was not found.
    echo.
    echo Fix:
    echo - Install Visual Studio 2022 or "Build Tools for Visual Studio 2022"
    echo - Include the "MSBuild" and "Desktop development with C++" workloads
    echo.
    echo After installing, re-run this script. It will auto-detect MSBuild.
    exit /b 1
)

echo Using MSBuild: "%MSBUILD_EXE%"

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
"%MSBUILD_EXE%" LimitlessRemaster.sln /p:Configuration=%CONFIGURATION% /p:Platform=%PLATFORM% /m
if errorlevel 1 (
    echo Error: Build failed
    exit /b 1
)

echo Build completed successfully!

REM Premake uses cfg.shortname for output folders (e.g. debug_x64-windows-x64)
set "CONFIG_LOWER=%CONFIGURATION%"
if /i "%CONFIGURATION%"=="Debug" set "CONFIG_LOWER=debug"
if /i "%CONFIGURATION%"=="Release" set "CONFIG_LOWER=release"
if /i "%CONFIGURATION%"=="Dist" set "CONFIG_LOWER=dist"

set "PLATFORM_LOWER=%PLATFORM%"
if /i "%PLATFORM%"=="ARM64" set "PLATFORM_LOWER=arm64"

set "OUTPUT_DIR="
set "CANDIDATE_DIR=Build\%CONFIG_LOWER%_%PLATFORM_LOWER%-windows-%PLATFORM%"
if exist "%CANDIDATE_DIR%\" set "OUTPUT_DIR=%CANDIDATE_DIR%"

if "%OUTPUT_DIR%"=="" (
    set "CANDIDATE_DIR=Build\%CONFIG_LOWER%_%PLATFORM%-windows-%PLATFORM%"
    if exist "%CANDIDATE_DIR%\" set "OUTPUT_DIR=%CANDIDATE_DIR%"
)

if "%OUTPUT_DIR%"=="" (
    set "CANDIDATE_DIR=Build\%CONFIGURATION%-windows-%PLATFORM%"
    if exist "%CANDIDATE_DIR%\" set "OUTPUT_DIR=%CANDIDATE_DIR%"
)

if "%OUTPUT_DIR%"=="" (
    echo Warning: Could not locate output directory automatically.
    echo Expected something like: Build\%CONFIG_LOWER%_%PLATFORM_LOWER%-windows-%PLATFORM%\
    set "OUTPUT_DIR=Build"
)

echo Output directory: %OUTPUT_DIR%\

REM Run tests if they exist
if exist "%OUTPUT_DIR%\Test\Test.exe" (
    echo Running tests...
    "%OUTPUT_DIR%\Test\Test.exe" --success
    if errorlevel 1 (
        echo Warning: Some tests failed
    ) else (
        echo All tests passed!
    )
)

endlocal 