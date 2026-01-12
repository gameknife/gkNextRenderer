#!/bin/bash

# ============================================================================
# FETCH OIDN BINARY FOR CROSS-PLATFORM
# ============================================================================

set -e

# ============================================================================
# MAIN LOGIC
# ============================================================================

main() {
    init_variables

    echo "[fetch_oidn] Downloading OpenImageDenoise (OIDN)..."
    echo "[fetch_oidn] Platform: $PLATFORM"
    echo "[fetch_oidn] Source: $OIDN_URL"
    echo "[fetch_oidn] Target Directory: $OIDN_DIR"

    check_existing
    ensure_dirs
    download_oidn
    extract_and_install
    cleanup

    echo "[fetch_oidn] Successfully installed OpenImageDenoise"
}

# ============================================================================
# HELPER FUNCTIONS
# ============================================================================

init_variables() {
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
    THIRDPARTY_DIR="$PROJECT_ROOT/src/ThirdParty"
    OIDN_DIR="$THIRDPARTY_DIR/oidn"
    TEMP_DIR="$PROJECT_ROOT/temp_oidn"

    # Detect platform
    PLATFORM="$(uname -s)"
    ARCH="$(uname -m)"
    
    case "$PLATFORM" in
        Darwin*) 
            if [ "$ARCH" = "arm64" ]; then
                OIDN_URL="https://github.com/RenderKit/oidn/releases/download/v2.4.0/oidn-2.4.0.arm64.macos.tar.gz"
                ARCHIVE_NAME="oidn.tar.gz"
                EXTRACTED_FOLDER_NAME="oidn-2.4.0.arm64.macos"
            else
                OIDN_URL="https://github.com/RenderKit/oidn/releases/download/v2.4.0/oidn-2.4.0.x86_64.macos.tar.gz"
                ARCHIVE_NAME="oidn.tar.gz"
                EXTRACTED_FOLDER_NAME="oidn-2.4.0.x86_64.macos"
            fi
            ;; 
        Linux*) 
            OIDN_URL="https://github.com/RenderKit/oidn/releases/download/v2.4.0/oidn-2.4.0.x86_64.linux.tar.gz"
            ARCHIVE_NAME="oidn.tar.gz"
            EXTRACTED_FOLDER_NAME="oidn-2.4.0.x86_64.linux"
            ;; 
        CYGWIN*|MINGW*|MSYS*) 
            OIDN_URL="https://github.com/RenderKit/oidn/releases/download/v2.4.0/oidn-2.4.0.x64.windows.zip"
            ARCHIVE_NAME="oidn.zip"
            EXTRACTED_FOLDER_NAME="oidn-2.4.0.x64.windows"
            ;; 
        *) 
            echo "[fetch_oidn] Error: Unsupported platform: $PLATFORM"
            exit 1
            ;; 
    esac
}

check_existing() {
    if [ -f "$OIDN_DIR/bin/OpenImageDenoise.dll" ] || \
       [ -f "$OIDN_DIR/lib/libOpenImageDenoise.so" ] || \
       [ -f "$OIDN_DIR/lib/libOpenImageDenoise.dylib" ]; then
        echo "[fetch_oidn] OIDN binaries found in $OIDN_DIR."
        echo "[fetch_oidn] Skipping download."
        exit 0
    fi
}

ensure_dirs() {
    mkdir -p "$THIRDPARTY_DIR"
    mkdir -p "$TEMP_DIR"
}

download_oidn() {
    echo "[fetch_oidn] Downloading from $OIDN_URL..."

    if command -v curl >/dev/null 2>&1; then
        curl -L -o "$TEMP_DIR/$ARCHIVE_NAME" "$OIDN_URL"
    elif command -v wget >/dev/null 2>&1; then
        wget -O "$TEMP_DIR/$ARCHIVE_NAME" "$OIDN_URL"
    else
        echo "[fetch_oidn] Error: Neither curl nor wget is available"
        rm -rf "$TEMP_DIR"
        exit 1
    fi
}

extract_and_install() {
    echo "[fetch_oidn] Extracting archive..."
    
    cd "$TEMP_DIR"
    
    if [[ "$ARCHIVE_NAME" == *.zip ]]; then
        if ! command -v unzip >/dev/null 2>&1; then
             echo "[fetch_oidn] Error: unzip not found"
             cd "$PROJECT_ROOT"
             rm -rf "$TEMP_DIR"
             exit 1
        fi
        unzip -q "$ARCHIVE_NAME"
    else
        tar -xzf "$ARCHIVE_NAME"
    fi
    
    echo "[fetch_oidn] Installing OIDN to $OIDN_DIR..."
    mkdir -p "$OIDN_DIR"
    
    # Move contents using cp -r to merge/overwrite correctly
    cp -r "$EXTRACTED_FOLDER_NAME/"* "$OIDN_DIR/"
    
    cd "$PROJECT_ROOT"
}

cleanup() {
    echo "[fetch_oidn] Cleaning up temporary files..."
    rm -rf "$TEMP_DIR"
}

# ============================================================================
# SCRIPT ENTRY POINT
# ============================================================================

main "$@"
