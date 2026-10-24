// Renders the deck screen off-device, using the same LVGL version, the same
// DeckTheme/DeckControls/DeckIcons code and the same layout constants the
// firmware uses. The output is therefore a faithful picture of what the panel
// draws, which matters here because the migration is handed over unflashed.
//
// It is a picture, not evidence of usability: it says nothing about touch.
//
//   deck-preview out.ppm "PageName" pageIndex pageCount [--pressed N]
//                "Label|#RRGGBB|icon|firstActionValue" x6
#include <lvgl.h>

#include "DeckLayout.h"
#include "DeckTheme.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
std::uint32_t clockMs = 0;
std::uint32_t tickCb() { return clockMs; }

std::uint16_t framebuffer[decklayout::Width * decklayout::Height];

void flushCb(lv_display_t *display, const lv_area_t *area, std::uint8_t *pixels) {
  auto *source = reinterpret_cast<std::uint16_t *>(pixels);
  for (int y = area->y1; y <= area->y2; y++)
    for (int x = area->x1; x <= area->x2; x++)
      framebuffer[y * decklayout::Width + x] = source[(y - area->y1) * (area->x2 - area->x1 + 1) + (x - area->x1)];
  lv_display_flush_ready(display);
}

void nothing(lv_event_t *) {}

std::vector<std::string> split(const std::string &text, char separator) {
  std::vector<std::string> parts;
  std::string current;
  for (char c : text) {
    if (c == separator) { parts.push_back(current); current.clear(); }
    else current += c;
  }
  parts.push_back(current);
  return parts;
}
}  // namespace

int main(int argc, char **argv) {
  if (argc < 5) {
    std::fprintf(stderr, "usage: deck-preview out.ppm page index count [--pressed N] tile...\n");
    return 2;
  }
  const char *output = argv[1];
  const char *pageName = argv[2];
  int pageIndex = std::atoi(argv[3]), pageCount = std::atoi(argv[4]);
  int pressed = -1;
  std::vector<std::string> tiles;
  for (int i = 5; i < argc; i++) {
    if (std::strcmp(argv[i], "--pressed") == 0 && i + 1 < argc) { pressed = std::atoi(argv[++i]); continue; }
    tiles.push_back(argv[i]);
  }

  lv_init();
  lv_tick_set_cb(tickCb);
  static std::uint8_t buffer1[decklayout::Width * 32 * 2];
  static std::uint8_t buffer2[decklayout::Width * 32 * 2];
  lv_display_t *display = lv_display_create(decklayout::Width, decklayout::Height);
  lv_display_set_flush_cb(display, flushCb);
  lv_display_set_buffers(display, buffer1, buffer2, sizeof(buffer1), LV_DISPLAY_RENDER_MODE_PARTIAL);

  decktheme::Screen screen = decktheme::build(nothing, nothing);
  lv_screen_load(screen.root);
  decktheme::setHeader(screen, pageName, pageIndex, pageCount);
  decktheme::setHidReady(screen, true);

  static std::uint16_t iconPixels[decklayout::Tiles][deckicons::Pixels];
  static lv_image_dsc_t descriptors[decklayout::Tiles];
  for (int i = 0; i < decklayout::Tiles; i++) {
    std::string spec = i < (int)tiles.size() ? tiles[(std::size_t)i] : "";
    std::vector<std::string> parts = split(spec, '|');
    std::string label = parts.size() > 0 ? parts[0] : "";
    std::string colour = parts.size() > 1 ? parts[1] : "#888888";
    std::string icon = parts.size() > 2 ? parts[2] : "key";
    std::string action = parts.size() > 3 ? parts[3] : "";
    std::uint32_t accent = (std::uint32_t)std::strtoul(colour.c_str() + 1, nullptr, 16);
    decktheme::setTile(screen, i, label.c_str(), accent,
                       deckicons::resolve(icon.c_str(), action.c_str()), nullptr,
                       iconPixels[i], descriptors[i]);
    if (i == pressed) lv_obj_add_state(screen.controls.tiles[i], LV_STATE_PRESSED);
  }

  for (int i = 0; i < 40; i++) { lv_timer_handler(); clockMs += 20; }

  FILE *file = std::fopen(output, "wb");
  if (!file) { std::perror("open"); return 1; }
  std::fprintf(file, "P6\n%d %d\n255\n", decklayout::Width, decklayout::Height);
  for (int i = 0; i < decklayout::Width * decklayout::Height; i++) {
    std::uint16_t p = framebuffer[i];
    unsigned char rgb[3] = {(unsigned char)(((p >> 11) & 31) * 255 / 31),
                            (unsigned char)(((p >> 5) & 63) * 255 / 63),
                            (unsigned char)((p & 31) * 255 / 31)};
    std::fwrite(rgb, 1, 3, file);
  }
  std::fclose(file);
  std::printf("wrote %s\n", output);
  return 0;
}
