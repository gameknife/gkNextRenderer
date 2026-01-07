$ErrorActionPreference = "Stop"

# --- Configuration ---
$ScriptDir = $PSScriptRoot
$ProjectRoot = Resolve-Path "$ScriptDir\..\.."
$RunBat = Join-Path $ProjectRoot "run.bat"
$TempDir = Join-Path $env:TEMP "gkNextRunTest_$(Get-Random)"
$MockBin = Join-Path $TempDir "out\build\windows-dev\bin"

# --- Setup ---
Write-Host "Setting up test environment in $TempDir..." -ForegroundColor Gray

# Mock executable
New-Item -ItemType Directory -Path $MockBin -Force | Out-Null
$MockExe = Join-Path $MockBin "gkNextRenderer.exe"
# Create a dummy exe (bat wrapper in disguise for execution test?)
# Actually, run.bat calls it. If we make it a .bat, we need to name it .exe for run.bat logic?
# run.bat checks if exist %target% or %target%.exe.
# But `call !cmd!` might fail if it's not executable.
# Let's create a bat file named gkNextRenderer.bat and target it, 
# or just test --dry-run for execution path logic.
# Testing --dry-run is safer and sufficient for CLI parsing logic.

# Create a dummy file for existence check
New-Item -ItemType File -Path $MockExe -Force | Out-Null

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
    
    # Use & operator to execute directly, handling quoting better
    # But for redirects we need cmd /c or similar wrapping in PS
    # Let's construct the argument list for cmd /c
    
    $ProcessInfo = New-Object System.Diagnostics.ProcessStartInfo
    $ProcessInfo.FileName = "cmd.exe"
    $ProcessInfo.Arguments = "/c `"$Command`""
    $ProcessInfo.RedirectStandardOutput = $true
    $ProcessInfo.RedirectStandardError = $true
    $ProcessInfo.UseShellExecute = $false
    $ProcessInfo.CreateNoWindow = $true
    
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
    Write-Host "Running run.bat CLI compliance tests..." -ForegroundColor Cyan

    # We need to run run.bat from the temp dir so it finds the relative out/build path
    # OR we mock the root dir. run.bat uses %~dp0 as root_dir. 
    # So we can't easily mock the build dir location unless we use --bin-dir or --preset relative to the real root.
    # The real root doesn't have the build artifacts.
    # We will use --bin-dir for most tests, or --dry-run which skips execution.
    
    # 1. Help
    Assert-OutputContains "$RunBat --help" "Usage: run.bat" "Display help"

    # 2. Dry Run & Target
    # We point bin-dir to our mock bin so it finds the exe
    Assert-OutputContains "$RunBat --dry-run --bin-dir `"$MockBin`" --target gkNextRenderer.exe" "Command: .\gkNextRenderer.exe" "Dry run with explicit bin-dir"

    # 3. Arguments Passing
    Assert-OutputContains "$RunBat --dry-run --bin-dir `"$MockBin`" --scene my.gltf" "Command: .\gkNextRenderer.exe --load-scene=my.gltf" "Argument parsing --scene"
    
    Assert-OutputContains "$RunBat --dry-run --bin-dir `"$MockBin`" --present-mode 1" "Command: .\gkNextRenderer.exe --present-mode=1" "Argument parsing --present-mode"

    # 4. Equals sign parsing
    Assert-OutputContains "$RunBat --dry-run --bin-dir=`"$MockBin`"" "Working dir: $MockBin" "Equals sign in --bin-dir"
    Assert-OutputContains "$RunBat --dry-run --bin-dir `"$MockBin`" --scene=test.glb" "--load-scene=test.glb" "Equals sign in --scene"

    # 5. List
    Assert-OutputContains "$RunBat --list --bin-dir `"$MockBin`"" "gkNextRenderer.exe" "List command"

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
