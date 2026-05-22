#include "mnemonic_slots_page.h"
#include "../../core/key.h"
#include "../../core/mnemonic_slots.h"
#include "../../ui/dialog.h"
#include "../../ui/menu.h"
#include "../../ui/theme.h"
#include "../../utils/secure_mem.h"

#include <lvgl.h>
#include <stdio.h>
#include <string.h>

static ui_menu_t *slots_menu = NULL;
static lv_obj_t *slots_screen = NULL;
static void (*return_callback)(void) = NULL;
static void (*success_callback)(void) = NULL;
static size_t displayed_slots[MNEMONIC_SLOT_CAPACITY];
static size_t displayed_count = 0;

static void style_slots_menu_layout(void) {
  if (!slots_menu || !slots_menu->list)
    return;

  int top_gap = theme_get_corner_button_height();
  if (top_gap < 56)
    top_gap = 56;

  lv_obj_set_flex_flow(slots_menu->list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(slots_menu->list, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_top(slots_menu->list, top_gap, 0);
  lv_obj_set_style_pad_row(slots_menu->list, 14, 0);
}

static void style_slot_button(int index, const mnemonic_slot_info_t *info,
                              bool current, size_t slot_index) {
  if (!slots_menu || index < 0 || index >= UI_MENU_MAX_ENTRIES ||
      !slots_menu->buttons[index] || !info)
    return;

  lv_obj_t *btn = slots_menu->buttons[index];
  lv_obj_set_width(btn, LV_PCT(theme_get_screen_width() <= 520 ? 72 : 58));
  lv_obj_set_height(btn, 132);
  lv_obj_set_style_pad_all(btn, 12, 0);
  lv_obj_set_style_pad_gap(btn, 4, 0);

  lv_obj_t *label = lv_obj_get_child(btn, 0);
  if (!label)
    return;

  char text[96];
  snprintf(text, sizeof(text), "%s助记词 %u\n钱包指纹 %s\n%u 词",
           current ? "当前 " : "", (unsigned)(slot_index + 1),
           info->fingerprint, (unsigned)info->word_count);
  lv_label_set_text(label, text);
  lv_obj_set_width(label, LV_PCT(96));
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(label, theme_font_small(), 0);
  lv_obj_center(label);
}

static void back_cb(void) {
  if (return_callback)
    return_callback();
}

static void select_slot_cb(void) {
  int selected = ui_menu_get_selected(slots_menu);
  if (selected < 0 || (size_t)selected >= displayed_count)
    return;

  size_t slot = displayed_slots[selected];
  char *passphrase = NULL;
  (void)key_get_session_passphrase(&passphrase);
  if (!mnemonic_slots_load(slot, passphrase)) {
    SECURE_FREE_STRING(passphrase);
    dialog_show_error("切换失败", NULL, 0);
    return;
  }
  SECURE_FREE_STRING(passphrase);

  if (success_callback)
    success_callback();
}

void mnemonic_slots_page_create(lv_obj_t *parent, void (*return_cb)(void),
                                void (*success_cb)(void)) {
  if (!parent)
    return;

  return_callback = return_cb;
  success_callback = success_cb;
  displayed_count = 0;

  slots_screen = theme_create_page_container(parent);
  slots_menu = ui_menu_create(slots_screen, "选择助记词", back_cb);
  if (!slots_menu)
    return;
  style_slots_menu_layout();

  char current_fp[9] = {0};
  bool has_current = key_get_fingerprint_hex(current_fp);

  for (size_t i = 0; i < MNEMONIC_SLOT_CAPACITY; i++) {
    mnemonic_slot_info_t info;
    if (!mnemonic_slots_get_info(i, &info) || !info.used)
      continue;
    if (displayed_count >= UI_MENU_MAX_ENTRIES)
      break;

    bool current = has_current && strcmp(current_fp, info.fingerprint) == 0;
    int entry_index = slots_menu->config.entry_count;
    displayed_slots[displayed_count++] = i;
    ui_menu_add_entry(slots_menu, "助记词", select_slot_cb);
    style_slot_button(entry_index, &info, current, i);
  }

  if (displayed_count == 0) {
    dialog_show_error("没有会话助记词", return_cb, 0);
    return;
  }
}

void mnemonic_slots_page_show(void) {
  if (slots_screen)
    lv_obj_clear_flag(slots_screen, LV_OBJ_FLAG_HIDDEN);
  if (slots_menu)
    ui_menu_show(slots_menu);
}

void mnemonic_slots_page_hide(void) {
  if (slots_screen)
    lv_obj_add_flag(slots_screen, LV_OBJ_FLAG_HIDDEN);
  if (slots_menu)
    ui_menu_hide(slots_menu);
}

void mnemonic_slots_page_destroy(void) {
  if (slots_menu) {
    ui_menu_destroy(slots_menu);
    slots_menu = NULL;
  }
  if (slots_screen) {
    lv_obj_del(slots_screen);
    slots_screen = NULL;
  }
  return_callback = NULL;
  success_callback = NULL;
  displayed_count = 0;
}
