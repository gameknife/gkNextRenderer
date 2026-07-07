#!/usr/bin/env bash
set -euo pipefail

repo_path=""
remote_url=""
branch="dev"
backup_root=""
apply_patches=0
skip_patch_backup=0

usage() {
    cat <<'EOF'
Usage:
  reclone-after-history-rewrite.sh [options]

Options:
  --repo-path PATH       Existing clone path. Defaults to current Git repository.
  --remote-url URL       Remote URL. Defaults to origin URL from the old clone.
  --branch NAME          Branch to clone. Defaults to dev.
  --backup-root PATH     Backup directory root. Defaults to sibling directory.
  --apply-patches        Try to apply saved local patches to the new clone.
  --skip-patch-backup    Do not export local patches or untracked files.
  -h, --help             Show this help.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --repo-path)
            repo_path="${2:?missing value for --repo-path}"
            shift 2
            ;;
        --remote-url)
            remote_url="${2:?missing value for --remote-url}"
            shift 2
            ;;
        --branch)
            branch="${2:?missing value for --branch}"
            shift 2
            ;;
        --backup-root)
            backup_root="${2:?missing value for --backup-root}"
            shift 2
            ;;
        --apply-patches)
            apply_patches=1
            shift
            ;;
        --skip-patch-backup)
            skip_patch_backup=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

resolve_repo_path() {
    if [[ -n "$repo_path" ]]; then
        (cd "$repo_path" && pwd -P)
    else
        local top
        top="$(git rev-parse --show-toplevel 2>/dev/null || true)"
        if [[ -z "$top" ]]; then
            echo "repo path was not provided and current directory is not inside a Git repository" >&2
            exit 1
        fi
        (cd "$top" && pwd -P)
    fi
}

repo_path="$(resolve_repo_path)"
repo_parent="$(dirname "$repo_path")"
repo_name="$(basename "$repo_path")"
stamp="$(date +%Y%m%d-%H%M%S)"

if [[ ! -d "$repo_path/.git" ]]; then
    echo "repo path does not look like a non-bare Git working tree: $repo_path" >&2
    exit 1
fi

if [[ -z "$remote_url" ]]; then
    remote_url="$(cd "$repo_path" && git remote get-url origin)"
fi

if [[ -z "$backup_root" ]]; then
    backup_root="$repo_parent/$repo_name.history-rewrite-backups"
fi

backup_dir="$backup_root/$stamp"
old_repo_path="$repo_parent/$repo_name.old-history-$stamp"

echo "Repository: $repo_path"
echo "Remote:     $remote_url"
echo "Branch:     $branch"
echo "Backup:     $backup_dir"
echo "Old clone:  $old_repo_path"

mkdir -p "$backup_dir"

(
    cd "$repo_path"
    git status --short --branch > "$backup_dir/status-before.txt"
    git rev-parse HEAD > "$backup_dir/head-before.txt"
    git remote -v > "$backup_dir/remotes-before.txt"

    if [[ "$skip_patch_backup" -eq 0 ]]; then
        git diff --binary --output="$backup_dir/working-tree.patch"
        git diff --cached --binary --output="$backup_dir/index.patch"

        git ls-files -z --others --exclude-standard > "$backup_dir/untracked-files.zlist"
        git ls-files --others --exclude-standard > "$backup_dir/untracked-files.txt"
        if [[ -s "$backup_dir/untracked-files.zlist" ]]; then
            tar -czf "$backup_dir/untracked-files.tar.gz" --null -T "$backup_dir/untracked-files.zlist"
        fi
    fi
)

cd "$repo_parent"

if [[ -e "$old_repo_path" ]]; then
    echo "old repo backup path already exists: $old_repo_path" >&2
    exit 1
fi

mv "$repo_path" "$old_repo_path"

if ! git clone --branch "$branch" "$remote_url" "$repo_path"; then
    echo "clone failed; restoring old repo path" >&2
    rm -rf "$repo_path"
    mv "$old_repo_path" "$repo_path"
    exit 1
fi

(
    cd "$repo_path"
    git status --short --branch > "$backup_dir/status-after-clone.txt"

    if [[ "$apply_patches" -eq 1 && "$skip_patch_backup" -eq 0 ]]; then
        if [[ -s "$backup_dir/index.patch" ]]; then
            git apply --3way --whitespace=nowarn "$backup_dir/index.patch"
        fi
        if [[ -s "$backup_dir/working-tree.patch" ]]; then
            git apply --3way --whitespace=nowarn "$backup_dir/working-tree.patch"
        fi
        if [[ -f "$backup_dir/untracked-files.tar.gz" ]]; then
            tar -xzf "$backup_dir/untracked-files.tar.gz" -C "$repo_path"
        fi
        git status --short --branch > "$backup_dir/status-after-apply.txt"
    fi
)

echo
echo "Done."
echo "New clone:  $repo_path"
echo "Old clone:  $old_repo_path"
echo "Backup:     $backup_dir"
if [[ "$apply_patches" -eq 0 && "$skip_patch_backup" -eq 0 ]]; then
    echo "Local changes were backed up but not applied. Re-run with --apply-patches or apply patches manually if needed."
fi
