param(
    [Parameter(Mandatory)][string]$ObjRspPath,
    [Parameter(Mandatory)][string]$LinkStatsPath,
    [Parameter(Mandatory)][string]$OutputDir,
    [Parameter(Mandatory)][string]$NeedsCompileCount,
    [Parameter(Mandatory)][string]$ObjectListChanged,
    [Parameter(Mandatory)][string]$PrunedCount
)

$objRspPath = [IO.Path]::GetFullPath($ObjRspPath)
$linkStatsPath = [IO.Path]::GetFullPath($LinkStatsPath)
$dllPath = [IO.Path]::GetFullPath((Join-Path $OutputDir 'ScriptCore.dll'))
$libPath = [IO.Path]::GetFullPath((Join-Path $OutputDir 'ScriptCore.lib'))
$pdbPath = [IO.Path]::GetFullPath((Join-Path $OutputDir 'ScriptCore.pdb'))

$linkRequired = 0

if ($NeedsCompileCount -ne '0' -or $ObjectListChanged -ne '0' -or $PrunedCount -ne '0') {
    $linkRequired = 1
}
elseif (-not (Test-Path -LiteralPath $dllPath) -or -not (Test-Path -LiteralPath $libPath) -or -not (Test-Path -LiteralPath $pdbPath)) {
    $linkRequired = 1
}
elseif (Test-Path -LiteralPath $objRspPath) {
    $outputTime = @(
        (Get-Item -LiteralPath $dllPath).LastWriteTimeUtc,
        (Get-Item -LiteralPath $libPath).LastWriteTimeUtc,
        (Get-Item -LiteralPath $pdbPath).LastWriteTimeUtc
    ) | Sort-Object | Select-Object -First 1

    foreach ($objLine in Get-Content -LiteralPath $objRspPath) {
        $objPath = $objLine.Trim('"').Trim()
        if ($objPath -eq '') { continue }
        if (-not (Test-Path -LiteralPath $objPath)) {
            $linkRequired = 1
            break
        }
        if ((Get-Item -LiteralPath $objPath).LastWriteTimeUtc -gt $outputTime) {
            $linkRequired = 1
            break
        }
    }
}
else {
    $linkRequired = 1
}

Set-Content -Path $linkStatsPath -Value ("LINK_REQUIRED=" + $linkRequired) -Encoding Ascii
