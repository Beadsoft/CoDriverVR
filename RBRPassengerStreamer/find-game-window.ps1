$ErrorActionPreference = "Stop"

$configPath = Join-Path $PSScriptRoot "config.json"
if (-not (Test-Path -LiteralPath $configPath)) {
    $configPath = Join-Path $PSScriptRoot "config.example.json"
}

$config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
$processNames = @($config.game.processNames)
$titleParts = @($config.game.windowTitleContains)

$matches = Get-Process | Where-Object {
    $process = $_
    $nameMatch = $processNames | Where-Object { $process.ProcessName -ieq $_ }
    $titleMatch = $false
    if ($process.MainWindowTitle) {
        $titleMatch = [bool]($titleParts | Where-Object { $process.MainWindowTitle -like "*$_*" })
    }
    $nameMatch -or $titleMatch
} | Select-Object ProcessName, Id, MainWindowTitle, Path

if (-not $matches) {
    Write-Host "No matching game window found for '$($config.game.name)'."
    exit 1
}

$matches | Format-Table -AutoSize
