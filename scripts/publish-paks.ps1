[CmdletBinding()]
param(
    [switch]$All,
    [switch]$Ldraw,
    [switch]$Optional,
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

$DefaultTitle = "Optional Asset Paks ($Tag)"
$DefaultNotes = @"
Optional asset paks consumed by scripts/fetch-paks.ps1.

Files:
- ldraw.pak    — LDraw parts library used by BrickPlayer
- optional.pak — extra sample assets for the main renderer / Editor

Re-uploaded by scripts/publish-paks.ps1.
"@

$PakDir = Join-Path $RepoRoot 'assets\paks'

function Show-Usage {
    Write-Host @"
Usage: publish-paks.ps1 [-All] [-Ldraw] [-Optional] [-DryRun] [-Title TXT] [-Notes TXT]

Creates the release tag if it doesn't exist, then uploads (or replaces) the
selected pak assets. Defaults to -All when no selector is passed.

Options:
  -All        Publish every optional pak (default).
  -Ldraw      Publish only ldraw.pak.
  -Optional   Publish only optional.pak.
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

if (-not ($All -or $Ldraw -or $Optional)) { $All = $true }
if ($All) { $Ldraw = $true; $Optional = $true }

if (-not $Title) { $Title = $DefaultTitle }
if (-not $Notes) { $Notes = $DefaultNotes }

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

$Assets = @()
if ($Ldraw)    { $Assets += Join-Path $PakDir 'ldraw.pak' }
if ($Optional) { $Assets += Join-Path $PakDir 'optional.pak' }

$missing = $false
foreach ($a in $Assets) {
    if (-not (Test-Path $a)) {
        Write-Error "Missing local asset: $a"
        $missing = $true
    }
}
if ($missing) {
    Write-Error 'Build the paks first (see tools/optional-pak/ and tools/ldraw/).'
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
foreach ($a in $Assets) {
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
    $cmd = @('gh', 'release', 'upload', $Tag) + $Assets + @('--repo', $Repo, '--clobber')
    Invoke-OrEcho -Cmd $cmd
} else {
    Write-Host "[paks] Release $Tag not found - creating"
    $cmd = @('gh', 'release', 'create', $Tag) + $Assets + @(
        '--repo', $Repo,
        '--title', $Title,
        '--notes', $Notes,
        '--latest=false'
    )
    Invoke-OrEcho -Cmd $cmd
}

Write-Host '[paks] Done.'
