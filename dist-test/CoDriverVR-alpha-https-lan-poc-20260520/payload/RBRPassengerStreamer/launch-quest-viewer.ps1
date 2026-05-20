param(
    [string]$Serial,
    [string]$LaunchPath,
    [string]$HostAddress
)

$ErrorActionPreference = "Stop"

$configPath = Join-Path $PSScriptRoot "config.json"
if (-not (Test-Path -LiteralPath $configPath)) {
    $configPath = Join-Path $PSScriptRoot "config.example.json"
}

$config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
$port = [int]$config.server.streamPort
$adbPath = [string]$config.quest.adbPath
if (-not $Serial) {
    if ([string]$config.quest.connectionMode -eq "wifi" -and [string]$config.quest.wifiSerial) {
        $Serial = [string]$config.quest.wifiSerial
    } else {
        $Serial = [string]$config.quest.serial
    }
}
if (-not $LaunchPath) {
    $LaunchPath = [string]$config.quest.launchPath
}
if (-not $LaunchPath.StartsWith("/")) {
    $LaunchPath = "/$LaunchPath"
}

if (-not $adbPath -or -not (Test-Path -LiteralPath $adbPath)) {
    $adbCommand = Get-Command adb -ErrorAction SilentlyContinue
    if (-not $adbCommand) {
        throw "adb was not found. Set quest.adbPath in config.json or add adb.exe to PATH."
    }
    $adbPath = $adbCommand.Source
}

$adbArgs = @()
if ($Serial) {
    if ($Serial -match '^\d{1,3}(\.\d{1,3}){3}:\d+$') {
        & $adbPath connect $Serial | Write-Host
    }
    $adbArgs += @("-s", $Serial)
}

$useReverse = [bool]$config.quest.useAdbReverse
if ($useReverse) {
    & $adbPath @adbArgs reverse "tcp:$port" "tcp:$port"
    $url = "http://127.0.0.1:$port$LaunchPath"
} else {
    if (-not $HostAddress) {
        $HostAddress = (Get-NetIPAddress -AddressFamily IPv4 |
            Where-Object { -not $_.IPAddress.StartsWith("127.") -and $_.PrefixOrigin -ne "WellKnown" } |
            Select-Object -First 1 -ExpandProperty IPAddress)
    }
    if (-not $HostAddress) {
        throw "Could not determine a LAN host address. Pass -HostAddress or enable quest.useAdbReverse."
    }
    $url = "http://$HostAddress`:$port$LaunchPath"
}

& $adbPath @adbArgs shell am start -a android.intent.action.VIEW -d $url
Write-Host "Opened Quest Browser URL: $url"
