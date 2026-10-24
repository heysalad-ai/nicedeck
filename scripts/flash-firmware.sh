#!/bin/bash
set -euo pipefail
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${1:?Usage: bash scripts/flash-firmware.sh /dev/cu.usbmodemPORT}"
# Explicit board identity is required; do not guess from a USB port name.
: "${NICEDECK_SERIAL:?Set NICEDECK_SERIAL to the USB serial number of your board}"
export NICEDECK_SERIAL
ARDUINO_CLI="${ARDUINO_CLI:-arduino-cli}"
FQBN='esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=custom'
if [[ ! -f "$PROJECT_DIR/build/firmware/NiceDeck.ino.bin" ]]; then
  bash "$PROJECT_DIR/scripts/build-firmware.sh"
fi
PORT="$(python3 "$PROJECT_DIR/scripts/prepare-upload.py" "$PORT" ${ALLOW_ROM:+--allow-rom})"
echo "Flashing $PORT"
"$ARDUINO_CLI" upload --fqbn "$FQBN" --port "$PORT" --input-dir "$PROJECT_DIR/build/firmware" "$PROJECT_DIR/firmware/NiceDeck"
