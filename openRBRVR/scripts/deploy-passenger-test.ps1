param(
    [string]$RbrPath = "C:\richard burns rally",
    [string]$BuiltDll = "D:\LocalSyncApp\RBR Passenger\openRBRVR\zig-out\bin\openRBRVR.dll"
)

$ErrorActionPreference = "Stop"

$installPath = (Resolve-Path -LiteralPath $RbrPath).Path
$builtDllPath = (Resolve-Path -LiteralPath $BuiltDll).Path
$pluginsPath = Join-Path $installPath "Plugins"
$tomlPath = Join-Path $pluginsPath "openRBRVR.toml"
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$backupPath = Join-Path $installPath "backups\openRBRVR-passenger-$timestamp"

New-Item -ItemType Directory -Force -Path $backupPath | Out-Null

$backupFiles = @(
    "d3d9.dll",
    "Plugins\openRBRVR.dll",
    "Plugins\openRBRVR.toml"
)

foreach ($file in $backupFiles) {
    $source = Join-Path $installPath $file
    if (Test-Path -LiteralPath $source) {
        $destination = Join-Path $backupPath $file
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
        Copy-Item -LiteralPath $source -Destination $destination -Force
    }
}

Copy-Item -LiteralPath $builtDllPath -Destination (Join-Path $pluginsPath "openRBRVR.dll") -Force

$passengerBlock = @"

[PassengerVR]
enabled = true
cameraOffset = [-0.55, 0.02, 0.05]
cameraYawDegrees = 0.0
renderMode = "stereo"
streamHost = "0.0.0.0"
streamPort = 7790
posePort = 7791
recenterKey = "QuestMenu"
"@

if (Test-Path -LiteralPath $tomlPath) {
    $content = Get-Content -LiteralPath $tomlPath -Raw
    if ($content -match "(?ms)^\[PassengerVR\]\s.*?(?=^\[|\z)") {
        $content = [regex]::Replace($content, "(?ms)^\[PassengerVR\]\s.*?(?=^\[|\z)", $passengerBlock.TrimStart() + "`r`n")
    } else {
        $content = $content.TrimEnd() + "`r`n" + $passengerBlock + "`r`n"
    }
    Set-Content -LiteralPath $tomlPath -Value $content -NoNewline
} else {
    Set-Content -LiteralPath $tomlPath -Value ($passengerBlock.TrimStart() + "`r`n") -NoNewline
}

Write-Host "Backup: $backupPath"
Write-Host "Installed: $(Join-Path $pluginsPath 'openRBRVR.dll')"
Write-Host "PassengerVR.enabled = true"
