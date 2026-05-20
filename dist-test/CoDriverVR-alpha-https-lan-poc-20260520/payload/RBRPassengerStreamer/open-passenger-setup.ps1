$ErrorActionPreference = "Stop"

$configPath = Join-Path $PSScriptRoot "config.json"
if (-not (Test-Path -LiteralPath $configPath)) {
    $configPath = Join-Path $PSScriptRoot "config.example.json"
}

$config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
$port = [int]$config.server.streamPort
$url = "http://127.0.0.1:$port/setup.html"

Start-Process $url
Write-Host "Opened passenger setup: $url"
