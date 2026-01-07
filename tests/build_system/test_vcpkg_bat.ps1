$ErrorActionPreference = "Stop"

# --- Configuration ---
$ScriptDir = $PSScriptRoot
$ProjectRoot = Resolve-Path "$ScriptDir\..\.."
$VcpkgBat = Join-Path $ProjectRoot "vcpkg.bat"
$TempDir = Join-Path $env:TEMP "gkNextVcpkgTest_$(Get-Random)"
$MockVcpkgRoot = Join-Path $TempDir ".vcpkg"

# --- Setup ---
Write-Host "Setting up test environment in $TempDir..." -ForegroundColor Gray
New-Item -ItemType Directory -Path $MockVcpkgRoot -Force | Out-Null

# We need to mock git interaction?
# vcpkg.bat calls git. We can mock git via path override.
$MockBin = Join-Path $TempDir "bin"
New-Item -ItemType Directory -Path $MockBin -Force | Out-Null
$env:PATH = "$MockBin;$env:PATH"

# Mock git
$MockGit = Join-Path $MockBin "git.bat"
$MockGitContent = @"
@echo off
set "ARGS=%*"
echo MOCK_GIT_CALL: %ARGS%

REM Handle 'checkout' which might be preceded by -c ...
echo %ARGS% | findstr /C:"checkout" >nul
if %errorlevel% equ 0 (
    echo MOCK_GIT: checkout
    exit /b 0
)

echo %ARGS% | findstr /C:"clone" >nul
if %errorlevel% equ 0 (
    REM We need to extract the path to mkdir, but honestly ensuring it exists is enough for logic flow?
    REM The script uses the 3rd arg for path usually: git clone URL PATH
    REM Let's just pretend we did it.
    echo MOCK_GIT: clone
    exit /b 0
)

echo %ARGS% | findstr /C:"pull" >nul
if %errorlevel% equ 0 (
    echo MOCK_GIT: pull
    exit /b 0
)

exit /b 0
"@
Set-Content $MockGit $MockGitContent

# Mock bootstrap-vcpkg.bat
# It's called inside .vcpkg/
$MockBootstrap = Join-Path $MockVcpkgRoot "bootstrap-vcpkg.bat"
"@echo off`necho MOCK_BOOTSTRAP" | Set-Content $MockBootstrap

# We also need a dummy vcpkg.exe check. vcpkg.bat checks if exists.
# If we don't create it, it calls bootstrap.
# So test case 1: no vcpkg.exe -> calls bootstrap.

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
    
    $ProcessInfo = New-Object System.Diagnostics.ProcessStartInfo
    $ProcessInfo.FileName = "cmd.exe"
    $ProcessInfo.Arguments = "/c `"$Command`""
    $ProcessInfo.RedirectStandardOutput = $true
    $ProcessInfo.RedirectStandardError = $true
    $ProcessInfo.UseShellExecute = $false
    $ProcessInfo.CreateNoWindow = $true
    # Set env var VCPKG_ROOT for the process to avoid modifying real repo
    $ProcessInfo.EnvironmentVariables["VCPKG_ROOT"] = $MockVcpkgRoot
    
    $Process = [System.Diagnostics.Process]::Start($ProcessInfo)
    $Process.WaitForExit()
    
    $OutputStr = $Process.StandardOutput.ReadToEnd() + "`n" + $Process.StandardError.ReadToEnd()

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
    Write-Host "Running vcpkg.bat CLI compliance tests..." -ForegroundColor Cyan

    # 1. Help
    Assert-OutputContains "$VcpkgBat --help" "Usage: vcpkg.bat" "Display help"

    # 2. Bootstrap trigger (vcpkg.exe missing)
    # We fake .git folder so it doesn't try to clone, just update/checkout
    New-Item -ItemType Directory -Path "$MockVcpkgRoot\.git" -Force | Out-Null
    
    Assert-OutputContains "$VcpkgBat" "MOCK_BOOTSTRAP" "Bootstrap execution"
    Assert-OutputContains "$VcpkgBat" "MOCK_GIT: checkout" "Fixed version checkout"

    # 3. Update flag
    # Mocking git pull
    Assert-OutputContains "$VcpkgBat --update" "MOCK_GIT: pull" "Update flag triggers pull"

}
finally {
    Remove-Item -Path $TempDir -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "---------------------------------------"
Write-Host "Tests Passed: $Global:PassedCount"
Write-Host "Tests Failed: $Global:FailedCount"

if ($Global:FailedCount -gt 0) {
    exit 1
}
exit 0
