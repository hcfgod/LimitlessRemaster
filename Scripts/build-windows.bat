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

REM Generate Visual Studio solution
echo Generating Visual Studio solution...
Vendor\Premake\premake5.exe vs2022
if errorlevel 1 (
    echo Error: Failed to generate Visual Studio solution
    exit /b 1
)

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