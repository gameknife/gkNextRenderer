#!/usr/bin/env bash
set -euo pipefail

# Backwards-compatible shim. The canonical fetcher now lives at
# scripts/fetch-paks.sh and can pull every optional pak (ldraw.pak / optional.pak)
# from the project's GitHub release. See scripts/fetch-paks.sh --help.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

# Preserve the legacy OPTIONAL_PAK_URL override by mapping it onto PAKS_BASE_URL,
# stripping the trailing "/optional.pak" if the user passed a full asset URL.
if [[ -n "${OPTIONAL_PAK_URL:-}" && -z "${PAKS_BASE_URL:-}" ]]; then
    case "${OPTIONAL_PAK_URL}" in
        */optional.pak) export PAKS_BASE_URL="${OPTIONAL_PAK_URL%/optional.pak}" ;;
        *)              export PAKS_BASE_URL="${OPTIONAL_PAK_URL}" ;;
    esac
fi

exec "${REPO_ROOT}/scripts/fetch-paks.sh" --optional "$@"
