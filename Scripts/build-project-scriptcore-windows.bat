@echo off
setlocal enabledelayedexpansion

set "CONFIGURATION=%~1"
set "PLATFORM=%~2"
set "PROJECT_ROOT=%~3"

if "%CONFIGURATION%"=="" set "CONFIGURATION=Debug"
if "%PLATFORM%"=="" set "PLATFORM=x64"

if "%PROJECT_ROOT%"=="" (
    echo Error: Missing project root argument.
    echo Usage: build-project-scriptcore-windows.bat [Debug^|Release^|Dist] [x64^|ARM64] "C:\Path\To\Project"
    exit /b 1
)

if /I "%CONFIGURATION%"=="Debug" (
    set "CONFIG_DEFINE=LT_CONFIG_DEBUG"
) else if /I "%CONFIGURATION%"=="Release" (
    set "CONFIG_DEFINE=LT_CONFIG_RELEASE"
) else if /I "%CONFIGURATION%"=="Dist" (
    set "CONFIG_DEFINE=LT_CONFIG_DIST"
) else (
    echo Error: Invalid configuration "%CONFIGURATION%".
    exit /b 1
)

set "PLATFORM_LOWER=%PLATFORM%"
if /I "%PLATFORM%"=="ARM64" (
    set "PLATFORM_LOWER=arm64"
) else if /I "%PLATFORM%"=="x64" (
    set "PLATFORM_LOWER=x64"
) else (
    echo Error: Invalid platform "%PLATFORM%". Expected x64 or ARM64.
    exit /b 1
)

set "SCRIPT_DIR=%~dp0"
set "TOOLCHAIN_ROOT=%SCRIPT_DIR%.."
for %%I in ("%TOOLCHAIN_ROOT%") do set "TOOLCHAIN_ROOT=%%~fI"

set "GENERATED_DIR=%PROJECT_ROOT%\Build\Generated\ScriptCore"
if not exist "%GENERATED_DIR%" (
    mkdir "%GENERATED_DIR%"
    if errorlevel 1 (
        echo Error: Failed to create generated script directory "%GENERATED_DIR%".
        exit /b 1
    )
)

set "CFG_LOWER=%CONFIGURATION%"
if /I "%CONFIGURATION%"=="Debug" set "CFG_LOWER=debug"
if /I "%CONFIGURATION%"=="Release" set "CFG_LOWER=release"
if /I "%CONFIGURATION%"=="Dist" set "CFG_LOWER=dist"

set "BUILD_FOLDER=%CFG_LOWER%_%PLATFORM_LOWER%-windows-%PLATFORM%"
set "SDK_INCLUDE_DIR=%TOOLCHAIN_ROOT%\SDK\include"
set "SDK_VENDOR_DIR=%TOOLCHAIN_ROOT%\SDK\vendor"
set "SDK_LIB_DIR=%TOOLCHAIN_ROOT%\SDK\lib\%BUILD_FOLDER%"
set "OUTPUT_DIR=%TOOLCHAIN_ROOT%\Build\%BUILD_FOLDER%\Editor"
set "INTERMEDIATE_DIR=%TOOLCHAIN_ROOT%\Build\Intermediates\%BUILD_FOLDER%\ProjectScriptCore"
set "SOURCES_RSP=%INTERMEDIATE_DIR%\ScriptSources.rsp"

if not exist "%SDK_INCLUDE_DIR%\Limitless.h" (
    echo Error: SDK include root is missing Limitless headers: "%SDK_INCLUDE_DIR%\Limitless.h"
    exit /b 1
)

if not exist "%SDK_INCLUDE_DIR%\ScriptCoreRegistration.h" (
    echo Error: SDK include root is missing ScriptCoreRegistration.h: "%SDK_INCLUDE_DIR%\ScriptCoreRegistration.h"
    exit /b 1
)

if not exist "%SDK_LIB_DIR%\Limitless.lib" (
    echo Error: SDK library not found: "%SDK_LIB_DIR%\Limitless.lib"
    exit /b 1
)

if not exist "%SDK_LIB_DIR%\ScriptCoreHostGlue.lib" (
    echo Error: ScriptCore host glue library not found: "%SDK_LIB_DIR%\ScriptCoreHostGlue.lib"
    exit /b 1
)

if not exist "%INTERMEDIATE_DIR%" mkdir "%INTERMEDIATE_DIR%"
if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$sourceDir = [IO.Path]::GetFullPath('%GENERATED_DIR%');" ^
    "$rspPath = [IO.Path]::GetFullPath('%SOURCES_RSP%');" ^
    "$files = Get-ChildItem -Path $sourceDir -Recurse -Filter '*.cpp' -ErrorAction SilentlyContinue |" ^
    "    Where-Object { $_.Name -ne 'ScriptCoreHostGlue.cpp' } |" ^
    "    ForEach-Object { '\"' + $_.FullName + '\"' };" ^
    "Set-Content -Path $rspPath -Value $files -Encoding Ascii"
if errorlevel 1 (
    echo Error: Failed to enumerate generated script sources.
    exit /b 1
)

for %%A in ("%SOURCES_RSP%") do if %%~zA EQU 0 (
    > "%INTERMEDIATE_DIR%\DummyScriptCoreTranslationUnit.cpp" echo // Auto-generated fallback translation unit.
    > "%SOURCES_RSP%" echo "%INTERMEDIATE_DIR%\DummyScriptCoreTranslationUnit.cpp"
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSINSTALL="
if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        if not defined VSINSTALL set "VSINSTALL=%%i"
    )
)
if not defined VSINSTALL (
    echo Error: Could not locate Visual Studio installation with C++ tools.
    exit /b 1
)

call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 (
    echo Error: Failed to initialize MSVC build environment.
    exit /b 1
)

echo Building project ScriptCore module from generated scripts...
cl /nologo /std:c++20 /EHsc /MD /LD /bigobj /utf-8 /FS ^
   /D %CONFIG_DEFINE% /D LT_PLATFORM_WINDOWS /D SCRIPTCORE_EXPORTS /D _UNICODE /D UNICODE ^
   /I "%SDK_INCLUDE_DIR%" ^
   /I "%SDK_VENDOR_DIR%" ^
   /I "%SDK_VENDOR_DIR%\box2d\include" ^
   /I "%SDK_VENDOR_DIR%\glad" ^
   /I "%SDK_VENDOR_DIR%\spdlog" ^
   /I "%SDK_VENDOR_DIR%\doctest" ^
   /I "%SDK_VENDOR_DIR%\SDL3" ^
   /I "%SDK_VENDOR_DIR%\ffmpeg\include" ^
   /I "%SDK_VENDOR_DIR%\imgui" ^
   /I "%SDK_VENDOR_DIR%\glm" ^
   /I "%GENERATED_DIR%" ^
   @"%SOURCES_RSP%" ^
   /link /NOLOGO ^
   /OPT:NOREF /OPT:NOICF ^
   /OUT:"%OUTPUT_DIR%\ScriptCore.dll" ^
   /IMPLIB:"%OUTPUT_DIR%\ScriptCore.lib" ^
   /PDB:"%OUTPUT_DIR%\ScriptCore.pdb" ^
   /LIBPATH:"%SDK_LIB_DIR%" ^
   /WHOLEARCHIVE:ScriptCoreHostGlue.lib ^
   Limitless.lib VendorSpirvCross.lib VendorZstd.lib freetype.lib msdfgen.lib msdf-atlas-gen.lib ^
   ScriptCoreHostGlue.lib ^
   SDL3-static.lib vulkan-1.lib shaderc_shared.lib box2D.lib ^
   avcodec.lib avformat.lib avutil.lib swresample.lib ^
   user32.lib gdi32.lib winmm.lib imm32.lib ole32.lib oleaut32.lib uuid.lib version.lib advapi32.lib setupapi.lib shell32.lib psapi.lib

if errorlevel 1 (
    echo Error: Project ScriptCore build failed.
    exit /b 1
)

echo ScriptCore build completed successfully.
echo Output directory: %OUTPUT_DIR%
exit /b 0
