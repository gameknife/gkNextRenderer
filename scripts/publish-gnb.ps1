param(
    [string]$Repo = "gameknife/gkNextEngine",
    [string]$Tag = "paks-latest",
    [string[]]$Platforms = @("windows-amd64", "linux-amd64", "macos-arm64", "macos-amd64"),
    [string]$Go = "",
    [string]$Version = "",
    [switch]$DryRun,
    [switch]$SkipLocalCache
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

function Resolve-Go
{
    param([string]$RequestedGo)

    if ($RequestedGo -ne "")
    {
        return (Resolve-Path $RequestedGo).Path
    }

    $goCommand = Get-Command go -ErrorAction SilentlyContinue
    if ($null -ne $goCommand)
    {
        return $goCommand.Source
    }

    if ($env:ProgramFiles)
    {
        $programFilesGo = Join-Path $env:ProgramFiles "Go\bin\go.exe"
        if (Test-Path $programFilesGo)
        {
            return $programFilesGo
        }
    }

    throw "Cannot find go. Install Go, add it to PATH, or pass -Go <path-to-go.exe>."
}

function Get-PlatformSpec
{
    param([string]$Platform)

    switch ($Platform)
    {
        "windows-amd64"
        {
            $cacheDir = Join-Path (Join-Path (Join-Path $repoRoot "tools") "gnb-bin") "windows-amd64"
            return @{
                GOOS = "windows"
                GOARCH = "amd64"
                AssetName = "gnb-windows-amd64.exe"
                LocalCachePath = Join-Path $cacheDir "gnb.exe"
                LocalVersionPath = Join-Path $cacheDir "gnb-version.txt"
            }
        }
        "linux-amd64"
        {
            return @{
                GOOS = "linux"
                GOARCH = "amd64"
                AssetName = "gnb-linux-amd64"
                LocalCachePath = ""
                LocalVersionPath = ""
            }
        }
        "macos-arm64"
        {
            return @{
                GOOS = "darwin"
                GOARCH = "arm64"
                AssetName = "gnb-macos-arm64"
                LocalCachePath = ""
                LocalVersionPath = ""
            }
        }
        "macos-amd64"
        {
            return @{
                GOOS = "darwin"
                GOARCH = "amd64"
                AssetName = "gnb-macos-amd64"
                LocalCachePath = ""
                LocalVersionPath = ""
            }
        }
    }

    throw "Unsupported platform: $Platform"
}

function Resolve-Version
{
    param([string]$RequestedVersion, [string]$RepoRoot)

    if ($RequestedVersion -ne "")
    {
        return $RequestedVersion
    }

    if ($env:GITHUB_SHA)
    {
        return $env:GITHUB_SHA
    }

    $gitCommand = Get-Command git -ErrorAction SilentlyContinue
    if ($null -ne $gitCommand)
    {
        $sha = & $gitCommand.Source -C $RepoRoot rev-parse HEAD 2>$null
        if ($LASTEXITCODE -eq 0 -and $sha)
        {
            return $sha.Trim()
        }
    }

    return [DateTime]::UtcNow.ToString("yyyyMMddHHmmss")
}

function Get-Ldflags
{
    param([string]$BuildVersion)

    return "-s -w -X main.version=$BuildVersion"
}

function Ensure-Release
{
    param([string]$GhExe, [string]$RepoName, [string]$ReleaseTag)

    & $GhExe release view $ReleaseTag --repo $RepoName *> $null
    if ($LASTEXITCODE -eq 0)
    {
        return
    }

    Write-Host "Creating release: $RepoName@$ReleaseTag"
    $body = @{
        tag_name = $ReleaseTag
        name = "Optional Assets ($ReleaseTag)"
        body = "Bootstrap binaries and optional assets consumed by gnb."
        draft = $false
        prerelease = $false
        make_latest = "false"
    } | ConvertTo-Json -Compress
    $bodyFile = [System.IO.Path]::GetTempFileName()
    try
    {
        Set-Content -Path $bodyFile -Value $body
        & $GhExe api "repos/$RepoName/releases" --method POST --input $bodyFile
        if ($LASTEXITCODE -ne 0)
        {
            throw "failed to create release: $RepoName@$ReleaseTag"
        }
    }
    finally
    {
        Remove-Item -Force -ErrorAction SilentlyContinue $bodyFile
    }
}

$scriptDir = Split-Path -Parent $PSCommandPath
$repoRoot = (Resolve-Path (Join-Path $scriptDir "..")).Path
$gnbSourceDir = Join-Path (Join-Path $repoRoot "tools") "gnb"
$distDir = Join-Path (Join-Path (Join-Path $repoRoot "out") "dist") "gnb"
$versionValue = Resolve-Version $Version $repoRoot
$ldflags = Get-Ldflags $versionValue
$versionAssetName = "gnb-version.txt"
$versionAssetPath = Join-Path $distDir $versionAssetName

if ($Tag -eq "")
{
    throw "-Tag is required."
}

if (!(Test-Path (Join-Path $gnbSourceDir "go.mod")))
{
    throw "Cannot find tools\gnb\go.mod from repo root: $repoRoot"
}

New-Item -ItemType Directory -Force -Path $distDir | Out-Null
$goExe = Resolve-Go $Go
$builtAssets = @()

foreach ($platform in $Platforms)
{
    $spec = Get-PlatformSpec $platform
    $assetName = $spec.AssetName
    $assetPath = Join-Path $distDir $assetName
    $downloadUrl = "https://github.com/$Repo/releases/download/$Tag/$assetName"

    Write-Host "Building $assetName with $goExe"
    $oldGoos = $env:GOOS
    $oldGoarch = $env:GOARCH
    $oldCgo = $env:CGO_ENABLED
    $pushed = $false
    try
    {
        $env:GOOS = $spec.GOOS
        $env:GOARCH = $spec.GOARCH
        $env:CGO_ENABLED = "0"
        Push-Location $gnbSourceDir
        $pushed = $true
        & $goExe build -trimpath -ldflags $ldflags -o $assetPath ./cmd/gnb
        if ($LASTEXITCODE -ne 0)
        {
            throw "go build failed with exit code $LASTEXITCODE"
        }
    }
    finally
    {
        if ($pushed)
        {
            Pop-Location
        }
        $env:GOOS = $oldGoos
        $env:GOARCH = $oldGoarch
        $env:CGO_ENABLED = $oldCgo
    }

    if (!$SkipLocalCache -and $spec.LocalCachePath -ne "")
    {
        $localCachePath = $spec.LocalCachePath
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $localCachePath) | Out-Null
        Copy-Item -Force $assetPath $localCachePath
        Write-Host "Updated local cache: $localCachePath"
        if ($spec.LocalVersionPath -ne "")
        {
            Set-Content -Path $spec.LocalVersionPath -Value $versionValue
            Write-Host "Updated local cache version: $($spec.LocalVersionPath)"
        }
    }

    $assetInfo = Get-Item $assetPath
    Write-Host ("Built asset: {0} ({1} bytes)" -f $assetInfo.FullName, $assetInfo.Length)
    $builtAssets += @{
        Name = $assetName
        Path = $assetPath
        DownloadUrl = $downloadUrl
    }
}

Set-Content -Path $versionAssetPath -Value $versionValue
$builtAssets += @{
    Name = $versionAssetName
    Path = $versionAssetPath
    DownloadUrl = "https://github.com/$Repo/releases/download/$Tag/$versionAssetName"
}

if ($DryRun)
{
    foreach ($asset in $builtAssets)
    {
        Write-Host "Dry run: would upload to $($asset.DownloadUrl)"
    }
    exit 0
}

$ghCommand = Get-Command gh -ErrorAction SilentlyContinue
if ($null -eq $ghCommand)
{
    throw "GitHub CLI `gh` is required. Install it and run `gh auth login` first."
}

if (-not $env:GH_TOKEN -and -not $env:GITHUB_TOKEN)
{
    & $ghCommand.Source auth status
    if ($LASTEXITCODE -ne 0)
    {
        throw "GitHub CLI is not authenticated. Run `gh auth login` first."
    }
}

Write-Host "Resolving release: $Repo@$Tag"
Ensure-Release $ghCommand.Source $Repo $Tag

$releaseJson = & $ghCommand.Source api "repos/$Repo/releases/tags/$Tag"
if ($LASTEXITCODE -ne 0)
{
    throw "failed to resolve release: $Repo@$Tag"
}
$release = $releaseJson | ConvertFrom-Json

foreach ($asset in $builtAssets)
{
    $starterAssets = $release.assets | Where-Object { $_.name -eq $asset.Name -and $_.state -eq "starter" }
    foreach ($starterAsset in $starterAssets)
    {
        Write-Host "Deleting incomplete asset: $($asset.Name)"
        & $ghCommand.Source api -X DELETE "repos/$Repo/releases/assets/$($starterAsset.id)"
        if ($LASTEXITCODE -ne 0)
        {
            throw "failed to delete incomplete asset: $($asset.Name)"
        }
    }

    Write-Host "Uploading $($asset.Name)"
    & $ghCommand.Source release upload $Tag $asset.Path --repo $Repo --clobber
    if ($LASTEXITCODE -ne 0)
    {
        throw "gh release upload failed for $($asset.Name)"
    }
    Write-Host "Published: $($asset.DownloadUrl)"
}
