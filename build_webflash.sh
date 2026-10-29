#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
ENV_NAME="wemos_d1_mini32"
PIO_CMD="${PIO_CMD:-}"
if [[ -z "$PIO_CMD" ]]; then
  for candidate in "$(command -v pio || true)" "$(command -v platformio || true)" "$HOME/.platformio/penv/bin/pio"; do
    if [[ -x "$candidate" ]]; then PIO_CMD="$candidate"; break; fi
  done
fi
if [[ -z "$PIO_CMD" ]]; then echo "PlatformIO missing; set PIO_CMD to its executable." >&2; exit 1; fi
PIO_PYTHON="${PIO_PYTHON:-python3}"
CORE_DIR="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}"
"$PIO_CMD" run -e "$ENV_NAME"
# Build a full-install image using the same DIO/40MHz/4MB settings as this board.
"$PIO_PYTHON" "$CORE_DIR/packages/tool-esptoolpy/esptool.py" --chip esp32 merge_bin \
  -o webflash/merged-firmware.bin --flash_mode dio --flash_freq 40m --flash_size 4MB \
  0x1000 ".pio/build/$ENV_NAME/bootloader.bin" \
  0x8000 ".pio/build/$ENV_NAME/partitions.bin" \
  0xe000 "$CORE_DIR/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin" \
  0x10000 ".pio/build/$ENV_NAME/firmware.bin"
"$PIO_PYTHON" tools/package_webflash.py
printf '%s\n' 'Webflasher artifacts built and checksummed. Review, commit and push explicitly.'
