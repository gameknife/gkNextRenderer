[CmdletBinding()]
param(
    [switch]$All,
    [switch]$Ldraw,
    [switch]$Optional,
    [switch]$Sfx,
    [switch]$Ffmpeg,
    [switch]$Sdl,
    [switch]$DryRun,
    [string]$Title,
    [string]$Notes,
    [switch]$Help
)

$ErrorActionPreference = 'Stop'

$ScriptDir = $PSScriptRoot
$RepoRoot = Resolve-Path (Join-Path $ScriptDir '..')

$Repo = if ($env:PAKS_REPO)        { $env:PAKS_REPO }        else { 'gameknife/gkNextEngine' }
$Tag  = if ($env:PAKS_RELEASE_TAG) { $env:PAKS_RELEASE_TAG } else { 'paks-latest' }

$DefaultTitle = "Optional Assets ($Tag)"
$DefaultNotes = @"
Optional binary assets consumed by scripts/fetch-paks.ps1.

Groups:
- ldraw.pak         - LDraw parts library used by BrickPlayer
- optional.pak      - extra sample assets for the main renderer / Editor
- sfx (mp3/wav)     - background music & sound effects
- ffmpeg.exe        - Windows-only helper used by MagicaLego packaging
- SDL3-*.aar        - SDL3 Android archive used by android/ gradle build

Re-uploaded by scripts/publish-paks.ps1.
"@

# Manifest of optional assets. Source paths are relative to the repo root.
$Assets = @(
    @{ Id = 'ldraw';    Name = 'ldraw.pak';        Src = 'assets/paks/ldraw.pak' }
    @{ Id = 'optional'; Name = 'optional.pak';     Src = 'assets/paks/optional.pak' }
    @{ Id = 'sfx';      Name = 'bgm.mp3';          Src = 'assets/sfx/bgm.mp3' }
    @{ Id = 'sfx';      Name = 'bgm2.mp3';         Src = 'assets/sfx/bgm2.mp3' }
    @{ Id = 'sfx';      Name = 'put.mp3';          Src = 'assets/sfx/put.mp3' }
    @{ Id = 'sfx';      Name = 'put1.wav';         Src = 'assets/sfx/put1.wav' }
    @{ Id = 'sfx';      Name = 'put2.wav';         Src = 'assets/sfx/put2.wav' }
    @{ Id = 'sfx';      Name = 'put3.wav';         Src = 'assets/sfx/put3.wav' }
    @{ Id = 'ffmpeg';   Name = 'ffmpeg.exe';       Src = 'src/ThirdParty/ffmpeg/bin/ffmpeg.exe' }
    @{ Id = 'sdl';      Name = 'SDL3-3.2.22.aar';  Src = 'android/app/libs/SDL3-3.2.22.aar' }
)

function Show-Usage {
    Write-Host @"
Usage: publish-paks.ps1 [-All] [-Ldraw] [-Optional] [-Sfx] [-Ffmpeg] [-Sdl] [-DryRun] [-Title TXT] [-Notes TXT]

Creates the release tag if it doesn't exist, then uploads (or replaces) the
selected assets. Defaults to -All when no selector is passed.

Selectors (any combination):
  -All        Publish every group below.
  -Ldraw      ldraw.pak
  -Optional   optional.pak
  -Sfx        sfx mp3/wav files
  -Ffmpeg     ffmpeg.exe
  -Sdl        SDL3 Android archive

Other:
  -Title      Override the release title (only used on creation).
  -Notes      Override the release notes body (only used on creation).
  -DryRun     Print what would happen without touching the remote.
  -Help       Show this help.

Environment overrides:
  PAKS_REPO         GitHub repo (default: gameknife/gkNextEngine)
  PAKS_RELEASE_TAG  Release tag (default: paks-latest)
"@
}

if ($Help) { Show-Usage; exit 0 }

$AnySelector = ($All -or $Ldraw -or $Optional -or $Sfx -or $Ffmpeg -or $Sdl)
if (-not $AnySelector) { $All = $true }
if ($All) { $Ldraw = $true; $Optional = $true; $Sfx = $true; $Ffmpeg = $true; $Sdl = $true }

if (-not $Title) { $Title = $DefaultTitle }
if (-not $Notes) { $Notes = $DefaultNotes }

function Test-Wanted {
    param([string]$Id)
    switch ($Id) {
        'ldraw'    { return $Ldraw }
        'optional' { return $Optional }
        'sfx'      { return $Sfx }
        'ffmpeg'   { return $Ffmpeg }
        'sdl'      { return $Sdl }
        default    { return $false }
    }
}

if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
    Write-Error 'gh (GitHub CLI) is required. Install from https://cli.github.com/'
    exit 1
}

$prevPref = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
& gh auth status *> $null
$authExit = $LASTEXITCODE
$ErrorActionPreference = $prevPref
if ($authExit -ne 0) {
    Write-Error "gh is not authenticated. Run 'gh auth login' first."
    exit 1
}

$Selected = @()
$missing = $false
foreach ($a in $Assets) {
    if (-not (Test-Wanted -Id $a.Id)) { continue }
    $abs = Join-Path $RepoRoot $a.Src
    if (-not (Test-Path $abs)) {
        Write-Error "Missing local asset: $($a.Src)"
        $missing = $true
        continue
    }
    $Selected += $abs
}

if ($missing) {
    Write-Error 'Bring the missing files in place first (build paks, copy SDL aar, etc.).'
    exit 1
}

if ($Selected.Count -eq 0) {
    Write-Error 'Nothing selected to publish.'
    exit 1
}

function Invoke-OrEcho {
    param([string[]]$Cmd)
    if ($DryRun) {
        Write-Host "[dry-run] $($Cmd -join ' ')"
    } else {
        & $Cmd[0] @($Cmd[1..($Cmd.Length-1)])
        if ($LASTEXITCODE -ne 0) {
            throw "Command failed: $($Cmd -join ' ')"
        }
    }
}

Write-Host "[paks] Repo: $Repo"
Write-Host "[paks] Tag:  $Tag"
Write-Host "[paks] Files:"
foreach ($a in $Selected) {
    $size = (Get-Item $a).Length
    Write-Host "         $a ($size bytes)"
}

$prevPref = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
& gh release view $Tag --repo $Repo *> $null
$viewExit = $LASTEXITCODE
$ErrorActionPreference = $prevPref
if ($viewExit -eq 0) {
    Write-Host "[paks] Release $Tag already exists - uploading with --clobber"
    $cmd = @('gh', 'release', 'upload', $Tag) + $Selected + @('--repo', $Repo, '--clobber')
    Invoke-OrEcho -Cmd $cmd
} else {
    Write-Host "[paks] Release $Tag not found - creating"
    $cmd = @('gh', 'release', 'create', $Tag) + $Selected + @(
        '--repo', $Repo,
        '--title', $Title,
        '--notes', $Notes,
        '--latest=false'
    )
    Invoke-OrEcho -Cmd $cmd
}

Write-Host '[paks] Done.'
