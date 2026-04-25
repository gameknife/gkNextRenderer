#!/usr/bin/env bash
set -euo pipefail

# Download optional asset paks (ldraw.pak / optional.pak) from the project's
# GitHub release. These files are not committed to the repository to keep the
# clone small. The engine mounts them automatically when present at
# assets/paks/<name>.pak.
#
# Override defaults via environment variables:
#   PAKS_REPO         GitHub "owner/name" (default: gameknife/gkNextEngine)
#   PAKS_RELEASE_TAG  Release tag hosting the assets (default: paks-latest)
#   PAKS_BASE_URL     Full base URL; replaces the GitHub one when set
#                     (e.g. a private mirror). Files are appended as <BASE>/<name>.pak

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

REPO="${PAKS_REPO:-gameknife/gkNextEngine}"
TAG="${PAKS_RELEASE_TAG:-paks-latest}"
BASE_URL="${PAKS_BASE_URL:-https://github.com/${REPO}/releases/download/${TAG}}"

OUTPUT_DIR="${REPO_ROOT}/assets/paks"

WANT_LDRAW=0
WANT_OPTIONAL=0
FORCE=0

usage() {
    cat <<EOF
Usage: $(basename "$0") [--all|--ldraw|--optional] [--force]

Options:
  --all        Fetch every optional pak (default when no flag is given).
  --ldraw      Fetch only ldraw.pak.
  --optional   Fetch only optional.pak.
  --force      Re-download even if the file already exists.
  -h, --help   Show this help.

Environment overrides:
  PAKS_REPO         GitHub repo (default: gameknife/gkNextEngine)
  PAKS_RELEASE_TAG  Release tag (default: paks-latest)
  PAKS_BASE_URL     Full base URL override (skips GitHub release lookup)
EOF
}

if [[ $# -eq 0 ]]; then
    WANT_LDRAW=1
    WANT_OPTIONAL=1
fi

while [[ $# -gt 0 ]]; do
    case "$1" in
        --all)      WANT_LDRAW=1; WANT_OPTIONAL=1 ;;
        --ldraw)    WANT_LDRAW=1 ;;
        --optional) WANT_OPTIONAL=1 ;;
        --force)    FORCE=1 ;;
        -h|--help)  usage; exit 0 ;;
        *) echo "Unknown argument: $1" >&2; usage; exit 1 ;;
    esac
    shift
done

if [[ ${WANT_LDRAW} -eq 0 && ${WANT_OPTIONAL} -eq 0 ]]; then
    WANT_LDRAW=1
    WANT_OPTIONAL=1
fi

mkdir -p "${OUTPUT_DIR}"

download_one() {
    local name="$1"
    local url="${BASE_URL}/${name}"
    local out="${OUTPUT_DIR}/${name}"

    if [[ -f "${out}" && ${FORCE} -eq 0 ]]; then
        echo "[paks] ${name} already exists at ${out} (use --force to re-download)"
        return 0
    fi

    echo "[paks] Downloading ${name} from ${url}"
    if command -v curl >/dev/null 2>&1; then
        curl -L --fail --progress-bar -o "${out}.part" "${url}"
    elif command -v wget >/dev/null 2>&1; then
        wget --show-progress -O "${out}.part" "${url}"
    else
        echo "[paks] Neither curl nor wget is available; cannot download." >&2
        exit 1
    fi
    mv -f "${out}.part" "${out}"
    echo "[paks] Saved ${out}"
}

if [[ ${WANT_LDRAW}    -eq 1 ]]; then download_one "ldraw.pak";    fi
if [[ ${WANT_OPTIONAL} -eq 1 ]]; then download_one "optional.pak"; fi
