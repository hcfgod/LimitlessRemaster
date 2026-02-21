param(
    [ValidateSet("Debug", "Release", "Dist")]
    [string]$Configuration = "Dist",
    [string]$OutputRoot = ""
)

$ErrorActionPreference = "Stop"

if ($Configuration -ne "Dist") {
    Write-Host "Configuration '$Configuration' requested, but editor packaging is fixed to 'Dist'."
}
$Configuration = "Dist"

function Copy-IfExists {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Destination
    )

    if (!(Test-Path -LiteralPath $Source)) {
        Write-Host "Skipping missing path: $Source"
        return
    }

    if (Test-Path -LiteralPath $Source -PathType Container) {
        New-Item -ItemType Directory -Path $Destination -Force | Out-Null
        & robocopy $Source $Destination /E /R:1 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
        if ($LASTEXITCODE -ge 8) {
            throw "Failed to copy directory '$Source' to '$Destination' (robocopy exit code $LASTEXITCODE)."
        }
        return
    }

    $destinationParent = Split-Path -Parent $Destination
    if ($destinationParent -and !(Test-Path -LiteralPath $destinationParent)) {
        New-Item -ItemType Directory -Path $destinationParent -Force | Out-Null
    }
    Copy-Item -LiteralPath $Source -Destination $Destination -Force
}

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptRoot
$cfgShortName = "$($Configuration.ToLowerInvariant())_x64"
$platformFolder = "$cfgShortName-windows-x64"

$editorBuildDirectory = Join-Path $repoRoot "Build\$platformFolder\Editor"
$runtimeBuildDirectory = Join-Path $repoRoot "Build\$platformFolder\Runtime"
$limitlessLibraryPath = Join-Path $repoRoot "Build\$platformFolder\Limitless\Limitless.lib"
$projectAssetsDirectory = Join-Path $repoRoot "Assets"

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $repoRoot "Build\ShippedEditor\$platformFolder"
}

$packageRoot = Join-Path $OutputRoot "LimitlessEditor"
$toolchainRoot = Join-Path $packageRoot "Toolchain"
$runtimeTemplateDirectory = Join-Path $toolchainRoot "RuntimeTemplates\$platformFolder"
$sdkIncludeRoot = Join-Path $toolchainRoot "SDK\include"
$sdkVendorRoot = Join-Path $toolchainRoot "SDK\vendor"
$sdkLibRoot = Join-Path $toolchainRoot "SDK\lib\$platformFolder"

if (!(Test-Path -LiteralPath $editorBuildDirectory)) {
    throw "Editor build output not found: $editorBuildDirectory. Build the Editor first."
}

if (!(Test-Path -LiteralPath $runtimeBuildDirectory)) {
    throw "Runtime build output not found: $runtimeBuildDirectory. Build Runtime first."
}

if (!(Test-Path -LiteralPath $limitlessLibraryPath)) {
    throw "Limitless SDK library not found: $limitlessLibraryPath. Build Limitless first."
}

if (!(Test-Path -LiteralPath $projectAssetsDirectory)) {
    throw "Project Assets folder not found: $projectAssetsDirectory"
}

Write-Host "Preparing package root: $packageRoot"
if (Test-Path -LiteralPath $packageRoot) {
    Remove-Item -LiteralPath $packageRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $packageRoot -Force | Out-Null
New-Item -ItemType Directory -Path $toolchainRoot -Force | Out-Null

Write-Host "Copying Editor binaries..."
& robocopy $editorBuildDirectory $packageRoot /E /R:1 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) {
    throw "Failed to copy Editor binaries from '$editorBuildDirectory' (robocopy exit code $LASTEXITCODE)."
}

Write-Host "Copying project Assets next to Editor.exe..."
& robocopy $projectAssetsDirectory (Join-Path $packageRoot "Assets") /E /R:1 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) {
    throw "Failed to copy project Assets from '$projectAssetsDirectory' (robocopy exit code $LASTEXITCODE)."
}

Write-Host "Copying default ImGui layout..."
$defaultLayoutSource = Join-Path $repoRoot "Editor\imgui-default.ini"
if (!(Test-Path -LiteralPath $defaultLayoutSource)) {
    $defaultLayoutSource = Join-Path $repoRoot "Editor\imgui.ini"
}
Copy-IfExists -Source $defaultLayoutSource -Destination (Join-Path $packageRoot "imgui-default.ini")
Copy-IfExists -Source $defaultLayoutSource -Destination (Join-Path $packageRoot "imgui.ini")

Write-Host "Copying internal toolchain assets..."
New-Item -ItemType Directory -Path (Join-Path $toolchainRoot "Scripts") -Force | Out-Null
Copy-IfExists -Source (Join-Path $repoRoot "Scripts\build-project-scriptcore-windows.bat") -Destination (Join-Path $toolchainRoot "Scripts\build-project-scriptcore-windows.bat")
Copy-IfExists -Source (Join-Path $repoRoot "Scripts\build-project-scriptcore-unix.sh") -Destination (Join-Path $toolchainRoot "Scripts\build-project-scriptcore-unix.sh")

Write-Host "Copying SDK headers (no engine .cpp source)..."
New-Item -ItemType Directory -Path $sdkIncludeRoot -Force | Out-Null
& robocopy (Join-Path $repoRoot "Limitless\Source") $sdkIncludeRoot *.h *.hpp *.inl *.inc /S /R:1 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) {
    throw "Failed to copy SDK headers from Limitless/Source (robocopy exit code $LASTEXITCODE)."
}
Copy-IfExists -Source (Join-Path $repoRoot "ScriptCore\Source\ScriptCoreRegistration.h") -Destination (Join-Path $sdkIncludeRoot "ScriptCoreRegistration.h")

Write-Host "Copying SDK vendor headers..."
New-Item -ItemType Directory -Path $sdkVendorRoot -Force | Out-Null
& robocopy (Join-Path $repoRoot "Limitless\Vendor") $sdkVendorRoot *.h *.hpp *.inl *.inc *.ipp /S /R:1 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) {
    throw "Failed to copy SDK vendor headers (robocopy exit code $LASTEXITCODE)."
}

Write-Host "Copying SDK libraries..."
New-Item -ItemType Directory -Path $sdkLibRoot -Force | Out-Null
Copy-IfExists -Source $limitlessLibraryPath -Destination (Join-Path $sdkLibRoot "Limitless.lib")
Copy-IfExists -Source (Join-Path $repoRoot "Build\$platformFolder\VendorSpirvCross\VendorSpirvCross.lib") -Destination (Join-Path $sdkLibRoot "VendorSpirvCross.lib")
Copy-IfExists -Source (Join-Path $repoRoot "Build\$platformFolder\VendorZstd\VendorZstd.lib") -Destination (Join-Path $sdkLibRoot "VendorZstd.lib")
Copy-IfExists -Source (Join-Path $repoRoot "Build\$platformFolder\freetype\freetype.lib") -Destination (Join-Path $sdkLibRoot "freetype.lib")
Copy-IfExists -Source (Join-Path $repoRoot "Build\$platformFolder\msdfgen\msdfgen.lib") -Destination (Join-Path $sdkLibRoot "msdfgen.lib")
Copy-IfExists -Source (Join-Path $repoRoot "Build\$platformFolder\msdf-atlas-gen\msdf-atlas-gen.lib") -Destination (Join-Path $sdkLibRoot "msdf-atlas-gen.lib")
Copy-IfExists -Source (Join-Path $repoRoot "Limitless\Vendor\SDL3\SDL3Libs\SDL3-static.lib") -Destination (Join-Path $sdkLibRoot "SDL3-static.lib")
Copy-IfExists -Source (Join-Path $repoRoot "Limitless\Vendor\box2d\libs\box2D.lib") -Destination (Join-Path $sdkLibRoot "box2D.lib")
Copy-IfExists -Source (Join-Path $repoRoot "Limitless\Vendor\shaderc\libs\shaderc_shared.lib") -Destination (Join-Path $sdkLibRoot "shaderc_shared.lib")
Copy-IfExists -Source (Join-Path $repoRoot "Limitless\Vendor\VulkanSDK\lib\vulkan-1.lib") -Destination (Join-Path $sdkLibRoot "vulkan-1.lib")
Copy-IfExists -Source (Join-Path $repoRoot "Limitless\Vendor\ffmpeg\libs\avcodec.lib") -Destination (Join-Path $sdkLibRoot "avcodec.lib")
Copy-IfExists -Source (Join-Path $repoRoot "Limitless\Vendor\ffmpeg\libs\avformat.lib") -Destination (Join-Path $sdkLibRoot "avformat.lib")
Copy-IfExists -Source (Join-Path $repoRoot "Limitless\Vendor\ffmpeg\libs\avutil.lib") -Destination (Join-Path $sdkLibRoot "avutil.lib")
Copy-IfExists -Source (Join-Path $repoRoot "Limitless\Vendor\ffmpeg\libs\swresample.lib") -Destination (Join-Path $sdkLibRoot "swresample.lib")

Write-Host "Building ScriptCore host glue static library..."
$glueSourcePath = Join-Path $repoRoot "Scripts\ScriptCoreHostGlue.cpp"
if (!(Test-Path -LiteralPath $glueSourcePath)) {
    throw "Missing ScriptCore host glue source: $glueSourcePath"
}

$glueIntermediateDirectory = Join-Path $toolchainRoot "Build\Intermediates\$platformFolder\ScriptCoreHostGlue"
New-Item -ItemType Directory -Path $glueIntermediateDirectory -Force | Out-Null
$glueObjectPath = Join-Path $glueIntermediateDirectory "ScriptCoreHostGlue.obj"
$glueLibraryPath = Join-Path $sdkLibRoot "ScriptCoreHostGlue.lib"

$configDefine = "LT_CONFIG_DIST"

$vswherePath = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (!(Test-Path -LiteralPath $vswherePath)) {
    throw "vswhere.exe not found. Cannot build ScriptCore host glue library."
}

$vsInstallPath = (& $vswherePath -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
if ([string]::IsNullOrWhiteSpace($vsInstallPath)) {
    throw "Visual Studio with C++ tools not found. Cannot build ScriptCore host glue library."
}

$vcvarsPath = Join-Path $vsInstallPath "VC\Auxiliary\Build\vcvars64.bat"
if (!(Test-Path -LiteralPath $vcvarsPath)) {
    throw "vcvars64.bat not found: $vcvarsPath"
}

$compileAndArchiveCommand = @(
    "call `"$vcvarsPath`" >nul &&",
    "cl /nologo /std:c++20 /EHsc /MD /c /utf-8 /FS /bigobj",
    "/D $configDefine /D LT_PLATFORM_WINDOWS /D SCRIPTCORE_EXPORTS /D _UNICODE /D UNICODE",
    "/I `"$sdkIncludeRoot`" /I `"$sdkVendorRoot`" /I `"$sdkVendorRoot\box2d\include`" /I `"$sdkVendorRoot\glad`" /I `"$sdkVendorRoot\spdlog`" /I `"$sdkVendorRoot\doctest`" /I `"$sdkVendorRoot\SDL3`" /I `"$sdkVendorRoot\ffmpeg\include`" /I `"$sdkVendorRoot\imgui`" /I `"$sdkVendorRoot\glm`"",
    "/Fo`"$glueObjectPath`" `"$glueSourcePath`"",
    "&& lib /nologo /OUT:`"$glueLibraryPath`" `"$glueObjectPath`""
) -join " "

cmd /c $compileAndArchiveCommand
if ($LASTEXITCODE -ne 0) {
    throw "Failed to build ScriptCoreHostGlue.lib (exit code $LASTEXITCODE)."
}

Write-Host "Creating generated ScriptCore mirror folder..."
New-Item -ItemType Directory -Path (Join-Path $toolchainRoot "Build\Generated\ScriptCore") -Force | Out-Null

Write-Host "Creating runtime templates..."
New-Item -ItemType Directory -Path $runtimeTemplateDirectory -Force | Out-Null
& robocopy $runtimeBuildDirectory $runtimeTemplateDirectory /E /R:1 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) {
    throw "Failed to copy Runtime templates from '$runtimeBuildDirectory' (robocopy exit code $LASTEXITCODE)."
}

$toolchainReadme = @"
Internal Toolchain Folder Contract
=================================

This folder is consumed by the editor when Build Backend is set to InternalToolchain.

Required paths:
- Scripts/build-project-scriptcore-windows.bat
- SDK/include/* (public engine scripting headers)
- SDK/vendor/* (third-party headers)
- SDK/lib/<config-platform>/*.lib (includes Limitless + ScriptCoreHostGlue)
- RuntimeTemplates/<config-platform>/*
- Build/Generated/ScriptCore/

Versioning:
- Keep Toolchain content in sync with the same engine/editor commit.
- Regenerate this package after changing ScriptCore/Runtime build scripts or template layout.
"@

$toolchainReadmePath = Join-Path $toolchainRoot "README_INTERNAL_TOOLCHAIN.txt"
Set-Content -LiteralPath $toolchainReadmePath -Value $toolchainReadme -Encoding UTF8

Write-Host ""
Write-Host "Packaged shipped editor successfully."
Write-Host "Package root: $packageRoot"
