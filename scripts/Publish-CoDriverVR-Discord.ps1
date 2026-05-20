param(
    [string]$Version = "preview-$(Get-Date -Format yyyyMMdd)",
    [string]$ZipPath,
    [Parameter(Mandatory = $true)]
    [string]$DownloadUrl,
    [string]$WebhookUrl = $env:CODRIVERVR_DISCORD_WEBHOOK_URL,
    [switch]$Post
)

$ErrorActionPreference = "Stop"

function Get-LatestPackage {
    param([string]$DistDir)

    if (-not (Test-Path -LiteralPath $DistDir)) {
        throw "Distribution folder not found: $DistDir"
    }

    $latest = Get-ChildItem -LiteralPath $DistDir -Filter "CoDriverVR-*.zip" -File |
        Sort-Object LastWriteTimeUtc -Descending |
        Select-Object -First 1

    if (-not $latest) {
        throw "No CoDriverVR zip package found in $DistDir. Run scripts\Build-CoDriverVR-Package.ps1 first."
    }

    return $latest.FullName
}

function Format-FileSize {
    param([long]$Bytes)

    if ($Bytes -ge 1GB) {
        return "{0:N2} GB" -f ($Bytes / 1GB)
    }

    if ($Bytes -ge 1MB) {
        return "{0:N2} MB" -f ($Bytes / 1MB)
    }

    if ($Bytes -ge 1KB) {
        return "{0:N2} KB" -f ($Bytes / 1KB)
    }

    return "$Bytes bytes"
}

function New-DiscordMessage {
    param(
        [string]$Version,
        [string]$PackageName,
        [string]$PackageSize,
        [string]$Sha256,
        [string]$DownloadUrl
    )

    $lines = @(
        "**CoDriverVR $Version**",
        "",
        "Experimental RBR/openRBRVR passenger + roadbook preview build.",
        "",
        "Download: <$DownloadUrl>",
        "Package: ``$PackageName`` ($PackageSize)",
        "SHA256: ``$Sha256``",
        "",
        "**What it does**",
        "- Streams the RBR/openRBRVR companion window to Quest Browser over local WebRTC.",
        "- Sends Quest head pose back to the passenger camera.",
        "- Adds the native VR roadbook/pacenote panel.",
        "- Includes setup controls for passenger position, panel geometry, bitrate, FPS, prediction, and smoothing.",
        "",
        "**Install**",
        "1. Close RBR and extract the zip.",
        "2. Run ``Set-ExecutionPolicy -Scope Process Bypass``.",
        "3. Run ``.\Install-CoDriverVR.ps1 -RbrRoot `"C:\richard burns rally`"``.",
        "4. Start with ``%LOCALAPPDATA%\CoDriverVR\Start-CoDriverVR.ps1``.",
        "5. Open ``http://127.0.0.1:7790/setup.html``.",
        "",
        "**Known limitations**",
        "- Preview build for testers, not a polished public release.",
        "- Quest Browser/window capture still needs manual sharing.",
        "- Camera offset changes require restarting RBR.",
        "- Latency can be tuned but not eliminated yet."
    )

    return ($lines -join "`n")
}

$repo = Resolve-Path (Join-Path $PSScriptRoot "..")
$distDir = Join-Path $repo "dist"

if (-not $ZipPath) {
    $ZipPath = Get-LatestPackage -DistDir $distDir
}

$resolvedZip = Resolve-Path -LiteralPath $ZipPath
$zipItem = Get-Item -LiteralPath $resolvedZip
$sha256 = (Get-FileHash -LiteralPath $resolvedZip -Algorithm SHA256).Hash
$size = Format-FileSize -Bytes $zipItem.Length
$message = New-DiscordMessage -Version $Version -PackageName $zipItem.Name -PackageSize $size -Sha256 $sha256 -DownloadUrl $DownloadUrl

if ($message.Length -gt 1900) {
    throw "Discord message is $($message.Length) characters. Keep it below 1900 to stay under Discord's content limit."
}

$previewPath = Join-Path $distDir "discord-post-preview.md"
Set-Content -LiteralPath $previewPath -Value $message -Encoding UTF8

Write-Host "Discord release preview written:"
Write-Host $previewPath
Write-Host ""
Write-Host "Package:"
Write-Host $resolvedZip
Write-Host "SHA256: $sha256"

if (-not $Post) {
    Write-Host ""
    Write-Host "Dry run only. Re-run with -Post to send this message to Discord."
    exit 0
}

if (-not $WebhookUrl) {
    throw "Webhook URL is required for posting. Pass -WebhookUrl or set CODRIVERVR_DISCORD_WEBHOOK_URL."
}

$body = @{
    username = "CoDriverVR Releases"
    content = $message
    allowed_mentions = @{
        parse = @()
    }
} | ConvertTo-Json -Depth 5

Write-Host ""
Write-Host "Posting to configured Discord webhook..."
Invoke-RestMethod -Uri $WebhookUrl -Method Post -ContentType "application/json" -Body $body | Out-Null
Write-Host "Posted CoDriverVR $Version to Discord."
