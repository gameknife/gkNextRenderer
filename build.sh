#!/bin/bash
set -euo pipefail

# ==============================================================================
# gkNextRenderer Build Script (Linux/macOS)
# Wraps CMake Presets for a standard build workflow.
# ==============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR"

export VCPKG_ROOT="$PROJECT_ROOT/.vcpkg"
export VCPKG_BINARY_SOURCES="clear;files,$PROJECT_ROOT/.vcpkg_bincache,readwrite"

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log()  { printf "${GREEN}[build] %s${NC}\n" "$*"; }
warn() { printf "${YELLOW}[build] Warning: %s${NC}\n" "$*" >&2; }
err()  { printf "${RED}[build] Error: %s${NC}\n" "$*" >&2; exit 1; }

# ==============================================================================
# Helper Functions
# ==============================================================================

detect_platform() {
    case "$(uname -s)" in
        Darwin*) 
            if [ "$(uname -m)" = "arm64" ]; then echo "macos-arm64"; else echo "macos-x64"; fi ;;
        Linux*) echo "linux-release" ;; 
        MINGW*|MSYS*) echo "mingw" ;;
        *) echo "unknown" ;; 
    esac
}

ensure_vcpkg() {
    local vcpkg_cmake="$PROJECT_ROOT/.vcpkg/scripts/buildsystems/vcpkg.cmake"
    if [ ! -f "$vcpkg_cmake" ]; then
        log "vcpkg toolchain not found. Bootstrapping..."
        "$PROJECT_ROOT/vcpkg.sh" "$(detect_platform | cut -d- -f1)"
    fi
}

ensure_tsc() {
    local tsc_bin="$PROJECT_ROOT/tools/tsc/tsc"
    if [[ "$(uname -s)" == "MINGW"* || "$(uname -s)" == "MSYS"* ]]; then
        tsc_bin="$tsc_bin.exe"
    fi

    if [ ! -f "$tsc_bin" ]; then
        log "Fetching TypeScript Compiler (TSC)..."
        "$PROJECT_ROOT/tools/fetch_tsc.sh"
    fi
}

ensure_linux_deps() {
    # Check for slangc on Linux
    if [[ "$(uname -s)" == "Linux"* ]]; then
        if ! command -v slangc &> /dev/null && [ ! -f "$PROJECT_ROOT/external/slang/bin/slangc" ]; then
            # Basic check, existing script had complex logic, simplifying to trust the fetch script
             if [ -f "$PROJECT_ROOT/tools/fetch_slang_linux.sh" ]; then
                 log "Ensuring Slang compiler..."
                 "$PROJECT_ROOT/tools/fetch_slang_linux.sh"
             fi
        fi
    fi
}

ensure_ios_deps() {
    local ios_lib="$PROJECT_ROOT/external/MoltenVK/ios/lib/libMoltenVK.a"
    if [ ! -f "$ios_lib" ]; then
        log "Fetching MoltenVK for iOS..."
        "$PROJECT_ROOT/tools/fetch_moltenvk.sh" ios
    fi
}

ensure_oidn() {
    local oidn_lib="$PROJECT_ROOT/src/ThirdParty/oidn/lib/libOpenImageDenoise.so"
    if [[ "$(uname -s)" == "Darwin"* ]]; then
        oidn_lib="$PROJECT_ROOT/src/ThirdParty/oidn/lib/libOpenImageDenoise.dylib"
    fi
    
    if [ ! -f "$oidn_lib" ]; then
        log "OIDN binaries not found. Fetching..."
        if [ -f "$PROJECT_ROOT/tools/fetch_oidn.sh" ]; then
             "$PROJECT_ROOT/tools/fetch_oidn.sh"
        else
             warn "tools/fetch_oidn.sh not found. OIDN support might fail."
        fi
    fi
}

ensure_streamline() {
    local sl_lib="$PROJECT_ROOT/src/ThirdParty/streamline/lib/x64/sl.interposer.lib"
    
    if [ ! -f "$sl_lib" ]; then
        log "Streamline SDK not found. Fetching..."
        if [ -f "$PROJECT_ROOT/tools/fetch_streamline.bat" ]; then
             # Call the bat file as it's Windows-only anyway
             cmd.exe /c "$(cygpath -w "$PROJECT_ROOT/tools/fetch_streamline.bat")"
        else
             warn "tools/fetch_streamline.bat not found. DLSS support might fail."
        fi
    fi
}

# ==============================================================================
# Main Logic
# ==============================================================================

PRESET=""
CONFIG=""
TARGET=""
CLEAN=0
TARGET_ANDROID=0
AVIF=0
DLSS=0
OIDN=0

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --clean) CLEAN=1; shift ;;
        --android) TARGET_ANDROID=1; shift ;;
        --avif) AVIF=1; shift ;;
        --dlss) DLSS=1; shift ;;
        --oidn) OIDN=1; shift ;;
        --preset) PRESET="$2"; shift 2 ;;
        --preset=*) PRESET="${1#*=}"; shift ;;
        --config) CONFIG="$2"; shift 2 ;;
        --config=*) CONFIG="${1#*=}"; shift ;;
        --target) TARGET="$2"; shift 2 ;;
        --target=*) TARGET="${1#*=}"; shift ;;
        --help|-h)
            echo "Usage: ./build.sh [options]"
            echo "Options:"
            echo "  --preset <name>  CMake preset to use"
            echo "  --config <type>  Build configuration (Debug, Release, etc.)"
            echo "  --target <name>  Specific target to build"
            echo "  --clean          Clean build directory before building"
            echo "  --android        Build for Android"
            echo "  --avif           Enable AVIF support"
            echo "  --dlss           Enable DLSS support"
            echo "  --oidn           Enable OIDN support"
            echo "  -h, --help       Show this help"
            exit 0
            ;;
        *)
            # Allow positional argument for preset for backward compatibility
            if [[ "$1" != -* ]] && [ -z "$PRESET" ]; then
                PRESET="$1"
                shift
            else
                warn "Unknown argument: $1"
                shift
            fi
            ;;
    esac
done

# Android Build
if [ "$TARGET_ANDROID" -eq 1 ]; then
    ensure_tsc
    log "Building for Android..."
    cd "$PROJECT_ROOT/android"
    ./gradlew build
    exit $?
fi

# Native Build
if [ -z "$PRESET" ]; then
    PRESET=$(detect_platform)
    log "Auto-detected preset: $PRESET"
fi

ensure_vcpkg
ensure_tsc
ensure_linux_deps

# Special handling for iOS/MoltenVK if user manually selected an ios preset (future proofing)
if [[ "$PRESET" == *"ios"* ]]; then
    ensure_ios_deps
fi

if [ "$CLEAN" -eq 1 ]; then
    log "Cleaning build for preset: $PRESET..."
    # Removing the build directory defined in CMakePresets.json
    rm -rf "$PROJECT_ROOT/out/build/$PRESET"
fi

log "Configuring preset: $PRESET"
CMAKE_CONFIGURE_ARGS=("--preset" "$PRESET")
if [ "$AVIF" -eq 1 ]; then
    CMAKE_CONFIGURE_ARGS+=("-DGK_ENABLE_AVIF=ON" "-DVCPKG_MANIFEST_FEATURES=avif")
else
    CMAKE_CONFIGURE_ARGS+=("-DGK_ENABLE_AVIF=OFF" "-DVCPKG_MANIFEST_FEATURES=")
fi

if [ "$DLSS" -eq 1 ]; then
    # Check if we are on Windows (MINGW/MSYS)
    IS_WINDOWS=0
    case "$(uname -s)" in
        MINGW*|MSYS*) IS_WINDOWS=1 ;;
    esac

    if [ "$IS_WINDOWS" -eq 1 ]; then
        ensure_streamline
        CMAKE_CONFIGURE_ARGS+=("-DGK_ENABLE_DLSS=ON")
    else
        warn "DLSS/Streamline is currently only supported on Windows. Disabling."
        CMAKE_CONFIGURE_ARGS+=("-DGK_ENABLE_DLSS=OFF")
    fi
else
    CMAKE_CONFIGURE_ARGS+=("-DGK_ENABLE_DLSS=OFF")
fi

if [ "$OIDN" -eq 1 ]; then
    ensure_oidn
    CMAKE_CONFIGURE_ARGS+=("-DGK_ENABLE_OIDN=ON")
else
    CMAKE_CONFIGURE_ARGS+=("-DGK_ENABLE_OIDN=OFF")
fi
cmake "${CMAKE_CONFIGURE_ARGS[@]}"

log "Building preset: $PRESET"
CMAKE_BUILD_ARGS=("--build" "--preset" "$PRESET")

if [ -n "$CONFIG" ]; then
    CMAKE_BUILD_ARGS+=("--config" "$CONFIG")
fi

if [ -n "$TARGET" ]; then
    CMAKE_BUILD_ARGS+=("--target" "$TARGET")
fi

cmake "${CMAKE_BUILD_ARGS[@]}"