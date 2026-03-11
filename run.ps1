[CmdletBinding()]
param (
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$AllArgs
)

$ErrorActionPreference = "Stop"
$ScriptDir = $PSScriptRoot
$ProjectRoot = $ScriptDir

# Default values
$Target = "gkNextRenderer.exe"
$Preset = $null
$BinDir = $null
$PresentMode = @()
$Scene = @()
$List = $false
$DryRun = $false
$ExtraArgs = @()

$PresetOverridden = $false
$BinOverridden = $false

# Manual Argument Parsing
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
        "^--target$" {
            if ($Value) { $Target = $Value } else { $Target = $AllArgs[++$i] }
        }
        "^--preset$" {
            if ($Value) { $Preset = $Value } else { $Preset = $AllArgs[++$i] }
            $PresetOverridden = $true
        }
        "^--bin-dir$" {
            if ($Value) { $BinDir = $Value } else { $BinDir = $AllArgs[++$i] }
            $BinOverridden = $true
        }
        "^--present-mode$" {
            if ($Value) { $PresentMode += $Value } else { $PresentMode += $AllArgs[++$i] }
        }
        "^--scene$" {
            if ($Value) { $Scene += $Value } else { $Scene += $AllArgs[++$i] }
        }
        "^--list$" { $List = $true }
        "^--dry-run$" { $DryRun = $true }
        "^(-h|--help)$" {
            Write-Host "Usage: run.bat [options]"
            Write-Host "Options:"
            Write-Host "  --target NAME         Executable to launch (default: gkNextRenderer.exe)"
            Write-Host "  --preset NAME         CMake Preset name [REQUIRED]"
            Write-Host "  --bin-dir PATH        Explicit bin directory"
            Write-Host "  --present-mode VALUE  Append --present-mode=VALUE"
            Write-Host "  --scene PATH          Append --load-scene=PATH"
            Write-Host "  --list                List entries in the bin directory"
            Write-Host "  --dry-run             Print the command without running"
            Write-Host "  -h, --help            Show this help"
            exit 0
        }
        "^--$" {
            # Consuming rest of args
            $i++
            while ($i -lt $AllArgs.Count) {
                $ExtraArgs += $AllArgs[$i]
                $i++
            }
            break # break switch to break while? No, break switch.
            # We need to break outer loop.
        }
        default {
            # Treat unknown as extra arg (or warn?)
            # run.sh says: "Additional arguments after -- are passed through as-is."
            # but also "Unknown argument: $1" -> warn logic in build.sh?
            # run.bat logic: "set args=!args! %~1"
            # So implicit extra args.
            $ExtraArgs += $Arg
        }
    }
    
    # Break outer loop if we hit --
    if ($Key -eq "--") { break }
    
    $i++
}

if ([string]::IsNullOrWhiteSpace($Preset) -and [string]::IsNullOrWhiteSpace($BinDir) -and ($Preset -ne "android")) {
    Write-Host "[run] Error: No preset specified." -ForegroundColor Red
    Write-Host "You must explicitly specify a preset using --preset <name>."
    Write-Host "[run] Available configure presets (build them first with build.bat):" -ForegroundColor Cyan
    cmake --list-presets=configure
    exit 1
}

# ... (Rest of logic remains same, just ensure variables are used correctly)

# Handle Android
if ($Preset -eq "android") {
    $AndroidDir = Join-Path $ProjectRoot "android"
    if (-not (Test-Path $AndroidDir)) {
        Write-Error "Android project directory not found: $AndroidDir"
        exit 1
    }
    if ($List) {
        Write-Error "--list is not supported for android platform"
        exit 1
    }
    if ($ExtraArgs) {
        Write-Warning "Ignoring extra arguments for android platform: $ExtraArgs"
    }

    $Cmd = "gradlew.bat"
    $ArgsList = @("installAndLaunch")
    
    Write-Host "Working dir: $AndroidDir"
    Write-Host "Command: $Cmd $ArgsList"

    if ($DryRun) { exit 0 }

    Push-Location $AndroidDir
    try {
        if ($IsWindows) {
            Start-Process -FilePath "cmd.exe" -ArgumentList "/c $Cmd installAndLaunch" -Wait -NoNewWindow
            if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        } else {
             Start-Process -FilePath "./gradlew" -ArgumentList "installAndLaunch" -Wait -NoNewWindow
        }
    } finally {
        Pop-Location
    }
    exit 0
}

# Resolve Bin Dir
$ResolvedBin = $null

# Priority 1: Explicit --bin-dir
if (-not [string]::IsNullOrWhiteSpace($BinDir)) {
    $ResolvedBin = $BinDir
}

# Priority 2: New CMake Preset location
if ([string]::IsNullOrWhiteSpace($ResolvedBin)) {
    $CheckPath = Join-Path $ProjectRoot "out/build/$Preset/bin"
    if (Test-Path $CheckPath) {
        $ResolvedBin = $CheckPath
    }
}

if ([string]::IsNullOrWhiteSpace($ResolvedBin)) {
    Write-Error "Bin directory not found. Have you built the project for preset '$Preset'?"
    Write-Error "Expected: out/build/$Preset/bin"
    exit 1
}

if (-not (Test-Path $ResolvedBin)) {
    Write-Error "Bin directory does not exist: $ResolvedBin"
    exit 1
}

$ResolvedBin = (Resolve-Path -Path $ResolvedBin).Path

if ($List) {
    Write-Host "Entries in ${ResolvedBin}:"
    Get-ChildItem -Path $ResolvedBin | Select-Object -ExpandProperty Name
    exit 0
}

# Resolve Executable
$ExePath = Join-Path $ResolvedBin $Target
if (-not (Test-Path $ExePath)) {
    if (Test-Path "$ExePath.exe") {
        $ExePath = "$ExePath.exe"
    } else {
        Write-Error "Executable not found: $ExePath"
        exit 1
    }
}

$ExeName = Split-Path $ExePath -Leaf
$LaunchArgs = @()

if ($PresentMode) {
    foreach ($p in $PresentMode) { $LaunchArgs += "--present-mode=$p" }
}
if ($Scene) {
    foreach ($s in $Scene) { $LaunchArgs += "--load-scene=$s" }
}
$LaunchArgs += $ExtraArgs

Write-Host "Command: $ExePath $LaunchArgs"

if ($DryRun) { exit 0 }

& $ExePath $LaunchArgs
exit $LASTEXITCODE
