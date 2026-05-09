#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
DEFAULT_SOURCE_ROOT="${SCRIPT_DIR}/source"

LIBRARY_DIR=""
STUDIO_DIR=""
SHADOW_DIR=""
OUTPUT_PAK=""
MANIFEST_PATH=""
PACK_LOG_PATH=""
PACKAGER_PATH=""
NO_COMPRESS=0
KEEP_STAGE=0

print_usage() {
    cat <<'EOF'
Usage: build-ldraw-pak.sh [options]

Options:
  --library-dir PATH   LDraw library source directory
  --studio-dir PATH    Studio 2.0 ldraw directory (supplement, never overwrites)
  --shadow-dir PATH    LDraw shadow library source directory
  --output-pak PATH    Output pak path
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

sync_directory() {
    local source_dir="$1"
    local target_dir="$2"

    mkdir -p "${target_dir}"

    if command -v rsync >/dev/null 2>&1; then
        rsync -a --delete "${source_dir}/" "${target_dir}/"
        return 0
    fi

    rm -rf "${target_dir}"
    mkdir -p "${target_dir}"
    cp -R "${source_dir}/." "${target_dir}/"
}

# Merge source into target, only adding files that do not already exist.
# Returns the number of files added via stdout.
merge_directory() {
    local source_dir="$1"
    local target_dir="$2"
    local label="${3:-supplement}"

    if [[ ! -d "${source_dir}" ]]; then
        echo 0
        return 0
    fi

    mkdir -p "${target_dir}"

    local before after added
    before="$(find "${target_dir}" -type f | wc -l)"

    if command -v rsync >/dev/null 2>&1; then
        rsync -a --ignore-existing "${source_dir}/" "${target_dir}/"
    else
        # cp without overwrite
        find "${source_dir}" -type f | while IFS= read -r src_file; do
            local rel="${src_file#"${source_dir}/"}"
            local dst="${target_dir}/${rel}"
            if [[ ! -f "${dst}" ]]; then
                mkdir -p "$(dirname "${dst}")"
                cp "${src_file}" "${dst}"
            fi
        done
    fi

    after="$(find "${target_dir}" -type f | wc -l)"
    added=$((after - before))

    if [[ ${added} -gt 0 ]]; then
        echo "  Merged ${label} from ${source_dir} -> +${added} files" >&2
    fi

    echo "${added}"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --library-dir)
            LIBRARY_DIR="$2"
            shift 2
            ;;
        --studio-dir)
            STUDIO_DIR="$2"
            shift 2
            ;;
        --shadow-dir)
            SHADOW_DIR="$2"
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

if [[ -z "${LIBRARY_DIR}" ]]; then
    LIBRARY_DIR="${DEFAULT_SOURCE_ROOT}/ldraw"
fi

if [[ -z "${SHADOW_DIR}" ]]; then
    SHADOW_DIR="${DEFAULT_SOURCE_ROOT}/ldrawShadow"
fi

if [[ -z "${OUTPUT_PAK}" ]]; then
    OUTPUT_PAK="${REPO_ROOT}/assets/paks/ldraw.pak"
fi

if [[ -z "${MANIFEST_PATH}" ]]; then
    MANIFEST_PATH="${SCRIPT_DIR}/output/ldraw_manifest.json"
fi

if [[ -z "${PACK_LOG_PATH}" ]]; then
    PACK_LOG_PATH="${SCRIPT_DIR}/output/ldraw_pack.log"
fi

LIBRARY_DIR="$(resolve_path "${LIBRARY_DIR}")"
SHADOW_DIR="$(resolve_path "${SHADOW_DIR}")"
OUTPUT_PAK="$(resolve_path "${OUTPUT_PAK}")"
MANIFEST_PATH="$(resolve_path "${MANIFEST_PATH}")"
PACK_LOG_PATH="$(resolve_path "${PACK_LOG_PATH}")"

if [[ -n "${STUDIO_DIR}" ]]; then
    STUDIO_DIR="$(resolve_path "${STUDIO_DIR}")"
fi

if [[ -z "${PACKAGER_PATH}" ]]; then
    PACKAGER_PATH="$(find_packager)"
else
    PACKAGER_PATH="$(resolve_path "${PACKAGER_PATH}")"
fi

if [[ ! -d "${LIBRARY_DIR}" ]]; then
    echo "LDraw library directory not found: ${LIBRARY_DIR}" >&2
    exit 1
fi

if [[ ! -d "${SHADOW_DIR}" ]]; then
    echo "LDraw shadow library directory not found: ${SHADOW_DIR}" >&2
    exit 1
fi

if [[ ! -f "${LIBRARY_DIR}/LDConfig.ldr" ]]; then
    echo "LDConfig.ldr not found under library directory: ${LIBRARY_DIR}/LDConfig.ldr" >&2
    exit 1
fi

PACKAGER_BIN_DIR="$(dirname "${PACKAGER_PATH}")"
PACKAGER_RUNTIME_ROOT="$(resolve_path "${PACKAGER_BIN_DIR}/..")"
STAGE_ROOT="${PACKAGER_RUNTIME_ROOT}/ldraw_pak_stage"
STAGE_ASSETS_ROOT="${STAGE_ROOT}/assets"
STAGE_LIBRARY_DIR="${STAGE_ASSETS_ROOT}/ldraw"
STAGE_SHADOW_DIR="${STAGE_ASSETS_ROOT}/ldrawShadow"
STAGE_CONN_DIR="${STAGE_ASSETS_ROOT}/ldrawConn"

mkdir -p "$(dirname "${OUTPUT_PAK}")"
mkdir -p "$(dirname "${MANIFEST_PATH}")"
mkdir -p "$(dirname "${PACK_LOG_PATH}")"

# Stage 1: sync base LDraw library (mirror)
echo "Syncing base LDraw library: ${LIBRARY_DIR}"
sync_directory "${LIBRARY_DIR}" "${STAGE_LIBRARY_DIR}"

# Stage 2: merge Studio 2.0 parts (only add missing files, never overwrite)
if [[ -n "${STUDIO_DIR}" && -d "${STUDIO_DIR}" ]]; then
    echo "Merging Studio 2.0 library: ${STUDIO_DIR}"

    TOTAL_ADDED=0
    for sub in parts parts/s p p/48 p/8 p/4; do
        studio_sub="${STUDIO_DIR}/${sub}"
        stage_sub="${STAGE_LIBRARY_DIR}/${sub}"
        added="$(merge_directory "${studio_sub}" "${stage_sub}" "Studio Official/${sub}")"
        TOTAL_ADDED=$((TOTAL_ADDED + added))
    done

    # Merge Studio UnOfficial parts (lowest priority, only add missing)
    studio_unofficial="${STUDIO_DIR}/UnOfficial"
    if [[ -d "${studio_unofficial}" ]]; then
        for sub in parts p; do
            studio_sub="${studio_unofficial}/${sub}"
            stage_sub="${STAGE_LIBRARY_DIR}/${sub}"
            added="$(merge_directory "${studio_sub}" "${stage_sub}" "Studio UnOfficial/${sub}")"
            TOTAL_ADDED=$((TOTAL_ADDED + added))
        done
    fi

    echo "Studio 2.0 merge complete: +${TOTAL_ADDED} files added"
else
    echo "Studio 2.0 library not found, skipping supplement merge"
fi

# Stage 2c: copy Studio 2.0 connectivity files (.conn)
if [[ -n "${STUDIO_DIR}" && -d "${STUDIO_DIR}" ]]; then
    studio_conn_dir="${STUDIO_DIR}/connectivity"
    if [[ -d "${studio_conn_dir}" ]]; then
        echo "Copying Studio 2.0 connectivity: ${studio_conn_dir}"
        sync_directory "${studio_conn_dir}" "${STAGE_CONN_DIR}"
        conn_count="$(find "${STAGE_CONN_DIR}" -name '*.conn' -type f | wc -l)"
        echo "  ${conn_count} .conn files staged"
    fi
fi

# Stage 3: sync shadow library
sync_directory "${SHADOW_DIR}" "${STAGE_SHADOW_DIR}"

PACKAGER_ARGS=(
    "--out" "${OUTPUT_PAK}"
    "--src" "ldraw_pak_stage/assets"
    "--root" "ldraw_pak_stage"
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
