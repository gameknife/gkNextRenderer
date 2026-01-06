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
    else
        if [ "$UPDATE_REPO" -eq 1 ]; then
            log "Updating vcpkg in $VCPKG_ROOT (git pull)..."
            git -C "$VCPKG_ROOT" pull --ff-only || warn "Failed to update vcpkg repo."
        fi
    fi

    # Only checkout/reset if we are NOT updating (or if we want to enforce specific version even after update?)
    # Usually pinned version is preferred. If --update is passed, maybe we want latest? 
    # The spec says "--update: Force git pull". 
    # But usually projects want a pinned version.
    # Let's assume --update brings it to the pinned version or latest?
    # Spec says "Force git pull".
    # Existing code resets to VCPKG_GIT_REF.
    # If I update, I probably want to update the REF or ignore it?
    # Let's keep strict pinning for stability, but --update might be used to fetch new refs if VCPKG_GIT_REF changed in script.
    
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