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
    export VCPKG_DOWNLOADS="$PROJECT_ROOT/.vcpkg_cache/downloads"
    export X_VCPKG_REGISTRIES_CACHE="$PROJECT_ROOT/.vcpkg_cache/registries"

    if [ ! -d "$VCPKG_DEFAULT_BINARY_CACHE" ]; then
        mkdir -p "$VCPKG_DEFAULT_BINARY_CACHE"
    fi
    if [ ! -d "$VCPKG_DOWNLOADS" ]; then
        mkdir -p "$VCPKG_DOWNLOADS"
    fi
    if [ ! -d "$X_VCPKG_REGISTRIES_CACHE" ]; then
        mkdir -p "$X_VCPKG_REGISTRIES_CACHE"
    fi
    VCPKG_ROOT="${VCPKG_ROOT:-$DEFAULT_VCPKG_ROOT}"
    VCPKG_EXE="$VCPKG_ROOT/vcpkg"
    VCPKG_GIT_REF="2025.12.12"
    UPDATE_REPO=0
}

parse_arguments() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --update) UPDATE_REPO=1; shift ;;
            -h|--help) usage; exit 0 ;;
            *) warn "Unknown argument: $1"; shift ;;
        esac
    done
}

ensure_repo() {
    if [ ! -d "$VCPKG_ROOT/.git" ]; then
        log "Cloning vcpkg into $VCPKG_ROOT..."
        git clone https://github.com/microsoft/vcpkg "$VCPKG_ROOT"
    fi

    # Only force checkout specific ref if we are NOT updating.
    # If we updated, we presume the user wants the latest state they just pulled.
    if [ "$UPDATE_REPO" -eq 0 ]; then
        git -C "$VCPKG_ROOT" fetch origin --tags --force
        git -C "$VCPKG_ROOT" checkout --force "$VCPKG_GIT_REF"
        git -C "$VCPKG_ROOT" reset --hard "$VCPKG_GIT_REF"
    else
        # If updating, ensure we are on a branch that tracks origin
        # (Usually master).
        # But if we are detached, pull might fail without context.
        # "pull --ff-only" works if we have an upstream.
        # If we are detached at a specific commit, pull might be ambiguous.
        
        # Helper: switch to master before pulling if we want "latest"
        git -C "$VCPKG_ROOT" checkout master || git -C "$VCPKG_ROOT" checkout -b master origin/master
        
        log "Updating vcpkg in $VCPKG_ROOT (git pull)..."
        git -C "$VCPKG_ROOT" pull --ff-only || warn "Failed to update vcpkg repo."
    fi
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
Usage: ./vcpkg.sh [options]

Options:
  --update     Force git pull on the vcpkg repository.
  -h, --help   Show this help.
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