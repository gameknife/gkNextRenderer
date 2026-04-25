[CmdletBinding()]
param(
    [switch]$All,
    [switch]$Ldraw,
    [switch]$Optional,
    [switch]$Force,
    [switch]$Help
)

$ErrorActionPreference = 'Stop'

$ScriptDir = $PSScriptRoot
$RepoRoot = Resolve-Path (Join-Path $ScriptDir '..')

$Repo    = if ($env:PAKS_REPO)        { $env:PAKS_REPO }        else { 'gameknife/gkNextEngine' }
$Tag     = if ($env:PAKS_RELEASE_TAG) { $env:PAKS_RELEASE_TAG } else { 'paks-latest' }
$BaseUrl = if ($env:PAKS_BASE_URL)    { $env:PAKS_BASE_URL }    else { "https://github.com/$Repo/releases/download/$Tag" }

$OutputDir = Join-Path $RepoRoot 'assets\paks'

function Show-Usage {
    Write-Host @"
Usage: fetch-paks.ps1 [-All] [-Ldraw] [-Optional] [-Force]

Options:
  -All        Fetch every optional pak (default when no flag is given).
  -Ldraw      Fetch only ldraw.pak.
  -Optional   Fetch only optional.pak.
  -Force      Re-download even if the file already exists.
  -Help       Show this help.

Environment overrides:
  PAKS_REPO         GitHub repo (default: gameknife/gkNextEngine)
  PAKS_RELEASE_TAG  Release tag (default: paks-latest)
  PAKS_BASE_URL     Full base URL override (skips GitHub release lookup)
"@
}

if ($Help) { Show-Usage; exit 0 }

if (-not ($All -or $Ldraw -or $Optional)) {
    $All = $true
}
if ($All) { $Ldraw = $true; $Optional = $true }

if (-not (Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
}

function Get-OnePak {
    param([string]$Name)

    $Url = "$BaseUrl/$Name"
    $Out = Join-Path $OutputDir $Name

    if ((Test-Path $Out) -and -not $Force) {
        Write-Host "[paks] $Name already exists at $Out (use -Force to re-download)"
        return
    }

    Write-Host "[paks] Downloading $Name from $Url"
    $Tmp = "$Out.part"
    try {
        $ProgressPreference = 'Continue'
        Invoke-WebRequest -Uri $Url -OutFile $Tmp -UseBasicParsing
    } catch {
        if (Test-Path $Tmp) { Remove-Item -Force $Tmp }
        throw
    }
    Move-Item -Force $Tmp $Out
    Write-Host "[paks] Saved $Out"
}

if ($Ldraw)    { Get-OnePak -Name 'ldraw.pak' }
if ($Optional) { Get-OnePak -Name 'optional.pak' }
