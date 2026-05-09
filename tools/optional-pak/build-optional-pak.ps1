param(
    [string]$FileList,
    [string]$OutputPak,
    [string]$Manifest,
    [string]$PackLog,
    [string]$Packager,
    [switch]$NoCompress,
    [switch]$KeepStage
)

$ErrorActionPreference = 'Stop'

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Resolve-Path (Join-Path $ScriptDir '..\..')

if (-not $FileList)  { $FileList  = Join-Path $ScriptDir 'optional-files.txt' }
if (-not $OutputPak) { $OutputPak = Join-Path $RepoRoot 'assets\paks\optional.pak' }
if (-not $Manifest)  { $Manifest  = Join-Path $ScriptDir 'output\optional_manifest.json' }
if (-not $PackLog)   { $PackLog   = Join-Path $ScriptDir 'output\optional_pack.log' }

function Resolve-AbsolutePath($p) {
    if ([System.IO.Path]::IsPathRooted($p)) { return [System.IO.Path]::GetFullPath($p) }
    return [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $p))
}

function Find-Packager {
    $candidates = @(
        'out\build\windows\bin\Packager.exe',
        'out\build\linux\bin\Packager',
        'out\build\macos-arm64\bin\Packager'
    )
    foreach ($c in $candidates) {
        $p = Join-Path $RepoRoot $c
        if (Test-Path $p) { return (Resolve-AbsolutePath $p) }
    }
    $found = Get-ChildItem -Path (Join-Path $RepoRoot 'out\build') -Recurse -Include 'Packager','Packager.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($found) { return $found.FullName }
    throw "Packager binary not found. Build it first (e.g. gnb.bat build Packager)."
}

$FileList  = Resolve-AbsolutePath $FileList
$OutputPak = Resolve-AbsolutePath $OutputPak
$Manifest  = Resolve-AbsolutePath $Manifest
$PackLog   = Resolve-AbsolutePath $PackLog

if (-not (Test-Path $FileList)) {
    throw "File list not found: $FileList"
}

if (-not $Packager) { $Packager = Find-Packager } else { $Packager = Resolve-AbsolutePath $Packager }

$PackagerBinDir = Split-Path -Parent $Packager
$PackagerRuntimeRoot = Resolve-AbsolutePath (Join-Path $PackagerBinDir '..')
$StageRoot = Join-Path $PackagerRuntimeRoot 'optional_pak_stage'

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutputPak) | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Manifest)  | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $PackLog)   | Out-Null

if (Test-Path $StageRoot) { Remove-Item -Recurse -Force $StageRoot }
New-Item -ItemType Directory -Force -Path $StageRoot | Out-Null

$packed = 0
$missing = @()

function Stage-File($relPath) {
    $src = Join-Path $RepoRoot $relPath
    $dst = Join-Path $StageRoot $relPath
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dst) | Out-Null
    Copy-Item -Force $src $dst
    $script:packed++
}

Get-Content $FileList | ForEach-Object {
    $line = $_.Split('#')[0].Trim()
    if (-not $line) { return }

    # Trailing slash marks a directory entry: recurse.
    if ($line.EndsWith('/') -or $line.EndsWith('\')) {
        $dirRel = $line.TrimEnd('/','\')
        $dirAbs = Join-Path $RepoRoot $dirRel
        if (-not (Test-Path $dirAbs -PathType Container)) {
            $missing += $line
            return
        }
        Get-ChildItem -Path $dirAbs -Recurse -File | Where-Object { $_.Name -ne '.DS_Store' } | ForEach-Object {
            $relFromRepo = [System.IO.Path]::GetRelativePath($RepoRoot, $_.FullName).Replace('\','/')
            Stage-File $relFromRepo
        }
        return
    }

    $src = Join-Path $RepoRoot $line
    if (-not (Test-Path $src -PathType Leaf)) {
        $missing += $line
        return
    }
    Stage-File $line
}

if ($packed -eq 0) {
    Write-Error "No source files found. Nothing to pack."
    exit 1
}

Write-Host "Staged $packed file(s) into $StageRoot"
if ($missing.Count -gt 0) {
    Write-Warning "Missing $($missing.Count) file(s) listed in $FileList; they will be skipped:"
    $missing | ForEach-Object { Write-Warning "  - $_" }
}

$packagerArgs = @('--out', $OutputPak, '--src', 'optional_pak_stage', '--root', 'optional_pak_stage', '--manifest', $Manifest)
if ($NoCompress) { $packagerArgs += '--no-compress' }

Push-Location $PackagerBinDir
try {
    & $Packager @packagerArgs *>&1 | Tee-Object -FilePath $PackLog
    $exit = $LASTEXITCODE
} finally {
    Pop-Location
}

if ($exit -ne 0) {
    Write-Error "Packager failed with exit code $exit. See log: $PackLog"
    exit $exit
}

if (-not $KeepStage -and (Test-Path $StageRoot)) {
    Remove-Item -Recurse -Force $StageRoot
}

Write-Host "Generated pak: $OutputPak"
Write-Host "Manifest: $Manifest"
Write-Host "Pack log: $PackLog"
