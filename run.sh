#!/usr/bin/env bash
set -euo pipefail

print_usage() {
    cat <<'USAGE'
Usage: ./run.sh [options] [-- extra args]
  --target NAME          Executable to launch (default: gkNextRenderer)
  --preset NAME          CMake Preset name (default: auto-detected)
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

# Auto-detect default preset
case "$(uname -s)" in
    Darwin*)
        if [[ "$(uname -m)" == "arm64" ]]; then
            default_preset="macos-arm64"
        else
            default_preset="macos-x64"
        fi
        ;;
    Linux*)
        default_preset="linux-release"
        ;;
    MINGW*|MSYS*)
        default_preset="mingw" 
        ;;
    *)
        default_preset="unknown"
        ;;
esac

target="gkNextRenderer"
preset="$default_preset"
bin_dir=""
preset_overridden=0
bin_overridden=0
list_only=0
dry_run=0
cmd_args=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --target) target="$2"; shift 2 ;;
        --target=*) target="${1#*=}"; shift ;;
        --preset) preset="$2"; preset_overridden=1; shift 2 ;;
        --preset=*) preset="${1#*=}"; preset_overridden=1; shift ;;
        --bin-dir) bin_dir="$2"; bin_overridden=1; shift 2 ;;
        --bin-dir=*) bin_dir="${1#*=}"; bin_overridden=1; shift ;;
        --present-mode) cmd_args+=("--present-mode=$2"); shift 2 ;;
        --present-mode=*) cmd_args+=("--present-mode=${1#*=}"); shift ;;
        --scene) cmd_args+=("--load-scene=$2"); shift 2 ;;
        --scene=*) cmd_args+=("--load-scene=${1#*=}"); shift ;;
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
    
    # Priority 1: Explicit --bin-dir (already handled by assignment)
    
    # Priority 2: New CMake Preset location (out/build/<preset>/bin)
    if [[ -z "$resolved_bin" ]]; then
        local preset_path="$script_dir/out/build/$preset/bin"
        if [[ -d "$preset_path" ]]; then
            resolved_bin="$preset_path"
        fi
    fi

    # Priority 3: Old Build Location Fallback (before smart search)
    if [[ -z "$resolved_bin" ]]; then
       # Map preset names to old platform names
       local old_plat="unknown"
       case "$preset" in
           macos-arm64|macos-x64) old_plat="macos" ;;
           linux-*) old_plat="linux" ;;
           windows-*) old_plat="windows" ;;
           mingw) old_plat="mingw" ;;
       esac
       
       local old_path="$script_dir/build/$old_plat/bin"
       if [[ -d "$old_path" ]]; then
            resolved_bin="$old_path"
       fi
    fi
    
    # Priority 4: Smart Search (try all presets)
    if [[ -z "$resolved_bin" && $bin_overridden -eq 0 && $preset_overridden -eq 0 ]]; then
        # Try finding any valid build output in new preset locations
        for fallback in macos-arm64 macos-x64 linux-release windows-dev; do
            local candidate="$script_dir/out/build/$fallback/bin"
            if [[ -d "$candidate" ]]; then
                resolved_bin="$candidate"
                preset="$fallback"
                break
            fi
        done
    fi

    if [[ -z "$resolved_bin" || ! -d "$resolved_bin" ]]; then
        echo "Bin directory not found. Have you built the project?" >&2
        echo "Expected: out/build/$preset/bin" >&2
        exit 1
    fi

    if [[ $list_only -eq 1 ]]; then
        echo "Entries in $resolved_bin:"
        ls -1 "$resolved_bin"
        exit 0
    fi

    local exe="$resolved_bin/$target"
    # Try finding without extension, then with extension (if needed)
    if [[ ! -x "$exe" ]]; then
         if [[ -x "$exe.exe" ]]; then
             exe="$exe.exe"
         else
            echo "Executable not found: $exe" >&2
            exit 1
         fi
    fi

    local cmd=("./$(basename "$exe")")
    if [[ ${#cmd_args[@]} -gt 0 ]]; then
        cmd+=("${cmd_args[@]}")
    fi
    echo "Working dir: $resolved_bin"
    echo "Command: ${cmd[*]}"
    [[ $dry_run -eq 1 ]] && exit 0
    pushd "$resolved_bin" >/dev/null
    "${cmd[@]}"
    popd >/dev/null
}

if [[ "$preset" == "android" ]]; then
    run_android
else
    run_native
fi