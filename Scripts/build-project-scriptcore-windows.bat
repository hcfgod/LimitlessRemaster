@echo off
setlocal enabledelayedexpansion

rem -------------------------------------------------------------------------
rem  build-project-scriptcore-windows.bat
rem
rem  Incremental ScriptCore build.  Each .cpp is compiled to a separate .obj
rem  and only recompiled when the source or one of its header dependencies has
rem  changed.  All .obj files are then linked into ScriptCore.dll.
rem
rem  Pass --clean as the FIRST argument to force a full rebuild.
rem -------------------------------------------------------------------------

set "FORCE_CLEAN=0"
if /I "%~1"=="--clean" (
    set "FORCE_CLEAN=1"
    shift
)

set "CONFIGURATION=%~1"
set "PLATFORM=%~2"
set "PROJECT_ROOT=%~3"

if "%CONFIGURATION%"=="" set "CONFIGURATION=Debug"
if "%PLATFORM%"=="" set "PLATFORM=x64"

if "%PROJECT_ROOT%"=="" (
    echo Error: Missing project root argument.
    echo Usage: build-project-scriptcore-windows.bat [--clean] [Debug^|Release^|Dist] [x64^|ARM64] "C:\Path\To\Project"
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
    set "ARCH_DEFINE=LT_ARCHITECTURE_ARM64"
) else if /I "%PLATFORM%"=="x64" (
    set "PLATFORM_LOWER=x64"
    set "ARCH_DEFINE=LT_ARCHITECTURE_X64"
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
set "OBJ_DIR=%INTERMEDIATE_DIR%\obj"
set "DEP_DIR=%INTERMEDIATE_DIR%\dep"
set "SNAPSHOT_DIR=%INTERMEDIATE_DIR%\ScriptSourcesSnapshot"

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
if not exist "%OBJ_DIR%" mkdir "%OBJ_DIR%"
if not exist "%DEP_DIR%" mkdir "%DEP_DIR%"
if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"

rem --- Clean mode: wipe object and dep caches ---
if "%FORCE_CLEAN%"=="1" (
    echo Incremental build: clean requested, wiping object cache...
    if exist "%OBJ_DIR%" rd /s /q "%OBJ_DIR%" 2>nul
    if exist "%DEP_DIR%" rd /s /q "%DEP_DIR%" 2>nul
    mkdir "%OBJ_DIR%"
    mkdir "%DEP_DIR%"
)

rem --- Snapshot generated sources ---
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$sourceDir = [IO.Path]::GetFullPath('%GENERATED_DIR%');" ^
    "$snapshotDir = [IO.Path]::GetFullPath('%SNAPSHOT_DIR%');" ^
    "if (Test-Path -LiteralPath $snapshotDir) { Remove-Item -LiteralPath $snapshotDir -Recurse -Force -ErrorAction Stop };" ^
    "New-Item -ItemType Directory -Path $snapshotDir -Force | Out-Null;" ^
    "if (Test-Path -LiteralPath $sourceDir) {" ^
    "    Get-ChildItem -Path $sourceDir -Force -ErrorAction SilentlyContinue | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $snapshotDir -Recurse -Force -ErrorAction Stop }" ^
    "}"
if errorlevel 1 (
    echo Error: Failed to create script source snapshot.
    exit /b 1
)

rem --- Locate Visual Studio ---
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSINSTALL="
if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requiresAny -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -requires Microsoft.VisualStudio.Component.VC.Tools.ARM64 -property installationPath`) do (
        if not defined VSINSTALL set "VSINSTALL=%%i"
    )
)
if not defined VSINSTALL (
    echo Error: Could not locate Visual Studio installation with C++ tools.
    exit /b 1
)

if /I "%PLATFORM%"=="ARM64" (
    if exist "%VSINSTALL%\VC\Auxiliary\Build\vcvarsamd64_arm64.bat" (
        call "%VSINSTALL%\VC\Auxiliary\Build\vcvarsamd64_arm64.bat" >nul 2>&1
    ) else (
        call "%VSINSTALL%\VC\Auxiliary\Build\vcvarsarm64.bat" >nul 2>&1
    )
) else (
    call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
)
if errorlevel 1 (
    echo Error: Failed to initialize MSVC build environment.
    exit /b 1
)

echo Building project ScriptCore module (incremental)...

rem -------------------------------------------------------------------------
rem  INCREMENTAL COMPILE — PowerShell orchestrator
rem
rem  For each .cpp in the snapshot directory, compute a stable object name,
rem  check whether recompilation is needed (source newer than .obj, or any
rem  header dep newer than .obj), compile only if needed, and capture
rem  /showIncludes output to update the dep file.  At the end, write an
rem  object-file response file for the linker and report how many files were
rem  compiled vs skipped.
rem -------------------------------------------------------------------------
set "OBJ_RSP=%INTERMEDIATE_DIR%\ObjectFiles.rsp"

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$ErrorActionPreference = 'Stop';" ^
    "$snapshotDir = [IO.Path]::GetFullPath('%SNAPSHOT_DIR%');" ^
    "$objDir      = [IO.Path]::GetFullPath('%OBJ_DIR%');" ^
    "$depDir      = [IO.Path]::GetFullPath('%DEP_DIR%');" ^
    "$objRspPath  = [IO.Path]::GetFullPath('%OBJ_RSP%');" ^
    "" ^
    "$cppFiles = @(Get-ChildItem -Path $snapshotDir -Recurse -Filter '*.cpp' -ErrorAction SilentlyContinue | Where-Object { $_.Name -ne 'ScriptCoreHostGlue.cpp' });" ^
    "if ($cppFiles.Count -eq 0) {" ^
    "    $dummyCpp = Join-Path $snapshotDir 'DummyScriptCoreTranslationUnit.cpp';" ^
    "    Set-Content -Path $dummyCpp -Value '// Auto-generated fallback translation unit.' -Encoding Ascii;" ^
    "    $cppFiles = @(Get-Item $dummyCpp);" ^
    "}" ^
    "" ^
    "$compiledCount = 0;" ^
    "$skippedCount  = 0;" ^
    "$failedCount   = 0;" ^
    "$objFiles      = @();" ^
    "" ^
    "foreach ($cpp in $cppFiles) {" ^
    "    $relPath = $cpp.FullName.Substring($snapshotDir.Length).TrimStart('\','/');" ^
    "    $safeName = $relPath -replace '[\\/ ]','_';" ^
    "    $safeName = [IO.Path]::ChangeExtension($safeName, '.obj');" ^
    "    $objPath = Join-Path $objDir $safeName;" ^
    "    $depPath = Join-Path $depDir ([IO.Path]::ChangeExtension($safeName, '.dep'));" ^
    "    $objFiles += $objPath;" ^
    "" ^
    "    $needsCompile = $true;" ^
    "    if (Test-Path -LiteralPath $objPath) {" ^
    "        $objTime = (Get-Item $objPath).LastWriteTimeUtc;" ^
    "        if ($cpp.LastWriteTimeUtc -le $objTime) {" ^
    "            $needsCompile = $false;" ^
    "            if (Test-Path -LiteralPath $depPath) {" ^
    "                $deps = Get-Content -LiteralPath $depPath -ErrorAction SilentlyContinue;" ^
    "                foreach ($d in $deps) {" ^
    "                    $d = $d.Trim();" ^
    "                    if ($d -ne '' -and (Test-Path -LiteralPath $d)) {" ^
    "                        if ((Get-Item $d).LastWriteTimeUtc -gt $objTime) {" ^
    "                            $needsCompile = $true;" ^
    "                            break;" ^
    "                        }" ^
    "                    }" ^
    "                }" ^
    "            } else {" ^
    "                $needsCompile = $true;" ^
    "            }" ^
    "        }" ^
    "    }" ^
    "" ^
    "    if (-not $needsCompile) {" ^
    "        $skippedCount++;" ^
    "        continue;" ^
    "    }" ^
    "" ^
    "    $compileArgs = @(" ^
    "        '/nologo','/std:c++20','/EHsc','/MD','/c','/bigobj','/utf-8','/FS'," ^
    "        '/showIncludes'," ^
    "        '/D%CONFIG_DEFINE%','/D%ARCH_DEFINE%','/DLT_PLATFORM_WINDOWS','/DSCRIPTCORE_EXPORTS','/D_UNICODE','/DUNICODE'," ^
    "        '/I%SDK_INCLUDE_DIR%'," ^
    "        '/I%SDK_VENDOR_DIR%'," ^
    "        '/I%SDK_VENDOR_DIR%\box2d\include'," ^
    "        '/I%SDK_VENDOR_DIR%\glad'," ^
    "        '/I%SDK_VENDOR_DIR%\spdlog'," ^
    "        '/I%SDK_VENDOR_DIR%\doctest'," ^
    "        '/I%SDK_VENDOR_DIR%\SDL3'," ^
    "        '/I%SDK_VENDOR_DIR%\ffmpeg\include'," ^
    "        '/I%SDK_VENDOR_DIR%\imgui'," ^
    "        '/I%SDK_VENDOR_DIR%\glm'," ^
    "        \"/I$snapshotDir\"," ^
    "        \"/I%GENERATED_DIR%\"," ^
    "        \"/Fo$objPath\"," ^
    "        $cpp.FullName" ^
    "    );" ^
    "" ^
    "    $rawOutput = & cl @compileArgs 2>&1;" ^
    "    $exitCode = $LASTEXITCODE;" ^
    "" ^
    "    $headerDeps = @();" ^
    "    foreach ($line in $rawOutput) {" ^
    "        $s = \"$line\";" ^
    "        if ($s -match '^Note: including file:\\s*(.+)$') {" ^
    "            $headerDeps += $Matches[1].Trim();" ^
    "        } else {" ^
    "            if ($s.Trim() -ne '') { Write-Host $s }" ^
    "        }" ^
    "    }" ^
    "" ^
    "    if ($exitCode -ne 0) {" ^
    "        Write-Host \"Error: compilation failed for $($cpp.Name) (exit code $exitCode)\";" ^
    "        $failedCount++;" ^
    "        if (Test-Path -LiteralPath $objPath) { Remove-Item -LiteralPath $objPath -Force -ErrorAction SilentlyContinue }" ^
    "        if (Test-Path -LiteralPath $depPath) { Remove-Item -LiteralPath $depPath -Force -ErrorAction SilentlyContinue }" ^
    "    } else {" ^
    "        $compiledCount++;" ^
    "        Set-Content -Path $depPath -Value ($headerDeps -join \"`n\") -Encoding Ascii;" ^
    "    }" ^
    "}" ^
    "" ^
    "Write-Host \"Incremental compile: $compiledCount compiled, $skippedCount up-to-date, $failedCount failed.\";" ^
    "if ($failedCount -gt 0) { exit 1 }" ^
    "" ^
    "$objRspLines = $objFiles | ForEach-Object { '\"' + $_ + '\"' };" ^
    "Set-Content -Path $objRspPath -Value ($objRspLines -join \"`n\") -Encoding Ascii;"
if errorlevel 1 (
    echo Error: Incremental compilation failed.
    exit /b 1
)

rem --- Prune stale .obj files whose source no longer exists ---
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$objDir = [IO.Path]::GetFullPath('%OBJ_DIR%');" ^
    "$objRspPath = [IO.Path]::GetFullPath('%OBJ_RSP%');" ^
    "$depDir = [IO.Path]::GetFullPath('%DEP_DIR%');" ^
    "$expectedObjs = @{};" ^
    "if (Test-Path -LiteralPath $objRspPath) {" ^
    "    Get-Content -LiteralPath $objRspPath | ForEach-Object { $expectedObjs[$_.Trim('\"').Trim()] = $true }" ^
    "}" ^
    "$pruned = 0;" ^
    "Get-ChildItem -Path $objDir -Filter '*.obj' -ErrorAction SilentlyContinue | ForEach-Object {" ^
    "    if (-not $expectedObjs.ContainsKey($_.FullName)) {" ^
    "        Remove-Item -LiteralPath $_.FullName -Force -ErrorAction SilentlyContinue;" ^
    "        $depFile = Join-Path $depDir ([IO.Path]::ChangeExtension($_.Name, '.dep'));" ^
    "        if (Test-Path -LiteralPath $depFile) { Remove-Item -LiteralPath $depFile -Force -ErrorAction SilentlyContinue }" ^
    "        $pruned++;" ^
    "    }" ^
    "};" ^
    "if ($pruned -gt 0) { Write-Host \"Pruned $pruned stale object file(s).\" }"

rem --- Link all object files into ScriptCore.dll ---
echo Linking ScriptCore.dll...
link /NOLOGO /DLL ^
   /MACHINE:%PLATFORM% ^
   /OPT:NOREF /OPT:NOICF ^
   /OUT:"%OUTPUT_DIR%\ScriptCore.dll" ^
   /IMPLIB:"%OUTPUT_DIR%\ScriptCore.lib" ^
   /PDB:"%OUTPUT_DIR%\ScriptCore.pdb" ^
   @"%OBJ_RSP%" ^
   /LIBPATH:"%SDK_LIB_DIR%" ^
   /WHOLEARCHIVE:ScriptCoreHostGlue.lib ^
   Limitless.lib VendorSpirvCross.lib VendorZstd.lib freetype.lib msdfgen.lib msdf-atlas-gen.lib ^
   ScriptCoreHostGlue.lib ^
   SDL3-static.lib vulkan-1.lib shaderc_shared.lib box2D.lib ^
   avcodec.lib avformat.lib avutil.lib swresample.lib ^
   user32.lib gdi32.lib winmm.lib imm32.lib ole32.lib oleaut32.lib uuid.lib version.lib advapi32.lib setupapi.lib shell32.lib psapi.lib

if errorlevel 1 (
    echo Error: Project ScriptCore link failed.
    exit /b 1
)

echo ScriptCore build completed successfully.
echo Output directory: %OUTPUT_DIR%
exit /b 0
