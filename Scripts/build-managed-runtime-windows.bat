@echo off
setlocal

set "CONFIGURATION=%~1"
set "PLATFORM=%~2"
set "OUTPUT_DIR=%~3"
set "PROJECT_ROOT=%~4"

if "%CONFIGURATION%"=="" set "CONFIGURATION=Debug"
if "%PLATFORM%"=="" set "PLATFORM=x64"

if "%OUTPUT_DIR%"=="" (
    echo Error: Missing output directory argument.
    echo Usage: build-managed-runtime-windows.bat [Debug^|Release^|Dist] [x64^|ARM64] "C:\Path\To\Output" ["C:\Path\To\Project"]
    exit /b 1
)

for %%I in ("%OUTPUT_DIR%") do set "OUTPUT_DIR=%%~fI"
if defined PROJECT_ROOT for %%I in ("%PROJECT_ROOT%") do set "PROJECT_ROOT=%%~fI"

set "DOTNET_CONFIGURATION=%CONFIGURATION%"
if /I "%DOTNET_CONFIGURATION%"=="Dist" set "DOTNET_CONFIGURATION=Release"

where dotnet >nul 2>nul
if errorlevel 1 (
    echo Error: dotnet SDK was not found on PATH.
    exit /b 1
)

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "REPO_ROOT=%%~fI"
for %%I in ("%~f0") do set "MANAGED_BUILD_SCRIPT=%%~fI"
set "MANAGED_PROJECT_GENERATOR_SCRIPT=%REPO_ROOT%\Scripts\generate-managed-project-csproj-windows.ps1"
set "MANAGED_LOCK_ROOT=%REPO_ROOT%\Build\ManagedRuntimeLocks"
set "MANAGED_PROJECT_CSPROJ="
set "MANAGED_PROJECT_ASSEMBLY_FILE="
set "MANAGED_PROJECT_CACHE_KEY=engine"

if defined PROJECT_ROOT (
    for %%I in ("%PROJECT_ROOT%") do set "MANAGED_PROJECT_CACHE_KEY=project-%%~nxI"
)
set "MANAGED_LOCK_DIR=%MANAGED_LOCK_ROOT%\%DOTNET_CONFIGURATION%-%PLATFORM%\%MANAGED_PROJECT_CACHE_KEY%"

set "MANAGED_CACHE_ROOT=%REPO_ROOT%\Build\ManagedRuntimeCache"
set "MANAGED_CACHE_OUTPUT_DIR=%MANAGED_CACHE_ROOT%\%DOTNET_CONFIGURATION%-%PLATFORM%\%MANAGED_PROJECT_CACHE_KEY%"
set "MANAGED_OUTPUT_DIR=%MANAGED_CACHE_OUTPUT_DIR%\Managed"
set "MANAGED_MANIFEST_PATH=%MANAGED_OUTPUT_DIR%\Limitless.Managed.payload.json"
set "MANAGED_BUILD_ROOT=%MANAGED_CACHE_OUTPUT_DIR%\_build"
set "MANAGED_CORAL_BUILD_ROOT=%MANAGED_BUILD_ROOT%\Coral.Managed"
set "MANAGED_CONTRACT_BUILD_ROOT=%MANAGED_BUILD_ROOT%\Limitless.Managed"
set "MANAGED_TESTS_BUILD_ROOT=%MANAGED_BUILD_ROOT%\Limitless.Managed.TestScripts"
set "MANAGED_PROJECT_BUILD_ROOT=%MANAGED_BUILD_ROOT%\ProjectScripts"
set "MANAGED_CORAL_STAGE_DIR=%MANAGED_CORAL_BUILD_ROOT%\stage"
set "MANAGED_CONTRACT_STAGE_DIR=%MANAGED_CONTRACT_BUILD_ROOT%\stage"
set "MANAGED_TESTS_STAGE_DIR=%MANAGED_TESTS_BUILD_ROOT%\stage"
set "MANAGED_PROJECT_STAGE_DIR=%MANAGED_PROJECT_BUILD_ROOT%\stage"

set "MANAGED_LOCK_WAIT_SECONDS=0"
set "MANAGED_LOCK_WAIT_TIMEOUT_SECONDS=600"
set "MANAGED_LOCK_STALE_TIMEOUT_SECONDS=120"
set "MANAGED_LOCK_ACQUIRED=0"

call :ensure_directory "%MANAGED_OUTPUT_DIR%"
if errorlevel 1 (
    echo Error: Failed to create managed output directory "%MANAGED_OUTPUT_DIR%".
    exit /b 1
)

call :ensure_directory "%MANAGED_BUILD_ROOT%"
if errorlevel 1 (
    echo Error: Failed to create managed build root "%MANAGED_BUILD_ROOT%".
    exit /b 1
)

call :ensure_directory "%MANAGED_LOCK_ROOT%"
if errorlevel 1 (
    echo Error: Failed to create managed lock directory root "%MANAGED_LOCK_ROOT%".
    exit /b 1
)

if not defined PROJECT_ROOT goto managed_project_generation_done
if not exist "%MANAGED_PROJECT_GENERATOR_SCRIPT%" (
    echo Error: Managed project generator script not found: "%MANAGED_PROJECT_GENERATOR_SCRIPT%"
    goto managed_build_failed
)
for /f "usebackq tokens=1,2 delims=|" %%A in (`powershell -NoProfile -ExecutionPolicy Bypass -File "%MANAGED_PROJECT_GENERATOR_SCRIPT%" -ProjectRoot "%PROJECT_ROOT%" -RepoRoot "%REPO_ROOT%"`) do (
    set "MANAGED_PROJECT_CSPROJ=%%~A"
    set "MANAGED_PROJECT_ASSEMBLY_FILE=%%~B"
)
if errorlevel 1 (
    echo Error: Failed to generate project managed script build project for "%PROJECT_ROOT%".
    goto managed_build_failed
)
:managed_project_generation_done

call :evaluate_managed_cache_requirement
if errorlevel 1 goto managed_build_failed

if "%MANAGED_CACHE_REQUIRES_BUILD%"=="1" (
    call :acquire_managed_lock
    if errorlevel 1 goto managed_build_failed

    call :evaluate_managed_cache_requirement
    if errorlevel 1 goto managed_build_failed
)

if "%MANAGED_CACHE_REQUIRES_BUILD%"=="1" (
    if exist "%MANAGED_OUTPUT_DIR%" rd /s /q "%MANAGED_OUTPUT_DIR%" >nul 2>nul
    call :ensure_directory "%MANAGED_OUTPUT_DIR%"
    if errorlevel 1 (
        echo Error: Failed to recreate managed output directory "%MANAGED_OUTPUT_DIR%".
        goto managed_build_failed
    )

    echo Building managed scripting artifacts for %CONFIGURATION% %PLATFORM%...
    call :ensure_directory "%MANAGED_CORAL_STAGE_DIR%"
    if errorlevel 1 goto managed_build_failed
    dotnet publish "%REPO_ROOT%\Limitless\Vendor\Coral\Coral.Managed\Coral.Managed-Static.csproj" -c %DOTNET_CONFIGURATION% -o "%MANAGED_CORAL_STAGE_DIR%" /nologo /verbosity:minimal /p:BaseIntermediateOutputPath="%MANAGED_CORAL_BUILD_ROOT%\obj\\" /p:MSBuildProjectExtensionsPath="%MANAGED_CORAL_BUILD_ROOT%\obj\\" /p:BaseOutputPath="%MANAGED_CORAL_BUILD_ROOT%\bin\\"
    if errorlevel 1 goto managed_build_failed

    call :ensure_directory "%MANAGED_CONTRACT_STAGE_DIR%"
    if errorlevel 1 goto managed_build_failed
    dotnet build "%REPO_ROOT%\Managed\Limitless.Managed\Limitless.Managed.csproj" -c %DOTNET_CONFIGURATION% -o "%MANAGED_CONTRACT_STAGE_DIR%" /nologo /verbosity:minimal /p:BaseIntermediateOutputPath="%MANAGED_CONTRACT_BUILD_ROOT%\obj\\" /p:MSBuildProjectExtensionsPath="%MANAGED_CONTRACT_BUILD_ROOT%\obj\\" /p:BaseOutputPath="%MANAGED_CONTRACT_BUILD_ROOT%\bin\\"
    if errorlevel 1 goto managed_build_failed

    call :ensure_directory "%MANAGED_TESTS_STAGE_DIR%"
    if errorlevel 1 goto managed_build_failed
    dotnet build "%REPO_ROOT%\Managed\Limitless.Managed.TestScripts\Limitless.Managed.TestScripts.csproj" -c %DOTNET_CONFIGURATION% -o "%MANAGED_TESTS_STAGE_DIR%" /nologo /verbosity:minimal /p:BuildProjectReferences=false /p:LimitlessManagedReferencePath="%MANAGED_CONTRACT_STAGE_DIR%\Limitless.Managed.dll" /p:BaseIntermediateOutputPath="%MANAGED_TESTS_BUILD_ROOT%\obj\\" /p:MSBuildProjectExtensionsPath="%MANAGED_TESTS_BUILD_ROOT%\obj\\" /p:BaseOutputPath="%MANAGED_TESTS_BUILD_ROOT%\bin\\"
    if errorlevel 1 goto managed_build_failed

    if defined MANAGED_PROJECT_CSPROJ (
        call :ensure_directory "%MANAGED_PROJECT_STAGE_DIR%"
        if errorlevel 1 goto managed_build_failed
        dotnet build "%MANAGED_PROJECT_CSPROJ%" -c %DOTNET_CONFIGURATION% -o "%MANAGED_PROJECT_STAGE_DIR%" /nologo /verbosity:minimal /p:BuildProjectReferences=false /p:LimitlessManagedReferencePath="%MANAGED_CONTRACT_STAGE_DIR%\Limitless.Managed.dll" /p:BaseIntermediateOutputPath="%MANAGED_PROJECT_BUILD_ROOT%\obj\\" /p:MSBuildProjectExtensionsPath="%MANAGED_PROJECT_BUILD_ROOT%\obj\\" /p:BaseOutputPath="%MANAGED_PROJECT_BUILD_ROOT%\bin\\"
        if errorlevel 1 goto managed_build_failed
    )

    call :copy_directory_contents "%MANAGED_CORAL_STAGE_DIR%" "%MANAGED_OUTPUT_DIR%"
    if errorlevel 1 goto managed_build_failed
    call :copy_directory_contents "%MANAGED_CONTRACT_STAGE_DIR%" "%MANAGED_OUTPUT_DIR%"
    if errorlevel 1 goto managed_build_failed
    call :copy_directory_contents "%MANAGED_TESTS_STAGE_DIR%" "%MANAGED_OUTPUT_DIR%"
    if errorlevel 1 goto managed_build_failed
    if defined MANAGED_PROJECT_CSPROJ (
        call :copy_directory_contents "%MANAGED_PROJECT_STAGE_DIR%" "%MANAGED_OUTPUT_DIR%"
        if errorlevel 1 goto managed_build_failed
    )

    >"%MANAGED_MANIFEST_PATH%" echo {
    >>"%MANAGED_MANIFEST_PATH%" echo   "formatVersion": 1,
    >>"%MANAGED_MANIFEST_PATH%" echo   "apiVersion": 1,
    >>"%MANAGED_MANIFEST_PATH%" echo   "coralManagedAssembly": "Coral.Managed.dll",
    >>"%MANAGED_MANIFEST_PATH%" echo   "coralManagedRuntimeConfig": "Coral.Managed.runtimeconfig.json",
    >>"%MANAGED_MANIFEST_PATH%" echo   "contractAssembly": "Limitless.Managed.dll",
    >>"%MANAGED_MANIFEST_PATH%" echo   "contractRuntimeConfig": "Limitless.Managed.runtimeconfig.json",
    if defined MANAGED_PROJECT_ASSEMBLY_FILE (
        >>"%MANAGED_MANIFEST_PATH%" echo   "scriptAssemblies": ["Limitless.Managed.TestScripts.dll", "%MANAGED_PROJECT_ASSEMBLY_FILE%"],
    ) else (
        >>"%MANAGED_MANIFEST_PATH%" echo   "scriptAssemblies": ["Limitless.Managed.TestScripts.dll"],
    )
    >>"%MANAGED_MANIFEST_PATH%" echo   "buildConfiguration": "%DOTNET_CONFIGURATION%",
    >>"%MANAGED_MANIFEST_PATH%" echo   "targetOS": "Windows",
    >>"%MANAGED_MANIFEST_PATH%" echo   "targetArchitecture": "%PLATFORM%"
    >>"%MANAGED_MANIFEST_PATH%" echo }
) else (
    echo Using cached managed scripting artifacts for %CONFIGURATION% %PLATFORM%...
)

call :release_managed_lock
if errorlevel 1 goto managed_build_failed

call :copy_managed_payload "%MANAGED_CACHE_OUTPUT_DIR%" "%OUTPUT_DIR%"
if errorlevel 1 goto managed_build_failed

echo Managed scripting artifacts staged to "%OUTPUT_DIR%\Managed".
set "BUILD_EXIT_CODE=0"
goto managed_build_cleanup

:managed_build_failed
set "BUILD_EXIT_CODE=1"

:managed_build_cleanup
call :release_managed_lock >nul 2>nul
exit /b %BUILD_EXIT_CODE%

:evaluate_managed_cache_requirement
set "MANAGED_CACHE_REQUIRES_BUILD=1"
if not exist "%MANAGED_MANIFEST_PATH%" exit /b 0
for /f "usebackq delims=" %%R in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "$paths = New-Object System.Collections.Generic.List[System.IO.FileInfo];" ^
    "$roots = @('%REPO_ROOT%\Limitless\Vendor\Coral\Coral.Managed', '%REPO_ROOT%\Managed\Limitless.Managed', '%REPO_ROOT%\Managed\Limitless.Managed.TestScripts', '%MANAGED_PROJECT_GENERATOR_SCRIPT%', '%MANAGED_BUILD_SCRIPT%');" ^
    "foreach ($root in $roots) { if (Test-Path -LiteralPath $root) { $item = Get-Item -LiteralPath $root; if ($item -is [System.IO.DirectoryInfo]) { Get-ChildItem -LiteralPath $root -Recurse -File -ErrorAction SilentlyContinue | Where-Object { $_.FullName -notlike '*\bin\*' -and $_.FullName -notlike '*\obj\*' } | ForEach-Object { $paths.Add($_) } } else { $paths.Add($item) } } }" ^
    "if ('%MANAGED_PROJECT_CSPROJ%' -ne '') { if (Test-Path -LiteralPath '%MANAGED_PROJECT_CSPROJ%') { $paths.Add((Get-Item -LiteralPath '%MANAGED_PROJECT_CSPROJ%')) }; $assetsDir = Join-Path '%PROJECT_ROOT%' 'Assets'; if (Test-Path -LiteralPath $assetsDir) { Get-ChildItem -LiteralPath $assetsDir -Recurse -File -Filter '*.cs' -ErrorAction SilentlyContinue | ForEach-Object { $paths.Add($_) } } }" ^
    "$latest = [datetime]::MinValue;" ^
    "foreach ($path in $paths) { if ($path.LastWriteTimeUtc -gt $latest) { $latest = $path.LastWriteTimeUtc } }" ^
    "$manifestTime = (Get-Item -LiteralPath '%MANAGED_MANIFEST_PATH%').LastWriteTimeUtc;" ^
    "if ($manifestTime -ge $latest) { Write-Output '0' } else { Write-Output '1' }"`) do (
    set "MANAGED_CACHE_REQUIRES_BUILD=%%R"
)
exit /b 0

:acquire_managed_lock
set "MANAGED_LOCK_WAIT_SECONDS=0"
:wait_for_managed_lock
mkdir "%MANAGED_LOCK_DIR%" 2>nul
if errorlevel 1 (
    set /a MANAGED_LOCK_WAIT_SECONDS+=1
    if "%MANAGED_LOCK_WAIT_SECONDS%"=="1" echo Waiting for managed build lock: "%MANAGED_LOCK_DIR%"...
    if "%MANAGED_LOCK_WAIT_SECONDS%"=="60" echo Still waiting for managed build lock: "%MANAGED_LOCK_DIR%"...
    if "%MANAGED_LOCK_WAIT_SECONDS%"=="120" echo Still waiting for managed build lock: "%MANAGED_LOCK_DIR%"...
    if "%MANAGED_LOCK_WAIT_SECONDS%"=="180" echo Still waiting for managed build lock: "%MANAGED_LOCK_DIR%"...
    if "%MANAGED_LOCK_WAIT_SECONDS%"=="240" echo Still waiting for managed build lock: "%MANAGED_LOCK_DIR%"...
    if "%MANAGED_LOCK_WAIT_SECONDS%"=="300" echo Still waiting for managed build lock: "%MANAGED_LOCK_DIR%"...
    if "%MANAGED_LOCK_WAIT_SECONDS%"=="360" echo Still waiting for managed build lock: "%MANAGED_LOCK_DIR%"...
    if "%MANAGED_LOCK_WAIT_SECONDS%"=="420" echo Still waiting for managed build lock: "%MANAGED_LOCK_DIR%"...
    if "%MANAGED_LOCK_WAIT_SECONDS%"=="480" echo Still waiting for managed build lock: "%MANAGED_LOCK_DIR%"...
    if "%MANAGED_LOCK_WAIT_SECONDS%"=="540" echo Still waiting for managed build lock: "%MANAGED_LOCK_DIR%"...
    if "%MANAGED_LOCK_WAIT_SECONDS%"=="15" echo Waiting for another managed build to finish...
    if "%MANAGED_LOCK_WAIT_SECONDS%"=="120" echo Note: If this keeps happening, a stale lock may exist from an aborted build.

    if "%MANAGED_LOCK_WAIT_SECONDS%"=="30" call :check_stale_managed_lock
    if "%MANAGED_LOCK_WAIT_SECONDS%"=="60" call :check_stale_managed_lock
    if "%MANAGED_LOCK_WAIT_SECONDS%"=="90" call :check_stale_managed_lock
    if "%MANAGED_LOCK_WAIT_SECONDS%"=="120" call :check_stale_managed_lock

    if %MANAGED_LOCK_WAIT_SECONDS% GEQ %MANAGED_LOCK_WAIT_TIMEOUT_SECONDS% (
        echo Error: Timed out waiting for managed build lock after %MANAGED_LOCK_WAIT_TIMEOUT_SECONDS% seconds.
        echo If no build is running, delete the lock directory and retry: "%MANAGED_LOCK_DIR%"
        exit /b 1
    )
    >nul ping 127.0.0.1 -n 2
    goto wait_for_managed_lock
)

set "MANAGED_LOCK_OWNER_FILE=%MANAGED_LOCK_DIR%\owner.txt"
>"%MANAGED_LOCK_OWNER_FILE%" echo Managed lock acquired by: %USERNAME%@%COMPUTERNAME%
>>"%MANAGED_LOCK_OWNER_FILE%" echo Script: %~nx0
>>"%MANAGED_LOCK_OWNER_FILE%" echo Args: %CONFIGURATION% %PLATFORM% "%OUTPUT_DIR%" "%PROJECT_ROOT%"
>>"%MANAGED_LOCK_OWNER_FILE%" echo Timestamp: %DATE% %TIME%
set "MANAGED_LOCK_ACQUIRED=1"
exit /b 0

:release_managed_lock
if not "%MANAGED_LOCK_ACQUIRED%"=="1" exit /b 0
if exist "%MANAGED_LOCK_DIR%" rd /s /q "%MANAGED_LOCK_DIR%" >nul 2>nul
set "MANAGED_LOCK_ACQUIRED=0"
exit /b 0

:check_stale_managed_lock
powershell -NoProfile -ExecutionPolicy Bypass -Command "$lockDir = '%MANAGED_LOCK_DIR%'; if (Test-Path -LiteralPath $lockDir) { $age = (New-TimeSpan -Start (Get-Item -LiteralPath $lockDir).LastWriteTimeUtc -End (Get-Date).ToUniversalTime()).TotalSeconds; if ($age -ge %MANAGED_LOCK_STALE_TIMEOUT_SECONDS%) { Write-Host 'Warning: Managed build lock looks stale (age ' + [int]$age + 's). Removing: ' + $lockDir; Remove-Item -LiteralPath $lockDir -Recurse -Force -ErrorAction SilentlyContinue; } }" >nul 2>nul
exit /b 0

:copy_managed_payload
set "COPY_MANAGED_SOURCE_ROOT=%~1"
set "COPY_MANAGED_DEST_ROOT=%~2"
if /I "%COPY_MANAGED_SOURCE_ROOT%"=="%COPY_MANAGED_DEST_ROOT%" exit /b 0
set "COPY_SRC=%COPY_MANAGED_SOURCE_ROOT%\Managed"
set "COPY_DST=%COPY_MANAGED_DEST_ROOT%\Managed"
if not exist "%COPY_SRC%" (
    echo Error: Managed payload source not found: "%COPY_SRC%"
    exit /b 1
)
call :ensure_directory "%COPY_DST%"
if errorlevel 1 exit /b 1
robocopy "%COPY_SRC%" "%COPY_DST%" /MIR /NJH /NJS /NFL /NDL /NC /NS /NP >nul
if errorlevel 8 (
    echo Error: Failed to copy managed payload from "%COPY_SRC%" to "%COPY_DST%".
    exit /b 1
)
exit /b 0

:ensure_directory
set "ENSURE_TARGET=%~1"
if exist "%ENSURE_TARGET%" exit /b 0
mkdir "%ENSURE_TARGET%" >nul 2>nul
if exist "%ENSURE_TARGET%" exit /b 0
if errorlevel 1 (
    echo Error: Failed to prepare directory "%ENSURE_TARGET%".
    exit /b 1
)
echo Error: Failed to prepare directory "%ENSURE_TARGET%".
exit /b 1

:copy_directory_contents
set "COPY_CONTENTS_SRC=%~1"
set "COPY_CONTENTS_DST=%~2"
if not exist "%COPY_CONTENTS_SRC%" (
    echo Error: Managed build output not found: "%COPY_CONTENTS_SRC%"
    exit /b 1
)
call :ensure_directory "%COPY_CONTENTS_DST%"
if errorlevel 1 exit /b 1
robocopy "%COPY_CONTENTS_SRC%" "%COPY_CONTENTS_DST%" /E /NJH /NJS /NFL /NDL /NC /NS /NP >nul
if errorlevel 8 (
    echo Error: Failed to copy managed build output from "%COPY_CONTENTS_SRC%" to "%COPY_CONTENTS_DST%".
    exit /b 1
)
exit /b 0
