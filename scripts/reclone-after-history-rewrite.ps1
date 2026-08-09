param(
    [string]$RepoPath = "",
    [string]$RemoteUrl = "",
    [string]$Branch = "dev",
    [string]$BackupRoot = "",
    [switch]$ApplyPatches,
    [switch]$SkipPatchBackup
)

$ErrorActionPreference = "Stop"

function Resolve-RepoPath {
    param([string]$InputPath)

    if ($InputPath) {
        return (Resolve-Path -LiteralPath $InputPath).Path
    }

    $top = git rev-parse --show-toplevel 2>$null
    if (-not $top) {
        throw "RepoPath was not provided and current directory is not inside a Git repository."
    }
    return (Resolve-Path -LiteralPath $top).Path
}

$repoPath = Resolve-RepoPath $RepoPath
$repoParent = Split-Path -Parent $repoPath
$repoName = Split-Path -Leaf $repoPath
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"

if (-not $BackupRoot) {
    $BackupRoot = Join-Path $repoParent "$repoName.history-rewrite-backups"
}

$backupDir = Join-Path $BackupRoot $stamp
$oldRepoPath = Join-Path $repoParent "$repoName.old-history-$stamp"

if (-not (Test-Path -LiteralPath (Join-Path $repoPath ".git"))) {
    throw "RepoPath does not look like a non-bare Git working tree: $repoPath"
}

if (-not $RemoteUrl) {
    Push-Location -LiteralPath $repoPath
    try {
        $RemoteUrl = git remote get-url origin
        if ($LASTEXITCODE -ne 0 -or -not $RemoteUrl) {
            throw "Failed to read origin URL. Pass -RemoteUrl explicitly."
        }
    } finally {
        Pop-Location
    }
}

Write-Host "Repository: $repoPath"
Write-Host "Remote:     $RemoteUrl"
Write-Host "Branch:     $Branch"
Write-Host "Backup:     $backupDir"
Write-Host "Old clone:  $oldRepoPath"

New-Item -ItemType Directory -Force -Path $backupDir | Out-Null

Push-Location -LiteralPath $repoPath
try {
    git status --short --branch | Set-Content -Encoding utf8 (Join-Path $backupDir "status-before.txt")
    git rev-parse HEAD | Set-Content -Encoding ascii (Join-Path $backupDir "head-before.txt")
    git remote -v | Set-Content -Encoding utf8 (Join-Path $backupDir "remotes-before.txt")

    if (-not $SkipPatchBackup) {
        git diff --binary --output=(Join-Path $backupDir "working-tree.patch")
        git diff --cached --binary --output=(Join-Path $backupDir "index.patch")

        $untracked = @(git ls-files --others --exclude-standard)
        $untracked | Set-Content -Encoding utf8 (Join-Path $backupDir "untracked-files.txt")
        if ($untracked.Count -gt 0) {
            $tempUntrackedRoot = Join-Path $backupDir "untracked"
            New-Item -ItemType Directory -Force -Path $tempUntrackedRoot | Out-Null
            foreach ($relativePath in $untracked) {
                $src = Join-Path $repoPath $relativePath
                $dst = Join-Path $tempUntrackedRoot $relativePath
                New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dst) | Out-Null
                Copy-Item -LiteralPath $src -Destination $dst -Force
            }
            Compress-Archive -Path (Join-Path $tempUntrackedRoot "*") -DestinationPath (Join-Path $backupDir "untracked-files.zip") -Force
            Remove-Item -LiteralPath $tempUntrackedRoot -Recurse -Force
        }
    }
} finally {
    Pop-Location
}

Set-Location -LiteralPath $repoParent

if (Test-Path -LiteralPath $oldRepoPath) {
    throw "Old repo backup path already exists: $oldRepoPath"
}
if (-not (Test-Path -LiteralPath $repoPath)) {
    throw "Repo path disappeared before rename: $repoPath"
}

Rename-Item -LiteralPath $repoPath -NewName (Split-Path -Leaf $oldRepoPath)

git clone --branch $Branch $RemoteUrl $repoPath
if ($LASTEXITCODE -ne 0) {
    Write-Warning "Clone failed. Restoring old repo path."
    if (Test-Path -LiteralPath $repoPath) {
        Remove-Item -LiteralPath $repoPath -Recurse -Force
    }
    Rename-Item -LiteralPath $oldRepoPath -NewName $repoName
    throw "git clone failed"
}

Push-Location -LiteralPath $repoPath
try {
    git status --short --branch | Set-Content -Encoding utf8 (Join-Path $backupDir "status-after-clone.txt")

    if ($ApplyPatches -and -not $SkipPatchBackup) {
        $indexPatch = Join-Path $backupDir "index.patch"
        $workingPatch = Join-Path $backupDir "working-tree.patch"
        $untrackedZip = Join-Path $backupDir "untracked-files.zip"

        if ((Test-Path -LiteralPath $indexPatch) -and (Get-Item $indexPatch).Length -gt 0) {
            git apply --3way --whitespace=nowarn $indexPatch
            if ($LASTEXITCODE -ne 0) {
                throw "Failed to apply staged patch: $indexPatch"
            }
        }

        if ((Test-Path -LiteralPath $workingPatch) -and (Get-Item $workingPatch).Length -gt 0) {
            git apply --3way --whitespace=nowarn $workingPatch
            if ($LASTEXITCODE -ne 0) {
                throw "Failed to apply working-tree patch: $workingPatch"
            }
        }

        if (Test-Path -LiteralPath $untrackedZip) {
            Expand-Archive -LiteralPath $untrackedZip -DestinationPath $repoPath -Force
        }

        git status --short --branch | Set-Content -Encoding utf8 (Join-Path $backupDir "status-after-apply.txt")
    }
} finally {
    Pop-Location
}

Write-Host ""
Write-Host "Done."
Write-Host "New clone:  $repoPath"
Write-Host "Old clone:  $oldRepoPath"
Write-Host "Backup:     $backupDir"
if (-not $ApplyPatches -and -not $SkipPatchBackup) {
    Write-Host "Local changes were backed up but not applied. Re-run with -ApplyPatches or apply patches manually if needed."
}
