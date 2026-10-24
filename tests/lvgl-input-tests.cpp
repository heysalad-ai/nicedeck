// Integration tests for the real LVGL input pipeline.
//
// These build the same controls the firmware builds (DeckControls.h), register
// the same single LV_EVENT_CLICKED handler, and drive them through the real
// LVGL indev machinery using the real contact filter. Samples are expressed in
// raw CST816D panel coordinates and pass through decktouch::rawToScreen, so a
// rotation mistake would show up here as a click on the wrong tile.
//
// Nothing links against Arduino or USB, so no test can send a shortcut.
#include <lvgl.h>

#include "../firmware/NiceDeck/DeckControls.h"
#include "../firmware/NiceDeck/DeckLayout.h"
#include "../firmware/NiceDeck/DeckTouch.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {
int checks = 0;
int failures = 0;

void expect(bool condition, const std::string &message) {
  ++checks;
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

// --- Harness mirroring deckLoop() ------------------------------------------
std::uint32_t clockMs = 0;
std::uint32_t tickCb() { return clockMs; }

decktouch::ContactFilter filter;
bool pointerDown = false, clickSuppressed = false;
int pointerX = 0, pointerY = 0;

std::vector<int> tileClicks;
std::vector<int> pageSteps;

void readCb(lv_indev_t *, lv_indev_data_t *data) {
  data->point.x = pointerX;
  data->point.y = pointerY;
  data->state = pointerDown ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

// Same policy as DeckUI.cpp: PRESS_LOST withholds the action for the rest of
// the contact, so sliding onto another control cancels instead of activating.
bool consumePressLost(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_PRESS_LOST) return false;
  clickSuppressed = true;
  return true;
}
void tileEvent(lv_event_t *event) {
  if (consumePressLost(event) || clickSuppressed) return;
  tileClicks.push_back((int)(intptr_t)lv_event_get_user_data(event));
}
void navEvent(lv_event_t *event) {
  if (consumePressLost(event) || clickSuppressed) return;
  pageSteps.push_back((int)(intptr_t)lv_event_get_user_data(event));
}

// Same rule as Deck.cpp: a press starts a fresh contact, a hold suppresses it.
void setPointer(bool pressed, int x, int y, bool suppress) {
  if (pressed && !pointerDown) clickSuppressed = false;
  if (suppress) clickSuppressed = true;
  pointerDown = pressed;
  pointerX = x;
  pointerY = y;
}

lv_display_t *display = nullptr;
lv_obj_t *screen = nullptr;
deckcontrols::Controls controls{};

void flushCb(lv_display_t *disp, const lv_area_t *, uint8_t *) { lv_display_flush_ready(disp); }

constexpr std::uint32_t Poll = 8;  // measured device poll cadence

// Advance simulated time, feeding the sensor stream through the filter into
// LVGL exactly as the firmware loop does.
void advance(std::uint32_t ms, bool valid, bool down, int rawX, int rawY) {
  std::uint32_t end = clockMs + ms;
  while (clockMs < end) {
    int x = filter.x(), y = filter.y();
    if (valid && down) decktouch::rawToScreen(rawX, rawY, x, y);
    decktouch::Contact contact = filter.update(valid, down, x, y, clockMs);
    setPointer(contact.pressed, contact.x, contact.y, filter.clickInhibited());
    lv_timer_handler();
    clockMs += Poll;
  }
}

void idle(std::uint32_t ms) { advance(ms, true, false, 0, 0); }

// The raw panel point that lands on a given screen point.
void rawFor(int screenX, int screenY, int &rawX, int &rawY) {
  decktouch::rawToScreen(screenX, screenY, rawX, rawY);  // the transform is its own inverse
}

void reset() {
  tileClicks.clear();
  pageSteps.clear();
  filter.reset(clockMs);
  setPointer(false, 0, 0, false);
  idle(120);
  tileClicks.clear();
  pageSteps.clear();
}

// A deliberate tap: settle, press for `holdMs`, lift, settle.
void tap(int screenX, int screenY, std::uint32_t holdMs = 120) {
  int rawX = 0, rawY = 0;
  rawFor(screenX, screenY, rawX, rawY);
  advance(holdMs, true, true, rawX, rawY);
  idle(120);
}

// --- Tests -----------------------------------------------------------------
void everyTileRespondsToADeliberateTap() {
  for (int i = 0; i < decklayout::Tiles; ++i) {
    reset();
    tap(decklayout::tileX(i) + decklayout::TileWidth / 2,
        decklayout::tileY(i) + decklayout::TileHeight / 2);
    expect(tileClicks.size() == 1 && tileClicks[0] == i,
           "tile " + std::to_string(i) + " fires exactly once for a deliberate tap");
    expect(pageSteps.empty(), "tapping tile " + std::to_string(i) + " does not change page");
  }
}

void tapsNearTheTileEdgesAndInTheGaps() {
  reset();
  tap(decklayout::tileX(0) + 3, decklayout::tileY(0) + 3);
  expect(tileClicks.size() == 1 && tileClicks[0] == 0, "a tap near a tile corner still lands on it");

  // The gap between the two columns belongs to the nearer tile, so there is no
  // dead strip a user can fall into.
  reset();
  tap(decklayout::tileX(0) + decklayout::TileWidth + 1, decklayout::tileY(0) + 30);
  expect(tileClicks.size() == 1 && tileClicks[0] == 0, "the near half of the column gap activates the left tile");

  reset();
  tap(decklayout::tileX(1) - 1, decklayout::tileY(1) + 30);
  expect(tileClicks.size() == 1 && tileClicks[0] == 1, "the far half of the column gap activates the right tile");
}

void draggingAwayCancels() {
  // Press on one tile, slide onto its neighbour, release there.
  reset();
  int fromX = 0, fromY = 0, toX = 0, toY = 0;
  rawFor(decklayout::tileX(0) + decklayout::TileWidth / 2, decklayout::tileY(0) + decklayout::TileHeight / 2, fromX, fromY);
  rawFor(decklayout::tileX(1) + decklayout::TileWidth / 2, decklayout::tileY(1) + decklayout::TileHeight / 2, toX, toY);
  advance(80, true, true, fromX, fromY);
  // Move in steps so the filter treats it as genuine movement, not a glitch.
  for (int step = 1; step <= 6; ++step) {
    int x = fromX + (toX - fromX) * step / 6, y = fromY + (toY - fromY) * step / 6;
    advance(32, true, true, x, y);
  }
  advance(60, true, true, toX, toY);
  idle(150);
  expect(tileClicks.empty(), "dragging from one tile to another fires nothing at all");

  // Press on a tile, slide into the header, release outside any control.
  reset();
  rawFor(decklayout::tileX(2) + decklayout::TileWidth / 2, decklayout::tileY(2) + decklayout::TileHeight / 2, fromX, fromY);
  rawFor(decklayout::Width / 2, 6, toX, toY);
  advance(80, true, true, fromX, fromY);
  for (int step = 1; step <= 8; ++step) {
    int x = fromX + (toX - fromX) * step / 8, y = fromY + (toY - fromY) * step / 8;
    advance(32, true, true, x, y);
  }
  idle(150);
  expect(tileClicks.empty(), "dragging off a tile and releasing elsewhere fires nothing");
}

void longHoldDoesNotActivate() {
  reset();
  tap(decklayout::tileX(3) + decklayout::TileWidth / 2, decklayout::tileY(3) + decklayout::TileHeight / 2, 2000);
  expect(tileClicks.empty(), "holding a tile for two seconds does not run its action");

  // The next ordinary tap must still work.
  reset();
  tap(decklayout::tileX(3) + decklayout::TileWidth / 2, decklayout::tileY(3) + decklayout::TileHeight / 2);
  expect(tileClicks.size() == 1 && tileClicks[0] == 3, "a normal tap after a hold works again");
}

void noisePulsesDoNotActivate() {
  reset();
  for (int i = 0; i < 25; ++i) {
    int rawX = 0, rawY = 0;
    rawFor(decklayout::tileX(i % decklayout::Tiles) + 40, decklayout::tileY(i % decklayout::Tiles) + 30, rawX, rawY);
    advance(Poll, true, true, rawX, rawY);  // a single sample only
    idle(80);
  }
  expect(tileClicks.empty(), "isolated one-sample contacts never run an action");
  expect(pageSteps.empty(), "isolated one-sample contacts never change page");
}

void lostReadsDoNotActivate() {
  reset();
  int rawX = 0, rawY = 0;
  rawFor(decklayout::tileX(4) + decklayout::TileWidth / 2, decklayout::tileY(4) + decklayout::TileHeight / 2, rawX, rawY);
  advance(60, true, true, rawX, rawY);
  advance(400, false, false, 0, 0);  // I2C stops answering mid-contact
  expect(tileClicks.empty(), "a contact lost to sustained read failures does not run an action");

  // The finger is still on the glass when reads come back. Adopting it as a
  // press would activate a tile nobody deliberately tapped.
  advance(300, true, true, rawX, rawY);
  expect(tileClicks.empty(), "a contact already in progress when reads return does not run an action");
  idle(200);
  expect(tileClicks.empty(), "lifting that finger still runs nothing");

  reset();
  tap(decklayout::tileX(4) + decklayout::TileWidth / 2, decklayout::tileY(4) + decklayout::TileHeight / 2);
  expect(tileClicks.size() == 1 && tileClicks[0] == 4, "taps work again once reads recover");
}

void wakeAndReconnectDoNotActivate() {
  // Deck.cpp gates the filter on wake, reconnect, page change and config
  // apply. A finger already down when the gate lifts must not deliver a click.
  reset();
  int rawX = 0, rawY = 0;
  rawFor(decklayout::tileX(5) + decklayout::TileWidth / 2, decklayout::tileY(5) + decklayout::TileHeight / 2, rawX, rawY);
  advance(40, true, true, rawX, rawY);
  filter.gate(clockMs);
  setPointer(false, pointerX, pointerY, true);
  advance(400, true, true, rawX, rawY);
  idle(200);
  expect(tileClicks.empty(), "a touch held across a gate does not run an action");

  reset();
  tap(decklayout::tileX(5) + decklayout::TileWidth / 2, decklayout::tileY(5) + decklayout::TileHeight / 2);
  expect(tileClicks.size() == 1 && tileClicks[0] == 5, "a fresh tap after the gate works");
}

void repeatedTapsRunOncePerTap() {
  reset();
  for (int i = 0; i < 10; ++i)
    tap(decklayout::tileX(0) + decklayout::TileWidth / 2, decklayout::tileY(0) + decklayout::TileHeight / 2);
  expect(tileClicks.size() == 10, "ten taps run the action exactly ten times, never twice for one tap");
  bool allZero = true;
  for (int click : tileClicks) allZero = allZero && click == 0;
  expect(allZero, "every repeat lands on the same tile");
}

void pageControls() {
  reset();
  tap(decklayout::Margin + decklayout::NavButtonWidth / 2, decklayout::NavTop + decklayout::NavHeight / 2);
  expect(pageSteps.size() == 1 && pageSteps[0] == -1, "the previous-page control steps back once");
  expect(tileClicks.empty(), "the page control does not run a shortcut");

  reset();
  tap(decklayout::Width - decklayout::Margin - decklayout::NavButtonWidth / 2,
      decklayout::NavTop + decklayout::NavHeight / 2);
  expect(pageSteps.size() == 1 && pageSteps[0] == 1, "the next-page control steps forward once");

  // The gap between the two arrows is not a control, so a stray touch there
  // cannot change page.
  reset();
  tap(decklayout::Width / 2, decklayout::NavTop + decklayout::NavHeight / 2);
  expect(pageSteps.empty(), "the page indicator between the arrows is inert");
}

void rotationAgreesWithTheDisplay() {
  // Literal raw panel coordinates, not derived from the transform, so a change
  // to the rotation fails this test rather than silently moving with it.
  // The board is mounted USB-down, so raw (61,84) is physically near the
  // bottom-right of what the user sees, which is tile 5.
  reset();
  advance(120, true, true, 61, 84);
  idle(150);
  expect(tileClicks.size() == 1 && tileClicks[0] == 5,
         "raw (61,84) activates the bottom-right tile, matching the 180-degree display rotation");

  // Raw (177,240) is physically near the top-left, which is tile 0.
  reset();
  advance(120, true, true, 177, 240);
  idle(150);
  expect(tileClicks.size() == 1 && tileClicks[0] == 0,
         "raw (177,240) activates the top-left tile");

  // A raw point in the panel's own top-left corner lands in the navigation
  // band at the bottom of the screen, never on a tile.
  reset();
  advance(120, true, true, 4, 4);
  idle(150);
  expect(tileClicks.empty(), "the raw panel origin does not reach any tile");
  expect(pageSteps.size() == 1 && pageSteps[0] == 1,
         "the raw panel origin reaches the next-page control at the screen's far corner");
}

void setup() {
  lv_init();
  lv_tick_set_cb(tickCb);

  static std::uint8_t buffer1[decklayout::Width * 32 * 2];
  static std::uint8_t buffer2[decklayout::Width * 32 * 2];
  display = lv_display_create(decklayout::Width, decklayout::Height);
  lv_display_set_flush_cb(display, flushCb);
  lv_display_set_buffers(display, buffer1, buffer2, sizeof(buffer1), LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, readCb);
  lv_indev_set_display(indev, display);

  screen = lv_obj_create(nullptr);
  lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);
  controls = deckcontrols::create(screen, tileEvent, navEvent);
  lv_screen_load(screen);
  idle(200);
}
}  // namespace

int main() {
  setup();
  everyTileRespondsToADeliberateTap();
  tapsNearTheTileEdgesAndInTheGaps();
  draggingAwayCancels();
  longHoldDoesNotActivate();
  noisePulsesDoNotActivate();
  lostReadsDoNotActivate();
  wakeAndReconnectDoNotActivate();
  repeatedTapsRunOncePerTap();
  pageControls();
  rotationAgreesWithTheDisplay();

  std::cout << checks << " LVGL input checks, " << failures << " failed\n";
  return failures == 0 ? 0 : 1;
}
