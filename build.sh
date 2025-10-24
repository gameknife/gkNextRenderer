#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR"
BUILD_ROOT="$PROJECT_ROOT/build"
DEFAULT_VCPKG_ROOT="$PROJECT_ROOT/.vcpkg"
export VCPKG_DEFAULT_BINARY_CACHE="$PROJECT_ROOT/.vcpkg_bincache"
VCPKG_ROOT="${VCPKG_ROOT:-$DEFAULT_VCPKG_ROOT}"
TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
MOLTENVK_CACHE_ROOT="$PROJECT_ROOT/external/MoltenVK"

log()  { printf '[build] %s\n' "$*"; }
warn() { printf '[build] Warning: %s\n' "$*" >&2; }

require_toolchain() {
    if [ ! -f "$TOOLCHAIN_FILE" ]; then
        warn "未找到 vcpkg 工具链：$TOOLCHAIN_FILE"
        warn "请先运行 ./vcpkg.sh <platform> 准备依赖。"
        exit 1
    fi
}

normalize_path() {
    local path="$1"
    if [ -d "$path" ]; then
        (cd "$path" && pwd -P)
    else
        local dir
        dir=$(dirname "$path")
        local base
        base=$(basename "$path")
        if [ -d "$dir" ]; then
            (cd "$dir" && printf '%s/%s\n' "$(pwd -P)" "$base")
        else
            printf '%s\n' "$path"
        fi
    fi
}

prepare_build_dir() {
    local dir="$1"
    local should_clean=0
    if [ -f "$dir/CMakeCache.txt" ]; then
        local cached_toolchain
        cached_toolchain=$(grep -E '^CMAKE_TOOLCHAIN_FILE:FILEPATH=' "$dir/CMakeCache.txt" | head -n1 | cut -d= -f2- || true)
        if [ -n "${cached_toolchain:-}" ]; then
            local normalized_cached
            local normalized_toolchain
            normalized_cached=$(normalize_path "$cached_toolchain")
            normalized_toolchain=$(normalize_path "$TOOLCHAIN_FILE")
            if [ "$normalized_cached" != "$normalized_toolchain" ]; then
                should_clean=1
            fi
        fi
    fi
    if [ $should_clean -eq 1 ]; then
        log "检测到旧的 vcpkg 工具链路径，清理 ${dir}。"
        rm -rf "$dir"
    fi
    mkdir -p "$dir"
}

configure() {
    local dir="$1"
    shift
    prepare_build_dir "$dir"
    (cd "$dir" && cmake -Wno-dev "$@" "$PROJECT_ROOT")
}

build_target() {
    local dir="$1"
    shift
    local config_arg=()
    if [ $# -gt 1 ] && [ "$1" = "--config" ]; then
        config_arg=("--config" "$2")
        shift 2
    fi
    (cd "$dir" && cmake --build . "${config_arg[@]}" "$@")
}

find_slangc() {
    find "$PROJECT_ROOT/external" -maxdepth 3 -type f -name slangc 2>/dev/null | head -n1 || true
}

ensure_slang_linux() {
    if [ "$(detect_platform)" != "linux" ]; then
        return
    fi

    if [ -n "$(find_slangc)" ]; then
        return
    fi

    log "slangc 未找到，自动下载..."
    "$PROJECT_ROOT/tools/fetch_slang_linux.sh"

    if [ -z "$(find_slangc)" ]; then
        warn "无法获取 Slang 编译器，请检查网络或 tools/fetch_slang_linux.sh"
        exit 1
    fi
}

ensure_moltenvk_ios() {
    local ios_root="$MOLTENVK_CACHE_ROOT/ios"
    local lib_path="$ios_root/lib/libMoltenVK.a"
    if [ ! -f "$lib_path" ]; then
        log "MoltenVK iOS 库未找到，自动下载..."
        "$PROJECT_ROOT/tools/fetch_moltenvk.sh" ios
    fi
    if [ ! -f "$lib_path" ]; then
        warn "MoltenVK 仍不可用，请检查 tools/fetch_moltenvk.sh。"
        exit 1
    fi
}

fetch_tsc() {
    local tsc_dir="$PROJECT_ROOT/tools/tsc"
    local tsc_target=""

    # Determine target binary name based on platform
    case "$(uname -s)" in
        CYGWIN*|MINGW*|MSYS*)
            tsc_target="$tsc_dir/tsc.exe"
            ;;
        *)
            tsc_target="$tsc_dir/tsc"
            ;;
    esac

    if [ ! -f "$tsc_target" ]; then
        log "TSC not found at $tsc_target, downloading..."
        "$PROJECT_ROOT/tools/fetch_tsc.sh"
    else
        log "TSC already exists at $tsc_target, skipping download"
    fi
}

detect_platform() {
    case "$(uname -s)" in
        Darwin*)
            if [ "$(uname -m)" = "arm64" ]; then
                echo "macos"
            else
                echo "macos_x64"
            fi
            ;;
        Linux*)
            echo "linux"
            ;;
        MINGW*|MSYS*|CYGWIN*)
            echo "mingw"
            ;;
        *)
            echo "unknown"
            ;;
    esac
}

build_macos() {
    require_toolchain
    fetch_tsc
    local triplet="$1"
    local dir="$BUILD_ROOT/macos"
    SECONDS=0
    configure "$dir" -G Ninja \
        -D VCPKG_TARGET_TRIPLET="$triplet" \
        -D VCPKG_MANIFEST_MODE=ON \
        -D VCPKG_MANIFEST_DIR="$PROJECT_ROOT" \
        -D CMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
        -D CMAKE_BUILD_TYPE=RelWithDebInfo
    build_target "$dir" --config RelWithDebInfo
    log "macOS 构建耗时 ${SECONDS}s"
}

build_ios() {
    require_toolchain
    fetch_tsc
    ensure_moltenvk_ios
    local triplet="$1"
    local dir="$BUILD_ROOT/ios"
    local moltenvk_root="$MOLTENVK_CACHE_ROOT/ios"
    SECONDS=0
    configure "$dir" -G Xcode \
        -D CMAKE_SYSTEM_NAME=iOS \
        -D VCPKG_TARGET_TRIPLET="$triplet" \
        -D VCPKG_MANIFEST_MODE=ON \
        -D VCPKG_MANIFEST_DIR="$PROJECT_ROOT" \
        -D MOLTENVK_ROOT="$moltenvk_root" \
        -D GK_IOS_SKIP_CODE_SIGN=ON \
        -D CMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE"
    build_target "$dir" --config RelWithDebInfo
    log "iOS 构建耗时 ${SECONDS}s"
}

build_linux() {
    require_toolchain
    fetch_tsc
    ensure_slang_linux
    local dir="$BUILD_ROOT/linux"
    SECONDS=0
    configure "$dir" -G Ninja \
        -D CMAKE_BUILD_TYPE=Release \
        -D VCPKG_TARGET_TRIPLET=x64-linux \
        -D VCPKG_MANIFEST_MODE=ON \
        -D VCPKG_MANIFEST_DIR="$PROJECT_ROOT" \
        -D CMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE"
    build_target "$dir" --config Release
    log "Linux 构建耗时 ${SECONDS}s"
}

build_mingw() {
    require_toolchain
    fetch_tsc
    local dir="$BUILD_ROOT/mingw"
    SECONDS=0
    configure "$dir" -G Ninja \
        -D VCPKG_TARGET_TRIPLET=x64-mingw-static \
        -D VCPKG_MANIFEST_MODE=ON \
        -D VCPKG_MANIFEST_DIR="$PROJECT_ROOT" \
        -D CMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE"
    build_target "$dir" --config Release
    log "MinGW 构建耗时 ${SECONDS}s"
}

build_android() {
    fetch_tsc
    SECONDS=0
    (cd "$PROJECT_ROOT/android" && ./gradlew build)
    log "Android 构建耗时 ${SECONDS}s"
}

main() {
    local host_platform
    host_platform=$(detect_platform)
    local platform="${1:-$host_platform}"

    case "$platform" in
        macos)
            log "Building for macOS (arm64)..."
            build_macos "arm64-osx"
            ;;
        macos_x64)
            log "Building for macOS (x64)..."
            build_macos "x64-osx"
            ;;
        ios)
            log "Building for iOS (arm64)..."
            build_ios "arm64-ios"
            ;;
        linux)
            log "Building for Linux..."
            build_linux
            ;;
        android)
            log "Building for Android..."
            build_android
            ;;
        mingw)
            log "Building for MinGW..."
            build_mingw
            ;;
        *)
            warn "不支持的平台: $platform"
            warn "可选项: macos, macos_x64, ios, linux, android, mingw"
            exit 1
            ;;
    esac
}

main "$@"
