#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

// The sixteen built-in tile glyphs, drawn as vectors into a 40x40 RGB565
// buffer.
//
// The geometry is carried over unchanged from the Adafruit_GFX drawing the
// previous firmware used, but the rasteriser is local and depends on nothing:
// no Arduino, no display driver, no LVGL. That keeps the icons identical
// between the device and the host preview and test tools, so what is rendered
// off-device is what the panel shows.
namespace deckicons {

constexpr int Size = 40;
constexpr int Pixels = Size * Size;

class Canvas {
public:
  explicit Canvas(uint16_t *pixels) : buffer(pixels) {}

  void fill(uint16_t colour) {
    for (int i = 0; i < Pixels; i++) buffer[i] = colour;
  }
  void pixel(int x, int y, uint16_t colour) {
    if (x >= 0 && x < Size && y >= 0 && y < Size) buffer[y * Size + x] = colour;
  }
  void hLine(int x, int y, int w, uint16_t colour) {
    for (int i = 0; i < w; i++) pixel(x + i, y, colour);
  }
  void vLine(int x, int y, int h, uint16_t colour) {
    for (int i = 0; i < h; i++) pixel(x, y + i, colour);
  }
  void fillRect(int x, int y, int w, int h, uint16_t colour) {
    for (int j = 0; j < h; j++) hLine(x, y + j, w, colour);
  }
  void rect(int x, int y, int w, int h, uint16_t colour) {
    hLine(x, y, w, colour); hLine(x, y + h - 1, w, colour);
    vLine(x, y, h, colour); vLine(x + w - 1, y, h, colour);
  }
  void line(int x0, int y0, int x1, int y1, uint16_t colour) {
    int dx = x1 - x0 < 0 ? x0 - x1 : x1 - x0;
    int dy = y1 - y0 < 0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int error = dx - dy;
    for (;;) {
      pixel(x0, y0, colour);
      if (x0 == x1 && y0 == y1) break;
      int doubled = 2 * error;
      if (doubled > -dy) { error -= dy; x0 += sx; }
      if (doubled < dx) { error += dx; y0 += sy; }
    }
  }
  void thickLine(int x0, int y0, int x1, int y1, uint16_t colour) {
    line(x0, y0, x1, y1, colour);
    line(x0 + 1, y0, x1 + 1, y1, colour);
  }
  void circle(int cx, int cy, int r, uint16_t colour) {
    int x = r, y = 0, error = 1 - r;
    while (x >= y) {
      const int px[8] = {cx + x, cx + y, cx - y, cx - x, cx - x, cx - y, cx + y, cx + x};
      const int py[8] = {cy + y, cy + x, cy + x, cy + y, cy - y, cy - x, cy - x, cy - y};
      for (int i = 0; i < 8; i++) pixel(px[i], py[i], colour);
      y++;
      if (error < 0) error += 2 * y + 1;
      else { x--; error += 2 * (y - x) + 1; }
    }
  }
  void fillCircle(int cx, int cy, int r, uint16_t colour) {
    for (int dy = -r; dy <= r; dy++)
      for (int dx = -r; dx <= r; dx++)
        if (dx * dx + dy * dy <= r * r) pixel(cx + dx, cy + dy, colour);
  }
  void roundRect(int x, int y, int w, int h, int r, uint16_t colour) {
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    hLine(x + r, y, w - 2 * r, colour); hLine(x + r, y + h - 1, w - 2 * r, colour);
    vLine(x, y + r, h - 2 * r, colour); vLine(x + w - 1, y + r, h - 2 * r, colour);
    quadrant(x + r, y + r, r, colour, 1);
    quadrant(x + w - r - 1, y + r, r, colour, 2);
    quadrant(x + w - r - 1, y + h - r - 1, r, colour, 4);
    quadrant(x + r, y + h - r - 1, r, colour, 8);
  }
  void fillRoundRect(int x, int y, int w, int h, int r, uint16_t colour) {
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    fillRect(x + r, y, w - 2 * r, h, colour);
    for (int dy = 0; dy < h; dy++) {
      int inset = 0;
      int top = y + r, bottom = y + h - r - 1;
      int centreY = dy + y < top ? top : (dy + y > bottom ? bottom : dy + y);
      int offset = dy + y - centreY;
      if (offset) {
        int span = r;
        while (span > 0 && span * span + offset * offset > r * r) span--;
        inset = r - span;
      }
      hLine(x + inset, y + dy, r - inset, colour);
      hLine(x + w - r, y + dy, r - inset, colour);
    }
  }
  void triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t colour) {
    line(x0, y0, x1, y1, colour); line(x1, y1, x2, y2, colour); line(x2, y2, x0, y0, colour);
  }
  void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t colour) {
    // Sort by y, then walk the scanlines between the long edge and the two
    // short edges.
    if (y0 > y1) { swap(x0, x1); swap(y0, y1); }
    if (y1 > y2) { swap(x1, x2); swap(y1, y2); }
    if (y0 > y1) { swap(x0, x1); swap(y0, y1); }
    if (y0 == y2) {
      int left = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
      int right = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
      hLine(left, y0, right - left + 1, colour);
      return;
    }
    for (int y = y0; y <= y2; y++) {
      int a = interpolate(y, y0, y2, x0, x2);
      int b = y < y1 ? interpolate(y, y0, y1, x0, x1) : interpolate(y, y1, y2, x1, x2);
      if (a > b) swap(a, b);
      hLine(a, y, b - a + 1, colour);
    }
  }

private:
  static void swap(int &a, int &b) { int t = a; a = b; b = t; }
  static int interpolate(int value, int from, int to, int a, int b) {
    return to == from ? a : a + (b - a) * (value - from) / (to - from);
  }
  void quadrant(int cx, int cy, int r, uint16_t colour, int mask) {
    int x = r, y = 0, error = 1 - r;
    while (x >= y) {
      if (mask & 4) { pixel(cx + x, cy + y, colour); pixel(cx + y, cy + x, colour); }
      if (mask & 2) { pixel(cx + x, cy - y, colour); pixel(cx + y, cy - x, colour); }
      if (mask & 8) { pixel(cx - y, cy + x, colour); pixel(cx - x, cy + y, colour); }
      if (mask & 1) { pixel(cx - y, cy - x, colour); pixel(cx - x, cy - y, colour); }
      y++;
      if (error < 0) error += 2 * y + 1;
      else { x--; error += 2 * (y - x) + 1; }
    }
  }
  uint16_t *buffer;
};

// Point at `radius` and `angle` radians from a glyph's centre, matching the
// trigonometry the original star and gear used.
inline void polar(double angle, int radius, int &dx, int &dy) {
  dx = (int)(cos(angle) * radius);
  dy = (int)(sin(angle) * radius);
}
constexpr double Pi = 3.14159265358979323846;

inline bool same(const char *a, const char *b) { return a && b && strcmp(a, b) == 0; }

// Some stock buttons carry a generic icon name in the configuration but read
// better with a specific glyph. Resolving it here keeps the device and the
// off-device preview showing the same thing.
inline const char *resolve(const char *icon, const char *firstActionValue) {
  if (same(icon, "bolt") && same(firstActionValue, "cmd+z")) return "undo";
  if (same(icon, "folder") && same(firstActionValue, "cmd+s")) return "save";
  if (same(icon, "star") && same(firstActionValue, "cmd+shift+4")) return "capture";
  return icon ? icon : "";
}

// Draw `name` filling the canvas. `background` is used to punch holes so a
// glyph reads correctly against its tile colour.
inline void draw(uint16_t *pixels, const char *name, uint16_t colour, uint16_t background) {
  Canvas canvas(pixels);
  canvas.fill(background);
  const int x = 0, y = 0;

  if (same(name, "copy")) {
    canvas.fillRoundRect(x + 4, y + 3, 24, 29, 4, colour); canvas.fillRoundRect(x + 7, y + 6, 18, 23, 2, background);
    canvas.fillRoundRect(x + 13, y + 11, 24, 28, 4, colour); canvas.fillRoundRect(x + 16, y + 14, 18, 22, 2, background);
  } else if (same(name, "paste")) {
    canvas.fillRoundRect(x + 6, y + 7, 29, 32, 5, colour); canvas.fillRoundRect(x + 9, y + 10, 23, 26, 2, background);
    canvas.fillRoundRect(x + 14, y + 2, 13, 11, 4, colour); canvas.fillCircle(x + 20, y + 6, 2, background);
  } else if (same(name, "undo")) {
    for (int j = 0; j < 4; j++) canvas.circle(x + 21, y + 24, 12 + j, colour);
    canvas.fillRect(x + 1, y + 22, 23, 18, background);
    canvas.fillTriangle(x + 3, y + 11, x + 16, y + 1, x + 16, y + 21, colour);
    canvas.fillRect(x + 12, y + 9, 10, 5, colour);
  } else if (same(name, "save")) {
    canvas.fillRoundRect(x + 5, y + 3, 31, 35, 4, colour); canvas.fillRect(x + 11, y + 5, 17, 11, background);
    canvas.fillRoundRect(x + 12, y + 24, 17, 14, 2, background);
  } else if (same(name, "capture")) {
    for (int j = 0; j < 3; j++) {
      canvas.hLine(x + 4, y + 4 + j, 10, colour); canvas.vLine(x + 4 + j, y + 4, 10, colour);
      canvas.hLine(x + 26, y + 4 + j, 10, colour); canvas.vLine(x + 33 + j, y + 4, 10, colour);
      canvas.hLine(x + 4, y + 33 + j, 10, colour); canvas.vLine(x + 4 + j, y + 26, 10, colour);
      canvas.hLine(x + 26, y + 33 + j, 10, colour); canvas.vLine(x + 33 + j, y + 26, 10, colour);
    }
    canvas.fillCircle(x + 20, y + 20, 5, colour);
  } else if (same(name, "mic")) {
    canvas.roundRect(x + 15, y + 3, 11, 23, 5, colour); canvas.roundRect(x + 10, y + 15, 21, 17, 7, colour);
    canvas.fillRect(x + 8, y + 12, 7, 8, background); canvas.fillRect(x + 26, y + 12, 7, 8, background);
    canvas.thickLine(x + 20, y + 32, x + 20, y + 37, colour); canvas.thickLine(x + 13, y + 37, x + 27, y + 37, colour);
  } else if (same(name, "video")) {
    canvas.roundRect(x + 3, y + 9, 24, 23, 4, colour);
    canvas.triangle(x + 28, y + 16, x + 37, y + 10, x + 37, y + 31, colour);
  } else if (same(name, "play")) {
    canvas.fillTriangle(x + 12, y + 5, x + 12, y + 35, x + 34, y + 20, colour);
  } else if (same(name, "volume")) {
    canvas.fillRect(x + 3, y + 15, 8, 12, colour);
    canvas.fillTriangle(x + 10, y + 15, x + 21, y + 7, x + 21, y + 34, colour);
    canvas.roundRect(x + 23, y + 10, 12, 23, 6, colour); canvas.fillRect(x + 22, y + 7, 6, 29, background);
  } else if (same(name, "music")) {
    canvas.fillCircle(x + 10, y + 31, 6, colour); canvas.fillCircle(x + 28, y + 27, 6, colour);
    canvas.thickLine(x + 15, y + 6, x + 15, y + 31, colour);
    canvas.thickLine(x + 33, y + 3, x + 33, y + 27, colour);
    canvas.thickLine(x + 15, y + 6, x + 33, y + 3, colour);
  } else if (same(name, "globe")) {
    canvas.circle(x + 20, y + 20, 16, colour); canvas.roundRect(x + 13, y + 4, 15, 33, 7, colour);
    canvas.hLine(x + 5, y + 14, 31, colour); canvas.hLine(x + 5, y + 26, 31, colour);
  } else if (same(name, "app") || same(name, "terminal")) {
    canvas.roundRect(x + 3, y + 6, 34, 29, 4, colour); canvas.hLine(x + 4, y + 13, 32, colour);
    canvas.fillCircle(x + 9, y + 10, 1, colour);
    if (same(name, "terminal")) {
      canvas.thickLine(x + 9, y + 19, x + 15, y + 24, colour);
      canvas.thickLine(x + 15, y + 24, x + 9, y + 29, colour);
      canvas.thickLine(x + 20, y + 29, x + 28, y + 29, colour);
    } else {
      canvas.rect(x + 9, y + 19, 9, 9, colour); canvas.rect(x + 23, y + 19, 8, 3, colour);
      canvas.rect(x + 23, y + 25, 8, 3, colour);
    }
  } else if (same(name, "bolt")) {
    canvas.fillTriangle(x + 23, y + 1, x + 7, y + 23, x + 22, y + 23, colour);
    canvas.fillTriangle(x + 17, y + 17, x + 33, y + 17, x + 15, y + 39, colour);
  } else if (same(name, "folder")) {
    canvas.roundRect(x + 3, y + 12, 34, 23, 4, colour); canvas.roundRect(x + 3, y + 6, 16, 11, 3, colour);
    canvas.hLine(x + 4, y + 17, 32, colour);
  } else if (same(name, "heart")) {
    canvas.fillCircle(x + 12, y + 13, 9, colour); canvas.fillCircle(x + 28, y + 13, 9, colour);
    canvas.fillTriangle(x + 4, y + 17, x + 36, y + 17, x + 20, y + 37, colour);
  } else if (same(name, "star")) {
    int px[10], py[10];
    for (int i = 0; i < 10; i++) {
      int dx = 0, dy = 0;
      polar(-Pi / 2 + i * Pi / 5, i % 2 ? 8 : 18, dx, dy);
      px[i] = x + 20 + dx; py[i] = y + 20 + dy;
    }
    for (int i = 0; i < 10; i++)
      canvas.fillTriangle(x + 20, y + 20, px[i], py[i], px[(i + 1) % 10], py[(i + 1) % 10], colour);
  } else if (same(name, "gear")) {
    canvas.circle(x + 20, y + 20, 12, colour); canvas.circle(x + 20, y + 20, 5, colour);
    for (int i = 0; i < 8; i++) {
      int ix = 0, iy = 0, ox = 0, oy = 0;
      polar(i * Pi / 4, 13, ix, iy);
      polar(i * Pi / 4, 18, ox, oy);
      canvas.thickLine(x + 20 + ix, y + 20 + iy, x + 20 + ox, y + 20 + oy, colour);
    }
  } else {
    // "key" and anything unrecognised.
    canvas.roundRect(x + 5, y + 9, 30, 23, 4, colour);
    for (int i = 0; i < 3; i++) canvas.fillRect(x + 10 + i * 8, y + 15, 4, 4, colour);
    canvas.fillRoundRect(x + 12, y + 24, 16, 3, 1, colour);
  }
}

}  // namespace deckicons
