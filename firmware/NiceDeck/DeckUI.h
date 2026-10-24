#pragma once
#include <stdint.h>

// LVGL interface for the 240x320 panel. Everything that draws or receives a
// pointer event lives behind this header; Deck.cpp keeps configuration, the
// serial protocol, HID and the touch sensor, and never calls LVGL directly
// except through these functions.
//
// Ownership: LVGL is driven only from deckui::tick(), which is called from the
// single Arduino loop. There is no LVGL task and no locking, because nothing
// else may touch LVGL state. The flush callback is the one place that runs
// during rendering, and it is allowed to drain the outbound serial queue only
// (deckui::onFlush), which cannot re-enter LVGL.
namespace deckui {

// Set before begin(). Called from tick() when LVGL delivers a completed,
// deliberate interaction: released inside the same control it was pressed on,
// not dragged away and not held.
extern void (*onTile)(int index);
extern void (*onPageStep)(int delta);
// Drains outbound USB serial while a flush is in progress. Must not use LVGL.
extern void (*onFlush)();

bool begin();
void tick();

// Pointer state produced by DeckTouch's contact filter. suppressClick marks a
// contact that must not deliver an action (a hold), while still letting LVGL
// clear its own pressed state normally.
void setPointer(bool pressed, int x, int y, bool suppressClick);

// --- Main deck screen ---
void showDeck();
void setHeader(const char *pageName, int pageIndex, int pageCount);
void setHidReady(bool ready);
// image is 3200 bytes of big-endian RGB565 (40x40) or nullptr to draw iconName.
void setTile(int index, const char *label, uint32_t accentRgb, const char *iconName,
             const uint8_t *image);

// --- Diagnostics screen (no action handlers are attached to it at all) ---
void showDiagnostics(bool alignmentMode);
struct DiagnosticView {
  int rawX, rawY, x, y;
  bool down;
  uint32_t contacts, downSamples, readErrors, irqs, polls;
  uint32_t minIntervalMs, maxIntervalMs;
  const char *rejectReason;
  int target;          // 0..4 while running, AlignmentTargets when complete, -1 for motion mode
  int hits;
  int lastErrorX, lastErrorY;
  bool haveLastError;
};
void setDiagnostics(const DiagnosticView &view);

// Raw framebuffer readback for the screenshot protocol command. Returns false
// when no capture buffer could be allocated.
bool captureFramebuffer(uint16_t *out, uint32_t pixels);

}  // namespace deckui
