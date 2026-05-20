$ErrorActionPreference = "Stop"

Set-Location -LiteralPath $PSScriptRoot

$configPath = Join-Path $PSScriptRoot "config.json"
if (-not (Test-Path -LiteralPath $configPath)) {
    $configPath = Join-Path $PSScriptRoot "config.example.json"
}
$config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
$port = [int]$config.server.streamPort

Write-Host "Configured game target:"
& "$PSScriptRoot\find-game-window.ps1"

Write-Host ""
Write-Host "Starting passenger service. Open /harness.html for tests, /streamer.html for video, /quest.html on the Quest."
Start-Job -ScriptBlock {
    param($Url)
    Start-Sleep -Seconds 2
    Start-Process $Url
} -ArgumentList "http://127.0.0.1:$port/setup.html" | Out-Null
& "C:\Program Files\nodejs\node.exe" server.js
