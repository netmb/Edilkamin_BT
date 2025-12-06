#!/usr/bin/env bash
set -euo pipefail

# PlatformIO environment name
ENV_NAME="wemos_d1_mini32"

# Output directory for the WebFlasher files
WEBFLASH_DIR="webflash"

# Determine firmware version (argument or Git tag/commit)
VERSION="${1:-}"

if [[ -z "$VERSION" ]]; then
  if git describe --tags --dirty --always >/dev/null 2>&1; then
    VERSION="$(git describe --tags --dirty --always)"
  else
    VERSION="dev-$(date +%Y%m%d-%H%M)"
  fi
fi

echo "Building firmware for environment: $ENV_NAME"
echo "Version: $VERSION"
echo

# Locate PlatformIO CLI (PIO)
PIO_CMD=""

if command -v platformio >/dev/null 2>&1; then
  PIO_CMD="platformio"
elif command -v pio >/dev/null 2>&1; then
  PIO_CMD="pio"
elif [[ -x "$HOME/.platformio/penv/bin/platformio" ]]; then
  PIO_CMD="$HOME/.platformio/penv/bin/platformio"
elif [[ -x "$HOME/.platformio/penv/bin/pio" ]]; then
  PIO_CMD="$HOME/.platformio/penv/bin/pio"
fi

if [[ -z "$PIO_CMD" ]]; then
  echo "Error: PlatformIO CLI not found."
  echo "Run this script inside the VS Code terminal or install the PlatformIO CLI globally."
  exit 1
fi

echo "Using PlatformIO command: $PIO_CMD"
echo

# Build firmware using PlatformIO
"$PIO_CMD" run -e "$ENV_NAME"

BUILD_DIR=".pio/build/$ENV_NAME"

# Verify build artifacts exist
BOOTLOADER="$BUILD_DIR/bootloader.bin"
PARTITIONS="$BUILD_DIR/partitions.bin"
FIRMWARE="$BUILD_DIR/firmware.bin"

for f in "$BOOTLOADER" "$PARTITIONS" "$FIRMWARE"; do
  if [[ ! -f "$f" ]]; then
    echo "Error: Expected file not found: $f"
    echo "Check that the PlatformIO environment '$ENV_NAME' is correct."
    exit 1
  fi
done

echo "Build files found:"
echo "  $BOOTLOADER"
echo "  $PARTITIONS"
echo "  $FIRMWARE"
echo

# Create/refresh the webflash directory
mkdir -p "$WEBFLASH_DIR"

cp "$BOOTLOADER" "$WEBFLASH_DIR/bootloader.bin"
cp "$PARTITIONS" "$WEBFLASH_DIR/partitions.bin"
cp "$FIRMWARE"  "$WEBFLASH_DIR/firmware.bin"

echo "Copied build files into $WEBFLASH_DIR/"
echo

# Generate manifest.json for esp-web-tools / web.esphome.io
MANIFEST_PATH="$WEBFLASH_DIR/manifest.json"

cat > "$MANIFEST_PATH" <<EOF
{
  "name": "Edilkamin_BT Firmware",
  "version": "$VERSION",
  "builds": [
    {
      "chipFamily": "ESP32",
      "parts": [
        { "path": "bootloader.bin", "offset": 4096 },
        { "path": "partitions.bin", "offset": 32768 },
        { "path": "firmware.bin",  "offset": 65536 }
      ]
    }
  ]
}
EOF

echo "Generated manifest.json at $MANIFEST_PATH"
echo

# Commit and push changes to Git only if something changed
if git diff --quiet && git diff --cached --quiet; then
  echo "No changes detected. Nothing to commit."
  exit 0
fi

git add "$WEBFLASH_DIR/bootloader.bin" \
        "$WEBFLASH_DIR/partitions.bin" \
        "$WEBFLASH_DIR/firmware.bin" \
        "$MANIFEST_PATH"

git commit -m "Update webflash firmware ($VERSION)"

CURRENT_BRANCH="$(git rev-parse --abbrev-ref HEAD)"
git push origin "$CURRENT_BRANCH"

echo
echo "Done. WebFlasher URL:"
echo "https://web.esphome.io/?manifest=https://raw.githubusercontent.com/netmb/Edilkamin_BT/$CURRENT_BRANCH/$WEBFLASH_DIR/manifest.json"
echo