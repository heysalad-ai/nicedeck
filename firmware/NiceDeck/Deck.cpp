#include <Arduino.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <LittleFS.h>
#include <USB.h>
#include <USBHIDKeyboard.h>
#include <USBHIDConsumerControl.h>
#include <tusb.h>
#include <esp_heap_caps.h>
#include <mbedtls/base64.h>
#include <deque>

#include "DeckIcons.h"
#include "DeckLayout.h"
#include "DeckTouch.h"
#include "DeckUI.h"

#if ARDUINO_USB_MODE != 0 || ARDUINO_USB_CDC_ON_BOOT != 1
#error "Select USB-OTG (TinyUSB), with USB CDC On Boot enabled."
#endif

namespace {
// Verified against Waveshare's ESP32-S3-Touch-LCD-2 documentation and board
// schematic. LCD SPI pins live in DeckUI.cpp with the panel driver.
constexpr int TOUCH_SDA = 48, TOUCH_SCL = 47, TOUCH_INT = 46, SD_CS = 41, LCD_BL = 1;
constexpr int DECK_BOOT_PIN = 0;
constexpr uint8_t TOUCH_ADDR = 0x15;
constexpr size_t MAX_CONFIG = 280000, MAX_LINE = MAX_CONFIG + 512;

class PsramAllocator : public ArduinoJson::Allocator {
public:
  void *allocate(size_t size) override {
    void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : heap_caps_malloc(size, MALLOC_CAP_8BIT);
  }
  void deallocate(void *p) override { heap_caps_free(p); }
  void *reallocate(void *p, size_t size) override {
    if (!p) return allocate(size);
    return heap_caps_realloc(p, size, MALLOC_CAP_8BIT);
  }
} allocator;

USBHIDKeyboard keyboard;
USBHIDConsumerControl consumer;
USBHID hid;
JsonDocument config(&allocator);

bool fsReady = false, touchOK = false, sleeping = false, serialWasConnected = false;
bool touchConfigured = false, uiReady = false;
int currentPage = 0;
uint32_t lastActivity = 0, lastTouchPoll = 0, lastBootEdge = 0, ignoreInputUntil = 0, lastTouchInit = 0;
uint32_t lastTouchSuccess = 0, touchFailures = 0, pressCount = 0;
uint32_t touchPolls = 0, touchTaps = 0;
uint32_t maxFlushUs = 0, maxRenderUs = 0;
volatile bool touchIRQPending = false;
volatile uint32_t touchIRQs = 0;
bool bootWasDown = false, bootArmed = false;

decktouch::ContactFilter touch;

void ARDUINO_ISR_ATTR touchInterrupt() { touchIRQPending = true; touchIRQs++; }

// --- Diagnostics -----------------------------------------------------------
// A no-action mode. It never calls startSequence(), and its LVGL screen has no
// event handlers attached at all, so neither a code path nor a stray pointer
// event can produce HID or a host action.
enum class Diag : uint8_t { Off, Motion, Alignment };
Diag diagnostic = Diag::Off;
bool diagStream = false;
uint32_t diagContacts = 0, diagSamples = 0, diagFailures = 0, diagBootPresses = 0;
uint32_t diagMinInterval = 0, diagMaxInterval = 0, diagLastSampleAt = 0;
int diagRawX = 0, diagRawY = 0, diagX = 0, diagY = 0;
bool diagHaveError = false;
int diagTarget = -1, diagHits = 0, diagErrorX = 0, diagErrorY = 0;
int diagMeasuredX[decklayout::AlignmentTargets] = {};
int diagMeasuredY[decklayout::AlignmentTargets] = {};
bool diagMeasured[decklayout::AlignmentTargets] = {};
uint32_t diagAttempts = 0;

const char *rejectName(decktouch::Reject reject) {
  switch (reject) {
    case decktouch::Reject::Gated: return "gated";
    case decktouch::Reject::ShortPulse: return "short-pulse";
    case decktouch::Reject::Stale: return "read-loss";
    case decktouch::Reject::Outlier: return "outlier";
    default: return "none";
  }
}

// --- Serial transport ------------------------------------------------------
char *rx = nullptr;
size_t rxLength = 0;
bool rxDiscard = false;
uint32_t rxLastByte = 0;

struct Tx { char *data; size_t length; size_t sent; };
std::deque<Tx> txQueue;
size_t txBytes = 0;
void clearTx() { for (auto &t : txQueue) allocator.deallocate(t.data); txQueue.clear(); txBytes = 0; }
bool enqueue(JsonDocument &doc) {
  if (!Serial) return false;
  size_t n = measureJson(doc);
  if (txQueue.size() >= 24 || txBytes + n > 600000) return false;
  char *p = (char *)allocator.allocate(n + 2);
  if (!p) return false;
  size_t actual = serializeJson(doc, p, n + 1); p[actual++] = '\n';
  txQueue.push_back({p, actual, 0}); txBytes += actual; return true;
}
void pumpTx() {
  if (!Serial || txQueue.empty()) return;
  auto &t = txQueue.front();
  int available = Serial.availableForWrite();
  if (available <= 0) return;
  size_t n = min((size_t)available, min((size_t)512, t.length - t.sent));
  t.sent += Serial.write((uint8_t *)t.data + t.sent, n);
  if (t.sent == t.length) { txBytes -= t.length; allocator.deallocate(t.data); txQueue.pop_front(); }
}
void errorEvent(const char *error) { JsonDocument d; d["event"] = "error"; d["error"] = error; enqueue(d); }
void ack(const char *id, const char *op, bool ok, const char *error = nullptr) {
  JsonDocument d; d["event"] = "ack"; d["id"] = id; d["op"] = op; d["ok"] = ok;
  if (error) d["error"] = error; enqueue(d);
}
void hello(const char *id) {
  JsonDocument d; d["event"] = "hello"; d["id"] = id; d["protocol"] = 1; d["firmware"] = "1.1.0";
  d["orientation"] = "usb-down"; d["rotation"] = 2; d["ui"] = "lvgl-9.3.0";
  d["device"] = "NiceDeck"; d["pages"] = config["pages"].size(); d["page"] = currentPage;
  d["touch"] = touchOK; d["hid"] = tud_mounted(); enqueue(d);
}

// --- Configuration ---------------------------------------------------------
bool printable(const char *s, size_t maxLength, bool allowEmpty = true) {
  if (!s || strlen(s) > maxLength || (!allowEmpty && !*s)) return false;
  for (const unsigned char *p = (const unsigned char *)s; *p; p++) if (*p < 32 || *p > 126) return false;
  return true;
}
bool oneOf(const String &s, const char *list) {
  String hay = "|"; hay += list; hay += "|"; return hay.indexOf("|" + s + "|") >= 0;
}
bool identifier(const char *s) {
  if (!printable(s, 32, false)) return false;
  for (const unsigned char *p = (const unsigned char *)s; *p; p++) if (*p == 32) return false;
  return true;
}
bool validImage(const char *s) {
  if (!s) return false;
  size_t n = strlen(s); if (!n) return true;
  if (n != 4268) return false;
  for (size_t i = 0; i < n - 1; i++) if (!isalnum((unsigned char)s[i]) && s[i] != '+' && s[i] != '/') return false;
  if (s[n - 1] != '=') return false;
  uint8_t decoded[3200]; size_t len = 0;
  return mbedtls_base64_decode(decoded, sizeof(decoded), &len, (const uint8_t *)s, n) == 0 && len == 3200;
}
uint8_t namedKey(const String &key) {
  if (key == "space") return 0x2c; if (key == "enter" || key == "return") return 0x28;
  if (key == "tab") return 0x2b; if (key == "escape" || key == "esc") return 0x29;
  if (key == "backspace") return 0x2a; if (key == "delete") return 0x4c;
  if (key == "up") return 0x52; if (key == "down") return 0x51;
  if (key == "left") return 0x50; if (key == "right") return 0x4f;
  if (key == "home") return 0x4a; if (key == "end") return 0x4d;
  if (key == "pageup") return 0x4b; if (key == "pagedown") return 0x4e;
  if (key.length() >= 2 && key[0] == 'f') {
    String number = key.substring(1); int n = number.toInt();
    if (number == String(n) && n >= 1 && n <= 24) return n <= 12 ? 0x3a + n - 1 : 0x68 + n - 13;
  }
  return 0;
}
bool parseHotkey(String input, KeyReport &report) {
  memset(&report, 0, sizeof(report));
  if (input.isEmpty()) return false;
  while (input.length() > 1) {
    int split = input.indexOf('+'); if (split < 0) break;
    String part = input.substring(0, split); part.toLowerCase();
    if (oneOf(part, "cmd|gui|meta|super")) report.modifiers |= 8;
    else if (oneOf(part, "ctrl|control")) report.modifiers |= 1;
    else if (oneOf(part, "alt|option")) report.modifiers |= 4;
    else if (part == "shift") report.modifiers |= 2;
    else return false;
    input = input.substring(split + 1);
  }
  if (input.length() == 1) {
    unsigned char c = input[0]; if (c < 32 || c > 126) return false;
    if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    uint8_t key = KeyboardLayout_en_US[c]; if (!key) return false;
    if (key & 0x80) report.modifiers |= 2;
    report.keys[0] = key & 0x7f; return true;
  }
  input.toLowerCase(); report.keys[0] = namedKey(input); return report.keys[0] != 0;
}
bool validate(JsonVariantConst c, String &error) {
  #define REQUIRE(test, msg) do { if (!(test)) { error = msg; return false; } } while (0)
  REQUIRE(c.is<JsonObjectConst>(), "Configuration must be an object");
  REQUIRE(c["version"].is<int>() && c["version"].as<int>() == 1, "Unsupported config version");
  REQUIRE(c["brightness"].is<int>() && c["brightness"].as<int>() >= 5 && c["brightness"].as<int>() <= 100, "Brightness must be 5-100");
  REQUIRE(c["sleepSeconds"].is<int>(), "Sleep time must be an integer");
  int sleep = c["sleepSeconds"]; REQUIRE(sleep == 0 || (sleep >= 30 && sleep <= 3600), "Invalid sleep time");
  auto pages = c["pages"].as<JsonArrayConst>(); REQUIRE(!pages.isNull() && pages.size() >= 1 && pages.size() <= 8, "Use 1-8 pages");
  int pi = 0;
  for (auto p : pages) {
    REQUIRE(identifier(p["id"].as<const char *>()), "Invalid page ID");
    REQUIRE(printable(p["name"].as<const char *>(), 24), "Invalid page name");
    for (int j = 0; j < pi; j++) REQUIRE(strcmp(p["id"], pages[j]["id"]) != 0, "Duplicate page ID");
    auto buttons = p["buttons"].as<JsonArrayConst>();
    REQUIRE(!buttons.isNull() && buttons.size() == (size_t)decklayout::Tiles, "Each page needs 6 buttons");
    int bi = 0;
    for (auto b : buttons) {
      REQUIRE(identifier(b["id"].as<const char *>()), "Invalid button ID");
      for (int j = 0; j < bi; j++) REQUIRE(strcmp(b["id"], buttons[j]["id"]) != 0, "Duplicate button ID");
      REQUIRE(printable(b["label"].as<const char *>(), 24), "Labels need printable ASCII, max 24 characters");
      const char *color = b["color"]; REQUIRE(color && strlen(color) == 7 && color[0] == '#', "Use #RRGGBB colors");
      for (int j = 1; j < 7; j++) REQUIRE(isxdigit((unsigned char)color[j]), "Invalid color");
      REQUIRE(b["icon"].is<const char *>() && oneOf(b["icon"].as<String>(), "key|copy|paste|mic|video|play|volume|music|globe|app|terminal|bolt|folder|star|heart|gear"), "Unknown icon");
      REQUIRE(validImage(b["image"].as<const char *>()), "Images must be base64 40x40 RGB565");
      auto actions = b["actions"].as<JsonArrayConst>(); REQUIRE(!actions.isNull() && actions.size() <= 8, "Maximum 8 actions per button");
      for (auto a : actions) {
        REQUIRE(a["type"].is<const char *>() && a["value"].is<const char *>(), "Action needs type and value");
        String type = a["type"].as<String>(); const char *value = a["value"];
        REQUIRE(a["value"].as<JsonString>().size() == strlen(value), "Action value cannot contain null characters");
        REQUIRE(strlen(value) <= 512, "Action value exceeds 512 bytes");
        REQUIRE(a["delayMs"].is<int>() && a["delayMs"].as<int>() >= 0 && a["delayMs"].as<int>() <= 5000, "Delay must be 0-5000ms");
        REQUIRE(oneOf(type, "none|hotkey|text|media|url|app|shell|shortcut|page|delay"), "Unknown action type");
        if (type == "hotkey") { KeyReport r; REQUIRE(parseHotkey(value, r), "Invalid hotkey"); }
        if (type == "text") for (const unsigned char *q = (const unsigned char *)value; *q; q++) REQUIRE((*q >= 32 && *q <= 126) || *q == '\n' || *q == '\t', "Text requires ASCII with newline/tab");
        if (type == "media") REQUIRE(oneOf(value, "play_pause|next|previous|mute|volume_up|volume_down|stop"), "Unknown media action");
        if (type == "url") REQUIRE(String(value).startsWith("https://") || String(value).startsWith("http://"), "URL must use http(s)");
        if (type == "app" || type == "shell" || type == "shortcut") REQUIRE(*value, "Action value cannot be empty");
        if (type == "delay") REQUIRE(!*value, "Wait action value must be empty");
        if (type == "page") { bool found = false; for (auto dest : pages) if (strcmp(dest["id"], value) == 0) found = true; REQUIRE(found, "Page action target does not exist"); }
      }
      bi++;
    }
    pi++;
  }
  REQUIRE(measureJson(c) <= MAX_CONFIG, "Configuration is too large");
  #undef REQUIRE
  return true;
}
void defaults() {
  config.clear(); config["version"] = 1; config["brightness"] = 80; config["sleepSeconds"] = 300;
  auto pages = config["pages"].to<JsonArray>();
  const char *ids[12] = {"copy", "paste", "undo", "save", "screenshot", "media", "previous", "play", "next", "mute", "volume_down", "volume_up"};
  const char *labels[12] = {"Copy", "Paste", "Undo", "Save", "Capture", "Media", "Previous", "Play / Pause", "Next", "Mute", "Volume -", "Volume +"};
  const char *icons[12] = {"copy", "paste", "bolt", "folder", "star", "music", "music", "play", "music", "mic", "volume", "volume"};
  const char *values[12] = {"cmd+c", "cmd+v", "cmd+z", "cmd+s", "cmd+shift+4", "media", "previous", "play_pause", "next", "mute", "volume_down", "volume_up"};
  const char *colors[12] = {"#C2603F", "#2F5D8C", "#6B5AA6", "#4E7A52", "#C79A3C", "#B85C7A", "#C2603F", "#2F5D8C", "#6B5AA6", "#4E7A52", "#C79A3C", "#B85C7A"};
  for (int p = 0; p < 2; p++) {
    auto page = pages.add<JsonObject>(); page["id"] = p ? "media" : "main"; page["name"] = p ? "Media" : "Everyday";
    auto buttons = page["buttons"].to<JsonArray>();
    for (int i = 0; i < 6; i++) {
      auto b = buttons.add<JsonObject>();
      b["id"] = ids[p * 6 + i]; b["label"] = labels[p * 6 + i]; b["color"] = colors[p * 6 + i];
      b["icon"] = icons[p * 6 + i]; b["image"] = "";
      auto a = b["actions"].to<JsonArray>().add<JsonObject>();
      a["type"] = p ? "media" : (i == 5 ? "page" : "hotkey"); a["value"] = values[p * 6 + i]; a["delayMs"] = 0;
    }
  }
}
bool persist(JsonVariantConst value) {
  if (!fsReady) return false;
  File f = LittleFS.open("/config.tmp", "w"); if (!f) return false;
  size_t expected = measureJson(value), written = serializeJson(value, f); f.flush(); f.close();
  if (written != expected) { LittleFS.remove("/config.tmp"); return false; }
  if (!LittleFS.rename("/config.tmp", "/config.json")) { LittleFS.remove("/config.tmp"); return false; }
  return true;
}
void loadConfig() {
  fsReady = LittleFS.begin(true);
  if (fsReady) {
    File f = LittleFS.open("/config.json", "r");
    if (f && f.size() <= MAX_CONFIG) {
      auto result = deserializeJson(config, f); f.close(); String error;
      if (!result && validate(config.as<JsonVariantConst>(), error)) return;
    }
  }
  defaults();
}

// --- Presentation ----------------------------------------------------------
uint32_t rgb888(const char *hex) { return strtoul(hex + 1, nullptr, 16) & 0xFFFFFF; }

void applyPage() {
  if (!uiReady) return;
  uint32_t started = micros();
  auto page = config["pages"][currentPage];
  deckui::setHeader(page["name"] | "", currentPage, (int)config["pages"].size());
  deckui::setHidReady(tud_mounted());
  static uint8_t image[3200];
  for (int i = 0; i < decklayout::Tiles; i++) {
    auto b = page["buttons"][i];
    const char *encoded = b["image"] | "";
    size_t decoded = 0;
    bool haveImage = *encoded && mbedtls_base64_decode(image, sizeof(image), &decoded,
                                                       (const uint8_t *)encoded, strlen(encoded)) == 0 && decoded == 3200;
    const char *glyph = deckicons::resolve(b["icon"] | "", b["actions"][0]["value"] | "");
    deckui::setTile(i, b["label"] | "", rgb888(b["color"] | "#888888"), glyph,
                    haveImage ? image : nullptr);
  }
  maxRenderUs = max(maxRenderUs, micros() - started);
}

void brightness() { ledcWrite(LCD_BL, sleeping ? 0 : (int)config["brightness"] * 255 / 100); }
void wake() { sleeping = false; lastActivity = millis(); brightness(); }

// --- Action sequences ------------------------------------------------------
struct Sequence { bool active = false; int page = 0, button = 0, step = 0, textOffset = 0; uint8_t phase = 0; uint32_t due = 0; } sequence;
bool keysHeld = false, mediaHeld = false;
String waitingActionID;
uint32_t actionCounter = 0;
void releaseKeys() { if (keysHeld) { keyboard.releaseAll(); keysHeld = false; } if (mediaHeld) { consumer.release(); mediaHeld = false; } }
void cancelSequence() { sequence.active = false; waitingActionID = ""; releaseKeys(); }

void goPage(int page, bool emit = true) {
  touch.gate(millis());
  cancelSequence();
  currentPage = (page + (int)config["pages"].size()) % config["pages"].size();
  wake(); applyPage();
  if (emit) { JsonDocument d; d["event"] = "page"; d["page"] = currentPage; enqueue(d); }
}
uint16_t mediaCode(const String &s) {
  if (s == "play_pause") return CONSUMER_CONTROL_PLAY_PAUSE; if (s == "next") return CONSUMER_CONTROL_SCAN_NEXT;
  if (s == "previous") return CONSUMER_CONTROL_SCAN_PREVIOUS; if (s == "mute") return CONSUMER_CONTROL_MUTE;
  if (s == "volume_up") return CONSUMER_CONTROL_VOLUME_INCREMENT; if (s == "volume_down") return CONSUMER_CONTROL_VOLUME_DECREMENT;
  return CONSUMER_CONTROL_STOP;
}
void nextStep() { sequence.step++; sequence.phase = 0; sequence.textOffset = 0; sequence.due = millis() + 8; }
void startSequence(int button) {
  // Diagnostics must never reach here. The diagnostic screen also carries no
  // event handlers, so this is the second of two independent guarantees.
  if (diagnostic != Diag::Off || sequence.active) return;
  pressCount++;
  JsonDocument d; d["event"] = "press"; d["page"] = currentPage; d["button"] = button; enqueue(d);
  sequence = {true, currentPage, button, 0, 0, 0, millis()};
}
void pumpActions() {
  if (!sequence.active || (int32_t)(millis() - sequence.due) < 0) return;
  if (sequence.phase == 3) { cancelSequence(); errorEvent("Mac action timed out; sequence cancelled"); return; }
  auto actions = config["pages"][sequence.page]["buttons"][sequence.button]["actions"].as<JsonArray>();
  if (sequence.step >= (int)actions.size()) { cancelSequence(); return; }
  auto a = actions[sequence.step]; String type = a["type"].as<String>(); const char *value = a["value"];
  if (sequence.phase == 0) { sequence.phase = 1; sequence.due = millis() + (int)a["delayMs"]; return; }
  if (sequence.phase == 2) { releaseKeys(); if (type == "text") { sequence.phase = 1; sequence.textOffset++; sequence.due = millis() + 12; } else nextStep(); return; }
  if (type == "none" || type == "delay") { nextStep(); return; }
  if (type == "page") { int i = 0; for (auto p : config["pages"].as<JsonArray>()) { if (strcmp(p["id"], value) == 0) { goPage(i); return; } i++; } cancelSequence(); return; }
  if (oneOf(type, "url|app|shell|shortcut")) {
    waitingActionID = String(millis(), HEX) + "-" + String(++actionCounter, HEX);
    JsonDocument d; d["event"] = "host_action"; d["id"] = waitingActionID; d["page"] = sequence.page; d["button"] = sequence.button; d["step"] = sequence.step;
    if (!enqueue(d)) { cancelSequence(); errorEvent("Open the NiceDeck Mac app for this action"); return; }
    sequence.phase = 3; sequence.due = millis() + 30000; return;
  }
  if (!tud_mounted()) { cancelSequence(); errorEvent("USB keyboard is not connected"); return; }
  if (!hid.ready()) { sequence.due = millis() + 4; return; }
  if (type == "media") { consumer.press(mediaCode(value)); mediaHeld = true; }
  else {
    KeyReport report = {};
    if (type == "hotkey") parseHotkey(value, report);
    else {
      unsigned char c = value[sequence.textOffset]; if (!c) { nextStep(); return; }
      uint8_t code = KeyboardLayout_en_US[c]; report.keys[0] = code & 0x7f; if (code & 0x80) report.modifiers = 2;
    }
    keyboard.sendReport(&report); keysHeld = true;
  }
  sequence.phase = 2; sequence.due = millis() + 20;
}

// --- Touch -----------------------------------------------------------------
// CST816D register map (Hynitron): 0x01 GestureID, 0x02 FingerNum, 0x03 XposH,
// 0x04 XposL, 0x05 YposH, 0x06 YposL. Five bytes from 0x02 give finger count
// and both coordinates in one transaction.
bool touchRead(int &rawX, int &rawY, bool &down) {
  Wire.beginTransmission(TOUCH_ADDR); Wire.write(0x02);
  if (Wire.endTransmission(true) != 0) return false;
  if (Wire.requestFrom(TOUCH_ADDR, (uint8_t)5) != (uint8_t)5) { while (Wire.available()) Wire.read(); return false; }
  uint8_t count = Wire.read(), xh = Wire.read(), xl = Wire.read(), yh = Wire.read(), yl = Wire.read();
  uint8_t fingers = count & 0x0f;
  rawX = ((xh & 15) << 8) | xl;
  rawY = ((yh & 15) << 8) | yl;
  if (fingers > 1) return false;
  down = fingers == 1;
  if (down && !decktouch::rawInRange(rawX, rawY)) return false;
  return true;
}
bool touchInit() {
  lastTouchInit = millis();
  Wire.beginTransmission(TOUCH_ADDR); Wire.write(0xA7);
  if (Wire.endTransmission(true) != 0 || Wire.requestFrom(TOUCH_ADDR, (uint8_t)1) != 1) return false;
  int id = Wire.read(); if (id != 0xb6) return false;
  // Disable the controller's automatic standby so the finger count stays
  // readable between reports.
  Wire.beginTransmission(TOUCH_ADDR); Wire.write(0xFE); Wire.write(0x01);
  if (Wire.endTransmission(true) != 0) return false;
  Wire.beginTransmission(TOUCH_ADDR); Wire.write(0xFE);
  if (Wire.endTransmission(true) != 0 || Wire.requestFrom(TOUCH_ADDR, (uint8_t)1) != 1) return false;
  return Wire.read() == 0x01;
}

void streamSample(bool valid, bool down, int rawX, int rawY, int x, int y, uint32_t now, uint32_t interval) {
  JsonDocument d;
  d["event"] = "touch_sample"; d["t"] = now; d["dt"] = interval; d["valid"] = valid;
  d["down"] = down; d["rawX"] = rawX; d["rawY"] = rawY; d["x"] = x; d["y"] = y;
  d["pressed"] = touch.pressed();
  enqueue(d);
}

void recordAlignment(int x, int y) {
  diagAttempts++;
  if (diagTarget < 0 || diagTarget >= decklayout::AlignmentTargets) return;
  diagMeasuredX[diagTarget] = x; diagMeasuredY[diagTarget] = y; diagMeasured[diagTarget] = true;
  diagErrorX = x - decklayout::AlignmentTargetXY[diagTarget][0];
  diagErrorY = y - decklayout::AlignmentTargetXY[diagTarget][1];
  diagHaveError = true;
  // Advancement uses the diagnostic's own radius test, not the tile hit map,
  // so a broken interface layout cannot block the alignment check.
  if (decklayout::alignmentHit(diagTarget, x, y)) { diagHits++; diagTarget++; }
}

void pumpTouch() {
  uint32_t now = millis(), elapsed = now - lastTouchPoll;
  // The controller signals a fresh report on its interrupt line. The bounded
  // poll only exists so a missed edge cannot strand the contact state.
  if (elapsed < 2 || (!touchIRQPending && elapsed < 8)) return;
  touchIRQPending = false; lastTouchPoll = now; touchPolls++;

  int rawX = 0, rawY = 0, x = 0, y = 0; bool down = false;
  bool valid = touchRead(rawX, rawY, down);
  now = millis();
  if (valid && down) decktouch::rawToScreen(rawX, rawY, x, y);
  else { x = touch.x(); y = touch.y(); }

  if (!valid) {
    touchFailures++;
    if (now - lastTouchSuccess > 5000) { touchOK = false; touchConfigured = false; }
  } else {
    lastTouchSuccess = now; touchOK = true;
  }

  uint32_t interval = diagLastSampleAt ? now - diagLastSampleAt : 0;
  if (diagnostic != Diag::Off) {
    if (interval) {
      if (!diagMinInterval || interval < diagMinInterval) diagMinInterval = interval;
      if (interval > diagMaxInterval) diagMaxInterval = interval;
    }
    if (!valid) diagFailures++;
    else if (down) { diagSamples++; diagRawX = rawX; diagRawY = rawY; diagX = x; diagY = y; }
  }
  diagLastSampleAt = now;
  if (diagStream) streamSample(valid, down, rawX, rawY, x, y, now, interval);

  bool wasPressed = touch.pressed();
  if ((int32_t)(now - ignoreInputUntil) < 0) touch.gate(now);
  decktouch::Contact contact = touch.update(valid, down, x, y, now);

  if (contact.pressed && !wasPressed) {
    diagContacts++;
    lastActivity = now;
    if (sleeping) {
      // The first touch after sleep only wakes the display.
      wake(); touch.gate(now);
      deckui::setPointer(false, contact.x, contact.y, true);
      return;
    }
  }
  if (contact.pressed) lastActivity = now;

  if (diagnostic != Diag::Off) {
    // Diagnostics own their own release handling and never feed LVGL, so no
    // pointer event can reach a control while shortcuts are disabled.
    if (wasPressed && !contact.pressed && !touch.clickInhibited() &&
        touch.endReason() == decktouch::Reject::None)
      if (diagnostic == Diag::Alignment) recordAlignment(contact.x, contact.y);
    return;
  }

  deckui::setPointer(contact.pressed, contact.x, contact.y, touch.clickInhibited());

  if (!touchConfigured && !touch.pressed() && now - lastTouchInit >= 1000) touchConfigured = touchInit();
}

void pumpBoot() {
  bool down = digitalRead(DECK_BOOT_PIN) == LOW;
  if (down != bootWasDown) { bootWasDown = down; lastBootEdge = millis(); return; }
  if (millis() - lastBootEdge < 35) return;
  if (!down) { bootArmed = true; return; }
  if (bootArmed && (int32_t)(millis() - ignoreInputUntil) >= 0) {
    bootArmed = false;
    if (diagnostic != Diag::Off) { diagBootPresses++; return; }
    touch.gate(millis());
    if (sleeping) wake(); else goPage(currentPage + 1);
  }
}

// --- Diagnostics presentation and protocol ---------------------------------
void refreshDiagnosticScreen() {
  deckui::DiagnosticView view{};
  view.rawX = diagRawX; view.rawY = diagRawY; view.x = diagX; view.y = diagY;
  view.down = touch.pressed();
  view.contacts = diagContacts; view.downSamples = diagSamples; view.readErrors = diagFailures;
  view.irqs = touchIRQs; view.polls = touchPolls;
  view.minIntervalMs = diagMinInterval; view.maxIntervalMs = diagMaxInterval;
  view.rejectReason = rejectName(touch.lastReject());
  view.target = diagnostic == Diag::Alignment ? diagTarget : -1;
  view.hits = diagHits;
  view.lastErrorX = diagErrorX; view.lastErrorY = diagErrorY; view.haveLastError = diagHaveError;
  deckui::setDiagnostics(view);
}

void resetDiagnosticCounters() {
  diagContacts = diagSamples = diagFailures = diagBootPresses = 0;
  diagMinInterval = diagMaxInterval = 0; diagAttempts = 0;
  diagHits = 0; diagHaveError = false; diagErrorX = diagErrorY = 0;
  for (int i = 0; i < decklayout::AlignmentTargets; i++) { diagMeasuredX[i] = diagMeasuredY[i] = 0; diagMeasured[i] = false; }
}

void diagnosticReport(const char *id) {
  JsonDocument d;
  d["event"] = "touch_test"; d["id"] = id;
  d["enabled"] = diagnostic != Diag::Off;
  d["mode"] = diagnostic == Diag::Alignment ? "targets" : (diagnostic == Diag::Motion ? "motion" : "off");
  d["streaming"] = diagStream;
  d["contacts"] = diagContacts; d["downSamples"] = diagSamples; d["readErrors"] = diagFailures;
  d["bootPresses"] = diagBootPresses; d["attempts"] = diagAttempts;
  d["rawX"] = diagRawX; d["rawY"] = diagRawY; d["x"] = diagX; d["y"] = diagY;
  d["down"] = touch.pressed();
  d["minIntervalMs"] = diagMinInterval; d["maxIntervalMs"] = diagMaxInterval;
  d["irqs"] = (uint32_t)touchIRQs; d["polls"] = touchPolls;
  d["lastReject"] = rejectName(touch.lastReject());
  d["target"] = diagTarget; d["hits"] = diagHits;
  d["radiusPx"] = decklayout::AlignmentRadius;
  // Reported for inspection only. No correction is derived from these numbers
  // and nothing is written to flash: this verifies alignment, it does not
  // calibrate it.
  d["calibrationApplied"] = false;
  auto points = d["points"].to<JsonArray>();
  for (int i = 0; i < decklayout::AlignmentTargets; i++) {
    auto p = points.add<JsonObject>();
    p["expectedX"] = decklayout::AlignmentTargetXY[i][0];
    p["expectedY"] = decklayout::AlignmentTargetXY[i][1];
    p["measured"] = diagMeasured[i];
    p["x"] = diagMeasuredX[i]; p["y"] = diagMeasuredY[i];
    p["dx"] = diagMeasured[i] ? diagMeasuredX[i] - decklayout::AlignmentTargetXY[i][0] : 0;
    p["dy"] = diagMeasured[i] ? diagMeasuredY[i] - decklayout::AlignmentTargetXY[i][1] : 0;
  }
  enqueue(d);
}

void enterDiagnostics(Diag mode) {
  diagnostic = mode;
  cancelSequence();
  wake();
  touch.gate(millis());
  deckui::setPointer(false, 0, 0, true);
  resetDiagnosticCounters();
  diagTarget = mode == Diag::Alignment ? 0 : -1;
  deckui::showDiagnostics(mode == Diag::Alignment);
  refreshDiagnosticScreen();
}

void exitDiagnostics() {
  diagnostic = Diag::Off;
  diagStream = false;
  cancelSequence();
  touch.gate(millis());
  deckui::setPointer(false, 0, 0, true);
  ignoreInputUntil = millis() + 500;
  deckui::showDeck();
  applyPage();
}

void screenshot(const char *id) {
  if (txQueue.size() > 8) { ack(id, "screenshot", false, "USB busy; retry screenshot"); return; }
  constexpr uint32_t PIXELS = (uint32_t)decklayout::Width * decklayout::Height;
  auto *frame = (uint16_t *)allocator.allocate(PIXELS * 2);
  if (!frame) { ack(id, "screenshot", false, "Out of memory"); return; }
  if (!deckui::captureFramebuffer(frame, PIXELS)) {
    allocator.deallocate(frame); ack(id, "screenshot", false, "Framebuffer unavailable"); return;
  }
  constexpr size_t CHUNK = 12288;
  auto *raw = (uint8_t *)allocator.allocate(CHUNK);
  auto *encoded = (char *)allocator.allocate(CHUNK * 4 / 3 + 1);
  if (!raw || !encoded) {
    allocator.deallocate(raw); allocator.deallocate(encoded); allocator.deallocate(frame);
    ack(id, "screenshot", false, "Out of memory"); return;
  }
  JsonDocument begin;
  begin["event"] = "screenshot_begin"; begin["id"] = id; begin["width"] = decklayout::Width;
  begin["height"] = decklayout::Height; begin["encoding"] = "rgb565be"; enqueue(begin);
  for (size_t offset = 0; offset < PIXELS * 2; offset += CHUNK) {
    size_t length = min(CHUNK, (size_t)(PIXELS * 2) - offset);
    for (size_t i = 0; i < length / 2; i++) { uint16_t p = frame[offset / 2 + i]; raw[2 * i] = p >> 8; raw[2 * i + 1] = p & 255; }
    size_t written = 0;
    if (mbedtls_base64_encode((uint8_t *)encoded, CHUNK * 4 / 3 + 1, &written, raw, length) != 0) break;
    encoded[written] = 0;
    JsonDocument d(&allocator);
    d["event"] = "screenshot_chunk"; d["id"] = id; d["offset"] = offset; d["data"] = (const char *)encoded;
    if (!enqueue(d)) break;
    // The queue is bounded; drain it so a full-screen capture cannot stall.
    uint32_t guard = millis();
    while (txQueue.size() >= 20 && millis() - guard < 5000) pumpTx();
  }
  allocator.deallocate(raw); allocator.deallocate(encoded); allocator.deallocate(frame);
  JsonDocument end; end["event"] = "screenshot_end"; end["id"] = id; enqueue(end);
}

void handleLine() {
  JsonDocument request(&allocator);
  auto result = deserializeJson(request, (const char *)rx, rxLength, DeserializationOption::NestingLimit(12));
  if (result) { errorEvent("Invalid JSON request"); return; }
  const char *op = request["op"] | ""; const char *id = request["id"] | "";
  if (strlen(id) > 128) { errorEvent("Request ID is too long"); return; }
  if (strcmp(op, "hello") == 0) { hello(id); return; }
  if (strcmp(op, "ping") == 0) { ack(id, op, true); return; }
  if (strcmp(op, "touch_test") == 0) {
    const char *mode = request["mode"] | "status";
    if (strcmp(mode, "motion") == 0) enterDiagnostics(Diag::Motion);
    else if (strcmp(mode, "targets") == 0) enterDiagnostics(Diag::Alignment);
    else if (strcmp(mode, "exit") == 0) exitDiagnostics();
    else if (strcmp(mode, "stream") == 0) {
      if (diagnostic == Diag::Off) enterDiagnostics(Diag::Motion);
      diagStream = true;
    } else if (strcmp(mode, "stop_stream") == 0) diagStream = false;
    else if (strcmp(mode, "status") != 0) { ack(id, op, false, "Unknown test mode"); return; }
    diagnosticReport(id); return;
  }
  if (strcmp(op, "host_action_result") == 0) {
    if (sequence.active && sequence.phase == 3 && waitingActionID == id) {
      if (request["ok"].is<bool>() && request["ok"].as<bool>()) { waitingActionID = ""; nextStep(); }
      else { cancelSequence(); errorEvent("Mac action failed; sequence cancelled"); }
    }
    return;
  }
  if (strcmp(op, "status") == 0) {
    JsonDocument d;
    d["event"] = "status"; d["id"] = id; d["page"] = currentPage; d["pages"] = config["pages"].size();
    d["touch"] = touchOK; d["hid"] = tud_mounted(); d["freeHeap"] = ESP.getFreeHeap();
    d["freePsram"] = ESP.getFreePsram(); d["uptime"] = millis(); d["storage"] = fsReady;
    d["sleeping"] = sleeping; d["touchFailures"] = touchFailures; d["presses"] = pressCount;
    d["touchConfigured"] = touchConfigured; d["touchPolls"] = touchPolls; d["touchIRQs"] = (uint32_t)touchIRQs;
    d["touchStarts"] = touch.contacts(); d["touchTaps"] = touchTaps;
    d["touchRejected"] = touch.rejectedContacts();
    d["touchSwipes"] = 0;  // Swipe navigation was removed; retained for older clients.
    d["diagnostic"] = diagnostic != Diag::Off;
    d["maxButtonDrawUs"] = maxFlushUs; d["maxFullDrawUs"] = maxRenderUs;
    enqueue(d); return;
  }
  if (strcmp(op, "get_config") == 0) { JsonDocument d(&allocator); d["event"] = "config"; d["id"] = id; d["config"] = config; enqueue(d); return; }
  if (strcmp(op, "screenshot") == 0) { screenshot(id); return; }
  if (strcmp(op, "page") == 0) {
    if (!request["page"].is<int>() || request["page"].as<int>() < 0 || request["page"].as<int>() >= (int)config["pages"].size()) { ack(id, op, false, "Page out of range"); return; }
    goPage(request["page"], false); ignoreInputUntil = millis() + 250; ack(id, op, true); return;
  }
  if (strcmp(op, "config") == 0) {
    String error;
    if (!validate(request["config"].as<JsonVariantConst>(), error)) { ack(id, op, false, error.c_str()); return; }
    cancelSequence(); touch.gate(millis());
    if (!persist(request["config"].as<JsonVariantConst>())) { ack(id, op, false, "Could not save configuration to flash"); return; }
    config.set(request["config"]);
    currentPage = min(currentPage, (int)config["pages"].size() - 1);
    wake();
    if (diagnostic == Diag::Off) applyPage();
    ignoreInputUntil = millis() + 250;
    ack(id, op, true); return;
  }
  ack(id, op, false, "Unknown operation");
}

void pumpSerial() {
  bool connected = (bool)Serial;
  if (connected != serialWasConnected) {
    serialWasConnected = connected; cancelSequence(); clearTx(); rxLength = 0; rxDiscard = false;
    touch.gate(millis()); ignoreInputUntil = millis() + 300;
    if (connected) { hello(""); if (diagnostic == Diag::Off) applyPage(); }
  }
  if (!rx) return;
  int budget = 4096;
  while (Serial.available() && budget-- > 0) {
    int c = Serial.read(); rxLastByte = millis();
    if (c == '\n') { if (!rxDiscard && rxLength) { rx[rxLength] = 0; handleLine(); } rxLength = 0; rxDiscard = false; continue; }
    if (c == '\r') continue;
    if (rxDiscard) continue;
    if (rxLength >= MAX_LINE) { rxDiscard = true; rxLength = 0; errorEvent("Request exceeds size limit"); continue; }
    rx[rxLength++] = (char)c;
  }
  if ((rxLength || rxDiscard) && millis() - rxLastByte > 10000) { rxLength = 0; rxDiscard = false; errorEvent("Incomplete request timed out"); }
}

void onTileClicked(int index) {
  if (diagnostic != Diag::Off) return;
  touchTaps++;
  startSequence(index);
}
void onPageStep(int delta) {
  if (diagnostic != Diag::Off) return;
  goPage(currentPage + delta);
}
}  // namespace

void deckSetup() {
  pinMode(DECK_BOOT_PIN, INPUT_PULLUP);
  pinMode(LCD_BL, OUTPUT); digitalWrite(LCD_BL, LOW);
  // Waveshare schematic: SD_CS is GPIO41 and is held high while the card is
  // unused; GPIO46 is the touch controller's interrupt output and must only
  // ever be an input on the ESP32.
  pinMode(SD_CS, OUTPUT); digitalWrite(SD_CS, HIGH);
  pinMode(TOUCH_INT, INPUT);
  attachInterrupt(digitalPinToInterrupt(TOUCH_INT), touchInterrupt, FALLING);
  Wire.begin(TOUCH_SDA, TOUCH_SCL, 400000); Wire.setTimeOut(12);

  // CDC-on-boot starts USB before setup. build_opt.h sets the descriptor names
  // when the Arduino core is compiled; runtime setters here would be too late.
  keyboard.begin(); consumer.begin();
  Serial.setRxBufferSize(8192); Serial.setTxTimeoutMs(0); Serial.begin(115200);
  USB.begin();
  rx = (char *)allocator.allocate(MAX_LINE + 1);

  deckui::onTile = onTileClicked;
  deckui::onPageStep = onPageStep;
  deckui::onFlush = pumpTx;
  uiReady = deckui::begin();

  loadConfig();
  touchConfigured = touchInit();
  touchOK = touchConfigured;
  ledcAttach(LCD_BL, 20000, 8); brightness();
  applyPage();
  lastActivity = millis(); lastTouchSuccess = millis(); ignoreInputUntil = millis() + 1000;
  touch.gate(millis());
}

void deckLoop() {
  pumpSerial(); pumpTx(); pumpActions(); pumpTouch(); pumpBoot();

  static uint32_t lastDiagnosticDraw = 0;
  if (diagnostic != Diag::Off && millis() - lastDiagnosticDraw >= 100) {
    lastDiagnosticDraw = millis(); refreshDiagnosticScreen();
  }

  uint32_t started = micros();
  deckui::tick();
  uint32_t spent = micros() - started;
  if (spent > maxFlushUs) maxFlushUs = spent;

  int seconds = config["sleepSeconds"] | 300;
  if (seconds > 0 && diagnostic == Diag::Off && !sleeping && !touch.pressed() && !sequence.active &&
      millis() - lastActivity > (uint32_t)seconds * 1000) {
    sleeping = true; brightness();
  }
  delay(1);
}
