@echo off
setlocal enabledelayedexpansion

REM --------------------------------------------------------------------------------------
REM BootstrapPremake.bat
REM Ensures Vendor\Premake\premake5.exe exists for local Windows workflows.
REM Matches the GitHub Actions approach (download + extract) used in CI.
REM --------------------------------------------------------------------------------------

set "PREMAKE_DIR=%~dp0..\Vendor\Premake"
set "PREMAKE_EXE=%PREMAKE_DIR%\premake5.exe"
set "PREMAKE_URL=https://github.com/premake/premake-core/releases/download/v5.0.0-beta2/premake-5.0.0-beta2-windows.zip"
set "PREMAKE_ZIP=%PREMAKE_DIR%\premake5.zip"

if exist "%PREMAKE_EXE%" (
    exit /b 0
)

echo Premake5 not found at "%PREMAKE_EXE%".
echo Downloading Premake5 (v5.0.0-beta2)...

if not exist "%PREMAKE_DIR%" (
    mkdir "%PREMAKE_DIR%"
    if errorlevel 1 (
        echo Error: Failed to create directory "%PREMAKE_DIR%".
        exit /b 1
    )
)

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$ErrorActionPreference = 'Stop';" ^
    "Invoke-WebRequest -Uri '%PREMAKE_URL%' -OutFile '%PREMAKE_ZIP%';" ^
    "Expand-Archive -Path '%PREMAKE_ZIP%' -DestinationPath '%PREMAKE_DIR%' -Force;" ^
    "Remove-Item -Force '%PREMAKE_ZIP%';"

if errorlevel 1 (
    echo Error: Premake5 download or extraction failed.
    exit /b 1
)

if not exist "%PREMAKE_EXE%" (
    echo Error: Premake5 executable was not found after extraction.
    exit /b 1
)

echo Premake5 installed successfully.
"%PREMAKE_EXE%" --version

endlocal
exit /b 0

