#!/bin/bash

# ============================================================================
# FETCH TSC BINARY FOR CROSS-PLATFORM
# ============================================================================

set -e

# ============================================================================
# MAIN LOGIC
# ============================================================================

main() {
    init_variables

    echo "[fetch_tsc] Downloading TSGO TypeScript compiler..."
    echo "[fetch_tsc] Platform: $PLATFORM"
    echo "[fetch_tsc] Source: $TSC_URL"
    echo "[fetch_tsc] Target: $TSC_TARGET"

    ensure_tools_dir
    download_tsc
    verify_download

    echo "[fetch_tsc] Successfully downloaded TSGO TypeScript compiler"
}

# ============================================================================
# HELPER FUNCTIONS
# ============================================================================

init_variables() {
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
    TOOLS_DIR="$PROJECT_ROOT/tools"
    TSC_DIR="$TOOLS_DIR/tsc"

    # Detect platform
    PLATFORM="$(uname -s)"
    case "$PLATFORM" in
        Darwin*)
            TSC_URL="https://github.com/rxliuli/tsgo-npm-release/releases/download/v2025.5.23/tsgo-darwin-arm64"
            TSC_TARGET="$TSC_DIR/tsc"
            ;;
        Linux*)
            TSC_URL="https://github.com/rxliuli/tsgo-npm-release/releases/download/v2025.5.23/tsgo-linux-amd64"
            TSC_TARGET="$TSC_DIR/tsc"
            ;;
        CYGWIN*|MINGW*|MSYS*)
            TSC_URL="https://github.com/rxliuli/tsgo-npm-release/releases/download/v2025.5.23/tsgo-windows-amd64.exe"
            TSC_TARGET="$TSC_DIR/tsc.exe"
            ;;
        *)
            echo "[fetch_tsc] Error: Unsupported platform: $PLATFORM"
            exit 1
            ;;
    esac
}

ensure_tools_dir() {
    mkdir -p "$TOOLS_DIR"
    mkdir -p "$TSC_DIR"
}

download_tsc() {
    echo "[fetch_tsc] Downloading from $TSC_URL..."

    if command -v curl >/dev/null 2>&1; then
        curl -L -o "$TSC_TARGET" "$TSC_URL"
    elif command -v wget >/dev/null 2>&1; then
        wget -O "$TSC_TARGET" "$TSC_URL"
    else
        echo "[fetch_tsc] Error: Neither curl nor wget is available"
        exit 1
    fi
}

verify_download() {
    if [[ ! -f "$TSC_TARGET" ]]; then
        echo "[fetch_tsc] Error: Download failed - $TSC_TARGET not found"
        exit 1
    fi

    # Check if file is not empty (should be at least 1MB)
    local file_size=$(stat -f%z "$TSC_TARGET" 2>/dev/null || stat -c%s "$TSC_TARGET" 2>/dev/null || echo 0)
    if [[ $file_size -lt 1048576 ]]; then
        echo "[fetch_tsc] Error: Downloaded file is too small ($file_size bytes)"
        exit 1
    fi

    echo "[fetch_tsc] Downloaded file size: $file_size bytes"

    # Make executable on Unix-like systems
    if [[ "$PLATFORM" != "CYGWIN"* && "$PLATFORM" != "MINGW"* && "$PLATFORM" != "MSYS"* ]]; then
        chmod +x "$TSC_TARGET"
    fi
}

# ============================================================================
# SCRIPT ENTRY POINT
# ============================================================================

main "$@"