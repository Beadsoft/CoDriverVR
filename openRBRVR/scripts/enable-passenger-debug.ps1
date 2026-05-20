param(
    [string]$ConfigPath = "C:\richard burns rally\Plugins\openRBRVR.toml"
)

$ErrorActionPreference = "Stop"

$path = (Resolve-Path -LiteralPath $ConfigPath).Path
$content = Get-Content -LiteralPath $path -Raw

if ($content -match "(?m)^debug\s*=") {
    $content = [regex]::Replace($content, "(?m)^debug\s*=.*$", "debug = true")
} else {
    $content = "debug = true`r`n" + $content
}

if ($content -match "(?m)^debugMode\s*=") {
    $content = [regex]::Replace($content, "(?m)^debugMode\s*=.*$", "debugMode = 0")
} else {
    $content = "debugMode = 0`r`n" + $content
}

Set-Content -LiteralPath $path -Value $content -NoNewline
Write-Host "Enabled openRBRVR debug overlay in $path"
