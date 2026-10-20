#!/bin/sh
# Install mtm (linux/arm64) from GitHub releases into /usr/local/bin.
# Usage:  curl -fsSL https://raw.githubusercontent.com/ckunte/mtm/master/inst.sh | sh
#         VERSION=v1.0.0 ./inst.sh
set -eu

REPO="ckunte/mtm"
ASSET="mtm-linux-arm64"
DEST="${DEST:-/usr/local/bin}"
VERSION="${VERSION:-latest}"

die() { echo "error: $*" >&2; exit 1; }

[ "$(uname -s)" = "Linux" ] || die "this installer is for Linux only"
case "$(uname -m)" in
  aarch64|arm64) ;;
  *) die "this installer is for arm64 only (found $(uname -m))" ;;
esac

command -v curl >/dev/null 2>&1 || die "curl is required"
if command -v sha256sum >/dev/null 2>&1; then
  sha() { sha256sum "$1" | cut -d' ' -f1; }
elif command -v shasum >/dev/null 2>&1; then
  sha() { shasum -a 256 "$1" | cut -d' ' -f1; }
else
  die "sha256sum or shasum is required"
fi

if [ "$VERSION" = "latest" ]; then
  BASE="https://github.com/$REPO/releases/latest/download"
else
  BASE="https://github.com/$REPO/releases/download/$VERSION"
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT INT TERM

echo "Downloading $ASSET ($VERSION)..."
curl -fsSL -o "$TMP/$ASSET" "$BASE/$ASSET" || die "failed to download $ASSET"
curl -fsSL -o "$TMP/$ASSET.sha256" "$BASE/$ASSET.sha256" || die "failed to download checksum"

expected="$(cut -d' ' -f1 "$TMP/$ASSET.sha256" | head -n1)"
actual="$(sha "$TMP/$ASSET")"
if [ -z "$expected" ] || [ "$expected" != "$actual" ]; then
  die "checksum mismatch (expected $expected, got $actual); nothing installed"
fi
echo "Checksum OK: $actual"

SUDO=""
if [ ! -w "$DEST" ] 2>/dev/null; then
  [ "$(id -u)" -eq 0 ] || { command -v sudo >/dev/null 2>&1 || die "need root or sudo to write to $DEST"; SUDO="sudo"; }
fi

$SUDO install -d "$DEST"
$SUDO install -m 0755 "$TMP/$ASSET" "$DEST/mtm"
echo "Installed $DEST/mtm"
