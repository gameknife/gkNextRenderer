<#
.SYNOPSIS
    Build script for gkNextRenderer (Windows).
    Wraps CMake Presets and handles dependency checks.

.DESCRIPTION
    Standardizes the build process using CMake Presets.
    Automatically handles vcpkg bootstrapping and tool fetching.

.PARAMETER Target
    Build target. Defaults to 'windows-dev' (uses CMake Preset).
    Options: windows-dev, android

.PARAMETER Clean
    Clean the build directory before building.

.EXAMPLE
    .\build.ps1
    .\build.ps1 -Target android
    .\build.ps1 -Clean
#>

[CmdletBinding()]
param (
    [Parameter(Position = 0)]
    [ValidateSet("windows-dev", "android")]
    [string]$Target = "windows-dev",

    [Parameter()]
    [switch]$Clean = $false
)

$ErrorActionPreference = "Stop"
$ScriptDir = $PSScriptRoot
$ProjectRoot = $ScriptDir
$VcpkgRoot = Join-Path $ProjectRoot ".vcpkg"
$VcpkgToolchain = Join-Path $VcpkgRoot "scripts/buildsystems/vcpkg.cmake"

function Write-Log {
    param([string]$Message)
    Write-Host "[Build] $Message" -ForegroundColor Cyan
}

function Write-ErrorLog {
    param([string]$Message)
    Write-Host "[Error] $Message" -ForegroundColor Red
}

function Test-Command {
    param([string]$Name)
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "$Name is not installed or not in PATH."
    }
}

function Ensure-Vcpkg {
    if (-not (Test-Path $VcpkgToolchain)) {
        Write-Log "vcpkg toolchain not found. Bootstrapping..."
        $VcpkgBat = Join-Path $ProjectRoot "vcpkg.bat"
        if (Test-Path $VcpkgBat) {
            Start-Process -FilePath $VcpkgBat -ArgumentList "windows" -Wait -NoNewWindow -PassThru | ForEach-Object {
                if ($_.ExitCode -ne 0) { throw "vcpkg bootstrapping failed." }
            }
        } else {
            throw "vcpkg.bat not found."
        }
    }
}

function Ensure-TSC {
    $TscTarget = Join-Path $ProjectRoot "tools/tsc/tsc.exe"
    if (-not (Test-Path $TscTarget)) {
        Write-Log "TSC compiler not found. Fetching..."
        $FetchTsc = Join-Path $ProjectRoot "tools/fetch_tsc.bat"
        if (Test-Path $FetchTsc) {
            Start-Process -FilePath $FetchTsc -Wait -NoNewWindow -PassThru | ForEach-Object {
                if ($_.ExitCode -ne 0) { throw "Failed to fetch TSC." }
            }
        } else {
            Write-Warning "tools/fetch_tsc.bat not found. TypeScript compilation might fail."
        }
    }
}

function Build-Native {
    param([string]$Preset)

    Test-Command "cmake"
    Ensure-Vcpkg
    Ensure-TSC

    if ($Clean) {
        $BuildDir = Join-Path $ProjectRoot "out/build/$Preset"
        if (Test-Path $BuildDir) {
            Write-Log "Cleaning $BuildDir..."
            Remove-Item -Path $BuildDir -Recurse -Force
        }
    }

    Write-Log "Configuring ($Preset)..."
    cmake --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw "Configuration failed." }

    Write-Log "Building ($Preset)..."
    cmake --build --preset $Preset
    
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed."
    }
}

function Build-Android {
    Write-Log "Building for Android..."
    Ensure-TSC
    
    $AndroidDir = Join-Path $ProjectRoot "android"
    Push-Location $AndroidDir
    try {
        if ($IsWindows) {
            ./gradlew.bat build
        } else {
            ./gradlew build
        }
        
        if ($LASTEXITCODE -ne 0) { throw "Android build failed." }
    }
    finally {
        Pop-Location
    }
}

# --- Main Execution ---

$StopWatch = [System.Diagnostics.Stopwatch]::StartNew()

try {
    switch ($Target) {
        "android" { Build-Android }
        default   { Build-Native -Preset $Target }
    }
    
    $StopWatch.Stop()
    Write-Log "Build completed successfully in $($StopWatch.Elapsed.TotalSeconds.ToString("N2")) seconds."
}
catch {
    Write-ErrorLog $_.Exception.Message
    exit 1
}
