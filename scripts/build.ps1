<#
.SYNOPSIS
    Build script for gkNextRenderer v2 (Windows).
    Wraps CMake Presets and handles dependency checks.

.DESCRIPTION
    Standardizes the build process using CMake Presets.
    Allows pass-through arguments to CMake.
    Supports incremental configuration (skips configure if already configured).

.PARAMETER Preset
    The CMake preset to use. Required.

.PARAMETER Clean
    Clean the build directory before building.

.PARAMETER Reconfigure
    Force CMake reconfiguration even if already configured.

.PARAMETER Android
    Switch to the Android Gradle build.

.PARAMETER Avif
    Enable AVIF support and the matching vcpkg manifest feature.

.PARAMETER CMakeArgs
    Catches all remaining arguments and passes them to CMake.

.EXAMPLE
    .\build.ps1 --preset full-windows-dev
    .\build.ps1 --preset default-windows --avif
    .\build.ps1 --preset default-windows --reconfigure
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
$ProjectRoot = Split-Path $ScriptDir -Parent

# --- Defaults ---
$Preset = $null
$Clean = $false
$Reconfigure = $false
$Android = $false
$Avif = $false
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
        "^--reconfigure$" {
            $Reconfigure = $true
        }
        "^--android$" {
            $Android = $true
        }
        "^--avif$" {
            $Avif = $true
        }
        "^(-h|--help)$" {
            Write-Host "Usage: build.ps1 [options] [-- <cmake_args>...]"
            Write-Host "Options:"
            Write-Host "  --preset <name>  CMake preset to use [REQUIRED]"
            Write-Host "  --clean          Clean build directory before building"
            Write-Host "  --reconfigure    Force CMake reconfiguration"
            Write-Host "  --android        Build for Android"
            Write-Host "  --avif           Enable AVIF support and install the vcpkg avif feature"
            Write-Host "  -h, --help       Show this help"
            Write-Host ""
            Write-Host "Examples:"
            Write-Host "  build.ps1 --preset default-windows"
            Write-Host "  build.ps1 --preset full-windows --avif"
            Write-Host "  build.ps1 --preset full-windows -- -DENABLE_AVIF=ON"
            Write-Host "  build.ps1 --preset default-windows --reconfigure"
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

function Get-CMakeDefineValue {
    param(
        [string[]]$Args,
        [string]$Name
    )

    foreach ($Arg in $Args) {
        if ($Arg -match "^-D$([regex]::Escape($Name))(?::[^=]+)?=(.*)$") {
            return $matches[1]
        }
    }

    return $null
}

function Merge-ManifestFeatures {
    param([string]$ExistingValue)

    $features = @()
    if (-not [string]::IsNullOrWhiteSpace($ExistingValue)) {
        $features = $ExistingValue -split "[,;]" | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
    }

    if ($features -notcontains "avif") {
        $features += "avif"
    }

    return ($features | Select-Object -Unique) -join ";"
}

function Normalize-AvifArgs {
    param(
        [string[]]$Args,
        [bool]$EnableAvif
    )

    $inputArgs = @($Args)
    $enableAvifValue = Get-CMakeDefineValue -Args $inputArgs -Name "ENABLE_AVIF"
    $avifRequested = $EnableAvif -or ($enableAvifValue -match "^(ON|TRUE|1)$")

    if (-not $avifRequested) {
        return ,$inputArgs
    }

    $manifestFeaturesValue = Get-CMakeDefineValue -Args $inputArgs -Name "VCPKG_MANIFEST_FEATURES"
    $normalizedArgs = [System.Collections.Generic.List[string]]::new()
    foreach ($Arg in $inputArgs) {
        if (
            $Arg -notmatch "^-D$([regex]::Escape("ENABLE_AVIF"))(?::[^=]+)?=" -and
            $Arg -notmatch "^-D$([regex]::Escape("VCPKG_MANIFEST_FEATURES"))(?::[^=]+)?="
        ) {
            $normalizedArgs.Add($Arg)
        }
    }

    $normalizedArgs.Add("-DENABLE_AVIF=ON")
    $normalizedArgs.Add("-DVCPKG_MANIFEST_FEATURES=$(Merge-ManifestFeatures -ExistingValue $manifestFeaturesValue)")

    Write-Log "AVIF enabled: injecting -DENABLE_AVIF=ON and matching vcpkg manifest features."
    return ,$normalizedArgs.ToArray()
}

function Ensure-Vcpkg {
    $VcpkgToolchain = Join-Path $ProjectRoot ".vcpkg/scripts/buildsystems/vcpkg.cmake"
    if (-not (Test-Path $VcpkgToolchain)) {
        Write-Log "vcpkg toolchain not found. Bootstrapping..."
        & (Join-Path $ScriptDir "vcpkg.bat")
    }
}

# --- Main Build Logic ---

function Build-Native {
    param (
        [string]$Preset,
        [string[]]$ExtraArgs,
        [bool]$ForceReconfigure = $false
    )

    Ensure-Vcpkg

    $BuildDir = Join-Path $ProjectRoot "out/build/$Preset"
    $CacheFile = Join-Path $BuildDir "CMakeCache.txt"

    if ($Clean) {
        if (Test-Path $BuildDir) {
            Write-Log "Cleaning build for preset: $Preset..."
            Remove-Item -Path $BuildDir -Recurse -Force
        }
    }

    # Check if reconfiguration is needed
    $NeedsConfigure = $ForceReconfigure -or $Clean -or (-not (Test-Path $CacheFile))

    # Check if any -D arguments are passed (requires reconfigure)
    $HasDefineArgs = $ExtraArgs | Where-Object { $_ -match "^-D" }
    if ($HasDefineArgs) {
        $NeedsConfigure = $true
    }

    if ($NeedsConfigure) {
        Write-Log "Configuring preset: $Preset with extra args: $($ExtraArgs -join ' ')"
        cmake --preset $Preset $ExtraArgs
        if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed." }
    } else {
        Write-Log "Skipping configure (already configured). Use --reconfigure to force."
    }

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
        $NormalizedCMakeArgs = Normalize-AvifArgs -Args $CMakeArgs -EnableAvif $Avif
        Build-Native -Preset $Preset -ExtraArgs $NormalizedCMakeArgs -ForceReconfigure $Reconfigure
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
