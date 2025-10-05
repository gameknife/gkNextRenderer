#!/usr/bin/env bash
set -euo pipefail

print_usage() {
    cat <<'USAGE'
Usage: ./run.sh [options] [-- extra args]
  --target NAME          Executable to launch (default: gkNextRenderer)
  --platform NAME        build/<platform>/bin subdirectory override
  --bin-dir PATH         absolute/relative bin directory override
  --present-mode VALUE   append --present-mode=VALUE
  --scene PATH           append --load-scene=PATH
  --list                 list entries in the bin directory
  --dry-run              print command without running
  -h, --help             show this help

Additional arguments after -- are passed through as-is.
USAGE
}

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
case "$(uname -s)" in
    Darwin*)
        if [[ "$(uname -m)" == "arm64" ]]; then
            default_platform="macos"
        else
            default_platform="macos_x64"
        fi
        ;;
    MINGW*|MSYS*)
        default_platform="mingw"
        ;;
    *)
        default_platform="linux"
        ;;
esac

target="gkNextRenderer"
platform="$default_platform"
bin_dir=""
platform_overridden=0
bin_overridden=0
list_only=0
dry_run=0
cmd_args=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --target|--platform|--bin-dir|--present-mode|--scene)
            [[ $# -lt 2 ]] && { echo "Missing value for $1" >&2; exit 1; }
            case "$1" in
                --target) target="$2" ;;
                --platform) platform="$2"; platform_overridden=1 ;;
                --bin-dir) bin_dir="$2"; bin_overridden=1 ;;
                --present-mode) cmd_args+=("--present-mode=$2") ;;
                --scene) cmd_args+=("--load-scene=$2") ;;
            esac
            shift 2
            ;;
        --list) list_only=1; shift ;;
        --dry-run) dry_run=1; shift ;;
        -h|--help) print_usage; exit 0 ;;
        --) shift; cmd_args+=("$@"); break ;;
        *) cmd_args+=("$1"); shift ;;
    esac
done

run_android() {
    local android_dir="$script_dir/android"
    [[ -d "$android_dir" ]] || { echo "Android project directory not found: $android_dir" >&2; exit 1; }
    if [[ $list_only -eq 1 ]]; then
        echo "--list is not supported for android platform" >&2
        exit 1
    fi
    if [[ ${#cmd_args[@]} -gt 0 ]]; then
        echo "Ignoring extra arguments for android platform: ${cmd_args[*]}" >&2
    fi
    local cmd=("./gradlew" "installAndLaunch")
    echo "Working dir: $android_dir"
    echo "Command: ${cmd[*]}"
    [[ $dry_run -eq 1 ]] && exit 0
    pushd "$android_dir" >/dev/null
    "${cmd[@]}"
    popd >/dev/null
}

run_native() {
    local resolved_bin="$bin_dir"
    if [[ -z "$resolved_bin" ]]; then
        resolved_bin="$script_dir/build/$platform/bin"
    fi

    if [[ ! -d "$resolved_bin" && $bin_overridden -eq 0 ]]; then
        if [[ $platform_overridden -eq 0 ]]; then
            for fallback in linux macos macos_x64 mingw; do
                local candidate="$script_dir/build/$fallback/bin"
                if [[ -d "$candidate" ]]; then
                    resolved_bin="$candidate"
                    platform="$fallback"
                    break
                fi
            done
        fi
    fi

    if [[ ! -d "$resolved_bin" ]]; then
        echo "Bin directory not found: $resolved_bin" >&2
        exit 1
    fi

    if [[ $list_only -eq 1 ]]; then
        echo "Entries in $resolved_bin:"
        ls -1 "$resolved_bin"
        exit 0
    fi

    local exe="$resolved_bin/$target"
    if [[ ! -x "$exe" ]]; then
        echo "Executable not found: $exe" >&2
        exit 1
    fi

    local cmd=("./$target" "${cmd_args[@]}")
    echo "Working dir: $resolved_bin"
    echo "Command: ${cmd[*]}"
    [[ $dry_run -eq 1 ]] && exit 0
    pushd "$resolved_bin" >/dev/null
    "${cmd[@]}"
    popd >/dev/null
}

if [[ "$platform" == "android" ]]; then
    run_android
else
    run_native
fi
