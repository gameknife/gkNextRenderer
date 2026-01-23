<#
.SYNOPSIS
    Build script for gkNextRenderer v2 (Windows).
    Wraps CMake Presets and handles dependency checks.

.DESCRIPTION
    Standardizes the build process using CMake Presets.
    Allows pass-through arguments to CMake.

.PARAMETER Preset
    The CMake preset to use. Required.

.PARAMETER Clean
    Clean the build directory before building.

.PARAMETER Android
    Switch to the Android Gradle build.

.PARAMETER CMakeArgs
    Catches all remaining arguments and passes them to CMake.

.EXAMPLE
    .\build.ps1 --preset full-windows-dev
    .\build.ps1 --preset default-windows-dev -- -DGK_ENABLE_AVIF=ON
    .\build.ps1 --clean
    .\build.ps1 --android
#>

[CmdletBinding(SupportsShouldProcess=$true)]
param (
    [Parameter(ValueFromRemainingArguments=$true)]
    [string[]]$AllArgs
)

$ErrorActionPreference = "Stop"
$ScriptDir = $PSScriptRoot
$ProjectRoot = $ScriptDir

# --- Defaults ---
$Preset = $null
$Clean = $false
$Android = $false
$CMakeArgs = @()

# --- Argument Parsing ---
$i = 0
while ($i -lt $AllArgs.Count) {
    $Arg = $AllArgs[$i]
    
    # Handle --option=value
    $Key = $Arg
    $Value = $null
    if ($Arg -match "^([^=]+)=(.*)$") {
        $Key = $matches[1]
        $Value = $matches[2]
    }

    switch -Regex ($Key) {
        "^--preset$" {
            if ($Value) { $Preset = $Value } else { $Preset = $AllArgs[++$i] }
        }
        "^--clean$" {
            $Clean = $true
        }
        "^--android$" {
            $Android = $true
        }
        "^(-h|--help)$" {
            Write-Host "Usage: build.ps1 [options] [-- <cmake_args>...]"
            Write-Host "Options:"
            Write-Host "  --preset <name>  CMake preset to use [REQUIRED]"
            Write-Host "  --clean          Clean build directory before building"
            Write-Host "  --android        Build for Android"
            Write-Host "  -h, --help       Show this help"
            Write-Host ""
            Write-Host "Examples:"
            Write-Host "  build.ps1 --preset default-windows"
            Write-Host "  build.ps1 --preset full-windows -- -DGK_ENABLE_AVIF=ON"
            exit 0
        }
        "^--$" {
            # Everything after -- is a CMake arg
            $i++
            while ($i -lt $AllArgs.Count) {
                $CMakeArgs += $AllArgs[$i]
                $i++
            }
            break
        }
        default {
            # Treat unknown arguments as CMake args (or warn?)
            # Following build.sh pattern, we treat them as pass-through or error?
            # build.sh treats unknown as cmake arg if not handled.
            $CMakeArgs += $Arg
        }
    }
    $i++
}

# --- Validation ---

if ([string]::IsNullOrWhiteSpace($Preset) -and -not $Android) {
    Write-Host "[build] Error: No preset specified. You must explicitly specify a preset." -ForegroundColor Red
    Write-Host "[build] Available configure presets:" -ForegroundColor Cyan
    cmake --list-presets=configure
    exit 1
}

# --- Helper Functions ---

function Write-Log {
    param([string]$Message)
    Write-Host "[build] $Message" -ForegroundColor Cyan
}

function Ensure-Vcpkg {
    $VcpkgToolchain = Join-Path $ProjectRoot ".vcpkg/scripts/buildsystems/vcpkg.cmake"
    if (-not (Test-Path $VcpkgToolchain)) {
        Write-Log "vcpkg toolchain not found. Bootstrapping..."
        & (Join-Path $ProjectRoot "vcpkg.bat") "windows"
    }
}

function Ensure-TSC {
    $TscTarget = Join-Path $ProjectRoot "tools/tsc/tsc.exe"
    if (-not (Test-Path $TscTarget)) {
        Write-Log "TSC compiler not found. Fetching..."
        & (Join-Path $ProjectRoot "tools/fetch_tsc.bat")
    }
}

# --- Main Build Logic ---

function Build-Native {
    param (
        [string]$Preset,
        [string[]]$ExtraArgs
    )

    Ensure-Vcpkg
    Ensure-TSC

    if ($Clean) {
        $BuildDir = Join-Path $ProjectRoot "out/build/$Preset"
        if (Test-Path $BuildDir) {
            Write-Log "Cleaning build for preset: $Preset..."
            Remove-Item -Path $BuildDir -Recurse -Force
        }
    }

    Write-Log "Configuring preset: $Preset with extra args: $($ExtraArgs -join ' ')"
    cmake --preset $Preset $ExtraArgs
    if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed." }

    Write-Log "Building preset: $Preset"
    # Filter for args that are relevant to the build command
    $buildSpecificArgs = @()
    for ($i = 0; $i -lt $ExtraArgs.Length; $i++) {
        if ($ExtraArgs[$i] -in @("--target", "--config", "-j", "--verbose")) {
            $buildSpecificArgs += $ExtraArgs[$i]
            if ($i + 1 -lt $ExtraArgs.Length -and -not ($ExtraArgs[$i+1].StartsWith("-"))) {
                 $buildSpecificArgs += $ExtraArgs[$i+1]
            }
        }
    }
    
    cmake --build --preset $Preset $buildSpecificArgs
    if ($LASTEXITCODE -ne 0) { throw "CMake build failed." }
}

function Build-Android {
    Write-Log "Building for Android..."
    Ensure-TSC
    
    Push-Location (Join-Path $ProjectRoot "android")
    try {
        ./gradlew.bat build
        if ($LASTEXITCODE -ne 0) { throw "Android build failed." }
    }
    finally {
        Pop-Location
    }
}


# --- Main Execution ---

$Global:StopWatch = [System.Diagnostics.Stopwatch]::StartNew()

try {
    if ($Android) {
        Build-Android
    } else {
        Build-Native -Preset $Preset -ExtraArgs $CMakeArgs
    }
    
    $Global:StopWatch.Stop()
    Write-Log "--------------------------------------------------"
    Write-Log "Build Finished Successfully!"
    Write-Log "  Preset:      $Preset"
    Write-Log "  Total Time:  $($Global:StopWatch.Elapsed.TotalSeconds.ToString("N2"))s"
    Write-Log "--------------------------------------------------"
}
catch {
    Write-Host "[build] Error: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}