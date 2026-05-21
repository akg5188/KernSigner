// UI Menu Component - Touch menu for LVGL

#include "menu.h"
#include "input_helpers.h"
#include "theme.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int max_i(int a, int b) { return a > b ? a : b; }

static void menu_button_event_cb(lv_event_t *e) {
  lv_obj_t *btn = lv_event_get_target(e);
  ui_menu_t *menu = (ui_menu_t *)lv_event_get_user_data(e);

  for (int i = 0; i < menu->config.entry_count; i++) {
    if (menu->buttons[i] == btn) {
      menu->config.selected_index = i;
      if (menu->config.entries[i].enabled && menu->config.entries[i].callback)
        menu->config.entries[i].callback();
      break;
    }
  }
}

static void menu_back_button_event_cb(lv_event_t *e) {
  ui_menu_t *menu = (ui_menu_t *)lv_event_get_user_data(e);
  if (menu && menu->back_callback)
    menu->back_callback();
}

ui_menu_t *ui_menu_create(lv_obj_t *parent, const char *title,
                          ui_menu_callback_t back_cb) {
  if (!parent || !title)
    return NULL;

  ui_menu_t *menu = malloc(sizeof(ui_menu_t));
  if (!menu)
    return NULL;

  memset(&menu->config, 0, sizeof(ui_menu_config_t));

  menu->container = lv_obj_create(parent);
  lv_obj_set_size(menu->container, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(menu->container, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(menu->container, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_hor(menu->container, theme_get_default_padding(), 0);
  lv_obj_set_style_pad_top(menu->container,
                           theme_get_default_padding() +
                               theme_get_small_padding(),
                           0);
  lv_obj_set_style_pad_bottom(menu->container, theme_get_small_padding(), 0);
  lv_obj_set_style_pad_gap(menu->container, max_i(8, theme_get_small_padding()),
                           0);
  lv_obj_clear_flag(menu->container, LV_OBJ_FLAG_SCROLLABLE);
  theme_apply_screen(menu->container);

  menu->title_label = lv_label_create(menu->container);
  lv_label_set_text(menu->title_label, title);
  lv_obj_set_width(menu->title_label, LV_PCT(88));
  lv_label_set_long_mode(menu->title_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(menu->title_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(menu->title_label, theme_font_medium(), 0);
  lv_obj_set_style_margin_bottom(menu->title_label, theme_get_small_padding(),
                                 0);
  theme_apply_label(menu->title_label, false);

  menu->list = lv_obj_create(menu->container);
  lv_obj_set_width(menu->list, LV_PCT(100));
  lv_obj_set_height(menu->list, 0);
  theme_apply_transparent_container(menu->list);
  bool wide = theme_get_screen_width() >= 420;
  lv_obj_set_flex_flow(menu->list,
                       wide ? LV_FLEX_FLOW_ROW_WRAP : LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(menu->list, LV_FLEX_ALIGN_START,
                        wide ? LV_FLEX_ALIGN_START : LV_FLEX_ALIGN_CENTER,
                        wide ? LV_FLEX_ALIGN_START : LV_FLEX_ALIGN_CENTER);
  lv_obj_set_flex_grow(menu->list, 1);
  lv_obj_set_style_pad_gap(menu->list, max_i(10, theme_get_small_padding()), 0);
  lv_obj_set_style_pad_row(menu->list, max_i(10, theme_get_small_padding()), 0);
  lv_obj_set_style_pad_column(menu->list, max_i(10, theme_get_small_padding()), 0);
  lv_obj_set_style_outline_width(menu->list, 0, 0);
  lv_obj_set_scrollbar_mode(menu->list, LV_SCROLLBAR_MODE_AUTO);

  for (int i = 0; i < UI_MENU_MAX_ENTRIES; i++)
    menu->buttons[i] = NULL;

  menu->back_btn = NULL;
  menu->back_callback = back_cb;

  if (back_cb) {
    menu->back_btn = ui_create_back_button(menu->container, NULL);
    if (menu->back_btn) {
      lv_obj_add_flag(menu->back_btn, LV_OBJ_FLAG_FLOATING);
      lv_obj_move_foreground(menu->back_btn);
      lv_obj_add_event_cb(menu->back_btn, menu_back_button_event_cb,
                          LV_EVENT_CLICKED, menu);
    }
  }

  return menu;
}

bool ui_menu_add_entry(ui_menu_t *menu, const char *name,
                       ui_menu_callback_t callback) {
  if (!menu || !name || !callback ||
      menu->config.entry_count >= UI_MENU_MAX_ENTRIES)
    return false;

  int idx = menu->config.entry_count;
  menu->config.entries[idx].callback = callback;
  menu->config.entries[idx].enabled = true;

  menu->buttons[idx] = lv_btn_create(menu->list);
  bool wide = theme_get_screen_width() >= 420;
  lv_obj_set_size(menu->buttons[idx], wide ? LV_PCT(48) : LV_PCT(100),
                  wide ? 118 : max_i(72, theme_get_min_touch_size()));
  lv_obj_add_event_cb(menu->buttons[idx], menu_button_event_cb,
                      LV_EVENT_CLICKED, menu);
  theme_apply_touch_button(menu->buttons[idx], false);
  lv_obj_set_style_bg_color(menu->buttons[idx], panel_color(), 0);
  lv_obj_set_style_bg_opa(menu->buttons[idx], LV_OPA_80, 0);
  lv_obj_set_style_border_color(menu->buttons[idx], highlight_color(), 0);
  lv_obj_set_style_border_width(menu->buttons[idx], 1, 0);

  lv_obj_t *label = lv_label_create(menu->buttons[idx]);
  lv_label_set_text(label, name);
  lv_obj_set_width(label, LV_PCT(92));
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_pad_ver(label, 2, 0);
  lv_obj_center(label);
  theme_apply_button_label(label, false);

  menu->config.entry_count++;
  return true;
}

static void action_button_event_cb(lv_event_t *e) {
  ui_menu_t *menu = (ui_menu_t *)lv_event_get_user_data(e);
  lv_obj_t *btn = lv_event_get_target(e);
  int idx = (int)(intptr_t)lv_obj_get_user_data(btn);
  if (idx >= 0 && idx < menu->config.entry_count &&
      menu->config.entries[idx].action_callback)
    menu->config.entries[idx].action_callback(idx);
}

bool ui_menu_add_entry_with_action(ui_menu_t *menu, const char *name,
                                   ui_menu_callback_t callback,
                                   const char *action_icon,
                                   ui_menu_action_callback_t action_cb) {
  if (!menu || !name || !callback || !action_icon || !action_cb ||
      menu->config.entry_count >= UI_MENU_MAX_ENTRIES)
    return false;

  int idx = menu->config.entry_count;
  menu->config.entries[idx].callback = callback;
  menu->config.entries[idx].action_callback = action_cb;
  menu->config.entries[idx].enabled = true;

  /* Main button — row layout */
  menu->buttons[idx] = lv_btn_create(menu->list);
  bool wide = theme_get_screen_width() >= 420;
  lv_obj_set_size(menu->buttons[idx], wide ? LV_PCT(48) : LV_PCT(100),
                  wide ? 118 : max_i(72, theme_get_min_touch_size()));
  lv_obj_set_flex_flow(menu->buttons[idx], LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(menu->buttons[idx], LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(menu->buttons[idx], 0, 0);
  /* Flush the action icon to the right edge of the row */
  lv_obj_set_style_pad_right(menu->buttons[idx], 0, 0);
  lv_obj_add_event_cb(menu->buttons[idx], menu_button_event_cb,
                      LV_EVENT_CLICKED, menu);
  theme_apply_touch_button(menu->buttons[idx], false);
  lv_obj_set_style_bg_color(menu->buttons[idx], panel_color(), 0);
  lv_obj_set_style_bg_opa(menu->buttons[idx], LV_OPA_80, 0);
  lv_obj_set_style_border_color(menu->buttons[idx], highlight_color(), 0);
  lv_obj_set_style_border_width(menu->buttons[idx], 1, 0);

  /* Label on the left */
  lv_obj_t *label = lv_label_create(menu->buttons[idx]);
  lv_label_set_text(label, name);
  lv_obj_set_flex_grow(label, 1);
  lv_obj_set_width(label, LV_PCT(74));
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_pad_ver(label, 2, 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
  theme_apply_button_label(label, false);

  /* Action icon button on the right — matches the label's vertical extent so
     the touch target fills the row height (LV_PCT(100) can't resolve against
     a SIZE_CONTENT parent) */
  lv_obj_t *icon_btn = lv_btn_create(menu->buttons[idx]);
  lv_obj_set_size(icon_btn, theme_get_min_touch_size(),
                  max_i(50, theme_get_min_touch_size()));
  lv_obj_set_style_bg_color(icon_btn, disabled_color(), 0);
  lv_obj_set_style_bg_opa(icon_btn, LV_OPA_COVER, 0);
  lv_obj_set_style_shadow_width(icon_btn, 0, 0);
  lv_obj_set_style_border_width(icon_btn, 0, 0);
  lv_obj_set_style_pad_hor(icon_btn, 0, 0);
  lv_obj_set_style_pad_ver(icon_btn, 4, 0);
  lv_obj_clear_flag(icon_btn, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_set_user_data(icon_btn, (void *)(intptr_t)idx);
  lv_obj_add_event_cb(icon_btn, action_button_event_cb, LV_EVENT_CLICKED, menu);

  lv_obj_t *icon_label = lv_label_create(icon_btn);
  lv_label_set_text(icon_label, action_icon);
  lv_obj_center(icon_label);
  lv_obj_set_style_text_color(icon_label, error_color(), 0);

  menu->config.entry_count++;
  return true;
}

bool ui_menu_set_entry_enabled(ui_menu_t *menu, int index, bool enabled) {
  if (!menu || index < 0 || index >= menu->config.entry_count)
    return false;

  menu->config.entries[index].enabled = enabled;
  if (enabled) {
    lv_obj_clear_state(menu->buttons[index], LV_STATE_DISABLED);
  } else {
    lv_obj_add_state(menu->buttons[index], LV_STATE_DISABLED);
  }

  /* Update label color to reflect enabled/disabled state */
  lv_obj_t *label = lv_obj_get_child(menu->buttons[index], 0);
  if (label) {
    lv_obj_set_style_text_color(label,
                                enabled ? main_color() : disabled_color(), 0);
  }
  return true;
}

int ui_menu_get_selected(ui_menu_t *menu) {
  return menu ? menu->config.selected_index : -1;
}

void ui_menu_show(ui_menu_t *menu) {
  if (menu && menu->container)
    lv_obj_clear_flag(menu->container, LV_OBJ_FLAG_HIDDEN);
}

void ui_menu_hide(ui_menu_t *menu) {
  if (menu && menu->container)
    lv_obj_add_flag(menu->container, LV_OBJ_FLAG_HIDDEN);
}

void ui_menu_destroy(ui_menu_t *menu) {
  if (!menu)
    return;
  if (menu->back_btn)
    lv_obj_del(menu->back_btn);
  if (menu->container)
    lv_obj_del(menu->container);
  free(menu);
}
