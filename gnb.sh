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
  if [ -z "${GOCACHE:-}" ]; then
    GOCACHE="$ROOT/tools/gnb-bin/go-build-cache"
    export GOCACHE
  fi
  GO_BUILD_TAGS="production"
  if [ "$OS" = "Linux" ]; then
    if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists gtk+-3.0 webkit2gtk-4.0; then
      GO_BUILD_TAGS="desktop,production,wv2runtime.embed"
    else
      echo "[gnb] warning: GTK/WebKitGTK development packages not found; building browser-mode gnb" >&2
      echo "[gnb] warning: install gtk+-3.0 and webkit2gtk-4.0 pkg-config packages to enable native dashboard" >&2
    fi
  else
    GO_BUILD_TAGS="desktop,production,wv2runtime.embed"
  fi
  NEED_BUILD=0
  if [ ! -x "$LOCAL_GNB" ]; then
    NEED_BUILD=1
  elif find "$ROOT/tools/gnb" -type f \( -name '*.go' -o -name 'go.mod' -o -name 'go.sum' -o -name '*.html' \) -newer "$LOCAL_GNB" | grep -q .; then
    NEED_BUILD=1
  fi
  if [ "$NEED_BUILD" -eq 1 ]; then
    echo "[gnb] building gnb ..."
    (cd "$ROOT/tools/gnb" && go build -tags "$GO_BUILD_TAGS" -trimpath -ldflags="-s -w -X main.version=$LOCAL_VERSION" -o "$LOCAL_GNB" ./cmd/gnb)
  fi
  GNB="$LOCAL_GNB"
else
  sync_remote_gnb
  GNB="$CACHE_GNB"
fi

exec "$GNB" "$@"
