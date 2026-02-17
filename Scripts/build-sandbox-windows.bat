@echo off
setlocal enabledelayedexpansion

set CONFIGURATION=Debug
set PLATFORM=x64

if "%1"=="Release" set CONFIGURATION=Release
if "%1"=="Dist" set CONFIGURATION=Dist
if "%2"=="ARM64" set PLATFORM=ARM64

echo Building Sandbox runtime in %CONFIGURATION% configuration for %PLATFORM%...

call "%~dp0BootstrapPremake.bat"
if errorlevel 1 (
    echo Error: Premake bootstrap failed.
    exit /b 1
)

cd /d "%~dp0.."

set "MSBUILD_EXE="
for /f "delims=" %%i in ('where msbuild 2^>nul') do (
    if exist "%%i" (
        if not defined MSBUILD_EXE set "MSBUILD_EXE=%%i"
    )
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\Current\Bin\MSBuild.exe`) do (
        if exist "%%i" (
            if not defined MSBUILD_EXE set "MSBUILD_EXE=%%i"
        )
    )
)

if not defined MSBUILD_EXE if exist "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" set "MSBUILD_EXE=%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
if not defined MSBUILD_EXE if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" set "MSBUILD_EXE=%ProgramFiles%\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"

if not defined MSBUILD_EXE (
    echo Error: MSBuild was not found.
    exit /b 1
)

echo Using MSBuild: "%MSBUILD_EXE%"
echo Generating Visual Studio solution...
Vendor\Premake\premake5.exe vs2022
if errorlevel 1 (
    echo Error: Failed to generate Visual Studio solution.
    exit /b 1
)

echo Building Sandbox project only...
"%MSBUILD_EXE%" Sandbox\Sandbox.vcxproj /p:Configuration=%CONFIGURATION% /p:Platform=%PLATFORM% /m
if errorlevel 1 (
    echo Error: Sandbox build failed
    exit /b 1
)

echo Sandbox build completed successfully!
endlocal
