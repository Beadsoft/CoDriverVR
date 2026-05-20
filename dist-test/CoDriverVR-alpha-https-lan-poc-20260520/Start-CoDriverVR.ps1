$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$streamer = Join-Path $root "RBRPassengerStreamer"
$logs = Join-Path $root "logs"
New-Item -ItemType Directory -Force -Path $logs | Out-Null

if (!(Get-Command node -ErrorAction SilentlyContinue)) {
    throw "Node.js is required. Install Node.js 20 LTS or newer, then run this script again."
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
    if (Get-NetFirewallRule -DisplayName $Name -ErrorAction SilentlyContinue) {
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
        Write-Warning "Could not add firewall rule '$Name'. If LAN invites do not open on another device, allow Node.js through Windows Firewall or run this script as Administrator once."
    }
}

Ensure-FirewallRule "CoDriverVR Streamer" 7790

Start-Process -FilePath "npm.cmd" `
    -ArgumentList "start" `
    -WorkingDirectory $streamer `
    -WindowStyle Hidden `
    -RedirectStandardOutput (Join-Path $logs "passenger-streamer.out.log") `
    -RedirectStandardError (Join-Path $logs "passenger-streamer.err.log")

Start-Sleep -Seconds 2
Start-Process "http://127.0.0.1:7790/setup.html"
Write-Host "CoDriverVR streamer started: http://127.0.0.1:7790/setup.html"
