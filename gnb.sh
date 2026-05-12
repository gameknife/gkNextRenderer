#!/usr/bin/env sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
OS="$(uname -s)"
ARCH="$(uname -m)"
if command -v python3 >/dev/null 2>&1; then
  USER_PYTHON_BIN="$(python3 -c 'import site; print(site.USER_BASE + "/bin")' 2>/dev/null || true)"
  if [ -n "${USER_PYTHON_BIN:-}" ] && [ -d "$USER_PYTHON_BIN" ]; then
    PATH="$USER_PYTHON_BIN:$PATH"
    export PATH
  fi
fi
case "$OS/$ARCH" in
  Linux/x86_64) PLATFORM="linux-amd64" ;;
  Darwin/arm64) PLATFORM="macos-arm64" ;;
  Darwin/x86_64) PLATFORM="macos-amd64" ;;
  *) echo "unsupported platform: $OS/$ARCH" >&2; exit 1 ;;
esac
GNB="$ROOT/tools/gnb-bin/$PLATFORM/gnb"
if [ -x "$ROOT/gnb" ]; then GNB="$ROOT/gnb"; fi
if command -v go >/dev/null 2>&1; then
  NEED_BUILD=0
  if [ ! -x "$ROOT/gnb" ]; then
    NEED_BUILD=1
  elif find "$ROOT/tools/gnb" -type f \( -name '*.go' -o -name 'go.mod' -o -name 'go.sum' \) -newer "$ROOT/gnb" | grep -q .; then
    NEED_BUILD=1
  fi
  if [ "$NEED_BUILD" -eq 1 ]; then
    (cd "$ROOT/tools/gnb" && go build -trimpath -ldflags="-s -w" -o "$ROOT/gnb" ./cmd/gnb)
    GNB="$ROOT/gnb"
  fi
fi
if [ ! -x "$GNB" ]; then
  mkdir -p "$(dirname "$GNB")"
  curl -L -o "$GNB" "https://github.com/gameknife/gkNextEngine/releases/download/paks-latest/gnb-$PLATFORM"
  chmod +x "$GNB"
fi

download_file() {
  URL="$1"
  DST="$2"
  TMP="${DST}.part"
  mkdir -p "$(dirname "$DST")"
  curl -L --fail -o "$TMP" "$URL"
  mv "$TMP" "$DST"
}

ensure_intel_macos_host_tools() {
  TSC_URL="https://github.com/rxliuli/tsgo-npm-release/releases/download/v2025.5.23/tsgo-darwin-amd64"
  TSC_DST="$ROOT/tools/tsc/tsc"
  if [ ! -x "$TSC_DST" ] || ! file "$TSC_DST" | grep -q "x86_64"; then
    echo "[gnb] download $TSC_URL"
    download_file "$TSC_URL" "$TSC_DST"
    chmod +x "$TSC_DST"
  fi

  SLANG_URL="https://github.com/shader-slang/slang/releases/download/v2025.6.1/slang-2025.6.1-macos-x86_64.zip"
  SLANG_DIR="$ROOT/external/slang-2025.6.1-macos-x86_64"
  SLANG_SENTINEL="$SLANG_DIR/bin/slangc"
  if [ ! -x "$SLANG_SENTINEL" ] || ! file "$SLANG_SENTINEL" | grep -q "x86_64"; then
    TMP_ZIP="$ROOT/external/.download-slang-2025.6.1-macos-x86_64.zip"
    echo "[gnb] download $SLANG_URL"
    download_file "$SLANG_URL" "$TMP_ZIP"
    rm -rf "$SLANG_DIR"
    mkdir -p "$SLANG_DIR"
    unzip -q "$TMP_ZIP" -d "$SLANG_DIR"
    rm -f "$TMP_ZIP"
    chmod +x "$SLANG_SENTINEL"
  fi
  ln -sfn "$(basename "$SLANG_DIR")" "$ROOT/external/slang"
}

fallback_intel_macos_build() {
  PRESET="macos-arm64"
  BUILD_DIR="$ROOT/out/build/$PRESET"
  TARGET=""
  CLEAN=0
  RECONFIGURE=0
  NO_UNITY=0
  LTO=0
  PRINT_CMD=0
  SKIP_SETUP=0
  JOBS=""

  while [ $# -gt 0 ]; do
    case "$1" in
      --clean) CLEAN=1 ;;
      --reconfigure) RECONFIGURE=1 ;;
      --no-unity) NO_UNITY=1 ;;
      --lto) LTO=1 ;;
      --print-cmd) PRINT_CMD=1 ;;
      --skip-setup) SKIP_SETUP=1 ;;
      --jobs)
        if [ $# -lt 2 ]; then
          echo "--jobs requires a value" >&2
          exit 1
        fi
        JOBS="$2"
        shift
        ;;
      --jobs=*) JOBS="${1#*=}" ;;
      -*)
        echo "unsupported build flag on Darwin/x86_64 fallback: $1" >&2
        exit 1
        ;;
      *)
        if [ -n "$TARGET" ]; then
          echo "build accepts at most one target" >&2
          exit 1
        fi
        TARGET="$1"
        ;;
    esac
    shift
  done

  if [ "$CLEAN" -eq 1 ]; then
    echo "[gnb] rm -rf $BUILD_DIR"
    if [ "$PRINT_CMD" -eq 0 ]; then
      rm -rf "$BUILD_DIR"
    fi
  fi

  if [ "$SKIP_SETUP" -eq 0 ]; then
    echo "[gnb] $GNB setup --skip-paks"
    if [ "$PRINT_CMD" -eq 0 ]; then
      "$GNB" setup --skip-paks
    fi
  fi
  if [ "$PRINT_CMD" -eq 0 ]; then
    ensure_intel_macos_host_tools
  fi

  NEED_CONFIGURE=0
  if [ "$RECONFIGURE" -eq 1 ] || [ "$NO_UNITY" -eq 1 ] || [ "$LTO" -eq 1 ] || [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    NEED_CONFIGURE=1
  fi

  if [ "$NEED_CONFIGURE" -eq 1 ]; then
    set -- --preset "$PRESET"
    if [ "$NO_UNITY" -eq 1 ]; then
      set -- "$@" -DENABLE_UNITY_BUILD=OFF
    fi
    if [ "$LTO" -eq 1 ]; then
      set -- "$@" -DENABLE_LTO=ON
    fi
    echo "[gnb] cmake $*"
    if [ "$PRINT_CMD" -eq 0 ]; then
      (cd "$ROOT" && cmake "$@")
    fi
  else
    echo "[info] configure skipped; use --reconfigure to force"
  fi

  set -- --build --preset "$PRESET"
  if [ -n "$TARGET" ]; then
    set -- "$@" --target "$TARGET"
  fi
  if [ -n "$JOBS" ]; then
    set -- "$@" --parallel "$JOBS"
  fi
  echo "[gnb] cmake $*"
  if [ "$PRINT_CMD" -eq 0 ]; then
    (cd "$ROOT" && cmake "$@")
  fi
}

if [ "$OS/$ARCH" = "Darwin/x86_64" ] && [ "${1:-}" = "build" ]; then
  shift
  fallback_intel_macos_build "$@"
  exit 0
fi

exec "$GNB" "$@"
