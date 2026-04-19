param(
    [string]$Url = $env:OPTIONAL_PAK_URL,
    [string]$Output
)

$ErrorActionPreference = 'Stop'

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Resolve-Path (Join-Path $ScriptDir '..\..')

# TODO: replace with the real COS/CDN URL once uploaded.
$DefaultUrl = 'https://TODO-replace-me.example.com/gkNextRenderer/optional.pak'

if (-not $Url) {
    $Url = $DefaultUrl
}

if ($Url -like '*TODO-replace-me*') {
    Write-Error 'OPTIONAL_PAK_URL is not set and the default URL is a TODO placeholder. Set $env:OPTIONAL_PAK_URL or edit fetch-optional-pak.ps1.'
    exit 1
}

if (-not $Output) {
    $Output = Join-Path $RepoRoot 'assets\paks\optional.pak'
}

$OutputDir = Split-Path -Parent $Output
if (-not (Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
}

Write-Host "Downloading optional.pak from $Url"
$TmpPath = "$Output.part"
Invoke-WebRequest -Uri $Url -OutFile $TmpPath -UseBasicParsing
Move-Item -Force $TmpPath $Output
Write-Host "Saved to $Output"
