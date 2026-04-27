#!/usr/bin/env bash
set -euo pipefail

# Download optional binary assets from the project's GitHub release.
# These files are not committed to the repo to keep the clone size small.
#
# Supported groups:
#   ldraw    -> assets/paks/ldraw.pak           (LDraw parts library, BrickPlayer)
#   optional -> assets/paks/optional.pak        (extra sample assets)
#   sfx      -> assets/sfx/*.{mp3,wav}          (background music & sound effects)
#   ffmpeg   -> src/ThirdParty/ffmpeg/bin/      (Windows-only ffmpeg.exe for MagicaLego)
#
# The SDL3 Android archive is no longer fetched here; android/app/build.gradle
# pulls it directly from libsdl-org's official release on first build.
#
# Environment overrides:
#   PAKS_REPO         GitHub "owner/name" (default: gameknife/gkNextEngine)
#   PAKS_RELEASE_TAG  Release tag (default: paks-latest)
#   PAKS_BASE_URL     Full base URL override; files are appended as <BASE>/<name>

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

REPO="${PAKS_REPO:-gameknife/gkNextEngine}"
TAG="${PAKS_RELEASE_TAG:-paks-latest}"
BASE_URL="${PAKS_BASE_URL:-https://github.com/${REPO}/releases/download/${TAG}}"

# Manifest entries: "id|asset-name|destination-relative-to-repo-root"
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
FORCE=0
ANY_SELECTOR=0

usage() {
    cat <<EOF
Usage: $(basename "$0") [selectors...] [--force]

Selectors (any combination; defaults to --all when none given):
  --all        Fetch every group below.
  --ldraw      ldraw.pak             -> assets/paks/
  --optional   optional.pak          -> assets/paks/
  --sfx        background music + sound effects -> assets/sfx/
  --ffmpeg     ffmpeg.exe            -> src/ThirdParty/ffmpeg/bin/   (Windows only)

Other:
  --force      Re-download even if the file already exists.
  -h, --help   Show this help.

Environment overrides:
  PAKS_REPO         GitHub repo (default: gameknife/gkNextEngine)
  PAKS_RELEASE_TAG  Release tag (default: paks-latest)
  PAKS_BASE_URL     Full base URL override (skips GitHub release lookup)
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --all)      WANT_LDRAW=1; WANT_OPTIONAL=1; WANT_SFX=1; WANT_FFMPEG=1; ANY_SELECTOR=1 ;;
        --ldraw)    WANT_LDRAW=1;    ANY_SELECTOR=1 ;;
        --optional) WANT_OPTIONAL=1; ANY_SELECTOR=1 ;;
        --sfx)      WANT_SFX=1;      ANY_SELECTOR=1 ;;
        --ffmpeg)   WANT_FFMPEG=1;   ANY_SELECTOR=1 ;;
        --force)    FORCE=1 ;;
        -h|--help)  usage; exit 0 ;;
        *) echo "Unknown argument: $1" >&2; usage; exit 1 ;;
    esac
    shift
done

if [[ ${ANY_SELECTOR} -eq 0 ]]; then
    WANT_LDRAW=1; WANT_OPTIONAL=1; WANT_SFX=1; WANT_FFMPEG=1
fi

is_wanted() {
    case "$1" in
        ldraw)    [[ ${WANT_LDRAW}    -eq 1 ]] ;;
        optional) [[ ${WANT_OPTIONAL} -eq 1 ]] ;;
        sfx)      [[ ${WANT_SFX}      -eq 1 ]] ;;
        ffmpeg)   [[ ${WANT_FFMPEG}   -eq 1 ]] ;;
        *)        return 1 ;;
    esac
}

download_one() {
    local name="$1"
    local dest_rel="$2"
    local url="${BASE_URL}/${name}"
    local out="${REPO_ROOT}/${dest_rel}"

    if [[ -f "${out}" && ${FORCE} -eq 0 ]]; then
        echo "[paks] ${dest_rel} already exists (use --force to re-download)"
        return 0
    fi

    mkdir -p "$(dirname "${out}")"
    echo "[paks] Downloading ${name} -> ${dest_rel}"
    if command -v curl >/dev/null 2>&1; then
        curl -L --fail --progress-bar -o "${out}.part" "${url}"
    elif command -v wget >/dev/null 2>&1; then
        wget --show-progress -O "${out}.part" "${url}"
    else
        echo "[paks] Neither curl nor wget is available; cannot download." >&2
        exit 1
    fi
    mv -f "${out}.part" "${out}"
}

for entry in "${ASSETS[@]}"; do
    IFS='|' read -r id name dest <<<"${entry}"
    if is_wanted "${id}"; then
        download_one "${name}" "${dest}"
    fi
done

echo "[paks] Done."
