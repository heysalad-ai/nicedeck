#!/bin/bash
# Host tests for the touch stack.
#   1. touch-tests: the pure transform, geometry and contact filter.
#   2. lvgl-input-tests: the real LVGL event pipeline, built against the same
#      pinned LVGL sources the firmware uses and the same control factory.
# Neither links Arduino or USB, so no test can emit a keystroke.
set -euo pipefail
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
LVGL_DIR="${LVGL_DIR:-$HOME/Documents/Arduino/libraries/lvgl}"
BUILD="$PROJECT_DIR/build"
LVGL_OBJ="$BUILD/lvgl-host"
mkdir -p "$BUILD"

CXX="${CXX:-clang++}"
CC="${CC:-clang}"
STRICT=(-std=c++17 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion
        -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)

echo "== pure touch tests =="
"$CXX" "${STRICT[@]}" "$PROJECT_DIR/tests/touch-tests.cpp" -o "$BUILD/touch-tests"
"$BUILD/touch-tests"

if [[ ! -d "$LVGL_DIR/src" ]]; then
  echo "LVGL sources not found at $LVGL_DIR; run scripts/setup-firmware-deps.sh" >&2
  exit 1
fi

LV_FLAGS=(-DLV_CONF_INCLUDE_SIMPLE "-I$PROJECT_DIR/firmware/NiceDeck" "-I$LVGL_DIR")

# Compile the pinned LVGL once and cache the archive; it is unchanged between
# runs unless the pinned version or lv_conf.h changes.
STAMP="$LVGL_OBJ/.stamp"
CURRENT="$(cat "$LVGL_DIR/library.properties" 2>/dev/null | grep '^version=' || echo unknown)-$(shasum "$PROJECT_DIR/firmware/NiceDeck/lv_conf.h" | cut -d' ' -f1)"
if [[ ! -f "$BUILD/liblvgl-host.a" || "$(cat "$STAMP" 2>/dev/null || true)" != "$CURRENT" ]]; then
  echo "== building pinned LVGL for the host (cached afterwards) =="
  rm -rf "$LVGL_OBJ"; mkdir -p "$LVGL_OBJ"
  index=0
  while IFS= read -r source; do
    "$CC" -c "${LV_FLAGS[@]}" -g -O1 -o "$LVGL_OBJ/$index.o" "$source" &
    index=$((index + 1))
    if (( index % 16 == 0 )); then wait; fi
  done < <(find "$LVGL_DIR/src" -name '*.c')
  wait
  ar rcs "$BUILD/liblvgl-host.a" "$LVGL_OBJ"/*.o
  printf '%s' "$CURRENT" > "$STAMP"
fi

echo "== LVGL input tests =="
"$CXX" -std=c++17 -Wall -Wextra -g -O1 "${LV_FLAGS[@]}" \
  "$PROJECT_DIR/tests/lvgl-input-tests.cpp" "$BUILD/liblvgl-host.a" \
  -o "$BUILD/lvgl-input-tests"
"$BUILD/lvgl-input-tests"
