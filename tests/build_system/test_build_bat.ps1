$ErrorActionPreference = "Stop"

# --- Configuration ---
$ScriptDir = $PSScriptRoot
$ProjectRoot = Resolve-Path "$ScriptDir\..\.."
$BuildBat = Join-Path $ProjectRoot "build.bat"
$TempDir = Join-Path $env:TEMP "gkNextBuildTest_$(Get-Random)"
$MockBin = Join-Path $TempDir "bin"

# --- Setup ---
Write-Host "Setting up test environment in $TempDir..." -ForegroundColor Gray
New-Item -ItemType Directory -Path $MockBin -Force | Out-Null
$env:PATH = "$MockBin;$env:PATH"

# Mock 'cmake' to capture arguments
$MockCmake = Join-Path $MockBin "cmake.bat"
"@echo off`necho MOCK_CMAKE_ARGS: %*" | Set-Content $MockCmake

# Mock 'gradlew' (for Android test)
$MockAndroidDir = Join-Path $ProjectRoot "android" 
# Note: We won't actually run gradle in the android dir because build.ps1 pushes location.
# But build.ps1 checks for existence of 'android' folder. 
# We'll just rely on mocking the command execution if possible, 
# but build.ps1 executes ./gradlew, which requires the file to exist.
# For this test, we might skip full android execution or mock the file in the real path carefully?
# Ideally, we don't want to modify the repo. 
# Let's focus on CLI args parsing first.

# Mock .vcpkg structure so Ensure-Vcpkg passes
$VcpkgDir = Join-Path $ProjectRoot ".vcpkg/scripts/buildsystems"
if (-not (Test-Path $VcpkgDir)) {
    # If it doesn't exist in repo (it should), we might need to fake it or skip.
    # The repo context says it exists.
}

# --- Helpers ---
$Global:FailedCount = 0
$Global:PassedCount = 0

function Assert-OutputContains {
    param(
        [string]$Command,
        [string]$Expected,
        [string]$TestName
    )
    
    Write-Host -NoNewline "Test: $TestName... "
    
    # Run command and capture output (both stdout and stderr)
    # We use cmd /c to run the .bat properly
    $Output = Invoke-Expression "cmd /c `"$Command`" 2>&1"
    
    # Convert Output array to single string for regex match
    $OutputStr = $Output -join "`n"

    if ($OutputStr -match [regex]::Escape($Expected)) {
        Write-Host "PASSED" -ForegroundColor Green
        $Global:PassedCount++
    } else {
        Write-Host "FAILED" -ForegroundColor Red
        Write-Host "  Expected to contain: $Expected"
        Write-Host "  Actual Output:"
        Write-Host $OutputStr -ForegroundColor DarkGray
        $Global:FailedCount++
    }
}

try {
    Write-Host "Running build.bat CLI compliance tests..." -ForegroundColor Cyan

    # 1. Help
    Assert-OutputContains "$BuildBat --help" "Usage: build.bat" "Display help with --help"
    Assert-OutputContains "$BuildBat -h" "Usage: build.bat" "Display help with -h"

    # 2. Preset
    Assert-OutputContains "$BuildBat --preset my-preset" "MOCK_CMAKE_ARGS: --preset my-preset" "Set preset via --preset"

    # 3. Config
    Assert-OutputContains "$BuildBat --config Release" "--config Release" "Set config via --config"

    # 4. Target
    Assert-OutputContains "$BuildBat --target my-app" "--target my-app" "Set build target via --target"

    # 5. Clean
    # Need to fake a build dir to trigger clean log
    $FakeBuildDir = Join-Path $ProjectRoot "out/build/windows-dev"
    if (-not (Test-Path $FakeBuildDir)) { New-Item -ItemType Directory -Path $FakeBuildDir -Force | Out-Null }
    
    Assert-OutputContains "$BuildBat --clean" "Cleaning build for preset: windows-dev" "Handle --clean"
    
    # Cleanup Fake Dir if we created it strictly for test? 
    # Better leave it or let git clean handle it.

}
finally {
    # Cleanup
    Remove-Item -Path $TempDir -Recurse -Force -ErrorAction SilentlyContinue
    # Note: We cannot easily restore $env:PATH for the parent shell, but this process ends anyway.
}

Write-Host "---------------------------------------"
Write-Host "Tests Passed: $Global:PassedCount"
Write-Host "Tests Failed: $Global:FailedCount"

if ($Global:FailedCount -gt 0) {
    exit 1
}
exit 0
