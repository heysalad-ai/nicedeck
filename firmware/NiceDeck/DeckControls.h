#pragma once
#include <lvgl.h>

#include "DeckLayout.h"

// The interactive controls of the deck screen: six tiles and two page arrows.
// Geometry, touch-area extension and event registration live here so the
// firmware and the host tests build the identical control set. A test that
// passes against these controls is a test of what the device actually runs.
//
// Exactly one action path exists: LV_EVENT_CLICKED, which LVGL emits once, on
// a release that lands inside the control that was pressed. There is no second
// recogniser and nothing that can fire twice.
//
// Two LVGL defaults have to be turned off for that to behave safely.
//
// LV_OBJ_FLAG_PRESS_LOCK is set on every child object by LVGL itself
// (lv_obj.c: "if(parent) obj->flags |= LV_OBJ_FLAG_PRESS_LOCK"). It makes a
// control keep the press after the finger slides off it, so a release anywhere
// on the screen still fires the control the finger started on -- precisely the
// accidental activation this firmware exists to remove. Clearing it lets LVGL
// re-target while the finger moves.
//
// With that cleared, LV_EVENT_PRESS_LOST arrives whenever the finger leaves
// the control it pressed, including when it lands on a neighbouring tile. It
// is routed to the same handler purely to withhold the action for the rest of
// the contact, so sliding across the grid cancels the interaction instead of
// transferring it to whichever tile the finger happened to end on.
namespace deckcontrols {

struct Controls {
  lv_obj_t *tiles[decklayout::Tiles];
  lv_obj_t *navPrevious;
  lv_obj_t *navNext;
};

inline Controls create(lv_obj_t *parent, lv_event_cb_t tileEvent, lv_event_cb_t navEvent) {
  using namespace decklayout;
  Controls controls{};

  for (int i = 0; i < Tiles; i++) {
    lv_obj_t *tile = lv_button_create(parent);
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_set_size(tile, TileWidth, TileHeight);
    lv_obj_set_pos(tile, tileX(i), tileY(i));
    // Extend the touch area into the surrounding gap so neighbouring targets
    // meet without overlapping and there is no dead strip between tiles.
    lv_obj_set_ext_click_area(tile, TileTouchExtend);
    lv_obj_add_event_cb(tile, tileEvent, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    lv_obj_add_event_cb(tile, tileEvent, LV_EVENT_PRESS_LOST, (void *)(intptr_t)i);
    controls.tiles[i] = tile;
  }

  int right = Width - Margin - NavButtonWidth;
  for (int step = 0; step < 2; step++) {
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(button, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_set_size(button, NavButtonWidth, NavHeight);
    lv_obj_set_pos(button, step ? right : Margin, NavTop);
    lv_obj_set_ext_click_area(button, NavTouchExtend);
    lv_obj_add_event_cb(button, navEvent, LV_EVENT_CLICKED, (void *)(intptr_t)(step ? 1 : -1));
    lv_obj_add_event_cb(button, navEvent, LV_EVENT_PRESS_LOST, (void *)(intptr_t)(step ? 1 : -1));
    (step ? controls.navNext : controls.navPrevious) = button;
  }
  return controls;
}

}  // namespace deckcontrols
