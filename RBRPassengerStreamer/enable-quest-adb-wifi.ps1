param(
    [string]$UsbSerial,
    [string]$QuestIp,
    [int]$Port,
    [switch]$UpdateConfig
)

$ErrorActionPreference = "Stop"

$configPath = Join-Path $PSScriptRoot "config.json"
if (-not (Test-Path -LiteralPath $configPath)) {
    $configPath = Join-Path $PSScriptRoot "config.example.json"
}

$config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
$adbPath = [string]$config.quest.adbPath
if (-not $Port) {
    $Port = [int]$config.quest.wifiPort
    if (-not $Port) {
        $Port = 5555
    }
}
if (-not $UsbSerial) {
    $UsbSerial = [string]$config.quest.serial
}

if (-not $adbPath -or -not (Test-Path -LiteralPath $adbPath)) {
    $adb = Get-Command adb -ErrorAction SilentlyContinue
    if (-not $adb) {
        throw "adb was not found. Set quest.adbPath in config.json or add adb.exe to PATH."
    }
    $adbPath = $adb.Source
}

$usbArgs = @()
if ($UsbSerial) {
    $usbArgs += @("-s", $UsbSerial)
}

if (-not $QuestIp) {
    $routes = & $adbPath @usbArgs shell ip route
    $routeLine = $routes | Where-Object { $_ -match '\bsrc\s+(\d{1,3}(\.\d{1,3}){3})' } | Select-Object -First 1
    if ($routeLine -match '\bsrc\s+(\d{1,3}(\.\d{1,3}){3})') {
        $QuestIp = $Matches[1]
    }
}

if (-not $QuestIp) {
    throw "Could not determine Quest WiFi IP. Pass -QuestIp, or connect the Quest by USB once so adb shell ip route works."
}

& $adbPath @usbArgs tcpip $Port | Write-Host
Start-Sleep -Seconds 2

$wifiSerial = "$QuestIp`:$Port"
& $adbPath connect $wifiSerial | Write-Host

if ($UpdateConfig) {
    $config.quest.connectionMode = "wifi"
    $config.quest.wifiSerial = $wifiSerial
    $config.quest.wifiPort = $Port
    $config | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $configPath -Encoding UTF8
    Write-Host "Updated config.json to use WiFi ADB target: $wifiSerial"
}

Write-Host "Quest WiFi ADB target: $wifiSerial"
Write-Host "You can now unplug USB and run: .\launch-quest-viewer.ps1"
