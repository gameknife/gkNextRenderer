#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
BUILD_DIR=${BUILD_DIR:-cmake-build-debug}
COMPILE_DIR="$REPO_ROOT/$BUILD_DIR"
COMPILE_DB="$COMPILE_DIR/compile_commands.json"

CLANG_TIDY_BIN=${CLANG_TIDY:-/opt/homebrew/opt/llvm/bin/clang-tidy}
CHECKS=${CHECKS:--*,readability-identifier-naming}
HEADER_FILTER=${HEADER_FILTER:-"^(?!.*(ThirdParty|external)/).*"}
FIX=${FIX:-0}

if [[ ! -f "$COMPILE_DB" ]]; then
  echo "缺少 compile_commands.json: $COMPILE_DB" >&2
  echo "请先运行 'cmake -S . -B $BUILD_DIR -DCMAKE_EXPORT_COMPILE_COMMANDS=ON'" >&2
  exit 1
fi

if [[ ! -x "$CLANG_TIDY_BIN" ]]; then
  echo "未找到 clang-tidy 可执行文件: $CLANG_TIDY_BIN" >&2
  exit 1
fi

if command -v sysctl >/dev/null 2>&1; then
  DEFAULT_JOBS=$(sysctl -n hw.ncpu)
else
  DEFAULT_JOBS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
fi
JOBS=${JOBS:-$DEFAULT_JOBS}

if [[ -n "${FILES_OVERRIDE:-}" ]]; then
  # 支持通过环境变量显式指定文件列表（以换行分隔）
  mapfile -t FILES < <(printf '%s\n' "$FILES_OVERRIDE")
else
  mapfile -t FILES < <(
    cd "$REPO_ROOT" &&
    find src \
      -type f \
      \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.cxx' -o -name '*.h' -o -name '*.hh' -o -name '*.hpp' -o -name '*.hxx' \) \
      ! -path '*/ThirdParty/*' \
      ! -name '*.glsl' \
      ! -name '*.slang'
  )
fi

if [[ ${#FILES[@]} -eq 0 ]]; then
  echo "未找到待处理的源文件" >&2
  exit 1
fi

TIDY_ARGS=(-p "$COMPILE_DIR" -checks "$CHECKS" -header-filter "$HEADER_FILTER" "$@")
if [[ "$FIX" -eq 1 ]]; then
  TIDY_ARGS+=(-fix)
fi

printf '%s\0' "${FILES[@]}" | xargs -0 -n1 -P "$JOBS" "$CLANG_TIDY_BIN" "${TIDY_ARGS[@]}"
