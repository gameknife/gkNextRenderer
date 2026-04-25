[CmdletBinding()]
param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Rest
)

# Backwards-compatible shim. The canonical fetcher now lives at
# scripts/fetch-paks.ps1 and can pull every optional pak (ldraw.pak / optional.pak)
# from the project's GitHub release. See scripts/fetch-paks.ps1 -Help.

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

$Target = Join-Path $RepoRoot 'scripts\fetch-paks.ps1'
& $Target -Optional @Rest
exit $LASTEXITCODE
