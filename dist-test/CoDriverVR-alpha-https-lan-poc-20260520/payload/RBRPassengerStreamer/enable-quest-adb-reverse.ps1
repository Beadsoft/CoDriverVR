$ErrorActionPreference = "Stop"

$configPath = Join-Path $PSScriptRoot "config.json"
if (-not (Test-Path -LiteralPath $configPath)) {
    $configPath = Join-Path $PSScriptRoot "config.example.json"
}

$config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
$port = [int]$config.server.streamPort
$adbPath = [string]$config.quest.adbPath
if ([string]$config.quest.connectionMode -eq "wifi" -and [string]$config.quest.wifiSerial) {
    $serial = [string]$config.quest.wifiSerial
} else {
    $serial = [string]$config.quest.serial
}

if (-not $adbPath -or -not (Test-Path -LiteralPath $adbPath)) {
    $adb = Get-Command adb -ErrorAction SilentlyContinue
    if (-not $adb) {
        throw "adb was not found. Set quest.adbPath in config.json or add adb.exe to PATH."
    }
    $adbPath = $adb.Source
}

$adbArgs = @()
if ($serial) {
    if ($serial -match '^\d{1,3}(\.\d{1,3}){3}:\d+$') {
        & $adbPath connect $serial | Write-Host
    }
    $adbArgs += @("-s", $serial)
}

& $adbPath @adbArgs reverse "tcp:$port" "tcp:$port"
Write-Host "ADB reverse enabled. In Quest Browser open: http://127.0.0.1:$port$($config.quest.launchPath)"
