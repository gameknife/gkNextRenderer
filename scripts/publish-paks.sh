#!/usr/bin/env bash
set -euo pipefail

# Create or refresh the GitHub release that hosts the optional pak files
# (ldraw.pak / optional.pak). Uploads/replaces release assets with --clobber.
#
# Requires: GitHub CLI (gh) authenticated against the target repo.
#
# Environment overrides:
#   PAKS_REPO         GitHub "owner/name" (default: gameknife/gkNextEngine)
#   PAKS_RELEASE_TAG  Release tag (default: paks-latest)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

REPO="${PAKS_REPO:-gameknife/gkNextEngine}"
TAG="${PAKS_RELEASE_TAG:-paks-latest}"
TITLE_DEFAULT="Optional Asset Paks (${TAG})"
NOTES_DEFAULT="Optional asset paks consumed by scripts/fetch-paks.sh.

Files:
- ldraw.pak    — LDraw parts library used by BrickPlayer
- optional.pak — extra sample assets for the main renderer / Editor

Re-uploaded by scripts/publish-paks.sh."

PAK_DIR="${REPO_ROOT}/assets/paks"

WANT_LDRAW=0
WANT_OPTIONAL=0
DRY_RUN=0
TITLE=""
NOTES=""

usage() {
    cat <<EOF
Usage: $(basename "$0") [--all|--ldraw|--optional] [--dry-run] [--title TXT] [--notes TXT]

Creates the release tag if it doesn't exist, then uploads (or replaces) the
selected pak assets. Defaults to --all when no selector is passed.

Options:
  --all        Publish every optional pak (default).
  --ldraw      Publish only ldraw.pak.
  --optional   Publish only optional.pak.
  --title TXT  Override the release title (only used on creation).
  --notes TXT  Override the release notes body (only used on creation).
  --dry-run    Print what would happen without touching the remote.
  -h, --help   Show this help.

Environment overrides:
  PAKS_REPO         GitHub repo (default: gameknife/gkNextEngine)
  PAKS_RELEASE_TAG  Release tag (default: paks-latest)
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --all)      WANT_LDRAW=1; WANT_OPTIONAL=1 ;;
        --ldraw)    WANT_LDRAW=1 ;;
        --optional) WANT_OPTIONAL=1 ;;
        --dry-run)  DRY_RUN=1 ;;
        --title)    TITLE="${2:-}"; shift ;;
        --notes)    NOTES="${2:-}"; shift ;;
        -h|--help)  usage; exit 0 ;;
        *) echo "Unknown argument: $1" >&2; usage; exit 1 ;;
    esac
    shift
done

if [[ ${WANT_LDRAW} -eq 0 && ${WANT_OPTIONAL} -eq 0 ]]; then
    WANT_LDRAW=1
    WANT_OPTIONAL=1
fi

TITLE="${TITLE:-${TITLE_DEFAULT}}"
NOTES="${NOTES:-${NOTES_DEFAULT}}"

if ! command -v gh >/dev/null 2>&1; then
    echo "[paks] gh (GitHub CLI) is required. Install from https://cli.github.com/" >&2
    exit 1
fi

if ! gh auth status >/dev/null 2>&1; then
    echo "[paks] gh is not authenticated. Run 'gh auth login' first." >&2
    exit 1
fi

ASSETS=()
[[ ${WANT_LDRAW}    -eq 1 ]] && ASSETS+=("${PAK_DIR}/ldraw.pak")
[[ ${WANT_OPTIONAL} -eq 1 ]] && ASSETS+=("${PAK_DIR}/optional.pak")

# Verify all picked assets exist locally before touching the remote.
missing=0
for asset in "${ASSETS[@]}"; do
    if [[ ! -f "${asset}" ]]; then
        echo "[paks] Missing local asset: ${asset}" >&2
        missing=1
    fi
done
if [[ ${missing} -ne 0 ]]; then
    echo "[paks] Build the paks first (see tools/optional-pak/ and tools/ldraw/)." >&2
    exit 1
fi

run() {
    if [[ ${DRY_RUN} -eq 1 ]]; then
        printf "[dry-run]"
        printf " %q" "$@"
        printf "\n"
    else
        "$@"
    fi
}

echo "[paks] Repo: ${REPO}"
echo "[paks] Tag:  ${TAG}"
echo "[paks] Files:"
for asset in "${ASSETS[@]}"; do
    size=$(wc -c < "${asset}" 2>/dev/null || echo "?")
    echo "         ${asset} (${size} bytes)"
done

if gh release view "${TAG}" --repo "${REPO}" >/dev/null 2>&1; then
    echo "[paks] Release ${TAG} already exists — uploading with --clobber"
    run gh release upload "${TAG}" "${ASSETS[@]}" --repo "${REPO}" --clobber
else
    echo "[paks] Release ${TAG} not found — creating"
    run gh release create "${TAG}" "${ASSETS[@]}" \
        --repo "${REPO}" \
        --title "${TITLE}" \
        --notes "${NOTES}" \
        --latest=false
fi

echo "[paks] Done."
