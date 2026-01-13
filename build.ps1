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
    [string]$Preset = "windows-dev",

    [Parameter()]
    [string]$Config,

    [Parameter()]
    [string]$Target,

    [Parameter()]
    [switch]$Clean = $false,

    [Parameter()]
    [switch]$Android = $false,

    [Parameter()]
    [switch]$Avif = $false,

    [Parameter()]
    [switch]$Dlss = $false,

    [Parameter()]
    [switch]$Oidn = $false
)

$ErrorActionPreference = "Stop"
$ScriptDir = $PSScriptRoot
$ProjectRoot = $ScriptDir
$VcpkgRoot = Join-Path $ProjectRoot ".vcpkg"
$VcpkgToolchain = Join-Path $VcpkgRoot "scripts/buildsystems/vcpkg.cmake"
$VcpkgDefaultBinaryCache = Join-Path $ProjectRoot ".vcpkg_bincache"

$env:VCPKG_ROOT = $VcpkgRoot
$env:VCPKG_BINARY_SOURCES = "clear;files,$VcpkgDefaultBinaryCache,readwrite"

$script:ConfigDuration = 0
$script:BuildDuration = 0
$script:SystemInfo = "Unknown"

function Write-Log {
    param([string]$Message)
    Write-Host "[build] $Message" -ForegroundColor Cyan
}

function Write-ErrorLog {
    param([string]$Message)
    Write-Host "[build] Error: $Message" -ForegroundColor Red
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
            Start-Process -FilePath $FetchTsc -Wait -NoNewWindow -PassThru
            #  | ForEach-Object {
            #     if ($_.ExitCode -ne 0) { throw "Failed to fetch TSC." }
            # }
        } else {
            Write-Warning "tools/fetch_tsc.bat not found. TypeScript compilation might fail."
        }
    }
}

function Ensure-OIDN {
    $OidnTarget = Join-Path $ProjectRoot "src/ThirdParty/oidn/bin/OpenImageDenoise.dll"
    if (-not (Test-Path $OidnTarget)) {
        Write-Log "OIDN binaries not found. Fetching..."
        $FetchOidn = Join-Path $ProjectRoot "tools/fetch_oidn.bat"
        if (Test-Path $FetchOidn) {
            & $FetchOidn
            if ($LASTEXITCODE -ne 0) { throw "Failed to fetch OIDN." }
        } else {
            Write-Warning "tools/fetch_oidn.bat not found. OIDN support might fail."
        }
    }
}

function Ensure-Streamline {
    $StreamlineTarget = Join-Path $ProjectRoot "src/ThirdParty/streamline/lib/x64/sl.interposer.lib"
    if (-not (Test-Path $StreamlineTarget)) {
        Write-Log "Streamline SDK not found. Fetching..."
        $FetchStreamline = Join-Path $ProjectRoot "tools/fetch_streamline.bat"
        if (Test-Path $FetchStreamline) {
            & $FetchStreamline
            if ($LASTEXITCODE -ne 0) { throw "Failed to fetch Streamline SDK." }
        } else {
            Write-Warning "tools/fetch_streamline.bat not found. DLSS support might fail."
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
            Write-Log "Cleaning build for preset: $Preset..."
            Remove-Item -Path $BuildDir -Recurse -Force
        }
    }

    Write-Log "Configuring preset: $Preset"
    $ConfigureArgs = @("--preset", $Preset, "-Wno-dev")
    if ($Avif) {
        $ConfigureArgs += "-DGK_ENABLE_AVIF=ON"
        $ConfigureArgs += "-DVCPKG_MANIFEST_FEATURES=avif"
    } else {
        $ConfigureArgs += "-DGK_ENABLE_AVIF=OFF"
        $ConfigureArgs += "-DVCPKG_MANIFEST_FEATURES="
    }
    
    if ($Dlss) {
        Ensure-Streamline
        $ConfigureArgs += "-DGK_ENABLE_DLSS=ON"
    } else {
        $ConfigureArgs += "-DGK_ENABLE_DLSS=OFF"
    }
    
    if ($Oidn) {
        Ensure-OIDN
        $ConfigureArgs += "-DGK_ENABLE_OIDN=ON"
    } else {
        $ConfigureArgs += "-DGK_ENABLE_OIDN=OFF"
    }
    
    $ConfigStopWatch = [System.Diagnostics.Stopwatch]::StartNew()
    cmake $ConfigureArgs
    $ConfigStopWatch.Stop()
    $script:ConfigDuration = $ConfigStopWatch.Elapsed.TotalSeconds
    
    if ($LASTEXITCODE -ne 0) { throw "Configuration failed." }

    Write-Log "Building preset: $Preset"
    $BuildArgs = @("--build", "--preset", $Preset)
    if ($Config) {
        $BuildArgs += @("--config", $Config)
    }
    if ($Target) {
        $BuildArgs += @("--target", $Target)
    }
    
    # Reduce MSBuild verbosity for Windows presets
    if ($Preset -eq "windows-dev" -or $Preset -eq "windows-base") {
        $BuildArgs += @("--", "/verbosity:minimal", "/consoleloggerparameters:Summary")
    }
    
    $BuildStopWatch = [System.Diagnostics.Stopwatch]::StartNew()
    cmake $BuildArgs
    $BuildStopWatch.Stop()
    $script:BuildDuration = $BuildStopWatch.Elapsed.TotalSeconds
    
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

function Show-SystemInfo {
    try {
        $cpu = Get-CimInstance Win32_Processor | Select-Object -ExpandProperty Name -First 1
        $mem = Get-CimInstance Win32_ComputerSystem | Select-Object -ExpandProperty TotalPhysicalMemory
        $memGb = [math]::Round($mem / 1GB, 1)
        $script:SystemInfo = "$cpu, ${memGb}GB RAM"
    } catch {
        $script:SystemInfo = "Information unavailable"
    }
}

# --- Main Execution ---

$StopWatch = [System.Diagnostics.Stopwatch]::StartNew()

try {
    Show-SystemInfo

    if ($Android) {
        Build-Android
    } else {
        Build-Native -Preset $Preset
    }
    
    $StopWatch.Stop()
    
    Write-Log "--------------------------------------------------"
    Write-Log "Build Statistics:"
    Write-Log "  System:      $($script:SystemInfo)"
    Write-Log "  Preset:      $Preset"
    if ($script:ConfigDuration -gt 0) {
        Write-Log "  Configure:   $($script:ConfigDuration.ToString("N2"))s"
    }
    if ($script:BuildDuration -gt 0) {
        Write-Log "  Build:       $($script:BuildDuration.ToString("N2"))s"
    }
    Write-Log "  Total:       $($StopWatch.Elapsed.TotalSeconds.ToString("N2"))s"
    Write-Log "--------------------------------------------------"
}
catch {
    Write-ErrorLog $_.Exception.Message
    exit 1
}
