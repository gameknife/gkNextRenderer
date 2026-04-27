[CmdletBinding()]
param(
    [switch]$All,
    [switch]$Ldraw,
    [switch]$Optional,
    [switch]$Sfx,
    [switch]$Ffmpeg,
    [switch]$Force,
    [switch]$Help
)

$ErrorActionPreference = 'Stop'

$ScriptDir = $PSScriptRoot
$RepoRoot = Resolve-Path (Join-Path $ScriptDir '..')

$Repo    = if ($env:PAKS_REPO)        { $env:PAKS_REPO }        else { 'gameknife/gkNextEngine' }
$Tag     = if ($env:PAKS_RELEASE_TAG) { $env:PAKS_RELEASE_TAG } else { 'paks-latest' }
$BaseUrl = if ($env:PAKS_BASE_URL)    { $env:PAKS_BASE_URL }    else { "https://github.com/$Repo/releases/download/$Tag" }

# Manifest of optional assets. Dest paths are relative to the repo root.
# The SDL3 Android archive is no longer fetched here; android/app/build.gradle
# pulls it directly from libsdl-org's official release on first build.
$Assets = @(
    @{ Id = 'ldraw';    Name = 'ldraw.pak';        Dest = 'assets/paks/ldraw.pak' }
    @{ Id = 'optional'; Name = 'optional.pak';     Dest = 'assets/paks/optional.pak' }
    @{ Id = 'sfx';      Name = 'bgm.mp3';          Dest = 'assets/sfx/bgm.mp3' }
    @{ Id = 'sfx';      Name = 'bgm2.mp3';         Dest = 'assets/sfx/bgm2.mp3' }
    @{ Id = 'sfx';      Name = 'put.mp3';          Dest = 'assets/sfx/put.mp3' }
    @{ Id = 'sfx';      Name = 'put1.wav';         Dest = 'assets/sfx/put1.wav' }
    @{ Id = 'sfx';      Name = 'put2.wav';         Dest = 'assets/sfx/put2.wav' }
    @{ Id = 'sfx';      Name = 'put3.wav';         Dest = 'assets/sfx/put3.wav' }
    @{ Id = 'ffmpeg';   Name = 'ffmpeg.exe';       Dest = 'src/ThirdParty/ffmpeg/bin/ffmpeg.exe' }
)

function Show-Usage {
    Write-Host @"
Usage: fetch-paks.ps1 [-All] [-Ldraw] [-Optional] [-Sfx] [-Ffmpeg] [-Force]

Selectors (any combination; defaults to -All when none given):
  -All        Fetch every group below.
  -Ldraw      ldraw.pak             -> assets/paks/
  -Optional   optional.pak          -> assets/paks/
  -Sfx        background music + sound effects -> assets/sfx/
  -Ffmpeg     ffmpeg.exe            -> src/ThirdParty/ffmpeg/bin/   (Windows only)

Other:
  -Force      Re-download even if the file already exists.
  -Help       Show this help.

Environment overrides:
  PAKS_REPO         GitHub repo (default: gameknife/gkNextEngine)
  PAKS_RELEASE_TAG  Release tag (default: paks-latest)
  PAKS_BASE_URL     Full base URL override (skips GitHub release lookup)
"@
}

if ($Help) { Show-Usage; exit 0 }

$AnySelector = ($All -or $Ldraw -or $Optional -or $Sfx -or $Ffmpeg)
if (-not $AnySelector) { $All = $true }
if ($All) { $Ldraw = $true; $Optional = $true; $Sfx = $true; $Ffmpeg = $true }

function Test-Wanted {
    param([string]$Id)
    switch ($Id) {
        'ldraw'    { return $Ldraw }
        'optional' { return $Optional }
        'sfx'      { return $Sfx }
        'ffmpeg'   { return $Ffmpeg }
        default    { return $false }
    }
}

function Get-OneAsset {
    param([string]$Name, [string]$DestRel)

    $Url = "$BaseUrl/$Name"
    $Out = Join-Path $RepoRoot $DestRel

    if ((Test-Path $Out) -and -not $Force) {
        Write-Host "[paks] $DestRel already exists (use -Force to re-download)"
        return
    }

    $OutDir = Split-Path -Parent $Out
    if (-not (Test-Path $OutDir)) {
        New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
    }

    Write-Host "[paks] Downloading $Name -> $DestRel"
    $Tmp = "$Out.part"
    try {
        $ProgressPreference = 'Continue'
        Invoke-WebRequest -Uri $Url -OutFile $Tmp -UseBasicParsing
    } catch {
        if (Test-Path $Tmp) { Remove-Item -Force $Tmp }
        throw
    }
    Move-Item -Force $Tmp $Out
}

foreach ($a in $Assets) {
    if (Test-Wanted -Id $a.Id) {
        Get-OneAsset -Name $a.Name -DestRel $a.Dest
    }
}

Write-Host '[paks] Done.'
