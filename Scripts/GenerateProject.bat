@echo off
echo Generating Visual Studio solution...

REM Change to the project root directory
cd /d "%~dp0.."

REM Run Premake to generate Visual Studio solution
Vendor\Premake\premake5.exe vs2022

echo Solution generated successfully!
pause
