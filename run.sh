#!/usr/bin/env bash
set -euo pipefail

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

log()  { printf "${GREEN}[run] %s${NC}\n" "$*"; }
print_err() { printf "${RED}[run] Error: %s${NC}\n" "$*" >&2; }

list_presets_and_exit() {
    print_err "$1"
    log "Available configure presets (build them first with build.sh):"
    cmake --list-presets=configure | sed 's/^/  /'
    exit 1
}

print_usage() {
    cat <<'USAGE'
Usage: ./run.sh [options] [-- extra args]
  --target NAME          Executable to launch (default: gkNextRenderer)
  --preset NAME          CMake Preset name [REQUIRED]
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

target="gkNextRenderer"
preset=""
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

if [[ -z "$preset" && -z "$bin_dir" ]]; then
    list_presets_and_exit "No preset specified. You must explicitly specify a preset using --preset <name>."
fi

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
    
    # Priority 1: Explicit --bin-dir
    if [[ $bin_overridden -eq 1 ]]; then
        resolved_bin="$bin_dir"
    # Priority 2: New CMake Preset location (out/build/<preset>/bin)
    else
        local preset_path="$script_dir/out/build/$preset/bin"
        if [[ -d "$preset_path" ]]; then
            resolved_bin="$preset_path"
        fi
    fi
    
    if [[ -z "$resolved_bin" || ! -d "$resolved_bin" ]]; then
        echo "Bin directory not found. Have you built the project for preset '$preset'?" >&2
        echo "Expected to find: $script_dir/out/build/$preset/bin" >&2
        exit 1
    fi

    resolved_bin="$(cd "$resolved_bin" && pwd)"

    if [[ $list_only -eq 1 ]]; then
        echo "Entries in $resolved_bin:"
        ls -1 "$resolved_bin"
        exit 0
    fi

    local exe="$resolved_bin/$target"
    if [[ ! -x "$exe" && -x "$exe.exe" ]]; then
        exe="$exe.exe"
    fi

    if [[ ! -x "$exe" ]]; then
        echo "Executable not found or not executable: $exe" >&2
        exit 1
    fi

    local cmd=("$exe")
    if [[ ${#cmd_args[@]} -gt 0 ]]; then
        cmd+=("${cmd_args[@]}")
    fi
    echo "Command: ${cmd[*]}"
    [[ $dry_run -eq 1 ]] && exit 0
    "${cmd[@]}"
}

if [[ "$preset" == "android" ]]; then
    run_android
else
    run_native
fi
