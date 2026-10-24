#!/bin/bash
# Install the exact firmware toolchain versions NiceDeck is built and verified
# against. Pinned so a rebuild on another machine produces the same binary
# behaviour. See README.md for the verified versions.
set -euo pipefail
ARDUINO_CLI="${ARDUINO_CLI:-arduino-cli}"

"$ARDUINO_CLI" core install esp32:esp32@3.3.11
"$ARDUINO_CLI" lib install "ArduinoJson@7.4.3"
"$ARDUINO_CLI" lib install "Adafruit GFX Library@1.12.6"
"$ARDUINO_CLI" lib install "Adafruit ST7735 and ST7789 Library@1.11.0"
"$ARDUINO_CLI" lib install "Adafruit BusIO@1.17.4"
# LVGL 9.3.0: the newest release verified against Arduino-ESP32 3.3.11 for this
# project. lv_conf.h lives in firmware/NiceDeck/ and is selected at build time
# with LV_CONF_INCLUDE_SIMPLE, so nothing is written outside the repository.
"$ARDUINO_CLI" lib install "lvgl@9.3.0"

echo "Installed. Versions:"
"$ARDUINO_CLI" core list | grep esp32 || true
"$ARDUINO_CLI" lib list | grep -Ei "ArduinoJson|Adafruit (GFX|ST7735|BusIO)|lvgl" || true
