// New Mnemonic Menu Page

#include "new_mnemonic_menu.h"
#include "../../ui/menu.h"
#include "../../ui/theme.h"
#include "../home/home.h"
#include "../load_mnemonic/manual_input.h"
#include "../shared/key_confirmation.h"
#include "../shared/mnemonic_editor.h"
#include "../shared/mnemonic_tool_page.h"
#include "entropy_from_camera.h"
#include "../../utils/secure_mem.h"
#include <lvgl.h>
#include <stdlib.h>
#include <string.h>

static ui_menu_t *new_mnemonic_menu = NULL;
static lv_obj_t *new_mnemonic_menu_screen = NULL;
static void (*return_callback)(void) = NULL;

static void from_dice_rolls_cb(void);
static void from_binary_entropy_cb(void);
static void from_words_cb(void);
static void from_camera_cb(void);
static void from_hex_entropy_cb(void);
static void from_d20_entropy_cb(void);
static void from_bip85_mnemonic_cb(void);
static void from_xor_mnemonic_cb(void);
static void from_secondary_shift_cb(void);
static void back_cb(void);
static void return_from_entropy_from_camera_cb(void);
static void return_from_manual_input_cb(void);
static void return_from_mnemonic_tool_cb(void);
static void return_from_mnemonic_editor_cb(void);
static void return_from_key_confirmation_cb(void);
static void success_from_key_confirmation_cb(void);

static void return_from_entropy_from_camera_cb(void) {
  char *mnemonic = entropy_from_camera_get_completed_mnemonic();
  entropy_from_camera_page_destroy();

  if (mnemonic) {
    mnemonic_editor_page_create(
        lv_screen_active(), return_from_mnemonic_editor_cb,
        success_from_key_confirmation_cb, mnemonic, true);
    mnemonic_editor_page_show();
    SECURE_FREE_STRING(mnemonic);
  } else {
    new_mnemonic_menu_page_show();
  }
}

static void return_from_manual_input_cb(void) {
  manual_input_page_destroy();
  new_mnemonic_menu_page_show();
}

static void return_from_mnemonic_tool_cb(void) {
  char *mnemonic = mnemonic_tool_page_get_completed_mnemonic();
  mnemonic_tool_page_destroy();

  if (mnemonic) {
    mnemonic_editor_page_create(
        lv_screen_active(), return_from_mnemonic_editor_cb,
        success_from_key_confirmation_cb, mnemonic, true);
    mnemonic_editor_page_show();
    SECURE_FREE_STRING(mnemonic);
  } else {
    new_mnemonic_menu_page_show();
  }
}

static void return_from_mnemonic_editor_cb(void) {
  mnemonic_editor_page_destroy();
  new_mnemonic_menu_page_show();
}

static void return_from_key_confirmation_cb(void) {
  key_confirmation_page_destroy();
  new_mnemonic_menu_page_show();
}

static void success_from_key_confirmation_cb(void) {
  key_confirmation_page_destroy();
  new_mnemonic_menu_page_destroy();
  home_page_create(lv_screen_active());
  home_page_show();
}

static void launch_words(void) {
  manual_input_page_create(lv_screen_active(), return_from_manual_input_cb,
                           success_from_key_confirmation_cb, true);
  manual_input_page_show();
}

static void launch_camera(void) {
  entropy_from_camera_page_create(lv_screen_active(),
                                  return_from_entropy_from_camera_cb);
  entropy_from_camera_page_show();
}

static void launch_hex_entropy(void) {
  mnemonic_tool_page_create(lv_screen_active(), return_from_mnemonic_tool_cb,
                            MNEMONIC_TOOL_HEX_ENTROPY);
  mnemonic_tool_page_show();
}

static void launch_d6_entropy(void) {
  mnemonic_tool_page_create(lv_screen_active(), return_from_mnemonic_tool_cb,
                            MNEMONIC_TOOL_D6_ENTROPY);
  mnemonic_tool_page_show();
}

static void launch_binary_entropy(void) {
  mnemonic_tool_page_create(lv_screen_active(), return_from_mnemonic_tool_cb,
                            MNEMONIC_TOOL_BINARY_ENTROPY);
  mnemonic_tool_page_show();
}

static void launch_d20_entropy(void) {
  mnemonic_tool_page_create(lv_screen_active(), return_from_mnemonic_tool_cb,
                            MNEMONIC_TOOL_D20_ENTROPY);
  mnemonic_tool_page_show();
}

static void launch_bip85_mnemonic(void) {
  mnemonic_tool_page_create(lv_screen_active(), return_from_mnemonic_tool_cb,
                            MNEMONIC_TOOL_BIP85_MNEMONIC);
  mnemonic_tool_page_show();
}

static void launch_xor_mnemonic(void) {
  mnemonic_tool_page_create(lv_screen_active(), return_from_mnemonic_tool_cb,
                            MNEMONIC_TOOL_XOR_CURRENT);
  mnemonic_tool_page_show();
}

static void launch_secondary_shift(void) {
  mnemonic_tool_page_create(lv_screen_active(), return_from_mnemonic_tool_cb,
                            MNEMONIC_TOOL_SECONDARY_SHIFT);
  mnemonic_tool_page_show();
}

static void launch_from_menu(void (*action)(void)) {
  new_mnemonic_menu_page_hide();
  if (action)
    action();
}

static void from_dice_rolls_cb(void) { launch_from_menu(launch_d6_entropy); }

static void from_binary_entropy_cb(void) {
  launch_from_menu(launch_binary_entropy);
}

static void from_words_cb(void) { launch_from_menu(launch_words); }

static void from_camera_cb(void) { launch_from_menu(launch_camera); }

static void from_hex_entropy_cb(void) { launch_from_menu(launch_hex_entropy); }

static void from_d20_entropy_cb(void) { launch_from_menu(launch_d20_entropy); }

static void from_bip85_mnemonic_cb(void) {
  launch_from_menu(launch_bip85_mnemonic);
}

static void from_xor_mnemonic_cb(void) { launch_from_menu(launch_xor_mnemonic); }

static void from_secondary_shift_cb(void) {
  launch_from_menu(launch_secondary_shift);
}

static void back_cb(void) {
  void (*callback)(void) = return_callback;
  new_mnemonic_menu_page_hide();
  new_mnemonic_menu_page_destroy();
  if (callback)
    callback();
}

void new_mnemonic_menu_page_create(lv_obj_t *parent, void (*return_cb)(void)) {
  if (!parent)
    return;

  return_callback = return_cb;

  new_mnemonic_menu_screen = theme_create_page_container(parent);

  new_mnemonic_menu =
      ui_menu_create(new_mnemonic_menu_screen, "新助记词", back_cb);
  if (!new_mnemonic_menu)
    return;

  ui_menu_add_entry(new_mnemonic_menu, "骰子", from_dice_rolls_cb);
  ui_menu_add_entry(new_mnemonic_menu, "抛硬币", from_binary_entropy_cb);
  ui_menu_add_entry(new_mnemonic_menu, "手动", from_words_cb);
  ui_menu_add_entry(new_mnemonic_menu, "相机", from_camera_cb);
  ui_menu_add_entry(new_mnemonic_menu, "16进制", from_hex_entropy_cb);
  ui_menu_add_entry(new_mnemonic_menu, "二十面骰", from_d20_entropy_cb);
  ui_menu_add_entry(new_mnemonic_menu, "派生助记词", from_bip85_mnemonic_cb);
  ui_menu_add_entry(new_mnemonic_menu, "异或", from_xor_mnemonic_cb);
  ui_menu_add_entry(new_mnemonic_menu, "加密", from_secondary_shift_cb);
  ui_menu_show(new_mnemonic_menu);
}

void new_mnemonic_menu_page_show(void) {
  if (new_mnemonic_menu)
    ui_menu_show(new_mnemonic_menu);
}

void new_mnemonic_menu_page_hide(void) {
  if (new_mnemonic_menu)
    ui_menu_hide(new_mnemonic_menu);
}

void new_mnemonic_menu_page_destroy(void) {
  if (new_mnemonic_menu) {
    ui_menu_destroy(new_mnemonic_menu);
    new_mnemonic_menu = NULL;
  }
  if (new_mnemonic_menu_screen) {
    lv_obj_del(new_mnemonic_menu_screen);
    new_mnemonic_menu_screen = NULL;
  }
  return_callback = NULL;
}
