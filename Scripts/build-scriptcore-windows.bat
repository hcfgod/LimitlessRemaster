@echo off
setlocal enabledelayedexpansion

set CONFIGURATION=Debug
set PLATFORM=x64
set "PROJECT_ROOT=%~3"

if "%1"=="Release" set CONFIGURATION=Release
if "%1"=="Dist" set CONFIGURATION=Dist
if "%2"=="ARM64" set PLATFORM=ARM64

if not "%PROJECT_ROOT%"=="" for %%I in ("%PROJECT_ROOT%") do set "PROJECT_ROOT=%%~fI"

echo Building ScriptCore in %CONFIGURATION% configuration for %PLATFORM%...

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

set "CFG_LOWER=%CONFIGURATION%"
if /I "%CONFIGURATION%"=="Debug" set "CFG_LOWER=debug"
if /I "%CONFIGURATION%"=="Release" set "CFG_LOWER=release"
if /I "%CONFIGURATION%"=="Dist" set "CFG_LOWER=dist"

set "PLATFORM_LOWER=%PLATFORM%"
if /I "%PLATFORM%"=="ARM64" set "PLATFORM_LOWER=arm64"
if /I "%PLATFORM%"=="x64" set "PLATFORM_LOWER=x64"

set "BUILD_FOLDER=%CFG_LOWER%_%PLATFORM_LOWER%-windows-%PLATFORM%"
set "OUTPUT_DIR=%CD%\Build\%BUILD_FOLDER%\Editor"
set "RUNTIME_TEMPLATE_DIR=%CD%\RuntimeTemplates\%BUILD_FOLDER%"
set "MANAGED_BUILD_SCRIPT=%CD%\Scripts\build-managed-runtime-windows.bat"
set "PROJECT_LOCAL_OUTPUT_DIR="
if not "%PROJECT_ROOT%"=="" set "PROJECT_LOCAL_OUTPUT_DIR=%PROJECT_ROOT%\Build\ScriptCore\%BUILD_FOLDER%"

echo Building ScriptCore project only...
set "SCRIPTCORE_PDB_FILE=%TEMP%\ScriptCore-%RANDOM%-%RANDOM%.pdb"
"%MSBUILD_EXE%" ScriptCore\ScriptCore.vcxproj /p:Configuration=%CONFIGURATION% /p:Platform=%PLATFORM% /p:GenerateDebugInformation=false /p:LinkIncremental=false /p:LinkProgramDatabaseFile="%SCRIPTCORE_PDB_FILE%" /m
if errorlevel 1 (
    echo Error: ScriptCore build failed
    exit /b 1
)

if not exist "%MANAGED_BUILD_SCRIPT%" (
    echo Error: Managed runtime build script not found: "%MANAGED_BUILD_SCRIPT%"
    exit /b 1
)

call "%MANAGED_BUILD_SCRIPT%" %CONFIGURATION% %PLATFORM% "%OUTPUT_DIR%" "%PROJECT_ROOT%"
if errorlevel 1 (
    echo Error: Failed to refresh managed runtime payload in "%OUTPUT_DIR%".
    exit /b 1
)

if not "%PROJECT_LOCAL_OUTPUT_DIR%"=="" (
    if not exist "%PROJECT_LOCAL_OUTPUT_DIR%\Managed" mkdir "%PROJECT_LOCAL_OUTPUT_DIR%\Managed"
    robocopy "%OUTPUT_DIR%\Managed" "%PROJECT_LOCAL_OUTPUT_DIR%\Managed" /MIR /NJH /NJS /NFL /NDL /NC /NS /NP >nul
    if !ERRORLEVEL! GEQ 8 (
        echo Error: Failed to refresh managed runtime payload in "%PROJECT_LOCAL_OUTPUT_DIR%".
        exit /b 1
    )
)

if not exist "%RUNTIME_TEMPLATE_DIR%\Managed" mkdir "%RUNTIME_TEMPLATE_DIR%\Managed"
robocopy "%OUTPUT_DIR%\Managed" "%RUNTIME_TEMPLATE_DIR%\Managed" /MIR /NJH /NJS /NFL /NDL /NC /NS /NP >nul
if %ERRORLEVEL% GEQ 8 (
    echo Error: Failed to refresh managed runtime payload in "%RUNTIME_TEMPLATE_DIR%".
    exit /b 1
)

echo ScriptCore build completed successfully!
endlocal
exit /b 0
