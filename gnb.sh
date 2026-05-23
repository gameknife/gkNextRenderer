#!/usr/bin/env sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
OS="$(uname -s)"
ARCH="$(uname -m)"
RELEASE_BASE_URL="https://github.com/gameknife/gkNextEngine/releases/download/paks-latest"
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
LOCAL_GNB="$ROOT/gnb"
CACHE_DIR="$ROOT/tools/gnb-bin/$PLATFORM"
CACHE_GNB="$CACHE_DIR/gnb"
CACHE_VERSION_FILE="$CACHE_DIR/gnb-version.txt"
REMOTE_GNB_URL="$RELEASE_BASE_URL/gnb-$PLATFORM"
REMOTE_VERSION_URL="$RELEASE_BASE_URL/gnb-version.txt"
GNB="$CACHE_GNB"
LOCAL_VERSION="${GNB_VERSION:-}"
if [ -z "$LOCAL_VERSION" ] && command -v git >/dev/null 2>&1; then
  LOCAL_VERSION="$(git -C "$ROOT" rev-parse HEAD 2>/dev/null || true)"
fi
if [ -z "$LOCAL_VERSION" ]; then
  LOCAL_VERSION="dev"
fi

download_file() {
  URL="$1"
  DST="$2"
  TMP="${DST}.part"
  mkdir -p "$(dirname "$DST")"
  if ! curl -L --fail -o "$TMP" "$URL"; then
    rm -f "$TMP"
    return 1
  fi
  mv "$TMP" "$DST"
}

read_first_line() {
  sed -n '1s/\r$//p' "$1"
}

fetch_remote_version() {
  TMP="$CACHE_DIR/.gnb-version.remote"
  mkdir -p "$CACHE_DIR"
  if ! curl -fsSL -o "$TMP" "$REMOTE_VERSION_URL"; then
    rm -f "$TMP"
    return 1
  fi
  VERSION_VALUE="$(read_first_line "$TMP")"
  rm -f "$TMP"
  printf '%s' "$VERSION_VALUE"
}

sync_remote_gnb() {
  mkdir -p "$CACHE_DIR"
  NEED_DOWNLOAD=0
  HAD_CACHE=0
  if [ -x "$CACHE_GNB" ]; then
    HAD_CACHE=1
  fi
  REMOTE_VERSION_VALUE=""
  if REMOTE_VERSION_VALUE="$(fetch_remote_version)"; then
    :
  else
    REMOTE_VERSION_VALUE=""
  fi
  LOCAL_VERSION_VALUE=""
  if [ -f "$CACHE_VERSION_FILE" ]; then
    LOCAL_VERSION_VALUE="$(read_first_line "$CACHE_VERSION_FILE")"
  fi
  if [ ! -x "$CACHE_GNB" ]; then
    NEED_DOWNLOAD=1
  elif [ -n "$REMOTE_VERSION_VALUE" ] && [ "$REMOTE_VERSION_VALUE" != "$LOCAL_VERSION_VALUE" ]; then
    NEED_DOWNLOAD=1
  fi
  if [ "$NEED_DOWNLOAD" -eq 1 ]; then
    echo "[gnb] download $REMOTE_GNB_URL"
    if download_file "$REMOTE_GNB_URL" "$CACHE_GNB"; then
      chmod +x "$CACHE_GNB"
      if [ -n "$REMOTE_VERSION_VALUE" ]; then
        printf '%s\n' "$REMOTE_VERSION_VALUE" > "$CACHE_VERSION_FILE"
      fi
    elif [ "$HAD_CACHE" -eq 1 ]; then
      echo "[gnb] warning: failed to update bootstrap binary, using cached copy" >&2
    else
      echo "[gnb] bootstrap binary download failed" >&2
      exit 1
    fi
  fi
  if [ ! -x "$CACHE_GNB" ]; then
    echo "[gnb] bootstrap binary missing and download failed" >&2
    exit 1
  fi
}

if command -v go >/dev/null 2>&1; then
  NEED_BUILD=0
  if [ ! -x "$LOCAL_GNB" ]; then
    NEED_BUILD=1
  elif find "$ROOT/tools/gnb" -type f \( -name '*.go' -o -name 'go.mod' -o -name 'go.sum' -o -name '*.html' \) -newer "$LOCAL_GNB" | grep -q .; then
    NEED_BUILD=1
  fi
  if [ "$NEED_BUILD" -eq 1 ]; then
    (cd "$ROOT/tools/gnb" && go build -trimpath -ldflags="-s -w -X main.version=$LOCAL_VERSION" -o "$LOCAL_GNB" ./cmd/gnb)
  fi
  GNB="$LOCAL_GNB"
else
  sync_remote_gnb
  GNB="$CACHE_GNB"
fi

ensure_intel_macos_host_tools() {
  TSC_URL="https://github.com/rxliuli/tsgo-npm-release/releases/download/v2025.5.23/tsgo-darwin-amd64"
  TSC_DST="$ROOT/tools/tsc/tsc"
  if [ ! -x "$TSC_DST" ] || ! file "$TSC_DST" | grep -q "x86_64"; then
    echo "[gnb] download $TSC_URL"
    download_file "$TSC_URL" "$TSC_DST"
    chmod +x "$TSC_DST"
  fi
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
