param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectRoot,
    [Parameter(Mandatory = $true)]
    [string]$RepoRoot
)

$ErrorActionPreference = 'Stop'

function Get-RelativePathCompat {
    param(
        [Parameter(Mandatory = $true)]
        [string]$BasePath,
        [Parameter(Mandatory = $true)]
        [string]$TargetPath
    )

    $normalizedBasePath = [IO.Path]::GetFullPath($BasePath).TrimEnd('\', '/') + '\'
    $normalizedTargetPath = [IO.Path]::GetFullPath($TargetPath)
    $baseUri = New-Object System.Uri($normalizedBasePath)
    $targetUri = New-Object System.Uri($normalizedTargetPath)
    $relativeUri = $baseUri.MakeRelativeUri($targetUri)
    return [System.Uri]::UnescapeDataString($relativeUri.ToString()).Replace('/', '\')
}

function Escape-Xml {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Value
    )

    return [System.Security.SecurityElement]::Escape($Value)
}

$projectRoot = [IO.Path]::GetFullPath($ProjectRoot)
$repoRoot = [IO.Path]::GetFullPath($RepoRoot)
$assetsDir = Join-Path $projectRoot 'Assets'
if (!(Test-Path -LiteralPath $assetsDir))
{
    exit 0
}

$scripts = @(Get-ChildItem -Path $assetsDir -Recurse -Filter '*.cs' -File -ErrorAction SilentlyContinue | Sort-Object FullName)
if ($scripts.Count -eq 0)
{
    exit 0
}

$projectLeaf = Split-Path -Leaf $projectRoot
$sanitizedCharacters = foreach ($character in $projectLeaf.ToCharArray())
{
    if ([char]::IsLetterOrDigit($character) -or $character -eq '_')
    {
        $character
    }
}
$sanitized = -join $sanitizedCharacters
if ([string]::IsNullOrWhiteSpace($sanitized))
{
    $sanitized = 'Project'
}
if ([char]::IsDigit($sanitized[0]))
{
    $sanitized = 'Project_' + $sanitized
}

$assemblyName = $sanitized + '.ManagedScripts'
$generatedDir = Join-Path $projectRoot 'Build\Generated\ManagedScripts'
New-Item -ItemType Directory -Path $generatedDir -Force | Out-Null
$csprojPath = Join-Path $generatedDir ($assemblyName + '.csproj')
$contractPath = Join-Path $repoRoot 'Managed\Limitless.Managed\Limitless.Managed.csproj'

$compileItems = @()
foreach ($script in $scripts)
{
    $relative = (Get-RelativePathCompat -BasePath $assetsDir -TargetPath $script.FullName).Replace('\', '/')
    $escapedScriptPath = Escape-Xml $script.FullName
    $escapedLinkPath = Escape-Xml ('Assets/' + $relative)
    $compileItems += ('    <Compile Include="{0}"><Link>{1}</Link></Compile>' -f $escapedScriptPath, $escapedLinkPath)
}

$escapedAssemblyName = Escape-Xml $assemblyName
$escapedContractPath = Escape-Xml $contractPath

$csprojLines = @(
    '<Project Sdk="Microsoft.NET.Sdk">',
    '  <PropertyGroup>',
    '    <TargetFramework>net9.0</TargetFramework>',
    '    <OutputType>Library</OutputType>',
    '    <Nullable>enable</Nullable>',
    '    <ImplicitUsings>enable</ImplicitUsings>',
    '    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>',
    ('    <AssemblyName>{0}</AssemblyName>' -f $escapedAssemblyName),
    ('    <RootNamespace>{0}</RootNamespace>' -f $escapedAssemblyName),
    '    <EnableDynamicLoading>true</EnableDynamicLoading>',
    '  </PropertyGroup>',
    '  <ItemGroup Condition="''$(LimitlessManagedReferencePath)'' != ''''">',
    '    <Reference Include="Limitless.Managed">',
    '      <HintPath>$(LimitlessManagedReferencePath)</HintPath>',
    '      <Private>true</Private>',
    '    </Reference>',
    '  </ItemGroup>',
    '  <ItemGroup Condition="''$(LimitlessManagedReferencePath)'' == ''''">',
    ('    <ProjectReference Include="{0}" />' -f $escapedContractPath),
    '  </ItemGroup>',
    '  <ItemGroup>'
)
$csprojLines += $compileItems
$csprojLines += '  </ItemGroup>'
$csprojLines += '</Project>'

$newContent = ($csprojLines -join [Environment]::NewLine) + [Environment]::NewLine
$existingContent = $null
if (Test-Path -LiteralPath $csprojPath)
{
    $existingContent = [IO.File]::ReadAllText($csprojPath)
}

if ($existingContent -ne $newContent)
{
    [IO.File]::WriteAllText($csprojPath, $newContent, [Text.UTF8Encoding]::new($false))
}

Write-Output ($csprojPath + '|' + $assemblyName + '.dll')
