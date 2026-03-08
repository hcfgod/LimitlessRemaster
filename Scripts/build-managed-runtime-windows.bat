@echo off
setlocal

set "CONFIGURATION=%~1"
set "PLATFORM=%~2"
set "OUTPUT_DIR=%~3"

if "%CONFIGURATION%"=="" set "CONFIGURATION=Debug"
if "%PLATFORM%"=="" set "PLATFORM=x64"

if "%OUTPUT_DIR%"=="" (
    echo Error: Missing output directory argument.
    echo Usage: build-managed-runtime-windows.bat [Debug^|Release^|Dist] [x64^|ARM64] "C:\Path\To\Output"
    exit /b 1
)

for %%I in ("%OUTPUT_DIR%") do set "OUTPUT_DIR=%%~fI"

set "DOTNET_CONFIGURATION=%CONFIGURATION%"
if /I "%DOTNET_CONFIGURATION%"=="Dist" set "DOTNET_CONFIGURATION=Release"

where dotnet >nul 2>nul
if errorlevel 1 (
    echo Error: dotnet SDK was not found on PATH.
    exit /b 1
)

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "REPO_ROOT=%%~fI"
set "MANAGED_OUTPUT_DIR=%OUTPUT_DIR%\Managed"
set "MANAGED_MANIFEST_PATH=%MANAGED_OUTPUT_DIR%\Limitless.Managed.payload.json"
set "MANAGED_LOCK_ROOT=%REPO_ROOT%\Build\ManagedRuntimeLocks"
set "MANAGED_LOCK_DIR=%MANAGED_LOCK_ROOT%\%DOTNET_CONFIGURATION%-%PLATFORM%"

if not exist "%MANAGED_OUTPUT_DIR%" mkdir "%MANAGED_OUTPUT_DIR%"
if errorlevel 1 (
    echo Error: Failed to create managed output directory "%MANAGED_OUTPUT_DIR%".
    exit /b 1
)

if not exist "%MANAGED_LOCK_ROOT%" mkdir "%MANAGED_LOCK_ROOT%"
if errorlevel 1 (
    echo Error: Failed to create managed lock directory root "%MANAGED_LOCK_ROOT%".
    exit /b 1
)

:wait_for_managed_lock
mkdir "%MANAGED_LOCK_DIR%" 2>nul
if errorlevel 1 (
    >nul ping 127.0.0.1 -n 2
    goto wait_for_managed_lock
)

echo Building managed scripting artifacts for %CONFIGURATION% %PLATFORM%...
dotnet publish "%REPO_ROOT%\Limitless\Vendor\Coral\Coral.Managed\Coral.Managed-Static.csproj" -c %DOTNET_CONFIGURATION% -o "%MANAGED_OUTPUT_DIR%" /nologo /verbosity:minimal
if errorlevel 1 goto managed_build_failed

dotnet build "%REPO_ROOT%\Managed\Limitless.Managed\Limitless.Managed.csproj" -c %DOTNET_CONFIGURATION% -o "%MANAGED_OUTPUT_DIR%" /nologo /verbosity:minimal
if errorlevel 1 goto managed_build_failed

dotnet build "%REPO_ROOT%\Managed\Limitless.Managed.TestScripts\Limitless.Managed.TestScripts.csproj" -c %DOTNET_CONFIGURATION% -o "%MANAGED_OUTPUT_DIR%" /nologo /verbosity:minimal
if errorlevel 1 goto managed_build_failed

>"%MANAGED_MANIFEST_PATH%" echo {
>>"%MANAGED_MANIFEST_PATH%" echo   "formatVersion": 1,
>>"%MANAGED_MANIFEST_PATH%" echo   "apiVersion": 1,
>>"%MANAGED_MANIFEST_PATH%" echo   "coralManagedAssembly": "Coral.Managed.dll",
>>"%MANAGED_MANIFEST_PATH%" echo   "coralManagedRuntimeConfig": "Coral.Managed.runtimeconfig.json",
>>"%MANAGED_MANIFEST_PATH%" echo   "contractAssembly": "Limitless.Managed.dll",
>>"%MANAGED_MANIFEST_PATH%" echo   "contractRuntimeConfig": "Limitless.Managed.runtimeconfig.json",
>>"%MANAGED_MANIFEST_PATH%" echo   "scriptAssemblies": ["Limitless.Managed.TestScripts.dll"],
>>"%MANAGED_MANIFEST_PATH%" echo   "buildConfiguration": "%DOTNET_CONFIGURATION%",
>>"%MANAGED_MANIFEST_PATH%" echo   "targetOS": "Windows",
>>"%MANAGED_MANIFEST_PATH%" echo   "targetArchitecture": "%PLATFORM%"
>>"%MANAGED_MANIFEST_PATH%" echo }

echo Managed scripting artifacts staged to "%MANAGED_OUTPUT_DIR%".
set "BUILD_EXIT_CODE=0"
goto managed_build_cleanup

:managed_build_failed
set "BUILD_EXIT_CODE=1"

:managed_build_cleanup
if exist "%MANAGED_LOCK_DIR%" rmdir "%MANAGED_LOCK_DIR%" >nul 2>nul
exit /b %BUILD_EXIT_CODE%
