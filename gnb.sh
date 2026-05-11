#!/usr/bin/env sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
OS="$(uname -s)"
ARCH="$(uname -m)"
case "$OS/$ARCH" in
  Linux/x86_64) PLATFORM="linux-amd64" ;;
  Darwin/arm64) PLATFORM="macos-arm64" ;;
  Darwin/x86_64) PLATFORM="macos-amd64" ;;
  *) echo "unsupported platform: $OS/$ARCH" >&2; exit 1 ;;
esac
GNB="$ROOT/tools/gnb-bin/$PLATFORM/gnb"
if [ -x "$ROOT/gnb" ]; then GNB="$ROOT/gnb"; fi
if [ ! -x "$GNB" ] && command -v go >/dev/null 2>&1; then
  (cd "$ROOT/tools/gnb" && go build -trimpath -ldflags="-s -w" -o "$ROOT/gnb" ./cmd/gnb)
  GNB="$ROOT/gnb"
fi
if [ ! -x "$GNB" ]; then
  mkdir -p "$(dirname "$GNB")"
  curl -L -o "$GNB" "https://github.com/gameknife/gkNextEngine/releases/download/paks-latest/gnb-$PLATFORM"
  chmod +x "$GNB"
fi
exec "$GNB" "$@"
