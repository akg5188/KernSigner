#include "signer_shell.h"
#include "signer_port/signer_camera_preview.h"
#include "signer_port/signer_feature_catalog.h"
#include "signer_port/signer_hardware_probe.h"
#include "signer_port/signer_services.h"
#include "signer_port/signer_storage_browser.h"
#include "core/crypto_utils.h"
#include "core/evm.h"
#include "core/key.h"
#include "core/pin.h"
#include "core/settings.h"
#include "i18n/i18n.h"
#include "qr/scanner.h"
#include "qr/viewer.h"
#include "smartcard/smartcard_ccid.h"
#include "smartcard/smartcard_satochip.h"
#include "smartcard/smartcard_transport.h"
#include "ui/dialog.h"
#include "ui/i18n_text.h"
#include "ui/input_helpers.h"
#include "ui/theme.h"
#include "utils/secure_mem.h"

#ifndef SIMULATOR
#include "esp_attr.h"
#include "core/mnemonic_slots.h"
#include "core/wallet.h"
#include "pages/home/addresses.h"
#include "pages/home/backup/backup_menu.h"
#include "pages/home/backup/mnemonic_1248.h"
#include "pages/home/backup/mnemonic_entropy.h"
#include "pages/home/backup/mnemonic_grid.h"
#include "pages/home/backup/mnemonic_qr.h"
#include "pages/home/backup/mnemonic_steel.h"
#include "pages/home/backup/mnemonic_words.h"
#include "pages/home/home.h"
#include "pages/home/public_key.h"
#include "pages/load_mnemonic/load_menu.h"
#include "pages/load_mnemonic/load_punch_grid.h"
#include "pages/load_mnemonic/load_storage.h"
#include "pages/load_mnemonic/stackbit_restore.h"
#include "pages/load_mnemonic/tinyseed_restore.h"
#include "pages/load_mnemonic/manual_input.h"
#include "pages/shared/key_confirmation.h"
#include "pages/shared/mnemonic_editor.h"
#include "pages/shared/mnemonic_tool_page.h"
#include "pages/login/login.h"
#include "pages/login/login_settings.h"
#include "pages/new_mnemonic/entropy_from_camera.h"
#include "pages/new_mnemonic/new_mnemonic_menu.h"
#include "pages/passphrase.h"
#include "qr/scanner.h"
#include "pages/scan/scan.h"
#include "pages/settings/descriptor_manager.h"
#include "pages/settings/wallet_settings.h"
#include "pages/store_mnemonic.h"
#include "pages/shared/bip39_check_page.h"
#include "pages/shared/custom_derivation_page.h"
#include "pages/shared/mnemonic_slots_page.h"
#include "pages/shared/loaded_mnemonic_menu.h"
#include "pages/shared/sensitive_pin.h"
#include "core/mnemonic_tools.h"
#endif
#ifdef SIMULATOR
#include "pages/shared/custom_derivation_page.h"
#endif

#ifndef EXT_RAM_BSS_ATTR
#define EXT_RAM_BSS_ATTR
#endif

#include <bsp/display.h>
#include <ctype.h>
#include <esp_app_desc.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/idf_additions.h>
#include <freertos/task.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wally_bip32.h>
#include <wally_bip39.h>
#include <wally_core.h>

static const char *TAG = "KSIG_SHELL";
#ifndef ESP_ERR_INVALID_RESPONSE
#define ESP_ERR_INVALID_RESPONSE ESP_ERR_INVALID_STATE
#endif
static lv_obj_t *s_parent;
static lv_obj_t *s_wallet_settings_status_label;
static lv_obj_t *s_qr_textarea;
static lv_obj_t *s_qr_preview;
static lv_obj_t *s_qr_status_label;
static const char *s_current_screen_id = "home";
static bool s_menu_grid_mode;
static bool s_rendering_home_grid;
static bool s_compact_menu_grid;
static ui_text_input_t s_satochip_pin_input;
static bool s_satochip_pin_input_active;
static evm_web3_profile_t s_satochip_pending_profile;
static char s_satochip_pending_return_id[40];
typedef enum {
  SATOCHIP_PIN_FLOW_CONNECT = 0,
  SATOCHIP_PIN_FLOW_TOOL,
} satochip_pin_flow_t;
typedef enum {
  SATOCHIP_TOOL_NONE = 0,
  SATOCHIP_TOOL_BTC_ZPUB,
  SATOCHIP_TOOL_BTC_XPUB,
  SATOCHIP_TOOL_BTC_YPUB,
  SATOCHIP_TOOL_BTC_TPUB,
  SATOCHIP_TOOL_BTC_UPUB,
  SATOCHIP_TOOL_BTC_VPUB,
  SATOCHIP_TOOL_PATH_ADDRESS,
} satochip_tool_mode_t;
static satochip_pin_flow_t s_satochip_pin_flow = SATOCHIP_PIN_FLOW_CONNECT;
static satochip_tool_mode_t s_satochip_tool_mode = SATOCHIP_TOOL_NONE;
static lv_obj_t *s_satochip_path_textarea;
#ifndef SIMULATOR
static bool s_allow_sensitive_render;
static char s_custom_derivation_return_id[40];
#endif

static const char *const SIGNER_HOME_MENU_IDS[] = {
    "legacy_scan_sign", "pi_connect_wallet", "pi_mnemonic_tools",
    "smartcard_tools", "settings", "pi_self_check",
};

typedef struct {
  const char *label;
  const char *target_id;
  const char *label_key;
} signer_menu_override_t;

static bool signer_id_is(const char *id, const char *expected) {
  return id && expected && strcmp(id, expected) == 0;
}

static bool signer_id_is_any(const char *id, const char *a, const char *b) {
  return signer_id_is(id, a) || signer_id_is(id, b);
}

static bool signer_smartcard_tools_group_id(const char *id) {
  return signer_id_is_any(id, "smartcard_satochip_tools",
                        "smartcard_satochip_maint") ||
         signer_id_is_any(id, "smartcard_satochip_advanced_tools",
                        "smartcard_satochip_pubkey_tools") ||
         signer_id_is_any(id, "smartcard_satochip_2fa_tools",
                        "smartcard_satochip_session_tools");
}

static bool signer_smartcard_seedkeeper_group_id(const char *id) {
  return signer_id_is_any(id, "smartcard_satochip_seedkeeper_tools",
                        "smartcard_seedkeeper_advanced_tools");
}

static bool signer_smartcard_certificate_group_id(const char *id) {
  return signer_id_is_any(id, "smartcard_satochip_certificate_tools",
                        "smartcard_certificate_tools");
}

static bool signer_id_has_prefix(const char *id, const char *prefix) {
  if (!id || !prefix)
    return false;
  return strncmp(id, prefix, strlen(prefix)) == 0;
}

static const char *shell_i18n_key_for_id(const char *id) {
  if (!id)
    return NULL;

  typedef struct {
    const char *id;
    const char *key;
  } id_key_t;
  static const id_key_t map[] = {
      {"home", "common.home"},
      {"legacy_scan_sign", "menu.scan_sign"},
      {"pi_connect_wallet", "menu.connect_wallet"},
      {"pi_mnemonic_tools", "menu.mnemonic"},
      {"smartcard_tools", "menu.smartcard"},
      {"settings", "menu.settings"},
      {"pi_self_check", "menu.self_check"},
      {"pi_features", "menu.features"},
      {"tools", "menu.tools"},
      {"about", "menu.about"},
      {"settings_pin", "pin.settings"},
      {"settings_locale", "settings.language"},
      {"settings_display", "settings.display"},
      {"settings_camera", "settings.camera"},
      {"settings_wallet", "settings.wallet"},
      {"settings_security", "settings.security"},
      {"smartcard_probe", "menu.smartcard"},
      {"system_overview", "tools.system_overview"},
      {"security_check", "tools.security_check"},
      {"delivery_check", "tools.delivery_check"},
      {"device_tests", "settings.device_tests"},
      {"test_screen_touch", "tools.touch_test"},
      {"test_camera", "tools.camera_test"},
      {"test_storage", "tools.storage_test"},
      {"test_power", "tools.power_test"},
      {"load_mnemonic", "wallet.load_mnemonic"},
      {"new_mnemonic", "wallet.new_mnemonic"},
      {"load_camera", "menu.qr_code"},
      {"load_manual", "menu.manual"},
      {"load_digits", "input.index_import"},
      {"load_seedkeeper_mnemonic", "menu.smartcard"},
      {"load_sd", "menu.storage_card"},
      {"load_encrypted_kef", "menu.encrypted"},
      {"load_steel_restore", "input.steel_restore"},
      {"load_punch_grid", "input.punch_grid"},
      {"new_dice_d6", "input.dice"},
      {"new_coin_entropy", "input.coin_flip"},
      {"new_dice_d20", "input.d20"},
      {"new_cards_entropy", "input.cards"},
      {"new_words_select", "menu.manual"},
      {"new_camera_entropy", "menu.camera"},
      {"new_hex_entropy", "input.hex"},
      {"new_seedkeeper_create_mnemonic", "menu.smartcard"},
      {"tools_create_qr", "menu.qr_code"},
      {"tools_qr_capture", "common.scan"},
      {"tools_file_manager", "menu.file"},
      {"legacy_select_mnemonic", "wallet.select_mnemonic"},
      {"legacy_public_key", "menu.public_key"},
      {"legacy_backup_wallet", "menu.backup"},
      {"mnemonic_write_smartcard", "menu.write_card"},
      {"login_passphrase", "menu.passphrase"},
      {"login_clear_session", "menu.clear_session"},
      {"pi_mnemonic_advanced", "menu.advanced"},
      {"tools_secondary_mnemonic", "input.mnemonic_encryption"},
      {"bip85", "input.bip85"},
      {"bip85_mnemonic", "input.bip85_mnemonic"},
      {"bip39_check_tools", "menu.self_check"},
      {"custom_derivation", "menu.derived_address"},
      {"addresses", "menu.addresses"},
      {"backup_export", "menu.backup"},
      {"backup_seed_words", "input.index_import"},
      {"backup_entropy", "input.hex"},
      {"backup_seed_qr", "menu.qr_code"},
      {"backup_kef", "menu.encrypted"},
      {"backup_grid", "input.punch_grid"},
      {"backup_steel_punch", "input.steel_restore"},
      {"backup_stackbit", "input.grid_1248"},
      {"smartcard_satochip_tools", "brand.satochip"},
      {"smartcard_satochip_seedkeeper_tools", "brand.seedkeeper"},
      {"smartcard_card_info", "menu.info"},
      {"satochip_path_address", "sign.derive_address"},
      {"connect_wallet_satochip_address", "sign.derive_address"},
      {"web3_address_satochip", "sign.derive_address"},
      {"connect_keystone", NULL},
      {"smartcard_satochip_write_mnemonic", "backup.satochip_write"},
      {"smartcard_satochip_maint", "menu.maintenance"},
      {"smartcard_satochip_advanced_tools", "menu.advanced"},
      {"smartcard_satochip_pubkey_tools", "menu.public_key"},
      {"smartcard_satochip_2fa_tools", NULL},
      {"smartcard_satochip_session_tools", NULL},
      {"smartcard_satochip_certificate_tools", "menu.certificate"},
      {"smartcard_satochip_setup_pin", "sign.setup_card_pin"},
      {"smartcard_satochip_change_pin", "sign.change_card_pin"},
      {"smartcard_satochip_unblock_pin", "sign.unlock_card"},
      {"smartcard_satochip_set_label", "sign.secret_label"},
      {"smartcard_satochip_nfc_policy", NULL},
      {"smartcard_satochip_feature_policy", NULL},
      {"smartcard_satochip_reset_seed", "common.reset"},
      {"smartcard_satochip_reset_factory", "menu.factory"},
      {"smartcard_satochip_authenticity", "sign.authenticity"},
      {"smartcard_satochip_export_authentikey", "sign.export_public_key"},
      {"smartcard_satochip_import_ndef_authentikey", "action.import"},
      {"smartcard_satochip_import_trusted_pubkey", "action.import"},
      {"smartcard_satochip_export_trusted_pubkey", "action.export"},
      {"smartcard_satochip_logout_all", "menu.clear_session"},
      {"smartcard_satochip_set_2fa_key", "action.save"},
      {"smartcard_satochip_reset_2fa_key", "dialog.clear"},
      {"smartcard_seedkeeper_status_page", "menu.status"},
      {"smartcard_seedkeeper_setup_pin", "sign.setup_card_pin"},
      {"smartcard_seedkeeper_change_pin", "sign.change_card_pin"},
      {"smartcard_seedkeeper_reset", NULL},
      {"smartcard_seedkeeper_free_space", "tools.free_space"},
      {"smartcard_seedkeeper_list_page", "menu.list"},
      {"smartcard_seedkeeper_create_mnemonic", "menu.create"},
      {"smartcard_seedkeeper_write_mnemonic", "backup.seedkeeper_write"},
      {"smartcard_seedkeeper_view_mnemonic", "menu.view"},
      {"smartcard_seedkeeper_load_mnemonic", "action.import"},
      {"smartcard_seedkeeper_save_secret", "action.save"},
      {"smartcard_seedkeeper_load_descriptor", "descriptor.load_descriptor"},
      {"smartcard_seedkeeper_save_descriptor", "descriptor.save_descriptor"},
      {"smartcard_seedkeeper_clone", "menu.clone"},
      {"smartcard_seedkeeper_advanced_tools", "menu.advanced"},
      {"smartcard_seedkeeper_generate_masterseed", NULL},
      {"smartcard_seedkeeper_generate_2fa_secret", NULL},
      {"smartcard_seedkeeper_derive_master_password", NULL},
      {"smartcard_seedkeeper_reset_secret", NULL},
      {"smartcard_certificate_export", "sign.export_certificate"},
      {"smartcard_certificate_import", "sign.import_certificate"},
      {"satochip_btc_pubkeys", "menu.public_key"},
  };

  for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
    if (strcmp(id, map[i].id) == 0)
      return map[i].key;
  }

  if (strstr(id, "_mnemonic"))
    return "menu.mnemonic";
  if (strstr(id, "_satochip") || strstr(id, "smartcard_"))
    return "menu.smartcard";
  if (strstr(id, "_camera"))
    return "menu.camera";
  return NULL;
}

static const char *shell_tr_for_id(const char *id, const char *fallback) {
  const char *key = shell_i18n_key_for_id(id);
  return key ? i18n_tr_or(key, fallback) : (fallback ? fallback : "");
}

static const char *shell_feature_title(const signer_feature_t *feature) {
  return feature ? shell_tr_for_id(feature->id, feature->title) : "";
}

static const char *shell_menu_item_label(const signer_menu_override_t *item) {
  if (!item)
    return "";
  if (item->label_key)
    return i18n_tr_or(item->label_key, item->label);
  return shell_tr_for_id(item->target_id, item->label);
}

static const char *shell_alias_target_for_id(const char *id) {
  if (!id)
    return NULL;
  if (strcmp(id, "connect_web3") == 0 || strcmp(id, "web3") == 0)
    return "pi_connect_wallet";
  if (strcmp(id, "web3_okx") == 0)
    return "connect_okx";
  if (strcmp(id, "web3_bitget") == 0)
    return "connect_bitget";
  if (strcmp(id, "web3_metamask") == 0)
    return "connect_metamask";
  if (strcmp(id, "web3_rabby") == 0)
    return "connect_rabby";
  if (strcmp(id, "web3_tokenpocket") == 0)
    return "connect_tokenpocket";
  if (strcmp(id, "web3_imtoken") == 0)
    return "connect_imtoken";
  if (strcmp(id, "connect_address") == 0 || strcmp(id, "web3_address") == 0)
    return "custom_derivation";
  if (strcmp(id, "smartcard_seedkeeper_save_password") == 0)
    return "smartcard_seedkeeper_save_secret";
  return NULL;
}

static const char *shell_back_target_for_feature(const signer_feature_t *feature) {
  if (!feature || !feature->parent_id)
    return "home";

  const char *id = feature->id;
  const char *alias_target = shell_alias_target_for_id(id);
  if (alias_target)
    return alias_target;

  if (strcmp(feature->parent_id, "connect_web3") == 0 ||
      strcmp(feature->parent_id, "web3") == 0)
    return "pi_connect_wallet";

  return feature->parent_id;
}

static bool signer_sign_wallet_group_id(const char *id) {
  return signer_id_is_any(id, "sign_okx", "sign_bitget") ||
         signer_id_is_any(id, "sign_metamask", "sign_rabby") ||
         signer_id_is_any(id, "sign_tokenpocket", "sign_imtoken") ||
         signer_id_is(id, "sign_btc");
}

static bool signer_sign_mnemonic_target_id(const char *id) {
  return signer_id_is_any(id, "sign_okx_mnemonic", "sign_bitget_mnemonic") ||
         signer_id_is_any(id, "sign_metamask_mnemonic",
                        "sign_rabby_mnemonic") ||
         signer_id_is_any(id, "sign_tokenpocket_mnemonic",
                        "sign_imtoken_mnemonic") ||
         signer_id_is(id, "sign_btc_mnemonic");
}

static bool signer_sign_satochip_target_id(const char *id) {
  return signer_id_is_any(id, "sign_okx_satochip", "sign_bitget_satochip") ||
         signer_id_is_any(id, "sign_metamask_satochip",
                        "sign_rabby_satochip") ||
         signer_id_is_any(id, "sign_tokenpocket_satochip",
                        "sign_imtoken_satochip") ||
         signer_id_is(id, "sign_btc_satochip");
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

static const signer_menu_override_t SIGNER_HOME_MENU[] = {
    {"Scan Sign", "legacy_scan_sign"},
    {"Connect Wallet", "pi_connect_wallet"},
    {"Mnemonic", "pi_mnemonic_tools"},
    {"Smartcard", "smartcard_tools"},
    {"Settings", "settings"},
    {"Self Check", "pi_self_check"},
    {"Features", "pi_features"},
    {"Tools", "tools"},
};

static const signer_menu_override_t SIGNER_LOAD_MENU[] = {
    {"QR Code", "load_camera"},
    {"Manual", "load_manual"},
    {"Indexes", "load_digits"},
    {"Smartcard", "load_seedkeeper_mnemonic"},
    {"Storage Card", "load_sd"},
    {"Encrypted", "load_encrypted_kef"},
    {"Steel Plate", "load_steel_restore"},
    {"Punch Grid", "load_punch_grid"},
};

static const signer_menu_override_t SIGNER_NEW_MENU[] = {
    {"Dice", "new_dice_d6"},
    {"Coin Flip", "new_coin_entropy"},
    {"D20", "new_dice_d20"},
    {"Cards", "new_cards_entropy"},
    {"Manual", "new_words_select"},
    {"Camera", "new_camera_entropy"},
    {"Hex", "new_hex_entropy"},
    {"Smartcard", "new_seedkeeper_create_mnemonic"},
};

static const signer_menu_override_t SIGNER_TOOLS_MENU[] = {
    {"QR Code", "tools_create_qr"},
    {"Scan", "tools_qr_capture"},
    {"Files", "tools_file_manager"},
};

static const signer_menu_override_t SIGNER_PI_MNEMONIC_MENU[] = {
    {"Create", "new_mnemonic"},
    {"Import", "load_mnemonic"},
    {"Device Mnemonics", "legacy_select_mnemonic"},
    {"Write Card", "mnemonic_write_smartcard"},
    {"Passphrase", "login_passphrase"},
    {"Advanced", "pi_mnemonic_advanced"},
};

static const signer_menu_override_t SIGNER_MNEMONIC_WRITE_SMARTCARD_MENU[] = {
    {"SeedKeeper", "smartcard_seedkeeper_write_mnemonic"},
    {"Satochip", "smartcard_satochip_write_mnemonic"},
};

static const signer_menu_override_t SIGNER_PI_MNEMONIC_ADVANCED_MENU[] = {
    {"Mnemonic Encryption", "tools_secondary_mnemonic"},
    {"Derived Address", "custom_derivation"},
    {"BIP85", "bip85"},
    {"Self Check", "bip39_check_tools"},
};

static const signer_menu_override_t SIGNER_PI_CONNECT_MENU[] = {
    {"BTC", "btc_wallet"},
    {"OKX", "connect_okx"},
    {"Bitget", "connect_bitget"},
    {"MetaMask", "connect_metamask"},
    {"Rabby", "connect_rabby"},
    {"TokenPocket", "connect_tokenpocket"},
    {"imToken", "connect_imtoken"},
    {"Derived Address", "custom_derivation", "menu.derived_address"},
    {"Keystone", "connect_keystone"},
};

static const signer_menu_override_t SIGNER_CONNECT_WEB3_MENU[] = {
    {"OKX", "connect_okx"},
    {"Bitget", "connect_bitget"},
    {"MetaMask", "connect_metamask"},
    {"Rabby", "connect_rabby"},
    {"TokenPocket", "connect_tokenpocket"},
    {"imToken", "connect_imtoken"},
};

static const signer_menu_override_t SIGNER_CONNECT_OKX_MENU[] = {
    {"Mnemonic", "web3_okx_mnemonic"},
    {"Smartcard", "web3_okx_satochip"},
};

static const signer_menu_override_t SIGNER_CONNECT_BITGET_MENU[] = {
    {"Mnemonic", "web3_bitget_mnemonic"},
    {"Smartcard", "web3_bitget_satochip"},
};

static const signer_menu_override_t SIGNER_CONNECT_METAMASK_MENU[] = {
    {"Mnemonic", "web3_metamask_mnemonic"},
    {"Smartcard", "web3_metamask_satochip"},
};

static const signer_menu_override_t SIGNER_CONNECT_RABBY_MENU[] = {
    {"Mnemonic", "web3_rabby_mnemonic"},
    {"Smartcard", "web3_rabby_satochip"},
};

static const signer_menu_override_t SIGNER_CONNECT_TOKENPOCKET_MENU[] = {
    {"Mnemonic", "web3_tokenpocket_mnemonic"},
    {"Smartcard", "web3_tokenpocket_satochip"},
};

static const signer_menu_override_t SIGNER_CONNECT_IMTOKEN_MENU[] = {
    {"Mnemonic", "web3_imtoken_mnemonic"},
    {"Smartcard", "web3_imtoken_satochip"},
};

static const signer_menu_override_t SIGNER_CONNECT_KEYSTONE_MENU[] = {
    {"Mnemonic", "web3_address_mnemonic"},
    {"Smartcard", "web3_address_satochip", "menu.smartcard"},
};

static const signer_menu_override_t SIGNER_BTC_WALLET_MENU[] = {
    {"Mnemonic", "btc_mnemonic"},
    {"Smartcard", "btc_satochip_zpub"},
};

static const signer_menu_override_t SIGNER_BTC_MNEMONIC_MENU[] = {
    {"zpub", "btc_bluewallet_zpub"},
    {"xpub", "btc_bluewallet_xpub"},
};

static const signer_menu_override_t SIGNER_BTC_SATOCHIP_MENU[] = {
    {"zpub", "btc_satochip_zpub"},
    {"xpub", "btc_satochip_xpub"},
};

static const signer_menu_override_t SIGNER_PI_SELF_CHECK_MENU[] = {
    {"System", "system_overview"},
    {"Security", "security_check"},
    {"Delivery", "delivery_check"},
    {"Smartcard", "smartcard_probe"},
    {"Device Tests", "device_tests"},
};

static const signer_menu_override_t SIGNER_WALLET_MENU[] = {
    {"Mnemonic", "legacy_select_mnemonic"},
    {"Scan Sign", "legacy_scan_sign"},
    {"Public Key", "legacy_public_key"},
    {"Derived Address", "custom_derivation"},
    {"Backup", "legacy_backup_wallet"},
    {"Settings", "settings_wallet"},
    {"Clear Session", "login_clear_session"},
};

static const signer_menu_override_t SIGNER_SIGNING_MENU[] = {
    {"BTC", "sign_btc"},
    {"OKX", "sign_okx"},
    {"Bitget", "sign_bitget"},
    {"MetaMask", "sign_metamask"},
    {"Rabby", "sign_rabby"},
    {"TokenPocket", "sign_tokenpocket"},
    {"imToken", "sign_imtoken"},
};

static const signer_menu_override_t SIGNER_SIGN_OKX_MENU[] = {
    {"Mnemonic", "sign_okx_mnemonic"},
    {"Smartcard", "sign_okx_satochip"},
};

static const signer_menu_override_t SIGNER_SIGN_BITGET_MENU[] = {
    {"Mnemonic", "sign_bitget_mnemonic"},
    {"Smartcard", "sign_bitget_satochip"},
};

static const signer_menu_override_t SIGNER_SIGN_METAMASK_MENU[] = {
    {"Mnemonic", "sign_metamask_mnemonic"},
    {"Smartcard", "sign_metamask_satochip"},
};

static const signer_menu_override_t SIGNER_SIGN_RABBY_MENU[] = {
    {"Mnemonic", "sign_rabby_mnemonic"},
    {"Smartcard", "sign_rabby_satochip"},
};

static const signer_menu_override_t SIGNER_SIGN_TOKENPOCKET_MENU[] = {
    {"Mnemonic", "sign_tokenpocket_mnemonic"},
    {"Smartcard", "sign_tokenpocket_satochip"},
};

static const signer_menu_override_t SIGNER_SIGN_IMTOKEN_MENU[] = {
    {"Mnemonic", "sign_imtoken_mnemonic"},
    {"Smartcard", "sign_imtoken_satochip"},
};

static const signer_menu_override_t SIGNER_SIGN_BTC_MENU[] = {
    {"Mnemonic", "sign_btc_mnemonic"},
    {"Smartcard", "sign_btc_satochip"},
};

static const signer_menu_override_t SIGNER_SMARTCARD_MENU[] = {
    {"Satochip", "smartcard_satochip_tools"},
    {"SeedKeeper", "smartcard_satochip_seedkeeper_tools"},
};

static const signer_menu_override_t SIGNER_SMARTCARD_SATOCHIP_MENU[] = {
    {"Info", "smartcard_card_info"},
    {"Address", "satochip_path_address"},
    {"Write", "smartcard_satochip_write_mnemonic"},
    {"BTC Public Key", "satochip_btc_pubkeys"},
    {"Maintenance", "smartcard_satochip_maint"},
    {"Advanced", "smartcard_satochip_advanced_tools"},
    {"Certificate", "smartcard_satochip_certificate_tools"},
};

static const signer_menu_override_t SIGNER_SMARTCARD_MAINT_MENU[] = {
    {"Setup PIN", "smartcard_satochip_setup_pin"},
    {"Change PIN", "smartcard_satochip_change_pin"},
    {"Unlock", "smartcard_satochip_unblock_pin"},
    {"Change Label", "smartcard_satochip_set_label"},
    {"NFC策略", "smartcard_satochip_nfc_policy"},
    {"功能策略", "smartcard_satochip_feature_policy"},
    {"Reset", "smartcard_satochip_reset_seed"},
    {"Factory", "smartcard_satochip_reset_factory"},
    {"Authenticity", "smartcard_satochip_authenticity"},
};

static const signer_menu_override_t SIGNER_SMARTCARD_ADVANCED_MENU[] = {
    {"Authentikey", "smartcard_satochip_pubkey_tools"},
    {"2FA", "smartcard_satochip_2fa_tools"},
    {"Session", "smartcard_satochip_session_tools"},
};

static const signer_menu_override_t SIGNER_SMARTCARD_PUBKEY_MENU[] = {
    {"Export Authentikey", "smartcard_satochip_export_authentikey"},
    {"Write NDEF", "smartcard_satochip_import_ndef_authentikey"},
    {"Write Trusted Key", "smartcard_satochip_import_trusted_pubkey"},
    {"Export Trusted Key", "smartcard_satochip_export_trusted_pubkey"},
};

static const signer_menu_override_t SIGNER_SMARTCARD_2FA_MENU[] = {
    {"Set 2FA", "smartcard_satochip_set_2fa_key"},
    {"Clear 2FA", "smartcard_satochip_reset_2fa_key"},
};

static const signer_menu_override_t SIGNER_SMARTCARD_SESSION_MENU[] = {
    {"Logout", "smartcard_satochip_logout_all"},
};

static const signer_menu_override_t SIGNER_SMARTCARD_SEEDKEEPER_MENU[] = {
    {"Status", "smartcard_seedkeeper_status_page"},
    {"Setup PIN", "smartcard_seedkeeper_setup_pin"},
    {"Change PIN", "smartcard_seedkeeper_change_pin"},
    {"清空SeedKeeper", "smartcard_seedkeeper_reset"},
    {"Free Space", "smartcard_seedkeeper_free_space"},
    {"List", "smartcard_seedkeeper_list_page"},
    {"Create", "smartcard_seedkeeper_create_mnemonic"},
    {"Write", "smartcard_seedkeeper_write_mnemonic"},
    {"View", "smartcard_seedkeeper_view_mnemonic"},
    {"Import", "smartcard_seedkeeper_load_mnemonic"},
    {"Save", "smartcard_seedkeeper_save_secret"},
    {"Load Descriptor", "smartcard_seedkeeper_load_descriptor"},
    {"Save Descriptor", "smartcard_seedkeeper_save_descriptor"},
    {"Clone", "smartcard_seedkeeper_clone"},
    {"Advanced", "smartcard_seedkeeper_advanced_tools"},
};

static const signer_menu_override_t SIGNER_SMARTCARD_SEEDKEEPER_ADVANCED_MENU[] = {
    {"Masterseed", "smartcard_seedkeeper_generate_masterseed"},
    {"2FA", "smartcard_seedkeeper_generate_2fa_secret"},
    {"Derive Password", "smartcard_seedkeeper_derive_master_password"},
    {"删除条目", "smartcard_seedkeeper_reset_secret"},
};

static const signer_menu_override_t SIGNER_SMARTCARD_CERTIFICATE_MENU[] = {
    {"Export", "smartcard_certificate_export"},
    {"Import", "smartcard_certificate_import"},
};

static const signer_feature_t SIGNER_LOCAL_SMARTCARD_FEATURES[] = {
    {"mnemonic_write_smartcard", "pi_mnemonic_tools", "Write Card",
     "SeedKeeper / Satochip", "Write the current mnemonic to a smartcard.",
     "main/pages/signer_shell/signer_shell.c", SIGNER_FEATURE_GROUP,
     SIGNER_FEATURE_UI_READY, SIGNER_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_satochip_write_mnemonic", "smartcard_satochip_tools",
     "Write", "Current mnemonic", "Write the current mnemonic to Satochip.",
     "main/pages/signer_shell/signer_shell.c", SIGNER_FEATURE_ACTION,
     SIGNER_FEATURE_UI_READY, SIGNER_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_satochip_seedkeeper_tools", "smartcard_tools",
     "SeedKeeper", "Status / Mnemonic / Advanced",
     "Manage SeedKeeper.",
     "main/pages/signer_shell/signer_shell.c", SIGNER_FEATURE_GROUP,
     SIGNER_FEATURE_UI_READY, SIGNER_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_seedkeeper_status_page",
     "smartcard_satochip_seedkeeper_tools", "Status", "Card / Space",
     "View SeedKeeper status and space.",
     "main/pages/signer_shell/signer_shell.c", SIGNER_FEATURE_ACTION,
     SIGNER_FEATURE_UI_READY, SIGNER_FEATURE_RISK_EXTERNAL_IO},
    {"smartcard_seedkeeper_setup_pin",
     "smartcard_satochip_seedkeeper_tools", "Setup PIN", "New card setup",
     "Set the PIN for a new or reset SeedKeeper.",
     "main/pages/signer_shell/signer_shell.c", SIGNER_FEATURE_ACTION,
     SIGNER_FEATURE_UI_READY, SIGNER_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_seedkeeper_free_space",
     "smartcard_satochip_seedkeeper_tools", "Free Space", "Available space",
     "View remaining SeedKeeper space.",
     "main/pages/signer_shell/signer_shell.c", SIGNER_FEATURE_ACTION,
     SIGNER_FEATURE_UI_READY, SIGNER_FEATURE_RISK_EXTERNAL_IO},
    {"smartcard_seedkeeper_list_page",
     "smartcard_satochip_seedkeeper_tools", "List", "Items",
     "View the item list on the card.",
     "main/pages/signer_shell/signer_shell.c", SIGNER_FEATURE_ACTION,
     SIGNER_FEATURE_UI_READY, SIGNER_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_seedkeeper_advanced_tools",
     "smartcard_satochip_seedkeeper_tools", "Advanced", "Generate / Derive / Delete",
     "SeedKeeper advanced actions.",
     "main/pages/signer_shell/signer_shell.c", SIGNER_FEATURE_GROUP,
     SIGNER_FEATURE_UI_READY, SIGNER_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_seedkeeper_generate_masterseed",
     "smartcard_seedkeeper_advanced_tools", "Masterseed", "Generate masterseed",
     "Generate a masterseed on the card.",
     "main/pages/signer_shell/signer_shell.c", SIGNER_FEATURE_ACTION,
     SIGNER_FEATURE_UI_READY, SIGNER_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_seedkeeper_generate_2fa_secret",
     "smartcard_seedkeeper_advanced_tools", "2FA", "Generate 2FA",
     "Generate a 2FA secret on the card.",
     "main/pages/signer_shell/signer_shell.c", SIGNER_FEATURE_ACTION,
     SIGNER_FEATURE_UI_READY, SIGNER_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_seedkeeper_derive_master_password",
     "smartcard_seedkeeper_advanced_tools", "Derive Password", "Derive by SID",
     "Derive a master password from a card item.",
     "main/pages/signer_shell/signer_shell.c", SIGNER_FEATURE_ACTION,
     SIGNER_FEATURE_UI_READY, SIGNER_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_seedkeeper_reset_secret",
     "smartcard_seedkeeper_advanced_tools", "删除条目", "按SID删除",
     "Delete a specific item on the card.",
     "main/pages/signer_shell/signer_shell.c", SIGNER_FEATURE_ACTION,
     SIGNER_FEATURE_UI_READY, SIGNER_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_seedkeeper_create_mnemonic",
     "smartcard_satochip_seedkeeper_tools", "Create on Card", "Card RNG",
     "Create a mnemonic from SeedKeeper random data.",
     "main/pages/signer_shell/signer_shell.c", SIGNER_FEATURE_ACTION,
     SIGNER_FEATURE_UI_READY, SIGNER_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_seedkeeper_write_mnemonic",
     "smartcard_satochip_seedkeeper_tools", "Write Card", "Current mnemonic",
     "Write the current mnemonic to the card.",
     "main/pages/signer_shell/signer_shell.c", SIGNER_FEATURE_ACTION,
     SIGNER_FEATURE_UI_READY, SIGNER_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_seedkeeper_view_mnemonic",
     "smartcard_satochip_seedkeeper_tools", "View Card", "Card mnemonic",
     "View the mnemonic stored on the card.",
     "main/pages/signer_shell/signer_shell.c", SIGNER_FEATURE_ACTION,
     SIGNER_FEATURE_UI_READY, SIGNER_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_seedkeeper_load_mnemonic",
     "smartcard_satochip_seedkeeper_tools", "Card Import", "Card to memory",
     "Load the card mnemonic into device memory.",
     "main/pages/signer_shell/signer_shell.c", SIGNER_FEATURE_ACTION,
     SIGNER_FEATURE_UI_READY, SIGNER_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_seedkeeper_save_secret",
     "smartcard_satochip_seedkeeper_tools", "Save", "Text / password",
     "Save text or password data to SeedKeeper.",
     "main/pages/signer_shell/signer_shell.c", SIGNER_FEATURE_ACTION,
     SIGNER_FEATURE_UI_READY, SIGNER_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_seedkeeper_load_descriptor",
     "smartcard_satochip_seedkeeper_tools", "Load Descriptor", "Load from card",
     "Load a wallet descriptor from SeedKeeper.",
     "main/pages/signer_shell/signer_shell.c", SIGNER_FEATURE_ACTION,
     SIGNER_FEATURE_UI_READY, SIGNER_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_seedkeeper_save_descriptor",
     "smartcard_satochip_seedkeeper_tools", "Save Descriptor", "Write card",
     "Save a wallet descriptor to SeedKeeper.",
     "main/pages/signer_shell/signer_shell.c", SIGNER_FEATURE_ACTION,
     SIGNER_FEATURE_UI_READY, SIGNER_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_seedkeeper_clone",
     "smartcard_satochip_seedkeeper_tools", "Clone", "Card to card",
     "Clone SeedKeeper items.",
     "main/pages/signer_shell/signer_shell.c", SIGNER_FEATURE_ACTION,
     SIGNER_FEATURE_UI_READY, SIGNER_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_seedkeeper_change_pin",
     "smartcard_satochip_seedkeeper_tools", "Change PIN", "Change card PIN",
     "Change the SeedKeeper card PIN.",
     "main/pages/signer_shell/signer_shell.c", SIGNER_FEATURE_ACTION,
     SIGNER_FEATURE_UI_READY, SIGNER_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_seedkeeper_reset",
     "smartcard_satochip_seedkeeper_tools", "清空SeedKeeper", "整卡清空",
     "Wipe SeedKeeper and return it to an uninitialized state.",
     "main/pages/signer_shell/signer_shell.c", SIGNER_FEATURE_ACTION,
     SIGNER_FEATURE_UI_READY, SIGNER_FEATURE_RISK_SECRET_MATERIAL},
    {"new_seedkeeper_create_mnemonic",
     "new_mnemonic", "Smartcard", "Card RNG",
     "Create a mnemonic from SeedKeeper random data.",
     "main/pages/signer_shell/signer_shell.c", SIGNER_FEATURE_ACTION,
     SIGNER_FEATURE_UI_READY, SIGNER_FEATURE_RISK_SECRET_MATERIAL},
    {"connect_keystone", "pi_connect_wallet", "Keystone", "Choose Source",
     "Choose a source for the Keystone-compatible address QR.",
     "main/pages/signer_shell/signer_shell.c", SIGNER_FEATURE_GROUP,
     SIGNER_FEATURE_UI_READY, SIGNER_FEATURE_RISK_VIEW_ONLY},
    {"web3_address_mnemonic", "connect_keystone", "Mnemonic",
     "Keystone Address QR",
     "Show the EVM address QR derived from the current mnemonic.",
     "main/pages/signer_shell/signer_shell.c", SIGNER_FEATURE_ACTION,
     SIGNER_FEATURE_UI_READY, SIGNER_FEATURE_RISK_VIEW_ONLY},
    {"web3_address_satochip", "connect_keystone", "Smartcard",
     "Keystone Address QR",
     "Read the EVM address from the smartcard and show its QR code.",
     "main/pages/signer_shell/signer_shell.c", SIGNER_FEATURE_ACTION,
     SIGNER_FEATURE_UI_READY, SIGNER_FEATURE_RISK_EXTERNAL_IO},
    {"load_seedkeeper_mnemonic",
     "load_mnemonic", "Smartcard", "Import from card",
     "Import a mnemonic from SeedKeeper.",
     "main/pages/signer_shell/signer_shell.c", SIGNER_FEATURE_ACTION,
     SIGNER_FEATURE_UI_READY, SIGNER_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_satochip_certificate_tools", "smartcard_satochip_tools",
     "Certificate", "Export / Import", "Personal certificate export and import.",
     "main/pages/signer_shell/signer_shell.c", SIGNER_FEATURE_GROUP,
     SIGNER_FEATURE_SERVICE_STUB, SIGNER_FEATURE_RISK_EXTERNAL_IO},
    {"smartcard_certificate_export", "smartcard_satochip_certificate_tools",
     "Export", "Export personal certificate", "Export the personal certificate PEM text.",
     "main/pages/signer_shell/signer_shell.c", SIGNER_FEATURE_ACTION,
     SIGNER_FEATURE_UI_READY, SIGNER_FEATURE_RISK_VIEW_ONLY},
    {"smartcard_certificate_import", "smartcard_satochip_certificate_tools",
     "Import", "Import personal certificate", "Import personal certificate PEM text.",
     "main/pages/signer_shell/signer_shell.c", SIGNER_FEATURE_ACTION,
     SIGNER_FEATURE_UI_READY, SIGNER_FEATURE_RISK_EXTERNAL_IO},
    {"smartcard_satochip_change_pin", "smartcard_satochip_maint", "Change PIN",
     "Change card PIN", "Enter the old PIN and new PIN.", "main/pages/signer_shell/signer_shell.c",
     SIGNER_FEATURE_ACTION, SIGNER_FEATURE_UI_READY,
     SIGNER_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_satochip_unblock_pin", "smartcard_satochip_maint", "Unlock",
     "PUK unlock", "Use the PUK to unblock a locked PIN.",
     "main/pages/signer_shell/signer_shell.c", SIGNER_FEATURE_ACTION,
     SIGNER_FEATURE_UI_READY, SIGNER_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_satochip_set_label", "smartcard_satochip_maint", "Change Label",
     "Change card label", "Write the card label.", "main/pages/signer_shell/signer_shell.c",
     SIGNER_FEATURE_ACTION, SIGNER_FEATURE_UI_READY,
     SIGNER_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_satochip_nfc_policy", "smartcard_satochip_maint", "NFC策略",
     "NFC开关", "Write the NFC policy value.", "main/pages/signer_shell/signer_shell.c",
     SIGNER_FEATURE_ACTION, SIGNER_FEATURE_UI_READY,
     SIGNER_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_satochip_feature_policy", "smartcard_satochip_maint",
     "功能策略", "功能开关", "Write the Schnorr / Nostr / Liquid policy.",
     "main/pages/signer_shell/signer_shell.c", SIGNER_FEATURE_ACTION,
     SIGNER_FEATURE_UI_READY, SIGNER_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_satochip_reset_seed", "smartcard_satochip_maint", "Reset",
     "Reset card seed", "Perform a seed reset.", "main/pages/signer_shell/signer_shell.c",
     SIGNER_FEATURE_ACTION, SIGNER_FEATURE_UI_READY,
     SIGNER_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_satochip_reset_factory", "smartcard_satochip_maint", "Factory",
     "Factory reset", "Send the factory reset signal.", "main/pages/signer_shell/signer_shell.c",
     SIGNER_FEATURE_ACTION, SIGNER_FEATURE_UI_READY,
     SIGNER_FEATURE_RISK_DEVICE_CONTROL},
    {"smartcard_satochip_authenticity", "smartcard_satochip_maint", "Authenticity",
     "Check authenticity", "Read personal certificate and authenticity status.", "main/pages/signer_shell/signer_shell.c",
     SIGNER_FEATURE_ACTION, SIGNER_FEATURE_UI_READY, SIGNER_FEATURE_RISK_VIEW_ONLY},
};

static const signer_menu_override_t SIGNER_SATOCHIP_PUBKEY_MENU[] = {
    {"zpub", "satochip_btc_zpub"},
    {"ypub", "satochip_btc_ypub"},
    {"xpub", "satochip_btc_xpub"},
    {"vpub", "satochip_btc_vpub"},
    {"upub", "satochip_btc_upub"},
    {"tpub", "satochip_btc_tpub"},
};

static const signer_menu_override_t SIGNER_BACKUP_MENU[] = {
    {"Index", "backup_seed_words", "input.index_import"},
    {"Raw Entropy", "backup_entropy", "wallet.raw_entropy"},
    {"QR Code", "backup_seed_qr", "menu.qr_code"},
    {"Encrypted", "backup_kef", "menu.encrypted"},
    {"Punch Grid", "backup_grid", "input.punch_grid"},
    {"Steel", "backup_steel_punch", "input.steel_restore"},
    {"1248", "backup_stackbit", "input.grid_1248"},
};

static const signer_menu_override_t SIGNER_ADDRESSES_MENU[] = {
    {"Derived Address", "custom_derivation", "menu.derived_address"},
    {"Receive", "custom_derivation", "address.receive"},
    {"Change", "custom_derivation", "address.change"},
    {"Scan", "legacy_addresses", "common.scan"},
    {"QR Code", "legacy_addresses", "menu.qr_code"},
};

static const signer_menu_override_t SIGNER_SETTINGS_MENU[] = {
    {"PIN", "settings_pin", "pin.settings"},
    {"Language", "settings_locale", "settings.language"},
    {"Brightness", "settings_display", "settings.brightness"},
    {"Camera", "settings_camera", "settings.camera"},
    {"Wallet", "settings_wallet", "settings.wallet"},
    {"Security", "settings_security", "settings.security"},
    {"About", "about", "menu.about"},
};

static const signer_menu_override_t SIGNER_DEVICE_TESTS_MENU[] = {
    {"Touch", "test_screen_touch", "tools.touch_test"},
    {"Camera", "test_camera", "tools.camera_test"},
    {"Storage Card", "test_storage", "tools.storage_test"},
    {"Power", "test_power", "tools.power_test"},
};

#pragma GCC diagnostic pop

static const char *const PRODUCT_SCREEN_IDS[] = {
    "home",
    "pi_connect_wallet",
    "connect_okx",
    "connect_bitget",
    "connect_metamask",
    "connect_rabby",
    "connect_tokenpocket",
    "connect_imtoken",
    "web3_okx_mnemonic",
    "web3_okx_satochip",
    "web3_bitget_mnemonic",
    "web3_bitget_satochip",
    "web3_metamask_mnemonic",
    "web3_metamask_satochip",
    "web3_rabby_mnemonic",
    "web3_rabby_satochip",
    "web3_tokenpocket_mnemonic",
    "web3_tokenpocket_satochip",
    "web3_imtoken_mnemonic",
    "web3_imtoken_satochip",
    "connect_keystone",
    "web3_address_mnemonic",
    "web3_address_satochip",
    "btc_wallet",
    "btc_mnemonic",
    "btc_satochip",
    "btc_bluewallet_zpub",
    "btc_bluewallet_xpub",
    "btc_satochip_zpub",
    "btc_satochip_xpub",
    "custom_derivation",
    "connect_wallet_satochip_address",
    "pi_mnemonic_tools",
    "mnemonic_write_smartcard",
    "smartcard_tools",
    "smartcard_satochip_tools",
    "smartcard_satochip_write_mnemonic",
    "smartcard_satochip_maint",
    "smartcard_satochip_advanced_tools",
    "smartcard_satochip_pubkey_tools",
    "smartcard_satochip_2fa_tools",
    "smartcard_satochip_session_tools",
    "smartcard_satochip_seedkeeper_tools",
    "smartcard_satochip_certificate_tools",
    "smartcard_satochip_change_pin",
    "smartcard_satochip_unblock_pin",
    "smartcard_satochip_set_label",
    "smartcard_satochip_nfc_policy",
    "smartcard_satochip_feature_policy",
    "smartcard_satochip_reset_seed",
    "smartcard_satochip_reset_factory",
    "smartcard_satochip_authenticity",
    "smartcard_satochip_setup_pin",
    "smartcard_satochip_export_authentikey",
    "smartcard_satochip_import_ndef_authentikey",
    "smartcard_satochip_import_trusted_pubkey",
    "smartcard_satochip_export_trusted_pubkey",
    "smartcard_satochip_set_2fa_key",
    "smartcard_satochip_reset_2fa_key",
    "smartcard_satochip_logout_all",
    "smartcard_seedkeeper_status_page",
    "smartcard_seedkeeper_free_space",
    "smartcard_seedkeeper_list_page",
    "smartcard_seedkeeper_advanced_tools",
    "smartcard_seedkeeper_setup_pin",
    "smartcard_seedkeeper_create_mnemonic",
    "smartcard_seedkeeper_write_mnemonic",
    "smartcard_seedkeeper_view_mnemonic",
    "smartcard_seedkeeper_load_mnemonic",
    "smartcard_seedkeeper_save_secret",
    "smartcard_seedkeeper_load_descriptor",
    "smartcard_seedkeeper_save_descriptor",
    "smartcard_seedkeeper_clone",
    "smartcard_seedkeeper_change_pin",
    "smartcard_seedkeeper_reset",
    "smartcard_seedkeeper_generate_masterseed",
    "smartcard_seedkeeper_generate_2fa_secret",
    "smartcard_seedkeeper_derive_master_password",
    "smartcard_seedkeeper_reset_secret",
    "new_seedkeeper_create_mnemonic",
    "load_seedkeeper_mnemonic",
    "smartcard_certificate_import",
    "satochip_btc_pubkeys",
    "satochip_btc_ypub",
    "satochip_btc_tpub",
    "satochip_btc_upub",
    "satochip_btc_vpub",
    "pi_self_check",
    "tools",
    "pi_features",
    "settings",
    "about",
    "boot_login",
    "login_pin",
    "login_passphrase",
    "login_encrypted_backup",
    "login_clear_session",
    "load_mnemonic",
    "load_camera",
    "load_manual",
    "load_digits",
    "load_steel_restore",
    "load_punch_grid",
    "load_tinyseed_restore",
    "load_stackbit_restore",
    "load_sd",
    "load_encrypted_kef",
    "new_mnemonic",
    "new_dice_d6",
    "new_coin_entropy",
    "new_dice_d20",
    "new_cards_entropy",
    "new_hex_entropy",
    "new_words_select",
    "new_camera_entropy",
    "bip39_check_tools",
    "pi_mnemonic_advanced",
    "bip85_mnemonic",
    "tools_secondary_mnemonic",
    "wallet_home",
    "backup_export",
    "backup_seed_words",
    "backup_entropy",
    "backup_steel_punch",
    "backup_stackbit",
    "backup_grid",
    "backup_seed_qr",
    "backup_kef",
    "smartcard_card_info",
    "satochip_path_address",
    "satochip_btc_zpub",
    "satochip_btc_xpub",
    "tools_create_qr",
    "tools_qr_capture",
    "tools_file_manager",
    "settings_pin",
    "settings_locale",
    "settings_display",
    "settings_camera",
    "settings_wallet",
    "settings_security",
    "device_tests",
    "test_screen_touch",
    "test_camera",
    "test_storage",
    "test_power",
    "smartcard_probe",
    "system_overview",
    "security_check",
    "delivery_check",
};

static bool product_screen_is_visible(const char *id) {
  if (!id)
    return false;

  return !signer_id_is_any(id, "connect_mnemonic", "connect_satochip") &&
         strcmp(id, "smartcard_seedkeeper") != 0 &&
         !signer_id_is_any(id, "smartcard_seedkeeper_status",
                         "smartcard_seedkeeper_list") &&
         strcmp(id, "smartcard_seedkeeper_logs") != 0 &&
         strcmp(id, "smartcard_seedkeeper_logs_page") != 0 &&
         !signer_id_is_any(id, "seedkeeper_status", "seedkeeper_list") &&
         strcmp(id, "seedkeeper_logs") != 0 &&
         strcmp(id, "connect_address") != 0 &&
         !signer_id_is_any(id, "addresses", "addr_receive") &&
         !signer_id_is_any(id, "addr_change", "addr_scan_check") &&
         strcmp(id, "addr_qr_view") != 0 &&
         strcmp(id, "connect_web3") != 0 &&
         strcmp(id, "smartcard_web3_scan") != 0 &&
         strcmp(id, "web3_satochip") != 0 &&
         !signer_id_is_any(id, "sign_mnemonic", "sign_satochip") &&
         !signer_id_is_any(id, "web3", "web3_okx") &&
         !signer_id_is_any(id, "web3_bitget", "web3_metamask") &&
         !signer_id_is_any(id, "web3_rabby", "web3_tokenpocket") &&
         strcmp(id, "web3_imtoken") != 0 &&
         !signer_id_is_any(id, "web3_address", "web3_message_sign") &&
         strcmp(id, "web3_typed_data") != 0;
}

static const signer_feature_t *shell_local_feature_find(const char *id) {
  if (!id)
    return NULL;

  for (size_t i = 0; i < sizeof(SIGNER_LOCAL_SMARTCARD_FEATURES) /
                           sizeof(SIGNER_LOCAL_SMARTCARD_FEATURES[0]); i++) {
    if (strcmp(SIGNER_LOCAL_SMARTCARD_FEATURES[i].id, id) == 0)
      return &SIGNER_LOCAL_SMARTCARD_FEATURES[i];
  }
  return NULL;
}

static const signer_feature_t *shell_feature_find(const char *id) {
  const signer_feature_t *feature = signer_feature_find(id);
  if (feature)
    return feature;
  return shell_local_feature_find(id);
}

static int max_i(int a, int b) { return a > b ? a : b; }

static lv_color_t signer_canvas_color(void) { return lv_color_hex(0x000000); }
static lv_color_t signer_card_color(void) { return bg_color(); }
static lv_color_t signer_card_pressed_color(void) { return highlight_color(); }
static lv_color_t signer_text_color(void) { return main_color(); }
static lv_color_t signer_muted_color(void) { return secondary_color(); }

static bool shell_is_wave_43_portrait(void) {
  return theme_get_screen_width() <= 520 && theme_get_screen_height() >= 760;
}

static int shell_margin_h(void) {
  int w = theme_get_screen_width();
  if (shell_is_wave_43_portrait())
    return 18;
  if (w >= 700)
    return 38;
  return max_i(18, w / 18);
}

static int shell_margin_v(void) {
  if (shell_is_wave_43_portrait())
    return 16;
  return max_i(18, theme_get_default_padding());
}

static int shell_status_height(void) {
  return shell_is_wave_43_portrait() ? 34 : 32;
}

static int shell_header_pad_y(void) {
  return shell_is_wave_43_portrait() ? 12 : 18;
}

static int shell_root_gap(bool is_home) {
  if (shell_is_wave_43_portrait())
    return is_home ? 8 : 8;
  return is_home ? 28 : 12;
}

static int shell_card_radius(void) {
  return 8;
}

static int shell_menu_button_height(bool multiline, bool has_subtitle) {
  if (shell_is_wave_43_portrait())
    return (multiline || has_subtitle) ? 84 : 66;
  return (multiline || has_subtitle) ? 96 : 74;
}

static int shell_home_card_height(void) {
  if (shell_is_wave_43_portrait()) {
    const int rows = 4;
    const int row_gaps = rows - 1;
    const int home_gap = 10;
    int available = theme_get_screen_height() - shell_margin_v() * 2 -
                    shell_status_height() - shell_root_gap(true) -
                    home_gap * row_gaps;
    return max_i(128, available / rows);
  }
  return 168;
}

static int shell_bottom_nav_height(void) {
  return shell_is_wave_43_portrait() ? 54 : 58;
}

static int shell_menu_gap(void) {
  if (s_compact_menu_grid)
    return shell_is_wave_43_portrait() ? 6 : 8;
  return shell_is_wave_43_portrait() ? 10 : max_i(10, theme_get_small_padding());
}

static bool shell_home_grid_active(void) {
  return s_menu_grid_mode && s_rendering_home_grid;
}

static bool shell_compact_grid_active(void) {
  return s_menu_grid_mode && s_compact_menu_grid;
}

static bool shell_should_use_grid(size_t menu_count, bool is_home,
                                  const char *id) {
  if (theme_get_screen_width() < 420)
    return false;
  if (id && (strcmp(id, "smartcard_tools") == 0 ||
             signer_smartcard_tools_group_id(id) ||
             signer_smartcard_seedkeeper_group_id(id) ||
             signer_smartcard_certificate_group_id(id)))
    return true;
  return is_home || menu_count >= 5;
}

static bool shell_force_single_column_menu(const char *id) {
  return id && (strcmp(id, "sign_okx") == 0 ||
                strcmp(id, "sign_bitget") == 0 ||
                strcmp(id, "sign_metamask") == 0 ||
                strcmp(id, "sign_rabby") == 0 ||
                strcmp(id, "sign_tokenpocket") == 0 ||
                strcmp(id, "sign_btc") == 0 ||
                strcmp(id, "sign_satochip") == 0 ||
                strcmp(id, "btc_wallet") == 0 ||
                strcmp(id, "btc_mnemonic") == 0 ||
                strcmp(id, "btc_satochip") == 0);
}

static lv_color_t status_color(signer_feature_status_t status) {
  switch (status) {
  case SIGNER_FEATURE_VERIFIED:
    return yes_color();
  case SIGNER_FEATURE_HARDWARE_WIRED:
    return cyan_color();
  case SIGNER_FEATURE_SERVICE_STUB:
    return highlight_color();
  case SIGNER_FEATURE_UI_READY:
    return main_color();
  case SIGNER_FEATURE_NOT_STARTED:
  default:
    return secondary_color();
  }
}

static lv_color_t risk_color(signer_feature_risk_t risk) {
  switch (risk) {
  case SIGNER_FEATURE_RISK_SIGNING:
  case SIGNER_FEATURE_RISK_SECRET_MATERIAL:
    return error_color();
  case SIGNER_FEATURE_RISK_EXTERNAL_IO:
  case SIGNER_FEATURE_RISK_DEVICE_CONTROL:
    return cyan_color();
  case SIGNER_FEATURE_RISK_VIEW_ONLY:
  default:
    return highlight_color();
  }
}

static bool use_two_column_cards(const signer_feature_t *feature) {
  return theme_get_screen_width() >= 420 &&
         signer_feature_child_count(feature->id) > 2;
}

static lv_obj_t *create_text(lv_obj_t *parent, const char *text, bool medium,
                             lv_color_t color) {
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, ui_i18n_text(text));
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(label, LV_PCT(100));
  lv_obj_set_style_text_font(label, medium ? theme_font_medium()
                                           : theme_font_small(),
                             0);
  lv_obj_set_style_text_color(label, color, 0);
  lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(label, 0, 0);
  return label;
}

static lv_obj_t *create_panel(lv_obj_t *parent, lv_color_t border_color,
                              int pad) {
  (void)border_color;
  lv_obj_t *panel = lv_obj_create(parent);
  lv_obj_set_width(panel, LV_PCT(100));
  lv_obj_set_height(panel, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(panel, bg_color(), 0);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(panel, highlight_color(), 0);
  lv_obj_set_style_border_width(panel, 2, 0);
  lv_obj_set_style_radius(panel, 8, 0);
  lv_obj_set_style_shadow_width(panel, 0, 0);
  lv_obj_set_style_pad_all(panel, pad, 0);
  lv_obj_set_style_pad_gap(panel, theme_get_small_padding(), 0);
  lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);
  return panel;
}

static lv_obj_t *create_chip(lv_obj_t *parent, const char *text,
                             lv_color_t color) {
  (void)color;
  lv_obj_t *chip = lv_label_create(parent);
  lv_label_set_text(chip, text ? text : "");
  lv_label_set_long_mode(chip, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_font(chip, theme_font_small(), 0);
  lv_obj_set_style_text_color(chip, main_color(), 0);
  lv_obj_set_style_bg_color(chip, bg_color(), 0);
  lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(chip, highlight_color(), 0);
  lv_obj_set_style_border_width(chip, 1, 0);
  lv_obj_set_style_radius(chip, 8, 0);
  lv_obj_set_style_pad_left(chip, 8, 0);
  lv_obj_set_style_pad_right(chip, 8, 0);
  lv_obj_set_style_pad_top(chip, 4, 0);
  lv_obj_set_style_pad_bottom(chip, 4, 0);
  return chip;
}

static void legacy_wallet_return_to_signer_cb(void);
static void legacy_wallet_return_to_home_cb(void);
static void legacy_wallet_launch_load(void);
static void legacy_wallet_launch_home(void);
static void legacy_wallet_launch_slots(void);
static void legacy_wallet_launch_scan_for_shell(void);
static void legacy_wallet_launch_unified_scan_for_shell(void);
static const char *legacy_wallet_route_for_target(const char *target_id);
static bool is_web3_wallet_choice(const char *id);
static bool is_btc_wallet_choice(const char *id);
static bool target_requires_signing_key(const char *id);
static bool target_requires_shell_gate(const char *id);
static void web3_qr_state_reset(void);
#ifndef SIMULATOR
static void smartcard_web3_scan_event_cb(lv_event_t *event);
#endif
static void shell_transient_state_reset(void);
static void satochip_maint_input_cleanup(void);
static bool satochip_legacy_seedkeeper_id(const char *id);
static bool smartcard_task_busy(void);
static void smartcard_probe_start(lv_obj_t *label);
static void satochip_status_start(lv_obj_t *label);
static lv_obj_t *satochip_maint_attach_extra_field(lv_obj_t *parent,
                                                   const char *label_text,
                                                   const char *placeholder,
                                                   const char *default_text,
                                                   bool password_mode,
                                                   bool multiline,
                                                   size_t max_len,
                                                   lv_obj_t **slot);
static void create_signer_header(lv_obj_t *root, const signer_feature_t *feature);
static lv_obj_t *create_signer_list(lv_obj_t *root, bool center_items,
                                    bool grid_mode);
static void create_signer_child_menu(lv_obj_t *list,
                                   const signer_feature_t *feature);
static const signer_feature_t *shell_feature_find(const char *id);
#ifdef SIMULATOR
static bool simulator_launch_custom_derivation(void);
#endif

#ifdef SIMULATOR
static void simulator_custom_derivation_return_cb(void) {
  custom_derivation_page_destroy();
  (void)signer_shell_show_screen("pi_connect_wallet");
}

static void simulator_custom_derivation_import_cb(void) {
  custom_derivation_page_destroy();
  (void)signer_shell_show_screen("load_mnemonic");
}

static void simulator_custom_derivation_smartcard_cb(void) {
  custom_derivation_page_destroy();
  (void)signer_shell_show_screen("connect_wallet_satochip_address");
}

static bool simulator_launch_custom_derivation(void) {
  if (!s_parent)
    s_parent = lv_screen_active();
  shell_transient_state_reset();
  lv_obj_clean(s_parent);
  s_current_screen_id = "custom_derivation";
  custom_derivation_page_create_with_import(
      s_parent, simulator_custom_derivation_return_cb,
      simulator_custom_derivation_import_cb,
      simulator_custom_derivation_smartcard_cb);
  custom_derivation_page_show();
  return true;
}

#endif

#ifndef SIMULATOR
static bool wallet_ready_for_legacy_pages(void) {
  return key_is_loaded() && wallet_is_initialized();
}

static void legacy_wallet_intermediate_blocked_cb(void) {
  (void)signer_shell_show_screen("pi_loaded_mnemonic");
}

static bool legacy_wallet_require_signing_key(void) {
  if (key_has_signing_key() && wallet_is_initialized())
    return true;
  if (!key_is_loaded())
    legacy_wallet_launch_load();
  else
    dialog_show_error("Temporary mnemonic cannot be used for signing or address derivation.",
                      legacy_wallet_intermediate_blocked_cb, 0);
  return false;
}

static bool legacy_wallet_require_valid_mnemonic(void) {
  if (key_mnemonic_is_valid())
    return true;
  if (!key_is_loaded())
    legacy_wallet_launch_load();
  else
    dialog_show_error("Temporary mnemonic cannot be used for signing or address derivation.",
                      legacy_wallet_intermediate_blocked_cb, 0);
  return false;
}

static lv_obj_t *legacy_wallet_prepare_root(void) {
  if (!s_parent)
    s_parent = lv_screen_active();
  shell_transient_state_reset();
  lv_obj_clean(s_parent);
  return s_parent;
}

static void legacy_wallet_return_to_signer_cb(void) {
  (void)signer_shell_show_screen("home");
}

static void legacy_wallet_launch_login(void) {
  lv_obj_t *root = legacy_wallet_prepare_root();
  login_page_create(root);
  login_page_show();
}

static void legacy_wallet_launch_load(void) {
  lv_obj_t *root = legacy_wallet_prepare_root();
  load_menu_page_create(root, legacy_wallet_return_to_signer_cb);
  load_menu_page_show();
}

static void legacy_wallet_launch_new(void) {
  lv_obj_t *root = legacy_wallet_prepare_root();
  new_mnemonic_menu_page_create(root, legacy_wallet_return_to_signer_cb);
  new_mnemonic_menu_page_show();
}

static void shell_return_from_key_confirmation_cb(void) {
  key_confirmation_page_destroy();
  (void)signer_shell_show_screen("pi_mnemonic_tools");
}

static void shell_success_from_key_confirmation_cb(void) {
  key_confirmation_page_destroy();
  (void)signer_shell_show_screen("home");
}

static void shell_return_from_mnemonic_editor_cb(void) {
  mnemonic_editor_page_destroy();
  (void)signer_shell_show_screen("new_mnemonic");
}

static void shell_return_from_load_mnemonic_editor_cb(void) {
  mnemonic_editor_page_destroy();
  (void)signer_shell_show_screen("load_mnemonic");
}

static void shell_return_from_load_qr_cb(void) {
  qr_scanner_page_destroy();
  (void)signer_shell_show_screen("load_mnemonic");
}

static void shell_return_from_load_storage_cb(void) {
  load_storage_page_destroy();
  (void)signer_shell_show_screen("load_mnemonic");
}

static void shell_return_from_manual_input_cb(void) {
  manual_input_page_destroy();
  (void)signer_shell_show_screen("new_mnemonic");
}

static void shell_return_from_load_manual_input_cb(void) {
  manual_input_page_destroy();
  (void)signer_shell_show_screen("load_mnemonic");
}

static void shell_success_from_manual_input_cb(void) {
  key_confirmation_page_destroy();
  manual_input_page_destroy();
  (void)signer_shell_show_screen("home");
}

static void shell_show_generated_mnemonic(char *mnemonic) {
  if (!mnemonic) {
    (void)signer_shell_show_screen("new_mnemonic");
    return;
  }

  lv_obj_t *root = legacy_wallet_prepare_root();
  mnemonic_editor_page_create(root, shell_return_from_mnemonic_editor_cb,
                              shell_success_from_key_confirmation_cb, mnemonic,
                              true);
  mnemonic_editor_page_show();
  SECURE_FREE_STRING(mnemonic);
}

static void shell_return_from_entropy_camera_cb(void) {
  char *mnemonic = entropy_from_camera_get_completed_mnemonic();
  entropy_from_camera_page_destroy();
  shell_show_generated_mnemonic(mnemonic);
}

static void shell_return_from_mnemonic_tool_cb(void) {
  char *mnemonic = mnemonic_tool_page_get_completed_mnemonic();
  mnemonic_tool_page_destroy();
  shell_show_generated_mnemonic(mnemonic);
}

static void shell_return_from_load_mnemonic_tool_cb(void) {
  char *mnemonic = mnemonic_tool_page_get_completed_mnemonic();
  mnemonic_tool_page_destroy();
  if (mnemonic) {
    lv_obj_t *root = legacy_wallet_prepare_root();
    mnemonic_editor_page_create(root, shell_return_from_load_mnemonic_editor_cb,
                                shell_success_from_key_confirmation_cb,
                                mnemonic, false);
    mnemonic_editor_page_show();
    SECURE_FREE_STRING(mnemonic);
  } else {
    (void)signer_shell_show_screen("load_mnemonic");
  }
}

static void shell_return_from_bip39_check_cb(void) {
  bip39_check_page_destroy();
  (void)signer_shell_show_screen("pi_mnemonic_tools");
}

static void shell_return_from_punch_grid_cb(void) {
  load_punch_grid_page_destroy();
  (void)signer_shell_show_screen("load_mnemonic");
}

static void shell_return_from_tinyseed_restore_cb(void) {
  tinyseed_restore_page_destroy();
  (void)signer_shell_show_screen("load_punch_grid");
}

static void shell_return_from_stackbit_restore_cb(void) {
  stackbit_restore_page_destroy();
  (void)signer_shell_show_screen("load_punch_grid");
}

static void shell_return_from_custom_derivation_cb(void) {
  custom_derivation_page_destroy();
  (void)signer_shell_show_screen(s_custom_derivation_return_id[0]
                                   ? s_custom_derivation_return_id
                                   : "pi_connect_wallet");
}

static void shell_import_from_custom_derivation_cb(void) {
  custom_derivation_page_destroy();
  legacy_wallet_launch_load();
}

static void shell_custom_derivation_smartcard_cb(void) {
  custom_derivation_page_destroy();
  (void)signer_shell_show_screen("connect_wallet_satochip_address");
}

static void legacy_wallet_launch_words(void) {
  lv_obj_t *root = legacy_wallet_prepare_root();
  manual_input_page_create(root, shell_return_from_manual_input_cb,
                           shell_success_from_manual_input_cb, true);
  manual_input_page_show();
}

static void legacy_wallet_launch_camera_entropy(void) {
  lv_obj_t *root = legacy_wallet_prepare_root();
  entropy_from_camera_page_create(root, shell_return_from_entropy_camera_cb);
  entropy_from_camera_page_show();
}

static void legacy_wallet_launch_mnemonic_tool(mnemonic_tool_mode_t mode) {
  if (mode == MNEMONIC_TOOL_BIP85_MNEMONIC &&
      !legacy_wallet_require_signing_key()) {
    return;
  }

  lv_obj_t *root = legacy_wallet_prepare_root();
  mnemonic_tool_page_create(root, shell_return_from_mnemonic_tool_cb, mode);
  mnemonic_tool_page_show();
}

static void legacy_wallet_launch_bip39_check(void) {
  lv_obj_t *root = legacy_wallet_prepare_root();
  bip39_check_page_create(root, shell_return_from_bip39_check_cb);
  bip39_check_page_show();
}

static void legacy_wallet_return_to_home_cb(void) {
  legacy_wallet_launch_home();
}

static void legacy_wallet_launch_home(void) {
  if (!legacy_wallet_require_signing_key()) {
    return;
  }

  lv_obj_t *root = legacy_wallet_prepare_root();
  home_page_create(root);
  home_page_show();
}

static void legacy_wallet_launch_public_key(void) {
  if (!legacy_wallet_require_signing_key()) {
    return;
  }

  lv_obj_t *root = legacy_wallet_prepare_root();
  public_key_page_create(root, legacy_wallet_return_to_home_cb);
  public_key_page_show();
}

static void legacy_wallet_launch_public_key_mode(
    public_key_export_mode_t mode) {
  if (!legacy_wallet_require_signing_key()) {
    return;
  }

  lv_obj_t *root = legacy_wallet_prepare_root();
  public_key_page_create_with_mode(root, legacy_wallet_return_to_home_cb, mode);
  public_key_page_show();
}

static void legacy_wallet_launch_addresses(void) {
  if (!legacy_wallet_require_signing_key()) {
    return;
  }

  lv_obj_t *root = legacy_wallet_prepare_root();
  addresses_page_create(root, legacy_wallet_return_to_home_cb);
  addresses_page_show();
}

static void legacy_wallet_launch_backup(void) {
  if (!legacy_wallet_require_valid_mnemonic()) {
    return;
  }

  lv_obj_t *root = legacy_wallet_prepare_root();
  backup_menu_page_create(root, legacy_wallet_return_to_home_cb);
  backup_menu_page_show();
}

static void shell_return_from_backup_words_cb(void) {
  mnemonic_words_page_destroy();
  (void)signer_shell_show_screen("backup_export");
}

static void shell_return_from_backup_entropy_cb(void) {
  mnemonic_entropy_page_destroy();
  (void)signer_shell_show_screen("backup_export");
}

static void shell_return_from_backup_grid_cb(void) {
  mnemonic_grid_page_destroy();
  (void)signer_shell_show_screen("backup_export");
}

static void shell_return_from_backup_steel_cb(void) {
  mnemonic_steel_page_destroy();
  (void)signer_shell_show_screen("backup_export");
}

static void shell_return_from_backup_1248_cb(void) {
  mnemonic_1248_page_destroy();
  (void)signer_shell_show_screen("backup_export");
}

static void shell_return_from_backup_qr_cb(void) {
  mnemonic_qr_page_destroy();
  (void)signer_shell_show_screen("backup_export");
}

static void shell_return_from_backup_kef_cb(void) {
  store_mnemonic_page_destroy();
  (void)signer_shell_show_screen("backup_export");
}

static void legacy_wallet_launch_scan(void) {
  if (!legacy_wallet_require_signing_key()) {
    return;
  }

  lv_obj_t *root = legacy_wallet_prepare_root();
  scan_page_create(root, legacy_wallet_return_to_home_cb);
  scan_page_show();
}

static void legacy_wallet_launch_scan_for_shell(void) {
  if (!legacy_wallet_require_signing_key()) {
    return;
  }

  lv_obj_t *root = legacy_wallet_prepare_root();
  scan_page_create(root, legacy_wallet_return_to_signer_cb);
  scan_page_show();
}

static void legacy_wallet_return_from_slots_cb(void) {
  mnemonic_slots_page_destroy();
  (void)signer_shell_show_screen("pi_mnemonic_tools");
}

static void legacy_wallet_success_from_slots_cb(void) {
  mnemonic_slots_page_destroy();
  lv_obj_t *root = legacy_wallet_prepare_root();
  loaded_mnemonic_menu_page_create(root, legacy_wallet_return_from_slots_cb);
  loaded_mnemonic_menu_page_show();
}

static void legacy_wallet_scan_success_from_slots_cb(void) {
  mnemonic_slots_page_destroy();
  legacy_wallet_launch_scan_for_shell();
}

static void legacy_wallet_cancel_direct_sensitive(void);

static void legacy_wallet_launch_scan_with_slot_choice(void) {
  if (!legacy_wallet_require_signing_key()) {
    return;
  }

  (void)mnemonic_slots_add_current(NULL);
  if (mnemonic_slots_count() > 1) {
    lv_obj_t *root = legacy_wallet_prepare_root();
    mnemonic_slots_page_create(root, legacy_wallet_cancel_direct_sensitive,
                               legacy_wallet_scan_success_from_slots_cb);
    mnemonic_slots_page_show();
    return;
  }

  legacy_wallet_launch_scan_for_shell();
}

static void legacy_wallet_launch_slots(void) {
  if (!key_is_loaded()) {
    legacy_wallet_launch_load();
    return;
  }

  (void)mnemonic_slots_add_current(NULL);
  lv_obj_t *root = legacy_wallet_prepare_root();
  mnemonic_slots_page_create(root, legacy_wallet_return_from_slots_cb,
                             legacy_wallet_success_from_slots_cb);
  mnemonic_slots_page_show();
}

static void legacy_wallet_return_from_settings_cb(void) {
  wallet_settings_page_destroy();
  legacy_wallet_launch_home();
}

static void legacy_wallet_launch_settings(void) {
  if (!legacy_wallet_require_signing_key()) {
    return;
  }

  lv_obj_t *root = legacy_wallet_prepare_root();
  wallet_settings_page_create(root, legacy_wallet_return_from_settings_cb);
  wallet_settings_page_show();
}

static void legacy_system_return_from_pin_settings_cb(void) {
  login_settings_page_destroy();
  (void)signer_shell_show_screen("settings");
}

static void legacy_system_launch_pin_settings(void) {
  lv_obj_t *root = legacy_wallet_prepare_root();
  login_settings_page_create(root, legacy_system_return_from_pin_settings_cb);
  login_settings_page_show();
}

static void legacy_wallet_passphrase_done_cb(void *user_data) {
  (void)user_data;
  (void)signer_shell_show_screen("pi_mnemonic_tools");
}

static void legacy_wallet_passphrase_error_cb(void) {
  legacy_wallet_passphrase_done_cb(NULL);
}

static void legacy_wallet_return_from_passphrase_cb(void) {
  passphrase_page_destroy();
  (void)signer_shell_show_screen("pi_mnemonic_tools");
}

static void legacy_wallet_passphrase_success_cb(const char *passphrase) {
  char *mnemonic = NULL;
  if (!key_get_mnemonic(&mnemonic)) {
    passphrase_page_destroy();
    dialog_show_error("Load a mnemonic first", legacy_wallet_passphrase_error_cb, 0);
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
    dialog_show_error("Failed to apply passphrase", legacy_wallet_passphrase_error_cb, 0);
    return;
  }

  (void)mnemonic_slots_add_current(NULL);
  dialog_show_info("Passphrase applied", "Wallet fingerprint updated",
                   legacy_wallet_passphrase_done_cb, NULL,
                   DIALOG_STYLE_FULLSCREEN);
}

static void legacy_wallet_launch_passphrase_unlocked(void) {
  if (!legacy_wallet_require_valid_mnemonic()) {
    return;
  }

  lv_obj_t *root = legacy_wallet_prepare_root();
  passphrase_page_create(root, legacy_wallet_return_from_passphrase_cb,
                         legacy_wallet_passphrase_success_cb);
  passphrase_page_show();
}

static void legacy_wallet_return_from_descriptor_cb(void) {
  descriptor_manager_page_destroy();
  legacy_wallet_launch_home();
}

static void legacy_wallet_launch_descriptor(void) {
  if (!legacy_wallet_require_signing_key()) {
    return;
  }

  lv_obj_t *root = legacy_wallet_prepare_root();
  descriptor_manager_page_create(root, legacy_wallet_return_from_descriptor_cb);
  descriptor_manager_page_show();
}

static void legacy_wallet_launch_logout(void) {
  mnemonic_slots_clear_all();
  key_unload();
  wallet_unload();
  legacy_wallet_launch_login();
}
#else
static void legacy_wallet_return_to_signer_cb(void) {
  (void)signer_shell_show_screen("home");
}

static void legacy_wallet_return_to_home_cb(void) {
  (void)signer_shell_show_screen("wallet_home");
}

static void legacy_wallet_launch_home(void) {
  (void)signer_shell_show_screen("wallet_home");
}

static void legacy_wallet_launch_login(void) {
  (void)signer_shell_show_screen("wallet_home");
}

static void legacy_wallet_launch_load(void) {
  (void)signer_shell_show_screen("load_mnemonic");
}

static void legacy_wallet_launch_new(void) {
  (void)signer_shell_show_screen("new_mnemonic");
}

static void legacy_wallet_launch_public_key(void) {
  (void)signer_shell_show_screen("btc_bluewallet_zpub");
}

static void legacy_wallet_launch_addresses(void) {
  (void)signer_shell_show_screen("addresses");
}

static void legacy_wallet_launch_backup(void) {
  (void)signer_shell_show_screen("backup_export");
}

static void legacy_wallet_launch_scan(void) {
  (void)signer_shell_show_screen("sign_psbt_qr");
}

static void legacy_wallet_launch_scan_for_shell(void) {
  (void)signer_shell_show_screen("sign_psbt_qr");
}

static void legacy_wallet_launch_slots(void) {
  (void)signer_shell_show_screen("wallet_home");
}

static void legacy_wallet_launch_settings(void) {
  (void)signer_shell_show_screen("settings_wallet");
}

static void legacy_system_launch_pin_settings(void) {
  (void)signer_shell_show_screen("settings_pin");
}

static void legacy_wallet_launch_descriptor(void) {
  (void)signer_shell_show_screen("wallet_descriptor");
}

static void legacy_wallet_launch_logout(void) {
  (void)signer_shell_show_screen("boot_login");
}
#endif

static bool target_is_any(const char *target_id, const char *const *ids,
                          size_t count) {
  if (!target_id)
    return false;
  for (size_t i = 0; i < count; i++) {
    if (strcmp(target_id, ids[i]) == 0)
      return true;
  }
  return false;
}

#ifndef SIMULATOR
static mnemonic_tool_mode_t s_pending_direct_tool_mode =
    MNEMONIC_TOOL_INDEX_IMPORT;
typedef void (*legacy_backup_launch_cb_t)(void);
static legacy_backup_launch_cb_t s_pending_backup_launch = NULL;
static public_key_export_mode_t s_pending_public_key_mode =
    PUBLIC_KEY_EXPORT_STANDARD;
static const char *s_pending_sensitive_screen = NULL;
static char s_pending_mnemonic_screen[40];

static bool legacy_wallet_needs_mnemonic_slot_choice(const char *id) {
  return id && (strcmp(id, "web3_okx_mnemonic") == 0 ||
                strcmp(id, "web3_bitget_mnemonic") == 0 ||
                strcmp(id, "web3_metamask_mnemonic") == 0 ||
                strcmp(id, "web3_rabby_mnemonic") == 0 ||
                strcmp(id, "web3_tokenpocket_mnemonic") == 0 ||
                strcmp(id, "web3_imtoken_mnemonic") == 0 ||
                strcmp(id, "web3_address_mnemonic") == 0 ||
                strcmp(id, "smartcard_satochip_write_mnemonic") == 0 ||
                strcmp(id, "smartcard_seedkeeper_write_mnemonic") == 0 ||
                strcmp(id, "btc_mnemonic") == 0 ||
                strcmp(id, "btc_bluewallet_zpub") == 0 ||
                strcmp(id, "btc_bluewallet_xpub") == 0 ||
                signer_sign_mnemonic_target_id(id));
}

static void legacy_wallet_resume_pending_mnemonic_screen(void) {
  char target[sizeof(s_pending_mnemonic_screen)];
  snprintf(target, sizeof(target), "%s", s_pending_mnemonic_screen);
  s_pending_mnemonic_screen[0] = '\0';
  mnemonic_slots_page_destroy();
  if (target[0] == '\0')
    return;

  s_allow_sensitive_render = true;
  (void)signer_shell_show_screen(target);
  s_allow_sensitive_render = false;
}

static void legacy_wallet_cancel_direct_sensitive(void) {
  s_pending_backup_launch = NULL;
  s_pending_sensitive_screen = NULL;
  s_pending_mnemonic_screen[0] = '\0';
  s_pending_public_key_mode = PUBLIC_KEY_EXPORT_STANDARD;
  (void)signer_shell_show_screen(s_current_screen_id ? s_current_screen_id
                                                   : "home");
}

static void legacy_wallet_launch_pending_direct_tool(void) {
  legacy_wallet_launch_mnemonic_tool(s_pending_direct_tool_mode);
}

static void legacy_wallet_launch_secondary_tool_unlocked(void) {
  legacy_wallet_launch_mnemonic_tool(MNEMONIC_TOOL_SECONDARY_SHIFT);
}

static void legacy_wallet_launch_pending_public_key_mode(void) {
  public_key_export_mode_t mode = s_pending_public_key_mode;
  s_pending_public_key_mode = PUBLIC_KEY_EXPORT_STANDARD;
  legacy_wallet_launch_public_key_mode(mode);
}

static void legacy_wallet_launch_custom_derivation_unlocked(void) {
  snprintf(s_custom_derivation_return_id, sizeof(s_custom_derivation_return_id),
           "%s", s_current_screen_id ? s_current_screen_id : "pi_connect_wallet");
  lv_obj_t *root = legacy_wallet_prepare_root();
  custom_derivation_page_create_with_import(
      root, shell_return_from_custom_derivation_cb,
      shell_import_from_custom_derivation_cb,
      shell_custom_derivation_smartcard_cb);
  custom_derivation_page_show();
}

static void legacy_wallet_show_pending_sensitive_screen(void) {
  const char *target = s_pending_sensitive_screen;
  s_pending_sensitive_screen = NULL;
  if (target) {
    if (legacy_wallet_needs_mnemonic_slot_choice(target)) {
      (void)mnemonic_slots_add_current(NULL);
      if (mnemonic_slots_count() > 1) {
        snprintf(s_pending_mnemonic_screen, sizeof(s_pending_mnemonic_screen),
                 "%s", target);
        lv_obj_t *root = legacy_wallet_prepare_root();
        mnemonic_slots_page_create(root,
                                   legacy_wallet_cancel_direct_sensitive,
                                   legacy_wallet_resume_pending_mnemonic_screen);
        mnemonic_slots_page_show();
        return;
      }
    }
    s_allow_sensitive_render = true;
    (void)signer_shell_show_screen(target);
    s_allow_sensitive_render = false;
  }
}

static void legacy_wallet_launch_backup_words_direct(void) {
  lv_obj_t *root = legacy_wallet_prepare_root();
  mnemonic_words_page_create(root, shell_return_from_backup_words_cb);
  mnemonic_words_page_show();
}

static void legacy_wallet_launch_backup_entropy_direct(void) {
  lv_obj_t *root = legacy_wallet_prepare_root();
  mnemonic_entropy_page_create(root, shell_return_from_backup_entropy_cb);
  mnemonic_entropy_page_show();
}

static void legacy_wallet_launch_backup_steel_direct(void) {
  lv_obj_t *root = legacy_wallet_prepare_root();
  mnemonic_steel_page_create(root, shell_return_from_backup_steel_cb);
  mnemonic_steel_page_show();
}

static void legacy_wallet_launch_backup_grid_direct(void) {
  lv_obj_t *root = legacy_wallet_prepare_root();
  mnemonic_grid_page_create(root, shell_return_from_backup_grid_cb);
  mnemonic_grid_page_show();
}

static void legacy_wallet_launch_backup_1248_direct(void) {
  lv_obj_t *root = legacy_wallet_prepare_root();
  mnemonic_1248_page_create(root, shell_return_from_backup_1248_cb);
  mnemonic_1248_page_show();
}

static void legacy_wallet_launch_backup_qr_direct(void) {
  lv_obj_t *root = legacy_wallet_prepare_root();
  mnemonic_qr_page_create(root, shell_return_from_backup_qr_cb);
  mnemonic_qr_page_show();
}

static void legacy_wallet_launch_backup_kef_direct(void) {
  lv_obj_t *root = legacy_wallet_prepare_root();
  store_mnemonic_page_create(root, shell_return_from_backup_kef_cb, STORAGE_SD);
  store_mnemonic_page_show();
}

static void legacy_wallet_run_pending_backup_launch(void) {
  legacy_backup_launch_cb_t launch = s_pending_backup_launch;
  s_pending_backup_launch = NULL;
  if (launch)
    launch();
}

static void legacy_wallet_backup_warn_cb(bool confirmed, void *user_data) {
  (void)user_data;
  if (!confirmed) {
    legacy_wallet_cancel_direct_sensitive();
    return;
  }
  legacy_wallet_run_pending_backup_launch();
}

static void legacy_wallet_confirm_pending_backup_launch(void) {
  dialog_show_danger_confirm(DIALOG_SENSITIVE_DATA_WARNING,
                             legacy_wallet_backup_warn_cb, NULL,
                             DIALOG_STYLE_OVERLAY);
}

static bool legacy_wallet_require_backup_direct(
    legacy_backup_launch_cb_t launch) {
  if (!launch)
    return true;
  if (!legacy_wallet_require_valid_mnemonic())
    return true;

  s_pending_backup_launch = launch;
  sensitive_pin_require(legacy_wallet_confirm_pending_backup_launch,
                        legacy_wallet_cancel_direct_sensitive);
  return true;
}
#endif

static bool legacy_wallet_handle_direct_target(const char *target_id) {
  if (!target_id)
    return false;

#ifndef SIMULATOR
  if (strcmp(target_id, "new_dice_d6") == 0) {
    legacy_wallet_launch_mnemonic_tool(MNEMONIC_TOOL_D6_ENTROPY);
    return true;
  }
  if (strcmp(target_id, "new_coin_entropy") == 0) {
    legacy_wallet_launch_mnemonic_tool(MNEMONIC_TOOL_BINARY_ENTROPY);
    return true;
  }
  if (strcmp(target_id, "new_words_select") == 0) {
    legacy_wallet_launch_words();
    return true;
  }
  if (strcmp(target_id, "new_camera_entropy") == 0) {
    legacy_wallet_launch_camera_entropy();
    return true;
  }
  if (strcmp(target_id, "new_hex_entropy") == 0) {
    legacy_wallet_launch_mnemonic_tool(MNEMONIC_TOOL_HEX_ENTROPY);
    return true;
  }
  if (strcmp(target_id, "new_cards_entropy") == 0) {
    legacy_wallet_launch_mnemonic_tool(MNEMONIC_TOOL_CARD_ENTROPY);
    return true;
  }
  if (strcmp(target_id, "new_dice_d20") == 0) {
    legacy_wallet_launch_mnemonic_tool(MNEMONIC_TOOL_D20_ENTROPY);
    return true;
  }
  if (strcmp(target_id, "load_steel_restore") == 0) {
    legacy_wallet_launch_mnemonic_tool(MNEMONIC_TOOL_STEEL_RESTORE);
    return true;
  }
  if (strcmp(target_id, "load_camera") == 0) {
    lv_obj_t *root = legacy_wallet_prepare_root();
    qr_scanner_page_create(root, shell_return_from_load_qr_cb);
    qr_scanner_page_show();
    return true;
  }
  if (strcmp(target_id, "load_manual") == 0) {
    lv_obj_t *root = legacy_wallet_prepare_root();
    manual_input_page_create(root, shell_return_from_load_manual_input_cb,
                             shell_success_from_manual_input_cb, false);
    manual_input_page_show();
    return true;
  }
  if (strcmp(target_id, "load_digits") == 0) {
    lv_obj_t *root = legacy_wallet_prepare_root();
    mnemonic_tool_page_create(root, shell_return_from_load_mnemonic_tool_cb,
                              MNEMONIC_TOOL_INDEX_IMPORT);
    mnemonic_tool_page_show();
    return true;
  }
  if (strcmp(target_id, "load_punch_grid") == 0) {
    lv_obj_t *root = legacy_wallet_prepare_root();
    load_punch_grid_page_create(root, shell_return_from_punch_grid_cb,
                                shell_success_from_key_confirmation_cb);
    load_punch_grid_page_show();
    return true;
  }
  if (strcmp(target_id, "load_tinyseed_restore") == 0) {
    lv_obj_t *root = legacy_wallet_prepare_root();
    tinyseed_restore_page_create(root, shell_return_from_tinyseed_restore_cb,
                                 shell_success_from_key_confirmation_cb);
    tinyseed_restore_page_show();
    return true;
  }
  if (strcmp(target_id, "load_stackbit_restore") == 0) {
    lv_obj_t *root = legacy_wallet_prepare_root();
    stackbit_restore_page_create(root, shell_return_from_stackbit_restore_cb,
                                 shell_success_from_key_confirmation_cb);
    stackbit_restore_page_show();
    return true;
  }
  if (strcmp(target_id, "load_sd") == 0) {
    lv_obj_t *root = legacy_wallet_prepare_root();
    load_storage_page_create(root, shell_return_from_load_storage_cb,
                             shell_success_from_key_confirmation_cb, STORAGE_SD);
    load_storage_page_show();
    return true;
  }
  if (strcmp(target_id, "bip39_check_tools") == 0) {
    legacy_wallet_launch_bip39_check();
    return true;
  }
  if (strcmp(target_id, "custom_derivation") == 0) {
    sensitive_pin_require(legacy_wallet_launch_custom_derivation_unlocked,
                          legacy_wallet_cancel_direct_sensitive);
    return true;
  }
  if (strcmp(target_id, "smartcard_satochip_write_mnemonic") == 0 ||
      strcmp(target_id, "smartcard_seedkeeper_write_mnemonic") == 0) {
    if (!legacy_wallet_require_valid_mnemonic())
      return true;
    s_pending_sensitive_screen = target_id;
    sensitive_pin_require(legacy_wallet_show_pending_sensitive_screen,
                          legacy_wallet_cancel_direct_sensitive);
    return true;
  }
  if (is_web3_wallet_choice(target_id)) {
    if (!legacy_wallet_require_signing_key())
      return true;
    s_pending_sensitive_screen = target_id;
    sensitive_pin_require(legacy_wallet_show_pending_sensitive_screen,
                          legacy_wallet_cancel_direct_sensitive);
    return true;
  }
  if (signer_sign_mnemonic_target_id(target_id)) {
    legacy_wallet_launch_unified_scan_for_shell();
    return true;
  }
  if (signer_sign_satochip_target_id(target_id)) {
    legacy_wallet_launch_unified_scan_for_shell();
    return true;
  }
  if (strcmp(target_id, "btc_bluewallet_zpub") == 0) {
    if (!legacy_wallet_require_signing_key())
      return true;
    s_pending_public_key_mode = PUBLIC_KEY_EXPORT_BLUEWALLET_ZPUB;
    sensitive_pin_require(legacy_wallet_launch_pending_public_key_mode,
                          legacy_wallet_cancel_direct_sensitive);
    return true;
  }
  if (strcmp(target_id, "btc_bluewallet_xpub") == 0) {
    if (!legacy_wallet_require_signing_key())
      return true;
    s_pending_public_key_mode = PUBLIC_KEY_EXPORT_BLUEWALLET_XPUB;
    sensitive_pin_require(legacy_wallet_launch_pending_public_key_mode,
                          legacy_wallet_cancel_direct_sensitive);
    return true;
  }
  if (strcmp(target_id, "backup_seed_words") == 0) {
    return legacy_wallet_require_backup_direct(
        legacy_wallet_launch_backup_words_direct);
  }
  if (strcmp(target_id, "backup_entropy") == 0) {
    return legacy_wallet_require_backup_direct(
        legacy_wallet_launch_backup_entropy_direct);
  }
  if (strcmp(target_id, "backup_steel_punch") == 0) {
    return legacy_wallet_require_backup_direct(
        legacy_wallet_launch_backup_steel_direct);
  }
  if (strcmp(target_id, "backup_grid") == 0) {
    return legacy_wallet_require_backup_direct(
        legacy_wallet_launch_backup_grid_direct);
  }
  if (strcmp(target_id, "backup_stackbit") == 0) {
    return legacy_wallet_require_backup_direct(
        legacy_wallet_launch_backup_1248_direct);
  }
  if (strcmp(target_id, "backup_seed_qr") == 0) {
    return legacy_wallet_require_backup_direct(
        legacy_wallet_launch_backup_qr_direct);
  }
  if (strcmp(target_id, "backup_kef") == 0) {
    return legacy_wallet_require_backup_direct(
        legacy_wallet_launch_backup_kef_direct);
  }
  if (strcmp(target_id, "tools_secondary_mnemonic") == 0) {
    if (!legacy_wallet_require_valid_mnemonic())
      return true;
    sensitive_pin_require(legacy_wallet_launch_secondary_tool_unlocked,
                          legacy_wallet_cancel_direct_sensitive);
    return true;
  }
  if (strcmp(target_id, "bip85") == 0 ||
      strcmp(target_id, "bip85_mnemonic") == 0) {
    if (!legacy_wallet_require_signing_key())
      return true;
    s_pending_direct_tool_mode = MNEMONIC_TOOL_BIP85_MNEMONIC;
    sensitive_pin_require(legacy_wallet_launch_pending_direct_tool,
                          legacy_wallet_cancel_direct_sensitive);
    return true;
  }
  if (strcmp(target_id, "login_passphrase") == 0) {
    if (!legacy_wallet_require_valid_mnemonic())
      return true;
    sensitive_pin_require(legacy_wallet_launch_passphrase_unlocked,
                          legacy_wallet_cancel_direct_sensitive);
    return true;
  }
#else
  if (strcmp(target_id, "bip85") == 0)
    return signer_shell_show_screen("bip85_mnemonic");
  if (strcmp(target_id, "load_camera") == 0 ||
      strcmp(target_id, "load_manual") == 0 ||
      strcmp(target_id, "load_digits") == 0 ||
      strcmp(target_id, "load_sd") == 0)
    return signer_shell_show_screen(target_id);
  if (strcmp(target_id, "new_dice_d6") == 0 ||
      strcmp(target_id, "new_coin_entropy") == 0 ||
      strcmp(target_id, "new_words_select") == 0 ||
      strcmp(target_id, "new_camera_entropy") == 0 ||
      strcmp(target_id, "new_hex_entropy") == 0 ||
      strcmp(target_id, "new_cards_entropy") == 0 ||
      strcmp(target_id, "new_dice_d20") == 0 ||
      strcmp(target_id, "load_steel_restore") == 0 ||
      strcmp(target_id, "load_punch_grid") == 0 ||
      strcmp(target_id, "load_tinyseed_restore") == 0 ||
      strcmp(target_id, "load_stackbit_restore") == 0 ||
      strcmp(target_id, "bip39_check_tools") == 0 ||
      strcmp(target_id, "custom_derivation") == 0 ||
      signer_sign_mnemonic_target_id(target_id) ||
      signer_sign_satochip_target_id(target_id) ||
      strcmp(target_id, "backup_seed_words") == 0 ||
      strcmp(target_id, "backup_entropy") == 0 ||
      strcmp(target_id, "backup_grid") == 0 ||
      strcmp(target_id, "backup_steel_punch") == 0 ||
      strcmp(target_id, "backup_stackbit") == 0 ||
      strcmp(target_id, "backup_seed_qr") == 0 ||
      strcmp(target_id, "backup_kef") == 0 ||
      strcmp(target_id, "tools_secondary_mnemonic") == 0 ||
      strcmp(target_id, "bip85_mnemonic") == 0 ||
      strcmp(target_id, "login_passphrase") == 0)
    return signer_shell_show_screen(target_id);
#endif

  return false;
}

static const char *legacy_wallet_route_for_target(const char *target_id) {
  if (!target_id)
    return NULL;

  static const char *const login_ids[] = {
      "legacy_login", "login_pin"};
  static const char *const load_ids[] = {
      "legacy_load_wallet", "load_camera",
      "load_sd",            "load_manual",       "load_digits",
      "load_encrypted_kef",
      "login_encrypted_backup"};
  static const char *const new_ids[] = {
      "legacy_new_wallet"};
  static const char *const home_ids[] = {
      "legacy_wallet_home", "wallet_dashboard"};
  static const char *const public_key_ids[] = {
      "legacy_public_key", "wallet_public_key", "export_public_data"};
  static const char *const descriptor_ids[] = {
      "wallet_descriptor"};
  static const char *const addresses_ids[] = {
      "legacy_addresses", "wallet_addresses", "addr_receive",
      "addr_change",     "addr_scan_check",   "addr_qr_view"};
  static const char *const backup_ids[] = {
      "legacy_backup_wallet", "wallet_backup",
      "backup_seed_words",    "backup_seed_qr",    "backup_kef",
      "backup_entropy",       "backup_steel_punch", "backup_stackbit",
      "backup_grid",
  };
  static const char *const scan_ids[] = {
      "legacy_scan_sign",
      "sign_psbt_qr",    "sign_psbt_review",
      "sign_psbt_export", "sign_message"};
  static const char *const slots_ids[] = {
      "legacy_select_mnemonic"};
  static const char *const settings_ids[] = {
      "settings_wallet"};
  static const char *const pin_settings_ids[] = {
      "settings_pin"};
  static const char *const logout_ids[] = {
      "login_clear_session"};

  if (target_is_any(target_id, login_ids,
                    sizeof(login_ids) / sizeof(login_ids[0])))
    return "legacy_login";
  if (target_is_any(target_id, load_ids,
                    sizeof(load_ids) / sizeof(load_ids[0])))
    return "legacy_load_wallet";
  if (target_is_any(target_id, new_ids, sizeof(new_ids) / sizeof(new_ids[0])))
    return "legacy_new_wallet";
  if (target_is_any(target_id, home_ids,
                    sizeof(home_ids) / sizeof(home_ids[0])))
    return "legacy_wallet_home";
  if (target_is_any(target_id, public_key_ids,
                    sizeof(public_key_ids) / sizeof(public_key_ids[0])))
    return "legacy_public_key";
  if (target_is_any(target_id, descriptor_ids,
                    sizeof(descriptor_ids) / sizeof(descriptor_ids[0])))
    return "legacy_descriptor";
  if (target_is_any(target_id, addresses_ids,
                    sizeof(addresses_ids) / sizeof(addresses_ids[0])))
    return "legacy_addresses";
  if (target_is_any(target_id, backup_ids,
                    sizeof(backup_ids) / sizeof(backup_ids[0])))
    return "legacy_backup_wallet";
  if (target_is_any(target_id, scan_ids,
                    sizeof(scan_ids) / sizeof(scan_ids[0])))
    return "legacy_scan_sign";
  if (target_is_any(target_id, slots_ids,
                    sizeof(slots_ids) / sizeof(slots_ids[0])))
    return "legacy_select_mnemonic";
  if (target_is_any(target_id, settings_ids,
                    sizeof(settings_ids) / sizeof(settings_ids[0])))
    return "legacy_wallet_settings";
  if (target_is_any(target_id, pin_settings_ids,
                    sizeof(pin_settings_ids) / sizeof(pin_settings_ids[0])))
    return "legacy_pin_settings";
  if (target_is_any(target_id, logout_ids,
                    sizeof(logout_ids) / sizeof(logout_ids[0])))
    return "legacy_logout";

  return NULL;
}

static const char *legacy_wallet_route_label(const char *route) {
  if (!route)
    return "Open Wallet Feature";
  if (strcmp(route, "legacy_login") == 0)
    return "Open Wallet";
  if (strcmp(route, "legacy_load_wallet") == 0)
    return "Import Wallet";
  if (strcmp(route, "legacy_new_wallet") == 0)
    return "Create Wallet";
  if (strcmp(route, "legacy_wallet_home") == 0)
    return "Open Wallet Home";
  if (strcmp(route, "legacy_public_key") == 0)
    return "View Public Key";
  if (strcmp(route, "legacy_descriptor") == 0)
    return "Manage Wallet Descriptor";
  if (strcmp(route, "legacy_addresses") == 0)
    return "View and Verify Addresses";
  if (strcmp(route, "legacy_backup_wallet") == 0)
    return "Back Up Mnemonic";
  if (strcmp(route, "legacy_scan_sign") == 0)
    return "Scan and Sign";
  if (strcmp(route, "legacy_select_mnemonic") == 0)
    return "Select Mnemonic";
  if (strcmp(route, "legacy_wallet_settings") == 0)
    return "Open Settings";
  if (strcmp(route, "legacy_pin_settings") == 0)
    return "Open PIN Settings";
  if (strcmp(route, "legacy_logout") == 0)
    return "Clear Current Session";
  return "Open Wallet Feature";
}

#ifndef SIMULATOR
static const char *s_pending_sensitive_route = NULL;

static void legacy_wallet_run_pending_sensitive_route(void) {
  const char *route = s_pending_sensitive_route;
  s_pending_sensitive_route = NULL;
  if (!route)
    return;

  if (strcmp(route, "legacy_login") == 0) {
    legacy_wallet_launch_login();
    return;
  }
  if (strcmp(route, "legacy_load_wallet") == 0) {
    legacy_wallet_launch_load();
    return;
  }
  if (strcmp(route, "legacy_new_wallet") == 0) {
    legacy_wallet_launch_new();
    return;
  }
  if (strcmp(route, "legacy_wallet_home") == 0) {
    legacy_wallet_launch_home();
    return;
  }
  if (strcmp(route, "legacy_public_key") == 0) {
    legacy_wallet_launch_public_key();
    return;
  }
  if (strcmp(route, "legacy_descriptor") == 0) {
    legacy_wallet_launch_descriptor();
    return;
  }
  if (strcmp(route, "legacy_addresses") == 0) {
    legacy_wallet_launch_addresses();
    return;
  }
  if (strcmp(route, "legacy_backup_wallet") == 0) {
    legacy_wallet_launch_backup();
    return;
  }
  if (strcmp(route, "legacy_scan_sign") == 0) {
    legacy_wallet_launch_unified_scan_for_shell();
    return;
  }
  if (strcmp(route, "legacy_select_mnemonic") == 0) {
    legacy_wallet_launch_slots();
    return;
  }
  if (strcmp(route, "legacy_wallet_settings") == 0) {
    legacy_wallet_launch_settings();
    return;
  }
  if (strcmp(route, "legacy_pin_settings") == 0) {
    legacy_system_launch_pin_settings();
    return;
  }
  if (strcmp(route, "legacy_logout") == 0) {
    legacy_wallet_launch_logout();
    return;
  }
}

static void legacy_wallet_cancel_pending_sensitive_route(void) {
  s_pending_sensitive_route = NULL;
  s_pending_mnemonic_screen[0] = '\0';
  (void)signer_shell_show_screen(s_current_screen_id ? s_current_screen_id
                                                   : "home");
}
#endif

static bool legacy_wallet_handle_target(const char *target_id) {
  if (legacy_wallet_handle_direct_target(target_id))
    return true;

  const char *route = legacy_wallet_route_for_target(target_id);
  if (!route)
    return false;

  if (strcmp(route, "legacy_load_wallet") == 0) {
    legacy_wallet_launch_load();
    return true;
  }
  if (strcmp(route, "legacy_new_wallet") == 0) {
    legacy_wallet_launch_new();
    return true;
  }

#ifndef SIMULATOR
  if (strcmp(route, "legacy_scan_sign") == 0) {
    legacy_wallet_launch_unified_scan_for_shell();
    return true;
  }

  s_pending_sensitive_route = route;
  sensitive_pin_require(legacy_wallet_run_pending_sensitive_route,
                        legacy_wallet_cancel_pending_sensitive_route);
  return true;
#else
  if (strcmp(route, "legacy_login") == 0) {
    legacy_wallet_launch_login();
    return true;
  }
  if (strcmp(route, "legacy_wallet_home") == 0) {
    legacy_wallet_launch_home();
    return true;
  }
  if (strcmp(route, "legacy_public_key") == 0) {
    legacy_wallet_launch_public_key();
    return true;
  }
  if (strcmp(route, "legacy_descriptor") == 0) {
    legacy_wallet_launch_descriptor();
    return true;
  }
  if (strcmp(route, "legacy_addresses") == 0) {
    legacy_wallet_launch_addresses();
    return true;
  }
  if (strcmp(route, "legacy_backup_wallet") == 0) {
    legacy_wallet_launch_backup();
    return true;
  }
  if (strcmp(route, "legacy_scan_sign") == 0) {
    legacy_wallet_launch_unified_scan_for_shell();
    return true;
  }
  if (strcmp(route, "legacy_select_mnemonic") == 0) {
    legacy_wallet_launch_slots();
    return true;
  }
  if (strcmp(route, "legacy_wallet_settings") == 0) {
    legacy_wallet_launch_settings();
    return true;
  }
  if (strcmp(route, "legacy_pin_settings") == 0) {
    legacy_system_launch_pin_settings();
    return true;
  }
  if (strcmp(route, "legacy_logout") == 0) {
    legacy_wallet_launch_logout();
    return true;
  }

  return false;
#endif
}

static lv_obj_t *create_row(lv_obj_t *parent) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_set_width(row, LV_PCT(100));
  lv_obj_set_height(row, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_style_pad_gap(row, 8, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  return row;
}

static void create_hero(lv_obj_t *root, const signer_feature_t *feature) {
  lv_obj_t *hero =
      create_panel(root,
                   feature->parent_id ? disabled_color() : highlight_color(),
                   max_i(14, theme_get_default_padding() / 2));

  lv_obj_t *top = create_row(hero);
  lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  char progress_text[80];
  if (!feature->parent_id) {
    snprintf(progress_text, sizeof(progress_text), "%zu features",
             signer_feature_action_count());
  } else {
    snprintf(progress_text, sizeof(progress_text), "%zu items",
             signer_feature_child_count(feature->id));
  }
  create_chip(top, progress_text, disabled_color());

  lv_obj_t *title = create_text(hero, shell_feature_title(feature), true,
                                main_color());
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_LEFT, 0);

  lv_obj_t *chips = create_row(hero);
  create_chip(chips, signer_feature_status_name(feature->status),
              status_color(feature->status));
  create_chip(chips, signer_feature_risk_name(feature->risk),
              risk_color(feature->risk));
}

static void nav_event_cb(lv_event_t *event) {
  const char *target_id = (const char *)lv_event_get_user_data(event);
  const char *alias_target = shell_alias_target_for_id(target_id);
  if (alias_target)
    target_id = alias_target;
  if (satochip_legacy_seedkeeper_id(target_id)) {
    (void)signer_shell_show_screen("smartcard_satochip_seedkeeper_tools");
    return;
  }
#ifndef SIMULATOR
  if (target_requires_signing_key(target_id) &&
      !legacy_wallet_require_signing_key())
    return;
#endif
  if (legacy_wallet_handle_target(target_id))
    return;
  (void)signer_shell_show_screen(target_id);
}

static lv_obj_t *create_menu_card(lv_obj_t *parent,
                                  const signer_feature_t *feature, bool primary,
                                  bool two_columns) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_width(btn, two_columns ? LV_PCT(48) : LV_PCT(100));
  lv_obj_set_height(btn, max_i(two_columns ? 102 : 86,
                               theme_get_min_touch_size()));
  theme_apply_touch_button(btn, primary);
  lv_obj_set_style_bg_color(btn, bg_color(), 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(btn, 12, 0);
  lv_obj_set_style_pad_gap(btn, 7, 0);
  lv_obj_set_style_radius(btn, 8, 0);
  lv_obj_set_style_border_width(btn, 2, 0);
  lv_obj_set_style_border_color(btn, highlight_color(), 0);
  lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);
  lv_obj_add_event_cb(btn, nav_event_cb, LV_EVENT_CLICKED,
                      (void *)feature->id);

  lv_obj_t *title_row = create_row(btn);

  lv_obj_t *btn_label = lv_label_create(title_row);
  lv_label_set_text(btn_label, shell_feature_title(feature));
  lv_obj_set_width(btn_label, LV_PCT(58));
  lv_label_set_long_mode(btn_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(btn_label, theme_font_medium(), 0);
  lv_obj_set_style_text_color(btn_label, main_color(), 0);

  create_chip(title_row, signer_feature_status_name(feature->status),
              status_color(feature->status));
  return btn;
}

static lv_obj_t *create_nav_button(lv_obj_t *parent, const char *label,
                                   const char *target_id, bool primary) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_width(btn, LV_PCT(100));
  lv_obj_set_height(btn, max_i(58, theme_get_min_touch_size() * 2 / 3));
  theme_apply_touch_button(btn, primary);
  lv_obj_set_style_bg_color(btn, bg_color(), 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(btn, 8, 0);
  lv_obj_set_style_border_width(btn, 2, 0);
  lv_obj_set_style_border_color(btn, highlight_color(), 0);
  if (target_id && target_id[0]) {
    lv_obj_add_event_cb(btn, nav_event_cb, LV_EVENT_CLICKED, (void *)target_id);
  } else {
    lv_obj_add_state(btn, LV_STATE_DISABLED);
  }

  lv_obj_t *btn_label = lv_label_create(btn);
  lv_label_set_text(btn_label, ui_i18n_text(label));
  lv_obj_set_style_text_font(btn_label, theme_font_medium(), 0);
  lv_obj_set_style_text_color(btn_label, main_color(), 0);
  lv_obj_center(btn_label);
  return btn;
}

static lv_obj_t *create_action_button(lv_obj_t *parent, const char *label,
                                      lv_event_cb_t cb, void *user_data,
                                      bool primary) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_width(btn, LV_PCT(100));
  lv_obj_set_height(btn, max_i(58, theme_get_min_touch_size() * 2 / 3));
  theme_apply_touch_button(btn, primary);
  lv_obj_set_style_bg_color(btn, bg_color(), 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(btn, 8, 0);
  lv_obj_set_style_border_width(btn, 2, 0);
  lv_obj_set_style_border_color(btn, highlight_color(), 0);
  if (cb) {
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
  } else {
    lv_obj_add_state(btn, LV_STATE_DISABLED);
  }

  lv_obj_t *btn_label = lv_label_create(btn);
  lv_label_set_text(btn_label, ui_i18n_text(label));
  lv_obj_set_style_text_font(btn_label, theme_font_medium(), 0);
  lv_obj_set_style_text_color(btn_label, main_color(), 0);
  lv_obj_center(btn_label);
  return btn;
}

static void style_slider(lv_obj_t *slider) {
  lv_obj_set_width(slider, LV_PCT(100));
  lv_obj_set_height(slider, 14);
  lv_obj_set_style_bg_color(slider, bg_color(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_color(slider, highlight_color(), LV_PART_MAIN);
  lv_obj_set_style_border_width(slider, 1, LV_PART_MAIN);
  lv_obj_set_style_bg_color(slider, highlight_color(), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(slider, main_color(), LV_PART_KNOB);
  lv_obj_set_style_pad_all(slider, 8, LV_PART_KNOB);
}

static void brightness_slider_event_cb(lv_event_t *event) {
  lv_obj_t *slider = lv_event_get_target(event);
  lv_obj_t *label = (lv_obj_t *)lv_event_get_user_data(event);
  int32_t val = lv_slider_get_value(slider);
  if (val < 10)
    val = 10;

  (void)bsp_display_brightness_set((int)val);
  (void)settings_set_brightness((uint8_t)val);

  if (label) {
    char text[160];
    snprintf(text, sizeof(text),
             i18n_tr_or("shell.brightness_percent_format", "Brightness: %ld%%"),
             (long)val);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, yes_color(), 0);
  }
}

static void create_display_settings_block(lv_obj_t *parent) {
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(12, theme_get_small_padding()));
  create_text(panel, "Brightness", false, highlight_color());

  uint8_t cur = settings_get_brightness();
  if (cur < 10)
    cur = 10;

  char status[128];
  snprintf(status, sizeof(status),
           i18n_tr_or("shell.current_brightness_format",
                      "Current brightness: %u%%"),
           (unsigned)cur);
  lv_obj_t *status_label = create_text(panel, status, true, main_color());

  lv_obj_t *slider = lv_slider_create(panel);
  lv_slider_set_range(slider, 10, 100);
  lv_slider_set_value(slider, cur, LV_ANIM_OFF);
  style_slider(slider);
  lv_obj_add_event_cb(slider, brightness_slider_event_cb,
                      LV_EVENT_VALUE_CHANGED, status_label);
}

static void ae_slider_event_cb(lv_event_t *event) {
  lv_obj_t *slider = lv_event_get_target(event);
  lv_obj_t *label = (lv_obj_t *)lv_event_get_user_data(event);
  int32_t val = lv_slider_get_value(slider);

  (void)settings_set_ae_target((uint8_t)val);
  if (label) {
    char text[96];
    snprintf(text, sizeof(text),
             i18n_tr_or("shell.exposure_target_format", "Exposure target: %u"),
             (unsigned)val);
    lv_label_set_text(label, text);
  }
}

static void focus_slider_event_cb(lv_event_t *event) {
  lv_obj_t *slider = lv_event_get_target(event);
  lv_obj_t *label = (lv_obj_t *)lv_event_get_user_data(event);
  int32_t val = lv_slider_get_value(slider);

  (void)settings_set_focus_position((uint16_t)val);
  if (label) {
    char text[96];
    snprintf(text, sizeof(text),
             i18n_tr_or("shell.focus_position_format", "Focus position: %u"),
             (unsigned)val);
    lv_label_set_text(label, text);
  }
}

static void create_camera_settings_block(lv_obj_t *parent) {
  lv_obj_t *panel =
      create_panel(parent, cyan_color(), max_i(12, theme_get_small_padding()));
  create_text(panel, "Scan Settings", false, highlight_color());

  char line[160];
  snprintf(line, sizeof(line),
           i18n_tr_or("shell.exposure_target_format", "Exposure target: %u"),
           (unsigned)settings_get_ae_target());
  lv_obj_t *ae_label = create_text(panel, line, false, main_color());

  lv_obj_t *ae_slider = lv_slider_create(panel);
  lv_slider_set_range(ae_slider, AE_TARGET_MIN, AE_TARGET_MAX);
  lv_slider_set_value(ae_slider, settings_get_ae_target(), LV_ANIM_OFF);
  style_slider(ae_slider);
  lv_obj_add_event_cb(ae_slider, ae_slider_event_cb, LV_EVENT_VALUE_CHANGED,
                      ae_label);

  snprintf(line, sizeof(line),
           i18n_tr_or("shell.focus_position_format", "Focus position: %u"),
           (unsigned)settings_get_focus_position());
  lv_obj_t *focus_label = create_text(panel, line, false, main_color());

  lv_obj_t *focus_slider = lv_slider_create(panel);
  lv_slider_set_range(focus_slider, 0, FOCUS_POSITION_MAX);
  lv_slider_set_value(focus_slider, settings_get_focus_position(), LV_ANIM_OFF);
  style_slider(focus_slider);
  lv_obj_add_event_cb(focus_slider, focus_slider_event_cb,
                      LV_EVENT_VALUE_CHANGED, focus_label);

}

static void locale_language_event_cb(lv_event_t *event) {
  i18n_language_t language =
      (i18n_language_t)(uintptr_t)lv_event_get_user_data(event);
  if (!i18n_language_valid(language))
    language = I18N_LANG_EN;

  esp_err_t err = settings_set_language(language);
  if (err != ESP_OK) {
    dialog_show_error(i18n_tr_or("dialog.save_failed", "Save failed"), NULL,
                      1600);
    return;
  }
  i18n_set_language(language);
  (void)signer_shell_show_screen("settings_locale");
}

static void create_locale_settings_block(lv_obj_t *parent) {
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(12, theme_get_small_padding()));
  create_text(panel, i18n_tr_or("settings.language", "Language"), false,
              highlight_color());

  const i18n_language_info_t *current = i18n_language_info(i18n_get_language());
  char status[128];
  snprintf(status, sizeof(status), "%s: %s (%s)",
           i18n_tr_or("settings.current_language", "Current Language"),
           current->english_name, current->code);
  create_text(panel, status, false, main_color());

  for (size_t i = 0; i < i18n_language_count(); i++) {
    const i18n_language_info_t *info = i18n_language_info((i18n_language_t)i);
    char label[128];
    snprintf(label, sizeof(label), "%s%s (%s)",
             info->id == i18n_get_language() ? "> " : "", info->english_name,
             info->code);
    create_action_button(panel, label, locale_language_event_cb,
                         (void *)(uintptr_t)info->id,
                         info->id == i18n_get_language());
  }
}

static void refresh_file_list_label(lv_obj_t *label) {
  if (!label)
    return;

  lv_label_set_text(label,
                    i18n_tr_or("tools.mount_storage",
                               "Mounting and reading the storage card root..."));
  lv_refr_now(NULL);

  static char text[4096];
  esp_err_t ret = signer_storage_browser_format_root(text, sizeof(text));
  if (ret != ESP_OK) {
    if (text[0]) {
      lv_label_set_text(label, text);
    } else {
      lv_label_set_text_fmt(
          label,
          i18n_tr_or("shell.file_browse_failed_format", "File browse failed: %s"),
          esp_err_to_name(ret));
    }
    lv_obj_set_style_text_color(label, error_color(), 0);
    return;
  }

  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, yes_color(), 0);
}

static void file_list_event_cb(lv_event_t *event) {
  lv_obj_t *label = (lv_obj_t *)lv_event_get_user_data(event);
  refresh_file_list_label(label);
}

static void create_file_manager_block(lv_obj_t *parent) {
  lv_obj_t *panel =
      create_panel(parent, cyan_color(), max_i(12, theme_get_small_padding()));
  create_text(panel, "Storage Card Files", false, highlight_color());

  lv_obj_t *result = create_text(panel, "Waiting for refresh", false, main_color());
  create_action_button(panel, "Refresh File List", file_list_event_cb, result, true);
}

static void create_delivery_status_block(lv_obj_t *parent) {
  lv_obj_t *panel =
      create_panel(parent, yes_color(), max_i(12, theme_get_small_padding()));
  create_text(panel, "Wallet Status", false, highlight_color());
  create_text(panel, "Offline", false, main_color());
}

static void create_build_identity_block(lv_obj_t *parent) {
  lv_obj_t *panel =
      create_panel(parent, cyan_color(), max_i(12, theme_get_small_padding()));
  create_text(panel, "Version and Build", false, highlight_color());

  const esp_app_desc_t *desc = esp_app_get_description();
  char line[384];
  snprintf(line, sizeof(line),
           "Project: %s\nVersion: %s\nIDF: %s\nBuild: %s %s\nTarget board: wave_43 / 480x800\nProfile: Offline wallet",
           desc ? desc->project_name : "KernSigner",
           desc ? desc->version : "unknown",
           desc ? desc->idf_ver : "unknown",
           desc ? desc->date : "unknown",
           desc ? desc->time : "unknown");
  create_text(panel, line, false, main_color());
}

static bool shell_pin_is_configured(void) {
#ifdef SIMULATOR
  return true;
#else
  return pin_is_configured();
#endif
}

static uint16_t shell_pin_session_timeout(void) {
#ifdef SIMULATOR
  return PIN_DEFAULT_TIMEOUT_SEC;
#else
  return pin_get_session_timeout();
#endif
}

static uint16_t shell_pin_poweroff_timeout(void) {
  return PIN_DEFAULT_POWER_OFF_TIMEOUT_SEC;
}

static uint8_t shell_pin_max_failures(void) {
#ifdef SIMULATOR
  return 3;
#else
  return pin_get_max_failures();
#endif
}

static bool shell_pin_has_anti_phishing(void) {
#ifdef SIMULATOR
  return true;
#else
  return pin_has_anti_phishing();
#endif
}

static void create_system_security_block(lv_obj_t *parent) {
  lv_obj_t *panel =
      create_panel(parent, error_color(), max_i(12, theme_get_small_padding()));
  create_text(panel, "Security", false, error_color());

  uint16_t timeout = shell_pin_session_timeout();
  uint16_t poweroff_timeout = shell_pin_poweroff_timeout();
  char timeout_text[32];
  if (timeout == 0) {
    snprintf(timeout_text, sizeof(timeout_text), "Off");
  } else if (timeout % 60 == 0) {
    snprintf(timeout_text, sizeof(timeout_text), "%u min",
             (unsigned)(timeout / 60));
  } else {
    snprintf(timeout_text, sizeof(timeout_text), "%u sec", (unsigned)timeout);
  }
  char poweroff_text[32];
  if (poweroff_timeout % 60 == 0) {
    snprintf(poweroff_text, sizeof(poweroff_text), "%u min",
             (unsigned)(poweroff_timeout / 60));
  } else {
    snprintf(poweroff_text, sizeof(poweroff_text), "%u sec",
             (unsigned)poweroff_timeout);
  }

  char line[384];
  snprintf(line, sizeof(line),
           "Boot PIN: %s\n"
           "Auto lock: %s\n"
           "Auto clear session: %s\n"
           "PIN retry protection: %u attempts\n"
           "Hardware key: %s",
           shell_pin_is_configured() ? "Enabled" : "Not set",
           timeout_text, poweroff_text, (unsigned)shell_pin_max_failures(),
           shell_pin_has_anti_phishing() ? "Enabled" : "Not enabled");
  create_text(panel, line, false, main_color());
}

static void create_wallet_entry_block(lv_obj_t *parent,
                                      const signer_feature_t *feature) {
  const char *route = legacy_wallet_route_for_target(feature ? feature->id : NULL);
  lv_obj_t *panel =
      create_panel(parent, route ? yes_color() : error_color(),
                   max_i(12, theme_get_small_padding()));

  if (route) {
    create_text(panel, "Wallet Entry", false, yes_color());
    create_nav_button(panel, legacy_wallet_route_label(route), route, true);
    return;
  }

  create_text(panel, "Feature Protection", false, error_color());
}

static bool is_web3_wallet_choice(const char *id) {
  return id && (strcmp(id, "web3_okx") == 0 ||
                strcmp(id, "web3_bitget") == 0 ||
                strcmp(id, "web3_metamask") == 0 ||
                strcmp(id, "web3_rabby") == 0 ||
                strcmp(id, "web3_tokenpocket") == 0 ||
                strcmp(id, "web3_imtoken") == 0 ||
                strcmp(id, "web3_address") == 0 ||
                strcmp(id, "web3_okx_mnemonic") == 0 ||
                strcmp(id, "web3_bitget_mnemonic") == 0 ||
                strcmp(id, "web3_metamask_mnemonic") == 0 ||
                strcmp(id, "web3_rabby_mnemonic") == 0 ||
                strcmp(id, "web3_tokenpocket_mnemonic") == 0 ||
                strcmp(id, "web3_imtoken_mnemonic") == 0 ||
                strcmp(id, "web3_address_mnemonic") == 0);
}

static bool is_web3_satochip_choice(const char *id) {
  return id && (strcmp(id, "web3_okx_satochip") == 0 ||
                strcmp(id, "web3_bitget_satochip") == 0 ||
                strcmp(id, "web3_metamask_satochip") == 0 ||
                strcmp(id, "web3_rabby_satochip") == 0 ||
                strcmp(id, "web3_tokenpocket_satochip") == 0 ||
                strcmp(id, "web3_imtoken_satochip") == 0 ||
                strcmp(id, "web3_address_satochip") == 0);
}

static bool is_web3_any_wallet_choice(const char *id) {
  return is_web3_wallet_choice(id) || is_web3_satochip_choice(id);
}

static bool is_connect_wallet_source_menu(const char *id) {
  return id && (strcmp(id, "connect_okx") == 0 ||
                strcmp(id, "connect_bitget") == 0 ||
                strcmp(id, "connect_metamask") == 0 ||
                strcmp(id, "connect_rabby") == 0 ||
                strcmp(id, "connect_tokenpocket") == 0 ||
                strcmp(id, "connect_imtoken") == 0 ||
                strcmp(id, "connect_keystone") == 0);
}

static bool is_connect_wallet_group_menu(const char *id) {
  return id && (strcmp(id, "connect_web3") == 0 ||
                is_connect_wallet_source_menu(id) ||
                strcmp(id, "btc_wallet") == 0 ||
                strcmp(id, "btc_mnemonic") == 0 ||
                strcmp(id, "btc_satochip") == 0);
}

static bool is_btc_wallet_choice(const char *id) {
  return id && (strcmp(id, "btc_bluewallet_zpub") == 0 ||
                strcmp(id, "btc_bluewallet_xpub") == 0);
}

static bool target_requires_signing_key(const char *id) {
  return id && (is_btc_wallet_choice(id) ||
                strcmp(id, "legacy_public_key") == 0 ||
                strcmp(id, "legacy_addresses") == 0);
}

static bool target_requires_shell_gate(const char *id) {
  if (!id)
    return false;
  return strcmp(id, "sign_mnemonic") == 0 ||
         signer_sign_mnemonic_target_id(id) ||
         signer_sign_satochip_target_id(id) ||
         is_web3_wallet_choice(id) ||
         is_btc_wallet_choice(id) ||
         strcmp(id, "legacy_public_key") == 0 ||
         strcmp(id, "legacy_addresses") == 0 ||
         strcmp(id, "custom_derivation") == 0 ||
         strcmp(id, "backup_seed_words") == 0 ||
         strcmp(id, "backup_entropy") == 0 ||
         strcmp(id, "backup_steel_punch") == 0 ||
         strcmp(id, "backup_grid") == 0 ||
         strcmp(id, "backup_stackbit") == 0 ||
         strcmp(id, "backup_seed_qr") == 0 ||
         strcmp(id, "backup_kef") == 0 ||
         strcmp(id, "tools_secondary_mnemonic") == 0 ||
         strcmp(id, "tools_mnemonic_xor") == 0 ||
         strcmp(id, "bip85") == 0 ||
         strcmp(id, "bip85_mnemonic") == 0 ||
         strcmp(id, "login_passphrase") == 0 ||
         strcmp(id, "legacy_scan_sign") == 0 ||
         strcmp(id, "sign_psbt_qr") == 0 ||
         strcmp(id, "sign_message") == 0;
}

static evm_web3_qr_bundle_t s_web3_qr_bundle;
static size_t s_web3_qr_page_index;
static lv_obj_t *s_web3_qr_obj;
static lv_obj_t *s_web3_qr_page_label;
static lv_obj_t *s_web3_qr_payload_label;
static lv_timer_t *s_web3_qr_timer;

static void web3_qr_timer_stop(void) {
  if (s_web3_qr_timer) {
    lv_timer_del(s_web3_qr_timer);
    s_web3_qr_timer = NULL;
  }
}

static void web3_qr_bundle_release(void) {
  for (size_t i = 0; i < EVM_WEB3_MAX_QR_PAGES; i++) {
    free(s_web3_qr_bundle.pages[i]);
    s_web3_qr_bundle.pages[i] = NULL;
  }
  s_web3_qr_bundle.page_count = 0;
  s_web3_qr_bundle.animated = false;
  s_web3_qr_bundle.address[0] = '\0';
  s_web3_qr_bundle.summary[0] = '\0';
}

static void web3_qr_state_reset(void) {
  web3_qr_timer_stop();
  s_web3_qr_obj = NULL;
  s_web3_qr_page_label = NULL;
  s_web3_qr_payload_label = NULL;
  s_web3_qr_page_index = 0;
  web3_qr_bundle_release();
}

#ifndef SIMULATOR
static void web3_qr_return_cb(void) {
  qr_viewer_page_destroy();
}

static void web3_qr_event_cb(lv_event_t *event) {
  (void)event;
  if (s_web3_qr_bundle.page_count == 0)
    return;
  web3_qr_timer_stop();
  const char *const *frames = (const char *const *)s_web3_qr_bundle.pages;
  bool shown = qr_viewer_page_create_frames(
      lv_screen_active(), frames, s_web3_qr_bundle.page_count, "Connect Wallet",
      web3_qr_return_cb, 170);
  if (!shown) {
    dialog_show_error("Connection code display failed", NULL, 2000);
    return;
  }
  qr_viewer_page_show();
}
#endif

static evm_web3_profile_t web3_profile_for_feature(const char *id) {
  if (!id)
    return EVM_WEB3_PROFILE_ADDRESS;
  if (strcmp(id, "web3_address") == 0 ||
      strcmp(id, "web3_address_mnemonic") == 0 ||
      strcmp(id, "web3_address_satochip") == 0)
    return EVM_WEB3_PROFILE_ADDRESS;
  if (strcmp(id, "web3_okx") == 0 ||
      strcmp(id, "web3_okx_mnemonic") == 0 ||
      strcmp(id, "web3_okx_satochip") == 0)
    return EVM_WEB3_PROFILE_OKX;
  if (strcmp(id, "web3_bitget") == 0 ||
      strcmp(id, "web3_bitget_mnemonic") == 0 ||
      strcmp(id, "web3_bitget_satochip") == 0)
    return EVM_WEB3_PROFILE_BITGET;
  if (strcmp(id, "web3_metamask") == 0 ||
      strcmp(id, "web3_metamask_mnemonic") == 0 ||
      strcmp(id, "web3_metamask_satochip") == 0)
    return EVM_WEB3_PROFILE_METAMASK;
  if (strcmp(id, "web3_rabby") == 0 ||
      strcmp(id, "web3_rabby_mnemonic") == 0 ||
      strcmp(id, "web3_rabby_satochip") == 0)
    return EVM_WEB3_PROFILE_RABBY;
  if (strcmp(id, "web3_tokenpocket") == 0 ||
      strcmp(id, "web3_tokenpocket_mnemonic") == 0 ||
      strcmp(id, "web3_tokenpocket_satochip") == 0)
    return EVM_WEB3_PROFILE_TOKENPOCKET;
  if (strcmp(id, "web3_imtoken") == 0 ||
      strcmp(id, "web3_imtoken_mnemonic") == 0 ||
      strcmp(id, "web3_imtoken_satochip") == 0)
    return EVM_WEB3_PROFILE_IMTOKEN;
  return EVM_WEB3_PROFILE_ADDRESS;
}

static const char *web3_qr_kind(evm_web3_profile_t profile) {
  switch (profile) {
  case EVM_WEB3_PROFILE_OKX:
    return "Multi-account connection code";
  case EVM_WEB3_PROFILE_BITGET:
    return "Multi-account connection code";
  case EVM_WEB3_PROFILE_METAMASK:
  case EVM_WEB3_PROFILE_RABBY:
  case EVM_WEB3_PROFILE_TOKENPOCKET:
  case EVM_WEB3_PROFILE_IMTOKEN:
    return "Account connection code";
  case EVM_WEB3_PROFILE_ADDRESS:
  default:
    return "Plain address";
  }
}

static void web3_update_qr_display(void) {
  if (!s_web3_qr_obj || s_web3_qr_page_index >= s_web3_qr_bundle.page_count)
    return;

  const char *payload = s_web3_qr_bundle.pages[s_web3_qr_page_index];
  if (!payload)
    return;

  size_t len = strlen(payload);
  ESP_LOGD(TAG, "WEB3_QR page=%u/%u len=%u animated=%d",
           (unsigned)(s_web3_qr_page_index + 1),
           (unsigned)s_web3_qr_bundle.page_count, (unsigned)len,
           s_web3_qr_bundle.animated ? 1 : 0);

  lv_qrcode_update(s_web3_qr_obj, payload, (uint32_t)len);

  char page_text[64];
  snprintf(page_text, sizeof(page_text),
           i18n_tr_or("shell.page_count_format", "Page %u/%u"),
           (unsigned)(s_web3_qr_page_index + 1),
           (unsigned)s_web3_qr_bundle.page_count);
  if (s_web3_qr_page_label)
    lv_label_set_text(s_web3_qr_page_label, page_text);

  if (s_web3_qr_payload_label)
    lv_label_set_text(s_web3_qr_payload_label, "");
}

static void web3_next_page_event_cb(lv_event_t *event) {
  (void)event;
  if (s_web3_qr_bundle.page_count <= 1)
    return;
  s_web3_qr_page_index =
      (s_web3_qr_page_index + 1) % s_web3_qr_bundle.page_count;
  web3_update_qr_display();
}

static void web3_auto_page_timer_cb(lv_timer_t *timer) {
  (void)timer;
  if (s_web3_qr_bundle.page_count <= 1 || !s_web3_qr_obj)
    return;
  s_web3_qr_page_index =
      (s_web3_qr_page_index + 1) % s_web3_qr_bundle.page_count;
  web3_update_qr_display();
}

static void create_connect_wallet_block(lv_obj_t *parent,
                                        const signer_feature_t *feature) {
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? shell_feature_title(feature) : "Ethereum Wallet", true,
              main_color());

  evm_web3_profile_t profile =
      web3_profile_for_feature(feature ? feature->id : NULL);

#ifndef SIMULATOR
  if (!key_has_signing_key() || !wallet_is_initialized()) {
    if (key_is_loaded())
      create_text(panel, "Temporary mnemonic cannot be used for signing or address derivation.",
                  false, error_color());
    else
      create_text(panel, "Import or create a mnemonic first.",
                  false, main_color());
    create_nav_button(panel, "Import", "legacy_load_wallet", true);
    return;
  }
#endif

#ifndef SIMULATOR
  if (!evm_web3_build_connect_qr(profile, &s_web3_qr_bundle)) {
    create_text(panel, "Connection code generation failed. Try again.",
                false, error_color());
    create_nav_button(panel, "Import Again", "legacy_load_wallet", true);
    return;
  }
#else
  memset(&s_web3_qr_bundle, 0, sizeof(s_web3_qr_bundle));
  snprintf(s_web3_qr_bundle.address, sizeof(s_web3_qr_bundle.address),
           "0x0000000000000000000000000000000000000000");
  snprintf(s_web3_qr_bundle.summary, sizeof(s_web3_qr_bundle.summary),
           "%s", web3_qr_kind(profile));
  s_web3_qr_bundle.pages[0] =
      strdup(profile == EVM_WEB3_PROFILE_ADDRESS
                 ? s_web3_qr_bundle.address
                 : "ethereum:0x0000000000000000000000000000000000000000");
  s_web3_qr_bundle.page_count = 1;
  s_web3_qr_bundle.animated = false;
#endif

  s_web3_qr_page_index = 0;
  create_text(panel, s_web3_qr_bundle.summary[0] ? s_web3_qr_bundle.summary
                                                 : web3_qr_kind(profile),
              false, highlight_color());

  int qr_size = theme_get_screen_width() - max_i(180, theme_get_default_padding() * 6);
  if (qr_size > 260)
    qr_size = 260;
  if (qr_size < 160)
    qr_size = 160;

  s_web3_qr_obj = lv_qrcode_create(panel);
  lv_qrcode_set_size(s_web3_qr_obj, qr_size);
  lv_qrcode_set_dark_color(s_web3_qr_obj, lv_color_hex(0x000000));
  lv_qrcode_set_light_color(s_web3_qr_obj, lv_color_hex(0xFFFFFF));
  lv_obj_set_style_border_color(s_web3_qr_obj, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_width(s_web3_qr_obj, 8, 0);
  lv_obj_set_style_radius(s_web3_qr_obj, 0, 0);

  s_web3_qr_page_label = create_text(panel, "", false, secondary_color());
  s_web3_qr_payload_label = create_text(panel, "", false, secondary_color());
  create_text(panel, s_web3_qr_bundle.address, false, main_color());
  web3_update_qr_display();

  if (s_web3_qr_bundle.page_count > 1) {
    s_web3_qr_timer = lv_timer_create(web3_auto_page_timer_cb, 170, NULL);
    create_action_button(panel, "Next", web3_next_page_event_cb, NULL, true);
  }
#ifndef SIMULATOR
  create_action_button(panel, "Show Large QR", web3_qr_event_cb, NULL, true);
#endif
}

static void satochip_fill_external_hdkey(
    evm_web3_external_hdkey_t *dst, const smartcard_satochip_account_t *src,
    const char *path, const char *children_path, bool include_chain_code,
    bool include_children, const uint8_t *parent_fingerprint,
    bool has_parent_fingerprint, uint32_t coin_type, bool has_coin_type,
    int origin_depth, int children_depth, const char *note) {
  if (!dst || !src)
    return;
  memset(dst, 0, sizeof(*dst));
  memcpy(dst->pubkey, src->compressed_pubkey, sizeof(dst->pubkey));
  if (include_chain_code)
    memcpy(dst->chain_code, src->chain_code, sizeof(dst->chain_code));
  dst->include_chain_code = include_chain_code;
  dst->include_children = include_children;
  dst->coin_type = coin_type;
  dst->has_coin_type = has_coin_type;
  dst->origin_depth = origin_depth;
  dst->children_depth = children_depth;
  dst->has_parent_fingerprint = has_parent_fingerprint;
  if (has_parent_fingerprint && parent_fingerprint)
    memcpy(dst->parent_fingerprint, parent_fingerprint,
           sizeof(dst->parent_fingerprint));
  snprintf(dst->path, sizeof(dst->path), "%s", path ? path : "");
  snprintf(dst->children_path, sizeof(dst->children_path), "%s",
           children_path ? children_path : "");
  snprintf(dst->note, sizeof(dst->note), "%s", note ? note : "");
}

static bool satochip_build_external_account(
    const smartcard_satochip_web3_account_t *card,
    evm_web3_external_account_t *account) {
  if (!card || !account || !card->address_key.has_address ||
      !card->account_key.has_compressed_pubkey || !card->account_key.has_chain_code)
    return false;

  memset(account, 0, sizeof(*account));
  snprintf(account->address, sizeof(account->address), "%s",
           card->address_key.address);
  if (card->has_master_fingerprint) {
    memcpy(account->master_fingerprint, card->master_fingerprint,
           sizeof(account->master_fingerprint));
    account->has_master_fingerprint = true;
  }

  uint8_t parent_fp[4] = {0};
  bool has_parent_fp = false;
  if (card->has_parent_fingerprint) {
    memcpy(parent_fp, card->parent_fingerprint, sizeof(parent_fp));
    has_parent_fp = true;
  }

  satochip_fill_external_hdkey(
      &account->standard, &card->account_key, "m/44'/60'/0'", "0/*", true,
      true, parent_fp, has_parent_fp, 60, true, -1, -1, "account.standard");

  for (size_t i = 0; i < card->ledger_live_count &&
                     i < EVM_WEB3_MAX_EXTERNAL_LEDGER_KEYS;
       i++) {
    satochip_fill_external_hdkey(
        &account->ledger_live[account->ledger_live_count],
        &card->ledger_live[i], card->ledger_live[i].path, NULL, false, false,
        NULL, false, 0, false, -1, -1, "account.ledger_live");
    account->ledger_live_count++;
  }
  if (account->ledger_live_count == 0 &&
      card->address_key.has_compressed_pubkey) {
    satochip_fill_external_hdkey(
        &account->ledger_live[account->ledger_live_count],
        &card->address_key, "m/44'/60'/0'/0/0", NULL, false, false,
        NULL, false, 0, false, -1, -1, "account.ledger_live");
    account->ledger_live_count++;
  }

  for (size_t i = 0; i < card->btc_count && i < EVM_WEB3_MAX_EXTERNAL_BTC_KEYS;
       i++) {
    satochip_fill_external_hdkey(
        &account->btc[account->btc_count], &card->btc[i], card->btc[i].path,
        NULL, true, false, parent_fp, has_parent_fp, 0, true, -2, -1, "");
    account->btc_count++;
  }
  return true;
}

static void create_satochip_qr_block(lv_obj_t *panel,
                                     evm_web3_profile_t profile,
                                     const evm_web3_qr_bundle_t *bundle) {
  (void)bundle;
  s_web3_qr_page_index = 0;
  create_text(panel, s_web3_qr_bundle.summary[0] ? s_web3_qr_bundle.summary
                                                 : web3_qr_kind(profile),
              false, highlight_color());

  int qr_size = theme_get_screen_width() - max_i(180, theme_get_default_padding() * 6);
  if (qr_size > 260)
    qr_size = 260;
  if (qr_size < 160)
    qr_size = 160;

  s_web3_qr_obj = lv_qrcode_create(panel);
  lv_qrcode_set_size(s_web3_qr_obj, qr_size);
  lv_qrcode_set_dark_color(s_web3_qr_obj, lv_color_hex(0x000000));
  lv_qrcode_set_light_color(s_web3_qr_obj, lv_color_hex(0xFFFFFF));
  lv_obj_set_style_border_color(s_web3_qr_obj, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_width(s_web3_qr_obj, 8, 0);
  lv_obj_set_style_radius(s_web3_qr_obj, 0, 0);

  s_web3_qr_page_label = create_text(panel, "", false, secondary_color());
  s_web3_qr_payload_label = create_text(panel, "", false, secondary_color());
  create_text(panel, s_web3_qr_bundle.address, false, main_color());
  web3_update_qr_display();

  if (s_web3_qr_bundle.page_count > 1) {
    s_web3_qr_timer = lv_timer_create(web3_auto_page_timer_cb, 170, NULL);
    create_action_button(panel, "Next", web3_next_page_event_cb, NULL, true);
  }
#ifndef SIMULATOR
  create_action_button(panel, "Show Large QR", web3_qr_event_cb, NULL, true);
#endif
}

#define SATOCHIP_CONNECT_TASK_STACK_SIZE 16384

static lv_obj_t *s_satochip_progress_dialog;
static lv_timer_t *s_satochip_poll_timer;
static TaskHandle_t s_satochip_task_handle;
static volatile bool s_satochip_task_done;
static volatile bool s_satochip_task_with_caps;
static esp_err_t s_satochip_task_err = ESP_OK;
static char s_satochip_task_pin[80];
static smartcard_satochip_web3_account_t s_satochip_task_card_account;

static void satochip_pin_input_cleanup(void) {
  if (s_satochip_pin_input_active) {
    ui_text_input_destroy(&s_satochip_pin_input);
    memset(&s_satochip_pin_input, 0, sizeof(s_satochip_pin_input));
    s_satochip_pin_input_active = false;
  }
  s_satochip_path_textarea = NULL;
}

static void satochip_pin_back_cb(lv_event_t *event) {
  (void)event;
  satochip_pin_input_cleanup();
  (void)signer_shell_show_screen(s_satochip_pending_return_id[0]
                                   ? s_satochip_pending_return_id
                                   : "pi_connect_wallet");
}

static void satochip_pin_cancel_cb(lv_event_t *event) {
  (void)event;
  satochip_pin_back_cb(event);
}

static void satochip_connect_task(void *arg) {
  (void)arg;
  memset(&s_satochip_task_card_account, 0,
         sizeof(s_satochip_task_card_account));
  const bool delete_with_caps = s_satochip_task_with_caps;
  const bool include_okx_multi_accounts =
      s_satochip_pending_profile == EVM_WEB3_PROFILE_OKX;
  ESP_LOGD(TAG, "SATOCHIP_CONNECT begin okx_multi=%u",
           include_okx_multi_accounts ? 1U : 0U);
  s_satochip_task_err = smartcard_satochip_get_web3_account(
      s_satochip_task_pin, &s_satochip_task_card_account, 10000,
      include_okx_multi_accounts);
  ESP_LOGD(TAG, "SATOCHIP_CONNECT done err=%s",
           esp_err_to_name(s_satochip_task_err));
  secure_memzero(s_satochip_task_pin, sizeof(s_satochip_task_pin));
  __atomic_store_n(&s_satochip_task_done, true, __ATOMIC_RELEASE);
  if (delete_with_caps) {
    vTaskDeleteWithCaps(NULL);
    return;
  }
  vTaskDelete(NULL);
}

static void satochip_connect_finish_ui(void) {
  if (s_satochip_progress_dialog) {
    lv_obj_del(s_satochip_progress_dialog);
    s_satochip_progress_dialog = NULL;
  }

  if (s_satochip_task_err != ESP_OK) {
    char msg[320];
    snprintf(msg, sizeof(msg), "Smartcard connection failed: %s\n%s",
             esp_err_to_name(s_satochip_task_err),
             s_satochip_task_card_account.detail);
    dialog_show_error(msg, NULL, 0);
    secure_memzero(&s_satochip_task_card_account,
                   sizeof(s_satochip_task_card_account));
    return;
  }

  evm_web3_external_account_t external_account;
  if (!satochip_build_external_account(&s_satochip_task_card_account,
                                       &external_account) ||
      !evm_web3_build_external_connect_qr(
          s_satochip_pending_profile, &external_account, &s_web3_qr_bundle)) {
    dialog_show_error("Smartcard connection code generation failed", NULL, 0);
    secure_memzero(&s_satochip_task_card_account,
                   sizeof(s_satochip_task_card_account));
    secure_memzero(&external_account, sizeof(external_account));
    return;
  }

  lv_obj_clean(s_parent);
  theme_apply_screen(s_parent);
  lv_obj_t *root = theme_create_page_container(s_parent);
  lv_obj_set_style_bg_color(root, signer_canvas_color(), 0);
  lv_obj_set_style_pad_top(root, shell_margin_v(), 0);
  lv_obj_set_style_pad_bottom(root, shell_margin_v(), 0);
  lv_obj_set_style_pad_left(root, shell_margin_h(), 0);
  lv_obj_set_style_pad_right(root, shell_margin_h(), 0);
  lv_obj_set_style_pad_gap(root, shell_root_gap(false), 0);
  lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  const signer_feature_t *feature = signer_feature_find(s_current_screen_id);
  if (feature)
    create_signer_header(root, feature);

  lv_obj_t *panel =
      create_panel(root, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, "Smartcard Connection Code", true, main_color());
  create_satochip_qr_block(panel, s_satochip_pending_profile,
                           &s_web3_qr_bundle);
  create_nav_button(panel, i18n_tr_or("common.back", "Back"),
                    s_satochip_pending_return_id[0]
                        ? s_satochip_pending_return_id
                        : "pi_connect_wallet",
                    false);

  secure_memzero(&s_satochip_task_card_account,
                 sizeof(s_satochip_task_card_account));
  secure_memzero(&external_account, sizeof(external_account));
}

static void satochip_connect_poll_cb(lv_timer_t *timer) {
  (void)timer;
  if (!__atomic_load_n(&s_satochip_task_done, __ATOMIC_ACQUIRE))
    return;

  if (s_satochip_poll_timer) {
    lv_timer_del(s_satochip_poll_timer);
    s_satochip_poll_timer = NULL;
  }
  s_satochip_task_handle = NULL;
  s_satochip_task_with_caps = false;
  satochip_connect_finish_ui();
}

static lv_obj_t *s_satochip_tool_progress_dialog;
static lv_timer_t *s_satochip_tool_poll_timer;
static TaskHandle_t s_satochip_tool_task_handle;
static volatile bool s_satochip_tool_task_done;
static volatile bool s_satochip_tool_task_with_caps;
static esp_err_t s_satochip_tool_task_err = ESP_OK;
static char s_satochip_tool_pin[80];
static char s_satochip_tool_path[128];
static char s_satochip_tool_title[64];
static char s_satochip_tool_result[768];
static char s_satochip_tool_qr_payload[256];
static lv_obj_t *s_card_info_progress_dialog;
static lv_timer_t *s_card_info_poll_timer;
static TaskHandle_t s_card_info_task_handle;
static volatile bool s_card_info_task_done;
static volatile bool s_card_info_task_with_caps;
static esp_err_t s_card_info_task_err = ESP_OK;
static EXT_RAM_BSS_ATTR char s_card_info_result[2048];

static void card_info_finish_ui(void);
static void card_info_poll_cb(lv_timer_t *timer);

static void card_info_task(void *arg) {
  (void)arg;
  const bool delete_with_caps = s_card_info_task_with_caps;
  ESP_LOGD(TAG, "CARD_INFO begin");

  s_card_info_task_err = ESP_OK;
  s_card_info_result[0] = '\0';

  char reader_text[1536];
  char label_text[512];
  smartcard_satochip_label_t label;
  memset(&label, 0, sizeof(label));

  esp_err_t probe_err = smartcard_transport_probe(30000);
  smartcard_transport_format_report(reader_text, sizeof(reader_text));

  if (probe_err == ESP_OK) {
    esp_err_t label_err = smartcard_satochip_get_label(&label, 30000);
    smartcard_satochip_format_label(&label, label_text, sizeof(label_text));

    if (label_err != ESP_OK)
      probe_err = label_err;

    snprintf(s_card_info_result, sizeof(s_card_info_result),
             "Reader\n%s\n\nCard Label\n%s", reader_text, label_text);
  } else {
    snprintf(s_card_info_result, sizeof(s_card_info_result),
             "Reader Overview\n%s\n\nFurther reads skipped: reader or card is not ready.",
             reader_text);
  }

  s_card_info_task_err = probe_err;
  secure_memzero(&label, sizeof(label));
  __atomic_store_n(&s_card_info_task_done, true, __ATOMIC_RELEASE);
  if (delete_with_caps) {
    vTaskDeleteWithCaps(NULL);
    return;
  }
  vTaskDelete(NULL);
}

static void card_info_start(void) {
  if (smartcard_task_busy()) {
    dialog_show_error("Smartcard busy", NULL, 1600);
    return;
  }

  s_card_info_result[0] = '\0';
  __atomic_store_n(&s_card_info_task_done, false, __ATOMIC_RELEASE);
  s_card_info_task_err = ESP_ERR_INVALID_STATE;
  s_card_info_task_with_caps = false;

  s_card_info_progress_dialog =
      dialog_show_progress("Reading Card Info", "Reading",
                           DIALOG_STYLE_OVERLAY);
  lv_refr_now(NULL);

  BaseType_t ok = xTaskCreatePinnedToCore(
      card_info_task, "card_info", SATOCHIP_CONNECT_TASK_STACK_SIZE, NULL, 4,
      &s_card_info_task_handle, 1);
  if (ok != pdPASS) {
    s_card_info_task_with_caps = true;
    ok = xTaskCreatePinnedToCoreWithCaps(
        card_info_task, "card_info", SATOCHIP_CONNECT_TASK_STACK_SIZE, NULL, 4,
        &s_card_info_task_handle, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
  if (ok != pdPASS) {
    s_card_info_task_handle = NULL;
    s_card_info_task_with_caps = false;
    if (s_card_info_progress_dialog) {
      lv_obj_del(s_card_info_progress_dialog);
      s_card_info_progress_dialog = NULL;
    }
    dialog_show_error("Card info task failed to start", NULL, 0);
    return;
  }

  s_card_info_poll_timer = lv_timer_create(card_info_poll_cb, 100, NULL);
}

static void card_info_start_event_cb(lv_event_t *event) {
  (void)event;
  card_info_start();
}

static void card_info_finish_ui(void) {
  if (s_card_info_progress_dialog) {
    lv_obj_del(s_card_info_progress_dialog);
    s_card_info_progress_dialog = NULL;
  }
  s_card_info_task_handle = NULL;
  s_card_info_task_with_caps = false;
  (void)signer_shell_show_screen(s_current_screen_id);
}

static void card_info_poll_cb(lv_timer_t *timer) {
  (void)timer;
  if (!__atomic_load_n(&s_card_info_task_done, __ATOMIC_ACQUIRE))
    return;

  if (s_card_info_poll_timer) {
    lv_timer_del(s_card_info_poll_timer);
    s_card_info_poll_timer = NULL;
  }
  card_info_finish_ui();
}

static void create_card_info_block(lv_obj_t *parent) {
  lv_obj_t *panel = create_panel(
      parent,
      s_card_info_result[0] ? (s_card_info_task_err == ESP_OK ? yes_color()
                                                             : error_color())
                            : highlight_color(),
      max_i(14, theme_get_small_padding()));
  create_text(panel, "Card Info", false, highlight_color());

  if (s_card_info_result[0] != '\0') {
    create_text(panel,
                s_card_info_task_err == ESP_OK ? "Read" : "Read failed",
                false, s_card_info_task_err == ESP_OK ? yes_color()
                                                     : error_color());
    create_text(panel, s_card_info_result, false, main_color());
    create_action_button(panel, "Refresh", card_info_start_event_cb, NULL,
                         true);
  } else {
    create_text(panel, "Waiting", false, main_color());
    create_action_button(panel, "Start Reading", card_info_start_event_cb, NULL,
                         true);
  }
}

static void satochip_normalize_path(char *dst, size_t dst_len,
                                    const char *src) {
  size_t pos = 0;
  if (!dst || dst_len == 0)
    return;
  for (const char *p = src; p && *p && pos + 1 < dst_len; p++) {
    if (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')
      continue;
    dst[pos++] = (*p == 'h' || *p == 'H') ? '\'' : *p;
  }
  dst[pos] = '\0';
}

static bool satochip_path_component(const char **cursor, uint32_t *value,
                                    bool *hardened) {
  if (!cursor || !*cursor || !value || !hardened)
    return false;
  const char *p = *cursor;
  if (*p < '0' || *p > '9')
    return false;
  uint32_t v = 0;
  while (*p >= '0' && *p <= '9') {
    uint32_t digit = (uint32_t)(*p - '0');
    if (v > (UINT32_MAX - digit) / 10U)
      return false;
    v = v * 10U + digit;
    p++;
  }
  bool hard = false;
  if (*p == '\'' || *p == 'h' || *p == 'H') {
    hard = true;
    p++;
  }
  *cursor = p;
  *value = v;
  *hardened = hard;
  return true;
}

static bool satochip_parse_path_components(const char *path, uint32_t *components,
                                           bool *hardened, size_t max_depth,
                                           size_t *depth_out) {
  if (depth_out)
    *depth_out = 0;
  if (!path || !components || !hardened || max_depth == 0)
    return false;

  const char *p = path;
  if (*p == 'm') {
    p++;
    if (*p == '/')
      p++;
    else if (*p != '\0')
      return false;
  }

  size_t depth = 0;
  while (*p) {
    if (depth >= max_depth)
      return false;
    if (!satochip_path_component(&p, &components[depth], &hardened[depth]))
      return false;
    depth++;
    if (*p == '/') {
      p++;
      if (*p == '\0')
        return false;
      continue;
    }
    if (*p != '\0')
      return false;
  }

  if (depth == 0)
    return false;
  if (depth_out)
    *depth_out = depth;
  return true;
}

static bool satochip_path_info(const char *path, uint32_t *purpose,
                               uint32_t *coin, bool *is_evm,
                               bool *is_testnet) {
  if (purpose)
    *purpose = 0;
  if (coin)
    *coin = 0;
  if (is_evm)
    *is_evm = false;
  if (is_testnet)
    *is_testnet = false;
  if (!path)
    return false;

  uint32_t components[10];
  bool hardened[10];
  size_t depth = 0;
  if (!satochip_parse_path_components(path, components, hardened, 10, &depth) ||
      depth < 2 || !hardened[0] || !hardened[1])
    return false;
  if (purpose)
    *purpose = components[0];
  if (coin)
    *coin = components[1];
  if (is_evm)
    *is_evm = components[0] == 44 && components[1] == 60;
  if (is_testnet)
    *is_testnet = components[1] == 1;
  return true;
}

static bool satochip_btc_address_path_allowed(const char *path,
                                              uint32_t purpose,
                                              uint32_t coin) {
  if (!((purpose == 44 || purpose == 49 || purpose == 84 || purpose == 86) &&
        (coin == 0 || coin == 1))) {
    return false;
  }

  uint32_t components[10];
  bool hardened[10];
  size_t depth = 0;
  if (!satochip_parse_path_components(path, components, hardened, 10, &depth))
    return false;

  return depth == 5 && components[0] == purpose && components[1] == coin &&
         hardened[0] && hardened[1] && hardened[2] && !hardened[3] &&
         !hardened[4] && components[3] <= 1;
}

static smartcard_satochip_btc_script_t
satochip_script_for_purpose(uint32_t purpose) {
  if (purpose == 49)
    return SMARTCARD_SATOCHIP_BTC_P2SH_P2WPKH;
  if (purpose == 84)
    return SMARTCARD_SATOCHIP_BTC_P2WPKH;
  if (purpose == 86)
    return SMARTCARD_SATOCHIP_BTC_P2TR;
  return SMARTCARD_SATOCHIP_BTC_P2PKH;
}

static const char *satochip_script_name(uint32_t purpose) {
  if (purpose == 49)
    return "BTC P2SH-SegWit";
  if (purpose == 84)
    return "BTC Native SegWit";
  if (purpose == 86)
    return "BTC Taproot";
  return "BTC Legacy";
}

typedef struct {
  const char *path;
  const char *xtype;
  bool is_testnet;
} satochip_xpub_request_t;

static bool satochip_xpub_request_for_mode(satochip_tool_mode_t mode,
                                           satochip_xpub_request_t *out) {
  if (!out)
    return false;
  memset(out, 0, sizeof(*out));
  switch (mode) {
  case SATOCHIP_TOOL_BTC_XPUB:
    out->path = "m/44'/0'/0'";
    out->xtype = "xpub";
    return true;
  case SATOCHIP_TOOL_BTC_YPUB:
    out->path = "m/49'/0'/0'";
    out->xtype = "ypub";
    return true;
  case SATOCHIP_TOOL_BTC_ZPUB:
    out->path = "m/84'/0'/0'";
    out->xtype = "zpub";
    return true;
  case SATOCHIP_TOOL_BTC_TPUB:
    out->path = "m/44'/1'/0'";
    out->xtype = "tpub";
    out->is_testnet = true;
    return true;
  case SATOCHIP_TOOL_BTC_UPUB:
    out->path = "m/49'/1'/0'";
    out->xtype = "upub";
    out->is_testnet = true;
    return true;
  case SATOCHIP_TOOL_BTC_VPUB:
    out->path = "m/84'/1'/0'";
    out->xtype = "vpub";
    out->is_testnet = true;
    return true;
  default:
    return false;
  }
}

static void satochip_tool_task(void *arg) {
  (void)arg;
  const bool delete_with_caps = s_satochip_tool_task_with_caps;
  ESP_LOGD(TAG, "SATOCHIP_TOOL begin mode=%d", (int)s_satochip_tool_mode);
  s_satochip_tool_task_err = ESP_ERR_INVALID_STATE;
  s_satochip_tool_result[0] = '\0';
  s_satochip_tool_qr_payload[0] = '\0';

  satochip_xpub_request_t xpub_request;
  if (satochip_xpub_request_for_mode(s_satochip_tool_mode, &xpub_request)) {
    smartcard_satochip_btc_xpub_t xpub;
    memset(&xpub, 0, sizeof(xpub));
    s_satochip_tool_task_err = smartcard_satochip_get_btc_xpub(
        s_satochip_tool_pin, xpub_request.path, xpub_request.xtype,
        xpub_request.is_testnet, &xpub, 20000);
    if (s_satochip_tool_task_err == ESP_OK && xpub.has_xpub) {
      snprintf(s_satochip_tool_title, sizeof(s_satochip_tool_title),
               "Satochip %s", xpub_request.xtype);
      snprintf(s_satochip_tool_qr_payload, sizeof(s_satochip_tool_qr_payload),
               "%s", xpub.xpub);
      snprintf(s_satochip_tool_result, sizeof(s_satochip_tool_result),
               "Path: %s\nType: %s\n%s\n\n%s", xpub.path, xpub.xtype,
               xpub.xpub,
               xpub.has_descriptor ? xpub.descriptor : "Descriptor: not generated");
    } else {
      snprintf(s_satochip_tool_result, sizeof(s_satochip_tool_result),
               "%s\nError: %s", xpub.detail,
               esp_err_to_name(s_satochip_tool_task_err));
    }
    secure_memzero(&xpub, sizeof(xpub));
  } else if (s_satochip_tool_mode == SATOCHIP_TOOL_PATH_ADDRESS) {
    uint32_t purpose = 0;
    uint32_t coin = 0;
    bool is_evm = false;
    bool is_testnet = false;
    if (!satochip_path_info(s_satochip_tool_path, &purpose, &coin, &is_evm,
                            &is_testnet)) {
      s_satochip_tool_task_err = ESP_ERR_INVALID_ARG;
      snprintf(s_satochip_tool_result, sizeof(s_satochip_tool_result),
               "Invalid path format: %s", s_satochip_tool_path);
    } else if (is_evm) {
      smartcard_satochip_account_t account;
      memset(&account, 0, sizeof(account));
      s_satochip_tool_task_err = smartcard_satochip_get_eth_account(
          s_satochip_tool_pin, s_satochip_tool_path, &account, 20000);
      if (s_satochip_tool_task_err == ESP_OK && account.has_address) {
        snprintf(s_satochip_tool_title, sizeof(s_satochip_tool_title),
                 "Satochip EVM Address");
        snprintf(s_satochip_tool_qr_payload,
                 sizeof(s_satochip_tool_qr_payload), "%s", account.address);
        snprintf(s_satochip_tool_result, sizeof(s_satochip_tool_result),
                 "Path: %s\nType: EVM\nAddress: %s", account.path,
                 account.address);
      } else {
        snprintf(s_satochip_tool_result, sizeof(s_satochip_tool_result),
                 "%s\nError: %s", account.detail,
                 esp_err_to_name(s_satochip_tool_task_err));
      }
      secure_memzero(&account, sizeof(account));
    } else {
      if (!satochip_btc_address_path_allowed(s_satochip_tool_path, purpose,
                                             coin)) {
        s_satochip_tool_task_err = ESP_ERR_INVALID_ARG;
        snprintf(s_satochip_tool_result, sizeof(s_satochip_tool_result),
                 "BTC address path must be a full address path, for example:\n"
                 "m/84'/0'/0'/0/0\n"
                 "m/44'/1'/0'/0/0");
      } else {
        smartcard_satochip_btc_address_t address;
        memset(&address, 0, sizeof(address));
        s_satochip_tool_task_err = smartcard_satochip_get_btc_address(
            s_satochip_tool_pin, s_satochip_tool_path,
            satochip_script_for_purpose(purpose), is_testnet, &address, 20000);
        if (s_satochip_tool_task_err == ESP_OK && address.has_address) {
          snprintf(s_satochip_tool_title, sizeof(s_satochip_tool_title),
                   "Satochip BTC Address");
          snprintf(s_satochip_tool_qr_payload,
                   sizeof(s_satochip_tool_qr_payload), "%s", address.address);
          snprintf(s_satochip_tool_result, sizeof(s_satochip_tool_result),
                   "Path: %s\nType: %s\nNetwork: %s\nAddress: %s", address.path,
                   satochip_script_name(purpose),
                   coin == 1 ? "Testnet" : "Mainnet", address.address);
        } else {
          snprintf(s_satochip_tool_result, sizeof(s_satochip_tool_result),
                   "%s\nError: %s", address.detail,
                   esp_err_to_name(s_satochip_tool_task_err));
        }
        secure_memzero(&address, sizeof(address));
      }
    }
  }

  secure_memzero(s_satochip_tool_pin, sizeof(s_satochip_tool_pin));
  __atomic_store_n(&s_satochip_tool_task_done, true, __ATOMIC_RELEASE);
  if (delete_with_caps) {
    vTaskDeleteWithCaps(NULL);
    return;
  }
  vTaskDelete(NULL);
}

static void satochip_tool_finish_ui(void) {
  if (s_satochip_tool_progress_dialog) {
    lv_obj_del(s_satochip_tool_progress_dialog);
    s_satochip_tool_progress_dialog = NULL;
  }

  lv_obj_clean(s_parent);
  theme_apply_screen(s_parent);
  lv_obj_t *root = theme_create_page_container(s_parent);
  lv_obj_set_style_bg_color(root, signer_canvas_color(), 0);
  lv_obj_set_style_pad_top(root, shell_margin_v(), 0);
  lv_obj_set_style_pad_bottom(root, shell_margin_v(), 0);
  lv_obj_set_style_pad_left(root, shell_margin_h(), 0);
  lv_obj_set_style_pad_right(root, shell_margin_h(), 0);
  lv_obj_set_style_pad_gap(root, shell_root_gap(false), 0);
  lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  const signer_feature_t *feature = signer_feature_find(s_current_screen_id);
  if (feature)
    create_signer_header(root, feature);

  lv_obj_t *content = create_signer_list(root, false, false);
  lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);

  lv_obj_t *panel =
      create_panel(content,
                   s_satochip_tool_task_err == ESP_OK ? yes_color()
                                                      : error_color(),
                   max_i(14, theme_get_small_padding()));
  lv_obj_set_flex_grow(panel, 1);
  lv_obj_add_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(panel, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_style_pad_right(panel, theme_get_small_padding() + 4, 0);
  create_text(panel,
              s_satochip_tool_title[0] ? s_satochip_tool_title : "Satochip",
              true,
              s_satochip_tool_task_err == ESP_OK ? yes_color()
                                                 : error_color());
  create_text(panel, s_satochip_tool_result, false, main_color());

  if (s_satochip_tool_task_err == ESP_OK &&
      s_satochip_tool_qr_payload[0] != '\0') {
    int qr_size = theme_get_screen_width() -
                  max_i(180, theme_get_default_padding() * 6);
    if (qr_size > 260)
      qr_size = 260;
    if (qr_size < 160)
      qr_size = 160;
    lv_obj_t *qr = lv_qrcode_create(panel);
    lv_qrcode_set_size(qr, qr_size);
    lv_qrcode_set_dark_color(qr, lv_color_hex(0x000000));
    lv_qrcode_set_light_color(qr, lv_color_hex(0xFFFFFF));
    lv_obj_set_style_border_color(qr, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(qr, 8, 0);
    lv_obj_set_style_radius(qr, 0, 0);
    lv_qrcode_update(qr, s_satochip_tool_qr_payload,
                     (uint32_t)strlen(s_satochip_tool_qr_payload));
  }

  const char *return_id = s_satochip_pending_return_id[0]
                              ? s_satochip_pending_return_id
                              : "smartcard_tools";
  create_nav_button(panel, i18n_tr_or("common.back", "Back"), return_id, true);
  create_nav_button(panel, i18n_tr_or("common.return_home", "Return Home"),
                    "home", false);
  secure_memzero(s_satochip_tool_result, sizeof(s_satochip_tool_result));
  secure_memzero(s_satochip_tool_qr_payload,
                 sizeof(s_satochip_tool_qr_payload));
}

static void satochip_tool_poll_cb(lv_timer_t *timer) {
  (void)timer;
  if (!__atomic_load_n(&s_satochip_tool_task_done, __ATOMIC_ACQUIRE))
    return;

  if (s_satochip_tool_poll_timer) {
    lv_timer_del(s_satochip_tool_poll_timer);
    s_satochip_tool_poll_timer = NULL;
  }
  s_satochip_tool_task_handle = NULL;
  s_satochip_tool_task_with_caps = false;
  satochip_tool_finish_ui();
}

static void satochip_tool_start(const char *pin_text) {
  if (!pin_text || pin_text[0] == '\0') {
    dialog_show_error("Enter smartcard PIN", NULL, 1600);
    return;
  }
  if (smartcard_task_busy()) {
    dialog_show_error("Smartcard busy", NULL, 1600);
    return;
  }

  char normalized_path[128] = {0};
  if (s_satochip_tool_mode == SATOCHIP_TOOL_PATH_ADDRESS) {
    const char *path_text =
        s_satochip_path_textarea ? lv_textarea_get_text(s_satochip_path_textarea)
                                 : "";
    satochip_normalize_path(normalized_path, sizeof(normalized_path),
                            path_text && path_text[0] ? path_text
                                                       : "m/44'/60'/0'/0/0");
    if (normalized_path[0] == '\0') {
      dialog_show_error("Enter derivation path", NULL, 1600);
      return;
    }
  }

  snprintf(s_satochip_tool_pin, sizeof(s_satochip_tool_pin), "%s", pin_text);
  snprintf(s_satochip_tool_path, sizeof(s_satochip_tool_path), "%s",
           normalized_path);
  __atomic_store_n(&s_satochip_tool_task_done, false, __ATOMIC_RELEASE);
  s_satochip_tool_task_err = ESP_ERR_INVALID_STATE;
  s_satochip_tool_task_with_caps = false;
  s_satochip_tool_title[0] = '\0';
  s_satochip_tool_result[0] = '\0';
  s_satochip_tool_qr_payload[0] = '\0';

  s_satochip_tool_progress_dialog =
      dialog_show_progress("Reading Smartcard", "Reading",
                           DIALOG_STYLE_OVERLAY);
  lv_refr_now(NULL);

  BaseType_t ok = xTaskCreatePinnedToCore(
      satochip_tool_task, "satochip_tool", SATOCHIP_CONNECT_TASK_STACK_SIZE,
      NULL, 4, &s_satochip_tool_task_handle, 1);
  if (ok != pdPASS) {
    s_satochip_tool_task_with_caps = true;
    ok = xTaskCreatePinnedToCoreWithCaps(
        satochip_tool_task, "satochip_tool", SATOCHIP_CONNECT_TASK_STACK_SIZE,
        NULL, 4, &s_satochip_tool_task_handle, 1,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
  if (ok != pdPASS) {
    secure_memzero(s_satochip_tool_pin, sizeof(s_satochip_tool_pin));
    s_satochip_tool_task_handle = NULL;
    s_satochip_tool_task_with_caps = false;
    if (s_satochip_tool_progress_dialog) {
      lv_obj_del(s_satochip_tool_progress_dialog);
      s_satochip_tool_progress_dialog = NULL;
    }
    dialog_show_error("Smartcard task failed to start", NULL, 0);
    return;
  }

  s_satochip_tool_poll_timer = lv_timer_create(satochip_tool_poll_cb, 100, NULL);
}

static void satochip_pin_ready_cb(lv_event_t *event) {
  (void)event;
  if (!s_satochip_pin_input.textarea)
    return;

  const char *pin_text = lv_textarea_get_text(s_satochip_pin_input.textarea);
  if (!pin_text || pin_text[0] == '\0') {
    dialog_show_error("Enter smartcard PIN", NULL, 1600);
    return;
  }

  char pin_copy[80];
  snprintf(pin_copy, sizeof(pin_copy), "%s", pin_text);
  if (s_satochip_pin_flow == SATOCHIP_PIN_FLOW_TOOL) {
    satochip_tool_start(pin_copy);
    secure_memzero(pin_copy, sizeof(pin_copy));
    satochip_pin_input_cleanup();
    return;
  }
  satochip_pin_input_cleanup();

  if (smartcard_task_busy()) {
    secure_memzero(pin_copy, sizeof(pin_copy));
    dialog_show_error("Smartcard busy", NULL, 1600);
    return;
  }

  snprintf(s_satochip_task_pin, sizeof(s_satochip_task_pin), "%s", pin_copy);
  secure_memzero(pin_copy, sizeof(pin_copy));
  __atomic_store_n(&s_satochip_task_done, false, __ATOMIC_RELEASE);
  s_satochip_task_err = ESP_ERR_INVALID_STATE;
  memset(&s_satochip_task_card_account, 0,
         sizeof(s_satochip_task_card_account));
  s_satochip_task_with_caps = false;

  s_satochip_progress_dialog =
      dialog_show_progress("Reading Smartcard", "Reading",
                           DIALOG_STYLE_OVERLAY);
  lv_refr_now(NULL);

  BaseType_t ok = xTaskCreatePinnedToCore(
      satochip_connect_task, "satochip_conn", SATOCHIP_CONNECT_TASK_STACK_SIZE,
      NULL, 4, &s_satochip_task_handle, 1);
  if (ok != pdPASS) {
    ESP_LOGW(TAG,
             "Satochip connect task internal stack failed; internal=%u min_internal=%u spiram=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    s_satochip_task_with_caps = true;
    ok = xTaskCreatePinnedToCoreWithCaps(
        satochip_connect_task, "satochip_conn", SATOCHIP_CONNECT_TASK_STACK_SIZE,
        NULL, 4, &s_satochip_task_handle, 1,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
  if (ok != pdPASS) {
    ESP_LOGE(TAG,
             "Satochip connect task create failed; internal=%u min_internal=%u spiram=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    secure_memzero(s_satochip_task_pin, sizeof(s_satochip_task_pin));
    s_satochip_task_handle = NULL;
    s_satochip_task_with_caps = false;
    if (s_satochip_progress_dialog) {
      lv_obj_del(s_satochip_progress_dialog);
      s_satochip_progress_dialog = NULL;
    }
    dialog_show_error("Smartcard task failed to start", NULL, 0);
    return;
  }

  s_satochip_poll_timer = lv_timer_create(satochip_connect_poll_cb, 100, NULL);
}

static void create_satochip_connect_block(lv_obj_t *parent,
                                          const signer_feature_t *feature) {
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? shell_feature_title(feature) : "Smartcard",
              true, main_color());

  snprintf(s_satochip_pending_return_id, sizeof(s_satochip_pending_return_id),
           "%s", feature && feature->parent_id ? feature->parent_id
                                                : "pi_connect_wallet");
  s_satochip_pending_profile =
      web3_profile_for_feature(feature ? feature->id : NULL);
  s_satochip_pin_flow = SATOCHIP_PIN_FLOW_CONNECT;
  s_satochip_tool_mode = SATOCHIP_TOOL_NONE;

  satochip_pin_input_cleanup();
  ui_text_input_create(&s_satochip_pin_input, panel, "Smartcard PIN", true,
                       satochip_pin_ready_cb);
  s_satochip_pin_input_active = true;
  if (s_satochip_pin_input.keyboard)
    lv_obj_add_event_cb(s_satochip_pin_input.keyboard, satochip_pin_cancel_cb,
                        LV_EVENT_CANCEL, NULL);
  if (s_satochip_pin_input.textarea)
    lv_obj_add_event_cb(s_satochip_pin_input.textarea, satochip_pin_cancel_cb,
                        LV_EVENT_CANCEL, NULL);
  create_action_button(panel, i18n_tr_or("common.back", "Back"),
                       satochip_pin_back_cb, NULL, false);
}

static void satochip_tool_focus_path_cb(lv_event_t *event) {
  lv_event_code_t code = lv_event_get_code(event);
  if (s_satochip_pin_input.keyboard && s_satochip_path_textarea) {
    lv_obj_clear_flag(s_satochip_pin_input.keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_satochip_pin_input.keyboard);
    lv_keyboard_set_textarea(s_satochip_pin_input.keyboard,
                             s_satochip_path_textarea);
  }
  if (code != LV_EVENT_FOCUSED && s_satochip_pin_input.input_group &&
      s_satochip_path_textarea)
    lv_group_focus_obj(s_satochip_path_textarea);
  if (s_satochip_path_textarea) {
    lv_obj_update_layout(lv_screen_active());
    lv_obj_scroll_to_view_recursive(s_satochip_path_textarea, LV_ANIM_OFF);
  }
}

static void satochip_tool_focus_pin_cb(lv_event_t *event) {
  lv_event_code_t code = lv_event_get_code(event);
  if (s_satochip_pin_input.keyboard && s_satochip_pin_input.textarea) {
    lv_obj_clear_flag(s_satochip_pin_input.keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_satochip_pin_input.keyboard);
    lv_keyboard_set_textarea(s_satochip_pin_input.keyboard,
                             s_satochip_pin_input.textarea);
  }
  if (code != LV_EVENT_FOCUSED && s_satochip_pin_input.input_group &&
      s_satochip_pin_input.textarea)
    lv_group_focus_obj(s_satochip_pin_input.textarea);
  if (s_satochip_pin_input.textarea) {
    lv_obj_update_layout(lv_screen_active());
    lv_obj_scroll_to_view_recursive(s_satochip_pin_input.textarea, LV_ANIM_OFF);
  }
}

static void create_satochip_tool_block(lv_obj_t *parent,
                                       const signer_feature_t *feature,
                                       satochip_tool_mode_t mode) {
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? shell_feature_title(feature) : "Satochip", true,
              main_color());

  s_satochip_pin_flow = SATOCHIP_PIN_FLOW_TOOL;
  s_satochip_tool_mode = mode;
  snprintf(s_satochip_pending_return_id, sizeof(s_satochip_pending_return_id),
           "%s", feature && feature->parent_id ? feature->parent_id
                                                : "smartcard_tools");
  satochip_pin_input_cleanup();

  if (mode == SATOCHIP_TOOL_PATH_ADDRESS) {
    create_text(panel, "Path", false, highlight_color());
    s_satochip_path_textarea = lv_textarea_create(panel);
    lv_obj_set_width(s_satochip_path_textarea, LV_PCT(100));
    lv_obj_set_height(s_satochip_path_textarea, 64);
    lv_textarea_set_one_line(s_satochip_path_textarea, true);
    lv_textarea_set_placeholder_text(s_satochip_path_textarea,
                                     "m/44'/60'/0'/0/0");
    theme_apply_textarea(s_satochip_path_textarea, false);
    lv_textarea_set_text(s_satochip_path_textarea, "m/44'/60'/0'/0/0");
    lv_obj_set_style_bg_color(s_satochip_path_textarea, bg_color(), 0);
    lv_obj_set_style_bg_opa(s_satochip_path_textarea, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(s_satochip_path_textarea, main_color(), 0);
    lv_obj_set_style_text_font(s_satochip_path_textarea, theme_font_small(), 0);
    lv_obj_set_style_border_color(s_satochip_path_textarea, highlight_color(),
                                  0);
    lv_obj_set_style_border_width(s_satochip_path_textarea, 2, 0);
    lv_obj_set_style_radius(s_satochip_path_textarea, 8, 0);
    lv_obj_set_style_pad_top(s_satochip_path_textarea, 8, 0);
    lv_obj_set_style_pad_bottom(s_satochip_path_textarea, 8, 0);
    lv_obj_set_style_pad_left(s_satochip_path_textarea, 12, 0);
    lv_obj_set_style_pad_right(s_satochip_path_textarea, 12, 0);
    ui_textarea_enable_safe_keyboard_shortcuts(s_satochip_path_textarea);
    lv_obj_add_event_cb(s_satochip_path_textarea, satochip_tool_focus_path_cb,
                        LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(s_satochip_path_textarea, satochip_tool_focus_path_cb,
                        LV_EVENT_CLICKED, NULL);
  }

  create_text(panel, "Smartcard PIN", false, highlight_color());
  ui_text_input_create(&s_satochip_pin_input, panel, "Smartcard PIN", true,
                       satochip_pin_ready_cb);
  s_satochip_pin_input_active = true;
  if (s_satochip_path_textarea && s_satochip_pin_input.input_group)
    lv_group_add_obj(s_satochip_pin_input.input_group,
                     s_satochip_path_textarea);
  if (s_satochip_pin_input.keyboard)
    lv_obj_add_event_cb(s_satochip_pin_input.keyboard, satochip_pin_cancel_cb,
                        LV_EVENT_CANCEL, NULL);
  if (s_satochip_pin_input.textarea) {
    lv_obj_add_event_cb(s_satochip_pin_input.textarea, satochip_pin_cancel_cb,
                        LV_EVENT_CANCEL, NULL);
    lv_obj_add_event_cb(s_satochip_pin_input.textarea,
                        satochip_tool_focus_pin_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(s_satochip_pin_input.textarea,
                        satochip_tool_focus_pin_cb, LV_EVENT_CLICKED, NULL);
  }
  create_action_button(panel, "Start Reading", satochip_pin_ready_cb, NULL, true);
  create_action_button(panel, i18n_tr_or("common.back", "Back"),
                       satochip_pin_back_cb, NULL, false);
}

static void create_btc_wallet_block(lv_obj_t *parent,
                                    const signer_feature_t *feature) {
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? shell_feature_title(feature) : "Bitcoin Wallet",
              true, main_color());
  create_nav_button(panel, "Export Public Key", "legacy_public_key", true);
  create_nav_button(panel, "Derive Address", "custom_derivation", true);
}

static void create_direct_input_block(lv_obj_t *parent,
                                      const signer_feature_t *feature,
                                      const char *body) {
  (void)body;
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_nav_button(panel, "Start Input", feature ? feature->id : "home", true);
}

static const char *wallet_network_name(wallet_network_t network) {
  return network == WALLET_NETWORK_TESTNET ? "Testnet" : "Mainnet";
}

static const char *wallet_policy_name(wallet_policy_t policy) {
  return policy == WALLET_POLICY_MULTISIG ? "Multisig" : "Single-sig";
}

static void update_wallet_settings_label(lv_obj_t *label) {
  if (!label)
    return;

  char text[192];
  snprintf(text, sizeof(text),
           "Default network: %s\nDefault policy: %s\nPermissive signing: %s",
           wallet_network_name(settings_get_default_network()),
           wallet_policy_name(settings_get_default_policy()),
           settings_get_permissive_signing() ? "On" : "Off");
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(
      label, settings_get_permissive_signing() ? error_color() : yes_color(),
      0);
}

static void wallet_settings_event_cb(lv_event_t *event) {
  const char *action = (const char *)lv_event_get_user_data(event);

  if (!action)
    return;

  if (strcmp(action, "mainnet") == 0) {
    (void)settings_set_default_network(WALLET_NETWORK_MAINNET);
  } else if (strcmp(action, "testnet") == 0) {
    (void)settings_set_default_network(WALLET_NETWORK_TESTNET);
  } else if (strcmp(action, "singlesig") == 0) {
    (void)settings_set_default_policy(WALLET_POLICY_SINGLESIG);
  } else if (strcmp(action, "multisig") == 0) {
    (void)settings_set_default_policy(WALLET_POLICY_MULTISIG);
  }

  update_wallet_settings_label(s_wallet_settings_status_label);
}

static void strict_signing_event_cb(lv_event_t *event) {
  lv_obj_t *label = (lv_obj_t *)lv_event_get_user_data(event);
  (void)settings_set_permissive_signing(false);
  update_wallet_settings_label(label);
}

static void create_wallet_settings_block(lv_obj_t *parent) {
  lv_obj_t *panel =
      create_panel(parent, cyan_color(), max_i(12, theme_get_small_padding()));
  create_text(panel, "Default Wallet", false, highlight_color());

  lv_obj_t *status = create_text(panel, "", false, main_color());
  s_wallet_settings_status_label = status;
  update_wallet_settings_label(status);

  create_action_button(panel, "Default Mainnet", wallet_settings_event_cb,
                       (void *)"mainnet", false);
  create_action_button(panel, "Default Testnet", wallet_settings_event_cb,
                       (void *)"testnet", false);
  create_action_button(panel, "Default Single-sig", wallet_settings_event_cb,
                       (void *)"singlesig", false);
  create_action_button(panel, "Default Multisig", wallet_settings_event_cb,
                       (void *)"multisig", false);
  create_action_button(panel, "Force Permissive Signing Off", strict_signing_event_cb,
                       status, true);
}

static void create_security_settings_block(lv_obj_t *parent) {
  lv_obj_t *panel =
      create_panel(parent, error_color(), max_i(12, theme_get_small_padding()));
  create_text(panel, "Security Settings Status", false, error_color());

  lv_obj_t *status = create_text(panel, "", false, main_color());
  update_wallet_settings_label(status);
  create_action_button(panel, "Force Permissive Signing Off", strict_signing_event_cb,
                       status, true);
}

static void create_status_block(lv_obj_t *parent,
                                const signer_feature_t *feature) {
  lv_obj_t *status =
      create_panel(parent, status_color(feature->status),
                   max_i(14, theme_get_default_padding() / 2));

  create_text(status, "Feature Status", false, highlight_color());

  char line[192];
  snprintf(line, sizeof(line), "%s / %s",
           signer_feature_status_name(feature->status),
           signer_feature_risk_name(feature->risk));
  create_text(status, line, true, main_color());

  create_text(status, signer_feature_status_detail(feature->status), false,
              secondary_color());
  create_text(status, signer_feature_risk_detail(feature->risk), false,
              secondary_color());
  create_text(status, signer_service_guard_for_feature(feature), false,
              risk_color(feature->risk));
  create_text(status, signer_service_next_step_for_feature(feature), false,
              main_color());
}

static void create_source_block(lv_obj_t *parent,
                                const signer_feature_t *feature) {
  (void)parent;
  (void)feature;
}

static void create_service_block(lv_obj_t *parent) {
  lv_obj_t *panel =
      create_panel(parent, disabled_color(), max_i(12, theme_get_small_padding()));

  create_text(panel, "Enabled Services", false, highlight_color());
  for (size_t i = 0; i < signer_service_status_count(); i++) {
    const signer_service_status_t *service = signer_service_status_at(i);
    if (!service)
      continue;
    if (service->state != SIGNER_SERVICE_READY)
      continue;

    char line[224];
    snprintf(line, sizeof(line), "%s: %s\n%s", service->title,
             signer_service_state_name(service->state), service->summary);
    create_text(panel, line, false, yes_color());
  }
}

static void create_progress_block(lv_obj_t *parent) {
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(12, theme_get_small_padding()));

  char line[224];
  snprintf(line, sizeof(line),
           "Available features: %zu\nDevice checks: %zu\n\nHome keeps only common entries. Device checks collect hardware status.",
           signer_feature_count_by_status(SIGNER_FEATURE_VERIFIED),
           signer_feature_count_by_status(SIGNER_FEATURE_HARDWARE_WIRED));

  create_text(panel, "Available Feature Scope", false, highlight_color());
  create_text(panel, line, false, main_color());
}

static void create_hardware_snapshot_block(lv_obj_t *parent,
                                           const char *title) {
  lv_obj_t *panel =
      create_panel(parent, cyan_color(), max_i(12, theme_get_small_padding()));
  create_text(panel, title ? title : "Hardware Status", false, highlight_color());

  char text[512];
  signer_hardware_probe_format_snapshot(text, sizeof(text));
  create_text(panel, text, false, main_color());
}

static uint32_t s_touch_count;

static lv_obj_t *s_smartcard_probe_progress_dialog;
static lv_timer_t *s_smartcard_probe_poll_timer;
static TaskHandle_t s_smartcard_probe_task_handle;
static volatile bool s_smartcard_probe_task_done;
static volatile bool s_smartcard_probe_task_with_caps;
static esp_err_t s_smartcard_probe_task_err = ESP_OK;

static lv_obj_t *s_satochip_status_progress_dialog;
static lv_timer_t *s_satochip_status_poll_timer;
static TaskHandle_t s_satochip_status_task_handle;
static volatile bool s_satochip_status_task_done;
static volatile bool s_satochip_status_task_with_caps;
static esp_err_t s_satochip_status_task_err = ESP_OK;
static bool s_satochip_status_app_selected;
static EXT_RAM_BSS_ATTR char s_satochip_status_text[768];

static void refresh_smartcard_label(lv_obj_t *label) {
  if (!label)
    return;

  char text[1536];
  smartcard_transport_format_report(text, sizeof(text));
  lv_label_set_text(label, text);

  lv_color_t color = main_color();
  smartcard_transport_t active = smartcard_transport_active();
  if (active == SMARTCARD_TRANSPORT_USB_CCID ||
      active == SMARTCARD_TRANSPORT_NFC_PN5180) {
    color = yes_color();
  }
  if (s_smartcard_probe_task_err != ESP_OK) {
    color = error_color();
  }
  lv_obj_set_style_text_color(label, color, 0);
}

static void smartcard_probe_task(void *arg) {
  (void)arg;
  const bool delete_with_caps = s_smartcard_probe_task_with_caps;
  s_smartcard_probe_task_err = smartcard_transport_probe(30000);
  __atomic_store_n(&s_smartcard_probe_task_done, true, __ATOMIC_RELEASE);
  if (delete_with_caps) {
    vTaskDeleteWithCaps(NULL);
    return;
  }
  vTaskDelete(NULL);
}

static void smartcard_probe_finish_ui(void) {
  if (s_smartcard_probe_progress_dialog) {
    lv_obj_del(s_smartcard_probe_progress_dialog);
    s_smartcard_probe_progress_dialog = NULL;
  }
  s_smartcard_probe_task_handle = NULL;
  s_smartcard_probe_task_with_caps = false;
  if (strcmp(s_current_screen_id, "smartcard_probe") == 0)
    (void)signer_shell_show_screen("smartcard_probe");
}

static void smartcard_probe_poll_cb(lv_timer_t *timer) {
  (void)timer;
  if (!__atomic_load_n(&s_smartcard_probe_task_done, __ATOMIC_ACQUIRE))
    return;

  if (s_smartcard_probe_poll_timer) {
    lv_timer_del(s_smartcard_probe_poll_timer);
    s_smartcard_probe_poll_timer = NULL;
  }
  smartcard_probe_finish_ui();
}

static void smartcard_probe_start(lv_obj_t *label) {
#ifdef SIMULATOR
  if (label) {
    lv_label_set_text(label, i18n_tr_or("dialog.processing", "Checking"));
    lv_obj_set_style_text_color(label, highlight_color(), 0);
  }
  s_smartcard_probe_task_err = smartcard_transport_probe(30000);
  refresh_smartcard_label(label);
  return;
#endif

  if (smartcard_task_busy()) {
    dialog_show_error("Smartcard busy", NULL, 1600);
    return;
  }

  if (label) {
    lv_label_set_text(label, i18n_tr_or("dialog.processing", "Checking"));
    lv_obj_set_style_text_color(label, highlight_color(), 0);
  }
  __atomic_store_n(&s_smartcard_probe_task_done, false, __ATOMIC_RELEASE);
  s_smartcard_probe_task_err = ESP_ERR_INVALID_STATE;
  s_smartcard_probe_task_with_caps = false;
  s_smartcard_probe_progress_dialog =
      dialog_show_progress("Smartcard Probe", "Checking",
                           DIALOG_STYLE_OVERLAY);
  lv_refr_now(NULL);

  BaseType_t ok = xTaskCreatePinnedToCore(
      smartcard_probe_task, "card_probe", SATOCHIP_CONNECT_TASK_STACK_SIZE,
      NULL, 4, &s_smartcard_probe_task_handle, 1);
  if (ok != pdPASS) {
    s_smartcard_probe_task_with_caps = true;
    ok = xTaskCreatePinnedToCoreWithCaps(
        smartcard_probe_task, "card_probe", SATOCHIP_CONNECT_TASK_STACK_SIZE,
        NULL, 4, &s_smartcard_probe_task_handle, 1,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
  if (ok != pdPASS) {
    s_smartcard_probe_task_handle = NULL;
    s_smartcard_probe_task_with_caps = false;
    if (s_smartcard_probe_progress_dialog) {
      lv_obj_del(s_smartcard_probe_progress_dialog);
      s_smartcard_probe_progress_dialog = NULL;
    }
    dialog_show_error("Probe task failed to start", NULL, 0);
    return;
  }
  s_smartcard_probe_poll_timer =
      lv_timer_create(smartcard_probe_poll_cb, 100, NULL);
}

static void smartcard_probe_event_cb(lv_event_t *event) {
  lv_obj_t *label = (lv_obj_t *)lv_event_get_user_data(event);
  smartcard_probe_start(label);
}

#ifndef SIMULATOR
static void smartcard_web3_scan_event_cb(lv_event_t *event) {
  (void)event;
  lv_obj_t *root = legacy_wallet_prepare_root();
  scan_page_create_smartcard_web3(root, legacy_wallet_return_to_signer_cb);
  scan_page_show();
}
#endif

static void legacy_wallet_launch_unified_scan_for_shell(void) {
#ifndef SIMULATOR
  lv_obj_t *root = legacy_wallet_prepare_root();
  scan_page_create_unified(root, legacy_wallet_return_to_signer_cb);
  scan_page_show();
#else
  (void)signer_shell_show_screen("sign_psbt_qr");
#endif
}

static void satochip_status_event_cb(lv_event_t *event) {
  lv_obj_t *label = (lv_obj_t *)lv_event_get_user_data(event);
  satochip_status_start(label);
}

static void satochip_status_task(void *arg) {
  (void)arg;
  const bool delete_with_caps = s_satochip_status_task_with_caps;
  s_satochip_status_task_err = ESP_ERR_INVALID_STATE;
  s_satochip_status_app_selected = false;
  s_satochip_status_text[0] = '\0';

  smartcard_satochip_status_t *status = calloc(1, sizeof(*status));
  if (!status) {
    s_satochip_status_task_err = ESP_ERR_NO_MEM;
    snprintf(s_satochip_status_text, sizeof(s_satochip_status_text),
             "Out of memory. Read canceled.");
    goto status_done;
  }

  s_satochip_status_task_err = smartcard_satochip_read_status(status, 30000);
  s_satochip_status_app_selected = status->app_selected;
  smartcard_satochip_format_status(status, s_satochip_status_text,
                                   sizeof(s_satochip_status_text));
  secure_memzero(status, sizeof(*status));
  free(status);

status_done:
  __atomic_store_n(&s_satochip_status_task_done, true, __ATOMIC_RELEASE);
  if (delete_with_caps) {
    vTaskDeleteWithCaps(NULL);
    return;
  }
  vTaskDelete(NULL);
}

static void satochip_status_finish_ui(void) {
  if (s_satochip_status_progress_dialog) {
    lv_obj_del(s_satochip_status_progress_dialog);
    s_satochip_status_progress_dialog = NULL;
  }
  s_satochip_status_task_handle = NULL;
  s_satochip_status_task_with_caps = false;
  if (strcmp(s_current_screen_id, "smartcard_web3_scan") == 0 ||
      strcmp(s_current_screen_id, "web3_satochip") == 0)
    (void)signer_shell_show_screen(s_current_screen_id);
}

static void satochip_status_poll_cb(lv_timer_t *timer) {
  (void)timer;
  if (!__atomic_load_n(&s_satochip_status_task_done, __ATOMIC_ACQUIRE))
    return;

  if (s_satochip_status_poll_timer) {
    lv_timer_del(s_satochip_status_poll_timer);
    s_satochip_status_poll_timer = NULL;
  }
  satochip_status_finish_ui();
}

static void satochip_status_start(lv_obj_t *label) {
#ifdef SIMULATOR
  smartcard_satochip_status_t status;
  memset(&status, 0, sizeof(status));
  esp_err_t err = smartcard_satochip_read_status(&status, 30000);
  smartcard_satochip_format_status(&status, s_satochip_status_text,
                                   sizeof(s_satochip_status_text));
  s_satochip_status_task_err = err;
  s_satochip_status_app_selected = status.app_selected;
  if (label) {
    lv_label_set_text(label, s_satochip_status_text);
    lv_obj_set_style_text_color(
        label, err == ESP_OK && status.app_selected ? yes_color()
                                                    : error_color(),
        0);
  }
  secure_memzero(&status, sizeof(status));
  return;
#endif

  if (smartcard_task_busy()) {
    dialog_show_error("Smartcard busy", NULL, 1600);
    return;
  }

  if (label) {
    lv_label_set_text(label, i18n_tr_or("dialog.processing", "Reading"));
    lv_obj_set_style_text_color(label, highlight_color(), 0);
  }
  s_satochip_status_text[0] = '\0';
  s_satochip_status_app_selected = false;
  __atomic_store_n(&s_satochip_status_task_done, false, __ATOMIC_RELEASE);
  s_satochip_status_task_err = ESP_ERR_INVALID_STATE;
  s_satochip_status_task_with_caps = false;
  s_satochip_status_progress_dialog =
      dialog_show_progress("Reading Status", "Reading",
                           DIALOG_STYLE_OVERLAY);
  lv_refr_now(NULL);

  BaseType_t ok = xTaskCreatePinnedToCore(
      satochip_status_task, "card_status", SATOCHIP_CONNECT_TASK_STACK_SIZE,
      NULL, 4, &s_satochip_status_task_handle, 1);
  if (ok != pdPASS) {
    s_satochip_status_task_with_caps = true;
    ok = xTaskCreatePinnedToCoreWithCaps(
        satochip_status_task, "card_status", SATOCHIP_CONNECT_TASK_STACK_SIZE,
        NULL, 4, &s_satochip_status_task_handle, 1,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
  if (ok != pdPASS) {
    s_satochip_status_task_handle = NULL;
    s_satochip_status_task_with_caps = false;
    if (s_satochip_status_progress_dialog) {
      lv_obj_del(s_satochip_status_progress_dialog);
      s_satochip_status_progress_dialog = NULL;
    }
    dialog_show_error("Read task failed to start", NULL, 0);
    return;
  }
  s_satochip_status_poll_timer =
      lv_timer_create(satochip_status_poll_cb, 100, NULL);
}

static void create_satochip_web3_block(lv_obj_t *parent) {
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(12, theme_get_small_padding()));
  create_text(panel, "Satochip", true, main_color());

  lv_obj_t *result = create_text(panel, "Waiting", false, main_color());
  if (s_satochip_status_text[0]) {
    lv_label_set_text(result, s_satochip_status_text);
    lv_obj_set_style_text_color(
        result,
        s_satochip_status_task_err == ESP_OK && s_satochip_status_app_selected
            ? yes_color()
            : error_color(),
        0);
  }
  create_action_button(panel, "Read Status", satochip_status_event_cb, result,
                       true);
#ifndef SIMULATOR
  create_action_button(panel, "Scan Web3", smartcard_web3_scan_event_cb, NULL,
                       true);
#endif
}

static void create_smartcard_probe_block(lv_obj_t *parent) {
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(12, theme_get_small_padding()));
  create_text(panel, "Smartcard Probe", false, highlight_color());

  lv_obj_t *result = create_text(panel, "Waiting", false, main_color());
  create_action_button(panel, "Start Probe", smartcard_probe_event_cb, result,
                       true);
  refresh_smartcard_label(result);
}

#define SATOCHIP_MAINT_TASK_STACK_SIZE 32768

typedef enum {
  SATOCHIP_MAINT_NONE = 0,
  SATOCHIP_MAINT_SETUP_PIN,
  SATOCHIP_MAINT_CHANGE_PIN,
  SATOCHIP_MAINT_UNBLOCK_PIN,
  SATOCHIP_MAINT_SET_LABEL,
  SATOCHIP_MAINT_NFC_POLICY,
  SATOCHIP_MAINT_FEATURE_POLICY,
  SATOCHIP_MAINT_RESET_SEED,
  SATOCHIP_MAINT_RESET_FACTORY,
  SATOCHIP_MAINT_EXPORT_AUTHENTIKEY,
  SATOCHIP_MAINT_IMPORT_NDEF_AUTHENTIKEY,
  SATOCHIP_MAINT_IMPORT_TRUSTED_PUBKEY,
  SATOCHIP_MAINT_EXPORT_TRUSTED_PUBKEY,
  SATOCHIP_MAINT_SATOCHIP_WRITE_MNEMONIC,
  SATOCHIP_MAINT_SET_2FA_KEY,
  SATOCHIP_MAINT_RESET_2FA_KEY,
  SATOCHIP_MAINT_LOGOUT_ALL,
  SATOCHIP_MAINT_SEEDKEEPER_STATUS,
  SATOCHIP_MAINT_SEEDKEEPER_FREE_SPACE,
  SATOCHIP_MAINT_SEEDKEEPER_LIST,
  SATOCHIP_MAINT_SEEDKEEPER_LOGS,
  SATOCHIP_MAINT_SEEDKEEPER_CREATE_MNEMONIC,
  SATOCHIP_MAINT_SEEDKEEPER_WRITE_MNEMONIC,
  SATOCHIP_MAINT_SEEDKEEPER_VIEW_MNEMONIC,
  SATOCHIP_MAINT_SEEDKEEPER_LOAD_MNEMONIC,
  SATOCHIP_MAINT_SEEDKEEPER_SAVE_SECRET,
  SATOCHIP_MAINT_SEEDKEEPER_LOAD_DESCRIPTOR,
  SATOCHIP_MAINT_SEEDKEEPER_SAVE_DESCRIPTOR,
  SATOCHIP_MAINT_SEEDKEEPER_CLONE,
  SATOCHIP_MAINT_SEEDKEEPER_SETUP_PIN,
  SATOCHIP_MAINT_SEEDKEEPER_CHANGE_PIN,
  SATOCHIP_MAINT_SEEDKEEPER_RESET,
  SATOCHIP_MAINT_SEEDKEEPER_RESET_PIN_STEP,
  SATOCHIP_MAINT_SEEDKEEPER_RESET_PUK_STEP,
  SATOCHIP_MAINT_SEEDKEEPER_GENERATE_MASTERSEED,
  SATOCHIP_MAINT_SEEDKEEPER_GENERATE_2FA_SECRET,
  SATOCHIP_MAINT_SEEDKEEPER_DERIVE_MASTER_PASSWORD,
  SATOCHIP_MAINT_SEEDKEEPER_RESET_SECRET,
  SATOCHIP_MAINT_CERT_EXPORT,
  SATOCHIP_MAINT_CERT_IMPORT,
  SATOCHIP_MAINT_AUTHENTICITY,
} satochip_maint_mode_t;

static satochip_maint_mode_t s_satochip_maint_mode = SATOCHIP_MAINT_NONE;
static ui_text_input_t s_satochip_maint_input;
static bool s_satochip_maint_input_active;
static lv_obj_t *s_satochip_maint_extra_a;
static lv_obj_t *s_satochip_maint_extra_b;
static lv_obj_t *s_satochip_maint_extra_c;
static lv_obj_t *s_satochip_maint_progress_dialog;
static lv_timer_t *s_satochip_maint_poll_timer;
static TaskHandle_t s_satochip_maint_task_handle;
static volatile bool s_satochip_maint_task_done;
static volatile bool s_satochip_maint_task_with_caps;
static esp_err_t s_satochip_maint_task_err = ESP_OK;
static EXT_RAM_BSS_ATTR char s_satochip_maint_result[8192];
static char s_satochip_maint_pin[80];
static EXT_RAM_BSS_ATTR char s_satochip_maint_text_a[1024];
static EXT_RAM_BSS_ATTR char s_satochip_maint_text_b[4096];
static EXT_RAM_BSS_ATTR char s_satochip_maint_text_c[4096];
static EXT_RAM_BSS_ATTR char s_satochip_seedkeeper_scanned_secret[4096];
static EXT_RAM_BSS_ATTR char s_satochip_seedkeeper_export_secret_qr[4096];
static char s_satochip_seedkeeper_export_secret_qr_title[96];
static bool s_satochip_seedkeeper_export_secret_qr_pending;
static smartcard_seedkeeper_header_list_t s_satochip_seedkeeper_last_list;
static bool s_satochip_seedkeeper_last_list_valid;
static satochip_maint_mode_t s_satochip_seedkeeper_last_list_mode =
    SATOCHIP_MAINT_NONE;
typedef enum {
  SATOCHIP_SEEDKEEPER_LOOKUP_ENTER_PIN = 0,
  SATOCHIP_SEEDKEEPER_LOOKUP_LIST,
  SATOCHIP_SEEDKEEPER_LOOKUP_ITEM,
  SATOCHIP_SEEDKEEPER_LOOKUP_EDIT,
} satochip_seedkeeper_lookup_stage_t;

typedef enum {
  SATOCHIP_SEEDKEEPER_ITEM_OP_VIEW = 0,
  SATOCHIP_SEEDKEEPER_ITEM_OP_LOAD,
  SATOCHIP_SEEDKEEPER_ITEM_OP_DELETE,
  SATOCHIP_SEEDKEEPER_ITEM_OP_UPDATE,
  SATOCHIP_SEEDKEEPER_ITEM_OP_EXPORT,
} satochip_seedkeeper_item_op_t;

static satochip_seedkeeper_lookup_stage_t s_satochip_seedkeeper_lookup_stage =
    SATOCHIP_SEEDKEEPER_LOOKUP_ENTER_PIN;
static satochip_seedkeeper_item_op_t s_satochip_seedkeeper_item_op =
    SATOCHIP_SEEDKEEPER_ITEM_OP_VIEW;
static uint16_t s_satochip_seedkeeper_selected_sid;
static smartcard_seedkeeper_header_t s_satochip_seedkeeper_selected_header;
static bool s_satochip_seedkeeper_selected_header_valid;
static char s_satochip_seedkeeper_cached_pin[80];
static bool s_satochip_seedkeeper_ephemeral_create;
static void satochip_seedkeeper_lookup_state_reset(void);
static void satochip_seedkeeper_render_lookup_content(lv_obj_t *panel);
static void satochip_seedkeeper_lookup_select_item(uint16_t sid);
static void satochip_seedkeeper_select_item_cb(lv_event_t *event);
static void satochip_seedkeeper_lookup_back_cb(lv_event_t *event);
static void satochip_seedkeeper_lookup_edit_back_cb(lv_event_t *event);
static void satochip_seedkeeper_lookup_item_action_cb(lv_event_t *event);
static void satochip_seedkeeper_lookup_edit_cb(lv_event_t *event);
static void satochip_maint_start(void);
static void satochip_maint_start_event_cb(lv_event_t *event);
static void satochip_seedkeeper_reset_pin_step_event_cb(lv_event_t *event);
static void satochip_seedkeeper_reset_puk_step_event_cb(lv_event_t *event);
static void seedkeeper_secret_qr_state_reset(void);
static void seedkeeper_secret_qr_show_if_pending(void);
static void seedkeeper_secret_scan_event_cb(lv_event_t *event);
static void satochip_maint_attach_primary_input(lv_obj_t *parent,
                                                const char *placeholder,
                                                bool password,
                                                bool multiline,
                                                size_t max_len,
                                                const char *initial);
static lv_obj_t *satochip_maint_attach_extra_field(lv_obj_t *parent,
                                                   const char *label_text,
                                                   const char *placeholder,
                                                   const char *default_text,
                                                   bool password_mode,
                                                   bool multiline,
                                                   size_t max_len,
                                                   lv_obj_t **slot);
static void satochip_hex_bytes_upper(const uint8_t *data, size_t len, char *out,
                                     size_t out_len);

static bool smartcard_task_busy(void) {
  return s_satochip_task_handle || s_satochip_tool_task_handle ||
         s_card_info_task_handle || s_satochip_maint_task_handle ||
         s_smartcard_probe_task_handle || s_satochip_status_task_handle;
}

#define SEEDKEEPER_TYPE_MASTERSEED 0x10
#define SEEDKEEPER_TYPE_BIP39_MNEMONIC 0x30
#define SEEDKEEPER_TYPE_SECRET_KEY 0x80
#define SEEDKEEPER_TYPE_PASSWORD 0x90
#define SEEDKEEPER_TYPE_MASTER_PASSWORD 0x91
#define SEEDKEEPER_TYPE_2FA_SECRET 0xB0
#define SEEDKEEPER_TYPE_DATA 0xC0
#define SEEDKEEPER_SUBTYPE_GENERIC_TEXT 0x02
#define SEEDKEEPER_ORIGIN_PLAIN_IMPORT 0x01
#define SEEDKEEPER_ORIGIN_GENERATED_ON_CARD 0x03
#define SEEDKEEPER_EXPORT_PLAINTEXT_ALLOWED 0x01
#define SEEDKEEPER_MNEMONIC_MAX_BYTES 240
#define SEEDKEEPER_LABEL_MAX_BYTES 64

static void satochip_maint_focus_primary_cb(lv_event_t *event) {
  lv_event_code_t code = lv_event_get_code(event);
  if (s_satochip_maint_input.keyboard && s_satochip_maint_input.textarea) {
    lv_obj_clear_flag(s_satochip_maint_input.keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_satochip_maint_input.keyboard);
    lv_keyboard_set_textarea(s_satochip_maint_input.keyboard,
                             s_satochip_maint_input.textarea);
  }
  if (code != LV_EVENT_FOCUSED && s_satochip_maint_input.input_group &&
      s_satochip_maint_input.textarea)
    lv_group_focus_obj(s_satochip_maint_input.textarea);
  if (s_satochip_maint_input.textarea) {
    lv_obj_update_layout(lv_screen_active());
    lv_obj_scroll_to_view_recursive(s_satochip_maint_input.textarea,
                                    LV_ANIM_OFF);
  }
}

static void satochip_maint_focus_extra_cb(lv_event_t *event) {
  lv_event_code_t code = lv_event_get_code(event);
  lv_obj_t *textarea = (lv_obj_t *)lv_event_get_user_data(event);
  if (s_satochip_maint_input.keyboard && textarea) {
    lv_obj_clear_flag(s_satochip_maint_input.keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_satochip_maint_input.keyboard);
    lv_keyboard_set_textarea(s_satochip_maint_input.keyboard, textarea);
  }
  if (code != LV_EVENT_FOCUSED && s_satochip_maint_input.input_group &&
      textarea)
    lv_group_focus_obj(textarea);
  if (textarea) {
    lv_obj_update_layout(lv_screen_active());
    lv_obj_scroll_to_view_recursive(textarea, LV_ANIM_OFF);
  }
}

static void satochip_maint_destroy_textarea(lv_obj_t **slot) {
  if (!slot || !*slot)
    return;
  lv_obj_t *textarea = *slot;
  *slot = NULL;
  if (!lv_obj_is_valid(textarea))
    return;
  const char *text = lv_textarea_get_text(textarea);
  if (text && text[0]) {
    secure_memzero((void *)text, strlen(text));
    lv_textarea_set_text(textarea, "");
  }
  lv_obj_del(textarea);
}

static void satochip_maint_input_cleanup(void) {
  if (s_satochip_maint_input_active) {
    ui_text_input_destroy(&s_satochip_maint_input);
    memset(&s_satochip_maint_input, 0, sizeof(s_satochip_maint_input));
    s_satochip_maint_input_active = false;
  }
  satochip_maint_destroy_textarea(&s_satochip_maint_extra_a);
  satochip_maint_destroy_textarea(&s_satochip_maint_extra_b);
  satochip_maint_destroy_textarea(&s_satochip_maint_extra_c);
}

static void satochip_maint_hide_inputs(void) {
  ui_text_input_hide(&s_satochip_maint_input);
  if (s_satochip_maint_extra_a)
    lv_obj_add_flag(s_satochip_maint_extra_a, LV_OBJ_FLAG_HIDDEN);
  if (s_satochip_maint_extra_b)
    lv_obj_add_flag(s_satochip_maint_extra_b, LV_OBJ_FLAG_HIDDEN);
  if (s_satochip_maint_extra_c)
    lv_obj_add_flag(s_satochip_maint_extra_c, LV_OBJ_FLAG_HIDDEN);
}

static void shell_transient_state_reset(void) {
  web3_qr_state_reset();
  satochip_pin_input_cleanup();
  satochip_maint_input_cleanup();
  bool seedkeeper_lookup_screen =
      strcmp(s_current_screen_id, "smartcard_seedkeeper_view_mnemonic") == 0 ||
      strcmp(s_current_screen_id, "smartcard_seedkeeper_load_mnemonic") == 0 ||
      strcmp(s_current_screen_id, "load_seedkeeper_mnemonic") == 0;
  bool seedkeeper_lookup_mode =
      s_satochip_maint_mode == SATOCHIP_MAINT_SEEDKEEPER_VIEW_MNEMONIC ||
      s_satochip_maint_mode == SATOCHIP_MAINT_SEEDKEEPER_LOAD_MNEMONIC;
  if (!seedkeeper_lookup_screen && seedkeeper_lookup_mode)
    satochip_seedkeeper_lookup_state_reset();
}

static void satochip_maint_result_clear(void) {
  s_satochip_maint_result[0] = '\0';
  s_satochip_maint_task_err = ESP_ERR_INVALID_STATE;
}

static void seedkeeper_secret_qr_state_reset(void) {
  s_satochip_seedkeeper_export_secret_qr_pending = false;
  secure_memzero(s_satochip_seedkeeper_export_secret_qr,
                 sizeof(s_satochip_seedkeeper_export_secret_qr));
  secure_memzero(s_satochip_seedkeeper_export_secret_qr_title,
                 sizeof(s_satochip_seedkeeper_export_secret_qr_title));
}

#ifndef SIMULATOR
static void seedkeeper_secret_qr_return_cb(void) {
  qr_viewer_page_destroy();
  seedkeeper_secret_qr_state_reset();
  (void)signer_shell_show_screen(s_current_screen_id);
}

static void seedkeeper_secret_qr_show_if_pending(void) {
  if (!s_satochip_seedkeeper_export_secret_qr_pending ||
      !s_satochip_seedkeeper_export_secret_qr[0])
    return;
  const char *title = s_satochip_seedkeeper_export_secret_qr_title[0]
                          ? s_satochip_seedkeeper_export_secret_qr_title
                          : "SeedKeeper Secret";
  if (!qr_viewer_page_create(lv_screen_active(),
                             s_satochip_seedkeeper_export_secret_qr, title,
                             seedkeeper_secret_qr_return_cb)) {
    dialog_show_error("Secret QR display failed", NULL, 2000);
    seedkeeper_secret_qr_state_reset();
    return;
  }
  qr_viewer_page_show();
}

static void seedkeeper_secret_return_from_qr_cb(void) {
  size_t content_len = 0;
  char *scanned = qr_scanner_get_completed_content_with_len(&content_len);
  qr_scanner_page_destroy();
  secure_memzero(s_satochip_seedkeeper_scanned_secret,
                 sizeof(s_satochip_seedkeeper_scanned_secret));
  if (scanned && content_len > 0) {
    size_t copy_len = content_len;
    if (copy_len >= sizeof(s_satochip_seedkeeper_scanned_secret))
      copy_len = sizeof(s_satochip_seedkeeper_scanned_secret) - 1;
    memcpy(s_satochip_seedkeeper_scanned_secret, scanned, copy_len);
    s_satochip_seedkeeper_scanned_secret[copy_len] = '\0';
  }
  free(scanned);
  (void)signer_shell_show_screen("smartcard_seedkeeper_save_secret");
}
#else
static void seedkeeper_secret_qr_show_if_pending(void) {
  seedkeeper_secret_qr_state_reset();
}
#endif

static void seedkeeper_secret_scan_event_cb(lv_event_t *event) {
  (void)event;
#ifdef SIMULATOR
  snprintf(s_satochip_seedkeeper_scanned_secret,
           sizeof(s_satochip_seedkeeper_scanned_secret),
           "simulator scanned secret");
  (void)signer_shell_show_screen("smartcard_seedkeeper_save_secret");
#else
  lv_obj_t *root = legacy_wallet_prepare_root();
  qr_scanner_page_create(root, seedkeeper_secret_return_from_qr_cb);
  qr_scanner_page_show();
#endif
}

static void satochip_maint_log_stack_watermark(const char *stage) {
  ESP_LOGI(TAG, "satochip maint %s stack watermark=%u words",
           stage ? stage : "?", (unsigned)uxTaskGetStackHighWaterMark(NULL));
}

static const char *seedkeeper_wordlist_name_from_byte(uint8_t wordlist_byte) {
  switch (wordlist_byte) {
  case 0x00:
    return "english";
  case 0x01:
    return "japanese";
  case 0x02:
    return "korean";
  case 0x03:
    return "spanish";
  case 0x04:
    return "chinese_simplified";
  case 0x05:
    return "chinese_traditional";
  case 0x06:
    return "french";
  case 0x07:
    return "italian";
  case 0x08:
    return "czech";
  case 0x09:
    return "portuguese";
  default:
    return NULL;
  }
}

static char *dup_wally_string_local(char *wally_str) {
  if (!wally_str)
    return NULL;
  char *copy = strdup(wally_str);
  wally_free_string(wally_str);
  return copy;
}

static bool seedkeeper_header_is_loadable_mnemonic(
    const smartcard_seedkeeper_header_t *header) {
  return header &&
         ((header->type == SEEDKEEPER_TYPE_BIP39_MNEMONIC) ||
          (header->type == SEEDKEEPER_TYPE_MASTERSEED &&
           header->subtype == 0x01));
}

static bool seedkeeper_header_is_generic_secret(
    const smartcard_seedkeeper_header_t *header) {
  return header && header->type == SEEDKEEPER_TYPE_DATA;
}

static bool seedkeeper_header_is_password(
    const smartcard_seedkeeper_header_t *header) {
  return header && header->type == SEEDKEEPER_TYPE_PASSWORD;
}

static bool seedkeeper_header_is_viewable(
    const smartcard_seedkeeper_header_t *header) {
  return seedkeeper_header_is_loadable_mnemonic(header) ||
         seedkeeper_header_is_generic_secret(header) ||
         seedkeeper_header_is_password(header);
}

static const char *seedkeeper_header_kind_label(
  const smartcard_seedkeeper_header_t *header) {
  if (seedkeeper_header_is_password(header))
    return "Password";
  if (seedkeeper_header_is_generic_secret(header))
    return "Secret";
  if (seedkeeper_header_is_loadable_mnemonic(header))
    return "Mnemonic";
  return "Item";
}

static bool seedkeeper_fingerprint_from_mnemonic(
    const char *mnemonic, const char *passphrase, uint8_t out[4]) {
  if (!out)
    return false;
  (void)passphrase;
  unsigned char fingerprint[BIP32_KEY_FINGERPRINT_LEN];
  memset(fingerprint, 0, sizeof(fingerprint));

  if (!key_compute_mnemonic_fingerprint(fingerprint, mnemonic)) {
    secure_memzero(fingerprint, sizeof(fingerprint));
    return false;
  }

  memcpy(out, fingerprint, 4);
  secure_memzero(fingerprint, sizeof(fingerprint));
  return true;
}

static bool seedkeeper_wallet_fingerprint_hex_from_mnemonic(
    const char *mnemonic, const char *passphrase, char out[9]) {
  if (!out)
    return false;
  out[0] = '\0';
  (void)passphrase;
  return key_compute_mnemonic_fingerprint_hex(out, mnemonic);
}

static bool seedkeeper_label_is_fingerprint(const char *label) {
  if (!label || strlen(label) != 8)
    return false;
  for (size_t i = 0; i < 8; i++) {
    if (!isxdigit((unsigned char)label[i]))
      return false;
  }
  return true;
}

static void seedkeeper_format_fingerprint_hex(const uint8_t fingerprint[4],
                                              char out[9]) {
  if (!out)
    return;
  out[0] = '\0';
  if (!fingerprint)
    return;
  snprintf(out, 9, "%02x%02x%02x%02x", fingerprint[0], fingerprint[1],
           fingerprint[2], fingerprint[3]);
}

static void seedkeeper_header_mnemonic_fingerprint_hex(
    const smartcard_seedkeeper_header_t *header, char out[9]) {
  if (!out)
    return;
  out[0] = '\0';
  if (!header)
    return;
  if (seedkeeper_label_is_fingerprint(header->label)) {
    memcpy(out, header->label, 8);
    out[8] = '\0';
    for (size_t i = 0; out[i]; i++)
      out[i] = (char)tolower((unsigned char)out[i]);
    return;
  }
  seedkeeper_format_fingerprint_hex(header->fingerprint, out);
}

static void satochip_seedkeeper_store_last_list(
    const smartcard_seedkeeper_header_list_t *list,
    satochip_maint_mode_t mode) {
  secure_memzero(&s_satochip_seedkeeper_last_list,
                 sizeof(s_satochip_seedkeeper_last_list));
  if (!list || list->count == 0) {
    s_satochip_seedkeeper_last_list_valid = false;
    s_satochip_seedkeeper_last_list_mode = SATOCHIP_MAINT_NONE;
    return;
  }
  memcpy(&s_satochip_seedkeeper_last_list, list, sizeof(*list));
  s_satochip_seedkeeper_last_list_valid = true;
  s_satochip_seedkeeper_last_list_mode = mode;
}

static const smartcard_seedkeeper_header_t *
satochip_seedkeeper_find_last_header(uint16_t sid) {
  if (!s_satochip_seedkeeper_last_list_valid)
    return NULL;
  for (size_t i = 0; i < s_satochip_seedkeeper_last_list.count; i++) {
    const smartcard_seedkeeper_header_t *header =
        &s_satochip_seedkeeper_last_list.headers[i];
    if (header->id == sid)
      return header;
  }
  return NULL;
}

static void satochip_seedkeeper_lookup_state_reset(void) {
  secure_memzero(&s_satochip_seedkeeper_last_list,
                 sizeof(s_satochip_seedkeeper_last_list));
  s_satochip_seedkeeper_last_list_valid = false;
  s_satochip_seedkeeper_last_list_mode = SATOCHIP_MAINT_NONE;
  s_satochip_seedkeeper_lookup_stage = SATOCHIP_SEEDKEEPER_LOOKUP_ENTER_PIN;
  s_satochip_seedkeeper_item_op = SATOCHIP_SEEDKEEPER_ITEM_OP_VIEW;
  s_satochip_seedkeeper_selected_sid = 0;
  s_satochip_seedkeeper_selected_header_valid = false;
  secure_memzero(&s_satochip_seedkeeper_selected_header,
                 sizeof(s_satochip_seedkeeper_selected_header));
  secure_memzero(s_satochip_seedkeeper_cached_pin,
                 sizeof(s_satochip_seedkeeper_cached_pin));
}

static void satochip_seedkeeper_lookup_select_item(uint16_t sid) {
  const smartcard_seedkeeper_header_t *header =
      satochip_seedkeeper_find_last_header(sid);
  if (!header) {
    dialog_show_error("Item is no longer available", NULL, 1600);
    return;
  }
  s_satochip_seedkeeper_selected_sid = sid;
  memcpy(&s_satochip_seedkeeper_selected_header, header,
         sizeof(s_satochip_seedkeeper_selected_header));
  s_satochip_seedkeeper_selected_header_valid = true;
  s_satochip_seedkeeper_lookup_stage = SATOCHIP_SEEDKEEPER_LOOKUP_ITEM;
  s_satochip_maint_result[0] = '\0';
  (void)signer_shell_show_screen(s_current_screen_id);
}

static void satochip_seedkeeper_select_item_cb(lv_event_t *event) {
  if (!event)
    return;
  uintptr_t sid_value = (uintptr_t)lv_event_get_user_data(event);
  /* Some SeedKeeper cards can surface SID 0, so do not treat it as a null tap. */
  satochip_seedkeeper_lookup_select_item((uint16_t)sid_value);
}

static void satochip_seedkeeper_lookup_back_cb(lv_event_t *event) {
  (void)event;
  s_satochip_seedkeeper_lookup_stage = SATOCHIP_SEEDKEEPER_LOOKUP_LIST;
  s_satochip_seedkeeper_selected_sid = 0;
  s_satochip_seedkeeper_selected_header_valid = false;
  secure_memzero(&s_satochip_seedkeeper_selected_header,
                 sizeof(s_satochip_seedkeeper_selected_header));
  s_satochip_maint_result[0] = '\0';
  (void)signer_shell_show_screen(s_current_screen_id);
}

static void satochip_seedkeeper_lookup_edit_back_cb(lv_event_t *event) {
  (void)event;
  s_satochip_seedkeeper_lookup_stage = SATOCHIP_SEEDKEEPER_LOOKUP_ITEM;
  s_satochip_maint_result[0] = '\0';
  (void)signer_shell_show_screen(s_current_screen_id);
}

static bool satochip_seedkeeper_lookup_has_selected_item(void) {
  return s_satochip_seedkeeper_selected_header_valid &&
         (s_satochip_seedkeeper_lookup_stage ==
              SATOCHIP_SEEDKEEPER_LOOKUP_ITEM ||
          s_satochip_seedkeeper_lookup_stage ==
              SATOCHIP_SEEDKEEPER_LOOKUP_EDIT);
}

static void satochip_seedkeeper_lookup_item_action_cb(lv_event_t *event) {
  uintptr_t action = (uintptr_t)lv_event_get_user_data(event);
  s_satochip_seedkeeper_item_op = (satochip_seedkeeper_item_op_t)action;
  if (s_satochip_seedkeeper_item_op ==
      SATOCHIP_SEEDKEEPER_ITEM_OP_UPDATE) {
    s_satochip_seedkeeper_lookup_stage = SATOCHIP_SEEDKEEPER_LOOKUP_EDIT;
    s_satochip_maint_result[0] = '\0';
    (void)signer_shell_show_screen(s_current_screen_id);
    return;
  }
  satochip_maint_start();
}

static void satochip_seedkeeper_lookup_edit_cb(lv_event_t *event) {
  (void)event;
  s_satochip_seedkeeper_item_op = SATOCHIP_SEEDKEEPER_ITEM_OP_UPDATE;
  satochip_maint_start();
}

static void satochip_seedkeeper_render_list_entries(lv_obj_t *panel) {
  if (!panel || !s_satochip_seedkeeper_last_list_valid)
    return;
  if (s_satochip_seedkeeper_last_list_mode != s_satochip_maint_mode)
    return;
  if (s_satochip_seedkeeper_lookup_stage != SATOCHIP_SEEDKEEPER_LOOKUP_LIST)
    return;
  if (s_satochip_maint_mode != SATOCHIP_MAINT_SEEDKEEPER_VIEW_MNEMONIC &&
      s_satochip_maint_mode != SATOCHIP_MAINT_SEEDKEEPER_LOAD_MNEMONIC)
    return;

  bool has_buttons = false;
  create_text(panel, "Tap an item to open it.", false, secondary_color());

  for (size_t i = 0; i < s_satochip_seedkeeper_last_list.count; i++) {
    const smartcard_seedkeeper_header_t *header =
        &s_satochip_seedkeeper_last_list.headers[i];
    if (s_satochip_maint_mode == SATOCHIP_MAINT_SEEDKEEPER_LOAD_MNEMONIC &&
        !seedkeeper_header_is_loadable_mnemonic(header))
      continue;
    if (s_satochip_maint_mode == SATOCHIP_MAINT_SEEDKEEPER_VIEW_MNEMONIC &&
        !seedkeeper_header_is_viewable(header))
      continue;

	    char fingerprint[9];
	    seedkeeper_header_mnemonic_fingerprint_hex(header, fingerprint);
	    char label[96];
	    if (seedkeeper_header_is_loadable_mnemonic(header)) {
	      snprintf(label, sizeof(label), "SID %u %s", (unsigned)header->id,
	               fingerprint[0] ? fingerprint : "--------");
    } else if (seedkeeper_header_is_generic_secret(header)) {
      if (header->label[0])
        snprintf(label, sizeof(label), "%s", header->label);
      else
        snprintf(label, sizeof(label), "Secret %u", (unsigned)header->id);
    } else if (seedkeeper_header_is_password(header)) {
      snprintf(label, sizeof(label), "Password %s",
               header->label[0] ? header->label : fingerprint);
    } else {
      if (header->label[0])
        snprintf(label, sizeof(label), "%s", header->label);
      else
        snprintf(label, sizeof(label), "Item %u", (unsigned)header->id);
    }
    create_action_button(panel, label, satochip_seedkeeper_select_item_cb,
                         (void *)(uintptr_t)header->id, false);
    has_buttons = true;
  }

  if (!has_buttons)
    create_text(panel, "No viewable items.", false, secondary_color());
}

static void satochip_seedkeeper_render_lookup_content(lv_obj_t *panel) {
  if (!panel)
    return;

  if (s_satochip_seedkeeper_lookup_stage ==
      SATOCHIP_SEEDKEEPER_LOOKUP_ENTER_PIN) {
    satochip_maint_attach_primary_input(panel, "Card PIN", true, false, 64, "");
    if (s_satochip_maint_result[0])
      create_text(panel, s_satochip_maint_result, false,
                  s_satochip_maint_task_err == ESP_OK ? main_color()
                                                      : error_color());
    create_action_button(panel, "Read List", satochip_maint_start_event_cb, NULL,
                         true);
    return;
  }

  if (s_satochip_seedkeeper_lookup_stage == SATOCHIP_SEEDKEEPER_LOOKUP_LIST) {
    satochip_seedkeeper_render_list_entries(panel);
    if (s_satochip_maint_result[0] && !s_satochip_seedkeeper_last_list_valid)
      create_text(panel, s_satochip_maint_result, false,
                  s_satochip_maint_task_err == ESP_OK ? main_color()
                                                      : error_color());
    create_action_button(panel, "Refresh", satochip_maint_start_event_cb, NULL,
                         false);
    return;
  }

  const smartcard_seedkeeper_header_t *header =
      s_satochip_seedkeeper_selected_header_valid
          ? &s_satochip_seedkeeper_selected_header
          : satochip_seedkeeper_find_last_header(
                s_satochip_seedkeeper_selected_sid);

  char title[48];
  snprintf(title, sizeof(title), "%s", seedkeeper_header_kind_label(header));
  create_text(panel, title, true, highlight_color());
  if (header) {
	    if (seedkeeper_header_is_loadable_mnemonic(header)) {
	      char fingerprint[9];
	      seedkeeper_header_mnemonic_fingerprint_hex(header, fingerprint);
	      create_text(panel,
	                  fingerprint[0] ? fingerprint : "(empty)", false,
	                  secondary_color());
    } else if (header->label[0]) {
      create_text(panel, header->label, false, secondary_color());
    }
  }
  if (s_satochip_maint_result[0])
    create_text(panel, s_satochip_maint_result, false,
                s_satochip_maint_task_err == ESP_OK ? main_color()
                                                    : error_color());

  if (s_satochip_seedkeeper_lookup_stage == SATOCHIP_SEEDKEEPER_LOOKUP_EDIT) {
    if (!seedkeeper_header_is_password(header)) {
      create_text(panel, "Only password items can be edited.", false, error_color());
      create_action_button(panel, i18n_tr_or("common.back", "Back"),
                           satochip_seedkeeper_lookup_edit_back_cb, NULL, true);
      return;
    }
    satochip_maint_attach_primary_input(panel, "New Password", true, false, 512, "");
    satochip_maint_attach_extra_field(panel, "Label", "Password Name",
                                      header && header->label[0] ? header->label
                                                                 : "Password",
                                      false, false, 80,
                                      &s_satochip_maint_extra_a);
    create_action_button(panel, "Save Changes", satochip_seedkeeper_lookup_edit_cb,
                         NULL, true);
    create_action_button(panel, i18n_tr_or("common.back", "Back"),
                         satochip_seedkeeper_lookup_edit_back_cb, NULL, false);
    return;
  }

	  if (seedkeeper_header_is_password(header)) {
	    create_action_button(panel, "View Password",
	                         satochip_seedkeeper_lookup_item_action_cb,
	                         (void *)(uintptr_t)SATOCHIP_SEEDKEEPER_ITEM_OP_VIEW,
	                         true);
	    create_action_button(panel, "Export QR",
	                         satochip_seedkeeper_lookup_item_action_cb,
	                         (void *)(uintptr_t)SATOCHIP_SEEDKEEPER_ITEM_OP_EXPORT,
	                         false);
	    create_action_button(panel, "Edit",
	                         satochip_seedkeeper_lookup_item_action_cb,
	                         (void *)(uintptr_t)SATOCHIP_SEEDKEEPER_ITEM_OP_UPDATE,
	                         false);
  } else if (seedkeeper_header_is_generic_secret(header)) {
    create_action_button(panel, "View Secret",
                         satochip_seedkeeper_lookup_item_action_cb,
                         (void *)(uintptr_t)SATOCHIP_SEEDKEEPER_ITEM_OP_VIEW,
                         true);
    create_action_button(panel, "Export QR",
                         satochip_seedkeeper_lookup_item_action_cb,
                         (void *)(uintptr_t)SATOCHIP_SEEDKEEPER_ITEM_OP_EXPORT,
                         false);
  } else {
    create_action_button(panel, "View Mnemonic",
                         satochip_seedkeeper_lookup_item_action_cb,
                         (void *)(uintptr_t)SATOCHIP_SEEDKEEPER_ITEM_OP_VIEW,
                         true);
    create_action_button(panel, "Import to Device",
                         satochip_seedkeeper_lookup_item_action_cb,
                         (void *)(uintptr_t)SATOCHIP_SEEDKEEPER_ITEM_OP_LOAD,
                         false);
  }
  create_action_button(panel, "Delete", satochip_seedkeeper_lookup_item_action_cb,
                       (void *)(uintptr_t)SATOCHIP_SEEDKEEPER_ITEM_OP_DELETE,
                       false);
  create_action_button(panel, "Back to List", satochip_seedkeeper_lookup_back_cb, NULL,
                       false);
}

static void satochip_maint_prepare(satochip_maint_mode_t mode) {
  bool mode_changed = s_satochip_maint_mode != mode;
  bool was_seedkeeper_lookup =
      s_satochip_maint_mode == SATOCHIP_MAINT_SEEDKEEPER_VIEW_MNEMONIC ||
      s_satochip_maint_mode == SATOCHIP_MAINT_SEEDKEEPER_LOAD_MNEMONIC;
  bool is_seedkeeper_lookup =
      mode == SATOCHIP_MAINT_SEEDKEEPER_VIEW_MNEMONIC ||
      mode == SATOCHIP_MAINT_SEEDKEEPER_LOAD_MNEMONIC;
  bool returning_from_seedkeeper_reset_step =
      mode == SATOCHIP_MAINT_SEEDKEEPER_RESET &&
      (s_satochip_maint_mode == SATOCHIP_MAINT_SEEDKEEPER_RESET_PIN_STEP ||
       s_satochip_maint_mode == SATOCHIP_MAINT_SEEDKEEPER_RESET_PUK_STEP);
  if (mode_changed && !returning_from_seedkeeper_reset_step)
    satochip_maint_result_clear();
  s_satochip_maint_mode = mode;
  smartcard_ccid_set_factory_reset_mode(
      mode == SATOCHIP_MAINT_RESET_FACTORY);
  if (mode_changed && mode != SATOCHIP_MAINT_SEEDKEEPER_SAVE_SECRET)
    secure_memzero(s_satochip_seedkeeper_scanned_secret,
                   sizeof(s_satochip_seedkeeper_scanned_secret));
  if (mode_changed && (is_seedkeeper_lookup || was_seedkeeper_lookup))
    satochip_seedkeeper_lookup_state_reset();
}

static void satochip_maint_copy_text(char *dst, size_t dst_len,
                                     lv_obj_t *textarea) {
  if (!dst || dst_len == 0)
    return;
  const char *text = textarea ? lv_textarea_get_text(textarea) : NULL;
  snprintf(dst, dst_len, "%s", text ? text : "");
}

static bool satochip_parse_u8_text(const char *text, uint8_t *out) {
  if (!out)
    return false;
  if (!text || text[0] == '\0') {
    *out = 0;
    return true;
  }
  char *end = NULL;
  unsigned long value = strtoul(text, &end, 0);
  if (end == text)
    return false;
  while (end && *end == ' ')
    end++;
  if (end && *end != '\0')
    return false;
  if (value > UINT8_MAX)
    return false;
  *out = (uint8_t)value;
  return true;
}

static bool satochip_parse_u16_text(const char *text, uint16_t *out) {
  if (!out)
    return false;
  if (!text || text[0] == '\0') {
    *out = 0;
    return true;
  }
  char *end = NULL;
  unsigned long value = strtoul(text, &end, 0);
  if (end == text)
    return false;
  while (end && *end == ' ')
    end++;
  if (end && *end != '\0')
    return false;
  if (value > UINT16_MAX)
    return false;
  *out = (uint16_t)value;
  return true;
}

static bool satochip_parse_u32_text(const char *text, uint32_t *out) {
  if (!out)
    return false;
  if (!text || text[0] == '\0') {
    *out = 0;
    return true;
  }
  char *end = NULL;
  unsigned long value = strtoul(text, &end, 0);
  if (end == text)
    return false;
  while (end && *end == ' ')
    end++;
  if (end && *end != '\0')
    return false;
  if (value > UINT32_MAX)
    return false;
  *out = (uint32_t)value;
  return true;
}

static bool satochip_parse_u64_text(const char *text, uint64_t *out) {
  if (!out)
    return false;
  if (!text || text[0] == '\0') {
    *out = 0;
    return true;
  }
  char *end = NULL;
  unsigned long long value = strtoull(text, &end, 0);
  if (end == text)
    return false;
  while (end && *end == ' ')
    end++;
  if (end && *end != '\0')
    return false;
  *out = (uint64_t)value;
  return true;
}

static int satochip_hex_digit(int c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return 10 + c - 'a';
  if (c >= 'A' && c <= 'F')
    return 10 + c - 'A';
  return -1;
}

static size_t satochip_parse_hex_bytes(const char *text, uint8_t *out,
                                       size_t out_len) {
  if (!text || !out || out_len == 0)
    return 0;
  size_t count = 0;
  int high = -1;
  for (const char *p = text; *p; p++) {
    int digit = satochip_hex_digit((unsigned char)*p);
    if (digit < 0) {
      if (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t' || *p == ':' ||
          *p == ',' || *p == ';' || *p == '-') {
        continue;
      }
      if (*p == 'x' || *p == 'X') {
        high = -1;
        continue;
      }
      continue;
    }
    if (high < 0) {
      high = digit;
    } else {
      if (count >= out_len)
        break;
      out[count++] = (uint8_t)((high << 4) | digit);
      high = -1;
    }
  }
  return count;
}

static void satochip_hex_bytes_upper(const uint8_t *data, size_t len, char *out,
                                     size_t out_len) {
  if (!out || out_len == 0)
    return;
  out[0] = '\0';
  if (!data || len == 0)
    return;
  size_t pos = 0;
  for (size_t i = 0; i < len && pos + 2 < out_len; i++) {
    int written = snprintf(out + pos, out_len - pos, "%02X", data[i]);
    if (written <= 0)
      break;
    pos += (size_t)written;
  }
}

static void satochip_hex_bytes_spaced(const uint8_t *data, size_t len,
                                      char *out, size_t out_len) {
  if (!out || out_len == 0)
    return;
  out[0] = '\0';
  if (!data || len == 0)
    return;
  size_t pos = 0;
  for (size_t i = 0; i < len && pos + 3 < out_len; i++) {
    int written = snprintf(out + pos, out_len - pos, "%02X%s", data[i],
                           (i + 1 < len) ? " " : "");
    if (written <= 0)
      break;
    pos += (size_t)written;
  }
}

static void satochip_appendf(char *dst, size_t dst_len, size_t *pos,
                             const char *fmt, ...) {
  if (!dst || dst_len == 0 || !pos || *pos >= dst_len)
    return;
  va_list ap;
  va_start(ap, fmt);
  int written = vsnprintf(dst + *pos, dst_len - *pos, fmt, ap);
  va_end(ap);
  if (written <= 0)
    return;
  *pos += (size_t)written;
  if (*pos >= dst_len)
    dst[dst_len - 1] = '\0';
}

static void satochip_format_apdu_result(char *dst, size_t dst_len,
                                        const char *title,
                                        const smartcard_satochip_apdu_result_t *res) {
  if (!dst || dst_len == 0)
    return;
  size_t pos = 0;
  dst[0] = '\0';
  if (title && title[0])
    satochip_appendf(dst, dst_len, &pos, "%s\n", title);
  if (!res) {
    satochip_appendf(dst, dst_len, &pos, "No result.");
    return;
  }
  satochip_appendf(dst, dst_len, &pos, "SW: %04X\n", res->sw);
  if (res->detail[0])
    satochip_appendf(dst, dst_len, &pos, "%s\n", res->detail);
  if (res->response_len > 0) {
    char hex[1536];
    satochip_hex_bytes_spaced(res->response,
                              res->response_len > 96 ? 96 : res->response_len,
                              hex, sizeof(hex));
    satochip_appendf(dst, dst_len, &pos, "Response: %u bytes\n%s\n",
                     (unsigned)res->response_len, hex);
  }
}

static void satochip_format_seedkeeper_write_mnemonic_result(
    char *dst, size_t dst_len, const smartcard_satochip_apdu_result_t *res,
    const char *wallet_fp) {
  if (!dst || dst_len == 0)
    return;
  size_t pos = 0;
  dst[0] = '\0';
  satochip_appendf(dst, dst_len, &pos, "Write Mnemonic\n");
  if (!res) {
    satochip_appendf(dst, dst_len, &pos, "No result.");
    return;
  }
  satochip_appendf(dst, dst_len, &pos, "SW: %04X\n", res->sw);
  if (res->sw == 0x9000 && res->response_len >= 6) {
    uint16_t sid = ((uint16_t)res->response[0] << 8) | res->response[1];
    satochip_appendf(dst, dst_len, &pos, "SID: %u\n", (unsigned)sid);
    satochip_appendf(dst, dst_len, &pos, "Fingerprint: %s\n",
                     wallet_fp && wallet_fp[0] ? wallet_fp : "--");
    return;
  }
  if (res->detail[0])
    satochip_appendf(dst, dst_len, &pos, "%s\n", res->detail);
  if (res->response_len > 0) {
    char hex[1536];
    satochip_hex_bytes_spaced(res->response,
                              res->response_len > 96 ? 96 : res->response_len,
                              hex, sizeof(hex));
    satochip_appendf(dst, dst_len, &pos, "Response: %u bytes\n%s\n",
                     (unsigned)res->response_len, hex);
  }
}

static bool seedkeeper_build_plain_header_impl(uint8_t type, uint8_t subtype,
                                               uint8_t export_rights,
                                               const char *label,
                                               bool allow_empty_label,
                                               const uint8_t fingerprint[4],
                                               uint8_t *out, size_t out_len,
                                               size_t *written_out) {
  if (!out || out_len < 15 || !written_out)
    return false;

  const char *safe_label =
      (label && label[0]) ? label : (allow_empty_label ? "" : "KernSigner");
  size_t label_len = strlen(safe_label);
  if (label_len > SEEDKEEPER_LABEL_MAX_BYTES)
    label_len = SEEDKEEPER_LABEL_MAX_BYTES;
  if (15U + label_len > out_len)
    return false;

  size_t pos = 0;
  out[pos++] = 0x00;
  out[pos++] = 0x00;
  out[pos++] = type;
  out[pos++] = SEEDKEEPER_ORIGIN_PLAIN_IMPORT;
  out[pos++] = export_rights;
  out[pos++] = 0x00;
  out[pos++] = 0x00;
  out[pos++] = 0x00;
  if (fingerprint)
    memcpy(out + pos, fingerprint, 4);
  else
    memset(out + pos, 0, 4);
  pos += 4;
  out[pos++] = subtype;
  out[pos++] = 0x00;
  out[pos++] = (uint8_t)label_len;
  memcpy(out + pos, safe_label, label_len);
  pos += label_len;
  *written_out = pos;
  return true;
}

static bool seedkeeper_build_plain_header(uint8_t type, uint8_t subtype,
                                          uint8_t export_rights,
                                          const char *label,
                                          const uint8_t fingerprint[4],
                                          uint8_t *out, size_t out_len,
                                          size_t *written_out) {
  return seedkeeper_build_plain_header_impl(type, subtype, export_rights,
                                            label, false, fingerprint, out,
                                            out_len, written_out);
}

static bool seedkeeper_build_plain_header_allow_empty(
    uint8_t type, uint8_t subtype, uint8_t export_rights, const char *label,
    const uint8_t fingerprint[4], uint8_t *out, size_t out_len,
    size_t *written_out) {
  return seedkeeper_build_plain_header_impl(type, subtype, export_rights,
                                            label, true, fingerprint, out,
                                            out_len, written_out);
}

static bool seedkeeper_build_mnemonic_secret(const char *mnemonic,
                                             const char *passphrase,
                                             uint8_t *out, size_t out_len,
                                             size_t *written_out) {
  if (!mnemonic || !out || !written_out)
    return false;

  const char *safe_passphrase = passphrase ? passphrase : "";
  size_t mnemonic_len = strlen(mnemonic);
  size_t passphrase_len = strlen(safe_passphrase);
  if (mnemonic_len == 0 || mnemonic_len > SEEDKEEPER_MNEMONIC_MAX_BYTES ||
      passphrase_len > SEEDKEEPER_MNEMONIC_MAX_BYTES)
    return false;
  if (2U + mnemonic_len + passphrase_len > out_len)
    return false;

  size_t pos = 0;
  out[pos++] = (uint8_t)mnemonic_len;
  memcpy(out + pos, mnemonic, mnemonic_len);
  pos += mnemonic_len;
  out[pos++] = (uint8_t)passphrase_len;
  memcpy(out + pos, safe_passphrase, passphrase_len);
  pos += passphrase_len;
  *written_out = pos;
  return true;
}

static bool seedkeeper_build_mnemonic_header_from_text(
    const char *mnemonic, const char *passphrase, uint8_t *header,
    size_t header_len, size_t *written_out, char *wallet_fp_out,
    size_t wallet_fp_out_len) {
  if (!mnemonic || !mnemonic[0] || !header || !written_out)
    return false;

  uint8_t secret[2 + SEEDKEEPER_MNEMONIC_MAX_BYTES * 2];
  uint8_t secret_hash[CRYPTO_SHA256_SIZE];
  uint8_t header_fingerprint[4];
  bool ok = false;
  memset(secret, 0, sizeof(secret));
  memset(secret_hash, 0, sizeof(secret_hash));
  memset(header_fingerprint, 0, sizeof(header_fingerprint));
  if (wallet_fp_out && wallet_fp_out_len > 0)
    wallet_fp_out[0] = '\0';

  size_t secret_len = 0;
  ok = seedkeeper_build_mnemonic_secret(mnemonic, passphrase, secret,
                                        sizeof(secret), &secret_len) &&
       crypto_sha256(secret, secret_len, secret_hash) == CRYPTO_OK;
  if (!ok)
    goto done;

  if (!seedkeeper_fingerprint_from_mnemonic(mnemonic, passphrase,
                                            header_fingerprint)) {
    memcpy(header_fingerprint, secret_hash, sizeof(header_fingerprint));
  }

  char label_text[SEEDKEEPER_LABEL_MAX_BYTES + 1] = "";
  if (wallet_fp_out && wallet_fp_out_len > 0 &&
      seedkeeper_wallet_fingerprint_hex_from_mnemonic(
          mnemonic, passphrase, wallet_fp_out)) {
    snprintf(label_text, sizeof(label_text), "%s", wallet_fp_out);
  }

  ok = seedkeeper_build_plain_header(
      SEEDKEEPER_TYPE_BIP39_MNEMONIC, 0x00,
      SEEDKEEPER_EXPORT_PLAINTEXT_ALLOWED, label_text, header_fingerprint, header,
      header_len, written_out);
  if (!ok)
    goto done;

done:
  secure_memzero(secret, sizeof(secret));
  secure_memzero(secret_hash, sizeof(secret_hash));
  secure_memzero(header_fingerprint, sizeof(header_fingerprint));
  return ok;
}

static bool seedkeeper_collect_plain_secret(
    const smartcard_satochip_apdu_result_t *apdu, uint8_t *secret_out,
    size_t secret_out_len, size_t *secret_len_out,
    smartcard_seedkeeper_header_t *header_out);

static bool seedkeeper_build_text_secret(const char *text, bool two_byte_len,
                                         uint8_t *out, size_t out_len,
                                         size_t *written_out) {
  if (!text || !out || !written_out)
    return false;
  size_t text_len = strlen(text);
  size_t prefix_len = two_byte_len ? 2U : 1U;
  if (text_len == 0 || prefix_len + text_len > out_len)
    return false;
  if (!two_byte_len && text_len > UINT8_MAX)
    return false;
  if (two_byte_len && text_len > UINT16_MAX)
    return false;

  size_t pos = 0;
  if (two_byte_len) {
    out[pos++] = (uint8_t)(text_len >> 8);
    out[pos++] = (uint8_t)text_len;
  } else {
    out[pos++] = (uint8_t)text_len;
  }
  memcpy(out + pos, text, text_len);
  pos += text_len;
  *written_out = pos;
  return true;
}

static bool seedkeeper_extract_text_secret(
    const smartcard_satochip_apdu_result_t *apdu, char *text_out,
    size_t text_out_len, smartcard_seedkeeper_header_t *header_out) {
  if (!text_out || text_out_len == 0)
    return false;
  text_out[0] = '\0';

  smartcard_seedkeeper_header_t header;
  uint8_t secret[4096];
  size_t secret_len = 0;
  memset(secret, 0, sizeof(secret));
  if (!seedkeeper_collect_plain_secret(apdu, secret, sizeof(secret),
                                       &secret_len, &header)) {
    secure_memzero(secret, sizeof(secret));
    return false;
  }

  size_t offset = 0;
  size_t text_len = 0;
  if ((header.type == SEEDKEEPER_TYPE_DATA ||
       header.type == SEEDKEEPER_TYPE_MASTER_PASSWORD) &&
      secret_len >= 2) {
    text_len = ((size_t)secret[0] << 8) | secret[1];
    offset = 2;
  } else if (secret_len >= 1) {
    text_len = secret[0];
    offset = 1;
  } else {
    secure_memzero(secret, sizeof(secret));
    return false;
  }

  if (text_len == 0 || offset + text_len > secret_len) {
    secure_memzero(secret, sizeof(secret));
    return false;
  }
  size_t copy_len = text_len;
  if (copy_len >= text_out_len)
    copy_len = text_out_len - 1;
  memcpy(text_out, secret + offset, copy_len);
  text_out[copy_len] = '\0';
  if (header_out)
    *header_out = header;
  secure_memzero(secret, sizeof(secret));
  return true;
}

static bool seedkeeper_import_text_secret(
    const char *pin, uint8_t type, uint8_t subtype, const char *label,
    const char *text, bool two_byte_len, const char *title,
    smartcard_satochip_apdu_result_t *apdu, char *result,
    size_t result_len) {
  uint8_t header[15 + SEEDKEEPER_LABEL_MAX_BYTES];
  uint8_t secret[4096];
  uint8_t secret_hash[CRYPTO_SHA256_SIZE];
  size_t header_len = 0;
  size_t secret_len = 0;
  if (!seedkeeper_build_text_secret(text, two_byte_len, secret,
                                    sizeof(secret), &secret_len) ||
      crypto_sha256(secret, secret_len, secret_hash) != CRYPTO_OK ||
      !seedkeeper_build_plain_header(type, subtype,
                                     SEEDKEEPER_EXPORT_PLAINTEXT_ALLOWED,
                                     label, secret_hash, header,
                                     sizeof(header), &header_len)) {
    snprintf(result, result_len, "%s content is too long or empty.", title ? title : "");
    secure_memzero(header, sizeof(header));
    secure_memzero(secret, sizeof(secret));
    secure_memzero(secret_hash, sizeof(secret_hash));
    return false;
  }

  s_satochip_maint_task_err = smartcard_seedkeeper_import_secret(
      pin, header, header_len, secret, secret_len, 0, NULL, 0, NULL, 0, false,
      apdu, 30000);
  satochip_format_apdu_result(result, result_len, title, apdu);
  secure_memzero(header, sizeof(header));
  secure_memzero(secret, sizeof(secret));
  secure_memzero(secret_hash, sizeof(secret_hash));
  return true;
}

static bool seedkeeper_import_text_secret_allow_empty_label(
    const char *pin, uint8_t type, uint8_t subtype, const char *text,
    bool two_byte_len, const char *title, smartcard_satochip_apdu_result_t *apdu,
    char *result, size_t result_len) {
  uint8_t header[15 + SEEDKEEPER_LABEL_MAX_BYTES];
  uint8_t secret[4096];
  uint8_t secret_hash[CRYPTO_SHA256_SIZE];
  size_t header_len = 0;
  size_t secret_len = 0;
  if (!seedkeeper_build_text_secret(text, two_byte_len, secret,
                                    sizeof(secret), &secret_len) ||
      crypto_sha256(secret, secret_len, secret_hash) != CRYPTO_OK ||
      !seedkeeper_build_plain_header_allow_empty(
          type, subtype, SEEDKEEPER_EXPORT_PLAINTEXT_ALLOWED, "",
          secret_hash, header, sizeof(header), &header_len)) {
    snprintf(result, result_len, "%s content is too long or empty.",
             title ? title : "");
    secure_memzero(header, sizeof(header));
    secure_memzero(secret, sizeof(secret));
    secure_memzero(secret_hash, sizeof(secret_hash));
    return false;
  }

  s_satochip_maint_task_err = smartcard_seedkeeper_import_secret(
      pin, header, header_len, secret, secret_len, 0, NULL, 0, NULL, 0, false,
      apdu, 30000);
  satochip_format_apdu_result(result, result_len, title, apdu);
  secure_memzero(header, sizeof(header));
  secure_memzero(secret, sizeof(secret));
  secure_memzero(secret_hash, sizeof(secret_hash));
  return true;
}

static bool seedkeeper_secret_payload_offset(
    const smartcard_satochip_apdu_result_t *apdu, size_t *offset_out,
    smartcard_seedkeeper_header_t *header_out) {
  if (!apdu || !offset_out || apdu->response_len < 15)
    return false;

  smartcard_seedkeeper_header_t header;
  memset(&header, 0, sizeof(header));
  const uint8_t *payload = apdu->response;
  header.id = ((uint16_t)payload[0] << 8) | payload[1];
  header.type = payload[2];
  header.origin = payload[3];
  header.export_rights = payload[4];
  header.export_nbplain = payload[5];
  header.export_nbsecure = payload[6];
  header.export_counter = payload[7];
  memcpy(header.fingerprint, payload + 8, sizeof(header.fingerprint));
  header.subtype = payload[12];
  header.rfu1 = payload[12];
  header.rfu2 = payload[13];
  size_t label_len = payload[14];
  if (apdu->response_len < 15U + label_len)
    return false;
  size_t label_copy = label_len;
  if (label_copy >= sizeof(header.label))
    label_copy = sizeof(header.label) - 1;
  if (label_copy > 0)
    memcpy(header.label, payload + 15, label_copy);
  header.label[label_copy] = '\0';
  size_t offset = 15U + label_len;
  if (offset >= apdu->response_len)
    return false;

  if (header_out)
    *header_out = header;
  *offset_out = offset;
  return true;
}

static bool seedkeeper_collect_plain_secret(
    const smartcard_satochip_apdu_result_t *apdu, uint8_t *secret_out,
    size_t secret_out_len, size_t *secret_len_out,
    smartcard_seedkeeper_header_t *header_out) {
  if (!apdu || !secret_out || !secret_len_out)
    return false;
  *secret_len_out = 0;

  size_t offset = 0;
  smartcard_seedkeeper_header_t header;
  if (!seedkeeper_secret_payload_offset(apdu, &offset, &header))
    return false;

  const uint8_t *p = apdu->response + offset;
  size_t remaining = apdu->response_len - offset;
  size_t total = 0;
  bool saw_chunk = false;
  while (remaining >= 2) {
    size_t chunk_len = ((size_t)p[0] << 8) | p[1];
    if (!saw_chunk && chunk_len > remaining - 2U) {
      if (remaining > secret_out_len)
        return false;
      memcpy(secret_out, p, remaining);
      total = remaining;
      remaining = 0;
      break;
    }
    p += 2;
    remaining -= 2;
    if (chunk_len > remaining)
      return false;
    if (total + chunk_len > secret_out_len)
      return false;
    memcpy(secret_out + total, p, chunk_len);
    total += chunk_len;
    p += chunk_len;
    remaining -= chunk_len;
    saw_chunk = true;
    if (header.type == SEEDKEEPER_TYPE_BIP39_MNEMONIC && total >= 2) {
      size_t mnemonic_len = secret_out[0];
      size_t passphrase_len_offset = 1U + mnemonic_len;
      if (passphrase_len_offset < total) {
        size_t passphrase_len = secret_out[passphrase_len_offset];
        size_t needed = passphrase_len_offset + 1U + passphrase_len;
        if (total >= needed)
          break;
      }
    }
    if (remaining == 0)
      break;
    if (remaining >= 2) {
      size_t trailer_len = ((size_t)p[0] << 8) | p[1];
      if (trailer_len + 2U == remaining)
        break;
    }
  }

  if (!saw_chunk) {
    if (remaining > secret_out_len)
      return false;
    memcpy(secret_out, apdu->response + offset, remaining);
    total = remaining;
  }

  *secret_len_out = total;
  if (header_out)
    *header_out = header;
  return total > 0;
}

static bool seedkeeper_extract_bip39_mnemonic(
    const smartcard_satochip_apdu_result_t *apdu, char *mnemonic_out,
    size_t mnemonic_out_len, char *passphrase_out, size_t passphrase_out_len,
    smartcard_seedkeeper_header_t *header_out) {
  if (!mnemonic_out || mnemonic_out_len == 0)
    return false;
  mnemonic_out[0] = '\0';
  if (passphrase_out && passphrase_out_len > 0)
    passphrase_out[0] = '\0';

  smartcard_seedkeeper_header_t header;
  uint8_t secret[2 + SEEDKEEPER_MNEMONIC_MAX_BYTES * 2];
  size_t secret_len = 0;
  if (!seedkeeper_collect_plain_secret(apdu, secret, sizeof(secret),
                                       &secret_len, &header))
    return false;

  const uint8_t *payload = secret;
  size_t payload_len = secret_len;
  size_t pos = 0;
  bool ok = false;
  if (header.type == SEEDKEEPER_TYPE_BIP39_MNEMONIC) {
    if (payload_len < 2)
      goto done;

    size_t mnemonic_len = payload[pos++];
    if (mnemonic_len == 0 || mnemonic_len >= mnemonic_out_len ||
        pos + mnemonic_len > payload_len)
      goto done;
    memcpy(mnemonic_out, payload + pos, mnemonic_len);
    mnemonic_out[mnemonic_len] = '\0';
    pos += mnemonic_len;

    if (pos >= payload_len)
      goto done;
    size_t passphrase_len = payload[pos++];
    if (pos + passphrase_len > payload_len)
      goto done;
    if (passphrase_out && passphrase_out_len > 0) {
      size_t copy = passphrase_len;
      if (copy >= passphrase_out_len)
        copy = passphrase_out_len - 1;
      memcpy(passphrase_out, payload + pos, copy);
      passphrase_out[copy] = '\0';
    }
  } else if (header.type == SEEDKEEPER_TYPE_MASTERSEED &&
             header.subtype == 0x01) {
    if (payload_len < 4)
      goto done;

    size_t masterseed_len = payload[pos++];
    if (masterseed_len == 0 || pos + masterseed_len > payload_len)
      goto done;
    pos += masterseed_len;

    if (pos >= payload_len)
      goto done;
    const char *wordlist_name = seedkeeper_wordlist_name_from_byte(payload[pos++]);
    if (!wordlist_name)
      goto done;

    if (pos >= payload_len)
      goto done;
    size_t entropy_len = payload[pos++];
    if (entropy_len < 16 || entropy_len > 32 || (entropy_len % 4) != 0 ||
        pos + entropy_len > payload_len)
      goto done;

    struct words *wordlist = NULL;
    if (bip39_get_wordlist(wordlist_name, &wordlist) != WALLY_OK || !wordlist)
      goto done;

    char *mnemonic = NULL;
    if (bip39_mnemonic_from_bytes(wordlist, payload + pos, entropy_len,
                                  &mnemonic) != WALLY_OK ||
        !mnemonic)
      goto done;

    char *mnemonic_copy = dup_wally_string_local(mnemonic);
    if (!mnemonic_copy)
      goto done;
    snprintf(mnemonic_out, mnemonic_out_len, "%s", mnemonic_copy);
    SECURE_FREE_STRING(mnemonic_copy);
    pos += entropy_len;

    if (pos >= payload_len)
      goto done;
    size_t passphrase_len = payload[pos++];
    if (pos + passphrase_len > payload_len)
      goto done;
    if (passphrase_out && passphrase_out_len > 0) {
      size_t copy = passphrase_len;
      if (copy >= passphrase_out_len)
        copy = passphrase_out_len - 1;
      memcpy(passphrase_out, payload + pos, copy);
      passphrase_out[copy] = '\0';
    }
  } else {
    goto done;
  }

  if (header_out)
    *header_out = header;
  ok = true;

done:
  secure_memzero(secret, sizeof(secret));
  if (!ok) {
    mnemonic_out[0] = '\0';
    if (passphrase_out && passphrase_out_len > 0)
      passphrase_out[0] = '\0';
  }
  return ok;
}

static bool seedkeeper_extract_random_secret_bytes(
    const smartcard_satochip_apdu_result_t *apdu, uint8_t *entropy_out,
    size_t expected_len, smartcard_seedkeeper_header_t *header_out) {
  if (!entropy_out || expected_len == 0)
    return false;

  smartcard_seedkeeper_header_t header;
  uint8_t secret[128];
  size_t secret_len = 0;
  if (!seedkeeper_collect_plain_secret(apdu, secret, sizeof(secret),
                                       &secret_len, &header))
    return false;

  bool ok = false;
  if (header.type == SEEDKEEPER_TYPE_SECRET_KEY) {
    if (secret_len == expected_len) {
      memcpy(entropy_out, secret, expected_len);
      ok = true;
    } else if (secret_len >= expected_len + 2U &&
               (((size_t)secret[0] << 8) | secret[1]) == expected_len) {
      memcpy(entropy_out, secret + 2, expected_len);
      ok = true;
    } else if (secret_len >= expected_len + 1U &&
               secret[0] == expected_len) {
      memcpy(entropy_out, secret + 1, expected_len);
      ok = true;
    }
  }

  if (ok && header_out)
    *header_out = header;
  secure_memzero(secret, sizeof(secret));
  return ok;
}

static bool seedkeeper_load_mnemonic_into_session(const char *mnemonic,
                                                  const char *passphrase,
                                                  size_t *slot_index_out) {
  if (!mnemonic || !mnemonic[0])
    return false;
#ifdef SIMULATOR
  if (slot_index_out)
    *slot_index_out = 0;
  return true;
#else
  wallet_network_t net = settings_get_default_network();
  size_t slot_index = 0;
  bool ok = mnemonic_slots_add_mnemonic(mnemonic, &slot_index) &&
            key_load_from_mnemonic(mnemonic, passphrase ? passphrase : "",
                                   net == WALLET_NETWORK_TESTNET);
  if (ok) {
    wallet_cleanup();
    wallet_set_policy(settings_get_default_policy());
    ok = wallet_init(net);
  }
  if (ok && slot_index_out)
    *slot_index_out = slot_index;
  return ok;
#endif
}

static void satochip_format_seedkeeper_mnemonic_result(
    char *dst, size_t dst_len, const char *title,
    const smartcard_satochip_apdu_result_t *apdu, bool load_result,
    bool loaded_ok, size_t slot_index) {
  if (!dst || dst_len == 0)
    return;

  char mnemonic[SEEDKEEPER_MNEMONIC_MAX_BYTES + 1];
  char passphrase[SEEDKEEPER_MNEMONIC_MAX_BYTES + 1];
  smartcard_seedkeeper_header_t header;
  bool parsed = seedkeeper_extract_bip39_mnemonic(
      apdu, mnemonic, sizeof(mnemonic), passphrase, sizeof(passphrase),
      &header);

  size_t pos = 0;
  dst[0] = '\0';
  satochip_appendf(dst, dst_len, &pos, "%s\n", title ? title : "SeedKeeper");
  if (apdu)
    satochip_appendf(dst, dst_len, &pos, "SW: %04X\n", apdu->sw);
  if (apdu && apdu->detail[0])
    satochip_appendf(dst, dst_len, &pos, "%s\n", apdu->detail);
  if (!parsed) {
    satochip_appendf(dst, dst_len, &pos,
                     "Could not parse a BIP39 mnemonic. Check the SID and export permission.");
    return;
  }

  char wallet_fp[9] = "";
  (void)seedkeeper_wallet_fingerprint_hex_from_mnemonic(mnemonic, passphrase,
                                                        wallet_fp);
  satochip_appendf(dst, dst_len, &pos, "SID: %u\nFingerprint: %s\n\n%s",
                   (unsigned)header.id,
                   wallet_fp[0] ? wallet_fp : "--", mnemonic);
  if (passphrase[0])
    satochip_appendf(dst, dst_len, &pos, "\n\nPassphrase: %s", passphrase);
  if (load_result)
    satochip_appendf(dst, dst_len, &pos, "\n\nLoad: %s%s%u",
                     loaded_ok ? "success, slot " : "failed",
                     loaded_ok ? "" : ".",
                     loaded_ok ? (unsigned)(slot_index + 1U) : 0U);
}

static void satochip_format_seedkeeper_password_result(
    char *dst, size_t dst_len, const char *title,
    const smartcard_satochip_apdu_result_t *apdu) {
  if (!dst || dst_len == 0)
    return;

  char password[1024];
  smartcard_seedkeeper_header_t header;
  bool parsed = seedkeeper_extract_text_secret(apdu, password, sizeof(password),
                                               &header);

  size_t pos = 0;
  dst[0] = '\0';
  satochip_appendf(dst, dst_len, &pos, "%s\n", title ? title : "View Password");
  if (apdu)
    satochip_appendf(dst, dst_len, &pos, "SW: %04X\n", apdu->sw);
  if (apdu && apdu->detail[0])
    satochip_appendf(dst, dst_len, &pos, "%s\n", apdu->detail);
  if (!parsed) {
    satochip_appendf(dst, dst_len, &pos,
                     "Could not parse a password. Check the SID and export permission.");
    return;
  }

  satochip_appendf(dst, dst_len, &pos, "SID: %u\nLabel: %s\n\n%s",
                   (unsigned)header.id,
                   header.label[0] ? header.label : "(no label)", password);
  secure_memzero(password, sizeof(password));
  secure_memzero(&header, sizeof(header));
}

static void satochip_format_seedkeeper_secret_result(
    char *dst, size_t dst_len, const char *title,
    const smartcard_satochip_apdu_result_t *apdu) {
  if (!dst || dst_len == 0)
    return;

  char secret[4096];
  smartcard_seedkeeper_header_t header;
  bool parsed = seedkeeper_extract_text_secret(apdu, secret, sizeof(secret),
                                               &header);
  char fingerprint[9] = "";
  if (parsed)
    seedkeeper_format_fingerprint_hex(header.fingerprint, fingerprint);

  size_t pos = 0;
  dst[0] = '\0';
  satochip_appendf(dst, dst_len, &pos, "%s\n",
                   title ? title : "View Secret");
  if (apdu)
    satochip_appendf(dst, dst_len, &pos, "SW: %04X\n", apdu->sw);
  if (apdu && apdu->detail[0])
    satochip_appendf(dst, dst_len, &pos, "%s\n", apdu->detail);
  if (!parsed) {
    satochip_appendf(dst, dst_len, &pos,
                     "Could not parse a text secret. Check the SID and export permission.");
    return;
  }

  satochip_appendf(dst, dst_len, &pos, "SID: %u\n",
                   (unsigned)header.id);
  if (header.label[0]) {
    satochip_appendf(dst, dst_len, &pos, "Label: %s\n", header.label);
  }
  satochip_appendf(dst, dst_len, &pos, "Fingerprint: %s\n\n%s",
                   fingerprint[0] ? fingerprint : "--", secret);
  secure_memzero(secret, sizeof(secret));
  secure_memzero(&header, sizeof(header));
}

static void satochip_format_seedkeeper_create_result(
    char *dst, size_t dst_len, const char *title, uint16_t sid,
    const uint8_t fingerprint[4], const char *mnemonic, bool loaded_ok,
    size_t slot_index) {
  if (!dst || dst_len == 0)
    return;

  size_t pos = 0;
  dst[0] = '\0';
  satochip_appendf(dst, dst_len, &pos, "%s\n",
                   title ? title : "Smartcard Create");
  if (sid)
    satochip_appendf(dst, dst_len, &pos, "SID: %u\n", (unsigned)sid);
  if (mnemonic && mnemonic[0])
    satochip_appendf(dst, dst_len, &pos, "\n%s", mnemonic);
  satochip_appendf(dst, dst_len, &pos, "\n\nLoad: %s%s%u",
                   loaded_ok ? "success, slot " : "failed",
                   loaded_ok ? "" : ".",
                   loaded_ok ? (unsigned)(slot_index + 1U) : 0U);
  char wallet_fp[9] = "";
  if (mnemonic && mnemonic[0])
    (void)seedkeeper_wallet_fingerprint_hex_from_mnemonic(mnemonic, "",
                                                          wallet_fp);
  if (!wallet_fp[0] && fingerprint)
    seedkeeper_format_fingerprint_hex(fingerprint, wallet_fp);
  satochip_appendf(dst, dst_len, &pos, "\nFingerprint: %s",
                   wallet_fp[0] ? wallet_fp : "--");
}

static void satochip_format_certificate_export(
    const smartcard_satochip_certificate_t *cert, char *dst, size_t dst_len) {
  if (!dst || dst_len == 0)
    return;
  dst[0] = '\0';
  size_t pos = 0;
  if (!cert) {
    satochip_appendf(dst, dst_len, &pos, "No certificate result.");
    return;
  }

  satochip_appendf(dst, dst_len, &pos, "SW: %04X\n", cert->sw);
  if (cert->detail[0])
    satochip_appendf(dst, dst_len, &pos, "%s\n", cert->detail);
  if (cert->has_certificate && cert->pem[0]) {
    satochip_appendf(dst, dst_len, &pos, "\n%s", cert->pem);
  }
}

static void satochip_format_authenticity(
    const smartcard_satochip_authenticity_t *auth, char *dst, size_t dst_len) {
  if (!dst || dst_len == 0)
    return;
  dst[0] = '\0';
  size_t pos = 0;
  if (!auth) {
    satochip_appendf(dst, dst_len, &pos, "No authenticity result.");
    return;
  }

  satochip_appendf(dst, dst_len, &pos, "SW: %04X\n", auth->sw);
  satochip_appendf(dst, dst_len, &pos, "Result: %s\n",
                   auth->authentic ? "Passed" : "Failed");
  if (auth->subject_cn[0])
    satochip_appendf(dst, dst_len, &pos, "CN: %s\n", auth->subject_cn);
  if (auth->error[0])
    satochip_appendf(dst, dst_len, &pos, "%s\n", auth->error);
  if (auth->ca_text[0])
    satochip_appendf(dst, dst_len, &pos, "\nCA\n%s\n", auth->ca_text);
  if (auth->subca_text[0])
    satochip_appendf(dst, dst_len, &pos, "\nSubCA\n%s\n", auth->subca_text);
  if (auth->device_text[0])
    satochip_appendf(dst, dst_len, &pos, "\nDevice\n%s\n", auth->device_text);
  if (auth->has_certificate && auth->certificate_pem[0])
    satochip_appendf(dst, dst_len, &pos, "\n%s\n", auth->certificate_pem);
}

static void satochip_format_seedkeeper_header_list_result(
    const smartcard_seedkeeper_header_list_t *list, char *dst, size_t dst_len) {
  if (!dst || dst_len == 0)
    return;
  dst[0] = '\0';
  size_t pos = 0;

  if (!list) {
    satochip_appendf(dst, dst_len, &pos, "No list result.");
    return;
  }

  satochip_appendf(dst, dst_len, &pos, "SeedKeeper List\n");
  satochip_appendf(dst, dst_len, &pos, "SW: %04X\n", list->sw);
  if (list->detail[0])
    satochip_appendf(dst, dst_len, &pos, "%s\n", list->detail);
  satochip_appendf(dst, dst_len, &pos, "Items: %u\n",
                   (unsigned)list->count);

  if (list->count == 0) {
    satochip_appendf(dst, dst_len, &pos, "No items.");
    return;
  }

  for (size_t i = 0; i < list->count; i++) {
    const smartcard_seedkeeper_header_t *header = &list->headers[i];
    char fingerprint[9];
    if (seedkeeper_header_is_loadable_mnemonic(header))
      seedkeeper_header_mnemonic_fingerprint_hex(header, fingerprint);
    else
      seedkeeper_format_fingerprint_hex(header->fingerprint, fingerprint);
    satochip_appendf(dst, dst_len, &pos,
                     "\n#%u ID=%u\n"
                     "%s: %s\n"
                     "Type: %u/%u Origin: %u Rights: %u Count: %u\n",
                     (unsigned)(i + 1U), (unsigned)header->id,
                     seedkeeper_header_is_loadable_mnemonic(header) ||
                             seedkeeper_header_is_generic_secret(header)
                         ? "Fingerprint"
                         : "Label",
                     seedkeeper_header_is_loadable_mnemonic(header) ||
                             seedkeeper_header_is_generic_secret(header)
                         ? (fingerprint[0] ? fingerprint : "--------")
                         : (header->label[0] ? header->label : "(no label)"),
                     (unsigned)header->type, (unsigned)header->subtype,
                     (unsigned)header->origin,
                     (unsigned)header->export_rights,
                     (unsigned)header->export_counter);
  }

  satochip_appendf(dst, dst_len, &pos, "\n\nTap an item to view or import.");
}

static const char *
satochip_seedkeeper_stub_title(satochip_maint_mode_t mode) {
  switch (mode) {
  case SATOCHIP_MAINT_SEEDKEEPER_SAVE_SECRET:
    return "Save Secret";
  case SATOCHIP_MAINT_SEEDKEEPER_LOAD_DESCRIPTOR:
    return "Load Descriptor";
  case SATOCHIP_MAINT_SEEDKEEPER_SAVE_DESCRIPTOR:
    return "Save Descriptor";
  case SATOCHIP_MAINT_SEEDKEEPER_CLONE:
    return "Clone";
  case SATOCHIP_MAINT_SEEDKEEPER_SETUP_PIN:
    return "Setup PIN";
  case SATOCHIP_MAINT_SEEDKEEPER_RESET:
  case SATOCHIP_MAINT_SEEDKEEPER_RESET_PIN_STEP:
  case SATOCHIP_MAINT_SEEDKEEPER_RESET_PUK_STEP:
    return "清空SeedKeeper";
  case SATOCHIP_MAINT_SEEDKEEPER_RESET_SECRET:
    return "删除条目";
  case SATOCHIP_MAINT_SEEDKEEPER_GENERATE_MASTERSEED:
    return "Masterseed";
  case SATOCHIP_MAINT_SEEDKEEPER_GENERATE_2FA_SECRET:
    return "2FA";
  case SATOCHIP_MAINT_SEEDKEEPER_DERIVE_MASTER_PASSWORD:
    return "Derive Password";
  default:
    return "SeedKeeper";
  }
}

static bool satochip_maint_mode_requires_pin(satochip_maint_mode_t mode) {
  return mode == SATOCHIP_MAINT_SETUP_PIN ||
         mode == SATOCHIP_MAINT_CHANGE_PIN ||
         mode == SATOCHIP_MAINT_SEEDKEEPER_SETUP_PIN ||
         mode == SATOCHIP_MAINT_SEEDKEEPER_CHANGE_PIN ||
         mode == SATOCHIP_MAINT_RESET_FACTORY ||
         mode == SATOCHIP_MAINT_SET_LABEL ||
         mode == SATOCHIP_MAINT_NFC_POLICY ||
         mode == SATOCHIP_MAINT_FEATURE_POLICY ||
         mode == SATOCHIP_MAINT_RESET_SEED ||
         mode == SATOCHIP_MAINT_SATOCHIP_WRITE_MNEMONIC ||
         mode == SATOCHIP_MAINT_SEEDKEEPER_CREATE_MNEMONIC ||
         mode == SATOCHIP_MAINT_SEEDKEEPER_WRITE_MNEMONIC ||
         mode == SATOCHIP_MAINT_SEEDKEEPER_VIEW_MNEMONIC ||
         mode == SATOCHIP_MAINT_SEEDKEEPER_LOAD_MNEMONIC ||
         mode == SATOCHIP_MAINT_SEEDKEEPER_SAVE_SECRET ||
         mode == SATOCHIP_MAINT_SEEDKEEPER_LOAD_DESCRIPTOR ||
         mode == SATOCHIP_MAINT_SEEDKEEPER_SAVE_DESCRIPTOR ||
         mode == SATOCHIP_MAINT_SEEDKEEPER_CLONE ||
         mode == SATOCHIP_MAINT_SEEDKEEPER_GENERATE_MASTERSEED ||
         mode == SATOCHIP_MAINT_SEEDKEEPER_GENERATE_2FA_SECRET ||
         mode == SATOCHIP_MAINT_SEEDKEEPER_DERIVE_MASTER_PASSWORD ||
         mode == SATOCHIP_MAINT_SEEDKEEPER_RESET_SECRET;
}

static bool satochip_legacy_seedkeeper_id(const char *id) {
  return id && (strcmp(id, "smartcard_seedkeeper") == 0 ||
                strcmp(id, "smartcard_seedkeeper_status") == 0 ||
                strcmp(id, "smartcard_seedkeeper_list") == 0 ||
                strcmp(id, "smartcard_seedkeeper_logs") == 0 ||
                strcmp(id, "seedkeeper_status") == 0 ||
                strcmp(id, "seedkeeper_list") == 0 ||
                strcmp(id, "seedkeeper_logs") == 0);
}

static bool satochip_screen_uses_onscreen_keyboard(const char *id) {
  return id &&
         (is_web3_satochip_choice(id) ||
          strcmp(id, "smartcard_satochip_setup_pin") == 0 ||
          strcmp(id, "smartcard_satochip_change_pin") == 0 ||
          strcmp(id, "smartcard_satochip_unblock_pin") == 0 ||
          strcmp(id, "smartcard_satochip_set_label") == 0 ||
          strcmp(id, "smartcard_satochip_nfc_policy") == 0 ||
          strcmp(id, "smartcard_satochip_feature_policy") == 0 ||
          strcmp(id, "smartcard_satochip_reset_seed") == 0 ||
          strcmp(id, "smartcard_satochip_reset_factory") == 0 ||
          strcmp(id, "smartcard_satochip_write_mnemonic") == 0 ||
          strcmp(id, "smartcard_satochip_import_ndef_authentikey") == 0 ||
          strcmp(id, "smartcard_satochip_import_trusted_pubkey") == 0 ||
          strcmp(id, "smartcard_satochip_set_2fa_key") == 0 ||
          strcmp(id, "smartcard_satochip_reset_2fa_key") == 0 ||
          strcmp(id, "smartcard_seedkeeper_status_page") == 0 ||
          strcmp(id, "smartcard_seedkeeper_free_space") == 0 ||
          strcmp(id, "smartcard_seedkeeper_list_page") == 0 ||
          strcmp(id, "smartcard_seedkeeper_create_mnemonic") == 0 ||
          strcmp(id, "smartcard_seedkeeper_write_mnemonic") == 0 ||
          strcmp(id, "smartcard_seedkeeper_view_mnemonic") == 0 ||
          strcmp(id, "smartcard_seedkeeper_load_mnemonic") == 0 ||
          strcmp(id, "smartcard_seedkeeper_save_secret") == 0 ||
          strcmp(id, "smartcard_seedkeeper_load_descriptor") == 0 ||
          strcmp(id, "smartcard_seedkeeper_save_descriptor") == 0 ||
          strcmp(id, "smartcard_seedkeeper_clone") == 0 ||
          strcmp(id, "smartcard_seedkeeper_setup_pin") == 0 ||
          strcmp(id, "smartcard_seedkeeper_change_pin") == 0 ||
          strcmp(id, "smartcard_seedkeeper_reset") == 0 ||
          strcmp(id, "smartcard_seedkeeper_generate_masterseed") == 0 ||
          strcmp(id, "smartcard_seedkeeper_generate_2fa_secret") == 0 ||
          strcmp(id, "smartcard_seedkeeper_derive_master_password") == 0 ||
          strcmp(id, "smartcard_seedkeeper_reset_secret") == 0 ||
          strcmp(id, "new_seedkeeper_create_mnemonic") == 0 ||
          strcmp(id, "load_seedkeeper_mnemonic") == 0 ||
          strcmp(id, "smartcard_certificate_import") == 0 ||
          strcmp(id, "satochip_path_address") == 0 ||
          strcmp(id, "connect_wallet_satochip_address") == 0 ||
          strcmp(id, "satochip_btc_zpub") == 0 ||
          strcmp(id, "satochip_btc_xpub") == 0 ||
          strcmp(id, "satochip_btc_ypub") == 0 ||
          strcmp(id, "satochip_btc_tpub") == 0 ||
          strcmp(id, "satochip_btc_upub") == 0 ||
          strcmp(id, "satochip_btc_vpub") == 0 ||
          strcmp(id, "btc_satochip_zpub") == 0 ||
          strcmp(id, "btc_satochip_xpub") == 0);
}

static void satochip_maint_attach_primary_input(lv_obj_t *parent,
                                                const char *placeholder,
                                                bool password_mode,
                                                bool multiline,
                                                size_t max_len,
                                                const char *default_text) {
  memset(&s_satochip_maint_input, 0, sizeof(s_satochip_maint_input));
  s_satochip_maint_input.textarea = lv_textarea_create(parent);
  s_satochip_maint_input_active = true;
  lv_obj_t *textarea = s_satochip_maint_input.textarea;
  lv_obj_set_width(textarea, LV_PCT(100));
  lv_obj_set_height(textarea, multiline ? 128 : 64);
  lv_textarea_set_one_line(textarea, !multiline);
  lv_textarea_set_password_mode(textarea, password_mode);
  lv_textarea_set_placeholder_text(textarea, placeholder ? placeholder : "");
  theme_apply_textarea(textarea, false);
  if (default_text && default_text[0])
    lv_textarea_set_text(textarea, default_text);
  if (max_len > 0)
    lv_textarea_set_max_length(textarea, max_len);
  lv_obj_set_style_text_font(textarea, theme_font_small(), 0);
  lv_obj_set_style_text_color(textarea, main_color(), 0);
  lv_obj_set_style_bg_color(textarea, bg_color(), 0);
  lv_obj_set_style_bg_opa(textarea, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(textarea, highlight_color(), 0);
  lv_obj_set_style_border_width(textarea, 2, 0);
  lv_obj_set_style_radius(textarea, 8, 0);
  lv_obj_set_style_pad_top(textarea, 8, 0);
  lv_obj_set_style_pad_bottom(textarea, 8, 0);
  lv_obj_set_style_pad_left(textarea, 12, 0);
  lv_obj_set_style_pad_right(textarea, 12, 0);
  ui_textarea_enable_safe_keyboard_shortcuts(textarea);
  lv_obj_add_event_cb(textarea, satochip_maint_focus_primary_cb,
                      LV_EVENT_FOCUSED, NULL);
  lv_obj_add_event_cb(textarea, satochip_maint_focus_primary_cb,
                      LV_EVENT_CLICKED, NULL);
  s_satochip_maint_input.input_group = lv_group_create();
  lv_group_add_obj(s_satochip_maint_input.input_group, textarea);
  lv_group_focus_obj(textarea);

  s_satochip_maint_input.keyboard = lv_keyboard_create(lv_screen_active());
  lv_obj_set_size(s_satochip_maint_input.keyboard, LV_HOR_RES,
                  LV_VER_RES * 34 / 100);
  lv_obj_align(s_satochip_maint_input.keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_textarea(s_satochip_maint_input.keyboard, textarea);
  lv_keyboard_set_mode(s_satochip_maint_input.keyboard,
                       LV_KEYBOARD_MODE_TEXT_LOWER);
  ui_keyboard_apply_safe_text_map(s_satochip_maint_input.keyboard);
  lv_obj_set_style_bg_color(s_satochip_maint_input.keyboard, lv_color_black(),
                            0);
  lv_obj_set_style_border_width(s_satochip_maint_input.keyboard, 0, 0);
  lv_obj_set_style_pad_all(s_satochip_maint_input.keyboard, 4, 0);
  lv_obj_set_style_pad_gap(s_satochip_maint_input.keyboard, 6, 0);
  lv_obj_set_style_bg_color(s_satochip_maint_input.keyboard, bg_color(),
                            LV_PART_ITEMS);
  lv_obj_set_style_text_color(s_satochip_maint_input.keyboard, main_color(),
                              LV_PART_ITEMS);
  lv_obj_set_style_bg_color(s_satochip_maint_input.keyboard, bg_color(),
                            LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_text_color(s_satochip_maint_input.keyboard, main_color(),
                              LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_text_font(s_satochip_maint_input.keyboard,
                             theme_font_small(), LV_PART_ITEMS);
  lv_obj_set_style_border_color(s_satochip_maint_input.keyboard,
                                highlight_color(), LV_PART_ITEMS);
  lv_obj_set_style_border_color(s_satochip_maint_input.keyboard,
                                highlight_color(),
                                LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_border_width(s_satochip_maint_input.keyboard, 1,
                                LV_PART_ITEMS);
  lv_obj_set_style_border_width(s_satochip_maint_input.keyboard, 1,
                                LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_radius(s_satochip_maint_input.keyboard, 6, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(s_satochip_maint_input.keyboard, highlight_color(),
                            LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_add_flag(s_satochip_maint_input.keyboard, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *satochip_maint_attach_extra_field(lv_obj_t *parent,
                                                   const char *label_text,
                                                   const char *placeholder,
                                                   const char *default_text,
                                                   bool password_mode,
                                                   bool multiline,
                                                   size_t max_len,
                                                   lv_obj_t **slot) {
  if (label_text && label_text[0])
    create_text(parent, label_text, false, highlight_color());

  lv_obj_t *textarea = lv_textarea_create(parent);
  lv_obj_set_width(textarea, LV_PCT(100));
  lv_obj_set_height(textarea, multiline ? 128 : 64);
  lv_textarea_set_one_line(textarea, !multiline);
  lv_textarea_set_password_mode(textarea, password_mode);
  lv_textarea_set_placeholder_text(textarea, placeholder ? placeholder : "");
  theme_apply_textarea(textarea, false);
  if (default_text && default_text[0])
    lv_textarea_set_text(textarea, default_text);
  if (max_len > 0)
    lv_textarea_set_max_length(textarea, max_len);
  lv_obj_set_style_text_font(textarea, theme_font_small(), 0);
  lv_obj_set_style_text_color(textarea, main_color(), 0);
  lv_obj_set_style_bg_color(textarea, bg_color(), 0);
  lv_obj_set_style_bg_opa(textarea, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(textarea, highlight_color(), 0);
  lv_obj_set_style_border_width(textarea, 2, 0);
  lv_obj_set_style_radius(textarea, 8, 0);
  lv_obj_set_style_pad_top(textarea, 8, 0);
  lv_obj_set_style_pad_bottom(textarea, 8, 0);
  lv_obj_set_style_pad_left(textarea, 12, 0);
  lv_obj_set_style_pad_right(textarea, 12, 0);
  ui_textarea_enable_safe_keyboard_shortcuts(textarea);
  lv_obj_add_event_cb(textarea, satochip_maint_focus_extra_cb,
                      LV_EVENT_FOCUSED, textarea);
  lv_obj_add_event_cb(textarea, satochip_maint_focus_extra_cb,
                      LV_EVENT_CLICKED, textarea);
  if (s_satochip_maint_input.input_group)
    lv_group_add_obj(s_satochip_maint_input.input_group, textarea);
  if (slot)
    *slot = textarea;
  return textarea;
}

static void satochip_maint_task(void *arg) {
  (void)arg;
  const bool delete_with_caps = s_satochip_maint_task_with_caps;
  s_satochip_maint_task_err = ESP_ERR_INVALID_STATE;
  s_satochip_maint_result[0] = '\0';
  satochip_maint_log_stack_watermark("start");

  smartcard_satochip_apdu_result_t *apdu =
      heap_caps_calloc(1, sizeof(*apdu), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  smartcard_satochip_certificate_t *cert =
      heap_caps_calloc(1, sizeof(*cert), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  smartcard_satochip_authenticity_t *auth =
      heap_caps_calloc(1, sizeof(*auth), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!apdu || !cert || !auth) {
    if (!apdu)
      apdu = calloc(1, sizeof(*apdu));
    if (!cert)
      cert = calloc(1, sizeof(*cert));
    if (!auth)
      auth = calloc(1, sizeof(*auth));
  }
  if (!apdu || !cert || !auth) {
    s_satochip_maint_task_err = ESP_ERR_NO_MEM;
    snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
             "Out of memory. Smartcard operation canceled.");
    goto satochip_maint_done;
  }

  switch (s_satochip_maint_mode) {
  case SATOCHIP_MAINT_SETUP_PIN:
    s_satochip_maint_task_err =
        smartcard_satochip_card_setup_pin(s_satochip_maint_pin, apdu, 30000);
    satochip_format_apdu_result(s_satochip_maint_result,
                                sizeof(s_satochip_maint_result),
                                "Satochip Setup PIN", apdu);
    break;
  case SATOCHIP_MAINT_CHANGE_PIN: {
    s_satochip_maint_task_err = smartcard_satochip_card_change_pin(
        0, s_satochip_maint_pin, s_satochip_maint_text_a, apdu, 20000);
    satochip_format_apdu_result(s_satochip_maint_result,
                                sizeof(s_satochip_maint_result), "Change PIN",
                                apdu);
    break;
  }
  case SATOCHIP_MAINT_SEEDKEEPER_SETUP_PIN:
    s_satochip_maint_task_err = smartcard_seedkeeper_setup_pin(
        s_satochip_maint_pin, apdu, 30000);
    satochip_format_apdu_result(s_satochip_maint_result,
                                sizeof(s_satochip_maint_result),
                                "SeedKeeper Setup PIN", apdu);
    break;
  case SATOCHIP_MAINT_SEEDKEEPER_CHANGE_PIN: {
    s_satochip_maint_task_err = smartcard_seedkeeper_change_pin(
        0, s_satochip_maint_pin, s_satochip_maint_text_a, apdu, 20000);
    satochip_format_apdu_result(s_satochip_maint_result,
                                sizeof(s_satochip_maint_result),
                                "SeedKeeper Change PIN", apdu);
    break;
  }
  case SATOCHIP_MAINT_SEEDKEEPER_RESET_PIN_STEP:
    s_satochip_maint_task_err = smartcard_seedkeeper_reset_wrong_pin_step(
        s_satochip_maint_pin, apdu, 20000);
    satochip_format_apdu_result(s_satochip_maint_result,
                                sizeof(s_satochip_maint_result),
                                "SeedKeeper Wrong PIN", apdu);
    break;
  case SATOCHIP_MAINT_SEEDKEEPER_RESET_PUK_STEP:
    s_satochip_maint_task_err = smartcard_seedkeeper_reset_wrong_puk_step(
        s_satochip_maint_pin, apdu, 20000);
    satochip_format_apdu_result(s_satochip_maint_result,
                                sizeof(s_satochip_maint_result),
                                "SeedKeeper Wrong PUK", apdu);
    break;
  case SATOCHIP_MAINT_UNBLOCK_PIN: {
    size_t puk_len = strlen(s_satochip_maint_text_a);
    if (puk_len < 4) {
      s_satochip_maint_task_err = ESP_ERR_INVALID_ARG;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "PUK must be at least 4 characters.");
      break;
    }
    s_satochip_maint_task_err = smartcard_satochip_card_unblock_pin(
        0, (const uint8_t *)s_satochip_maint_text_a, puk_len, apdu, 20000);
    satochip_format_apdu_result(s_satochip_maint_result,
                                sizeof(s_satochip_maint_result), "Unblock PIN",
                                apdu);
    break;
  }
  case SATOCHIP_MAINT_SET_LABEL:
    s_satochip_maint_task_err = smartcard_satochip_card_set_label(
        s_satochip_maint_pin, s_satochip_maint_text_a, apdu, 20000);
    satochip_format_apdu_result(s_satochip_maint_result,
                                sizeof(s_satochip_maint_result), "Change Label",
                                apdu);
    break;
  case SATOCHIP_MAINT_NFC_POLICY: {
    uint8_t policy = 0;
    if (!satochip_parse_u8_text(s_satochip_maint_text_a, &policy) ||
        policy > 2) {
      s_satochip_maint_task_err = ESP_ERR_INVALID_ARG;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "NFC policy must be 0/1/2.");
      break;
    }
    s_satochip_maint_task_err = smartcard_satochip_card_set_nfc_policy(
        s_satochip_maint_pin, policy, apdu, 20000);
    satochip_format_apdu_result(s_satochip_maint_result,
                                sizeof(s_satochip_maint_result), "NFC Policy",
                                apdu);
    break;
  }
  case SATOCHIP_MAINT_FEATURE_POLICY: {
    uint8_t feature_id = 0;
    uint8_t policy = 0;
    if (!satochip_parse_u8_text(s_satochip_maint_text_a, &feature_id) ||
        !satochip_parse_u8_text(s_satochip_maint_text_b, &policy) ||
        feature_id > 2 || policy > 2) {
      s_satochip_maint_task_err = ESP_ERR_INVALID_ARG;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "Feature and policy must be 0/1/2.");
      break;
    }
    s_satochip_maint_task_err = smartcard_satochip_card_set_feature_policy(
        s_satochip_maint_pin, feature_id, policy, apdu, 20000);
    satochip_format_apdu_result(
        s_satochip_maint_result, sizeof(s_satochip_maint_result), "Feature Policy",
        apdu);
    break;
  }
  case SATOCHIP_MAINT_RESET_SEED: {
    uint8_t hmac[64];
    size_t hmac_len =
        satochip_parse_hex_bytes(s_satochip_maint_text_a, hmac, sizeof(hmac));
    s_satochip_maint_task_err = smartcard_satochip_card_reset_seed(
        s_satochip_maint_pin, hmac_len ? hmac : NULL, hmac_len, apdu, 20000);
    secure_memzero(hmac, sizeof(hmac));
    satochip_format_apdu_result(s_satochip_maint_result,
                                sizeof(s_satochip_maint_result), "Reset Seed",
                                apdu);
    break;
  }
  case SATOCHIP_MAINT_RESET_FACTORY:
    s_satochip_maint_task_err =
        smartcard_satochip_card_reset_factory_signal(apdu, 20000);
    satochip_format_apdu_result(s_satochip_maint_result,
                                sizeof(s_satochip_maint_result), "Factory Reset",
                                apdu);
    break;
  case SATOCHIP_MAINT_EXPORT_AUTHENTIKEY:
    s_satochip_maint_task_err = smartcard_satochip_card_export_authentikey(
        apdu, 20000);
    satochip_format_apdu_result(s_satochip_maint_result,
                                sizeof(s_satochip_maint_result), "Export Authentikey",
                                apdu);
    break;
  case SATOCHIP_MAINT_IMPORT_NDEF_AUTHENTIKEY: {
    uint8_t privkey[32];
    size_t privkey_len = satochip_parse_hex_bytes(s_satochip_maint_text_a,
                                                  privkey, sizeof(privkey));
    if (privkey_len != sizeof(privkey)) {
      s_satochip_maint_task_err = ESP_ERR_INVALID_ARG;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "Private key must be 32 bytes of hex.");
      secure_memzero(privkey, sizeof(privkey));
      break;
    }
    s_satochip_maint_task_err = smartcard_satochip_card_import_ndef_authentikey(
        privkey, apdu, 20000);
    secure_memzero(privkey, sizeof(privkey));
    satochip_format_apdu_result(s_satochip_maint_result,
                                sizeof(s_satochip_maint_result), "Write NDEF",
                                apdu);
    break;
  }
  case SATOCHIP_MAINT_IMPORT_TRUSTED_PUBKEY: {
    uint8_t pubkey[65];
    size_t pubkey_len = satochip_parse_hex_bytes(s_satochip_maint_text_a,
                                                 pubkey, sizeof(pubkey));
    if (pubkey_len != sizeof(pubkey)) {
      s_satochip_maint_task_err = ESP_ERR_INVALID_ARG;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "Public key must be 65 bytes of hex.");
      secure_memzero(pubkey, sizeof(pubkey));
      break;
    }
    if (pubkey[0] != 0x04) {
      s_satochip_maint_task_err = ESP_ERR_INVALID_ARG;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "Public key first byte must be 04.");
      secure_memzero(pubkey, sizeof(pubkey));
      break;
    }
    s_satochip_maint_task_err = smartcard_satochip_card_import_trusted_pubkey(
        pubkey, apdu, 20000);
    secure_memzero(pubkey, sizeof(pubkey));
    satochip_format_apdu_result(s_satochip_maint_result,
                                sizeof(s_satochip_maint_result), "Write Trusted Key",
                                apdu);
    break;
  }
  case SATOCHIP_MAINT_EXPORT_TRUSTED_PUBKEY:
    s_satochip_maint_task_err = smartcard_satochip_card_export_trusted_pubkey(
        apdu, 20000);
    satochip_format_apdu_result(s_satochip_maint_result,
                                sizeof(s_satochip_maint_result), "Export Trusted Key",
                                apdu);
    break;
  case SATOCHIP_MAINT_SATOCHIP_WRITE_MNEMONIC: {
#ifdef SIMULATOR
    const char *mnemonic =
        "abandon abandon abandon abandon abandon abandon abandon abandon "
        "abandon abandon abandon about";
    const char *passphrase = "";
    s_satochip_maint_task_err = smartcard_satochip_card_import_mnemonic_seed(
        s_satochip_maint_pin, mnemonic, passphrase, apdu, 30000);
#else
    char *mnemonic_owned = NULL;
    char *passphrase_owned = NULL;
    if (!key_get_mnemonic(&mnemonic_owned)) {
      s_satochip_maint_task_err = ESP_ERR_INVALID_STATE;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "Import or create a mnemonic first.");
      break;
    }
    (void)key_get_session_passphrase(&passphrase_owned);
    s_satochip_maint_task_err = smartcard_satochip_card_import_mnemonic_seed(
        s_satochip_maint_pin, mnemonic_owned,
        passphrase_owned ? passphrase_owned : "", apdu, 30000);
    SECURE_FREE_STRING(mnemonic_owned);
    SECURE_FREE_STRING(passphrase_owned);
#endif
    satochip_format_apdu_result(s_satochip_maint_result,
                                sizeof(s_satochip_maint_result),
                                "Write to Satochip", apdu);
    break;
  }
  case SATOCHIP_MAINT_SET_2FA_KEY: {
    uint8_t key[20];
    size_t key_len =
        satochip_parse_hex_bytes(s_satochip_maint_text_a, key, sizeof(key));
    uint64_t limit = 0;
    if (key_len != sizeof(key) ||
        !satochip_parse_u64_text(s_satochip_maint_text_b, &limit)) {
      s_satochip_maint_task_err = ESP_ERR_INVALID_ARG;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "2FA key must be 20 bytes and limit must be numeric.");
      secure_memzero(key, sizeof(key));
      break;
    }
    s_satochip_maint_task_err = smartcard_satochip_card_set_2fa_key(
        key, sizeof(key), limit, apdu, 20000);
    secure_memzero(key, sizeof(key));
    satochip_format_apdu_result(s_satochip_maint_result,
                                sizeof(s_satochip_maint_result), "Set 2FA",
                                apdu);
    break;
  }
  case SATOCHIP_MAINT_RESET_2FA_KEY: {
    uint8_t chal[20];
    size_t chal_len =
        satochip_parse_hex_bytes(s_satochip_maint_text_a, chal, sizeof(chal));
    if (chal_len != sizeof(chal)) {
      s_satochip_maint_task_err = ESP_ERR_INVALID_ARG;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "Challenge must be 20 bytes of hex.");
      secure_memzero(chal, sizeof(chal));
      break;
    }
    s_satochip_maint_task_err = smartcard_satochip_card_reset_2fa_key(
        chal, sizeof(chal), apdu, 20000);
    secure_memzero(chal, sizeof(chal));
    satochip_format_apdu_result(s_satochip_maint_result,
                                sizeof(s_satochip_maint_result), "Clear 2FA",
                                apdu);
    break;
  }
  case SATOCHIP_MAINT_LOGOUT_ALL:
    s_satochip_maint_task_err = smartcard_satochip_card_logout_all(apdu, 20000);
    satochip_format_apdu_result(s_satochip_maint_result,
                                sizeof(s_satochip_maint_result), "Logout",
                                apdu);
    break;
  case SATOCHIP_MAINT_SEEDKEEPER_STATUS:
  case SATOCHIP_MAINT_SEEDKEEPER_FREE_SPACE: {
    smartcard_seedkeeper_status_t *status = heap_caps_calloc(
        1, sizeof(*status), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!status)
      status = calloc(1, sizeof(*status));
    if (!status) {
      s_satochip_maint_task_err = ESP_ERR_NO_MEM;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "Out of memory. Could not read status.");
      break;
    }
    s_satochip_maint_task_err = smartcard_seedkeeper_read_status(
        s_satochip_maint_pin[0] ? s_satochip_maint_pin : NULL, status, 30000);
    smartcard_seedkeeper_format_status(status, s_satochip_maint_result,
                                       sizeof(s_satochip_maint_result));
    secure_memzero(status, sizeof(*status));
    free(status);
    break;
  }
  case SATOCHIP_MAINT_SEEDKEEPER_LIST: {
    smartcard_seedkeeper_header_list_t *list = heap_caps_calloc(
        1, sizeof(*list), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!list)
      list = calloc(1, sizeof(*list));
    if (!list) {
      s_satochip_maint_task_err = ESP_ERR_NO_MEM;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "Out of memory. Could not read list.");
      break;
    }
	      s_satochip_maint_task_err = smartcard_seedkeeper_list_secret_headers(
	          s_satochip_maint_pin[0] ? s_satochip_maint_pin : NULL, list, 30000);
	      satochip_format_seedkeeper_header_list_result(
	          list, s_satochip_maint_result, sizeof(s_satochip_maint_result));
    satochip_seedkeeper_store_last_list(list, SATOCHIP_MAINT_SEEDKEEPER_LIST);
    secure_memzero(list, sizeof(*list));
    free(list);
    break;
  }
  case SATOCHIP_MAINT_SEEDKEEPER_LOGS:
    s_satochip_maint_task_err = smartcard_seedkeeper_print_logs(
        s_satochip_maint_pin[0] ? s_satochip_maint_pin : NULL, apdu, 30000);
    satochip_format_apdu_result(s_satochip_maint_result,
                                sizeof(s_satochip_maint_result),
                                "SeedKeeper Logs", apdu);
    break;
  case SATOCHIP_MAINT_SEEDKEEPER_CREATE_MNEMONIC: {
    uint32_t words = 12;
    if (s_satochip_maint_text_a[0] &&
        !satochip_parse_u32_text(s_satochip_maint_text_a, &words)) {
      s_satochip_maint_task_err = ESP_ERR_INVALID_ARG;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "Invalid word count.");
      break;
    }

    size_t entropy_len = 0;
    if (words == 12)
      entropy_len = 16;
    else if (words == 15)
      entropy_len = 20;
    else if (words == 18)
      entropy_len = 24;
    else if (words == 21)
      entropy_len = 28;
    else if (words == 24)
      entropy_len = 32;
    else {
      s_satochip_maint_task_err = ESP_ERR_INVALID_ARG;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "Word count supports only 12/15/18/21/24.");
      break;
    }

    const char *label = "Mnemonic entropy";
    s_satochip_maint_text_b[0] = '\0';
    s_satochip_maint_task_err = smartcard_seedkeeper_generate_random_secret(
        s_satochip_maint_pin, SEEDKEEPER_TYPE_SECRET_KEY, 0x00,
        (uint8_t)entropy_len, SEEDKEEPER_EXPORT_PLAINTEXT_ALLOWED, label,
        false, NULL, 0, apdu, 30000);
    if (s_satochip_maint_task_err != ESP_OK || apdu->sw != 0x9000 ||
        apdu->response_len < 6) {
      satochip_format_apdu_result(s_satochip_maint_result,
                                  sizeof(s_satochip_maint_result),
                                  "Smartcard Create Failed", apdu);
      break;
    }

    uint16_t sid = ((uint16_t)apdu->response[0] << 8) | apdu->response[1];
    uint8_t fp[4];
    memcpy(fp, apdu->response + 2, sizeof(fp));

    memset(apdu, 0, sizeof(*apdu));
    s_satochip_maint_task_err = smartcard_seedkeeper_export_secret(
        s_satochip_maint_pin, sid, 0, false, apdu, 30000);
    if (s_satochip_maint_task_err != ESP_OK || apdu->sw != 0x9000) {
      satochip_format_apdu_result(s_satochip_maint_result,
                                  sizeof(s_satochip_maint_result),
                                  "Read Random Secret Failed", apdu);
      secure_memzero(fp, sizeof(fp));
      break;
    }

    uint8_t entropy[32];
    memset(entropy, 0, sizeof(entropy));
    smartcard_seedkeeper_header_t header;
    if (!seedkeeper_extract_random_secret_bytes(apdu, entropy, entropy_len,
                                                &header)) {
      s_satochip_maint_task_err = ESP_ERR_INVALID_RESPONSE;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "Random secret parse failed.");
      secure_memzero(entropy, sizeof(entropy));
      secure_memzero(fp, sizeof(fp));
      break;
    }

#ifndef SIMULATOR
    char entropy_hex[65];
    satochip_hex_bytes_upper(entropy, entropy_len, entropy_hex,
                             sizeof(entropy_hex));
    char *mnemonic = mnemonic_tools_from_hex_entropy(entropy_hex);
    if (!mnemonic) {
      s_satochip_maint_task_err = ESP_ERR_INVALID_RESPONSE;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "Mnemonic generation failed.");
      secure_memzero(entropy_hex, sizeof(entropy_hex));
      secure_memzero(entropy, sizeof(entropy));
      secure_memzero(fp, sizeof(fp));
      break;
    }
#else
    char *mnemonic = strdup(
        "abandon abandon abandon abandon abandon abandon abandon abandon "
        "abandon abandon abandon about");
    if (!mnemonic) {
      s_satochip_maint_task_err = ESP_ERR_NO_MEM;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "Out of memory.");
      secure_memzero(entropy, sizeof(entropy));
      secure_memzero(fp, sizeof(fp));
      break;
    }
#endif

    uint8_t mnemonic_header[15 + SEEDKEEPER_LABEL_MAX_BYTES];
    uint8_t mnemonic_secret[2 + SEEDKEEPER_MNEMONIC_MAX_BYTES * 2];
    size_t mnemonic_header_len = 0;
    size_t mnemonic_secret_len = 0;
    uint16_t mnemonic_sid = 0;
    bool stored_on_card = false;
    size_t slot_index = 0;
    bool loaded_ok = false;
    bool card_copy_removed = false;
    char wallet_fp[9] = "";
    char temp_fp[9] = "";
    seedkeeper_format_fingerprint_hex(fp, temp_fp);

    if (!seedkeeper_build_mnemonic_header_from_text(
            mnemonic, "", mnemonic_header, sizeof(mnemonic_header),
            &mnemonic_header_len, wallet_fp, sizeof(wallet_fp)) ||
        !seedkeeper_build_mnemonic_secret(mnemonic, "", mnemonic_secret,
                                          sizeof(mnemonic_secret),
                                          &mnemonic_secret_len)) {
      s_satochip_maint_task_err = ESP_ERR_INVALID_RESPONSE;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "Mnemonic packaging failed.");
      SECURE_FREE_STRING(mnemonic);
#ifndef SIMULATOR
      secure_memzero(entropy_hex, sizeof(entropy_hex));
#endif
      secure_memzero(entropy, sizeof(entropy));
      secure_memzero(fp, sizeof(fp));
      secure_memzero(mnemonic_header, sizeof(mnemonic_header));
      secure_memzero(mnemonic_secret, sizeof(mnemonic_secret));
      secure_memzero(wallet_fp, sizeof(wallet_fp));
      secure_memzero(temp_fp, sizeof(temp_fp));
      s_satochip_seedkeeper_ephemeral_create = false;
      break;
    }

    memset(apdu, 0, sizeof(*apdu));
    s_satochip_maint_task_err = smartcard_seedkeeper_import_secret(
        s_satochip_maint_pin, mnemonic_header, mnemonic_header_len,
        mnemonic_secret, mnemonic_secret_len, 0, NULL, 0, NULL, 0, false,
        apdu, 30000);
    if (s_satochip_maint_task_err == ESP_OK && apdu->sw == 0x9000 &&
        apdu->response_len >= 2) {
      mnemonic_sid = ((uint16_t)apdu->response[0] << 8) | apdu->response[1];
      stored_on_card = true;
    }

    loaded_ok = seedkeeper_load_mnemonic_into_session(mnemonic, "", &slot_index);

    memset(apdu, 0, sizeof(*apdu));
    {
      esp_err_t cleanup_err = smartcard_seedkeeper_reset_secret(
          s_satochip_maint_pin, sid, apdu, 30000);
      card_copy_removed = (cleanup_err == ESP_OK && apdu->sw == 0x9000);
    }

    size_t pos = 0;
    s_satochip_maint_result[0] = '\0';
    satochip_appendf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
                     &pos, "Smartcard Create\n");
    satochip_appendf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
                     &pos, "Mnemonic:\n%s\n\n", mnemonic);
    satochip_appendf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
                     &pos, "Fingerprint: %s\n",
                     wallet_fp[0] ? wallet_fp : temp_fp);
    satochip_appendf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
                     &pos, "Device load: %s",
                     loaded_ok ? "success" : "failed");
    if (loaded_ok) {
      satochip_appendf(s_satochip_maint_result,
                       sizeof(s_satochip_maint_result), &pos, ", slot %u\n",
                       (unsigned)(slot_index + 1U));
    } else {
      satochip_appendf(s_satochip_maint_result,
                       sizeof(s_satochip_maint_result), &pos, "\n");
    }
    satochip_appendf(
        s_satochip_maint_result, sizeof(s_satochip_maint_result), &pos,
        "SeedKeeper save: %s",
        stored_on_card ? "success" : "failed");
    if (stored_on_card) {
      satochip_appendf(s_satochip_maint_result,
                       sizeof(s_satochip_maint_result), &pos, ", SID %u\n",
                       (unsigned)mnemonic_sid);
    } else {
      satochip_appendf(s_satochip_maint_result,
                       sizeof(s_satochip_maint_result), &pos, "\n");
    }
    satochip_appendf(s_satochip_maint_result,
                     sizeof(s_satochip_maint_result), &pos,
                     "Temporary entropy item: %s",
                     card_copy_removed ? "removed" : "delete failed");
    if (!card_copy_removed) {
      satochip_appendf(s_satochip_maint_result,
                       sizeof(s_satochip_maint_result), &pos, ", SID %u",
                       (unsigned)sid);
    }

    s_satochip_maint_task_err =
        (loaded_ok && stored_on_card && card_copy_removed) ? ESP_OK
                                                           : ESP_ERR_INVALID_STATE;
    SECURE_FREE_STRING(mnemonic);
#ifndef SIMULATOR
    secure_memzero(entropy_hex, sizeof(entropy_hex));
#endif
    secure_memzero(entropy, sizeof(entropy));
    secure_memzero(fp, sizeof(fp));
    secure_memzero(mnemonic_header, sizeof(mnemonic_header));
    secure_memzero(mnemonic_secret, sizeof(mnemonic_secret));
    secure_memzero(wallet_fp, sizeof(wallet_fp));
    secure_memzero(temp_fp, sizeof(temp_fp));
    s_satochip_seedkeeper_ephemeral_create = false;
    break;
  }
  case SATOCHIP_MAINT_SEEDKEEPER_WRITE_MNEMONIC: {
#ifdef SIMULATOR
    const char *mnemonic =
        "abandon abandon abandon abandon abandon abandon abandon abandon "
        "abandon abandon abandon about";
#else
    char *mnemonic_owned = NULL;
    if (!key_get_mnemonic(&mnemonic_owned)) {
      s_satochip_maint_task_err = ESP_ERR_INVALID_STATE;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "Import or create a mnemonic first.");
      break;
    }
    const char *mnemonic = mnemonic_owned;
#endif
    uint8_t header[15 + SEEDKEEPER_LABEL_MAX_BYTES];
    uint8_t secret[2 + SEEDKEEPER_MNEMONIC_MAX_BYTES * 2];
    uint8_t secret_hash[CRYPTO_SHA256_SIZE];
    uint8_t header_fingerprint[4];
    char wallet_fp[9] = "";
    size_t header_len = 0;
    size_t secret_len = 0;
    const char *passphrase =
        s_satochip_maint_text_a[0] ? s_satochip_maint_text_a : "";
    memset(header_fingerprint, 0, sizeof(header_fingerprint));
    if (!seedkeeper_build_mnemonic_secret(mnemonic, passphrase, secret,
                                          sizeof(secret), &secret_len) ||
        crypto_sha256(secret, secret_len, secret_hash) != CRYPTO_OK) {
      s_satochip_maint_task_err = ESP_ERR_INVALID_ARG;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "Mnemonic or passphrase is too long.");
#ifndef SIMULATOR
      SECURE_FREE_STRING(mnemonic_owned);
#endif
      secure_memzero(header, sizeof(header));
      secure_memzero(secret, sizeof(secret));
      secure_memzero(secret_hash, sizeof(secret_hash));
      secure_memzero(header_fingerprint, sizeof(header_fingerprint));
      break;
    }
    if (!seedkeeper_fingerprint_from_mnemonic(mnemonic, passphrase,
                                              header_fingerprint))
      memcpy(header_fingerprint, secret_hash, sizeof(header_fingerprint));
    if (!seedkeeper_wallet_fingerprint_hex_from_mnemonic(mnemonic, passphrase,
                                                         wallet_fp) ||
        !wallet_fp[0]) {
      s_satochip_maint_task_err = ESP_ERR_INVALID_RESPONSE;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "Mnemonic fingerprint failed.");
#ifndef SIMULATOR
      SECURE_FREE_STRING(mnemonic_owned);
#endif
      secure_memzero(header, sizeof(header));
      secure_memzero(secret, sizeof(secret));
      secure_memzero(secret_hash, sizeof(secret_hash));
      secure_memzero(header_fingerprint, sizeof(header_fingerprint));
      secure_memzero(wallet_fp, sizeof(wallet_fp));
      break;
    }
	    if (!seedkeeper_build_plain_header(
	            SEEDKEEPER_TYPE_BIP39_MNEMONIC, 0x00,
	            SEEDKEEPER_EXPORT_PLAINTEXT_ALLOWED, wallet_fp,
	            header_fingerprint, header, sizeof(header), &header_len)) {
      s_satochip_maint_task_err = ESP_ERR_INVALID_ARG;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "Mnemonic fingerprint is too long.");
#ifndef SIMULATOR
      SECURE_FREE_STRING(mnemonic_owned);
#endif
      secure_memzero(header, sizeof(header));
      secure_memzero(secret, sizeof(secret));
      secure_memzero(secret_hash, sizeof(secret_hash));
      secure_memzero(header_fingerprint, sizeof(header_fingerprint));
      break;
    }
    s_satochip_maint_task_err = smartcard_seedkeeper_import_secret(
        s_satochip_maint_pin, header, header_len, secret, secret_len, 0, NULL, 0,
        NULL, 0, false, apdu, 30000);
	    satochip_format_seedkeeper_write_mnemonic_result(
	        s_satochip_maint_result, sizeof(s_satochip_maint_result), apdu,
	        wallet_fp);
#ifndef SIMULATOR
    SECURE_FREE_STRING(mnemonic_owned);
#endif
    secure_memzero(header, sizeof(header));
    secure_memzero(secret, sizeof(secret));
    secure_memzero(secret_hash, sizeof(secret_hash));
    secure_memzero(header_fingerprint, sizeof(header_fingerprint));
	    secure_memzero(wallet_fp, sizeof(wallet_fp));
	    break;
  }
  case SATOCHIP_MAINT_SEEDKEEPER_VIEW_MNEMONIC:
  case SATOCHIP_MAINT_SEEDKEEPER_LOAD_MNEMONIC: {
    uint16_t sid = 0;
    bool sid_from_selected_item = satochip_seedkeeper_lookup_has_selected_item();
    if (sid_from_selected_item) {
      sid = s_satochip_seedkeeper_selected_header.id;
    } else if (s_satochip_maint_text_a[0] == '\0') {
      smartcard_seedkeeper_header_list_t *list = heap_caps_calloc(
          1, sizeof(*list), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (!list)
        list = calloc(1, sizeof(*list));
      if (!list) {
        s_satochip_maint_task_err = ESP_ERR_NO_MEM;
        snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
                 "Out of memory. Could not read list.");
        break;
      }
      s_satochip_maint_task_err = smartcard_seedkeeper_list_secret_headers(
          s_satochip_maint_pin[0] ? s_satochip_maint_pin : NULL, list,
          30000);
      satochip_format_seedkeeper_header_list_result(
          list, s_satochip_maint_result, sizeof(s_satochip_maint_result));
      satochip_seedkeeper_store_last_list(list, s_satochip_maint_mode);
      if (s_satochip_maint_task_err == ESP_OK)
        s_satochip_seedkeeper_lookup_stage = SATOCHIP_SEEDKEEPER_LOOKUP_LIST;
      secure_memzero(list, sizeof(*list));
      free(list);
      break;
    }
    if (!sid_from_selected_item &&
        !satochip_parse_u16_text(s_satochip_maint_text_a, &sid)) {
      s_satochip_maint_task_err = ESP_ERR_INVALID_ARG;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "Invalid SID.");
      break;
    }

    const smartcard_seedkeeper_header_t *selected_header =
        s_satochip_seedkeeper_selected_header_valid
            ? &s_satochip_seedkeeper_selected_header
            : satochip_seedkeeper_find_last_header(sid);

    if (s_satochip_seedkeeper_item_op ==
        SATOCHIP_SEEDKEEPER_ITEM_OP_DELETE) {
      s_satochip_maint_task_err =
          smartcard_seedkeeper_reset_secret(s_satochip_maint_pin, sid, apdu,
                                            30000);
      satochip_format_apdu_result(s_satochip_maint_result,
                                  sizeof(s_satochip_maint_result),
                                  "Delete Item", apdu);
      if (s_satochip_maint_task_err == ESP_OK && apdu->sw == 0x9000) {
        s_satochip_seedkeeper_lookup_stage = SATOCHIP_SEEDKEEPER_LOOKUP_LIST;
        s_satochip_seedkeeper_selected_sid = 0;
        s_satochip_seedkeeper_selected_header_valid = false;
        secure_memzero(&s_satochip_seedkeeper_selected_header,
                       sizeof(s_satochip_seedkeeper_selected_header));
        s_satochip_seedkeeper_last_list_valid = false;
      }
      break;
    }

    if (s_satochip_seedkeeper_item_op ==
        SATOCHIP_SEEDKEEPER_ITEM_OP_UPDATE) {
      if (!seedkeeper_header_is_password(selected_header)) {
        s_satochip_maint_task_err = ESP_ERR_INVALID_ARG;
        snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
                 "Only password items can be edited.");
        break;
      }
      if (s_satochip_maint_text_c[0] == '\0') {
        s_satochip_maint_task_err = ESP_ERR_INVALID_ARG;
        snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
                 "Enter new password.");
        break;
      }

      const char *label =
          s_satochip_maint_text_b[0] ? s_satochip_maint_text_b : "Password";
      (void)seedkeeper_import_text_secret(
          s_satochip_maint_pin, SEEDKEEPER_TYPE_PASSWORD, 0x00, label,
          s_satochip_maint_text_c, false, "Edit Password", apdu,
          s_satochip_maint_result, sizeof(s_satochip_maint_result));
      if (s_satochip_maint_task_err != ESP_OK || apdu->sw != 0x9000) {
        break;
      }

      s_satochip_maint_task_err =
          smartcard_seedkeeper_reset_secret(s_satochip_maint_pin, sid, apdu,
                                            30000);
      if (s_satochip_maint_task_err != ESP_OK || apdu->sw != 0x9000) {
        satochip_format_apdu_result(
            s_satochip_maint_result, sizeof(s_satochip_maint_result),
            "Edit Password: new password saved, old item delete failed", apdu);
        break;
      }
      satochip_format_apdu_result(s_satochip_maint_result,
                                  sizeof(s_satochip_maint_result), "Edit Password",
                                  apdu);
      if (s_satochip_maint_task_err == ESP_OK && apdu->sw == 0x9000) {
        s_satochip_seedkeeper_lookup_stage = SATOCHIP_SEEDKEEPER_LOOKUP_LIST;
        s_satochip_seedkeeper_selected_sid = 0;
        s_satochip_seedkeeper_selected_header_valid = false;
        secure_memzero(&s_satochip_seedkeeper_selected_header,
                       sizeof(s_satochip_seedkeeper_selected_header));
        s_satochip_seedkeeper_last_list_valid = false;
      }
      break;
    }

    s_satochip_maint_task_err = smartcard_seedkeeper_export_secret(
        s_satochip_maint_pin, sid, 0, false, apdu, 30000);
    bool loaded_ok = false;
    size_t slot_index = 0;
    bool load_to_device =
        s_satochip_seedkeeper_item_op == SATOCHIP_SEEDKEEPER_ITEM_OP_LOAD ||
        s_satochip_maint_mode == SATOCHIP_MAINT_SEEDKEEPER_LOAD_MNEMONIC;
    if (s_satochip_maint_task_err == ESP_OK && load_to_device) {
      char mnemonic[SEEDKEEPER_MNEMONIC_MAX_BYTES + 1];
      char passphrase[SEEDKEEPER_MNEMONIC_MAX_BYTES + 1];
      if (seedkeeper_extract_bip39_mnemonic(apdu, mnemonic, sizeof(mnemonic),
                                            passphrase, sizeof(passphrase),
                                            NULL)) {
#ifdef SIMULATOR
        loaded_ok = true;
        slot_index = 0;
#else
        loaded_ok =
            seedkeeper_load_mnemonic_into_session(mnemonic, passphrase,
                                                  &slot_index);
#endif
        secure_memzero(mnemonic, sizeof(mnemonic));
        secure_memzero(passphrase, sizeof(passphrase));
      }
    }
    bool password_item = seedkeeper_header_is_password(selected_header);
    bool secret_item = seedkeeper_header_is_generic_secret(selected_header);
    if (password_item && !load_to_device) {
      satochip_format_seedkeeper_password_result(
          s_satochip_maint_result, sizeof(s_satochip_maint_result),
          s_satochip_seedkeeper_item_op == SATOCHIP_SEEDKEEPER_ITEM_OP_EXPORT
              ? "Export Password QR"
              : "View Password",
          apdu);
    } else if (secret_item && !load_to_device) {
      satochip_format_seedkeeper_secret_result(
          s_satochip_maint_result, sizeof(s_satochip_maint_result),
          s_satochip_seedkeeper_item_op == SATOCHIP_SEEDKEEPER_ITEM_OP_EXPORT
              ? "Export Secret QR"
              : "View Secret",
          apdu);
    } else {
      satochip_format_seedkeeper_mnemonic_result(
          s_satochip_maint_result, sizeof(s_satochip_maint_result),
          load_to_device ? "Import to Device" : "View Mnemonic", apdu,
          load_to_device, loaded_ok, slot_index);
    }

    if ((password_item || secret_item) && !load_to_device &&
        s_satochip_seedkeeper_item_op == SATOCHIP_SEEDKEEPER_ITEM_OP_EXPORT &&
        s_satochip_maint_task_err == ESP_OK && apdu->sw == 0x9000) {
      char secret[4096];
      smartcard_seedkeeper_header_t header;
      if (seedkeeper_extract_text_secret(apdu, secret, sizeof(secret), &header)) {
        seedkeeper_secret_qr_state_reset();
        snprintf(s_satochip_seedkeeper_export_secret_qr,
                 sizeof(s_satochip_seedkeeper_export_secret_qr), "%s", secret);
        snprintf(s_satochip_seedkeeper_export_secret_qr_title,
                 sizeof(s_satochip_seedkeeper_export_secret_qr_title), "%s",
                 header.label[0] ? header.label : "SeedKeeper Secret");
        s_satochip_seedkeeper_export_secret_qr_pending =
            (s_satochip_seedkeeper_export_secret_qr[0] != '\0');
        secure_memzero(secret, sizeof(secret));
        secure_memzero(&header, sizeof(header));
      }
    }
    break;
  }
  case SATOCHIP_MAINT_SEEDKEEPER_SAVE_SECRET: {
    const char *label = s_satochip_maint_text_b;
    const char *secret = s_satochip_maint_text_c;
    const char *title =
        label[0] ? label : "Save Secret";
    s_satochip_maint_task_err = ESP_ERR_INVALID_ARG;
    if (label[0]) {
      (void)seedkeeper_import_text_secret(
          s_satochip_maint_pin, SEEDKEEPER_TYPE_DATA,
          SEEDKEEPER_SUBTYPE_GENERIC_TEXT, label, secret, true, title, apdu,
          s_satochip_maint_result, sizeof(s_satochip_maint_result));
    } else {
      (void)seedkeeper_import_text_secret_allow_empty_label(
          s_satochip_maint_pin, SEEDKEEPER_TYPE_DATA,
          SEEDKEEPER_SUBTYPE_GENERIC_TEXT, secret, true, title, apdu,
          s_satochip_maint_result, sizeof(s_satochip_maint_result));
    }
    break;
  }
  case SATOCHIP_MAINT_SEEDKEEPER_LOAD_DESCRIPTOR: {
    uint16_t sid = 0;
    if (!satochip_parse_u16_text(s_satochip_maint_text_b, &sid)) {
      s_satochip_maint_task_err = ESP_ERR_INVALID_ARG;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "Enter descriptor SID.");
      break;
    }

    s_satochip_maint_task_err = smartcard_seedkeeper_export_secret(
        s_satochip_maint_pin, sid, 0, false, apdu, 30000);
    char descriptor[4096];
    smartcard_seedkeeper_header_t header;
    bool parsed = s_satochip_maint_task_err == ESP_OK &&
                  seedkeeper_extract_text_secret(apdu, descriptor,
                                                sizeof(descriptor), &header);
    bool loaded = false;
    if (parsed) {
#ifndef SIMULATOR
      loaded = wallet_load_descriptor(descriptor);
#else
      loaded = true;
#endif
    }
    size_t pos = 0;
    s_satochip_maint_result[0] = '\0';
    satochip_appendf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
                     &pos, "Load Descriptor\nSW: %04X\n", apdu->sw);
    if (apdu->detail[0])
      satochip_appendf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
                       &pos, "%s\n", apdu->detail);
    if (!parsed) {
      satochip_appendf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
                       &pos, "Could not parse descriptor text.");
      s_satochip_maint_task_err = ESP_ERR_INVALID_RESPONSE;
    } else {
      satochip_appendf(
          s_satochip_maint_result, sizeof(s_satochip_maint_result), &pos,
          "SID: %u\nLabel: %s\nLoad: %s\n\n%s",
          (unsigned)header.id, header.label[0] ? header.label : "(no label)",
          loaded ? "success" : "failed", descriptor);
      s_satochip_maint_task_err = loaded ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
    }
    secure_memzero(descriptor, sizeof(descriptor));
    break;
  }
  case SATOCHIP_MAINT_SEEDKEEPER_SAVE_DESCRIPTOR: {
    const char *label =
        s_satochip_maint_text_b[0] ? s_satochip_maint_text_b : "Wallet Descriptor";
    const char *descriptor = s_satochip_maint_text_c;
    char *owned_descriptor = NULL;
    if (!descriptor[0]) {
#ifndef SIMULATOR
      if (wallet_get_descriptor_string(&owned_descriptor))
        descriptor = owned_descriptor;
#endif
    }
    if (!descriptor || !descriptor[0]) {
      s_satochip_maint_task_err = ESP_ERR_INVALID_ARG;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "No descriptor to save.");
      break;
    }
    s_satochip_maint_task_err = ESP_ERR_INVALID_ARG;
    (void)seedkeeper_import_text_secret(
        s_satochip_maint_pin, SEEDKEEPER_TYPE_DATA, 0x00, label, descriptor,
        true, "Save Descriptor", apdu, s_satochip_maint_result,
        sizeof(s_satochip_maint_result));
    if (owned_descriptor) {
      secure_memzero(owned_descriptor, strlen(owned_descriptor));
      free(owned_descriptor);
    }
    break;
  }
  case SATOCHIP_MAINT_SEEDKEEPER_CLONE: {
    uint16_t sid = 0;
    uint16_t sid_pubkey = 0;
    if (!satochip_parse_u16_text(s_satochip_maint_text_b, &sid) ||
        !satochip_parse_u16_text(s_satochip_maint_text_c, &sid_pubkey)) {
      s_satochip_maint_task_err = ESP_ERR_INVALID_ARG;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "Enter SID and target public key SID.");
      break;
    }
    s_satochip_maint_task_err = smartcard_seedkeeper_export_secret_to_satochip(
        s_satochip_maint_pin, sid, sid_pubkey, apdu, 30000);
    satochip_format_apdu_result(s_satochip_maint_result,
                                sizeof(s_satochip_maint_result), "Clone",
                                apdu);
    break;
  }
  case SATOCHIP_MAINT_SEEDKEEPER_RESET: {
    s_satochip_maint_task_err = ESP_ERR_NOT_SUPPORTED;
    snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
             "For SeedKeeper reset, use the Wrong PIN / Wrong PUK buttons on this page.");
    break;
  }
  case SATOCHIP_MAINT_SEEDKEEPER_GENERATE_MASTERSEED: {
    uint32_t seed_size = 64;
    if (s_satochip_maint_text_b[0] &&
        !satochip_parse_u32_text(s_satochip_maint_text_b, &seed_size)) {
      s_satochip_maint_task_err = ESP_ERR_INVALID_ARG;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "Invalid length.");
      break;
    }
    if (seed_size != 16 && seed_size != 32 && seed_size != 64) {
      s_satochip_maint_task_err = ESP_ERR_INVALID_ARG;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "Length supports only 16/32/64.");
      break;
    }
    const char *label =
        s_satochip_maint_text_c[0] ? s_satochip_maint_text_c : "Masterseed";
    s_satochip_maint_task_err = smartcard_seedkeeper_generate_masterseed(
        s_satochip_maint_pin, (uint8_t)seed_size,
        SEEDKEEPER_EXPORT_PLAINTEXT_ALLOWED, label, apdu, 30000);
    satochip_format_apdu_result(s_satochip_maint_result,
                                sizeof(s_satochip_maint_result), "Masterseed",
                                apdu);
    break;
  }
  case SATOCHIP_MAINT_SEEDKEEPER_GENERATE_2FA_SECRET: {
    const char *label = s_satochip_maint_text_b[0] ? s_satochip_maint_text_b
                                                    : "2FA";
    s_satochip_maint_task_err = smartcard_seedkeeper_generate_2fa_secret(
        s_satochip_maint_pin, SEEDKEEPER_EXPORT_PLAINTEXT_ALLOWED, label, apdu,
        30000);
    satochip_format_apdu_result(s_satochip_maint_result,
                                sizeof(s_satochip_maint_result), "2FA",
                                apdu);
    break;
  }
  case SATOCHIP_MAINT_SEEDKEEPER_DERIVE_MASTER_PASSWORD: {
    uint16_t sid = 0;
    if (!satochip_parse_u16_text(s_satochip_maint_text_b, &sid)) {
      s_satochip_maint_task_err = ESP_ERR_INVALID_ARG;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "Enter SID.");
      break;
    }
    const char *salt =
        s_satochip_maint_text_c[0] ? s_satochip_maint_text_c : "KernSigner";
    s_satochip_maint_task_err = smartcard_seedkeeper_derive_master_password(
        s_satochip_maint_pin, salt, sid, 0, false, apdu, 30000);
    satochip_format_apdu_result(s_satochip_maint_result,
                                sizeof(s_satochip_maint_result), "Derive Password",
                                apdu);
    break;
  }
  case SATOCHIP_MAINT_SEEDKEEPER_RESET_SECRET: {
    uint16_t sid = 0;
    if (!satochip_parse_u16_text(s_satochip_maint_text_b, &sid)) {
      s_satochip_maint_task_err = ESP_ERR_INVALID_ARG;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "Enter SID.");
      break;
    }
    s_satochip_maint_task_err =
        smartcard_seedkeeper_reset_secret(s_satochip_maint_pin, sid, apdu,
                                          30000);
    satochip_format_apdu_result(s_satochip_maint_result,
                                sizeof(s_satochip_maint_result), "Delete Item",
                                apdu);
    break;
  }
  case SATOCHIP_MAINT_CERT_EXPORT:
    s_satochip_maint_task_err = smartcard_satochip_card_export_perso_certificate(
        cert, 20000);
    satochip_format_certificate_export(cert, s_satochip_maint_result,
                                       sizeof(s_satochip_maint_result));
    break;
  case SATOCHIP_MAINT_CERT_IMPORT:
    s_satochip_maint_task_err = smartcard_satochip_card_import_perso_certificate(
        s_satochip_maint_text_a, apdu, 20000);
    satochip_format_apdu_result(s_satochip_maint_result,
                                sizeof(s_satochip_maint_result), "Import Certificate",
                                apdu);
    break;
  case SATOCHIP_MAINT_AUTHENTICITY:
    s_satochip_maint_task_err = smartcard_satochip_card_verify_authenticity(
        auth, 20000);
    satochip_format_authenticity(auth, s_satochip_maint_result,
                                 sizeof(s_satochip_maint_result));
    break;
  case SATOCHIP_MAINT_NONE:
  default:
    s_satochip_maint_task_err = ESP_ERR_INVALID_STATE;
    snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
             "No task to run.");
    break;
  }

satochip_maint_done:
  satochip_maint_log_stack_watermark("done");
  if (apdu) {
    secure_memzero(apdu, sizeof(*apdu));
    free(apdu);
  }
  if (cert) {
    secure_memzero(cert, sizeof(*cert));
    free(cert);
  }
  if (auth) {
    secure_memzero(auth, sizeof(*auth));
    free(auth);
  }
  secure_memzero(s_satochip_maint_pin, sizeof(s_satochip_maint_pin));
  secure_memzero(s_satochip_maint_text_a, sizeof(s_satochip_maint_text_a));
  secure_memzero(s_satochip_maint_text_b, sizeof(s_satochip_maint_text_b));
  secure_memzero(s_satochip_maint_text_c, sizeof(s_satochip_maint_text_c));
  if (s_satochip_maint_mode == SATOCHIP_MAINT_SEEDKEEPER_SAVE_SECRET)
    secure_memzero(s_satochip_seedkeeper_scanned_secret,
                   sizeof(s_satochip_seedkeeper_scanned_secret));
  __atomic_store_n(&s_satochip_maint_task_done, true, __ATOMIC_RELEASE);
  if (delete_with_caps) {
    vTaskDeleteWithCaps(NULL);
    return;
  }
  vTaskDelete(NULL);
}

static void satochip_maint_finish_ui(void) {
  bool seedkeeper_reset_step_done =
      s_satochip_maint_mode == SATOCHIP_MAINT_SEEDKEEPER_RESET_PIN_STEP ||
      s_satochip_maint_mode == SATOCHIP_MAINT_SEEDKEEPER_RESET_PUK_STEP;
  if (s_satochip_maint_progress_dialog) {
    lv_obj_del(s_satochip_maint_progress_dialog);
    s_satochip_maint_progress_dialog = NULL;
  }
  s_satochip_maint_task_handle = NULL;
  s_satochip_maint_task_with_caps = false;
  if (seedkeeper_reset_step_done)
    s_satochip_maint_mode = SATOCHIP_MAINT_SEEDKEEPER_RESET;
  if (s_satochip_seedkeeper_export_secret_qr_pending) {
    seedkeeper_secret_qr_show_if_pending();
    return;
  }
  (void)signer_shell_show_screen(s_current_screen_id);
}

static void satochip_maint_poll_cb(lv_timer_t *timer) {
  (void)timer;
  if (!__atomic_load_n(&s_satochip_maint_task_done, __ATOMIC_ACQUIRE))
    return;

  if (s_satochip_maint_poll_timer) {
    lv_timer_del(s_satochip_maint_poll_timer);
    s_satochip_maint_poll_timer = NULL;
  }
  satochip_maint_finish_ui();
}

static void satochip_maint_start(void) {
  if (s_satochip_task_handle || s_satochip_tool_task_handle ||
      s_card_info_task_handle || s_satochip_maint_task_handle) {
    dialog_show_error("Smartcard busy", NULL, 1600);
    return;
  }

  bool seedkeeper_lookup_mode =
      s_satochip_maint_mode == SATOCHIP_MAINT_SEEDKEEPER_VIEW_MNEMONIC ||
      s_satochip_maint_mode == SATOCHIP_MAINT_SEEDKEEPER_LOAD_MNEMONIC;
  bool seedkeeper_reset_step_mode =
      s_satochip_maint_mode == SATOCHIP_MAINT_SEEDKEEPER_RESET_PIN_STEP ||
      s_satochip_maint_mode == SATOCHIP_MAINT_SEEDKEEPER_RESET_PUK_STEP;
  if (seedkeeper_reset_step_mode) {
    if (s_satochip_maint_pin[0] == '\0') {
      dialog_show_error("Enter the error value", NULL, 1600);
      return;
    }
  } else if (satochip_maint_mode_requires_pin(s_satochip_maint_mode)) {
    if (seedkeeper_lookup_mode &&
        s_satochip_seedkeeper_lookup_stage !=
            SATOCHIP_SEEDKEEPER_LOOKUP_ENTER_PIN) {
      snprintf(s_satochip_maint_pin, sizeof(s_satochip_maint_pin), "%s",
               s_satochip_seedkeeper_cached_pin);
      if (s_satochip_maint_pin[0] == '\0') {
        dialog_show_error("Read the list first", NULL, 1600);
        return;
      }
    } else if (!s_satochip_maint_input.textarea) {
      dialog_show_error("Page is not ready. Go back and reopen it.", NULL, 1600);
      return;
    } else {
      satochip_maint_copy_text(s_satochip_maint_pin,
                               sizeof(s_satochip_maint_pin),
                               s_satochip_maint_input.textarea);
    }
    if (s_satochip_maint_pin[0] == '\0') {
      dialog_show_error("Enter PIN", NULL, 1600);
      return;
    }
    if (seedkeeper_lookup_mode &&
        s_satochip_seedkeeper_lookup_stage ==
            SATOCHIP_SEEDKEEPER_LOOKUP_ENTER_PIN) {
      snprintf(s_satochip_seedkeeper_cached_pin,
               sizeof(s_satochip_seedkeeper_cached_pin), "%s",
               s_satochip_maint_pin);
    }
  } else {
    s_satochip_maint_pin[0] = '\0';
  }

  switch (s_satochip_maint_mode) {
  case SATOCHIP_MAINT_CHANGE_PIN:
  case SATOCHIP_MAINT_SEEDKEEPER_CHANGE_PIN:
    satochip_maint_copy_text(s_satochip_maint_text_a,
                             sizeof(s_satochip_maint_text_a),
                             s_satochip_maint_extra_a);
    satochip_maint_copy_text(s_satochip_maint_text_b,
                             sizeof(s_satochip_maint_text_b),
                             s_satochip_maint_extra_b);
    if (s_satochip_maint_text_a[0] == '\0' ||
        s_satochip_maint_text_b[0] == '\0') {
      dialog_show_error("Enter new PIN", NULL, 1600);
      return;
    }
    if (strcmp(s_satochip_maint_text_a, s_satochip_maint_text_b) != 0) {
      dialog_show_error("New PIN entries do not match", NULL, 1800);
      return;
    }
    snprintf(s_satochip_maint_text_c, sizeof(s_satochip_maint_text_c), "0");
    break;
  case SATOCHIP_MAINT_SETUP_PIN:
  case SATOCHIP_MAINT_SEEDKEEPER_SETUP_PIN:
    satochip_maint_copy_text(s_satochip_maint_text_a,
                             sizeof(s_satochip_maint_text_a),
                             s_satochip_maint_extra_a);
    if (s_satochip_maint_text_a[0] == '\0') {
      dialog_show_error("Enter PIN again", NULL, 1600);
      return;
    }
    if (strcmp(s_satochip_maint_pin, s_satochip_maint_text_a) != 0) {
      dialog_show_error("PIN entries do not match", NULL, 1800);
      return;
    }
    break;
  case SATOCHIP_MAINT_UNBLOCK_PIN:
    satochip_maint_copy_text(s_satochip_maint_text_a,
                             sizeof(s_satochip_maint_text_a),
                             s_satochip_maint_input.textarea);
    break;
  case SATOCHIP_MAINT_SET_LABEL:
  case SATOCHIP_MAINT_RESET_SEED:
  case SATOCHIP_MAINT_EXPORT_AUTHENTIKEY:
    satochip_maint_copy_text(s_satochip_maint_text_a,
                             sizeof(s_satochip_maint_text_a),
                             s_satochip_maint_extra_a);
    break;
  case SATOCHIP_MAINT_NFC_POLICY:
    satochip_maint_copy_text(s_satochip_maint_text_a,
                             sizeof(s_satochip_maint_text_a),
                             s_satochip_maint_extra_a);
    {
      uint8_t policy = 0;
      if (!satochip_parse_u8_text(s_satochip_maint_text_a, &policy) ||
          policy > 2) {
        dialog_show_error("Policy must be 0/1/2", NULL, 1800);
        return;
      }
    }
    break;
  case SATOCHIP_MAINT_FEATURE_POLICY:
    satochip_maint_copy_text(s_satochip_maint_text_a,
                             sizeof(s_satochip_maint_text_a),
                             s_satochip_maint_extra_a);
    satochip_maint_copy_text(s_satochip_maint_text_b,
                             sizeof(s_satochip_maint_text_b),
                             s_satochip_maint_extra_b);
    {
      uint8_t feature_id = 0;
      uint8_t policy = 0;
      if (!satochip_parse_u8_text(s_satochip_maint_text_a, &feature_id) ||
          !satochip_parse_u8_text(s_satochip_maint_text_b, &policy) ||
          feature_id > 2 || policy > 2) {
        dialog_show_error("Feature and policy must be 0/1/2", NULL, 1800);
        return;
      }
    }
    break;
  case SATOCHIP_MAINT_IMPORT_NDEF_AUTHENTIKEY:
  case SATOCHIP_MAINT_IMPORT_TRUSTED_PUBKEY:
    satochip_maint_copy_text(s_satochip_maint_text_a,
                             sizeof(s_satochip_maint_text_a),
                             s_satochip_maint_input.textarea);
    break;
  case SATOCHIP_MAINT_SET_2FA_KEY:
    satochip_maint_copy_text(s_satochip_maint_text_a,
                             sizeof(s_satochip_maint_text_a),
                             s_satochip_maint_input.textarea);
    satochip_maint_copy_text(s_satochip_maint_text_b,
                             sizeof(s_satochip_maint_text_b),
                             s_satochip_maint_extra_a);
    satochip_maint_copy_text(s_satochip_maint_text_c,
                             sizeof(s_satochip_maint_text_c),
                             s_satochip_maint_extra_b);
    break;
  case SATOCHIP_MAINT_RESET_2FA_KEY:
    satochip_maint_copy_text(s_satochip_maint_text_a,
                             sizeof(s_satochip_maint_text_a),
                             s_satochip_maint_input.textarea);
    break;
  case SATOCHIP_MAINT_CERT_IMPORT:
    satochip_maint_copy_text(s_satochip_maint_text_a,
                             sizeof(s_satochip_maint_text_a),
                             s_satochip_maint_input.textarea);
    break;
  case SATOCHIP_MAINT_SEEDKEEPER_CREATE_MNEMONIC:
    satochip_maint_copy_text(s_satochip_maint_text_a,
                             sizeof(s_satochip_maint_text_a),
                             s_satochip_maint_extra_a);
    satochip_maint_copy_text(s_satochip_maint_text_b,
                             sizeof(s_satochip_maint_text_b),
                             s_satochip_maint_extra_b);
    break;
  case SATOCHIP_MAINT_SEEDKEEPER_WRITE_MNEMONIC:
    satochip_maint_copy_text(s_satochip_maint_text_a,
                             sizeof(s_satochip_maint_text_a),
                             s_satochip_maint_extra_a);
    break;
  case SATOCHIP_MAINT_SEEDKEEPER_VIEW_MNEMONIC:
  case SATOCHIP_MAINT_SEEDKEEPER_LOAD_MNEMONIC:
    if (satochip_seedkeeper_lookup_has_selected_item()) {
      snprintf(s_satochip_maint_text_a, sizeof(s_satochip_maint_text_a), "%u",
               (unsigned)s_satochip_seedkeeper_selected_header.id);
    } else {
      s_satochip_maint_text_a[0] = '\0';
    }
    if (s_satochip_seedkeeper_lookup_stage ==
        SATOCHIP_SEEDKEEPER_LOOKUP_EDIT) {
      satochip_maint_copy_text(s_satochip_maint_text_b,
                               sizeof(s_satochip_maint_text_b),
                               s_satochip_maint_extra_a);
      satochip_maint_copy_text(s_satochip_maint_text_c,
                               sizeof(s_satochip_maint_text_c),
                               s_satochip_maint_input.textarea);
      if (s_satochip_maint_text_c[0] == '\0') {
        dialog_show_error("Enter new password", NULL, 1600);
        return;
      }
    }
    break;
  case SATOCHIP_MAINT_SEEDKEEPER_STATUS:
  case SATOCHIP_MAINT_SEEDKEEPER_FREE_SPACE:
  case SATOCHIP_MAINT_SEEDKEEPER_LIST:
  case SATOCHIP_MAINT_SEEDKEEPER_LOGS:
    satochip_maint_copy_text(s_satochip_maint_pin,
                             sizeof(s_satochip_maint_pin),
                             s_satochip_maint_input.textarea);
    break;
  case SATOCHIP_MAINT_SEEDKEEPER_LOAD_DESCRIPTOR:
  case SATOCHIP_MAINT_SEEDKEEPER_SAVE_DESCRIPTOR:
  case SATOCHIP_MAINT_SEEDKEEPER_SAVE_SECRET:
  case SATOCHIP_MAINT_SEEDKEEPER_CLONE:
  case SATOCHIP_MAINT_SEEDKEEPER_GENERATE_MASTERSEED:
  case SATOCHIP_MAINT_SEEDKEEPER_GENERATE_2FA_SECRET:
  case SATOCHIP_MAINT_SEEDKEEPER_DERIVE_MASTER_PASSWORD:
  case SATOCHIP_MAINT_SEEDKEEPER_RESET_SECRET:
    satochip_maint_copy_text(s_satochip_maint_text_a,
                             sizeof(s_satochip_maint_text_a),
                             s_satochip_maint_input.textarea);
    satochip_maint_copy_text(s_satochip_maint_text_b,
                             sizeof(s_satochip_maint_text_b),
                             s_satochip_maint_extra_a);
    satochip_maint_copy_text(s_satochip_maint_text_c,
                             sizeof(s_satochip_maint_text_c),
                             s_satochip_maint_extra_b);
    break;
  case SATOCHIP_MAINT_CERT_EXPORT:
  case SATOCHIP_MAINT_AUTHENTICITY:
  case SATOCHIP_MAINT_SEEDKEEPER_RESET:
  case SATOCHIP_MAINT_SEEDKEEPER_RESET_PIN_STEP:
  case SATOCHIP_MAINT_SEEDKEEPER_RESET_PUK_STEP:
  case SATOCHIP_MAINT_NONE:
  default:
    break;
  }

  satochip_maint_hide_inputs();
  satochip_maint_result_clear();
  __atomic_store_n(&s_satochip_maint_task_done, false, __ATOMIC_RELEASE);
  s_satochip_maint_task_err = ESP_ERR_INVALID_STATE;
  s_satochip_maint_task_with_caps = false;

  s_satochip_maint_progress_dialog =
      dialog_show_progress("Processing Smartcard", "Processing",
                           DIALOG_STYLE_OVERLAY);
  lv_refr_now(NULL);

  BaseType_t ok = xTaskCreatePinnedToCore(
      satochip_maint_task, "satochip_maint", SATOCHIP_MAINT_TASK_STACK_SIZE,
      NULL, 4, &s_satochip_maint_task_handle, 1);
  if (ok != pdPASS) {
    s_satochip_maint_task_with_caps = true;
    ok = xTaskCreatePinnedToCoreWithCaps(
        satochip_maint_task, "satochip_maint", SATOCHIP_MAINT_TASK_STACK_SIZE,
        NULL, 4, &s_satochip_maint_task_handle, 1,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
  if (ok != pdPASS) {
    s_satochip_maint_task_handle = NULL;
    s_satochip_maint_task_with_caps = false;
    if (s_satochip_maint_progress_dialog) {
      lv_obj_del(s_satochip_maint_progress_dialog);
      s_satochip_maint_progress_dialog = NULL;
    }
    dialog_show_error("Smartcard task failed to start", NULL, 0);
    return;
  }

  satochip_maint_input_cleanup();
  s_satochip_maint_poll_timer = lv_timer_create(satochip_maint_poll_cb, 100, NULL);
}

static void satochip_maint_start_event_cb(lv_event_t *event) {
  (void)event;
  satochip_maint_start();
}

static void satochip_seedkeeper_reset_start_step(
    satochip_maint_mode_t step_mode, lv_obj_t *textarea) {
  if (textarea) {
    satochip_maint_copy_text(s_satochip_maint_pin,
                             sizeof(s_satochip_maint_pin), textarea);
  } else {
    s_satochip_maint_pin[0] = '\0';
  }
  if (s_satochip_maint_pin[0] == '\0') {
    dialog_show_error("Enter the error value", NULL, 1600);
    return;
  }
  if (strlen(s_satochip_maint_pin) < 4) {
    dialog_show_error("At least 4 digits", NULL, 1600);
    return;
  }
  s_satochip_maint_mode = step_mode;
  satochip_maint_start();
}

static void satochip_seedkeeper_reset_pin_step_event_cb(lv_event_t *event) {
  lv_obj_t *textarea = event ? (lv_obj_t *)lv_event_get_user_data(event) : NULL;
  satochip_seedkeeper_reset_start_step(
      SATOCHIP_MAINT_SEEDKEEPER_RESET_PIN_STEP, textarea);
}

static void satochip_seedkeeper_reset_puk_step_event_cb(lv_event_t *event) {
  lv_obj_t *textarea = event ? (lv_obj_t *)lv_event_get_user_data(event) : NULL;
  satochip_seedkeeper_reset_start_step(
      SATOCHIP_MAINT_SEEDKEEPER_RESET_PUK_STEP, textarea);
}

static lv_obj_t *satochip_maint_create_result_panel(lv_obj_t *parent,
                                                    const char *title) {
  lv_color_t border = highlight_color();
  if (s_satochip_maint_result[0]) {
    border = s_satochip_maint_task_err == ESP_OK ? yes_color()
                                                 : error_color();
  }
  lv_obj_t *panel =
      create_panel(parent, border, max_i(14, theme_get_small_padding()));
  lv_obj_set_height(panel, shell_is_wave_43_portrait() ? 252 : 228);
  lv_obj_add_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(panel, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_style_pad_right(panel, theme_get_small_padding() + 4, 0);
  (void)title;
  create_text(panel, "Result", false, highlight_color());
  if (s_satochip_maint_result[0])
    create_text(panel, s_satochip_maint_result, false, main_color());
  else
    create_text(panel, "Waiting", false, secondary_color());
  if (s_satochip_maint_mode == SATOCHIP_MAINT_SEEDKEEPER_RESET)
    return panel;
  create_action_button(panel,
                       s_satochip_maint_result[0] ? "Run Again" : "Run",
                       satochip_maint_start_event_cb, NULL, true);
  return panel;
}

static void create_satochip_change_pin_block(lv_obj_t *parent,
                                             const signer_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_CHANGE_PIN);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? shell_feature_title(feature) : "Change PIN",
              true, main_color());
  satochip_maint_attach_primary_input(panel, "Old PIN", true, false, 64, "");
  create_text(panel, "New PIN", false, highlight_color());
  satochip_maint_attach_extra_field(panel, NULL, "New PIN", "", true, false, 64,
                                    &s_satochip_maint_extra_a);
  create_text(panel, "Confirm New PIN", false, highlight_color());
  satochip_maint_attach_extra_field(panel, NULL, "Enter again", "", true, false, 64,
                                    &s_satochip_maint_extra_b);
  satochip_maint_create_result_panel(parent, "Result");
}

static void create_satochip_setup_pin_block(lv_obj_t *parent,
                                            const signer_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_SETUP_PIN);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? shell_feature_title(feature) : "Setup PIN",
              true, main_color());
  satochip_maint_attach_primary_input(panel, "New PIN", true, false, 16, "");
  create_text(panel, "Confirm PIN", false, highlight_color());
  satochip_maint_attach_extra_field(panel, NULL, "Enter again", "", true, false,
                                    16, &s_satochip_maint_extra_a);
  satochip_maint_create_result_panel(parent, "Result");
}

static void create_satochip_seedkeeper_change_pin_block(
    lv_obj_t *parent, const signer_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_SEEDKEEPER_CHANGE_PIN);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? shell_feature_title(feature) : "Change PIN",
              true, main_color());
  satochip_maint_attach_primary_input(panel, "Old PIN", true, false, 64, "");
  create_text(panel, "New PIN", false, highlight_color());
  satochip_maint_attach_extra_field(panel, NULL, "New PIN", "", true, false, 64,
                                    &s_satochip_maint_extra_a);
  create_text(panel, "Confirm New PIN", false, highlight_color());
  satochip_maint_attach_extra_field(panel, NULL, "Enter again", "", true, false, 64,
                                    &s_satochip_maint_extra_b);
  satochip_maint_create_result_panel(parent, "Result");
}

static void create_satochip_seedkeeper_setup_pin_block(
    lv_obj_t *parent, const signer_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_SEEDKEEPER_SETUP_PIN);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? shell_feature_title(feature) : "Setup PIN",
              true, main_color());
  satochip_maint_attach_primary_input(panel, "New PIN", true, false, 16, "");
  create_text(panel, "Confirm PIN", false, highlight_color());
  satochip_maint_attach_extra_field(panel, NULL, "Enter again", "", true, false, 16,
                                    &s_satochip_maint_extra_a);
  satochip_maint_create_result_panel(parent, "Result");
}

static void create_satochip_seedkeeper_reset_block(
    lv_obj_t *parent, const signer_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_SEEDKEEPER_RESET);
  lv_obj_t *panel =
      create_panel(parent, error_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? shell_feature_title(feature) : "清空SeedKeeper", true,
              error_color());
  create_text(panel, "Newer SeedKeeper cards must lock PIN first, then PUK.", false,
              main_color());
  create_text(panel, "Wrong PIN", false, highlight_color());
  satochip_maint_attach_primary_input(panel, "Intentionally wrong PIN", true, false, 16,
                                      "0000");
  lv_obj_t *wrong_pin = s_satochip_maint_input.textarea;
  create_action_button(panel, "Wrong PIN Step",
                       satochip_seedkeeper_reset_pin_step_event_cb, wrong_pin,
                       true);
  lv_obj_t *wrong_puk = satochip_maint_attach_extra_field(
      panel, "Wrong PUK", "Run after PIN is locked", "0000", true, false, 16, NULL);
  create_action_button(panel, "Wrong PUK Step",
                       satochip_seedkeeper_reset_puk_step_event_cb, wrong_puk,
                       true);
  create_text(panel, "FF00 means the card is blank.", false, secondary_color());
  satochip_maint_create_result_panel(parent, "Result");
}

static void create_satochip_unblock_pin_block(lv_obj_t *parent,
                                              const signer_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_UNBLOCK_PIN);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? shell_feature_title(feature) : "Unlock", true,
              main_color());
  satochip_maint_attach_primary_input(panel, "PUK", true, false, 64, "");
  satochip_maint_create_result_panel(parent, "Result");
}

static void create_satochip_set_label_block(lv_obj_t *parent,
                                            const signer_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_SET_LABEL);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? shell_feature_title(feature) : "Change Label",
              true, main_color());
  satochip_maint_attach_primary_input(panel, "PIN", true, false, 64, "");
  satochip_maint_attach_extra_field(panel, "Label", "Enter label", "", false,
                                    false, 64, &s_satochip_maint_extra_a);
  satochip_maint_create_result_panel(parent, "Result");
}

static void create_satochip_nfc_policy_block(lv_obj_t *parent,
                                             const signer_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_NFC_POLICY);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? shell_feature_title(feature) : "NFC策略", true,
              main_color());
  satochip_maint_attach_primary_input(panel, "PIN", true, false, 64, "");
  satochip_maint_attach_extra_field(panel, "Policy", "0 enable 1 disable 2 block forever",
                                    "0", false, false, 1,
                                    &s_satochip_maint_extra_a);
  satochip_maint_create_result_panel(parent, "Result");
}

static void create_satochip_feature_policy_block(lv_obj_t *parent,
                                                 const signer_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_FEATURE_POLICY);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? shell_feature_title(feature) : "功能策略", true,
              main_color());
  satochip_maint_attach_primary_input(panel, "PIN", true, false, 64, "");
  satochip_maint_attach_extra_field(panel, "Feature ID", "0 sign 1 Nostr 2 Liquid", "0", false,
                                    false, 16, &s_satochip_maint_extra_a);
  satochip_maint_attach_extra_field(panel, "Policy", "0 enable 1 disable 2 block forever",
                                    "0", false, false, 1,
                                    &s_satochip_maint_extra_b);
  satochip_maint_create_result_panel(parent, "Result");
}

static void create_satochip_export_authentikey_block(
    lv_obj_t *parent, const signer_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_EXPORT_AUTHENTIKEY);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? shell_feature_title(feature) : "Export Authentikey",
              true, main_color());
  satochip_maint_create_result_panel(parent, "Result");
}

static void create_satochip_import_ndef_authentikey_block(
    lv_obj_t *parent, const signer_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_IMPORT_NDEF_AUTHENTIKEY);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? shell_feature_title(feature) : "Write NDEF",
              true, main_color());
  satochip_maint_attach_primary_input(panel, "32-byte private key", false, false, 128,
                                      "");
  satochip_maint_create_result_panel(parent, "Result");
}

static void create_satochip_import_trusted_pubkey_block(
    lv_obj_t *parent, const signer_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_IMPORT_TRUSTED_PUBKEY);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? shell_feature_title(feature) : "Write Trusted Key",
              true, main_color());
  satochip_maint_attach_primary_input(panel, "65-byte public key", false, false, 256,
                                      "");
  satochip_maint_create_result_panel(parent, "Result");
}

static void create_satochip_export_trusted_pubkey_block(
    lv_obj_t *parent, const signer_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_EXPORT_TRUSTED_PUBKEY);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? shell_feature_title(feature) : "Export Trusted Key",
              true, main_color());
  satochip_maint_create_result_panel(parent, "Result");
}

static void create_satochip_set_2fa_key_block(lv_obj_t *parent,
                                               const signer_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_SET_2FA_KEY);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? shell_feature_title(feature) : "Set 2FA", true,
              main_color());
  satochip_maint_attach_primary_input(panel, "20-byte key", false, false, 128,
                                      "");
  satochip_maint_attach_extra_field(panel, "Limit", "Decimal", "0", false, false,
                                    32, &s_satochip_maint_extra_a);
  satochip_maint_create_result_panel(parent, "Result");
}

static void create_satochip_reset_2fa_key_block(
    lv_obj_t *parent, const signer_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_RESET_2FA_KEY);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? shell_feature_title(feature) : "Clear 2FA", true,
              main_color());
  satochip_maint_attach_primary_input(panel, "20-byte challenge", false, false, 128,
                                      "");
  satochip_maint_create_result_panel(parent, "Result");
}

static void create_satochip_logout_all_block(lv_obj_t *parent,
                                             const signer_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_LOGOUT_ALL);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? shell_feature_title(feature) : "Logout", true,
              main_color());
  satochip_maint_create_result_panel(parent, "Result");
}

static void create_satochip_seedkeeper_status_block(
    lv_obj_t *parent, const signer_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_SEEDKEEPER_STATUS);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? shell_feature_title(feature) : "Status", true,
              main_color());
  satochip_maint_attach_primary_input(panel, "Card PIN (optional)", true, false, 64,
                                      "");
  satochip_maint_create_result_panel(parent, "Status Result");
}

static void create_satochip_seedkeeper_free_space_block(
    lv_obj_t *parent, const signer_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_SEEDKEEPER_FREE_SPACE);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? shell_feature_title(feature) : "Free Space",
              true, main_color());
  satochip_maint_attach_primary_input(panel, "Card PIN (optional)", true, false, 64,
                                      "");
  satochip_maint_create_result_panel(parent, "Space Result");
}

static void create_satochip_seedkeeper_list_block(
    lv_obj_t *parent, const signer_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_SEEDKEEPER_LIST);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? shell_feature_title(feature) : "List", true,
              main_color());
  satochip_maint_attach_primary_input(panel, "Card PIN (optional)", true, false, 64,
                                      "");
  satochip_maint_create_result_panel(parent, "List Result");
}

static void create_satochip_seedkeeper_logs_block(
    lv_obj_t *parent, const signer_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_SEEDKEEPER_LOGS);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? shell_feature_title(feature) : "Logs", true,
              main_color());
  satochip_maint_create_result_panel(parent, "Log Result");
}

static void create_satochip_seedkeeper_stub_block(
    lv_obj_t *parent, const signer_feature_t *feature, satochip_maint_mode_t mode,
    const char *primary_placeholder, bool primary_password,
    const char *extra_a_label, const char *extra_a_placeholder,
    const char *extra_b_label, const char *extra_b_placeholder) {
  satochip_maint_prepare(mode);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel,
              feature ? shell_feature_title(feature)
                      : satochip_seedkeeper_stub_title(mode),
              true, main_color());
  satochip_maint_attach_primary_input(
      panel, primary_placeholder ? primary_placeholder : "Input", primary_password,
      false, 256, "");
  if (extra_a_label || extra_a_placeholder)
    satochip_maint_attach_extra_field(
        panel, extra_a_label, extra_a_placeholder ? extra_a_placeholder : "",
        "", false, false, 256, &s_satochip_maint_extra_a);
  if (extra_b_label || extra_b_placeholder)
    satochip_maint_attach_extra_field(
        panel, extra_b_label, extra_b_placeholder ? extra_b_placeholder : "",
        "", false, false, 256, &s_satochip_maint_extra_b);
  satochip_maint_create_result_panel(parent, "Result");
}

static void create_satochip_reset_seed_block(lv_obj_t *parent,
                                             const signer_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_RESET_SEED);
  lv_obj_t *panel =
      create_panel(parent, error_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? shell_feature_title(feature) : "Reset Seed",
              true, error_color());
  create_text(panel, "After reset, set PIN again before writing a mnemonic.", false, main_color());
  satochip_maint_attach_primary_input(panel, "PIN", true, false, 64, "");
  satochip_maint_attach_extra_field(panel, "HMAC", "Hex, optional", "",
                                    false, false, 128, &s_satochip_maint_extra_a);
  satochip_maint_create_result_panel(parent, "Result");
}

static void create_satochip_reset_factory_block(lv_obj_t *parent,
                                                const signer_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_RESET_FACTORY);
  lv_obj_t *panel =
      create_panel(parent, error_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? shell_feature_title(feature) : "Factory", true,
              error_color());
  satochip_maint_attach_primary_input(panel, "Card PIN", true, false, 16, "");
  create_text(panel, "Unplug and reinsert the card, then run.", false, main_color());
  create_text(panel, "Repeat after the remaining attempts prompt.", false, secondary_color());
  satochip_maint_create_result_panel(parent, "Result");
}

static void create_satochip_seedkeeper_create_mnemonic_block(
    lv_obj_t *parent, const signer_feature_t *feature) {
  s_satochip_seedkeeper_ephemeral_create =
      feature && feature->id &&
      strcmp(feature->id, "new_seedkeeper_create_mnemonic") == 0;
  satochip_maint_prepare(SATOCHIP_MAINT_SEEDKEEPER_CREATE_MNEMONIC);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? shell_feature_title(feature) : "Create on Card", true,
              main_color());
  satochip_maint_attach_primary_input(panel, "Card PIN", true, false, 64, "");
  satochip_maint_attach_extra_field(panel, "Word Count", "12/15/18/21/24", "12",
                                    false, false, 8,
                                    &s_satochip_maint_extra_a);
  satochip_maint_create_result_panel(parent, "Create Result");
}

static void create_satochip_seedkeeper_write_mnemonic_block(
    lv_obj_t *parent, const signer_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_SEEDKEEPER_WRITE_MNEMONIC);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? shell_feature_title(feature) : "Write to Card",
              true, main_color());
  satochip_maint_attach_primary_input(panel, "Card PIN", true, false, 64, "");
  satochip_maint_attach_extra_field(panel, "Passphrase", "Optional", "", true,
                                    false, 128, &s_satochip_maint_extra_a);
  satochip_maint_create_result_panel(parent, "Result");
}

static void create_satochip_write_mnemonic_block(lv_obj_t *parent,
                                                 const signer_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_SATOCHIP_WRITE_MNEMONIC);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? shell_feature_title(feature) : "Write to Satochip", true,
              main_color());
  create_text(panel, "Write the current mnemonic. Reset the card first if it already has a seed.",
              false, secondary_color());
  satochip_maint_attach_primary_input(panel, "Card PIN", true, false, 64, "");
  satochip_maint_create_result_panel(parent, "Result");
}

static void create_satochip_seedkeeper_view_mnemonic_block(
    lv_obj_t *parent, const signer_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_SEEDKEEPER_VIEW_MNEMONIC);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? shell_feature_title(feature) : "View Card", true,
              main_color());
  satochip_seedkeeper_render_lookup_content(panel);
}

static void create_satochip_seedkeeper_load_mnemonic_block(
    lv_obj_t *parent, const signer_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_SEEDKEEPER_LOAD_MNEMONIC);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? shell_feature_title(feature) : "Import from Card",
              true, main_color());
  satochip_seedkeeper_render_lookup_content(panel);
}

static void create_satochip_seedkeeper_save_secret_block(
    lv_obj_t *parent, const signer_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_SEEDKEEPER_SAVE_SECRET);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? shell_feature_title(feature) : "Save Secret", true,
              main_color());
  satochip_maint_attach_primary_input(panel, "Card PIN", true, false, 64, "");
  satochip_maint_attach_extra_field(panel, "Label", "Optional", "", false,
                                    false, 80, &s_satochip_maint_extra_a);
  satochip_maint_attach_extra_field(
      panel, "Secret", "Text to save",
      s_satochip_seedkeeper_scanned_secret[0]
          ? s_satochip_seedkeeper_scanned_secret
          : "",
      false, true, 4096, &s_satochip_maint_extra_b);
  create_action_button(panel, "Scan QR", seedkeeper_secret_scan_event_cb, NULL,
                       false);
  satochip_maint_create_result_panel(parent, "Result");
}

static void create_satochip_seedkeeper_save_descriptor_block(
    lv_obj_t *parent, const signer_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_SEEDKEEPER_SAVE_DESCRIPTOR);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? shell_feature_title(feature) : "Save Descriptor", true,
              main_color());
  satochip_maint_attach_primary_input(panel, "Card PIN", true, false, 64, "");
  satochip_maint_attach_extra_field(panel, "Label", "Wallet descriptor", "", false,
                                    false, 80, &s_satochip_maint_extra_a);
  satochip_maint_attach_extra_field(panel, "Descriptor", "Leave empty to save current wallet", "",
                                    false, true, 4096,
                                    &s_satochip_maint_extra_b);
  satochip_maint_create_result_panel(parent, "Result");
}

static void create_satochip_certificate_export_block(
    lv_obj_t *parent, const signer_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_CERT_EXPORT);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? shell_feature_title(feature) : "Export Certificate", true,
              main_color());
  satochip_maint_create_result_panel(parent, "Export Result");
}

static void create_satochip_certificate_import_block(
    lv_obj_t *parent, const signer_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_CERT_IMPORT);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? shell_feature_title(feature) : "Import Certificate", true,
              main_color());
  satochip_maint_attach_primary_input(panel, "Certificate PEM", false, true, 4096,
                                      "");
  if (s_satochip_maint_input.textarea)
    lv_obj_set_height(s_satochip_maint_input.textarea, 160);
  satochip_maint_create_result_panel(parent, "Import Result");
}

static void create_satochip_authenticity_block(lv_obj_t *parent,
                                               const signer_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_AUTHENTICITY);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? shell_feature_title(feature) : "Authenticity Check", true,
              main_color());
  satochip_maint_create_result_panel(parent, "Check Result");
}

typedef struct {
  const char *panel_title;
  const char *body;
  const char *button_label;
  const char *preview_title;
  const char *notice;
  bool reveal_qr_payload;
} camera_preview_action_t;

static const camera_preview_action_t CAMERA_ACTION_TEST = {
    "Camera Test",
    "",
    "Open Camera Test",
    "Device Check: Camera",
    "",
    true};

static const camera_preview_action_t CAMERA_ACTION_SEED_QR = {
    "Mnemonic Scan",
    "",
    "Open Scanner",
    "Mnemonic Scan",
    "",
    false};

static const camera_preview_action_t CAMERA_ACTION_SIGN_QR = {
    "Transaction Scan",
    "",
    "Open Scanner",
    "Transaction Scan",
    "",
    false};

static const camera_preview_action_t CAMERA_ACTION_TOOLS_QR = {
    "QR Decode",
    "",
    "Open Scanner",
    "QR Decode",
    "",
    true};

static const camera_preview_action_t CAMERA_ACTION_ENTROPY = {
    "Camera Entropy",
    "",
    "Open Camera",
    "Camera Entropy",
    "",
    false};

static const camera_preview_action_t CAMERA_ACTION_ADDRESS = {
    "Address Scan",
    "",
    "Open Scanner",
    "Address Scan",
    "",
    true};

static void camera_preview_event_cb(lv_event_t *event) {
  const camera_preview_action_t *action =
      (const camera_preview_action_t *)lv_event_get_user_data(event);
  if (!action)
    action = &CAMERA_ACTION_TEST;

  (void)signer_camera_preview_open_ex(action->preview_title, action->notice,
                                    action->reveal_qr_payload);
}

static void touch_probe_event_cb(lv_event_t *event) {
  lv_obj_t *label = (lv_obj_t *)lv_event_get_user_data(event);
  if (!label)
    return;

  lv_indev_t *indev = lv_indev_active();
  lv_point_t point = {0};
  if (indev)
    lv_indev_get_point(indev, &point);

  s_touch_count++;
  char text[160];
  snprintf(text, sizeof(text),
           i18n_tr_or("shell.touch_ok_point_format",
                      "Touch OK: #%lu\nPoint: X=%ld, Y=%ld"),
           (unsigned long)s_touch_count, (long)point.x, (long)point.y);
  lv_label_set_text(label, text);
}

static void create_touch_probe_block(lv_obj_t *parent) {
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(12, theme_get_small_padding()));
  create_text(panel, "Touch Test", false, highlight_color());

  lv_obj_t *result = create_text(panel, "Waiting", true, main_color());

  lv_obj_t *pad = lv_obj_create(panel);
  lv_obj_set_width(pad, LV_PCT(100));
  lv_obj_set_height(pad, 220);
  lv_obj_set_style_bg_color(pad, bg_color(), 0);
  lv_obj_set_style_bg_opa(pad, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(pad, highlight_color(), 0);
  lv_obj_set_style_border_width(pad, 2, 0);
  lv_obj_set_style_radius(pad, 8, 0);
  lv_obj_add_flag(pad, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(pad, touch_probe_event_cb, LV_EVENT_PRESSED, result);
  lv_obj_add_event_cb(pad, touch_probe_event_cb, LV_EVENT_PRESSING, result);

  lv_obj_t *pad_label = lv_label_create(pad);
  lv_label_set_text(pad_label, i18n_tr_or("tools.touch_test", "Touch Test Area"));
  lv_obj_set_style_text_font(pad_label, theme_font_medium(), 0);
  lv_obj_set_style_text_color(pad_label, secondary_color(), 0);
  lv_obj_center(pad_label);
}

static void storage_probe_event_cb(lv_event_t *event) {
  lv_obj_t *label = (lv_obj_t *)lv_event_get_user_data(event);
  if (!label)
    return;

  lv_label_set_text(label, i18n_tr_or("dialog.processing", "Checking"));
  lv_refr_now(NULL);

  char detail[256];
  esp_err_t ret = signer_hardware_probe_storage_rw(detail, sizeof(detail));
  char text[320];
  snprintf(text, sizeof(text), "%s\nResult: %s",
           ret == ESP_OK ? "Storage read/write check passed" : "Storage read/write check failed",
           detail);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, ret == ESP_OK ? yes_color() : error_color(),
                              0);
}

static void create_storage_probe_block(lv_obj_t *parent) {
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(12, theme_get_small_padding()));
  create_text(panel, "Storage Test", false, highlight_color());

  lv_obj_t *result = create_text(panel, "Waiting", true, main_color());
  create_action_button(panel, "Start Storage Test", storage_probe_event_cb, result,
                       true);
}

static void create_camera_probe_block(lv_obj_t *parent) {
  lv_obj_t *panel =
      create_panel(parent, cyan_color(), max_i(12, theme_get_small_padding()));
  create_text(panel, CAMERA_ACTION_TEST.panel_title, false, highlight_color());
  create_action_button(panel, CAMERA_ACTION_TEST.button_label,
                       camera_preview_event_cb, (void *)&CAMERA_ACTION_TEST,
                       true);
}

static void create_camera_action_block(lv_obj_t *parent,
                                       const camera_preview_action_t *action) {
  lv_obj_t *panel =
      create_panel(parent, cyan_color(), max_i(12, theme_get_small_padding()));
  create_text(panel, action->panel_title, false, highlight_color());
  create_action_button(panel, action->button_label, camera_preview_event_cb,
                       (void *)action, true);
}

static void update_qr_text_preview(void) {
  if (!s_qr_preview || !s_qr_status_label)
    return;

  const char *text = s_qr_textarea ? lv_textarea_get_text(s_qr_textarea) : NULL;
  if (!text || text[0] == '\0')
    text = i18n_tr_or("tools.offline_wallet_qr", "Offline wallet QR");

  lv_result_t res = lv_qrcode_update(s_qr_preview, text, (uint32_t)strlen(text));
  if (res == LV_RESULT_OK) {
    char status[160];
    snprintf(status, sizeof(status),
             i18n_tr_or("tools.qr_generated_format", "QR generated: %u bytes"),
             (unsigned)strlen(text));
    lv_label_set_text(s_qr_status_label, status);
    lv_obj_set_style_text_color(s_qr_status_label, yes_color(), 0);
  } else {
    lv_label_set_text(s_qr_status_label,
                      i18n_tr_or("tools.qr_generation_failed_too_long",
                                 "QR generation failed. The content may be too long."));
    lv_obj_set_style_text_color(s_qr_status_label, error_color(), 0);
  }
}

static void qr_text_generate_event_cb(lv_event_t *event) {
  (void)event;
  update_qr_text_preview();
}

static void qr_keyboard_event_cb(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_READY)
    update_qr_text_preview();
}

static void create_qr_text_tool_block(lv_obj_t *parent) {
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(12, theme_get_small_padding()));
  lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  create_text(panel, "Text to QR", false, highlight_color());

  int qr_size = theme_get_screen_width() - max_i(128, theme_get_default_padding() * 4);
  if (qr_size > 250)
    qr_size = 250;
  if (qr_size < 150)
    qr_size = 150;

  s_qr_preview = lv_qrcode_create(panel);
  lv_qrcode_set_size(s_qr_preview, qr_size);
  lv_qrcode_set_dark_color(s_qr_preview, bg_color());
  lv_qrcode_set_light_color(s_qr_preview, main_color());
  lv_obj_set_style_border_color(s_qr_preview, main_color(), 0);
  lv_obj_set_style_border_width(s_qr_preview, 10, 0);
  lv_obj_set_style_radius(s_qr_preview, 0, 0);

  s_qr_status_label =
      create_text(panel, "Waiting", false, secondary_color());

  s_qr_textarea = lv_textarea_create(panel);
  lv_obj_set_width(s_qr_textarea, LV_PCT(100));
  lv_obj_set_height(s_qr_textarea, 74);
  lv_textarea_set_one_line(s_qr_textarea, false);
  lv_textarea_set_max_length(s_qr_textarea, 240);
  lv_textarea_set_placeholder_text(
      s_qr_textarea,
      i18n_tr_or("tools.enter_text_qr_placeholder", "Enter text to show as QR"));
  theme_apply_textarea(s_qr_textarea, false);
  lv_textarea_set_text(s_qr_textarea,
                       i18n_tr_or("tools.offline_wallet_qr", "Offline wallet QR"));
  lv_obj_set_style_text_font(s_qr_textarea, theme_font_small(), 0);
  lv_obj_set_style_text_color(s_qr_textarea, main_color(), 0);
  lv_obj_set_style_bg_color(s_qr_textarea, bg_color(), 0);
  lv_obj_set_style_bg_opa(s_qr_textarea, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(s_qr_textarea, highlight_color(), 0);
  lv_obj_set_style_border_width(s_qr_textarea, 2, 0);
  lv_obj_set_style_radius(s_qr_textarea, 8, 0);
#ifndef SIMULATOR
  ui_textarea_enable_safe_keyboard_shortcuts(s_qr_textarea);
#endif

  create_action_button(panel, "Generate QR", qr_text_generate_event_cb, NULL,
                       true);

  lv_obj_t *keyboard = lv_keyboard_create(panel);
  lv_obj_set_width(keyboard, LV_PCT(100));
  lv_obj_set_height(keyboard, 220);
  lv_keyboard_set_textarea(keyboard, s_qr_textarea);
  lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
#ifndef SIMULATOR
  ui_keyboard_apply_safe_text_map(keyboard);
#endif
  theme_apply_btnmatrix(keyboard);
  lv_obj_set_style_text_font(keyboard, theme_font_small(), LV_PART_ITEMS);
  lv_obj_add_event_cb(keyboard, qr_keyboard_event_cb, LV_EVENT_READY, NULL);

  update_qr_text_preview();
}

static void create_qr_demo_block(lv_obj_t *parent, const char *title,
                                 const char *payload,
                                 const char *warning) {
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(12, theme_get_small_padding()));
  lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  create_text(panel, title ? title : "QR Display", false, highlight_color());
  if (warning && warning[0])
    create_text(panel, warning, false, error_color());

  int qr_size = theme_get_screen_width() - max_i(96, theme_get_default_padding() * 3);
  if (qr_size > 320)
    qr_size = 320;
  if (qr_size < 180)
    qr_size = 180;

  lv_obj_t *qr = lv_qrcode_create(panel);
  lv_qrcode_set_size(qr, qr_size);
  lv_qrcode_set_dark_color(qr, bg_color());
  lv_qrcode_set_light_color(qr, main_color());
  lv_obj_set_style_border_color(qr, main_color(), 0);
  lv_obj_set_style_border_width(qr, 10, 0);
  lv_obj_set_style_radius(qr, 0, 0);

  const char *data = payload ? payload : "KSIG-QR";
  lv_result_t res = lv_qrcode_update(qr, data, (uint32_t)strlen(data));
  create_text(panel, res == LV_RESULT_OK ? "QR encoded" : "QR encoding failed",
              false, res == LV_RESULT_OK ? yes_color() : error_color());
}

static void create_delivery_acceptance_block(lv_obj_t *parent) {
  lv_obj_t *panel =
      create_panel(parent, yes_color(), max_i(12, theme_get_small_padding()));
  create_text(panel, "Wallet Check Overview", false, yes_color());

  create_build_identity_block(parent);
  create_hardware_snapshot_block(parent, "Hardware Snapshot");

  lv_obj_t *actions =
      create_panel(parent, highlight_color(), max_i(12, theme_get_small_padding()));
  create_text(actions, "Device Check", false, highlight_color());
  create_action_button(actions, "Open Scanner", camera_preview_event_cb,
                       (void *)&CAMERA_ACTION_TOOLS_QR, true);
  create_nav_button(actions, "Open Text QR", "tools_create_qr", false);
  create_nav_button(actions, "Open Touch Test", "test_screen_touch", false);
  create_nav_button(actions, "Open Brightness", "settings_display", false);
  create_nav_button(actions, "Open Wallet", "legacy_login", true);
  create_nav_button(actions, "Import or Create Wallet", "legacy_load_wallet", false);
  create_nav_button(actions, "Scan Sign", "legacy_scan_sign", false);

  lv_obj_t *storage_result =
      create_text(actions, "Storage: check not run yet.", false, main_color());
  create_action_button(actions, "Start Storage R/W", storage_probe_event_cb,
                       storage_result, false);

  lv_obj_t *files_result =
      create_text(actions, "File list: not refreshed yet.", false, main_color());
  create_action_button(actions, "Refresh Storage Files", file_list_event_cb,
                       files_result, false);

  lv_obj_t *wallet =
      create_panel(parent, yes_color(), max_i(12, theme_get_small_padding()));
  create_text(wallet, "Wallet Entry", false, yes_color());
  create_nav_button(wallet, "Open Wallet", "legacy_login", true);
}

static bool create_special_detail_cards(lv_obj_t *parent,
                                        const signer_feature_t *feature) {
  if (strcmp(feature->id, "test_screen_touch") == 0) {
    create_touch_probe_block(parent);
    return true;
  }

  if (strcmp(feature->id, "test_storage") == 0) {
    create_hardware_snapshot_block(parent, "Hardware Snapshot");
    create_storage_probe_block(parent);
    return true;
  }

  if (strcmp(feature->id, "test_camera") == 0) {
    create_camera_probe_block(parent);
    create_hardware_snapshot_block(parent, "Hardware Snapshot");
    return true;
  }

  if (strcmp(feature->id, "smartcard_probe") == 0) {
    create_smartcard_probe_block(parent);
    create_hardware_snapshot_block(parent, "Hardware Snapshot");
    return true;
  }

  if (strcmp(feature->id, "smartcard_card_info") == 0) {
    create_card_info_block(parent);
    return true;
  }

  if (strcmp(feature->id, "smartcard_web3_scan") == 0) {
    create_satochip_web3_block(parent);
    lv_obj_t *panel =
        create_panel(parent, cyan_color(), max_i(12, theme_get_small_padding()));
    create_text(panel, "Web3 Request", true, main_color());
#ifndef SIMULATOR
    create_action_button(panel, "Open Scanner", smartcard_web3_scan_event_cb, NULL,
                         true);
#else
    create_nav_button(panel, "Open Scanner", "legacy_scan_sign", true);
#endif
    return true;
  }

  if (strcmp(feature->id, "smartcard_seedkeeper_status_page") == 0) {
    create_satochip_seedkeeper_status_block(parent, feature);
    return true;
  }

  if (strcmp(feature->id, "smartcard_seedkeeper_free_space") == 0) {
    create_satochip_seedkeeper_free_space_block(parent, feature);
    return true;
  }

  if (strcmp(feature->id, "smartcard_seedkeeper_list_page") == 0) {
    create_satochip_seedkeeper_list_block(parent, feature);
    return true;
  }

  if (strcmp(feature->id, "smartcard_seedkeeper_logs_page") == 0) {
    create_satochip_seedkeeper_logs_block(parent, feature);
    return true;
  }

  if (strcmp(feature->id, "web3_satochip") == 0) {
    create_satochip_web3_block(parent);
    return true;
  }

  if (strcmp(feature->id, "smartcard_satochip_change_pin") == 0) {
    create_satochip_change_pin_block(parent, feature);
    return true;
  }

  if (strcmp(feature->id, "smartcard_satochip_setup_pin") == 0) {
    create_satochip_setup_pin_block(parent, feature);
    return true;
  }

  if (strcmp(feature->id, "smartcard_satochip_unblock_pin") == 0) {
    create_satochip_unblock_pin_block(parent, feature);
    return true;
  }

  if (strcmp(feature->id, "smartcard_satochip_set_label") == 0) {
    create_satochip_set_label_block(parent, feature);
    return true;
  }

  if (strcmp(feature->id, "smartcard_satochip_nfc_policy") == 0) {
    create_satochip_nfc_policy_block(parent, feature);
    return true;
  }

  if (strcmp(feature->id, "smartcard_satochip_feature_policy") == 0) {
    create_satochip_feature_policy_block(parent, feature);
    return true;
  }

  if (strcmp(feature->id, "smartcard_satochip_reset_seed") == 0) {
    create_satochip_reset_seed_block(parent, feature);
    return true;
  }

  if (strcmp(feature->id, "smartcard_satochip_reset_factory") == 0) {
    create_satochip_reset_factory_block(parent, feature);
    return true;
  }

  if (strcmp(feature->id, "smartcard_satochip_export_authentikey") == 0) {
    create_satochip_export_authentikey_block(parent, feature);
    return true;
  }

  if (strcmp(feature->id, "smartcard_satochip_import_ndef_authentikey") == 0) {
    create_satochip_import_ndef_authentikey_block(parent, feature);
    return true;
  }

  if (strcmp(feature->id, "smartcard_satochip_import_trusted_pubkey") == 0) {
    create_satochip_import_trusted_pubkey_block(parent, feature);
    return true;
  }

  if (strcmp(feature->id, "smartcard_satochip_export_trusted_pubkey") == 0) {
    create_satochip_export_trusted_pubkey_block(parent, feature);
    return true;
  }

  if (strcmp(feature->id, "smartcard_satochip_set_2fa_key") == 0) {
    create_satochip_set_2fa_key_block(parent, feature);
    return true;
  }

  if (strcmp(feature->id, "smartcard_satochip_reset_2fa_key") == 0) {
    create_satochip_reset_2fa_key_block(parent, feature);
    return true;
  }

  if (strcmp(feature->id, "smartcard_satochip_logout_all") == 0) {
    create_satochip_logout_all_block(parent, feature);
    return true;
  }

  if (strcmp(feature->id, "smartcard_seedkeeper_create_mnemonic") == 0 ||
      strcmp(feature->id, "new_seedkeeper_create_mnemonic") == 0) {
    create_satochip_seedkeeper_create_mnemonic_block(parent, feature);
    return true;
  }

  if (strcmp(feature->id, "smartcard_seedkeeper_write_mnemonic") == 0) {
    create_satochip_seedkeeper_write_mnemonic_block(parent, feature);
    return true;
  }

  if (strcmp(feature->id, "smartcard_satochip_write_mnemonic") == 0) {
    create_satochip_write_mnemonic_block(parent, feature);
    return true;
  }

  if (strcmp(feature->id, "smartcard_seedkeeper_view_mnemonic") == 0) {
    create_satochip_seedkeeper_view_mnemonic_block(parent, feature);
    return true;
  }

  if (strcmp(feature->id, "smartcard_seedkeeper_load_mnemonic") == 0 ||
      strcmp(feature->id, "load_seedkeeper_mnemonic") == 0) {
    create_satochip_seedkeeper_load_mnemonic_block(parent, feature);
    return true;
  }

  if (strcmp(feature->id, "smartcard_seedkeeper_save_secret") == 0) {
    create_satochip_seedkeeper_save_secret_block(parent, feature);
    return true;
  }

  if (strcmp(feature->id, "smartcard_seedkeeper_load_descriptor") == 0) {
    create_satochip_seedkeeper_stub_block(
        parent, feature, SATOCHIP_MAINT_SEEDKEEPER_LOAD_DESCRIPTOR, "Card PIN",
        true, "SID", "Required", NULL, NULL);
    return true;
  }

  if (strcmp(feature->id, "smartcard_seedkeeper_save_descriptor") == 0) {
    create_satochip_seedkeeper_save_descriptor_block(parent, feature);
    return true;
  }

  if (strcmp(feature->id, "smartcard_seedkeeper_clone") == 0) {
    create_satochip_seedkeeper_stub_block(
        parent, feature, SATOCHIP_MAINT_SEEDKEEPER_CLONE, "Source Card PIN", true,
        "SID", "Required", "Target Public Key SID", "Required");
    return true;
  }

  if (strcmp(feature->id, "smartcard_seedkeeper_setup_pin") == 0) {
    create_satochip_seedkeeper_setup_pin_block(parent, feature);
    return true;
  }

  if (strcmp(feature->id, "smartcard_seedkeeper_change_pin") == 0) {
    create_satochip_seedkeeper_change_pin_block(parent, feature);
    return true;
  }

  if (strcmp(feature->id, "smartcard_seedkeeper_reset") == 0) {
    create_satochip_seedkeeper_reset_block(parent, feature);
    return true;
  }

  if (strcmp(feature->id, "smartcard_seedkeeper_generate_masterseed") == 0) {
    create_satochip_seedkeeper_stub_block(
        parent, feature, SATOCHIP_MAINT_SEEDKEEPER_GENERATE_MASTERSEED,
        "Card PIN", true, "Length", "16/32/64", "Label", "Masterseed");
    return true;
  }

  if (strcmp(feature->id, "smartcard_seedkeeper_generate_2fa_secret") == 0) {
    create_satochip_seedkeeper_stub_block(
        parent, feature, SATOCHIP_MAINT_SEEDKEEPER_GENERATE_2FA_SECRET,
        "Card PIN", true, "Label", "2FA", NULL, NULL);
    return true;
  }

  if (strcmp(feature->id, "smartcard_seedkeeper_derive_master_password") == 0) {
    create_satochip_seedkeeper_stub_block(
        parent, feature, SATOCHIP_MAINT_SEEDKEEPER_DERIVE_MASTER_PASSWORD,
        "Card PIN", true, "SID", "Required", "Salt", "Optional");
    return true;
  }

  if (strcmp(feature->id, "smartcard_seedkeeper_reset_secret") == 0) {
    create_satochip_seedkeeper_stub_block(
        parent, feature, SATOCHIP_MAINT_SEEDKEEPER_RESET_SECRET, "Card PIN", true,
        "SID", "Required", NULL, NULL);
    return true;
  }

  if (strcmp(feature->id, "smartcard_certificate_export") == 0) {
    create_satochip_certificate_export_block(parent, feature);
    return true;
  }

  if (strcmp(feature->id, "smartcard_certificate_import") == 0) {
    create_satochip_certificate_import_block(parent, feature);
    return true;
  }

  if (strcmp(feature->id, "smartcard_satochip_authenticity") == 0) {
    create_satochip_authenticity_block(parent, feature);
    return true;
  }

  if (strcmp(feature->id, "delivery_check") == 0) {
    create_delivery_acceptance_block(parent);
    return true;
  }

  if (strcmp(feature->id, "satochip_path_address") == 0 ||
      strcmp(feature->id, "connect_wallet_satochip_address") == 0) {
    create_satochip_tool_block(parent, feature, SATOCHIP_TOOL_PATH_ADDRESS);
    return true;
  }

  if (strcmp(feature->id, "web3_address_satochip") == 0) {
    create_satochip_tool_block(parent, feature, SATOCHIP_TOOL_PATH_ADDRESS);
    return true;
  }

  if (strcmp(feature->id, "satochip_btc_zpub") == 0 ||
      strcmp(feature->id, "btc_satochip_zpub") == 0) {
    create_satochip_tool_block(parent, feature, SATOCHIP_TOOL_BTC_ZPUB);
    return true;
  }

  if (strcmp(feature->id, "satochip_btc_xpub") == 0 ||
      strcmp(feature->id, "btc_satochip_xpub") == 0) {
    create_satochip_tool_block(parent, feature, SATOCHIP_TOOL_BTC_XPUB);
    return true;
  }

  if (strcmp(feature->id, "satochip_btc_ypub") == 0) {
    create_satochip_tool_block(parent, feature, SATOCHIP_TOOL_BTC_YPUB);
    return true;
  }

  if (strcmp(feature->id, "satochip_btc_tpub") == 0) {
    create_satochip_tool_block(parent, feature, SATOCHIP_TOOL_BTC_TPUB);
    return true;
  }

  if (strcmp(feature->id, "satochip_btc_upub") == 0) {
    create_satochip_tool_block(parent, feature, SATOCHIP_TOOL_BTC_UPUB);
    return true;
  }

  if (strcmp(feature->id, "satochip_btc_vpub") == 0) {
    create_satochip_tool_block(parent, feature, SATOCHIP_TOOL_BTC_VPUB);
    return true;
  }

  if (is_web3_satochip_choice(feature->id)) {
    create_satochip_connect_block(parent, feature);
    return true;
  }

  if (strcmp(feature->id, "load_camera") == 0) {
    create_camera_action_block(parent, &CAMERA_ACTION_SEED_QR);
    return true;
  }

  if (strcmp(feature->id, "sign_psbt_qr") == 0) {
    create_camera_action_block(parent, &CAMERA_ACTION_SIGN_QR);
    return true;
  }

  if (strcmp(feature->id, "tools_qr_capture") == 0) {
    create_camera_action_block(parent, &CAMERA_ACTION_TOOLS_QR);
    return true;
  }

  if (strcmp(feature->id, "settings_camera") == 0) {
    create_camera_settings_block(parent);
    create_camera_action_block(parent, &CAMERA_ACTION_TOOLS_QR);
    return true;
  }

  if (strcmp(feature->id, "settings_locale") == 0) {
    create_locale_settings_block(parent);
    return true;
  }

  if (strcmp(feature->id, "settings_wallet") == 0) {
    create_wallet_settings_block(parent);
    return true;
  }

  if (strcmp(feature->id, "settings_security") == 0) {
    create_security_settings_block(parent);
    create_system_security_block(parent);
    return true;
  }

  if (strcmp(feature->id, "tools_file_manager") == 0) {
    create_file_manager_block(parent);
    return true;
  }

  if (strcmp(feature->id, "new_camera_entropy") == 0) {
    create_camera_action_block(parent, &CAMERA_ACTION_ENTROPY);
    return true;
  }

  if (strcmp(feature->id, "addr_scan_check") == 0) {
    create_camera_action_block(parent, &CAMERA_ACTION_ADDRESS);
    return true;
  }

  if (strcmp(feature->id, "test_power") == 0 ||
      strcmp(feature->id, "settings_display") == 0) {
    create_display_settings_block(parent);
    create_hardware_snapshot_block(parent, "Hardware Snapshot");
    return true;
  }

  if (strcmp(feature->id, "tools_create_qr") == 0) {
    create_qr_text_tool_block(parent);
    return true;
  }

  if (strcmp(feature->id, "system_overview") == 0) {
    create_build_identity_block(parent);
    create_hardware_snapshot_block(parent, "Hardware Snapshot");
    create_system_security_block(parent);
    return true;
  }

  if (strcmp(feature->id, "security_check") == 0) {
    create_system_security_block(parent);
    create_build_identity_block(parent);
    return true;
  }

  if (strcmp(feature->id, "addr_qr_view") == 0 ||
      strcmp(feature->id, "wallet_public_key") == 0 ||
      strcmp(feature->id, "wallet_descriptor") == 0 ||
      strcmp(feature->id, "export_public_data") == 0) {
    create_qr_demo_block(parent, "Public Data QR",
                         "bitcoin:tb1qexample0000000000000000000000000000000",
                         "");
    return true;
  }

  if (strcmp(feature->id, "backup_seed_qr") == 0) {
    create_qr_demo_block(parent, "Mnemonic QR", "Open Real QR", "");
    return true;
  }

  if (strcmp(feature->id, "backup_kef") == 0) {
    create_direct_input_block(parent, feature,
                              "Encrypt the current mnemonic and save it to storage.");
    return true;
  }

  if (is_web3_wallet_choice(feature->id)) {
    create_connect_wallet_block(parent, feature);
    return true;
  }

  if (is_btc_wallet_choice(feature->id)) {
    create_btc_wallet_block(parent, feature);
    return true;
  }

  if (strcmp(feature->id, "new_hex_entropy") == 0) {
    create_direct_input_block(parent, feature, "");
    return true;
  }

  if (strcmp(feature->id, "new_dice_d6") == 0) {
    create_direct_input_block(parent, feature, "");
    return true;
  }

  if (strcmp(feature->id, "new_coin_entropy") == 0) {
    create_direct_input_block(parent, feature, "");
    return true;
  }

  if (strcmp(feature->id, "new_words_select") == 0) {
    create_direct_input_block(parent, feature, "");
    return true;
  }

  if (strcmp(feature->id, "new_cards_entropy") == 0) {
    create_direct_input_block(parent, feature, "");
    return true;
  }

  if (strcmp(feature->id, "new_dice_d20") == 0) {
    create_direct_input_block(parent, feature, "");
    return true;
  }

  if (strcmp(feature->id, "bip85_mnemonic") == 0) {
    create_direct_input_block(parent, feature, "");
    return true;
  }

  if (strcmp(feature->id, "bip39_check_tools") == 0) {
    create_direct_input_block(parent, feature, "");
    return true;
  }

  if (strcmp(feature->id, "custom_derivation") == 0) {
    create_direct_input_block(parent, feature, "");
    return true;
  }

  if (strcmp(feature->id, "tools_secondary_mnemonic") == 0) {
    create_direct_input_block(parent, feature, "");
    return true;
  }

  if (strcmp(feature->id, "login_passphrase") == 0) {
    create_direct_input_block(parent, feature, "");
    return true;
  }

  if (strcmp(feature->id, "about") == 0) {
    create_build_identity_block(parent);
    create_system_security_block(parent);
    return true;
  }

  return false;
}

static void create_detail_cards(lv_obj_t *parent,
                                const signer_feature_t *feature) {
  if (strcmp(feature->id, "load_mnemonic") == 0) {
    create_signer_child_menu(parent, feature);
    return;
  }
  if (strcmp(feature->id, "load_punch_grid") == 0) {
    create_signer_child_menu(parent, feature);
    return;
  }
  if (strcmp(feature->id, "backup_seed_words") == 0 ||
      strcmp(feature->id, "backup_entropy") == 0 ||
      strcmp(feature->id, "backup_grid") == 0 ||
      strcmp(feature->id, "backup_steel_punch") == 0 ||
      strcmp(feature->id, "backup_stackbit") == 0 ||
      strcmp(feature->id, "backup_seed_qr") == 0 ||
      strcmp(feature->id, "backup_kef") == 0) {
    create_wallet_entry_block(parent, feature);
    return;
  }

  if (legacy_wallet_route_for_target(feature->id)) {
    create_wallet_entry_block(parent, feature);
    return;
  }

  if (create_special_detail_cards(parent, feature))
    return;

  if (feature->risk == SIGNER_FEATURE_RISK_SECRET_MATERIAL ||
      feature->risk == SIGNER_FEATURE_RISK_SIGNING) {
    create_wallet_entry_block(parent, feature);
  } else {
    create_build_identity_block(parent);
  }
}

static void style_signer_container(lv_obj_t *obj, lv_color_t color) {
  lv_obj_set_style_bg_color(obj, color, 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  lv_obj_set_style_outline_width(obj, 0, 0);
  lv_obj_set_style_shadow_width(obj, 0, 0);
  lv_obj_set_style_radius(obj, 0, 0);
}

static void create_signer_status_bar(lv_obj_t *root,
                                   const signer_feature_t *feature) {
  (void)feature;
  lv_obj_t *bar = lv_obj_create(root);
  lv_obj_set_width(bar, LV_PCT(100));
  lv_obj_set_height(bar, shell_status_height());
  style_signer_container(bar, signer_card_color());
  lv_obj_set_style_pad_all(bar, 0, 0);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *battery = lv_obj_create(bar);
  lv_obj_set_size(battery, 22, 10);
  lv_obj_align(battery, LV_ALIGN_RIGHT_MID, -6, 0);
  lv_obj_set_style_bg_color(battery, signer_card_color(), 0);
  lv_obj_set_style_bg_opa(battery, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(battery, signer_text_color(), 0);
  lv_obj_set_style_border_width(battery, 2, 0);
  lv_obj_set_style_radius(battery, 0, 0);
  lv_obj_set_style_pad_all(battery, 0, 0);

  lv_obj_t *tip = lv_obj_create(bar);
  lv_obj_set_size(tip, 3, 6);
  lv_obj_align_to(tip, battery, LV_ALIGN_OUT_RIGHT_MID, 0, 0);
  lv_obj_set_style_bg_color(tip, signer_text_color(), 0);
  lv_obj_set_style_bg_opa(tip, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(tip, 0, 0);
  lv_obj_set_style_radius(tip, 0, 0);
}

static void create_signer_header(lv_obj_t *root, const signer_feature_t *feature) {
  create_signer_status_bar(root, feature);

  if (!feature || strcmp(feature->id, "home") == 0)
    return;

  lv_obj_t *header = lv_obj_create(root);
  lv_obj_set_width(header, LV_PCT(100));
  lv_obj_set_height(header, LV_SIZE_CONTENT);
  style_signer_container(header, signer_canvas_color());
  lv_obj_set_style_pad_all(header, 0, 0);
  lv_obj_set_style_pad_top(header, shell_header_pad_y(), 0);
  lv_obj_set_style_pad_bottom(header, shell_header_pad_y(), 0);
  lv_obj_set_flex_flow(header, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(header, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  lv_obj_t *title_row = lv_obj_create(header);
  lv_obj_set_width(title_row, LV_PCT(100));
  lv_obj_set_height(title_row, shell_is_wave_43_portrait() ? 54 : 60);
  style_signer_container(title_row, signer_canvas_color());
  lv_obj_set_style_pad_all(title_row, 0, 0);
  lv_obj_set_style_pad_column(title_row, shell_is_wave_43_portrait() ? 16 : 18,
                              0);
  lv_obj_set_flex_flow(title_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(title_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  lv_obj_t *back = lv_btn_create(title_row);
  lv_obj_set_size(back, shell_is_wave_43_portrait() ? 84 : 96,
                  shell_is_wave_43_portrait() ? 52 : 54);
  lv_obj_set_style_bg_color(back, signer_card_color(), 0);
  lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(back, highlight_color(), 0);
  lv_obj_set_style_border_width(back, 2, 0);
  lv_obj_set_style_radius(back, shell_card_radius(), 0);
  lv_obj_set_style_shadow_width(back, 0, 0);
  lv_obj_add_event_cb(back, nav_event_cb, LV_EVENT_CLICKED,
                      (void *)shell_back_target_for_feature(feature));

  lv_obj_t *back_label = lv_label_create(back);
  lv_label_set_text(back_label, i18n_tr_or("common.back", "Back"));
  lv_label_set_long_mode(back_label, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_font(back_label, theme_font_small(), 0);
  lv_obj_set_style_text_color(back_label, main_color(), 0);
  lv_obj_center(back_label);

  lv_obj_t *title = lv_label_create(title_row);
  lv_label_set_text(title, shell_feature_title(feature));
  lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
  lv_obj_set_flex_grow(title, 1);
  lv_obj_set_style_text_font(title, theme_font_medium(), 0);
  lv_obj_set_style_text_color(title, main_color(), 0);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_t *spacer = lv_obj_create(title_row);
  lv_obj_set_size(spacer, shell_is_wave_43_portrait() ? 84 : 96,
                  shell_is_wave_43_portrait() ? 50 : 54);
  style_signer_container(spacer, signer_canvas_color());
  lv_obj_clear_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);

}

static lv_obj_t *create_signer_list(lv_obj_t *root, bool center_items,
                                  bool grid_mode) {
  bool home_grid = grid_mode && s_rendering_home_grid;
  lv_obj_t *list = lv_obj_create(root);
  lv_obj_set_width(list, LV_PCT(100));
  lv_obj_set_flex_grow(list, 1);
  style_signer_container(list, signer_canvas_color());
  lv_obj_set_style_pad_all(list, 0, 0);
  if (grid_mode && !home_grid) {
    lv_obj_set_style_pad_top(list, shell_is_wave_43_portrait() ? 22 : 20, 0);
  }
  lv_obj_set_style_pad_row(list, grid_mode ? shell_menu_gap()
                                           : (shell_is_wave_43_portrait() ? 8 : 0),
                           0);
  lv_obj_set_style_pad_column(list, grid_mode ? shell_menu_gap() : 0, 0);
  lv_obj_set_flex_flow(list, grid_mode ? LV_FLEX_FLOW_ROW_WRAP
                                       : LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(list,
                        grid_mode ? LV_FLEX_ALIGN_START
                                  : (center_items ? LV_FLEX_ALIGN_CENTER
                                                  : LV_FLEX_ALIGN_START),
                        grid_mode ? (home_grid ? LV_FLEX_ALIGN_SPACE_EVENLY
                                               : LV_FLEX_ALIGN_START)
                                  : LV_FLEX_ALIGN_CENTER,
                        grid_mode ? (home_grid ? LV_FLEX_ALIGN_SPACE_EVENLY
                                               : LV_FLEX_ALIGN_START)
                                  : LV_FLEX_ALIGN_CENTER);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
  return list;
}

static lv_obj_t *create_signer_menu_button(lv_obj_t *parent, const char *label,
                                         const char *subtitle,
                                         const char *target_id,
                                         bool primary) {
  lv_obj_t *btn = lv_btn_create(parent);
  bool home_grid = shell_home_grid_active();
  lv_obj_set_width(btn, s_menu_grid_mode ? (home_grid ? LV_PCT(47) : LV_PCT(47))
                                         : LV_PCT(100));
  bool multiline = label && strchr(label, '\n');
  (void)subtitle;
  bool has_subtitle = false;
  bool compact_grid = shell_compact_grid_active();
  int height = s_menu_grid_mode ? (home_grid ? (shell_is_wave_43_portrait()
                                                    ? shell_home_card_height()
                                                    : shell_home_card_height())
                                             : (compact_grid
                                                    ? (shell_is_wave_43_portrait()
                                                           ? 78
                                                           : 92)
                                                    : (shell_is_wave_43_portrait()
                                                           ? 92
                                                           : 112)))
                                : shell_menu_button_height(multiline,
                                                           has_subtitle);
  lv_obj_set_height(btn, height);
  lv_obj_set_style_bg_color(btn, signer_card_color(), LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(btn, signer_card_pressed_color(), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(btn, 8, 0);
  lv_obj_set_style_border_width(btn, 2, 0);
  lv_obj_set_style_border_color(btn, highlight_color(), 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_set_style_pad_left(btn, home_grid ? 14 : (s_menu_grid_mode ? 8
                                                  : (shell_is_wave_43_portrait()
                                                         ? 14
                                                         : 18)),
                            0);
  lv_obj_set_style_pad_right(btn, home_grid ? 14 : (s_menu_grid_mode ? 8
                                                   : (shell_is_wave_43_portrait()
                                                          ? 14
                                                          : 18)),
                             0);
  lv_obj_set_style_pad_top(btn, home_grid ? 12 : (compact_grid ? 4
                                                               : (s_menu_grid_mode
                                                                      ? 6
                                                                      : 8)),
                           0);
  lv_obj_set_style_pad_bottom(btn, home_grid ? 12 : (compact_grid ? 4
                                                                  : (s_menu_grid_mode
                                                                         ? 6
                                                                         : 8)),
                              0);
  lv_obj_set_style_pad_gap(btn, home_grid ? 10 : (s_menu_grid_mode ? 4 : 2), 0);
  lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_add_event_cb(btn, nav_event_cb, LV_EVENT_CLICKED, (void *)target_id);

  lv_obj_t *title = lv_label_create(btn);
  lv_label_set_text(title, label ? label : "");
  lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(title, LV_PCT(100));
  lv_obj_set_style_text_font(title, theme_font_medium(), 0);
  if (home_grid)
    lv_obj_set_style_text_font(title, theme_font_medium(), 0);
  lv_obj_set_style_text_color(title, signer_text_color(), 0);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

  return btn;
}

static bool create_signer_override_menu(lv_obj_t *list,
                                      const signer_menu_override_t *items,
                                      size_t count) {
  if (!items || count == 0)
    return false;

  for (size_t i = 0; i < count; i++) {
    create_signer_menu_button(list, shell_menu_item_label(&items[i]), NULL,
                              items[i].target_id, true);
  }
  return true;
}

static bool signer_screen_has_override_menu(const char *id) {
  return id && (strcmp(id, "home") == 0 || strcmp(id, "load_mnemonic") == 0 ||
                strcmp(id, "new_mnemonic") == 0 ||
                strcmp(id, "tools") == 0 ||
                strcmp(id, "settings") == 0 ||
                strcmp(id, "wallet_home") == 0 ||
                strcmp(id, "signing") == 0 ||
                signer_sign_wallet_group_id(id) ||
                strcmp(id, "satochip_btc_pubkeys") == 0 ||
                strcmp(id, "smartcard_tools") == 0 ||
                signer_smartcard_tools_group_id(id) ||
                signer_smartcard_seedkeeper_group_id(id) ||
                signer_smartcard_certificate_group_id(id) ||
                is_connect_wallet_group_menu(id) ||
                strcmp(id, "backup_export") == 0 ||
                strcmp(id, "addresses") == 0 ||
                strcmp(id, "device_tests") == 0 ||
                strcmp(id, "pi_mnemonic_tools") == 0 ||
                strcmp(id, "mnemonic_write_smartcard") == 0 ||
                strcmp(id, "pi_mnemonic_advanced") == 0 ||
                strcmp(id, "pi_connect_wallet") == 0 ||
                strcmp(id, "pi_self_check") == 0);
}

static size_t signer_override_menu_count(const char *id) {
  if (!id)
    return 0;
  if (strcmp(id, "home") == 0)
    return sizeof(SIGNER_HOME_MENU) / sizeof(SIGNER_HOME_MENU[0]);
  if (strcmp(id, "load_mnemonic") == 0)
    return sizeof(SIGNER_LOAD_MENU) / sizeof(SIGNER_LOAD_MENU[0]);
  if (strcmp(id, "new_mnemonic") == 0)
    return sizeof(SIGNER_NEW_MENU) / sizeof(SIGNER_NEW_MENU[0]);
  if (strcmp(id, "tools") == 0)
    return sizeof(SIGNER_TOOLS_MENU) / sizeof(SIGNER_TOOLS_MENU[0]);
  if (strcmp(id, "settings") == 0)
    return sizeof(SIGNER_SETTINGS_MENU) / sizeof(SIGNER_SETTINGS_MENU[0]);
  if (strcmp(id, "wallet_home") == 0)
    return sizeof(SIGNER_WALLET_MENU) / sizeof(SIGNER_WALLET_MENU[0]);
  if (strcmp(id, "signing") == 0)
    return sizeof(SIGNER_SIGNING_MENU) / sizeof(SIGNER_SIGNING_MENU[0]);
  if (strcmp(id, "sign_okx") == 0)
    return sizeof(SIGNER_SIGN_OKX_MENU) / sizeof(SIGNER_SIGN_OKX_MENU[0]);
  if (strcmp(id, "sign_bitget") == 0)
    return sizeof(SIGNER_SIGN_BITGET_MENU) / sizeof(SIGNER_SIGN_BITGET_MENU[0]);
  if (strcmp(id, "sign_metamask") == 0)
    return sizeof(SIGNER_SIGN_METAMASK_MENU) /
           sizeof(SIGNER_SIGN_METAMASK_MENU[0]);
  if (strcmp(id, "sign_rabby") == 0)
    return sizeof(SIGNER_SIGN_RABBY_MENU) / sizeof(SIGNER_SIGN_RABBY_MENU[0]);
  if (strcmp(id, "sign_tokenpocket") == 0)
    return sizeof(SIGNER_SIGN_TOKENPOCKET_MENU) /
           sizeof(SIGNER_SIGN_TOKENPOCKET_MENU[0]);
  if (strcmp(id, "sign_imtoken") == 0)
    return sizeof(SIGNER_SIGN_IMTOKEN_MENU) / sizeof(SIGNER_SIGN_IMTOKEN_MENU[0]);
  if (strcmp(id, "sign_btc") == 0)
    return sizeof(SIGNER_SIGN_BTC_MENU) / sizeof(SIGNER_SIGN_BTC_MENU[0]);
  if (strcmp(id, "satochip_btc_pubkeys") == 0)
    return sizeof(SIGNER_SATOCHIP_PUBKEY_MENU) /
           sizeof(SIGNER_SATOCHIP_PUBKEY_MENU[0]);
  if (strcmp(id, "connect_web3") == 0)
    return sizeof(SIGNER_CONNECT_WEB3_MENU) /
           sizeof(SIGNER_CONNECT_WEB3_MENU[0]);
  if (strcmp(id, "connect_okx") == 0)
    return sizeof(SIGNER_CONNECT_OKX_MENU) / sizeof(SIGNER_CONNECT_OKX_MENU[0]);
  if (strcmp(id, "connect_bitget") == 0)
    return sizeof(SIGNER_CONNECT_BITGET_MENU) /
           sizeof(SIGNER_CONNECT_BITGET_MENU[0]);
  if (strcmp(id, "connect_metamask") == 0)
    return sizeof(SIGNER_CONNECT_METAMASK_MENU) /
           sizeof(SIGNER_CONNECT_METAMASK_MENU[0]);
  if (strcmp(id, "connect_rabby") == 0)
    return sizeof(SIGNER_CONNECT_RABBY_MENU) /
           sizeof(SIGNER_CONNECT_RABBY_MENU[0]);
  if (strcmp(id, "connect_tokenpocket") == 0)
    return sizeof(SIGNER_CONNECT_TOKENPOCKET_MENU) /
           sizeof(SIGNER_CONNECT_TOKENPOCKET_MENU[0]);
  if (strcmp(id, "connect_imtoken") == 0)
    return sizeof(SIGNER_CONNECT_IMTOKEN_MENU) /
           sizeof(SIGNER_CONNECT_IMTOKEN_MENU[0]);
  if (strcmp(id, "connect_keystone") == 0)
    return sizeof(SIGNER_CONNECT_KEYSTONE_MENU) /
           sizeof(SIGNER_CONNECT_KEYSTONE_MENU[0]);
  if (strcmp(id, "btc_wallet") == 0)
    return sizeof(SIGNER_BTC_WALLET_MENU) / sizeof(SIGNER_BTC_WALLET_MENU[0]);
  if (strcmp(id, "btc_mnemonic") == 0)
    return sizeof(SIGNER_BTC_MNEMONIC_MENU) / sizeof(SIGNER_BTC_MNEMONIC_MENU[0]);
  if (strcmp(id, "btc_satochip") == 0)
    return sizeof(SIGNER_BTC_SATOCHIP_MENU) /
           sizeof(SIGNER_BTC_SATOCHIP_MENU[0]);
  if (strcmp(id, "backup_export") == 0)
    return sizeof(SIGNER_BACKUP_MENU) / sizeof(SIGNER_BACKUP_MENU[0]);
  if (strcmp(id, "addresses") == 0)
    return sizeof(SIGNER_ADDRESSES_MENU) / sizeof(SIGNER_ADDRESSES_MENU[0]);
  if (strcmp(id, "device_tests") == 0)
    return sizeof(SIGNER_DEVICE_TESTS_MENU) / sizeof(SIGNER_DEVICE_TESTS_MENU[0]);
  if (strcmp(id, "smartcard_tools") == 0)
    return sizeof(SIGNER_SMARTCARD_MENU) / sizeof(SIGNER_SMARTCARD_MENU[0]);
  if (strcmp(id, "smartcard_satochip_tools") == 0)
    return sizeof(SIGNER_SMARTCARD_SATOCHIP_MENU) /
           sizeof(SIGNER_SMARTCARD_SATOCHIP_MENU[0]);
  if (strcmp(id, "smartcard_satochip_maint") == 0)
    return sizeof(SIGNER_SMARTCARD_MAINT_MENU) /
           sizeof(SIGNER_SMARTCARD_MAINT_MENU[0]);
  if (strcmp(id, "smartcard_satochip_advanced_tools") == 0)
    return sizeof(SIGNER_SMARTCARD_ADVANCED_MENU) /
           sizeof(SIGNER_SMARTCARD_ADVANCED_MENU[0]);
  if (strcmp(id, "smartcard_satochip_pubkey_tools") == 0)
    return sizeof(SIGNER_SMARTCARD_PUBKEY_MENU) /
           sizeof(SIGNER_SMARTCARD_PUBKEY_MENU[0]);
  if (strcmp(id, "smartcard_satochip_2fa_tools") == 0)
    return sizeof(SIGNER_SMARTCARD_2FA_MENU) / sizeof(SIGNER_SMARTCARD_2FA_MENU[0]);
  if (strcmp(id, "smartcard_satochip_session_tools") == 0)
    return sizeof(SIGNER_SMARTCARD_SESSION_MENU) /
           sizeof(SIGNER_SMARTCARD_SESSION_MENU[0]);
  if (strcmp(id, "smartcard_seedkeeper_advanced_tools") == 0)
    return sizeof(SIGNER_SMARTCARD_SEEDKEEPER_ADVANCED_MENU) /
           sizeof(SIGNER_SMARTCARD_SEEDKEEPER_ADVANCED_MENU[0]);
  if (signer_smartcard_seedkeeper_group_id(id))
    return sizeof(SIGNER_SMARTCARD_SEEDKEEPER_MENU) /
           sizeof(SIGNER_SMARTCARD_SEEDKEEPER_MENU[0]);
  if (signer_smartcard_certificate_group_id(id))
    return sizeof(SIGNER_SMARTCARD_CERTIFICATE_MENU) /
           sizeof(SIGNER_SMARTCARD_CERTIFICATE_MENU[0]);
  if (strcmp(id, "pi_mnemonic_tools") == 0)
    return sizeof(SIGNER_PI_MNEMONIC_MENU) / sizeof(SIGNER_PI_MNEMONIC_MENU[0]);
  if (strcmp(id, "mnemonic_write_smartcard") == 0)
    return sizeof(SIGNER_MNEMONIC_WRITE_SMARTCARD_MENU) /
           sizeof(SIGNER_MNEMONIC_WRITE_SMARTCARD_MENU[0]);
  if (strcmp(id, "pi_mnemonic_advanced") == 0)
    return sizeof(SIGNER_PI_MNEMONIC_ADVANCED_MENU) /
           sizeof(SIGNER_PI_MNEMONIC_ADVANCED_MENU[0]);
  if (strcmp(id, "pi_connect_wallet") == 0)
    return sizeof(SIGNER_PI_CONNECT_MENU) / sizeof(SIGNER_PI_CONNECT_MENU[0]);
  if (strcmp(id, "pi_self_check") == 0)
    return sizeof(SIGNER_PI_SELF_CHECK_MENU) / sizeof(SIGNER_PI_SELF_CHECK_MENU[0]);
  return 0;
}

static void create_signer_home_menu(lv_obj_t *list) {
  (void)SIGNER_HOME_MENU_IDS;
  for (size_t i = 0; i < sizeof(SIGNER_HOME_MENU) / sizeof(SIGNER_HOME_MENU[0]); i++) {
    create_signer_menu_button(list, shell_menu_item_label(&SIGNER_HOME_MENU[i]), NULL,
                            SIGNER_HOME_MENU[i].target_id, true);
  }
}

static void create_signer_child_menu(lv_obj_t *list,
                                   const signer_feature_t *feature) {
  if (strcmp(feature->id, "load_mnemonic") == 0 &&
      create_signer_override_menu(
          list, SIGNER_LOAD_MENU,
          sizeof(SIGNER_LOAD_MENU) / sizeof(SIGNER_LOAD_MENU[0])))
    return;
  if (strcmp(feature->id, "new_mnemonic") == 0 &&
      create_signer_override_menu(list, SIGNER_NEW_MENU,
                                sizeof(SIGNER_NEW_MENU) / sizeof(SIGNER_NEW_MENU[0])))
    return;
  if (strcmp(feature->id, "tools") == 0 &&
      create_signer_override_menu(
          list, SIGNER_TOOLS_MENU,
          sizeof(SIGNER_TOOLS_MENU) / sizeof(SIGNER_TOOLS_MENU[0])))
    return;
  if (strcmp(feature->id, "settings") == 0 &&
      create_signer_override_menu(
          list, SIGNER_SETTINGS_MENU,
          sizeof(SIGNER_SETTINGS_MENU) / sizeof(SIGNER_SETTINGS_MENU[0])))
    return;
  if (strcmp(feature->id, "wallet_home") == 0 &&
      create_signer_override_menu(
          list, SIGNER_WALLET_MENU,
          sizeof(SIGNER_WALLET_MENU) / sizeof(SIGNER_WALLET_MENU[0])))
    return;
  if (strcmp(feature->id, "signing") == 0 &&
      create_signer_override_menu(
          list, SIGNER_SIGNING_MENU,
          sizeof(SIGNER_SIGNING_MENU) / sizeof(SIGNER_SIGNING_MENU[0])))
    return;
  if (strcmp(feature->id, "sign_okx") == 0 &&
      create_signer_override_menu(
          list, SIGNER_SIGN_OKX_MENU,
          sizeof(SIGNER_SIGN_OKX_MENU) / sizeof(SIGNER_SIGN_OKX_MENU[0])))
    return;
  if (strcmp(feature->id, "sign_bitget") == 0 &&
      create_signer_override_menu(
          list, SIGNER_SIGN_BITGET_MENU,
          sizeof(SIGNER_SIGN_BITGET_MENU) / sizeof(SIGNER_SIGN_BITGET_MENU[0])))
    return;
  if (strcmp(feature->id, "sign_metamask") == 0 &&
      create_signer_override_menu(
          list, SIGNER_SIGN_METAMASK_MENU,
          sizeof(SIGNER_SIGN_METAMASK_MENU) /
              sizeof(SIGNER_SIGN_METAMASK_MENU[0])))
    return;
  if (strcmp(feature->id, "sign_rabby") == 0 &&
      create_signer_override_menu(
          list, SIGNER_SIGN_RABBY_MENU,
          sizeof(SIGNER_SIGN_RABBY_MENU) / sizeof(SIGNER_SIGN_RABBY_MENU[0])))
    return;
  if (strcmp(feature->id, "sign_tokenpocket") == 0 &&
      create_signer_override_menu(
          list, SIGNER_SIGN_TOKENPOCKET_MENU,
          sizeof(SIGNER_SIGN_TOKENPOCKET_MENU) /
              sizeof(SIGNER_SIGN_TOKENPOCKET_MENU[0])))
    return;
  if (strcmp(feature->id, "sign_imtoken") == 0 &&
      create_signer_override_menu(
          list, SIGNER_SIGN_IMTOKEN_MENU,
          sizeof(SIGNER_SIGN_IMTOKEN_MENU) / sizeof(SIGNER_SIGN_IMTOKEN_MENU[0])))
    return;
  if (strcmp(feature->id, "sign_btc") == 0 &&
      create_signer_override_menu(
          list, SIGNER_SIGN_BTC_MENU,
          sizeof(SIGNER_SIGN_BTC_MENU) / sizeof(SIGNER_SIGN_BTC_MENU[0])))
    return;
  if (strcmp(feature->id, "satochip_btc_pubkeys") == 0 &&
      create_signer_override_menu(
          list, SIGNER_SATOCHIP_PUBKEY_MENU,
          sizeof(SIGNER_SATOCHIP_PUBKEY_MENU) /
              sizeof(SIGNER_SATOCHIP_PUBKEY_MENU[0])))
    return;
  if (strcmp(feature->id, "connect_web3") == 0 &&
      create_signer_override_menu(
          list, SIGNER_CONNECT_WEB3_MENU,
          sizeof(SIGNER_CONNECT_WEB3_MENU) / sizeof(SIGNER_CONNECT_WEB3_MENU[0])))
    return;
  if (strcmp(feature->id, "connect_okx") == 0 &&
      create_signer_override_menu(
          list, SIGNER_CONNECT_OKX_MENU,
          sizeof(SIGNER_CONNECT_OKX_MENU) / sizeof(SIGNER_CONNECT_OKX_MENU[0])))
    return;
  if (strcmp(feature->id, "connect_bitget") == 0 &&
      create_signer_override_menu(
          list, SIGNER_CONNECT_BITGET_MENU,
          sizeof(SIGNER_CONNECT_BITGET_MENU) /
              sizeof(SIGNER_CONNECT_BITGET_MENU[0])))
    return;
  if (strcmp(feature->id, "connect_metamask") == 0 &&
      create_signer_override_menu(
          list, SIGNER_CONNECT_METAMASK_MENU,
          sizeof(SIGNER_CONNECT_METAMASK_MENU) /
              sizeof(SIGNER_CONNECT_METAMASK_MENU[0])))
    return;
  if (strcmp(feature->id, "connect_rabby") == 0 &&
      create_signer_override_menu(
          list, SIGNER_CONNECT_RABBY_MENU,
          sizeof(SIGNER_CONNECT_RABBY_MENU) /
              sizeof(SIGNER_CONNECT_RABBY_MENU[0])))
    return;
  if (strcmp(feature->id, "connect_tokenpocket") == 0 &&
      create_signer_override_menu(
          list, SIGNER_CONNECT_TOKENPOCKET_MENU,
          sizeof(SIGNER_CONNECT_TOKENPOCKET_MENU) /
              sizeof(SIGNER_CONNECT_TOKENPOCKET_MENU[0])))
    return;
  if (strcmp(feature->id, "connect_imtoken") == 0 &&
      create_signer_override_menu(list, SIGNER_CONNECT_IMTOKEN_MENU,
                                sizeof(SIGNER_CONNECT_IMTOKEN_MENU) /
                                    sizeof(SIGNER_CONNECT_IMTOKEN_MENU[0])))
    return;
  if (strcmp(feature->id, "connect_keystone") == 0 &&
      create_signer_override_menu(
          list, SIGNER_CONNECT_KEYSTONE_MENU,
          sizeof(SIGNER_CONNECT_KEYSTONE_MENU) /
              sizeof(SIGNER_CONNECT_KEYSTONE_MENU[0])))
    return;
  if (strcmp(feature->id, "btc_wallet") == 0 &&
      create_signer_override_menu(
          list, SIGNER_BTC_WALLET_MENU,
          sizeof(SIGNER_BTC_WALLET_MENU) / sizeof(SIGNER_BTC_WALLET_MENU[0])))
    return;
  if (strcmp(feature->id, "btc_mnemonic") == 0 &&
      create_signer_override_menu(
          list, SIGNER_BTC_MNEMONIC_MENU,
          sizeof(SIGNER_BTC_MNEMONIC_MENU) /
              sizeof(SIGNER_BTC_MNEMONIC_MENU[0])))
    return;
  if (strcmp(feature->id, "btc_satochip") == 0 &&
      create_signer_override_menu(
          list, SIGNER_BTC_SATOCHIP_MENU,
          sizeof(SIGNER_BTC_SATOCHIP_MENU) /
              sizeof(SIGNER_BTC_SATOCHIP_MENU[0])))
    return;
  if (strcmp(feature->id, "backup_export") == 0 &&
      create_signer_override_menu(
          list, SIGNER_BACKUP_MENU,
          sizeof(SIGNER_BACKUP_MENU) / sizeof(SIGNER_BACKUP_MENU[0])))
    return;
  if (strcmp(feature->id, "addresses") == 0 &&
      create_signer_override_menu(
          list, SIGNER_ADDRESSES_MENU,
          sizeof(SIGNER_ADDRESSES_MENU) / sizeof(SIGNER_ADDRESSES_MENU[0])))
    return;
  if (strcmp(feature->id, "device_tests") == 0 &&
      create_signer_override_menu(
          list, SIGNER_DEVICE_TESTS_MENU,
          sizeof(SIGNER_DEVICE_TESTS_MENU) / sizeof(SIGNER_DEVICE_TESTS_MENU[0])))
    return;
  if (strcmp(feature->id, "smartcard_tools") == 0 &&
      create_signer_override_menu(
          list, SIGNER_SMARTCARD_MENU,
          sizeof(SIGNER_SMARTCARD_MENU) / sizeof(SIGNER_SMARTCARD_MENU[0])))
    return;
  if (strcmp(feature->id, "smartcard_satochip_tools") == 0 &&
      create_signer_override_menu(
          list, SIGNER_SMARTCARD_SATOCHIP_MENU,
          sizeof(SIGNER_SMARTCARD_SATOCHIP_MENU) /
              sizeof(SIGNER_SMARTCARD_SATOCHIP_MENU[0])))
    return;
  if (strcmp(feature->id, "smartcard_satochip_maint") == 0 &&
      create_signer_override_menu(
          list, SIGNER_SMARTCARD_MAINT_MENU,
          sizeof(SIGNER_SMARTCARD_MAINT_MENU) /
              sizeof(SIGNER_SMARTCARD_MAINT_MENU[0])))
    return;
  if (strcmp(feature->id, "smartcard_satochip_advanced_tools") == 0 &&
      create_signer_override_menu(
          list, SIGNER_SMARTCARD_ADVANCED_MENU,
          sizeof(SIGNER_SMARTCARD_ADVANCED_MENU) /
              sizeof(SIGNER_SMARTCARD_ADVANCED_MENU[0])))
    return;
  if (strcmp(feature->id, "smartcard_satochip_pubkey_tools") == 0 &&
      create_signer_override_menu(
          list, SIGNER_SMARTCARD_PUBKEY_MENU,
          sizeof(SIGNER_SMARTCARD_PUBKEY_MENU) /
              sizeof(SIGNER_SMARTCARD_PUBKEY_MENU[0])))
    return;
  if (strcmp(feature->id, "smartcard_satochip_2fa_tools") == 0 &&
      create_signer_override_menu(
          list, SIGNER_SMARTCARD_2FA_MENU,
          sizeof(SIGNER_SMARTCARD_2FA_MENU) / sizeof(SIGNER_SMARTCARD_2FA_MENU[0])))
    return;
  if (strcmp(feature->id, "smartcard_satochip_session_tools") == 0 &&
      create_signer_override_menu(
          list, SIGNER_SMARTCARD_SESSION_MENU,
          sizeof(SIGNER_SMARTCARD_SESSION_MENU) /
              sizeof(SIGNER_SMARTCARD_SESSION_MENU[0])))
    return;
  if (strcmp(feature->id, "smartcard_seedkeeper_advanced_tools") == 0 &&
      create_signer_override_menu(
          list, SIGNER_SMARTCARD_SEEDKEEPER_ADVANCED_MENU,
          sizeof(SIGNER_SMARTCARD_SEEDKEEPER_ADVANCED_MENU) /
              sizeof(SIGNER_SMARTCARD_SEEDKEEPER_ADVANCED_MENU[0])))
    return;
  if (strcmp(feature->id, "smartcard_satochip_seedkeeper_tools") == 0 &&
      create_signer_override_menu(
          list, SIGNER_SMARTCARD_SEEDKEEPER_MENU,
          sizeof(SIGNER_SMARTCARD_SEEDKEEPER_MENU) /
              sizeof(SIGNER_SMARTCARD_SEEDKEEPER_MENU[0])))
    return;
  if (strcmp(feature->id, "smartcard_satochip_certificate_tools") == 0 &&
      create_signer_override_menu(
          list, SIGNER_SMARTCARD_CERTIFICATE_MENU,
          sizeof(SIGNER_SMARTCARD_CERTIFICATE_MENU) /
              sizeof(SIGNER_SMARTCARD_CERTIFICATE_MENU[0])))
    return;
  if (strcmp(feature->id, "pi_mnemonic_tools") == 0 &&
      create_signer_override_menu(
          list, SIGNER_PI_MNEMONIC_MENU,
          sizeof(SIGNER_PI_MNEMONIC_MENU) / sizeof(SIGNER_PI_MNEMONIC_MENU[0])))
    return;
  if (strcmp(feature->id, "mnemonic_write_smartcard") == 0 &&
      create_signer_override_menu(
          list, SIGNER_MNEMONIC_WRITE_SMARTCARD_MENU,
          sizeof(SIGNER_MNEMONIC_WRITE_SMARTCARD_MENU) /
              sizeof(SIGNER_MNEMONIC_WRITE_SMARTCARD_MENU[0])))
    return;
  if (strcmp(feature->id, "pi_mnemonic_advanced") == 0 &&
      create_signer_override_menu(
          list, SIGNER_PI_MNEMONIC_ADVANCED_MENU,
          sizeof(SIGNER_PI_MNEMONIC_ADVANCED_MENU) /
              sizeof(SIGNER_PI_MNEMONIC_ADVANCED_MENU[0])))
    return;
  if (strcmp(feature->id, "pi_connect_wallet") == 0 &&
      create_signer_override_menu(
          list, SIGNER_PI_CONNECT_MENU,
          sizeof(SIGNER_PI_CONNECT_MENU) / sizeof(SIGNER_PI_CONNECT_MENU[0])))
    return;
  if (strcmp(feature->id, "pi_self_check") == 0 &&
      create_signer_override_menu(
          list, SIGNER_PI_SELF_CHECK_MENU,
          sizeof(SIGNER_PI_SELF_CHECK_MENU) / sizeof(SIGNER_PI_SELF_CHECK_MENU[0])))
    return;

  size_t child_count = signer_feature_child_count(feature->id);
  for (size_t i = 0; i < child_count; i++) {
    const signer_feature_t *child = signer_feature_child_at(feature->id, i);
    if (child) {
      create_signer_menu_button(list, shell_feature_title(child),
                              child->subtitle, child->id,
                              i == 0);
    }
  }
}

static void create_signer_leaf_content(lv_obj_t *list,
                                     const signer_feature_t *feature) {
  create_detail_cards(list, feature);
}

static void create_signer_bottom_nav(lv_obj_t *root,
                                   const signer_feature_t *feature) {
  if (!feature->parent_id)
    return;

  const char *back_target = shell_back_target_for_feature(feature);

  lv_obj_t *row = lv_obj_create(root);
  lv_obj_set_width(row, LV_PCT(100));
  lv_obj_set_height(row, LV_SIZE_CONTENT);
  style_signer_container(row, signer_canvas_color());
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_style_pad_column(row, 10, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  if (strcmp(back_target, "home") != 0) {
    lv_obj_t *home = create_signer_menu_button(
        row, i18n_tr_or("common.return_home", "Return Home"), NULL, "home",
        false);
    lv_obj_set_width(home, LV_PCT(48));
    lv_obj_set_height(home, shell_bottom_nav_height());
  }

  lv_obj_t *back = create_signer_menu_button(
      row, i18n_tr_or("common.back", "Back"), NULL, back_target, true);
  lv_obj_set_width(back, strcmp(back_target, "home") != 0 ? LV_PCT(48)
                                                          : LV_PCT(100));
  lv_obj_set_height(back, shell_bottom_nav_height());
}

static void render_screen(const signer_feature_t *feature) {
  if (!s_parent || !feature)
    return;

  shell_transient_state_reset();
  lv_obj_clean(s_parent);
  theme_apply_screen(s_parent);

  lv_obj_t *root = theme_create_page_container(s_parent);
  lv_obj_set_style_bg_color(root, signer_canvas_color(), 0);
  lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  bool is_home = strcmp(feature->id, "home") == 0;
  lv_obj_set_style_pad_top(root, shell_margin_v(), 0);
  lv_obj_set_style_pad_bottom(root, shell_margin_v(), 0);
  lv_obj_set_style_pad_left(root, shell_margin_h(), 0);
  lv_obj_set_style_pad_right(root, shell_margin_h(), 0);
  lv_obj_set_style_pad_gap(root, shell_root_gap(is_home), 0);

  create_signer_header(root, feature);

  size_t child_count = signer_feature_child_count(feature->id);
  bool has_override = signer_screen_has_override_menu(feature->id);
  size_t menu_count = has_override ? signer_override_menu_count(feature->id)
                                   : child_count;
  bool menu_screen = child_count > 0 || has_override;
  bool grid_mode =
      menu_screen && shell_should_use_grid(menu_count, is_home, feature->id);
  if (grid_mode && shell_force_single_column_menu(feature->id))
    grid_mode = false;
  bool center_items = menu_screen && menu_count > 0 && menu_count <= 5;
  s_menu_grid_mode = grid_mode;
  s_rendering_home_grid = grid_mode && is_home;
  s_compact_menu_grid = grid_mode && strcmp(feature->id, "smartcard_tools") == 0;
  lv_obj_t *list = create_signer_list(root, center_items, grid_mode);
  if (!menu_screen && satochip_screen_uses_onscreen_keyboard(feature->id)) {
    lv_obj_set_style_pad_bottom(list, LV_VER_RES * 38 / 100, 0);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
  }
  if (menu_screen) {
    if (is_home)
      create_signer_home_menu(list);
    else
      create_signer_child_menu(list, feature);
  } else {
    create_signer_leaf_content(list, feature);
  }

  s_menu_grid_mode = false;
  s_rendering_home_grid = false;
  s_compact_menu_grid = false;
  const char *back_target = shell_back_target_for_feature(feature);
  const signer_feature_t *parent = back_target ? signer_feature_find(back_target) : NULL;
  bool deep_menu_screen = menu_screen && feature->parent_id && parent &&
                          parent->parent_id &&
                          strcmp(parent->parent_id, "home") != 0;
  if (!menu_screen || deep_menu_screen)
    create_signer_bottom_nav(root, feature);
}

void signer_shell_create(lv_obj_t *parent) {
  s_parent = parent ? parent : lv_screen_active();
  (void)signer_shell_show_screen("home");
}

bool signer_shell_show_screen(const char *screen_id) {
  const char *requested_id = screen_id ? screen_id : "home";
  const char *alias_target = shell_alias_target_for_id(requested_id);
  if (alias_target)
    requested_id = alias_target;
  if (satochip_legacy_seedkeeper_id(requested_id))
    requested_id = "smartcard_satochip_seedkeeper_tools";
  if (!product_screen_is_visible(requested_id))
    return false;

#ifdef SIMULATOR
  if (strcmp(requested_id, "custom_derivation") == 0)
    return simulator_launch_custom_derivation();
#endif

#ifndef SIMULATOR
  if (!s_allow_sensitive_render && target_requires_shell_gate(requested_id) &&
      legacy_wallet_handle_target(requested_id)) {
    return true;
  }
#endif

  const signer_feature_t *feature = shell_feature_find(requested_id);
  if (!feature)
    return false;

  if (!s_parent)
    s_parent = lv_screen_active();

  s_current_screen_id = feature->id;
  render_screen(feature);
  return true;
}

size_t signer_shell_screen_count(void) {
  return sizeof(PRODUCT_SCREEN_IDS) / sizeof(PRODUCT_SCREEN_IDS[0]);
}

const char *signer_shell_screen_id_at(size_t index) {
  if (index >= signer_shell_screen_count())
    return NULL;
  return PRODUCT_SCREEN_IDS[index];
}

const char *signer_shell_screen_title_at(size_t index) {
  const char *id = signer_shell_screen_id_at(index);
  const signer_feature_t *feature = shell_feature_find(id);
  return feature ? shell_feature_title(feature) : NULL;
}

const char *signer_shell_current_screen_id(void) { return s_current_screen_id; }
