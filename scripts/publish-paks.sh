#!/usr/bin/env bash
set -euo pipefail

# Create or refresh the GitHub release that hosts the optional assets
# (ldraw.pak / optional.pak / sfx / ffmpeg.exe). Uploads or replaces release
# assets with --clobber. The SDL3 Android archive is sourced directly from
# libsdl-org's official release by android/app/build.gradle and is no longer
# mirrored here.
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
TITLE_DEFAULT="Optional Assets (${TAG})"
NOTES_DEFAULT="Optional binary assets consumed by scripts/fetch-paks.sh.

Groups:
- ldraw.pak         — LDraw parts library used by BrickPlayer
- optional.pak      — extra sample assets for the main renderer / Editor
- sfx (mp3/wav)     — background music & sound effects
- ffmpeg.exe        — Windows-only helper used by MagicaLego packaging

Re-uploaded by scripts/publish-paks.sh."

# Manifest entries: "id|asset-name|source-relative-to-repo-root"
ASSETS=(
    "ldraw|ldraw.pak|assets/paks/ldraw.pak"
    "optional|optional.pak|assets/paks/optional.pak"
    "sfx|bgm.mp3|assets/sfx/bgm.mp3"
    "sfx|bgm2.mp3|assets/sfx/bgm2.mp3"
    "sfx|put.mp3|assets/sfx/put.mp3"
    "sfx|put1.wav|assets/sfx/put1.wav"
    "sfx|put2.wav|assets/sfx/put2.wav"
    "sfx|put3.wav|assets/sfx/put3.wav"
    "ffmpeg|ffmpeg.exe|src/ThirdParty/ffmpeg/bin/ffmpeg.exe"
)

WANT_LDRAW=0; WANT_OPTIONAL=0; WANT_SFX=0; WANT_FFMPEG=0
ANY_SELECTOR=0
DRY_RUN=0
TITLE=""
NOTES=""

usage() {
    cat <<EOF
Usage: $(basename "$0") [selectors...] [--dry-run] [--title TXT] [--notes TXT]

Creates the release tag if it doesn't exist, then uploads (or replaces) the
selected assets. Defaults to --all when no selector is passed.

Selectors (any combination):
  --all        Publish every group below.
  --ldraw      ldraw.pak
  --optional   optional.pak
  --sfx        sfx mp3/wav files
  --ffmpeg     ffmpeg.exe

Other:
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
        --all)      WANT_LDRAW=1; WANT_OPTIONAL=1; WANT_SFX=1; WANT_FFMPEG=1; ANY_SELECTOR=1 ;;
        --ldraw)    WANT_LDRAW=1;    ANY_SELECTOR=1 ;;
        --optional) WANT_OPTIONAL=1; ANY_SELECTOR=1 ;;
        --sfx)      WANT_SFX=1;      ANY_SELECTOR=1 ;;
        --ffmpeg)   WANT_FFMPEG=1;   ANY_SELECTOR=1 ;;
        --dry-run)  DRY_RUN=1 ;;
        --title)    TITLE="${2:-}"; shift ;;
        --notes)    NOTES="${2:-}"; shift ;;
        -h|--help)  usage; exit 0 ;;
        *) echo "Unknown argument: $1" >&2; usage; exit 1 ;;
    esac
    shift
done

if [[ ${ANY_SELECTOR} -eq 0 ]]; then
    WANT_LDRAW=1; WANT_OPTIONAL=1; WANT_SFX=1; WANT_FFMPEG=1
fi

TITLE="${TITLE:-${TITLE_DEFAULT}}"
NOTES="${NOTES:-${NOTES_DEFAULT}}"

is_wanted() {
    case "$1" in
        ldraw)    [[ ${WANT_LDRAW}    -eq 1 ]] ;;
        optional) [[ ${WANT_OPTIONAL} -eq 1 ]] ;;
        sfx)      [[ ${WANT_SFX}      -eq 1 ]] ;;
        ffmpeg)   [[ ${WANT_FFMPEG}   -eq 1 ]] ;;
        *)        return 1 ;;
    esac
}

if ! command -v gh >/dev/null 2>&1; then
    echo "[paks] gh (GitHub CLI) is required. Install from https://cli.github.com/" >&2
    exit 1
fi

if ! gh auth status >/dev/null 2>&1; then
    echo "[paks] gh is not authenticated. Run 'gh auth login' first." >&2
    exit 1
fi

# Build the upload list and verify locally.
SELECTED=()
missing=0
for entry in "${ASSETS[@]}"; do
    IFS='|' read -r id name src <<<"${entry}"
    if ! is_wanted "${id}"; then continue; fi
    abs="${REPO_ROOT}/${src}"
    if [[ ! -f "${abs}" ]]; then
        echo "[paks] Missing local asset: ${src}" >&2
        missing=1
        continue
    fi
    SELECTED+=("${abs}")
done

if [[ ${missing} -ne 0 ]]; then
    echo "[paks] Bring the missing files in place first (build paks, fetch ffmpeg, etc.)." >&2
    exit 1
fi

if [[ ${#SELECTED[@]} -eq 0 ]]; then
    echo "[paks] Nothing selected to publish." >&2
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
for asset in "${SELECTED[@]}"; do
    size=$(wc -c < "${asset}" 2>/dev/null || echo "?")
    echo "         ${asset} (${size} bytes)"
done

if gh release view "${TAG}" --repo "${REPO}" >/dev/null 2>&1; then
    echo "[paks] Release ${TAG} already exists — uploading with --clobber"
    run gh release upload "${TAG}" "${SELECTED[@]}" --repo "${REPO}" --clobber
else
    echo "[paks] Release ${TAG} not found — creating"
    run gh release create "${TAG}" "${SELECTED[@]}" \
        --repo "${REPO}" \
        --title "${TITLE}" \
        --notes "${NOTES}" \
        --latest=false
fi

echo "[paks] Done."
