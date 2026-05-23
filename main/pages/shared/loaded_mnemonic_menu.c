#include "loaded_mnemonic_menu.h"
#include "../../core/key.h"
#include "../../core/mnemonic_slots.h"
#include "../../core/settings.h"
#include "../../core/wallet.h"
#include "../../ui/dialog.h"
#include "../../ui/menu.h"
#include "../../ui/theme.h"
#include "../../utils/secure_mem.h"
#include "../home/addresses.h"
#include "../home/backup/backup_menu.h"
#include "../home/backup/mnemonic_entropy.h"
#include "../home/backup/mnemonic_words.h"
#include "../home/public_key.h"
#include "../passphrase.h"
#include "custom_derivation_page.h"
#include "key_confirmation.h"
#include "mnemonic_editor.h"
#include "mnemonic_tool_page.h"
#include "sensitive_pin.h"
#include "../signer_shell/signer_shell.h"

#include <lvgl.h>
#include <string.h>

static ui_menu_t *loaded_menu = NULL;
static lv_obj_t *loaded_screen = NULL;
static void (*return_callback)(void) = NULL;

static void return_from_words_cb(void) {
  mnemonic_words_page_destroy();
  loaded_mnemonic_menu_page_show();
}

static void return_from_entropy_cb(void) {
  mnemonic_entropy_page_destroy();
  loaded_mnemonic_menu_page_show();
}

static void return_from_backup_cb(void) {
  backup_menu_page_destroy();
  loaded_mnemonic_menu_page_show();
}

static void return_from_public_key_cb(void) {
  public_key_page_destroy();
  loaded_mnemonic_menu_page_show();
}

static void return_from_addresses_cb(void) {
  addresses_page_destroy();
  loaded_mnemonic_menu_page_show();
}

static void return_from_custom_derivation_cb(void) {
  custom_derivation_page_destroy();
  loaded_mnemonic_menu_page_show();
}

static void return_from_generated_editor_cb(void) {
  mnemonic_editor_page_destroy();
  loaded_mnemonic_menu_page_show();
}

static void success_from_generated_editor_cb(void) {
  key_confirmation_page_destroy();
  mnemonic_editor_page_destroy();
  loaded_mnemonic_menu_page_show();
}

static void return_from_mnemonic_tool_cb(void) {
  char *mnemonic = mnemonic_tool_page_get_completed_mnemonic();
  mnemonic_tool_page_destroy();
  if (mnemonic) {
    loaded_mnemonic_menu_page_hide();
    mnemonic_editor_page_create(lv_screen_active(),
                                return_from_generated_editor_cb,
                                success_from_generated_editor_cb, mnemonic,
                                true);
    mnemonic_editor_page_show();
    SECURE_FREE_STRING(mnemonic);
    return;
  }
  loaded_mnemonic_menu_page_show();
}

static void return_from_passphrase_cb(void) {
  passphrase_page_destroy();
  loaded_mnemonic_menu_page_show();
}

static void passphrase_success_cb(const char *passphrase) {
  char *mnemonic = NULL;
  if (!key_get_mnemonic(&mnemonic)) {
    passphrase_page_destroy();
    dialog_show_error("请先加载助记词", return_from_passphrase_cb, 0);
    return;
  }

  wallet_network_t net = wallet_is_initialized() ? wallet_get_network()
                                                 : settings_get_default_network();
  wallet_policy_t policy = wallet_is_initialized() ? wallet_get_policy()
                                                   : settings_get_default_policy();
  uint32_t account = wallet_is_initialized() ? wallet_get_account() : 0;

  wallet_cleanup();
  wallet_set_policy(policy);
  wallet_set_account(account);

  bool ok = key_load_from_mnemonic(mnemonic, passphrase,
                                   net == WALLET_NETWORK_TESTNET) &&
            wallet_init(net);
  SECURE_FREE_STRING(mnemonic);

  passphrase_page_destroy();
  if (!ok) {
    dialog_show_error("密码短语应用失败", return_from_passphrase_cb, 0);
    return;
  }

  (void)mnemonic_slots_add_current(NULL);
  dialog_show_message("已完成", "密码短语已应用，钱包指纹已更新。");
  loaded_mnemonic_menu_page_show();
}

static void launch_words(void) {
  loaded_mnemonic_menu_page_hide();
  mnemonic_words_page_create(lv_screen_active(), return_from_words_cb);
  mnemonic_words_page_show();
}

static void launch_entropy(void) {
  loaded_mnemonic_menu_page_hide();
  mnemonic_entropy_page_create(lv_screen_active(), return_from_entropy_cb);
  mnemonic_entropy_page_show();
}

static void launch_backup(void) {
  loaded_mnemonic_menu_page_hide();
  backup_menu_page_create(lv_screen_active(), return_from_backup_cb);
  backup_menu_page_show();
}

static void launch_public_key(void) {
  loaded_mnemonic_menu_page_hide();
  public_key_page_create(lv_screen_active(), return_from_public_key_cb);
  public_key_page_show();
}

static void launch_addresses(void) {
  loaded_mnemonic_menu_page_hide();
  addresses_page_create(lv_screen_active(), return_from_addresses_cb);
  addresses_page_show();
}

static void launch_custom_derivation(void) {
  loaded_mnemonic_menu_page_hide();
  custom_derivation_page_create(lv_screen_active(),
                                return_from_custom_derivation_cb);
  custom_derivation_page_show();
}

static void launch_secondary_shift(void) {
  loaded_mnemonic_menu_page_hide();
  mnemonic_tool_page_create(lv_screen_active(), return_from_mnemonic_tool_cb,
                            MNEMONIC_TOOL_SECONDARY_SHIFT);
  mnemonic_tool_page_show();
}

static void launch_bip85(void) {
  loaded_mnemonic_menu_page_hide();
  mnemonic_tool_page_create(lv_screen_active(), return_from_mnemonic_tool_cb,
                            MNEMONIC_TOOL_BIP85_MNEMONIC);
  mnemonic_tool_page_show();
}

static void launch_passphrase(void) {
  loaded_mnemonic_menu_page_hide();
  passphrase_page_create(lv_screen_active(), return_from_passphrase_cb,
                         passphrase_success_cb);
  passphrase_page_show();
}

static void launch_seedkeeper_write(void) {
  if (!signer_shell_show_screen("smartcard_seedkeeper_write_mnemonic")) {
    dialog_show_error("页面不可用", loaded_mnemonic_menu_page_show, 0);
  }
}

static void launch_satochip_write(void) {
  if (!signer_shell_show_screen("smartcard_satochip_write_mnemonic")) {
    dialog_show_error("页面不可用", loaded_mnemonic_menu_page_show, 0);
  }
}

static void words_cb(void) {
  sensitive_pin_require(launch_words, loaded_mnemonic_menu_page_show);
}
static void entropy_cb(void) { sensitive_pin_require(launch_entropy, loaded_mnemonic_menu_page_show); }
static void backup_cb(void) { sensitive_pin_require(launch_backup, loaded_mnemonic_menu_page_show); }
static void public_key_cb(void) { sensitive_pin_require(launch_public_key, loaded_mnemonic_menu_page_show); }
static void addresses_cb(void) { sensitive_pin_require(launch_addresses, loaded_mnemonic_menu_page_show); }
static void custom_derivation_cb(void) { sensitive_pin_require(launch_custom_derivation, loaded_mnemonic_menu_page_show); }
static void secondary_shift_cb(void) { sensitive_pin_require(launch_secondary_shift, loaded_mnemonic_menu_page_show); }
static void bip85_cb(void) { sensitive_pin_require(launch_bip85, loaded_mnemonic_menu_page_show); }
static void passphrase_cb(void) { sensitive_pin_require(launch_passphrase, loaded_mnemonic_menu_page_show); }
static void seedkeeper_write_cb(void) { sensitive_pin_require(launch_seedkeeper_write, loaded_mnemonic_menu_page_show); }
static void satochip_write_cb(void) { sensitive_pin_require(launch_satochip_write, loaded_mnemonic_menu_page_show); }

static void back_cb(void) {
  if (return_callback)
    return_callback();
}

void loaded_mnemonic_menu_page_create(lv_obj_t *parent, void (*return_cb)(void)) {
  if (!parent || !key_is_loaded())
    return;

  return_callback = return_cb;
  loaded_screen = theme_create_page_container(parent);
  loaded_menu = ui_menu_create(loaded_screen,
                               key_has_signing_key() ? "已加载助记词"
                                                     : "临时助记词",
                               back_cb);
  if (!loaded_menu)
    return;

  if (key_has_signing_key()) {
    ui_menu_add_entry(loaded_menu, "扩展公钥", public_key_cb);
    ui_menu_add_entry(loaded_menu, "地址核对", addresses_cb);
    ui_menu_add_entry(loaded_menu, "派生地址", custom_derivation_cb);
  }
  if (key_mnemonic_is_valid()) {
    ui_menu_add_entry(loaded_menu, "序号", words_cb);
    ui_menu_add_entry(loaded_menu, "原始熵", entropy_cb);
    ui_menu_add_entry(loaded_menu, "密码短语", passphrase_cb);
    ui_menu_add_entry(loaded_menu, "写SeedKeeper", seedkeeper_write_cb);
    ui_menu_add_entry(loaded_menu, "写Satochip", satochip_write_cb);
  }
  if (key_has_signing_key())
    ui_menu_add_entry(loaded_menu, "BIP85", bip85_cb);
  ui_menu_add_entry(loaded_menu, "助记词加密", secondary_shift_cb);
  if (key_can_backup_mnemonic())
    ui_menu_add_entry(loaded_menu, "备份导出", backup_cb);

  ui_menu_apply_compact_grid(loaded_menu);
}

void loaded_mnemonic_menu_page_show(void) {
  if (loaded_screen)
    lv_obj_clear_flag(loaded_screen, LV_OBJ_FLAG_HIDDEN);
  if (loaded_menu)
    ui_menu_show(loaded_menu);
}

void loaded_mnemonic_menu_page_hide(void) {
  if (loaded_screen)
    lv_obj_add_flag(loaded_screen, LV_OBJ_FLAG_HIDDEN);
  if (loaded_menu)
    ui_menu_hide(loaded_menu);
}

void loaded_mnemonic_menu_page_destroy(void) {
  if (loaded_menu) {
    ui_menu_destroy(loaded_menu);
    loaded_menu = NULL;
  }
  if (loaded_screen) {
    lv_obj_del(loaded_screen);
    loaded_screen = NULL;
  }
  return_callback = NULL;
}
