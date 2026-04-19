#!/usr/bin/env bash
set -euo pipefail

# Download assets/paks/optional.pak from the CDN/COS.
# Override OPTIONAL_PAK_URL in the environment if you host the file elsewhere.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

# TODO: replace with the real COS/CDN URL once uploaded.
DEFAULT_URL="https://TODO-replace-me.example.com/gkNextRenderer/optional.pak"

URL="${OPTIONAL_PAK_URL:-${DEFAULT_URL}}"
OUTPUT="${REPO_ROOT}/assets/paks/optional.pak"

if [[ "${URL}" == *TODO-replace-me* ]]; then
    echo "OPTIONAL_PAK_URL is not set and the default URL is a TODO placeholder." >&2
    echo "Set OPTIONAL_PAK_URL or edit tools/optional-pak/fetch-optional-pak.sh to point at your CDN." >&2
    exit 1
fi

mkdir -p "$(dirname "${OUTPUT}")"

echo "Downloading optional.pak from ${URL}"
if command -v curl >/dev/null 2>&1; then
    curl -L --fail --progress-bar -o "${OUTPUT}.part" "${URL}"
elif command -v wget >/dev/null 2>&1; then
    wget -O "${OUTPUT}.part" "${URL}"
else
    echo "Neither curl nor wget is available; cannot download." >&2
    exit 1
fi

mv -f "${OUTPUT}.part" "${OUTPUT}"
echo "Saved to ${OUTPUT}"
