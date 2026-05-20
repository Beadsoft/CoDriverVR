param(
    [string]$RbrRoot = "C:\richard burns rally"
)

$ErrorActionPreference = "Stop"

$rbrProcess = Get-Process | Where-Object { $_.ProcessName -match "RichardBurnsRally|richardburnsrally|RBR" }
if ($rbrProcess) {
    throw "RBR is running. Close RBR before installing OpenRBRVR.dll."
}

$pluginDir = Join-Path $RbrRoot "Plugins"
$sourceDll = Join-Path $PSScriptRoot "openRBRVR\zig-out\bin\openRBRVR.dll"
$targetDll = Join-Path $pluginDir "OpenRBRVR.dll"
$targetToml = Join-Path $pluginDir "openRBRVR.toml"
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"

if (!(Test-Path $sourceDll)) {
    throw "Built DLL not found: $sourceDll"
}

Copy-Item -LiteralPath $targetDll -Destination (Join-Path $pluginDir "OpenRBRVR.dll.bak-$stamp") -Force
if (Test-Path $targetToml) {
    Copy-Item -LiteralPath $targetToml -Destination (Join-Path $pluginDir "openRBRVR.toml.bak-$stamp") -Force
}

Copy-Item -LiteralPath $sourceDll -Destination $targetDll -Force

if (Test-Path $targetToml) {
    $text = Get-Content -LiteralPath $targetToml -Raw
    if ($text -match "(?m)^swapEyes\s*=") {
        $text = $text -replace "(?m)^swapEyes\s*=.*$", "swapEyes = false"
    } else {
        $text = $text -replace "(?m)^runtime\s*=\s*'steamvr'\s*$", "runtime = 'steamvr'`r`nswapEyes = false"
    }
    Set-Content -LiteralPath $targetToml -Value $text -NoNewline
}

Write-Host "Installed $targetDll"
