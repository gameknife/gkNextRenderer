# gnb bootstrap installer (Windows).
#
# Usage:
#   irm https://github.com/gameknife/gkNextEngine/releases/download/paks-latest/gnb-init.ps1 | iex
#   # or download this file, drop into an empty folder, and run:
#   powershell -ExecutionPolicy Bypass -File .\gnb-init.ps1 [-TargetDir gkNextEngine]
#
# What it does:
#   1) Download gnb-windows-amd64.exe from the paks-latest release
#   2) Invoke `gnb init <TargetDir>` to clone gkNextEngine
#   3) Print next-step instructions

param(
    [string]$TargetDir = "gkNextEngine",
    [string]$Repo = "gameknife/gkNextEngine",
    [string]$Tag = "paks-latest"
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$releaseBase = "https://github.com/$Repo/releases/download/$Tag"
$assetName = "gnb-windows-amd64.exe"
$url = "$releaseBase/$assetName"

$tmpDir = Join-Path $env:TEMP ("gnb-init-" + [Guid]::NewGuid().ToString("N").Substring(0, 8))
New-Item -ItemType Directory -Path $tmpDir | Out-Null
try {
    $gnb = Join-Path $tmpDir "gnb.exe"
    Write-Host "[gnb-init] downloading $url"
    Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $gnb

    if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
        Write-Error "[gnb-init] git not found in PATH — install Git for Windows first."
    }

    Write-Host "[gnb-init] cloning gkNextEngine -> $TargetDir"
    & $gnb init $TargetDir
    if ($LASTEXITCODE -ne 0) {
        throw "gnb init failed with exit code $LASTEXITCODE"
    }
}
finally {
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $tmpDir
}
