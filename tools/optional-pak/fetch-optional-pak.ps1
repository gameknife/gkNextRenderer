[CmdletBinding()]
param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Rest
)

# Backwards-compatible shim. The canonical fetcher now lives in gnb.

$ErrorActionPreference = 'Stop'
$ScriptDir = $PSScriptRoot
$RepoRoot = Resolve-Path (Join-Path $ScriptDir '..\..')

# Preserve the legacy OPTIONAL_PAK_URL override by mapping it onto PAKS_BASE_URL.
if ($env:OPTIONAL_PAK_URL -and -not $env:PAKS_BASE_URL) {
    $legacy = $env:OPTIONAL_PAK_URL
    if ($legacy.EndsWith('/optional.pak')) {
        $legacy = $legacy.Substring(0, $legacy.Length - '/optional.pak'.Length)
    }
    $env:PAKS_BASE_URL = $legacy
}

& (Join-Path $RepoRoot 'gnb.bat') paks fetch optional @Rest
exit $LASTEXITCODE
