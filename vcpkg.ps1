[CmdletBinding()]
param (
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$AllArgs
)

$ErrorActionPreference = "Stop"
$ScriptDir = $PSScriptRoot
$ProjectRoot = $ScriptDir
$DefaultVcpkgRoot = Join-Path $ProjectRoot ".vcpkg"
$VcpkgDefaultBinaryCache = Join-Path $ProjectRoot ".vcpkg_bincache"

# Configuration
$VcpkgGitRef = "2025.10.17"
$Update = $false

function Show-Usage {
    Write-Host "Usage: vcpkg.bat [options]"
    Write-Host "Options:"
    Write-Host "  --update     Force git pull on the vcpkg repository."
    Write-Host "  -h, --help   Show this help."
}

# Manual Argument Parsing
foreach ($Arg in $AllArgs) {
    switch ($Arg) {
        "--update" { $Update = $true }
        "-h" { Show-Usage; exit 0 }
        "--help" { Show-Usage; exit 0 }
        default {
            Write-Warning "Unknown argument: $Arg"
        }
    }
}

# Environment Setup
if (-not (Test-Path $VcpkgDefaultBinaryCache)) {
    New-Item -ItemType Directory -Path $VcpkgDefaultBinaryCache -Force | Out-Null
}

$VcpkgRoot = $env:VCPKG_ROOT
if ([string]::IsNullOrWhiteSpace($VcpkgRoot)) {
    $VcpkgRoot = $DefaultVcpkgRoot
}
$VcpkgExe = Join-Path $VcpkgRoot "vcpkg.exe"

function Write-Log { param([string]$Msg) Write-Host "[vcpkg] $Msg" -ForegroundColor Cyan }
function Write-Warn { param([string]$Msg) Write-Host "[vcpkg] Warning: $Msg" -ForegroundColor Yellow }

# --- Ensure Repo ---
if (-not (Test-Path "$VcpkgRoot\.git")) {
    Write-Log "Cloning vcpkg into $VcpkgRoot..."
    git clone https://github.com/microsoft/vcpkg "$VcpkgRoot"
    if ($LASTEXITCODE -ne 0) { throw "Git clone failed." }
}

if (-not $Update) {
    # Ensure fixed commit
    Push-Location $VcpkgRoot
    try {
        git fetch origin --tags --force
        git -c advice.detachedHead=false checkout --force "$VcpkgGitRef"
        git reset --hard "$VcpkgGitRef"
    } finally {
        Pop-Location
    }
} else {
    # Update to latest
    Write-Log "Updating vcpkg in $VcpkgRoot..."
    Push-Location $VcpkgRoot
    try {
        git checkout master 2>$null 
        if ($LASTEXITCODE -ne 0) { git checkout -b master origin/master }
        
        git pull --ff-only
        if ($LASTEXITCODE -ne 0) { Write-Warn "Failed to update vcpkg repo." }
    } finally {
        Pop-Location
    }
}

# --- Ensure Bootstrap ---
if (-not (Test-Path $VcpkgExe)) {
    Write-Log "Bootstrapping vcpkg..."
    Push-Location $VcpkgRoot
    try {
        Start-Process -FilePath "cmd.exe" -ArgumentList "/c bootstrap-vcpkg.bat -disableMetrics" -Wait -NoNewWindow
        if ($LASTEXITCODE -ne 0) { throw "Bootstrap failed." }
    } finally {
        Pop-Location
    }
}

Write-Log "Done. If using custom paths, reuse VCPKG_ROOT=$VcpkgRoot."
exit 0