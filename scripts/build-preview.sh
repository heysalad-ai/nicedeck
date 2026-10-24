#!/bin/bash
# Builds the off-device screen renderer against the pinned LVGL.
set -euo pipefail
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
LVGL_DIR="${LVGL_DIR:-$HOME/Documents/Arduino/libraries/lvgl}"
if [[ ! -f "$PROJECT_DIR/build/liblvgl-host.a" ]]; then
  bash "$PROJECT_DIR/scripts/test-touch.sh" >/dev/null
fi
"${CXX:-clang++}" -std=c++17 -g -O1 -DLV_CONF_INCLUDE_SIMPLE \
  "-I$PROJECT_DIR/firmware/NiceDeck" "-I$LVGL_DIR" \
  "$PROJECT_DIR/tools/deck-preview.cpp" "$PROJECT_DIR/build/liblvgl-host.a" \
  -o "$PROJECT_DIR/build/deck-preview"
echo "Built $PROJECT_DIR/build/deck-preview"
