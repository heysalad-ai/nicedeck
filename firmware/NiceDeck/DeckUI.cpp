#include "DeckUI.h"

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_ST7789.h>
#include <lvgl.h>
#include <esp_heap_caps.h>

#include "DeckControls.h"
#include "DeckIcons.h"
#include "DeckLayout.h"
#include "DeckTheme.h"
#include "DeckTouch.h"

namespace deckui {

void (*onTile)(int) = nullptr;
void (*onPageStep)(int) = nullptr;
void (*onFlush)() = nullptr;

namespace {
using namespace decklayout;

constexpr int LCD_SCK = 39, LCD_MOSI = 38, LCD_MISO = 40, LCD_DC = 42, LCD_CS = 45;

// Two partial render buffers. 240x32 keeps the longest blocking SPI transfer
// near 3ms, against the 59ms full-screen redraw the previous firmware ran on
// every change -- long enough to lose touch samples while it was drawing.
constexpr uint32_t BufferLines = 32;
constexpr uint32_t BufferPixels = (uint32_t)Width * BufferLines;

Adafruit_ST7789 panel(&SPI, LCD_CS, LCD_DC, -1);
lv_display_t *display = nullptr;
lv_indev_t *pointer = nullptr;
bool started = false;

bool pointerDown = false, clickSuppressed = false;
int pointerX = 0, pointerY = 0;

decktheme::Screen deck;
lv_image_dsc_t tileIconDsc[Tiles] = {};
uint16_t *tileIconPixels[Tiles] = {};

lv_obj_t *diagScreen = nullptr;
lv_obj_t *diagState = nullptr, *diagRaw = nullptr, *diagMapped = nullptr;
lv_obj_t *diagCounters = nullptr, *diagTiming = nullptr, *diagReject = nullptr;
lv_obj_t *diagPrompt = nullptr, *diagError = nullptr;
lv_obj_t *diagRing = nullptr, *diagCrossH = nullptr, *diagCrossV = nullptr;

uint32_t tickCb() { return millis(); }

void flushCb(lv_display_t *disp, const lv_area_t *area, uint8_t *pixels) {
  uint32_t w = (uint32_t)(area->x2 - area->x1 + 1), h = (uint32_t)(area->y2 - area->y1 + 1);
  lv_draw_sw_rgb565_swap(pixels, w * h);
  panel.startWrite();
  panel.setAddrWindow(area->x1, area->y1, (uint16_t)w, (uint16_t)h);
  panel.writePixels((uint16_t *)pixels, w * h, true, true);
  panel.endWrite();
  // Keep the outbound USB queue moving while a redraw is in progress. This is
  // write-side only and never calls back into LVGL, so it cannot re-enter the
  // renderer.
  if (onFlush) onFlush();
  lv_display_flush_ready(disp);
}

void readCb(lv_indev_t *, lv_indev_data_t *data) {
  data->point.x = pointerX;
  data->point.y = pointerY;
  data->state = pointerDown ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

// LV_EVENT_CLICKED is the only route to an action. LV_EVENT_PRESS_LOST arrives
// when the finger leaves the control it pressed; it withholds the action for
// the rest of that contact, so sliding onto a neighbouring tile cancels the
// interaction instead of activating it.
bool consumePressLost(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_PRESS_LOST) return false;
  clickSuppressed = true;
  return true;
}

void tileEvent(lv_event_t *event) {
  if (consumePressLost(event) || clickSuppressed) return;
  if (onTile) onTile((int)(intptr_t)lv_event_get_user_data(event));
}

void navEvent(lv_event_t *event) {
  if (consumePressLost(event) || clickSuppressed) return;
  if (onPageStep) onPageStep((int)(intptr_t)lv_event_get_user_data(event));
}

lv_obj_t *diagLine(int y, const lv_font_t *font, uint32_t colour) {
  lv_obj_t *text = decktheme::label(diagScreen, font, colour);
  lv_obj_set_width(text, Width);
  lv_obj_set_pos(text, 0, y);
  lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  return text;
}

void showCrosshair(bool visible) {
  lv_opa_t opacity = visible ? LV_OPA_COVER : LV_OPA_TRANSP;
  lv_obj_set_style_opa(diagRing, opacity, LV_PART_MAIN);
  lv_obj_set_style_opa(diagCrossH, opacity, LV_PART_MAIN);
  lv_obj_set_style_opa(diagCrossV, opacity, LV_PART_MAIN);
}

void buildDiagnosticScreen() {
  diagScreen = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(diagScreen, lv_color_hex(decktheme::Background), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(diagScreen, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_remove_flag(diagScreen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(diagScreen, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(diagScreen, 0, LV_PART_MAIN);

  // Crosshair for the alignment check. No control is created on this screen at
  // all, so there is nothing here that could receive a click.
  diagRing = lv_obj_create(diagScreen);
  lv_obj_remove_flag(diagRing, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(diagRing, 2 * AlignmentRadius, 2 * AlignmentRadius);
  lv_obj_set_style_bg_opa(diagRing, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_color(diagRing, lv_color_hex(decktheme::Accent), LV_PART_MAIN);
  lv_obj_set_style_border_width(diagRing, 2, LV_PART_MAIN);
  lv_obj_set_style_radius(diagRing, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_pad_all(diagRing, 0, LV_PART_MAIN);

  diagCrossH = lv_obj_create(diagScreen);
  diagCrossV = lv_obj_create(diagScreen);
  lv_obj_t *bars[2] = {diagCrossH, diagCrossV};
  for (int i = 0; i < 2; i++) {
    lv_obj_remove_flag(bars[i], LV_OBJ_FLAG_SCROLLABLE);
    decktheme::styleCard(bars[i], decktheme::Accent, 0);
  }
  lv_obj_set_size(diagCrossH, 2 * AlignmentRadius + 16, 2);
  lv_obj_set_size(diagCrossV, 2, 2 * AlignmentRadius + 16);

  lv_obj_t *title = diagLine(2, &lv_font_montserrat_20, decktheme::Ink);
  lv_label_set_text(title, "Touch check");
  lv_obj_t *warning = diagLine(26, &lv_font_montserrat_14, decktheme::Accent);
  lv_label_set_text(warning, "SHORTCUTS DISABLED");

  diagPrompt = diagLine(98, &lv_font_montserrat_16, decktheme::Ink);
  diagState = diagLine(124, &lv_font_montserrat_16, decktheme::Ink);
  diagRaw = diagLine(148, &lv_font_montserrat_14, decktheme::Muted);
  diagMapped = diagLine(166, &lv_font_montserrat_14, decktheme::Muted);
  diagError = diagLine(184, &lv_font_montserrat_14, decktheme::Muted);
  diagCounters = diagLine(206, &lv_font_montserrat_14, decktheme::Muted);
  diagTiming = diagLine(224, &lv_font_montserrat_14, decktheme::Muted);
  diagReject = diagLine(242, &lv_font_montserrat_14, decktheme::Muted);

  lv_obj_t *hint = diagLine(292, &lv_font_montserrat_14, decktheme::Muted);
  lv_label_set_text(hint, "Finish from the Mac app");
}
}  // namespace

bool begin() {
  SPI.begin(LCD_SCK, LCD_MISO, LCD_MOSI, LCD_CS);
  panel.init(Width, Height);
  // Paired with decktouch::rawToScreen: both describe the same 180-degree
  // rotation for the USB-connector-down mounting, and nothing else rotates.
  panel.setRotation(2);
  panel.invertDisplay(true);
  panel.setSPISpeed(40000000);

  for (int i = 0; i < Tiles; i++) {
    tileIconPixels[i] = (uint16_t *)heap_caps_malloc(deckicons::Pixels * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!tileIconPixels[i]) tileIconPixels[i] = (uint16_t *)malloc(deckicons::Pixels * 2);
    if (!tileIconPixels[i]) return false;
  }

  lv_init();
  lv_tick_set_cb(tickCb);

  void *buffer1 = heap_caps_malloc(BufferPixels * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  void *buffer2 = heap_caps_malloc(BufferPixels * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  if (!buffer1 || !buffer2) return false;

  display = lv_display_create(Width, Height);
  if (!display) return false;
  lv_display_set_flush_cb(display, flushCb);
  lv_display_set_buffers(display, buffer1, buffer2, BufferPixels * 2, LV_DISPLAY_RENDER_MODE_PARTIAL);

  pointer = lv_indev_create();
  if (!pointer) return false;
  lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(pointer, readCb);
  lv_indev_set_display(pointer, display);

  deck = decktheme::build(tileEvent, navEvent);
  buildDiagnosticScreen();
  lv_screen_load(deck.root);
  started = true;
  return true;
}

void tick() {
  if (started) lv_timer_handler();
}

void setPointer(bool pressed, int x, int y, bool suppressClick) {
  if (pressed && !pointerDown) clickSuppressed = false;
  if (suppressClick) clickSuppressed = true;
  pointerDown = pressed;
  pointerX = x;
  pointerY = y;
}

void showDeck() {
  if (started) lv_screen_load(deck.root);
}

void showDiagnostics(bool alignmentMode) {
  if (!started) return;
  showCrosshair(alignmentMode);
  lv_screen_load(diagScreen);
}

void setHeader(const char *pageName, int pageIndex, int pageCount) {
  if (started) decktheme::setHeader(deck, pageName, pageIndex, pageCount);
}

void setHidReady(bool isReady) {
  if (started) decktheme::setHidReady(deck, isReady);
}

void setTile(int index, const char *label, uint32_t accentRgb, const char *iconName,
             const uint8_t *image) {
  if (!started || index < 0 || index >= Tiles) return;
  decktheme::setTile(deck, index, label, accentRgb, iconName, image,
                     tileIconPixels[index], tileIconDsc[index]);
}

void setDiagnostics(const DiagnosticView &view) {
  if (!started) return;
  char text[96];

  if (view.target >= 0 && view.target < AlignmentTargets) {
    int cx = AlignmentTargetXY[view.target][0], cy = AlignmentTargetXY[view.target][1];
    lv_obj_set_pos(diagRing, cx - AlignmentRadius, cy - AlignmentRadius);
    lv_obj_set_pos(diagCrossH, cx - AlignmentRadius - 8, cy - 1);
    lv_obj_set_pos(diagCrossV, cx - 1, cy - AlignmentRadius - 8);
    showCrosshair(true);
    snprintf(text, sizeof(text), "Tap target %d of %d", view.target + 1, AlignmentTargets);
  } else {
    showCrosshair(false);
    snprintf(text, sizeof(text), "%s",
             view.target >= AlignmentTargets ? "Alignment check complete" : "Move the case; do not touch");
  }
  lv_label_set_text(diagPrompt, text);

  lv_label_set_text(diagState, view.down ? "Contact: DOWN" : "Contact: up");
  snprintf(text, sizeof(text), "raw %d, %d", view.rawX, view.rawY);
  lv_label_set_text(diagRaw, text);
  snprintf(text, sizeof(text), "mapped %d, %d", view.x, view.y);
  lv_label_set_text(diagMapped, text);

  if (view.haveLastError) snprintf(text, sizeof(text), "target error %+d, %+d px", view.lastErrorX, view.lastErrorY);
  else snprintf(text, sizeof(text), "target error -");
  lv_label_set_text(diagError, text);

  snprintf(text, sizeof(text), "contacts %lu  samples %lu  errors %lu",
           (unsigned long)view.contacts, (unsigned long)view.downSamples, (unsigned long)view.readErrors);
  lv_label_set_text(diagCounters, text);
  snprintf(text, sizeof(text), "interval %lu-%lu ms  irq %lu",
           (unsigned long)view.minIntervalMs, (unsigned long)view.maxIntervalMs, (unsigned long)view.irqs);
  lv_label_set_text(diagTiming, text);
  snprintf(text, sizeof(text), "hits %d/%d  last reject %s", view.hits, AlignmentTargets,
           view.rejectReason ? view.rejectReason : "none");
  lv_label_set_text(diagReject, text);
}

bool captureFramebuffer(uint16_t *out, uint32_t pixels) {
  if (!started || pixels != (uint32_t)Width * Height) return false;
  // LVGL renders in partial buffers, so there is no standing framebuffer to
  // copy. A snapshot re-renders the live screen into one.
  lv_draw_buf_t *snapshot = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
  if (!snapshot) return false;
  memcpy(out, snapshot->data, (size_t)pixels * 2);
  lv_draw_buf_destroy(snapshot);
  return true;
}

}  // namespace deckui
