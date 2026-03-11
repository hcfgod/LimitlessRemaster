param(
    [Parameter(Mandatory)][string]$ObjDir,
    [Parameter(Mandatory)][string]$ObjRspPath,
    [Parameter(Mandatory)][string]$DepDir,
    [Parameter(Mandatory)][string]$StatsPath
)

$objDir = [IO.Path]::GetFullPath($ObjDir)
$objRspPath = [IO.Path]::GetFullPath($ObjRspPath)
$depDir = [IO.Path]::GetFullPath($DepDir)
$statsPath = [IO.Path]::GetFullPath($StatsPath)

$expectedObjs = @{}
if (Test-Path -LiteralPath $objRspPath) {
    Get-Content -LiteralPath $objRspPath | ForEach-Object {
        $expectedObjs[$_.Trim('"').Trim()] = $true
    }
}

$pruned = 0
Get-ChildItem -Path $objDir -Filter '*.obj' -ErrorAction SilentlyContinue | ForEach-Object {
    if (-not $expectedObjs.ContainsKey($_.FullName)) {
        Remove-Item -LiteralPath $_.FullName -Force -ErrorAction SilentlyContinue
        $depFile = Join-Path $depDir ([IO.Path]::ChangeExtension($_.Name, '.dep'))
        if (Test-Path -LiteralPath $depFile) {
            Remove-Item -LiteralPath $depFile -Force -ErrorAction SilentlyContinue
        }
        $pruned++
    }
}

if ($pruned -gt 0) {
    Write-Host "Pruned $pruned stale object file(s)."
}

Set-Content -Path $statsPath -Value ("PRUNED_COUNT=" + $pruned) -Encoding Ascii
