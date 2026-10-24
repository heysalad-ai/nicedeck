# NiceDeck protocol v1

Transport: USB CDC, 115200 baud, newline-delimited compact UTF-8 JSON. Native USB HID keyboard and consumer controls share the USB device. No Wi-Fi required.

Firmware 1.1.0 rebuilds the on-device interface on LVGL 9.3.0 and replaces the tap recogniser; the configuration format, flash partitions and every command below are unchanged, so 1.0.x clients keep working. Its one behaviour change is that swipe navigation is gone: pages change through the on-screen arrows, a page action, the BOOT button or the host. `touchSwipes` is still reported, always as 0.

Firmware 1.0.2 renames the product to NiceDeck without changing configuration format or flash partitions. Its hello identity and USB product/manufacturer names are `NiceDeck`. Clients also accept the legacy `OpenStreamDeck` identity with protocol 1 so existing boards can be upgraded. Identify the device through the hello exchange, not its USB product descriptor: older firmware may enumerate as `ESP32S3_DEV`, and the ROM download port retains Espressif's own identity. USB descriptor names are set by `firmware/NiceDeck/build_opt.h` when compiling the Arduino core because CDC-on-boot initializes USB before sketch setup.

Configuration shape (persist entire document on host and board):
```json
{"version":1,"brightness":80,"sleepSeconds":300,"pages":[{"id":"main","name":"Everyday","buttons":[{"id":"b1","label":"Copy","color":"#4466EE","icon":"copy","image":"","actions":[{"type":"hotkey","value":"cmd+c","delayMs":0}]}]}]}
```

Constraints: 1–8 pages; exactly six buttons per page; unique nonempty page IDs (max 32 ASCII characters) and button IDs (max 32 ASCII characters, unique within page); names and labels max 24 printable ASCII characters; color #RRGGBB; brightness 5–100; sleepSeconds 0 (never) or 30–3600; max 8 actions/button. Action values max 512 UTF-8 bytes, delayMs 0–5000. Entire compact config <=280000 bytes. Built-in icons: key, copy, paste, mic, video, play, volume, music, globe, app, terminal, bolt, folder, star, heart, gear. Empty image or base64 of exactly 3200 bytes (40x40 pixels, RGB565 big endian). Images rendered at 40x40.

Action types: none, hotkey, text, media, url, app, shell, shortcut, page, delay. Hotkey e.g. cmd+shift+4, ctrl+alt+delete, F13. Modifiers cmd/gui/meta/super, ctrl/control, alt/option, shift. Keys letters, digits, single ASCII punctuation, space, enter/return, tab, escape/esc, backspace, delete, up/down/left/right, home/end, pageup/pagedown, F1–F24. Text supports printable ASCII and newline/tab for firmware HID; host validation rejects other characters for this type. Media values: play_pause, next, previous, mute, volume_up, volume_down, stop. URL must http(s). App value is macOS application name or absolute .app path. Shell value is explicit user-authored shell command; default profiles must contain no shell actions. Shortcut is a macOS Shortcuts name. Page action value is target page ID. Delay action uses delayMs (value empty). All actions support delayMs as a delay before execution. Navigation action ends the current sequence.

Host -> board:
* {"op":"hello","id":"requestID"}
* {"op":"config","id":"requestID","config":{...}}
* {"op":"page","id":"requestID","page":0}
* {"op":"ping","id":"requestID"}
* {"op":"status","id":"requestID"}
* {"op":"get_config","id":"requestID"}
* {"op":"screenshot","id":"requestID"} optional diagnostic RGB565 screenshot (no action execution). Under LVGL there is no standing framebuffer, so this re-renders the live screen into one.
* {"op":"touch_test","id":"requestID","mode":"motion|targets|stream|stop_stream|status|exit"} no-action touch diagnostics.
* {"op":"host_action_result","id":"actionID","ok":true} after a host action completes; ok:false plus error cancels the sequence. Board waits at most 30 seconds, then cancels.

Board -> host:
* {"event":"hello","id":"requestID","protocol":1,"firmware":"1.1.0","device":"NiceDeck","pages":2,"page":0,"touch":true,"hid":true,"orientation":"usb-down","rotation":2,"ui":"lvgl-9.3.0"} (on hello; unsolicited connection hello uses an empty id). Identify by `device` and `protocol`; `firmware`, `orientation`, `rotation` and `ui` are informational.
* {"event":"ack","id":"requestID","op":"config","ok":true} after validated, persisted and rendered; ok:false with error for failures. Ack page/ping similarly.
* {"event":"status","id":"requestID","page":0,"pages":2,"touch":true,"hid":true,"freeHeap":123,"uptime":1000}
* {"event":"config","id":"requestID","config":{...}}
* {"event":"press","page":0,"button":0} on touchscreen tap before action sequence.
* {"event":"host_action","id":"actionID","page":0,"button":0,"step":0} for url/app/shell/shortcut. Host MUST resolve indices only against last config acknowledged by that board; never accept command text from serial input. Execute only while connection is identified as protocol 1 NiceDeck (or legacy OpenStreamDeck). On restart before any sync, get_config establishes the acknowledged config, and host commands are allowed only when they match the locally saved action at those indices. Explicit Sync authorizes the sent snapshot. Host serial read loop must not execute actions synchronously. Reply host_action_result when completed (including failures).
* {"event":"page","page":0} after on-device page navigation.
* {"event":"error","error":"..."} for parse/runtime errors.
* Screenshot: {"event":"screenshot_begin","id":"requestID","width":240,"height":320,"encoding":"rgb565be"}, followed by {"event":"screenshot_chunk","id":"requestID","offset":0,"data":"base64"} chunks (offset in bytes), then {"event":"screenshot_end","id":"requestID"}. Combined bytes must total 153600; verify no missing chunks.

## Touch diagnostics (firmware 1.1.0)

`touch_test` puts the device on a diagnostic screen that has no controls and no event handlers, and `startSequence()` refuses to run while it is active. Neither HID nor a host action can be produced in this mode. Test mode survives a serial disconnect, so an interrupted run does not silently re-enable shortcuts; a power cycle leaves it.

Modes: `motion` counts contacts while nothing should be touching the glass; `targets` runs the five-point alignment check; `stream` additionally emits every conditioned sample; `stop_stream` ends streaming; `status` reads counters without resetting them; `exit` returns to the deck and re-enables shortcuts. `motion`, `targets` and `exit` reset the counters.

Every mode replies with:
```json
{"event":"touch_test","id":"requestID","enabled":true,"mode":"targets","streaming":false,
 "contacts":0,"downSamples":0,"readErrors":0,"bootPresses":0,"attempts":0,
 "rawX":0,"rawY":0,"x":0,"y":0,"down":false,
 "minIntervalMs":0,"maxIntervalMs":0,"irqs":0,"polls":0,"lastReject":"none",
 "target":0,"hits":0,"radiusPx":28,"calibrationApplied":false,
 "points":[{"expectedX":40,"expectedY":60,"measured":false,"x":0,"y":0,"dx":0,"dy":0}]}
```
`rawX`/`rawY` are the controller's own coordinates and `x`/`y` the mapped screen position, so a mapping fault can be separated from a sensor fault. `lastReject` is why the last contact was refused: `none`, `gated` (wake, reconnect, configuration apply, page change or diagnostics), `short-pulse` (an isolated sample, not a finger), `read-loss` (sustained I2C failure) or `outlier` (one implausible coordinate jump). `target` is the current alignment target, or the target count once complete, or -1 outside alignment mode.

The alignment check uses its own radius test, independent of the button hit map, so a broken interface layout cannot block it. `points[].dx`/`dy` are the measured error in pixels for inspection only: **no correction is derived, applied or stored**, and `calibrationApplied` is always false. This verifies alignment; it does not calibrate it.

While streaming, each conditioned sample is emitted as:
```json
{"event":"touch_sample","t":1000,"dt":8,"valid":true,"down":true,"rawX":150,"rawY":90,"x":89,"y":229,"pressed":true}
```
`t` is device milliseconds, `dt` the interval since the previous sample, `valid` whether the I2C read succeeded, `down` the controller's finger count, and `pressed` what the interface was told after filtering.

Firmware 1.0.1 status also includes `touchConfigured` (verified automatic-sleep disable), `touchPolls`, `touchIRQs`, `touchStarts`, `touchTaps`, `touchRejected`, `touchSwipes`, `touchFailures`, `presses`, `maxButtonDrawUs`, and `maxFullDrawUs`. Counters reset on reboot. In firmware 1.1.0 `touchStarts` counts confirmed contacts, `touchTaps` counts deliberate activations delivered by LVGL, `touchRejected` counts contacts refused before they became a press, and `presses` counts accepted action sequences (taps during an active sequence are not queued). `touchSwipes` is always 0. `maxButtonDrawUs` is the longest single display flush and `maxFullDrawUs` the longest interface update, both in microseconds; `diagnostic` reports whether test mode is active.

Boot with a useful default config (Everyday hotkeys + Media page). Header title and page number; 2 columns x 3 rows of 108x70 tiles on the 240x320 portrait screen; bottom previous/next page controls. Touch regions extend 4 pixels into the surrounding gap so neighbouring targets meet without overlapping and no strip between them is dead.

An action runs only when the finger is released inside the tile it pressed. Sliding onto another tile or off the grid cancels the whole interaction rather than transferring it. A contact must persist about 18ms to become a press, which rejects isolated spurious samples; a gap of up to 40ms in the controller's reports does not split one touch into several; and a contact held beyond 1200ms is a hold, not a tap, and runs nothing. Movement within the pressed tile is not penalised. There is no swipe navigation and no travel limit.

No action while booting, saving config, changing page, waking or on reconnect: each of these refuses new contacts until the finger has been seen off the glass. Tap wakes a sleeping screen without firing an action. BOOT advances the page. No HID activity from serial diagnostics or test mode.

Mac app: native SwiftUI, Swift 5 language mode, no external packages, macOS 13+. Left sidebar pages add/rename/delete; centre device preview / button selection; right editor appearance + multiple actions. Save locally immediately/automatically; explicit Sync sends config to board and waits for ack. JSON import/export, icon image chooser, brightness/sleep settings, port auto-discovery with reconnect. Clear offline/sync/error status. Host config saved at ~/Library/Application Support/NiceDeck/config.json, migrated once from the legacy OpenStreamDeck folder when the new file is absent; localhost server not needed. Buildable using xcrun swiftc.
