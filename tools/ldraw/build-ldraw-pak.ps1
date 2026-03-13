param(
    [string]$LibraryDir = "",
    [string]$ShadowDir = "",
    [string]$OutputPak = "",
    [string]$ManifestPath = "",
    [string]$PackLogPath = "",
    [string]$PackagerPath = "",
    [switch]$NoCompress,
    [switch]$KeepStage
)

$ErrorActionPreference = "Stop"

function Resolve-NormalizedPath
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$PathValue
    )

    return [System.IO.Path]::GetFullPath($PathValue)
}

function Find-Packager
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$RepoRoot
    )

    $candidatePaths = @(
        (Join-Path $RepoRoot "out/build/full-windows/bin/Packager.exe"),
        (Join-Path $RepoRoot "out/build/default-windows/bin/Packager.exe"),
        (Join-Path $RepoRoot "out/build/full-mingw/bin/Packager.exe"),
        (Join-Path $RepoRoot "out/build/default-mingw/bin/Packager.exe")
    )

    foreach ($candidatePath in $candidatePaths)
    {
        if (Test-Path $candidatePath)
        {
            return (Resolve-NormalizedPath $candidatePath)
        }
    }

    $discoveredPackager = Get-ChildItem -Path (Join-Path $RepoRoot "out/build") -Recurse -Filter Packager.exe -ErrorAction SilentlyContinue |
        Sort-Object FullName |
        Select-Object -First 1
    if ($null -ne $discoveredPackager)
    {
        return (Resolve-NormalizedPath $discoveredPackager.FullName)
    }

    throw "Packager.exe not found. Build it first, e.g. `cmake --build --preset full-windows --target Packager`."
}

function Sync-Directory
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$SourceDir,
        [Parameter(Mandatory = $true)]
        [string]$TargetDir
    )

    New-Item -ItemType Directory -Force -Path $TargetDir | Out-Null

    & robocopy $SourceDir $TargetDir /MIR /R:1 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8)
    {
        throw "robocopy failed while syncing '$SourceDir' to '$TargetDir' (exit code $LASTEXITCODE)."
    }
}

$repoRoot = Resolve-NormalizedPath (Join-Path $PSScriptRoot "..\\..")
$defaultSourceRoot = Join-Path $PSScriptRoot "source"

if ([string]::IsNullOrWhiteSpace($LibraryDir))
{
    $LibraryDir = Join-Path $defaultSourceRoot "ldraw"
}

if ([string]::IsNullOrWhiteSpace($ShadowDir))
{
    $ShadowDir = Join-Path $defaultSourceRoot "ldrawShadow"
}

if ([string]::IsNullOrWhiteSpace($OutputPak))
{
    $OutputPak = Join-Path $repoRoot "assets/paks/ldraw.pak"
}

if ([string]::IsNullOrWhiteSpace($ManifestPath))
{
    $ManifestPath = Join-Path $PSScriptRoot "output/ldraw_manifest.json"
}

if ([string]::IsNullOrWhiteSpace($PackLogPath))
{
    $PackLogPath = Join-Path $PSScriptRoot "output/ldraw_pack.log"
}

$LibraryDir = Resolve-NormalizedPath $LibraryDir
$ShadowDir = Resolve-NormalizedPath $ShadowDir
$OutputPak = Resolve-NormalizedPath $OutputPak
$ManifestPath = Resolve-NormalizedPath $ManifestPath
$PackLogPath = Resolve-NormalizedPath $PackLogPath

if ([string]::IsNullOrWhiteSpace($PackagerPath))
{
    $PackagerPath = Find-Packager -RepoRoot $repoRoot
}
else
{
    $PackagerPath = Resolve-NormalizedPath $PackagerPath
}

if (-not (Test-Path $LibraryDir))
{
    throw "LDraw library directory not found: $LibraryDir"
}

if (-not (Test-Path $ShadowDir))
{
    throw "LDraw shadow library directory not found: $ShadowDir"
}

$ldConfigPath = Join-Path $LibraryDir "LDConfig.ldr"
if (-not (Test-Path $ldConfigPath))
{
    throw "LDConfig.ldr not found under library directory: $ldConfigPath"
}

$packagerRuntimeRoot = Resolve-NormalizedPath (Join-Path (Split-Path (Split-Path $PackagerPath -Parent) -Parent) ".")
$stageRoot = Join-Path $packagerRuntimeRoot "ldraw_pak_stage"
$stageAssetsRoot = Join-Path $stageRoot "assets"
$stageLibraryDir = Join-Path $stageAssetsRoot "ldraw"
$stageShadowDir = Join-Path $stageAssetsRoot "ldrawShadow"

New-Item -ItemType Directory -Force -Path (Split-Path $OutputPak -Parent) | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path $ManifestPath -Parent) | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path $PackLogPath -Parent) | Out-Null

Sync-Directory -SourceDir $LibraryDir -TargetDir $stageLibraryDir
Sync-Directory -SourceDir $ShadowDir -TargetDir $stageShadowDir

$packagerArgs = @(
    "--out", $OutputPak,
    "--src", "ldraw_pak_stage/assets",
    "--root", "ldraw_pak_stage",
    "--manifest", $ManifestPath
)

if ($NoCompress)
{
    $packagerArgs += "--no-compress"
}

Push-Location (Join-Path $packagerRuntimeRoot "bin")
try
{
    & $PackagerPath @packagerArgs *>&1 | Tee-Object -FilePath $PackLogPath | Out-Null
    if ($LASTEXITCODE -ne 0)
    {
        throw "Packager.exe failed with exit code $LASTEXITCODE. See log: $PackLogPath"
    }
}
finally
{
    Pop-Location
}

if (-not $KeepStage -and (Test-Path $stageRoot))
{
    Remove-Item -Recurse -Force $stageRoot
}

Write-Host "Generated pak: $OutputPak"
Write-Host "Manifest: $ManifestPath"
Write-Host "Pack log: $PackLogPath"
