param(
    [string]$Version = "preview",
    [string]$OutDir = "$PSScriptRoot\..\dist"
)

$ErrorActionPreference = "Stop"

$repo = Resolve-Path (Join-Path $PSScriptRoot "..")
$stage = Join-Path $OutDir "CoDriverVR-$Version"
$zip = Join-Path $OutDir "CoDriverVR-$Version.zip"

if (Test-Path $stage) {
    Remove-Item -LiteralPath $stage -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $stage | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $stage "payload\Plugins\openRBRVR") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $stage "payload\RBRRoot") | Out-Null

Copy-Item -LiteralPath (Join-Path $repo "installer\Install-CoDriverVR.ps1") -Destination (Join-Path $stage "Install-CoDriverVR.ps1") -Force
Copy-Item -LiteralPath (Join-Path $repo "installer\Start-CoDriverVR.ps1") -Destination (Join-Path $stage "Start-CoDriverVR.ps1") -Force
Copy-Item -LiteralPath (Join-Path $repo "installer\README-SHARE.md") -Destination (Join-Path $stage "README.md") -Force
Copy-Item -LiteralPath (Join-Path $repo "DISCORD_RELEASE_NOTES.md") -Destination (Join-Path $stage "DISCORD_RELEASE_NOTES.md") -Force
Copy-Item -LiteralPath (Join-Path $repo "docs") -Destination (Join-Path $stage "docs") -Recurse -Force

Copy-Item -LiteralPath (Join-Path $repo "openRBRVR\zig-out\bin\openRBRVR.dll") -Destination (Join-Path $stage "payload\Plugins\OpenRBRVR.dll") -Force
Copy-Item -LiteralPath (Join-Path $repo "openRBRVR\zig-out\bin\CoDriverVR.dll") -Destination (Join-Path $stage "payload\Plugins\CoDriverVR.dll") -Force
Copy-Item -LiteralPath (Join-Path $repo "openRBRVR\openRBRVR.toml.sample") -Destination (Join-Path $stage "payload\Plugins\openRBRVR.toml.sample") -Force
Copy-Item -LiteralPath (Join-Path $repo "openRBRVR\multiviewpatcher\multiviewpatcher.dll") -Destination (Join-Path $stage "payload\Plugins\openRBRVR\multiviewpatcher.dll") -Force
Copy-Item -LiteralPath (Join-Path $repo "openRBRVR\thirdparty\obsmirror") -Destination (Join-Path $stage "payload\Plugins\openRBRVR\obsmirror") -Recurse -Force
Copy-Item -LiteralPath (Join-Path $repo "openRBRVR\thirdparty\quad-views-foveated") -Destination (Join-Path $stage "payload\Plugins\openRBRVR\quad-views-foveated") -Recurse -Force
Copy-Item -LiteralPath (Join-Path $repo "openRBRVR\thirdparty\dll\d3d9.dll") -Destination (Join-Path $stage "payload\RBRRoot\d3d9.dll") -Force

Copy-Item -LiteralPath (Join-Path $repo "RBRPassengerStreamer") -Destination (Join-Path $stage "payload\RBRPassengerStreamer") -Recurse -Force
Remove-Item -LiteralPath (Join-Path $stage "payload\RBRPassengerStreamer\node_modules\.cache") -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -Path (Join-Path $stage "payload\RBRPassengerStreamer\*.log") -Force -ErrorAction SilentlyContinue
Copy-Item -LiteralPath (Join-Path $stage "payload\RBRPassengerStreamer\config.example.json") -Destination (Join-Path $stage "payload\RBRPassengerStreamer\config.json") -Force

$cloudflareWorker = Resolve-Path (Join-Path $repo "..\RBR Passenger\CloudflareRoomWorker") -ErrorAction SilentlyContinue
if ($cloudflareWorker) {
    Copy-Item -LiteralPath $cloudflareWorker -Destination (Join-Path $stage "CloudflareRoomWorker") -Recurse -Force
    Remove-Item -LiteralPath (Join-Path $stage "CloudflareRoomWorker\node_modules") -Recurse -Force -ErrorAction SilentlyContinue
}

if (Test-Path $zip) {
    Remove-Item -LiteralPath $zip -Force
}
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip -Force

Write-Host "Created package:"
Write-Host $zip
