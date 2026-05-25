param(
    [string]$RbrPath = "C:\richard burns rally"
)

$ErrorActionPreference = "Stop"

$installPath = Resolve-Path -LiteralPath $RbrPath
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$backupPath = Join-Path $installPath "backups\openRBRVR-passenger-$timestamp"

New-Item -ItemType Directory -Force -Path $backupPath | Out-Null

$files = @(
    "d3d9.dll",
    "Plugins\openRBRVR.dll",
    "Plugins\openRBRVR.toml"
)

foreach ($file in $files) {
    $source = Join-Path $installPath $file
    if (Test-Path -LiteralPath $source) {
        $destination = Join-Path $backupPath $file
        $destinationDirectory = Split-Path -Parent $destination
        New-Item -ItemType Directory -Force -Path $destinationDirectory | Out-Null
        Copy-Item -LiteralPath $source -Destination $destination -Force
    }
}

Write-Host "Backed up openRBRVR files to $backupPath"
