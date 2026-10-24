#pragma once
#include <lvgl.h>
#include <stdio.h>

#include "DeckControls.h"
#include "DeckIcons.h"
#include "DeckLayout.h"

// The deck screen's appearance: palette, tile styling and chrome. It depends
// only on LVGL, so the firmware and the host preview tool build the same
// screen from the same code and a rendering produced off-device is a faithful
// picture of what the panel shows.
//
// Visual direction follows design/ui-concepts/NiceDeck-warm-everyday-v1.png,
// adapted to the real 2-inch display rather than scaled from the mockup:
// a warm off-white ground, pastel tiles derived from each button's own accent
// colour, dark readable text, consistent rounded icons and restrained press
// feedback.
namespace decktheme {

constexpr uint32_t Background = 0xF7F3EA;  // warm off-white
constexpr uint32_t Surface = 0xEFE7DE;     // header and navigation card
constexpr uint32_t Ink = 0x313031;         // dark readable text
constexpr uint32_t Muted = 0x6B6D63;
constexpr uint32_t Accent = 0xC2603F;      // terracotta, for diagnostics only
constexpr uint32_t Online = 0x4FA96B, Offline = 0x9A968E;

// Pastel tiles keep the user's own accent colour recognisable while staying
// light enough for dark text: 19% of the accent over the background, and 40%
// when pressed.
constexpr int TileTint = 19, TilePressedTint = 40, IconTint = 85;

inline uint16_t to565(uint32_t rgb) {
  return (uint16_t)(((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F));
}

// weight% of `a` over `b`, in 8-bit channels.
inline uint32_t blend(uint32_t a, uint32_t b, int weight) {
  uint32_t r = (((a >> 16) & 255) * (uint32_t)weight + ((b >> 16) & 255) * (uint32_t)(100 - weight)) / 100;
  uint32_t g = (((a >> 8) & 255) * (uint32_t)weight + ((b >> 8) & 255) * (uint32_t)(100 - weight)) / 100;
  uint32_t c = ((a & 255) * (uint32_t)weight + (b & 255) * (uint32_t)(100 - weight)) / 100;
  return (r << 16) | (g << 8) | c;
}

inline void styleCard(lv_obj_t *obj, uint32_t fill, int radius) {
  lv_obj_set_style_bg_color(obj, lv_color_hex(fill), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(obj, radius, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(obj, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
}

inline lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t colour) {
  lv_obj_t *text = lv_label_create(parent);
  lv_obj_set_style_text_font(text, font, LV_PART_MAIN);
  lv_obj_set_style_text_color(text, lv_color_hex(colour), LV_PART_MAIN);
  lv_label_set_text(text, "");
  return text;
}

struct Screen {
  lv_obj_t *root = nullptr;
  deckcontrols::Controls controls{};
  lv_obj_t *tileLabels[decklayout::Tiles] = {};
  lv_obj_t *tileIcons[decklayout::Tiles] = {};
  lv_obj_t *title = nullptr, *pageName = nullptr, *hidDot = nullptr, *pageCount = nullptr;
};

inline Screen build(lv_event_cb_t tileEvent, lv_event_cb_t navEvent) {
  using namespace decklayout;
  Screen screen;
  screen.root = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(screen.root, lv_color_hex(Background), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen.root, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_remove_flag(screen.root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(screen.root, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(screen.root, 0, LV_PART_MAIN);

  screen.title = label(screen.root, &lv_font_montserrat_20, Ink);
  lv_label_set_text(screen.title, "NiceDeck");
  lv_obj_set_width(screen.title, Width);
  lv_obj_set_pos(screen.title, 0, 1);
  lv_obj_set_style_text_align(screen.title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

  screen.hidDot = lv_obj_create(screen.root);
  lv_obj_remove_flag(screen.hidDot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(screen.hidDot, 8, 8);
  styleCard(screen.hidDot, Offline, 4);
  lv_obj_set_pos(screen.hidDot, Width - 22, 10);

  screen.pageName = label(screen.root, &lv_font_montserrat_14, Muted);
  lv_obj_set_width(screen.pageName, Width);
  lv_obj_set_pos(screen.pageName, 0, 23);
  lv_obj_set_style_text_align(screen.pageName, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

  screen.controls = deckcontrols::create(screen.root, tileEvent, navEvent);

  for (int i = 0; i < Tiles; i++) {
    lv_obj_t *tile = screen.controls.tiles[i];
    lv_obj_set_style_pad_all(tile, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(tile, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(tile, 14, LV_PART_MAIN);

    screen.tileIcons[i] = lv_image_create(tile);
    lv_obj_set_pos(screen.tileIcons[i], (TileWidth - deckicons::Size) / 2, 3);

    lv_obj_t *text = label(tile, &lv_font_montserrat_14, Ink);
    lv_label_set_long_mode(text, LV_LABEL_LONG_DOT);
    lv_obj_set_width(text, TileWidth - 6);
    lv_obj_set_pos(text, 3, TileHeight - 22);
    lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    screen.tileLabels[i] = text;
  }

  const char *glyphs[2] = {LV_SYMBOL_LEFT, LV_SYMBOL_RIGHT};
  lv_obj_t *navButtons[2] = {screen.controls.navPrevious, screen.controls.navNext};
  for (int i = 0; i < 2; i++) {
    styleCard(navButtons[i], Surface, 12);
    lv_obj_set_style_bg_color(navButtons[i], lv_color_hex(blend(Ink, Surface, 12)),
                              LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_t *glyph = label(navButtons[i], &lv_font_montserrat_16, Muted);
    lv_label_set_text(glyph, glyphs[i]);
    lv_obj_center(glyph);
  }

  screen.pageCount = label(screen.root, &lv_font_montserrat_14, Muted);
  lv_obj_set_width(screen.pageCount, Width);
  lv_obj_set_pos(screen.pageCount, 0, NavTop + (NavHeight - 16) / 2);
  lv_obj_set_style_text_align(screen.pageCount, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  return screen;
}

inline void setHeader(Screen &screen, const char *pageName, int pageIndex, int pageCount) {
  lv_label_set_text(screen.pageName, pageName ? pageName : "");
  char text[24];
  snprintf(text, sizeof(text), "%d / %d", pageIndex + 1, pageCount);
  lv_label_set_text(screen.pageCount, text);
  lv_opa_t opacity = pageCount <= 1 ? LV_OPA_40 : LV_OPA_COVER;
  lv_obj_set_style_opa(screen.controls.navPrevious, opacity, LV_PART_MAIN);
  lv_obj_set_style_opa(screen.controls.navNext, opacity, LV_PART_MAIN);
}

inline void setHidReady(Screen &screen, bool ready) {
  lv_obj_set_style_bg_color(screen.hidDot, lv_color_hex(ready ? Online : Offline), LV_PART_MAIN);
}

// Apply a button's appearance. `iconPixels` receives the rendered 40x40 glyph
// unless `image` supplies the user's own 40x40 big-endian RGB565 artwork.
inline void setTile(Screen &screen, int index, const char *text, uint32_t accent,
                    const char *iconName, const uint8_t *image,
                    uint16_t *iconPixels, lv_image_dsc_t &descriptor) {
  using namespace decklayout;
  if (index < 0 || index >= Tiles) return;
  uint32_t tint = blend(accent, Background, TileTint);
  uint32_t pressed = blend(accent, Background, TilePressedTint);
  uint32_t ink = blend(accent, Ink, IconTint);

  styleCard(screen.controls.tiles[index], tint, 14);
  lv_obj_set_style_bg_color(screen.controls.tiles[index], lv_color_hex(pressed),
                            LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_border_color(screen.controls.tiles[index], lv_color_hex(ink),
                                LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_border_width(screen.controls.tiles[index], 2, LV_PART_MAIN | LV_STATE_PRESSED);

  lv_label_set_text(screen.tileLabels[index], text ? text : "");

  if (image) {
    // Stored artwork is big-endian RGB565; LVGL wants the panel's native order.
    for (int i = 0; i < deckicons::Pixels; i++)
      iconPixels[i] = (uint16_t)((image[i * 2] << 8) | image[i * 2 + 1]);
  } else {
    deckicons::draw(iconPixels, deckicons::resolve(iconName, nullptr), to565(ink), to565(tint));
  }
  descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
  descriptor.header.cf = LV_COLOR_FORMAT_RGB565;
  descriptor.header.flags = 0;
  descriptor.header.w = deckicons::Size;
  descriptor.header.h = deckicons::Size;
  descriptor.header.stride = deckicons::Size * 2;
  descriptor.data_size = deckicons::Pixels * 2;
  descriptor.data = (const uint8_t *)iconPixels;
  lv_image_set_src(screen.tileIcons[index], &descriptor);
  lv_obj_invalidate(screen.tileIcons[index]);
}

}  // namespace decktheme
