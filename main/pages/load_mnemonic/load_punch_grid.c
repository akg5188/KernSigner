#include "load_punch_grid.h"
#include "../../core/mnemonic_tools.h"
#include "../../ui/dialog.h"
#include "../../ui/menu.h"
#include "../../ui/theme.h"
#include "../shared/mnemonic_tool_page.h"
#include "../shared/key_confirmation.h"
#include "../../utils/secure_mem.h"
#include "stackbit_restore.h"
#include "tinyseed_restore.h"

#include <lvgl.h>
#include <string.h>

static ui_menu_t *punch_menu = NULL;
static lv_obj_t *punch_screen = NULL;
static void (*return_callback)(void) = NULL;
static void (*success_callback)(void) = NULL;

static void return_from_key_confirmation_cb(void) {
  key_confirmation_page_destroy();
  load_punch_grid_page_show();
}

void load_punch_grid_page_handle_completed_mnemonic(char *mnemonic) {
  if (mnemonic) {
    key_confirmation_page_create(lv_screen_active(),
                                 return_from_key_confirmation_cb,
                                 success_callback, mnemonic, strlen(mnemonic));
    key_confirmation_page_show();
    SECURE_FREE_STRING(mnemonic);
  } else {
    load_punch_grid_page_show();
  }
}

static void return_from_mnemonic_tool_cb(void) {
  char *mnemonic = mnemonic_tool_page_get_completed_mnemonic();
  mnemonic_tool_page_destroy();
  load_punch_grid_page_handle_completed_mnemonic(mnemonic);
}

static void launch_restore(void) {
  load_punch_grid_page_hide();
  mnemonic_tool_page_create(lv_screen_active(), return_from_mnemonic_tool_cb,
                            MNEMONIC_TOOL_STEEL_RESTORE);
  mnemonic_tool_page_show();
}

static void return_from_tinyseed_restore_cb(void) {
  tinyseed_restore_page_destroy();
  load_punch_grid_page_show();
}

static void success_from_tinyseed_restore_cb(void) {
  tinyseed_restore_page_destroy();
  load_punch_grid_page_destroy();
  if (success_callback)
    success_callback();
}

static void tinyseed_cb(void) {
  load_punch_grid_page_hide();
  tinyseed_restore_page_create(lv_screen_active(),
                               return_from_tinyseed_restore_cb,
                               success_from_tinyseed_restore_cb);
  tinyseed_restore_page_show();
}

static void return_from_stackbit_restore_cb(void) {
  stackbit_restore_page_destroy();
  load_punch_grid_page_show();
}

static void success_from_stackbit_restore_cb(void) {
  stackbit_restore_page_destroy();
  load_punch_grid_page_destroy();
  if (success_callback)
    success_callback();
}

static void stackbit_cb(void) {
  load_punch_grid_page_hide();
  stackbit_restore_page_create(lv_screen_active(),
                               return_from_stackbit_restore_cb,
                               success_from_stackbit_restore_cb);
  stackbit_restore_page_show();
}

static void back_cb(void) {
  void (*callback)(void) = return_callback;
  load_punch_grid_page_hide();
  load_punch_grid_page_destroy();
  if (callback)
    callback();
}

void load_punch_grid_page_create(lv_obj_t *parent, void (*return_cb)(void),
                                 void (*success_cb)(void)) {
  if (!parent)
    return;

  return_callback = return_cb;
  success_callback = success_cb;
  punch_screen = theme_create_page_container(parent);
  punch_menu = ui_menu_create(punch_screen, "点阵/1248", back_cb);
  if (!punch_menu)
    return;

  ui_menu_add_entry(punch_menu, "点阵板", tinyseed_cb);
  ui_menu_add_entry(punch_menu, "1248打孔", stackbit_cb);
  ui_menu_show(punch_menu);
}

void load_punch_grid_page_show(void) {
  if (punch_screen)
    lv_obj_clear_flag(punch_screen, LV_OBJ_FLAG_HIDDEN);
  if (punch_menu)
    ui_menu_show(punch_menu);
}

void load_punch_grid_page_hide(void) {
  if (punch_screen)
    lv_obj_add_flag(punch_screen, LV_OBJ_FLAG_HIDDEN);
  if (punch_menu)
    ui_menu_hide(punch_menu);
}

void load_punch_grid_page_destroy(void) {
  if (punch_menu) {
    ui_menu_destroy(punch_menu);
    punch_menu = NULL;
  }
  if (punch_screen) {
    lv_obj_del(punch_screen);
    punch_screen = NULL;
  }
  return_callback = NULL;
  success_callback = NULL;
}
