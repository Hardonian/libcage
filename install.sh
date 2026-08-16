#!/bin/sh
# Libcage installer — curl|sh
# Usage: curl -fsSL https://raw.githubusercontent.com/Hardonian/libcage/main/install.sh | sh
# Downloads the latest libcage release binary for this platform into ~/.local/bin.
set -e

REPO="Hardonian/libcage"
INSTALL_DIR="${HOME}/.local/bin"
BIN="libcage"

# Detect platform
OS="$(uname -s)"; ARCH="$(uname -m)"
case "$OS" in
  Linux)  PLAT="linux-amd64" ;;
  Darwin) PLAT="macos-amd64" ;;
  *) echo "Unsupported OS: $OS" >&2; exit 1 ;;
esac

echo "Libcage installer: detecting $OS/$ARCH -> $PLAT"

# Fetch latest release tag via GitHub API
TAG="$(curl -fsSL "https://api.github.com/repos/${REPO}/releases/latest" \
      | tr ',' '\n' | grep '"tag_name"' | head -1 | sed -E 's/.*:[[:space:]]*"([^"]+)".*/\1/')"
if [ -z "$TAG" ]; then echo "Could not determine latest release" >&2; exit 1; fi
echo "Latest release: $TAG"

URL="https://github.com/${REPO}/releases/download/${TAG}/libcage-${PLAT}"
echo "Downloading $URL"

mkdir -p "$INSTALL_DIR"
TMP="$(mktemp)"
curl -fsSL "$URL" -o "$TMP"
chmod +x "$TMP"
mv "$TMP" "${INSTALL_DIR}/${BIN}"
echo "Installed: ${INSTALL_DIR}/${BIN}"
"${INSTALL_DIR}/${BIN}" --version 2>/dev/null || "${INSTALL_DIR}/${BIN}" 2>&1 | head -1
echo "Done. Run: libcage \"fix this\" file.c \"cc -o out file.c\""
