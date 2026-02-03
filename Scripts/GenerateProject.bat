@echo off
echo Generating Visual Studio solution...

REM Ensure Premake5 exists (download if missing)
call "%~dp0BootstrapPremake.bat"
if errorlevel 1 (
    echo Error: Premake bootstrap failed.
    exit /b 1
)

REM Change to the project root directory
cd /d "%~dp0.."

REM Run Premake to generate Visual Studio solution
Vendor\Premake\premake5.exe vs2022

echo Solution generated successfully!
pause
