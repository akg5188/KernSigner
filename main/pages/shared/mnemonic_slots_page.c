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

  char current_fp[9] = {0};
  bool has_current = key_get_fingerprint_hex(current_fp);

  for (size_t i = 0; i < MNEMONIC_SLOT_CAPACITY; i++) {
    mnemonic_slot_info_t info;
    if (!mnemonic_slots_get_info(i, &info) || !info.used)
      continue;
    if (displayed_count >= UI_MENU_MAX_ENTRIES)
      break;

    char label[64];
    bool current = has_current && strcmp(current_fp, info.fingerprint) == 0;
    snprintf(label, sizeof(label), "%s助记词%u  %s  %u词",
             current ? "当前 " : "", (unsigned)(i + 1), info.fingerprint,
             (unsigned)info.word_count);
    displayed_slots[displayed_count++] = i;
    ui_menu_add_entry(slots_menu, label, select_slot_cb);
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
