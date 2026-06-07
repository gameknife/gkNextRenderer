#!/usr/bin/env sh
# gnb bootstrap installer.
#
# Usage:
#   curl -fsSL https://github.com/gameknife/gkNextEngine/releases/download/paks-latest/gnb-init.sh | sh
#   # or download this file, drop into an empty folder, and run:
#   sh gnb-init.sh [target-dir]
#
# What it does:
#   1) Detect platform (linux-amd64 / macos-arm64 / macos-amd64)
#   2) Download the matching gnb binary from the paks-latest release
#   3) Invoke `gnb init <target-dir>` to clone gkNextEngine
#   4) Print next-step instructions (setup / build / dashboard)
set -eu

REPO="gameknife/gkNextEngine"
TAG="paks-latest"
TARGET_DIR="${1:-gkNextEngine}"
RELEASE_BASE="https://github.com/${REPO}/releases/download/${TAG}"

OS="$(uname -s)"
ARCH="$(uname -m)"
case "$OS/$ARCH" in
  Linux/x86_64)  PLATFORM="linux-amd64"  ; BIN="gnb-linux-amd64"  ;;
  Darwin/arm64)  PLATFORM="macos-arm64"  ; BIN="gnb-macos-arm64"  ;;
  Darwin/x86_64) PLATFORM="macos-amd64"  ; BIN="gnb-macos-amd64"  ;;
  *)
    echo "[gnb-init] unsupported platform: $OS/$ARCH" >&2
    echo "[gnb-init] manually download a gnb binary from $RELEASE_BASE and run `gnb init`." >&2
    exit 1
    ;;
esac

TMP_DIR="$(mktemp -d 2>/dev/null || mktemp -d -t gnb-init)"
trap 'rm -rf "$TMP_DIR"' EXIT
GNB="$TMP_DIR/gnb"

URL="$RELEASE_BASE/$BIN"
echo "[gnb-init] platform: $PLATFORM"
echo "[gnb-init] downloading $URL"
if command -v curl >/dev/null 2>&1; then
  curl -fL --retry 3 -o "$GNB" "$URL"
elif command -v wget >/dev/null 2>&1; then
  wget -O "$GNB" "$URL"
else
  echo "[gnb-init] need curl or wget" >&2
  exit 1
fi
chmod +x "$GNB"

if ! command -v git >/dev/null 2>&1; then
  echo "[gnb-init] git not found in PATH — install git first." >&2
  exit 1
fi

echo "[gnb-init] cloning gkNextEngine → $TARGET_DIR"
"$GNB" init "$TARGET_DIR"
