#!/bin/bash
set -euo pipefail
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SKETCH_DIR="$PROJECT_DIR/firmware/NiceDeck"
ARDUINO_CLI="${ARDUINO_CLI:-arduino-cli}"
FQBN='esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=custom'
# LVGL reads its configuration through LV_CONF_INCLUDE_SIMPLE, which makes
# lv_conf_internal.h include "lv_conf.h" from the include path. Adding the
# sketch directory keeps the pinned configuration inside the repository instead
# of in the user's Arduino libraries folder.
LV_FLAGS="-DLV_CONF_INCLUDE_SIMPLE -I$SKETCH_DIR"
mkdir -p "$PROJECT_DIR/build/firmware"
"$ARDUINO_CLI" compile --fqbn "$FQBN" \
  --build-property "compiler.c.extra_flags=$LV_FLAGS" \
  --build-property "compiler.cpp.extra_flags=$LV_FLAGS" \
  --build-path "$PROJECT_DIR/build/firmware" "$SKETCH_DIR"
