# NiceDeck

NiceDeck by HeySalad. Created by Peter Machona.

**Prototype source release.** Firmware 1.1.0 has host-test evidence but has not been verified on the physical device. Final v3b screw fit also needs physical confirmation.

Previously named OpenStreamDeck. The NiceDeck app imports your existing Mac settings on first launch and retains the original configuration as a backup. The workspace folder keeps its original name so existing file links remain valid.

A custom touchscreen shortcut deck for the **Waveshare ESP32-S3-Touch-LCD-2**, with a native macOS editor. Connect one USB data cable; no Wi-Fi, account, or subscription is required.

## Start using it

Double-click **NiceDeck.command** in this folder, or open **build/NiceDeck.app**. The editor discovers the board automatically.

1. Select an Everyday or Media page, then click a button in the preview.
2. Choose its label, colour, built-in icon, or your own image.
3. Add actions and put them in the order you want. Each action can have a delay before it runs.
4. Click **Sync to deck** (⌘S). The footer confirms when the board has saved the configuration.
5. Tap the physical touchscreen. Use the screen’s bottom arrows or press the board’s BOOT button to change pages.

A button runs when you lift your finger on the same button. Sliding off it or holding it down is designed to cancel a press. Movement-induced false contacts and resting fingers still require testing on the physical hardware.

Edits are automatically saved on the Mac. Sync applies the entire configuration to the board, which remembers it after unplugging. The first tap after screen sleep only wakes the display.

Swipe navigation was removed in firmware 1.1.0. On a screen this size a swipe begins on a button and is the gesture most likely to fire something you did not mean; the arrows, the BOOT button and page actions all remain.

## Customisation

- **1–8 pages**, with **six buttons on each page** (up to 48 buttons). Add, rename, reorder, and remove pages in the sidebar.
- Label, any RGB colour, 16 built-in icons, and image upload. Images are fitted to 40×40 pixels and stored on the board. Use a square PNG for best results; transparent areas become black.
- Up to **eight actions per button**, with delays from 0 to 5 seconds per action. Move actions using their menu. Swap a button’s position with the arrows; duplicate/reset it from the button menu.
- Brightness and sleep settings in the top-right settings button.
- JSON import/export for backups, sharing, and editing outside the app.

| Action | Example | Mac editor needed during use? |
|---|---|---|
| Keyboard shortcut | `cmd+shift+4`, `cmd+c`, `ctrl+alt+delete`, `F13` | No |
| Type text | ASCII text, tabs, newlines | No |
| Media control | Play/pause, next/previous, mute, volume | No |
| Switch page | Another page in this deck | No |
| Wait | Delay before the next step | No |
| Open website | `https://example.com` | Yes |
| Open application | `Safari` or `/Applications/YourApp.app` | Yes |
| Run Apple Shortcut | Exact name in macOS Shortcuts | Yes |
| Run shell command | Your own command, executed with `/bin/zsh -c` | Yes |

For an application followed by a keyboard shortcut, set a delay (for example 500–1000 ms) on the keyboard action to give the app time to become ready. The board waits for host actions to complete before proceeding. Host commands time out after 25 seconds; the device cancels an unanswered host action after 30 seconds. A page action ends its sequence. Additional taps while a sequence is running are ignored.

Keyboard actions use **US keyboard mapping**, so punctuation can differ when macOS uses another input layout. Labels/page names support up to 24 printable ASCII characters; typed text supports up to 512 ASCII bytes plus tab/newline. Hotkeys include Command/Control/Option/Shift, arrows, navigation keys, and F1–F24. This is an independent open-source deck; Elgato plugins/profiles are not compatible. OBS and other applications can be controlled through their assigned keyboard shortcuts; there is no dedicated OBS status integration.

The companion runs locally and has no network listener. Shell commands execute only when their assigned physical button is pressed; importing a configuration never executes it. After reconnect, host actions must match the local saved configuration, or you must Sync to authorise that configuration. Keep the editor open for these actions. You can add the built app to macOS Login Items if you want it to open at login.

## Build and flash

The current host cannot rebuild firmware because its installed Arduino `ctags` binary reports `bad CPU type in executable`. The earlier build is historical evidence only; no firmware binaries are published. Use a compatible Arduino toolchain and consult `VERIFICATION.txt`.

The Mac app targets Apple silicon and macOS 13+. Building requires the Xcode command-line tools. It uses SwiftUI/AppKit without package dependencies.

```sh
bash scripts/build-mac.sh
bash scripts/test-model.sh
bash scripts/test-touch.sh
python3 -m unittest discover -s tests -p 'test_prepare_upload.py'
```

Firmware was built with Arduino CLI 1.5.1, Espressif Arduino core **3.3.11**, ArduinoJson **7.4.3**, Adafruit GFX **1.12.6**, Adafruit ST7735 and ST7789 **1.11.0**, Adafruit BusIO **1.17.4**, and **LVGL 9.3.0**. Install the pinned versions with `scripts/setup-firmware-deps.sh`. The LVGL configuration lives in `firmware/NiceDeck/lv_conf.h` and is selected at build time, so nothing is written outside this folder.

```sh
bash scripts/setup-firmware-deps.sh
bash scripts/build-firmware.sh
NICEDECK_SERIAL=YOUR_BOARD_SERIAL bash scripts/flash-firmware.sh /dev/cu.usbmodemPORT
```

Check which board you are flashing first — other ESP32 boards appear as `/dev/cu.usbmodem*` too:

```sh
ioreg -p IOUSB -w0 -l | grep -E '"(USB Product Name|USB Serial Number)"'
```

The flash script refuses any port that does not identify itself as a NiceDeck over the protocol handshake, and requires you to set the serial number in `NICEDECK_SERIAL`. A board already sitting in ROM download mode cannot answer that handshake; confirm which board it is, then re-run with `ALLOW_ROM=1`.

Close the Mac editor before flashing or using the diagnostic script. The USB port may change after flashing. If automatic reset fails, hold **BOOT**, tap **RESET**, release **BOOT**, then retry on the newly listed `/dev/cu.usbmodem*` port. Normal use resumes after reset/replugging.

The firmware uses native USB-OTG/TinyUSB CDC + keyboard and consumer HID, 16 MB flash, OPI PSRAM, and the included custom partition table. Do not change USB mode to hardware CDC/JTAG: it cannot provide the required keyboard interface. Build output includes separate binaries and a merged 16 MB image. The flash script writes only firmware/partition regions, preserving the deck’s settings on subsequent updates.

## Before first installation

Back up your board's original flash and settings before replacing its firmware. No personal flash dumps or configuration backups are distributed here. For a stock board that cannot answer the NiceDeck handshake, identify its serial number and port, enter ROM download mode with BOOT/RESET, then explicitly set `ALLOW_ROM=1` alongside `NICEDECK_SERIAL` for the flash command. This override relies on you selecting the correct board.

## Diagnostics and files

`scripts/deck-device.py` requires Python 3 and `pyserial`. It supports `status`, `get-config FILE`, `sync FILE`, `screenshot FILE.ppm`, and `smoke-test`. None of these trigger keyboard or host actions. The smoke test temporarily changes a page name, verifies saving/readback and page navigation, and restores the previous configuration.

```sh
python3 scripts/deck-device.py status
python3 scripts/deck-device.py get-config build/device-config.json
python3 scripts/deck-device.py smoke-test
python3 scripts/deck-device.py screenshot build/device-screen.ppm
sips -s format png build/device-screen.ppm --out build/device-screen.png
```

- `firmware/NiceDeck/`: display, touch, HID, persistence, and USB protocol implementation.
- `VERIFICATION.txt`: release checks and outstanding hardware validation.
- `macos/`: native editor, config validation, serial connection, and host action executor.
- `PROTOCOL.md`: configuration and serial protocol reference.
- `~/Library/Application Support/NiceDeck/config.json`: Mac configuration draft.
- `build/`: application bundle, firmware binaries, and verification outputs.

Hardware wiring was verified against [Waveshare’s official Arduino examples](https://docs.waveshare.com/ESP32-S3-Touch-LCD-2/Arduino) and [board schematic](https://files.waveshare.com/wiki/ESP32-S3-LCD-2/ESP32-S3-Touch-LCD-2-SchDoc.pdf). LCD SPI pins: SCK 39, MOSI 38, MISO 40, DC 42, CS 45, backlight 1. SD card chip select is **41**, held high when unused. Touch I²C: SDA 48, SCL 47, address 0x15, interrupt **46** configured as an input. Display is ST7789T3 at 240×320; touch is CST816D.

Firmware 1.1.0 draws the interface with LVGL 9.3.0 using partial rendering, so the longest display transfer is about 3ms instead of the 59ms full-screen redraw earlier versions performed on every change. Touch is read on the controller's interrupt with an 8ms polling fallback; a contact filter decides only whether a finger is down and where, and LVGL decides what counts as a deliberate press. The status command exposes touch and render counters for diagnosis. The Mac app includes a cream icon with six pastel keys; regenerate it with `xcrun swift scripts/make-app-icon.swift assets`.

## Checking the touchscreen

Open **Settings → Touch check** in the Mac app, or run `scripts/touch-test.py`. The device shows a diagnostic screen with shortcuts switched off, so you can test touch without anything running. It reports raw and mapped coordinates, contact state, sample timing, read errors, and why a contact was refused.

The movement check counts contacts while you move the case and cable without touching the glass. The alignment check asks you to tap five crosshairs and reports how far each tap landed from its target. That is a measurement only — no correction is calculated or saved.

```sh
python3 scripts/touch-test.py guided --port /dev/cu.usbmodemPORT
python3 scripts/touch-test.py stream --port /dev/cu.usbmodemPORT --seconds 20 --output build/trace.json
```

You can also render the deck's screens on the Mac, without the device:

```sh
bash scripts/build-preview.sh
python3 scripts/render-preview.py
```

## Current printable enclosure

Use `enclosure/output-clip-v3/shell.stl`, `enclosure/output-screw-fix-v3b/NiceDeck-carrier-10mm-screw-fix.stl`, and `enclosure/output/desk_cradle.stl`. Read [the screw-fit instructions](enclosure/output-screw-fix-v3b/PRINT-ME.md). Editable CadQuery scripts and STEP exports are included.

The manufacturer board model is not redistributed here. Its original download URL and expected model hash are recorded in `enclosure/reference/source.json`. To regenerate all CAD, obtain that model, place it at the recorded path, and run `build_enclosure.py`, `build_clip_enclosure.py`, then `build_screw_reach_fix.py` in that order after installing the enclosure requirements.

## Python tools

Use Python 3 with a virtual environment: `python3 -m venv .venv`, `source .venv/bin/activate`, then `python -m pip install -r requirements.txt`. The diagnostic and flashing preparation tools require pyserial.

## Licence and credits

The project source, original icon artwork and enclosure designs are provided under the [MIT licence](LICENSE). Existing OpenStreamDeck contributor attribution is preserved because NiceDeck is the same project under a new name. See [third-party notices](THIRD_PARTY_NOTICES.txt) for separately installed dependencies. HeySalad and NiceDeck names do not imply endorsement of forks.

This source release does not include a notarized Mac installer or a hardware-validated firmware release.
