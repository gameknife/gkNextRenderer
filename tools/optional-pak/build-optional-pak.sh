#!/usr/bin/env bash
set -euo pipefail

# Build assets/paks/optional.pak from the list in optional-files.txt.
# Missing source files are reported but do not abort the build, so this script
# works against a trimmed repo once the source files have been re-downloaded
# locally (e.g. via fetch-optional-pak.sh or a manual copy).

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

FILE_LIST="${SCRIPT_DIR}/optional-files.txt"
OUTPUT_PAK=""
MANIFEST_PATH=""
PACK_LOG_PATH=""
PACKAGER_PATH=""
NO_COMPRESS=0
KEEP_STAGE=0

print_usage() {
    cat <<'EOF'
Usage: build-optional-pak.sh [options]

Options:
  --file-list PATH     Text file listing paths (relative to repo root) to pack
  --output-pak PATH    Output pak path (default: assets/paks/optional.pak)
  --manifest PATH      Manifest json path
  --pack-log PATH      Packager log path
  --packager PATH      Packager binary path
  --no-compress        Disable compression
  --keep-stage         Keep temporary stage directory
  -h, --help           Show this help
EOF
}

resolve_path() {
    python3 -c 'import os,sys; print(os.path.abspath(sys.argv[1]))' "$1"
}

find_packager() {
    local candidates=(
        "${REPO_ROOT}/out/build/linux/bin/Packager"
        "${REPO_ROOT}/out/build/macos-arm64/bin/Packager"
        "${REPO_ROOT}/out/build/windows/bin/Packager.exe"
    )

    local candidate
    for candidate in "${candidates[@]}"; do
        if [[ -f "${candidate}" ]]; then
            printf '%s\n' "$(resolve_path "${candidate}")"
            return 0
        fi
    done

    local discovered
    discovered="$(find "${REPO_ROOT}/out/build" -type f \( -name 'Packager' -o -name 'Packager.exe' \) | sort | head -n 1 || true)"
    if [[ -n "${discovered}" ]]; then
        printf '%s\n' "$(resolve_path "${discovered}")"
        return 0
    fi

    echo "Packager binary not found. Build it first, e.g. './gnb.sh build Packager'." >&2
    return 1
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --file-list)
            FILE_LIST="$2"
            shift 2
            ;;
        --output-pak)
            OUTPUT_PAK="$2"
            shift 2
            ;;
        --manifest)
            MANIFEST_PATH="$2"
            shift 2
            ;;
        --pack-log)
            PACK_LOG_PATH="$2"
            shift 2
            ;;
        --packager)
            PACKAGER_PATH="$2"
            shift 2
            ;;
        --no-compress)
            NO_COMPRESS=1
            shift
            ;;
        --keep-stage)
            KEEP_STAGE=1
            shift
            ;;
        -h|--help)
            print_usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            print_usage >&2
            exit 1
            ;;
    esac
done

if [[ -z "${OUTPUT_PAK}" ]]; then
    OUTPUT_PAK="${REPO_ROOT}/assets/paks/optional.pak"
fi

if [[ -z "${MANIFEST_PATH}" ]]; then
    MANIFEST_PATH="${SCRIPT_DIR}/output/optional_manifest.json"
fi

if [[ -z "${PACK_LOG_PATH}" ]]; then
    PACK_LOG_PATH="${SCRIPT_DIR}/output/optional_pack.log"
fi

FILE_LIST="$(resolve_path "${FILE_LIST}")"
OUTPUT_PAK="$(resolve_path "${OUTPUT_PAK}")"
MANIFEST_PATH="$(resolve_path "${MANIFEST_PATH}")"
PACK_LOG_PATH="$(resolve_path "${PACK_LOG_PATH}")"

if [[ ! -f "${FILE_LIST}" ]]; then
    echo "File list not found: ${FILE_LIST}" >&2
    exit 1
fi

if [[ -z "${PACKAGER_PATH}" ]]; then
    PACKAGER_PATH="$(find_packager)"
else
    PACKAGER_PATH="$(resolve_path "${PACKAGER_PATH}")"
fi

PACKAGER_BIN_DIR="$(dirname "${PACKAGER_PATH}")"
PACKAGER_RUNTIME_ROOT="$(resolve_path "${PACKAGER_BIN_DIR}/..")"
STAGE_ROOT="${PACKAGER_RUNTIME_ROOT}/optional_pak_stage"

mkdir -p "$(dirname "${OUTPUT_PAK}")"
mkdir -p "$(dirname "${MANIFEST_PATH}")"
mkdir -p "$(dirname "${PACK_LOG_PATH}")"

# Stage: copy listed files into STAGE_ROOT preserving their repo-relative paths.
rm -rf "${STAGE_ROOT}"
mkdir -p "${STAGE_ROOT}"

PACKED_COUNT=0
MISSING_COUNT=0
MISSING_FILES=()

stage_file() {
    local rel="$1"
    local src="${REPO_ROOT}/${rel}"
    local dst="${STAGE_ROOT}/${rel}"
    mkdir -p "$(dirname "${dst}")"
    cp "${src}" "${dst}"
    PACKED_COUNT=$((PACKED_COUNT + 1))
}

while IFS= read -r raw_line || [[ -n "${raw_line}" ]]; do
    # strip comments and trim whitespace
    line="${raw_line%%#*}"
    line="${line#"${line%%[![:space:]]*}"}"
    line="${line%"${line##*[![:space:]]}"}"
    [[ -z "${line}" ]] && continue

    # Trailing slash marks a directory entry: recurse through all files under it.
    if [[ "${line}" == */ ]]; then
        dir_rel="${line%/}"
        dir_abs="${REPO_ROOT}/${dir_rel}"
        if [[ ! -d "${dir_abs}" ]]; then
            MISSING_COUNT=$((MISSING_COUNT + 1))
            MISSING_FILES+=("${line}")
            continue
        fi
        while IFS= read -r -d '' file_abs; do
            file_rel="${file_abs#"${REPO_ROOT}/"}"
            stage_file "${file_rel}"
        done < <(find "${dir_abs}" -type f ! -name '.DS_Store' -print0)
        continue
    fi

    src="${REPO_ROOT}/${line}"
    if [[ ! -f "${src}" ]]; then
        MISSING_COUNT=$((MISSING_COUNT + 1))
        MISSING_FILES+=("${line}")
        continue
    fi

    stage_file "${line}"
done < "${FILE_LIST}"

if [[ ${PACKED_COUNT} -eq 0 ]]; then
    echo "No source files found. Nothing to pack." >&2
    if [[ ${MISSING_COUNT} -gt 0 ]]; then
        echo "All ${MISSING_COUNT} listed files are missing locally." >&2
    fi
    exit 1
fi

echo "Staged ${PACKED_COUNT} file(s) into ${STAGE_ROOT}"
if [[ ${MISSING_COUNT} -gt 0 ]]; then
    echo "Warning: ${MISSING_COUNT} file(s) in ${FILE_LIST} not found on disk and will be skipped:"
    printf '  - %s\n' "${MISSING_FILES[@]}"
fi

PACKAGER_ARGS=(
    "--out" "${OUTPUT_PAK}"
    "--src" "optional_pak_stage"
    "--root" "optional_pak_stage"
    "--manifest" "${MANIFEST_PATH}"
)

if [[ ${NO_COMPRESS} -eq 1 ]]; then
    PACKAGER_ARGS+=("--no-compress")
fi

pushd "${PACKAGER_BIN_DIR}" >/dev/null
set +e
"${PACKAGER_PATH}" "${PACKAGER_ARGS[@]}" >"${PACK_LOG_PATH}" 2>&1
PACKAGER_EXIT=$?
set -e
popd >/dev/null

if [[ ${PACKAGER_EXIT} -ne 0 ]]; then
    echo "Packager failed with exit code ${PACKAGER_EXIT}. See log: ${PACK_LOG_PATH}" >&2
    exit ${PACKAGER_EXIT}
fi

if [[ ${KEEP_STAGE} -eq 0 && -d "${STAGE_ROOT}" ]]; then
    rm -rf "${STAGE_ROOT}"
fi

echo "Generated pak: ${OUTPUT_PAK}"
echo "Manifest: ${MANIFEST_PATH}"
echo "Pack log: ${PACK_LOG_PATH}"
