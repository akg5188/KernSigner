// Backup Menu Page

#include "backup_menu.h"
#include "../../../core/key.h"
#include "../../../i18n/i18n.h"
#include "../../../ui/dialog.h"
#include "../../../ui/menu.h"
#include "../../../ui/theme.h"
#include "../../shared/sensitive_pin.h"
#include "../../store_mnemonic.h"
#include "mnemonic_entropy.h"
#include "mnemonic_1248.h"
#include "mnemonic_grid.h"
#include "mnemonic_qr.h"
#include "mnemonic_steel.h"
#include "mnemonic_words.h"
#include <lvgl.h>

static ui_menu_t *backup_menu = NULL;
static lv_obj_t *backup_menu_screen = NULL;
static void (*return_callback)(void) = NULL;

/* --- Words / QR Code callbacks --- */

static void return_from_mnemonic_words_cb(void) {
  mnemonic_words_page_destroy();
  backup_menu_page_show();
}

static void return_from_mnemonic_qr_cb(void) {
  mnemonic_qr_page_destroy();
  backup_menu_page_show();
}

static void return_from_mnemonic_entropy_cb(void) {
  mnemonic_entropy_page_destroy();
  backup_menu_page_show();
}

static void return_from_mnemonic_grid_cb(void) {
  mnemonic_grid_page_destroy();
  backup_menu_page_show();
}

static void return_from_mnemonic_grid_numbers_cb(void) {
  mnemonic_grid_numbers_page_destroy();
  backup_menu_page_show();
}

static void return_from_mnemonic_steel_cb(void) {
  mnemonic_steel_page_destroy();
  backup_menu_page_show();
}

static void return_from_mnemonic_1248_cb(void) {
  mnemonic_1248_page_destroy();
  backup_menu_page_show();
}

static void return_from_store_mnemonic_cb(void) {
  store_mnemonic_page_destroy();
  backup_menu_page_show();
}

static void (*pending_action)(void) = NULL;

static void launch_words(void) {
  mnemonic_words_page_create(lv_screen_active(), return_from_mnemonic_words_cb);
  mnemonic_words_page_show();
}

static void launch_qr(void) {
  mnemonic_qr_page_create(lv_screen_active(), return_from_mnemonic_qr_cb);
  mnemonic_qr_page_show();
}

static void launch_entropy(void) {
  mnemonic_entropy_page_create(lv_screen_active(),
                               return_from_mnemonic_entropy_cb);
  mnemonic_entropy_page_show();
}

static void launch_grid(void) {
  mnemonic_grid_page_create(lv_screen_active(), return_from_mnemonic_grid_cb);
  mnemonic_grid_page_show();
}

static void launch_grid_numbers(void) {
  mnemonic_grid_numbers_page_create(lv_screen_active(),
                                    return_from_mnemonic_grid_numbers_cb);
  mnemonic_grid_numbers_page_show();
}

static void launch_steel(void) {
  mnemonic_steel_page_create(lv_screen_active(), return_from_mnemonic_steel_cb);
  mnemonic_steel_page_show();
}

static void launch_1248(void) {
  mnemonic_1248_page_create(lv_screen_active(), return_from_mnemonic_1248_cb);
  mnemonic_1248_page_show();
}

static void launch_encrypted_backup(void) {
  store_mnemonic_page_create(lv_screen_active(), return_from_store_mnemonic_cb,
                             STORAGE_SD);
  store_mnemonic_page_show();
}

static void danger_confirm_cb(bool confirmed, void *user_data) {
  (void)user_data;
  if (!confirmed) {
    backup_menu_page_show();
    return;
  }
  backup_menu_page_hide();
  pending_action();
}

static void warn_after_pin(void) {
  dialog_show_danger_confirm(DIALOG_SENSITIVE_DATA_WARNING, danger_confirm_cb,
                             NULL, DIALOG_STYLE_OVERLAY);
}

static void pin_then_warn(void (*action)(void)) {
  pending_action = action;
  backup_menu_page_hide();
  sensitive_pin_require(warn_after_pin, backup_menu_page_show);
}

static void menu_words_cb(void) { pin_then_warn(launch_words); }

static void menu_qr_cb(void) { pin_then_warn(launch_qr); }

static void menu_entropy_cb(void) { pin_then_warn(launch_entropy); }

static void menu_grid_cb(void) { pin_then_warn(launch_grid); }

static void menu_grid_numbers_cb(void) { pin_then_warn(launch_grid_numbers); }

static void menu_steel_cb(void) { pin_then_warn(launch_steel); }

static void menu_1248_cb(void) { pin_then_warn(launch_1248); }

static void menu_encrypted_backup_cb(void) {
  pin_then_warn(launch_encrypted_backup);
}

/* --- Back --- */

static void back_cb(void) {
  if (return_callback) {
    return_callback();
  }
}

void backup_menu_page_create(lv_obj_t *parent, void (*return_cb)(void)) {
  if (!parent)
    return;

  return_callback = return_cb;

  if (!key_can_backup_mnemonic()) {
    dialog_show_error(i18n_tr_or("backup.no_temporary_export",
                                 "Temporary mnemonic cannot be exported"),
                      return_cb, 0);
    return;
  }

  backup_menu_screen = theme_create_page_container(parent);

  backup_menu = ui_menu_create(backup_menu_screen,
                               i18n_tr_or("backup.backup", "Backup"),
                               back_cb);
  if (!backup_menu)
    return;

  ui_menu_add_entry(backup_menu,
                    i18n_tr_or("backup.word_indexes", "Word indexes"),
                    menu_words_cb);
  ui_menu_add_entry(backup_menu,
                    i18n_tr_or("wallet.raw_entropy", "Raw entropy"),
                    menu_entropy_cb);
  ui_menu_add_entry(backup_menu, i18n_tr_or("menu.qr_code", "QR code"),
                    menu_qr_cb);
  ui_menu_add_entry(backup_menu,
                    i18n_tr_or("backup.encrypted_backup",
                               "Encrypted backup"),
                    menu_encrypted_backup_cb);
  ui_menu_add_entry(backup_menu, i18n_tr_or("input.punch_grid", "Punch grid"),
                    menu_grid_cb);
  ui_menu_add_entry(backup_menu,
                    i18n_tr_or("backup.punch_numbers", "Punch numbers"),
                    menu_grid_numbers_cb);
  ui_menu_add_entry(backup_menu,
                    i18n_tr_or("backup.steel_punch", "Steel punch"),
                    menu_steel_cb);
  ui_menu_add_entry(backup_menu,
                    i18n_tr_or("backup.1248_punch", "1248 punch"),
                    menu_1248_cb);
}

void backup_menu_page_show(void) {
  if (backup_menu_screen) {
    lv_obj_clear_flag(backup_menu_screen, LV_OBJ_FLAG_HIDDEN);
  }
  if (backup_menu) {
    ui_menu_show(backup_menu);
  }
}

void backup_menu_page_hide(void) {
  if (backup_menu_screen) {
    lv_obj_add_flag(backup_menu_screen, LV_OBJ_FLAG_HIDDEN);
  }
  if (backup_menu) {
    ui_menu_hide(backup_menu);
  }
}

void backup_menu_page_destroy(void) {
  if (backup_menu) {
    ui_menu_destroy(backup_menu);
    backup_menu = NULL;
  }

  if (backup_menu_screen) {
    lv_obj_del(backup_menu_screen);
    backup_menu_screen = NULL;
  }

  return_callback = NULL;
}
