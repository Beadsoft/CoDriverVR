param(
    [string]$RbrRoot = "C:\richard burns rally",
    [string]$InstallDir = "$env:LOCALAPPDATA\CoDriverVR",
    [ValidateSet("Prompt", "LocalOnly", "InternetEnabled")]
    [string]$InternetMode = "Prompt",
    [string]$RoomServerUrl = "",
    [switch]$InstallDxvk
)

$ErrorActionPreference = "Stop"

function Backup-IfExists($Path) {
    if (Test-Path $Path) {
        $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
        Copy-Item -LiteralPath $Path -Destination "$Path.bak-$stamp" -Force
    }
}

function Upsert-OpenRbrVrConfig($Path) {
    if (Test-Path $Path) {
        $text = Get-Content -LiteralPath $Path -Raw
    } else {
        $text = ""
    }
    if ($text -notmatch "(?m)^runtime\s*=") {
        $text = "runtime = 'steamvr'`r`n$text"
    }
    if ($text -match "(?m)^swapEyes\s*=") {
        $text = $text -replace "(?m)^swapEyes\s*=.*$", "swapEyes = false"
    } else {
        $text = $text -replace "(?m)^runtime\s*=\s*'steamvr'\s*$", "runtime = 'steamvr'`r`nswapEyes = false"
    }

    if ($text -notmatch "(?m)^\[PassengerVR\]") {
        $text += @"

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
    } else {
        $text = $text -replace "(?ms)(^\[PassengerVR\].*?enabled\s*=\s*)false", '$1true'
    }

    if ($text -notmatch "(?m)^\[RoadbookVR\]") {
        $text += @"

[RoadbookVR]
enabled = true
rbrRoot = '$RbrRoot'
source = 'ngpMyPacenotes'
lockHand = 'left'
pageHand = 'right'
driverVisible = true
passengerVisible = true
panelWidthMeters = 0.55
panelHeightMeters = 0.38
panelOffset = [0.04, 0.04, 0.12]
panelTiltDegrees = [-18.0, 0.0, 0.0]
notesPerPage = 12
fallbackPose = 'head'
"@
    } else {
        $text = $text -replace "(?ms)(^\[RoadbookVR\].*?enabled\s*=\s*)false", '$1true'
    }

    Set-Content -LiteralPath $Path -Value $text.TrimEnd() -Encoding UTF8
}

function Add-JsonPropertyIfMissing($Object, $Name, $Value) {
    if (-not ($Object.PSObject.Properties.Name -contains $Name)) {
        $Object | Add-Member -NotePropertyName $Name -NotePropertyValue $Value
    }
}

function Set-StreamerInternetConfig($Path, $Mode, $Url) {
    if (!(Test-Path $Path)) {
        return
    }

    if ($Mode -eq "Prompt") {
        $choice = Read-Host "Enable internet passenger rooms? Enter Y for internet, anything else for local-only"
        $Mode = if ($choice -match "^(y|yes)$") { "InternetEnabled" } else { "LocalOnly" }
    }

    if ($Mode -eq "InternetEnabled" -and [string]::IsNullOrWhiteSpace($Url)) {
        $Url = Read-Host "Room server URL"
    }

    $config = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    Add-JsonPropertyIfMissing $config "internet" ([pscustomobject]@{})
    Add-JsonPropertyIfMissing $config.internet "enabled" $false
    Add-JsonPropertyIfMissing $config.internet "roomServerUrl" ""
    Add-JsonPropertyIfMissing $config.internet "requirePublicUrl" $true
    $config.internet.enabled = ($Mode -eq "InternetEnabled" -and -not [string]::IsNullOrWhiteSpace($Url))
    if (-not [string]::IsNullOrWhiteSpace($Url)) {
        $config.internet.roomServerUrl = $Url.TrimEnd("/")
    }
    $config.internet.requirePublicUrl = $true

    Add-JsonPropertyIfMissing $config "lan" ([pscustomobject]@{})
    Add-JsonPropertyIfMissing $config.lan "enabled" $true
    Add-JsonPropertyIfMissing $config.lan "roomServerPort" 7790
    Add-JsonPropertyIfMissing $config.lan "preferAddress" "auto"
    $config.lan.enabled = $true
    $config.lan.roomServerPort = 7790
    if ([string]::IsNullOrWhiteSpace([string]$config.lan.preferAddress)) {
        $config.lan.preferAddress = "auto"
    }

    Add-JsonPropertyIfMissing $config "secureLan" ([pscustomobject]@{})
    Add-JsonPropertyIfMissing $config.secureLan "enabled" $true
    Add-JsonPropertyIfMissing $config.secureLan "tunnelUrl" ""
    Add-JsonPropertyIfMissing $config.secureLan "cloudflaredPath" "bin\cloudflared.exe"
    Add-JsonPropertyIfMissing $config.secureLan "autoStartTunnel" $true
    $config.secureLan.enabled = $true
    $config.secureLan.autoStartTunnel = $true

    Add-JsonPropertyIfMissing $config "driverRoom" ([pscustomobject]@{})
    Add-JsonPropertyIfMissing $config.driverRoom "enabled" $true
    Add-JsonPropertyIfMissing $config.driverRoom "autoCreateOnStreamerStart" $false
    Add-JsonPropertyIfMissing $config.driverRoom "defaultMode" "secure-lan"
    Add-JsonPropertyIfMissing $config.driverRoom "captureTarget" "auto-rbr-companion"
    Add-JsonPropertyIfMissing $config.driverRoom "sharePageMode" "quest-share-sheet"
    if ([string]::IsNullOrWhiteSpace([string]$config.driverRoom.defaultMode) -or $config.driverRoom.defaultMode -eq "lan") {
        $config.driverRoom.defaultMode = "secure-lan"
    }

    Add-JsonPropertyIfMissing $config "quest" ([pscustomobject]@{})
    Add-JsonPropertyIfMissing $config.quest "connectionMode" "wifi"
    Add-JsonPropertyIfMissing $config.quest "useAdbReverse" $false
    if ([string]::IsNullOrWhiteSpace([string]$config.quest.connectionMode)) {
        $config.quest.connectionMode = "wifi"
    }

    Add-JsonPropertyIfMissing $config "handTracking" ([pscustomobject]@{})
    Add-JsonPropertyIfMissing $config.handTracking "enabled" $true
    Add-JsonPropertyIfMissing $config.handTracking "pinchThresholdMeters" 0.035
    Add-JsonPropertyIfMissing $config.handTracking "swipeThresholdMeters" 0.14
    Add-JsonPropertyIfMissing $config.handTracking "debounceMs" 450

    Set-Content -LiteralPath $Path -Value (($config | ConvertTo-Json -Depth 20) + "`n") -Encoding UTF8
}

function Test-IsAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Add-FirewallRule($Name, $Port) {
    New-NetFirewallRule `
        -DisplayName $Name `
        -Direction Inbound `
        -Action Allow `
        -Protocol TCP `
        -LocalPort $Port `
        -Profile Private `
        -ErrorAction Stop | Out-Null
}

function Ensure-FirewallRule($Name, $Port) {
    if (!(Get-Command New-NetFirewallRule -ErrorAction SilentlyContinue)) {
        return
    }
    $existing = Get-NetFirewallRule -DisplayName $Name -ErrorAction SilentlyContinue
    if ($existing) {
        return
    }
    try {
        if (Test-IsAdministrator) {
            Add-FirewallRule $Name $Port
        } else {
            $escapedName = $Name.Replace("'", "''")
            $command = "New-NetFirewallRule -DisplayName '$escapedName' -Direction Inbound -Action Allow -Protocol TCP -LocalPort $Port -Profile Private"
            Start-Process -FilePath "powershell.exe" `
                -ArgumentList @("-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", $command) `
                -Verb RunAs `
                -Wait `
                -WindowStyle Hidden
        }
        Write-Host "Firewall rule added: $Name TCP $Port"
    } catch {
        Write-Warning "Could not add firewall rule '$Name'. Run PowerShell as Administrator or allow Node.js when Windows prompts."
    }
}

$root = Split-Path -Parent $PSScriptRoot
$payload = Join-Path $root "payload"
$plugins = Join-Path $RbrRoot "Plugins"

if (!(Test-Path $RbrRoot)) {
    throw "RBR root not found: $RbrRoot"
}
if (!(Test-Path $plugins)) {
    throw "RBR Plugins folder not found: $plugins"
}
if (Get-Process | Where-Object { $_.ProcessName -match "RichardBurnsRally|richardburnsrally|RBR" }) {
    throw "RBR is running. Close RBR before installing CoDriverVR."
}

New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null

Backup-IfExists (Join-Path $plugins "OpenRBRVR.dll")
Backup-IfExists (Join-Path $plugins "CoDriverVR.dll")
Backup-IfExists (Join-Path $plugins "openRBRVR.toml")
Copy-Item -LiteralPath (Join-Path $payload "Plugins\OpenRBRVR.dll") -Destination (Join-Path $plugins "OpenRBRVR.dll") -Force
Copy-Item -LiteralPath (Join-Path $payload "Plugins\CoDriverVR.dll") -Destination (Join-Path $plugins "CoDriverVR.dll") -Force
Copy-Item -LiteralPath (Join-Path $payload "Plugins\openRBRVR") -Destination $plugins -Recurse -Force
Upsert-OpenRbrVrConfig (Join-Path $plugins "openRBRVR.toml")

if ($InstallDxvk) {
    Backup-IfExists (Join-Path $RbrRoot "d3d9.dll")
    Copy-Item -LiteralPath (Join-Path $payload "RBRRoot\d3d9.dll") -Destination (Join-Path $RbrRoot "d3d9.dll") -Force
    Copy-Item -LiteralPath (Join-Path $payload "RBRRoot\d3d9.dll") -Destination (Join-Path $RbrRoot "d3d9.dll.codrivervr") -Force
}

Copy-Item -LiteralPath (Join-Path $payload "RBRPassengerStreamer") -Destination $InstallDir -Recurse -Force
Copy-Item -LiteralPath (Join-Path $root "Start-CoDriverVR.ps1") -Destination $InstallDir -Force
Set-StreamerInternetConfig (Join-Path $InstallDir "RBRPassengerStreamer\config.json") $InternetMode $RoomServerUrl
Ensure-FirewallRule "CoDriverVR Streamer" 7790

Write-Host ""
Write-Host "CoDriverVR installed."
Write-Host "Streamer folder: $InstallDir\RBRPassengerStreamer"
Write-Host "Start server:    $InstallDir\Start-CoDriverVR.ps1"
Write-Host "Setup page:      http://127.0.0.1:7790/setup.html"
Write-Host "Internet page:   http://127.0.0.1:7790/internet-driver.html"
Write-Host ""
Write-Host "Launch RBR normally after starting the streamer."
