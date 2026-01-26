#!/bin/bash
set -euo pipefail
# ### HELP_START ###
# ==============================================================================
# gkNextRenderer Build Script v2.2 (Linux/macOS)
# Wraps CMake Presets for a streamlined build workflow.
# Supports incremental configuration (skips configure if already configured).
#
# Usage:
#   ./build.sh [options] [-- <cmake_args>...]
#
# Options:
#   --preset <name>  CMake configure preset (e.g., default-linux) [REQUIRED]
#   --clean          Clean build directory before building.
#   --reconfigure    Force CMake reconfiguration even if already configured.
#   --android        Switch to Android Gradle build.
#   --help, -h       Show this help message.
#
# Examples:
#   ./build.sh --preset full-linux
#   ./build.sh --preset default-linux -- -DENABLE_AVIF=ON
#   ./build.sh --preset default-linux --clean
#   ./build.sh --preset default-linux --reconfigure
# ==============================================================================
# ### HELP_END ###

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR"

# Timing
TOTAL_START=$(date +%s)

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log()  { printf "${GREEN}[build] %s${NC}\n" "$*"; }
warn() { printf "${YELLOW}[build] Warning: %s${NC}\n" "$*" >&2; }
err()  { printf "${RED}[build] Error: %s${NC}\n" "$*" >&2; exit 1; }
print_err() { printf "${RED}[build] Error: %s${NC}\n" "$*" >&2; }

# ==============================================================================
# Helper Functions
# ==============================================================================

detect_default_preset() {
    case "$(uname -s)" in
        Darwin*) 
            if [ "$(uname -m)" = "arm64" ]; then echo "default-macos-arm64"; else echo "default-macos-x64"; fi ;;
        Linux*) echo "default-linux" ;; 
        MINGW*|MSYS*) echo "default-windows" ;;
        *) echo "unknown" ;; 
    esac
}

ensure_vcpkg() {
    if [ ! -f "$PROJECT_ROOT/.vcpkg/scripts/buildsystems/vcpkg.cmake" ]; then
        log "vcpkg toolchain not found. Bootstrapping..."
        local platform
        platform=$(detect_default_preset)
        if [[ "$platform" == *"macos"* ]]; then
            "$PROJECT_ROOT/vcpkg.sh" "macos"
        else
            "$PROJECT_ROOT/vcpkg.sh" "$(echo "$platform" | cut -d- -f2)"
        fi
    fi
}

show_help() {
    sed -n '/### HELP_START ###/,/### HELP_END ###/p' "$0" | \
    grep -v '### HELP_START ###' | grep -v '### HELP_END ###' | \
    cut -c3- # Remove leading '# '
    exit 0
}

list_presets_and_exit() {
    print_err "$1"
    log "Available configure presets:"
    cmake --list-presets=configure | sed 's/^/  /'
    exit 1
}

# ==============================================================================
# Main Logic
# ==============================================================================

CONFIGURE_PRESET=""
CLEAN=0
RECONFIGURE=0
TARGET_ANDROID=0
declare -a CMAKE_ARGS=()

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --clean) CLEAN=1; shift ;;
        --reconfigure) RECONFIGURE=1; shift ;;
        --android) TARGET_ANDROID=1; shift ;;
        --preset) 
            if [[ -z "$2" || "$2" == --* ]]; then
                list_presets_and_exit "--preset option requires a value."
            fi
            CONFIGURE_PRESET="$2"
            shift 2
            ;;
        --preset=*) CONFIGURE_PRESET="${1#*=}"; shift ;;
        --help|-h) show_help ;;
        --) shift; CMAKE_ARGS+=("$@"); break ;;
        *) CMAKE_ARGS+=("$1"); shift ;;
    esac
done

# Android Build
if [ "$TARGET_ANDROID" -eq 1 ]; then
    log "Building for Android..."
    cd "$PROJECT_ROOT/android"
    ./gradlew build
    exit $?
fi

# Native Build
if [ -z "$CONFIGURE_PRESET" ]; then
    list_presets_and_exit "No preset specified. You must explicitly specify a preset."
fi

ensure_vcpkg

BUILD_DIR="$PROJECT_ROOT/out/build/$CONFIGURE_PRESET"
CACHE_FILE="$BUILD_DIR/CMakeCache.txt"

if [ "$CLEAN" -eq 1 ]; then
    log "Cleaning build for preset: $CONFIGURE_PRESET..."
    rm -rf "$BUILD_DIR"
fi

# Check if reconfiguration is needed
NEEDS_CONFIGURE=0
if [ "$RECONFIGURE" -eq 1 ] || [ "$CLEAN" -eq 1 ] || [ ! -f "$CACHE_FILE" ]; then
    NEEDS_CONFIGURE=1
fi

# Check if any -D arguments are passed (requires reconfigure)
for arg in "${CMAKE_ARGS[@]}"; do
    if [[ "$arg" == -D* ]]; then
        NEEDS_CONFIGURE=1
        break
    fi
done

config_time=0
if [ "$NEEDS_CONFIGURE" -eq 1 ]; then
    if [ ${#CMAKE_ARGS[@]} -eq 0 ]; then
        log "Configuring preset: $CONFIGURE_PRESET"
    else
        log "Configuring preset: $CONFIGURE_PRESET with extra args: ${CMAKE_ARGS[*]}"
    fi
    config_start=$(date +%s)
    if [ ${#CMAKE_ARGS[@]} -eq 0 ]; then
        cmake --preset "$CONFIGURE_PRESET"
    else
        cmake --preset "$CONFIGURE_PRESET" "${CMAKE_ARGS[@]}"
    fi
    config_time=$(( $(date +%s) - config_start ))
else
    log "Skipping configure (already configured). Use --reconfigure to force."
fi

# Derive build preset name from configure preset name
# In current CMakePresets.json, build presets map 1:1 to configure presets
BUILD_PRESET="$CONFIGURE_PRESET"


log "Building with preset: $BUILD_PRESET"
build_start=$(date +%s)
# Pass only build-specific args to the build command
declare -a build_args=()
is_target_next=0
if [ ${#CMAKE_ARGS[@]} -gt 0 ]; then
    for arg in "${CMAKE_ARGS[@]}"; do
        if [ $is_target_next -eq 1 ]; then
            build_args+=("$arg")
            is_target_next=0
            continue
        fi
        if [[ "$arg" == "--target" ]]; then
            build_args+=("$arg")
            is_target_next=1
        elif [[ "$arg" == --* ]]; then
            # Heuristic: pass common build tool arguments
            if [[ "$arg" == "--config" || "$arg" == "-j" || "$arg" == "--verbose" ]]; then
                 build_args+=("$arg")
                 # if the arg is like --config=Release
                 if [[ "$arg" != *=* ]]; then
                    is_target_next=1
                 fi
            fi
        fi
    done
fi

if [ ${#build_args[@]} -eq 0 ]; then
    cmake --build --preset "$BUILD_PRESET"
else
    cmake --build --preset "$BUILD_PRESET" "${build_args[@]}"
fi
build_time=$(( $(date +%s) - build_start ))

TOTAL_TIME=$(( $(date +%s) - TOTAL_START ))

log "--------------------------------------------------"
log "Build Finished!"
log "  Preset:      $CONFIGURE_PRESET"
log "  Configure:   ${config_time}s"
log "  Build:       ${build_time}s"
log "  Total:       ${TOTAL_TIME}s"
log "--------------------------------------------------"
