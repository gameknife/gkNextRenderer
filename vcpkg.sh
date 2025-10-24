#!/bin/bash
set -euo pipefail

# ============================================================================
# HELPER FUNCTIONS
# ============================================================================

init_variables() {
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    PROJECT_ROOT="$SCRIPT_DIR"
    DEFAULT_VCPKG_ROOT="$PROJECT_ROOT/.vcpkg"
    export VCPKG_DEFAULT_BINARY_CACHE="$PROJECT_ROOT/.vcpkg_bincache"
    if [ ! -d "$VCPKG_DEFAULT_BINARY_CACHE" ]; then
        mkdir -p "$VCPKG_DEFAULT_BINARY_CACHE"
    fi
    VCPKG_ROOT="${VCPKG_ROOT:-$DEFAULT_VCPKG_ROOT}"
    VCPKG_EXE="$VCPKG_ROOT/vcpkg"
    VCPKG_GIT_REF="2025.10.17"
}

parse_arguments() {
    if [ $# -eq 0 ]; then
        usage
        exit 1
    fi
    
    PLATFORM="$1"
    FEATURES="${2:-}"
    
    if [ -n "$FEATURES" ]; then
        export VCPKG_MANIFEST_FEATURES="$FEATURES"
    else
        unset VCPKG_MANIFEST_FEATURES || true
    fi
}

ensure_repo() {
    if [ ! -d "$VCPKG_ROOT/.git" ]; then
        log "Cloning vcpkg into $VCPKG_ROOT..."
        git clone https://github.com/microsoft/vcpkg "$VCPKG_ROOT"
    else
        log "Updating vcpkg in $VCPKG_ROOT..."
        # if ! git -C "$VCPKG_ROOT" pull --ff-only; then
        #     warn "无法访问远程仓库，继续使用现有 vcpkg 副本。"
        # fi
    fi

    git -C "$VCPKG_ROOT" fetch origin --tags --force
    git -C "$VCPKG_ROOT" checkout --force "$VCPKG_GIT_REF"
    git -C "$VCPKG_ROOT" reset --hard "$VCPKG_GIT_REF"
}

ensure_bootstrap() {
    if [ ! -x "$VCPKG_EXE" ]; then
        log "Bootstrapping vcpkg..."
        (cd "$VCPKG_ROOT" && ./bootstrap-vcpkg.sh -disableMetrics)
    fi
}

# ============================================================================
# ERROR HANDLING & USAGE
# ============================================================================

log()  { printf '[vcpkg] %s\n' "$*"; }
warn() { printf '[vcpkg] Warning: %s\n' "$*" >&2; }

usage() {
    cat <<USAGE
Usage: ./vcpkg.sh <platform> [manifest-features]

Platforms:
  macos        (arm64-osx)
  macos_x64    (x64-osx)
  linux        (x64-linux)
  android      (arm64-android)
  ios          (arm64-ios)
  mingw        (x64-mingw-static)
  windows      (x64-windows-static)

Examples:
  ./vcpkg.sh macos
  ./vcpkg.sh linux avif
USAGE
}

# ============================================================================
# MAIN LOGIC
# ============================================================================

main() {
    init_variables
    parse_arguments "$@"
    
    ensure_repo
    ensure_bootstrap

    log "Done. 如果使用自定义路径，记得复用 VCPKG_ROOT=${VCPKG_ROOT}."
}

# ============================================================================
# MAIN SCRIPT ENTRY POINT
# ============================================================================

main "$@"