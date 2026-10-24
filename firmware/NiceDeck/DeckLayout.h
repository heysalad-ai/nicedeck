#pragma once
#include <stdint.h>

// Screen geometry shared by the LVGL interface, the diagnostics screen and the
// host tests, so a layout change cannot leave the touch map behind. Values are
// screen pixels on the 240x320 panel, which measures about 30.6 x 40.8 mm and
// therefore renders roughly 7.8 pixels per millimetre.
namespace decklayout {

constexpr int Width = 240, Height = 320;

// Header: product name, page name and the USB HID indicator.
constexpr int HeaderHeight = 40;

// Six tiles, two columns by three rows.
constexpr int Columns = 2, Rows = 3, Tiles = Columns * Rows;
constexpr int Margin = 8, Gap = 8;
constexpr int TileWidth = (Width - 2 * Margin - Gap) / Columns;   // 108 px, ~13.8 mm
constexpr int GridTop = HeaderHeight + 4;                          // 44
constexpr int TileHeight = 70;                                     // ~8.9 mm
constexpr int GridBottom = GridTop + Rows * TileHeight + (Rows - 1) * Gap;  // 270

// Each tile's touchable area is extended by half the gap so neighbouring
// targets meet without overlapping and there is no dead strip between them.
// LVGL applies this with lv_obj_set_ext_click_area; the value is shared here so
// tileAt() and the tests describe the same regions the interface uses.
constexpr int TileTouchExtend = Gap / 2;  // 4

// Page navigation occupies the bottom band.
constexpr int NavTop = GridBottom + 4;    // 274
constexpr int NavHeight = Height - NavTop - 2;  // 44, ~5.6 mm
constexpr int NavButtonWidth = 84;
constexpr int NavTouchExtend = 6;

constexpr int tileX(int index) { return Margin + (index % Columns) * (TileWidth + Gap); }
constexpr int tileY(int index) { return GridTop + (index / Columns) * (TileHeight + Gap); }

// Which tile contains a screen point, or -1. Mirrors the extended click areas
// LVGL uses, so diagnostics and tests agree with the live interface.
inline int tileAt(int x, int y) {
  for (int i = 0; i < Tiles; i++) {
    int left = tileX(i) - TileTouchExtend, top = tileY(i) - TileTouchExtend;
    if (x >= left && x < left + TileWidth + 2 * TileTouchExtend &&
        y >= top && y < top + TileHeight + 2 * TileTouchExtend)
      return i;
  }
  return -1;
}

// -1 none, 0 previous page, 1 next page.
inline int navAt(int x, int y) {
  if (y < NavTop - NavTouchExtend || y >= NavTop + NavHeight + NavTouchExtend) return -1;
  if (x >= Margin - NavTouchExtend && x < Margin + NavButtonWidth + NavTouchExtend) return 0;
  int right = Width - Margin - NavButtonWidth;
  if (x >= right - NavTouchExtend && x < right + NavButtonWidth + NavTouchExtend) return 1;
  return -1;
}

// Alignment-test targets. These are deliberately independent of tileAt(): the
// diagnostic must be able to show that touch lands where it is drawn even when
// the interface's own hit map is wrong, and the previous firmware could not,
// because it advanced the test only when the shortcut-button map produced a
// tap. Targets sit clear of the tile boundaries and the screen edges.
constexpr int AlignmentTargets = 5;
constexpr int AlignmentTargetXY[AlignmentTargets][2] = {
    {40, 60}, {200, 60}, {120, 160}, {40, 260}, {200, 260}};

// Acceptance radius for advancing the test, about 3.6 mm. This is a usability
// allowance for advancing through the sequence only. The measured error for
// every target is always reported in full, and no correction is derived from
// it or stored: this verifies alignment, it does not calibrate it.
constexpr int AlignmentRadius = 28;

inline bool alignmentHit(int index, int x, int y) {
  if (index < 0 || index >= AlignmentTargets) return false;
  int dx = x - AlignmentTargetXY[index][0], dy = y - AlignmentTargetXY[index][1];
  return dx * dx + dy * dy <= AlignmentRadius * AlignmentRadius;
}

}  // namespace decklayout
