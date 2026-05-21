#include "krux_shell.h"
#include "krux_port/krux_camera_preview.h"
#include "krux_port/krux_feature_catalog.h"
#include "krux_port/krux_hardware_probe.h"
#include "krux_port/krux_services.h"
#include "krux_port/krux_storage_browser.h"
#include "core/crypto_utils.h"
#include "core/evm.h"
#include "core/pin.h"
#include "core/settings.h"
#include "qr/viewer.h"
#include "smartcard/smartcard_ccid.h"
#include "smartcard/smartcard_satochip.h"
#include "ui/dialog.h"
#include "ui/input_helpers.h"
#include "ui/theme.h"
#include "utils/secure_mem.h"

#ifndef SIMULATOR
#include "core/key.h"
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

#include <bsp/display.h>
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

static const char *TAG = "KERN_KRUX";
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
#endif

static const char *const KRUX_HOME_MENU_IDS[] = {
    "legacy_scan_sign", "pi_connect_wallet", "pi_mnemonic_tools",
    "smartcard_tools", "settings", "pi_self_check",
};

typedef struct {
  const char *label;
  const char *target_id;
} krux_menu_override_t;

static bool krux_id_is(const char *id, const char *expected) {
  return id && expected && strcmp(id, expected) == 0;
}

static bool krux_id_is_any(const char *id, const char *a, const char *b) {
  return krux_id_is(id, a) || krux_id_is(id, b);
}

static bool krux_smartcard_tools_group_id(const char *id) {
  return krux_id_is_any(id, "smartcard_satochip_tools",
                        "smartcard_satochip_maint") ||
         krux_id_is_any(id, "smartcard_satochip_advanced_tools",
                        "smartcard_satochip_pubkey_tools") ||
         krux_id_is_any(id, "smartcard_satochip_2fa_tools",
                        "smartcard_satochip_session_tools");
}

static bool krux_smartcard_seedkeeper_group_id(const char *id) {
  return krux_id_is_any(id, "smartcard_satochip_seedkeeper_tools",
                        "smartcard_seedkeeper_advanced_tools");
}

static bool krux_smartcard_certificate_group_id(const char *id) {
  return krux_id_is_any(id, "smartcard_satochip_certificate_tools",
                        "smartcard_certificate_tools");
}

static bool krux_id_has_prefix(const char *id, const char *prefix) {
  if (!id || !prefix)
    return false;
  return strncmp(id, prefix, strlen(prefix)) == 0;
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
  if (strcmp(id, "web3_address") == 0)
    return "connect_address";
  return NULL;
}

static const char *shell_back_target_for_feature(const krux_feature_t *feature) {
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

static bool krux_sign_wallet_group_id(const char *id) {
  return krux_id_is_any(id, "sign_okx", "sign_bitget") ||
         krux_id_is_any(id, "sign_metamask", "sign_rabby") ||
         krux_id_is_any(id, "sign_tokenpocket", "sign_btc");
}

static bool krux_sign_mnemonic_target_id(const char *id) {
  return krux_id_is_any(id, "sign_okx_mnemonic", "sign_bitget_mnemonic") ||
         krux_id_is_any(id, "sign_metamask_mnemonic",
                        "sign_rabby_mnemonic") ||
         krux_id_is_any(id, "sign_tokenpocket_mnemonic",
                        "sign_btc_mnemonic");
}

static bool krux_sign_satochip_target_id(const char *id) {
  return krux_id_is_any(id, "sign_okx_satochip", "sign_bitget_satochip") ||
         krux_id_is_any(id, "sign_metamask_satochip",
                        "sign_rabby_satochip") ||
         krux_id_is_any(id, "sign_tokenpocket_satochip",
                        "sign_btc_satochip");
}

static const krux_menu_override_t KRUX_HOME_MENU[] = {
    {"扫码签名", "legacy_scan_sign"},
    {"连接钱包", "pi_connect_wallet"},
    {"助记词", "pi_mnemonic_tools"},
    {"智能卡", "smartcard_tools"},
    {"设置", "settings"},
    {"自检", "pi_self_check"},
    {"功能", "pi_features"},
    {"工具", "tools"},
};

static const krux_menu_override_t KRUX_LOAD_MENU[] = {
    {"二维码", "load_camera"},
    {"手动", "load_manual"},
    {"序号", "load_digits"},
    {"智能卡", "load_seedkeeper_mnemonic"},
    {"存储卡", "load_sd"},
    {"加密", "load_encrypted_kef"},
    {"钢板", "load_steel_restore"},
    {"点阵", "load_punch_grid"},
};

static const krux_menu_override_t KRUX_NEW_MENU[] = {
    {"骰子", "new_dice_d6"},
    {"抛硬币", "new_coin_entropy"},
    {"D20", "new_dice_d20"},
    {"扑克牌", "new_cards_entropy"},
    {"手动", "new_words_select"},
    {"相机", "new_camera_entropy"},
    {"十六进制", "new_hex_entropy"},
    {"智能卡", "new_seedkeeper_create_mnemonic"},
};

static const krux_menu_override_t KRUX_TOOLS_MENU[] = {
    {"二维码", "tools_create_qr"},
    {"扫码", "tools_qr_capture"},
    {"文件", "tools_file_manager"},
};

static const krux_menu_override_t KRUX_PI_MNEMONIC_MENU[] = {
    {"创建", "new_mnemonic"},
    {"导入", "load_mnemonic"},
    {"本机助记词", "legacy_select_mnemonic"},
    {"密语", "login_passphrase"},
    {"高级", "pi_mnemonic_advanced"},
};

static const krux_menu_override_t KRUX_PI_MNEMONIC_ADVANCED_MENU[] = {
    {"助记词加密", "tools_secondary_mnemonic"},
    {"派生地址", "custom_derivation"},
    {"BIP85", "bip85"},
    {"自检", "bip39_check_tools"},
};

static const krux_menu_override_t KRUX_PI_CONNECT_MENU[] = {
    {"OKX", "connect_okx"},
    {"Bitget", "connect_bitget"},
    {"MetaMask", "connect_metamask"},
    {"Rabby", "connect_rabby"},
    {"TokenPocket", "connect_tokenpocket"},
    {"地址", "connect_address"},
    {"BTC", "btc_wallet"},
};

static const krux_menu_override_t KRUX_CONNECT_WEB3_MENU[] = {
    {"OKX", "connect_okx"},
    {"Bitget", "connect_bitget"},
    {"MetaMask", "connect_metamask"},
    {"Rabby", "connect_rabby"},
    {"TokenPocket", "connect_tokenpocket"},
};

static const krux_menu_override_t KRUX_CONNECT_OKX_MENU[] = {
    {"助记词", "web3_okx_mnemonic"},
    {"智能卡", "web3_okx_satochip"},
};

static const krux_menu_override_t KRUX_CONNECT_BITGET_MENU[] = {
    {"助记词", "web3_bitget_mnemonic"},
    {"智能卡", "web3_bitget_satochip"},
};

static const krux_menu_override_t KRUX_CONNECT_METAMASK_MENU[] = {
    {"助记词", "web3_metamask_mnemonic"},
    {"智能卡", "web3_metamask_satochip"},
};

static const krux_menu_override_t KRUX_CONNECT_RABBY_MENU[] = {
    {"助记词", "web3_rabby_mnemonic"},
    {"智能卡", "web3_rabby_satochip"},
};

static const krux_menu_override_t KRUX_CONNECT_TOKENPOCKET_MENU[] = {
    {"助记词", "web3_tokenpocket_mnemonic"},
    {"智能卡", "web3_tokenpocket_satochip"},
};

static const krux_menu_override_t KRUX_CONNECT_ADDRESS_MENU[] = {
    {"助记词", "web3_address_mnemonic"},
    {"智能卡", "web3_address_satochip"},
};

static const krux_menu_override_t KRUX_BTC_WALLET_MENU[] = {
    {"助记词", "btc_mnemonic"},
    {"智能卡", "btc_satochip"},
};

static const krux_menu_override_t KRUX_BTC_MNEMONIC_MENU[] = {
    {"zpub", "btc_bluewallet_zpub"},
    {"xpub", "btc_bluewallet_xpub"},
};

static const krux_menu_override_t KRUX_BTC_SATOCHIP_MENU[] = {
    {"zpub", "btc_satochip_zpub"},
    {"xpub", "btc_satochip_xpub"},
};

static const krux_menu_override_t KRUX_PI_SELF_CHECK_MENU[] = {
    {"系统", "system_overview"},
    {"安全", "security_check"},
    {"交付", "delivery_check"},
    {"智能卡", "smartcard_probe"},
    {"外设", "device_tests"},
};

static const krux_menu_override_t KRUX_WALLET_MENU[] = {
    {"助记词", "legacy_select_mnemonic"},
    {"扫码签名", "legacy_scan_sign"},
    {"公钥", "legacy_public_key"},
    {"地址", "legacy_addresses"},
    {"备份", "legacy_backup_wallet"},
    {"设置", "settings_wallet"},
    {"清除会话", "login_clear_session"},
};

static const krux_menu_override_t KRUX_SIGNING_MENU[] = {
    {"OKX", "sign_okx"},
    {"Bitget", "sign_bitget"},
    {"MetaMask", "sign_metamask"},
    {"Rabby", "sign_rabby"},
    {"TokenPocket", "sign_tokenpocket"},
    {"BTC", "sign_btc"},
};

static const krux_menu_override_t KRUX_SIGN_OKX_MENU[] = {
    {"助记词", "sign_okx_mnemonic"},
    {"智能卡", "sign_okx_satochip"},
};

static const krux_menu_override_t KRUX_SIGN_BITGET_MENU[] = {
    {"助记词", "sign_bitget_mnemonic"},
    {"智能卡", "sign_bitget_satochip"},
};

static const krux_menu_override_t KRUX_SIGN_METAMASK_MENU[] = {
    {"助记词", "sign_metamask_mnemonic"},
    {"智能卡", "sign_metamask_satochip"},
};

static const krux_menu_override_t KRUX_SIGN_RABBY_MENU[] = {
    {"助记词", "sign_rabby_mnemonic"},
    {"智能卡", "sign_rabby_satochip"},
};

static const krux_menu_override_t KRUX_SIGN_TOKENPOCKET_MENU[] = {
    {"助记词", "sign_tokenpocket_mnemonic"},
    {"智能卡", "sign_tokenpocket_satochip"},
};

static const krux_menu_override_t KRUX_SIGN_BTC_MENU[] = {
    {"助记词", "sign_btc_mnemonic"},
    {"智能卡", "sign_btc_satochip"},
};

static const krux_menu_override_t KRUX_SMARTCARD_MENU[] = {
    {"Satochip", "smartcard_satochip_tools"},
    {"SeedKeeper", "smartcard_satochip_seedkeeper_tools"},
};

static const krux_menu_override_t KRUX_SMARTCARD_SATOCHIP_MENU[] = {
    {"信息", "smartcard_card_info"},
    {"地址", "satochip_path_address"},
    {"BTC公钥", "satochip_btc_pubkeys"},
    {"维护", "smartcard_satochip_maint"},
    {"高级", "smartcard_satochip_advanced_tools"},
    {"证书", "smartcard_satochip_certificate_tools"},
};

static const krux_menu_override_t KRUX_SMARTCARD_MAINT_MENU[] = {
    {"改PIN", "smartcard_satochip_change_pin"},
    {"解锁", "smartcard_satochip_unblock_pin"},
    {"改标签", "smartcard_satochip_set_label"},
    {"NFC", "smartcard_satochip_nfc_policy"},
    {"功能", "smartcard_satochip_feature_policy"},
    {"重置", "smartcard_satochip_reset_seed"},
    {"出厂", "smartcard_satochip_reset_factory"},
    {"真伪", "smartcard_satochip_authenticity"},
};

static const krux_menu_override_t KRUX_SMARTCARD_ADVANCED_MENU[] = {
    {"认证钥", "smartcard_satochip_pubkey_tools"},
    {"2FA", "smartcard_satochip_2fa_tools"},
    {"会话", "smartcard_satochip_session_tools"},
};

static const krux_menu_override_t KRUX_SMARTCARD_PUBKEY_MENU[] = {
    {"导主钥", "smartcard_satochip_export_authentikey"},
    {"写NDEF", "smartcard_satochip_import_ndef_authentikey"},
    {"写信任钥", "smartcard_satochip_import_trusted_pubkey"},
    {"导信任钥", "smartcard_satochip_export_trusted_pubkey"},
};

static const krux_menu_override_t KRUX_SMARTCARD_2FA_MENU[] = {
    {"设2FA", "smartcard_satochip_set_2fa_key"},
    {"清2FA", "smartcard_satochip_reset_2fa_key"},
};

static const krux_menu_override_t KRUX_SMARTCARD_SESSION_MENU[] = {
    {"登出", "smartcard_satochip_logout_all"},
};

static const krux_menu_override_t KRUX_SMARTCARD_SEEDKEEPER_MENU[] = {
    {"状态", "smartcard_seedkeeper_status_page"},
    {"剩余空间", "smartcard_seedkeeper_free_space"},
    {"列表", "smartcard_seedkeeper_list_page"},
    {"创建", "smartcard_seedkeeper_create_mnemonic"},
    {"写入", "smartcard_seedkeeper_write_mnemonic"},
    {"查看", "smartcard_seedkeeper_view_mnemonic"},
    {"导入", "smartcard_seedkeeper_load_mnemonic"},
    {"存密码", "smartcard_seedkeeper_save_password"},
    {"读描述符", "smartcard_seedkeeper_load_descriptor"},
    {"存描述符", "smartcard_seedkeeper_save_descriptor"},
    {"克隆", "smartcard_seedkeeper_clone"},
    {"改PIN", "smartcard_seedkeeper_change_pin"},
    {"重置", "smartcard_seedkeeper_reset"},
    {"高级", "smartcard_seedkeeper_advanced_tools"},
};

static const krux_menu_override_t KRUX_SMARTCARD_SEEDKEEPER_ADVANCED_MENU[] = {
    {"主种子", "smartcard_seedkeeper_generate_masterseed"},
    {"2FA", "smartcard_seedkeeper_generate_2fa_secret"},
    {"派生密码", "smartcard_seedkeeper_derive_master_password"},
    {"重置条目", "smartcard_seedkeeper_reset_secret"},
};

static const krux_menu_override_t KRUX_SMARTCARD_CERTIFICATE_MENU[] = {
    {"导出", "smartcard_certificate_export"},
    {"导入", "smartcard_certificate_import"},
};

static const krux_feature_t KRUX_LOCAL_SMARTCARD_FEATURES[] = {
    {"smartcard_satochip_seedkeeper_tools", "smartcard_tools",
     "SeedKeeper", "状态 / 助记词 / 高级",
     "管理 SeedKeeper。",
     "main/pages/krux_shell/krux_shell.c", KRUX_FEATURE_GROUP,
     KRUX_FEATURE_UI_READY, KRUX_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_seedkeeper_status_page",
     "smartcard_satochip_seedkeeper_tools", "状态", "读卡 / 空间",
     "查看 SeedKeeper 状态和空间。",
     "main/pages/krux_shell/krux_shell.c", KRUX_FEATURE_ACTION,
     KRUX_FEATURE_UI_READY, KRUX_FEATURE_RISK_EXTERNAL_IO},
    {"smartcard_seedkeeper_free_space",
     "smartcard_satochip_seedkeeper_tools", "剩余空间", "可用空间",
     "查看 SeedKeeper 剩余空间。",
     "main/pages/krux_shell/krux_shell.c", KRUX_FEATURE_ACTION,
     KRUX_FEATURE_UI_READY, KRUX_FEATURE_RISK_EXTERNAL_IO},
    {"smartcard_seedkeeper_list_page",
     "smartcard_satochip_seedkeeper_tools", "列表", "条目",
     "查看卡内条目列表。",
     "main/pages/krux_shell/krux_shell.c", KRUX_FEATURE_ACTION,
     KRUX_FEATURE_UI_READY, KRUX_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_seedkeeper_logs_page",
     "smartcard_satochip_seedkeeper_tools", "日志", "卡日志",
     "查看 SeedKeeper 日志。",
     "main/pages/krux_shell/krux_shell.c", KRUX_FEATURE_ACTION,
     KRUX_FEATURE_UI_READY, KRUX_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_seedkeeper_advanced_tools",
     "smartcard_satochip_seedkeeper_tools", "高级", "生成 / 派生 / 重置",
     "SeedKeeper 高级动作。",
     "main/pages/krux_shell/krux_shell.c", KRUX_FEATURE_GROUP,
     KRUX_FEATURE_UI_READY, KRUX_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_seedkeeper_generate_masterseed",
     "smartcard_seedkeeper_advanced_tools", "主种子", "生成主种子",
     "在卡上生成主种子。",
     "main/pages/krux_shell/krux_shell.c", KRUX_FEATURE_ACTION,
     KRUX_FEATURE_UI_READY, KRUX_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_seedkeeper_generate_2fa_secret",
     "smartcard_seedkeeper_advanced_tools", "2FA", "生成 2FA",
     "在卡上生成 2FA 秘密。",
     "main/pages/krux_shell/krux_shell.c", KRUX_FEATURE_ACTION,
     KRUX_FEATURE_UI_READY, KRUX_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_seedkeeper_derive_master_password",
     "smartcard_seedkeeper_advanced_tools", "派生密码", "按 SID 派生",
     "从卡内条目派生主密码。",
     "main/pages/krux_shell/krux_shell.c", KRUX_FEATURE_ACTION,
     KRUX_FEATURE_UI_READY, KRUX_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_seedkeeper_reset_secret",
     "smartcard_seedkeeper_advanced_tools", "重置条目", "按 SID 删除",
     "删除卡内指定条目。",
     "main/pages/krux_shell/krux_shell.c", KRUX_FEATURE_ACTION,
     KRUX_FEATURE_UI_READY, KRUX_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_seedkeeper_create_mnemonic",
     "smartcard_satochip_seedkeeper_tools", "智能卡创建", "卡内真随机",
     "用 SeedKeeper 随机数创建助记词。",
     "main/pages/krux_shell/krux_shell.c", KRUX_FEATURE_ACTION,
     KRUX_FEATURE_UI_READY, KRUX_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_seedkeeper_write_mnemonic",
     "smartcard_satochip_seedkeeper_tools", "写入卡", "当前助记词",
     "把当前助记词写入卡。",
     "main/pages/krux_shell/krux_shell.c", KRUX_FEATURE_ACTION,
     KRUX_FEATURE_UI_READY, KRUX_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_seedkeeper_view_mnemonic",
     "smartcard_satochip_seedkeeper_tools", "查看卡", "卡内助记词",
     "查看卡内助记词。",
     "main/pages/krux_shell/krux_shell.c", KRUX_FEATURE_ACTION,
     KRUX_FEATURE_UI_READY, KRUX_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_seedkeeper_load_mnemonic",
     "smartcard_satochip_seedkeeper_tools", "卡导入", "卡到内存",
     "把卡内助记词加载到开发板内存。",
     "main/pages/krux_shell/krux_shell.c", KRUX_FEATURE_ACTION,
     KRUX_FEATURE_UI_READY, KRUX_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_seedkeeper_save_password",
     "smartcard_satochip_seedkeeper_tools", "存密码", "密码条目",
     "保存密码到 SeedKeeper。",
     "main/pages/krux_shell/krux_shell.c", KRUX_FEATURE_ACTION,
     KRUX_FEATURE_UI_READY, KRUX_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_seedkeeper_load_descriptor",
     "smartcard_satochip_seedkeeper_tools", "读描述符", "从卡加载",
     "从 SeedKeeper 加载钱包描述符。",
     "main/pages/krux_shell/krux_shell.c", KRUX_FEATURE_ACTION,
     KRUX_FEATURE_UI_READY, KRUX_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_seedkeeper_save_descriptor",
     "smartcard_satochip_seedkeeper_tools", "存描述符", "写入卡",
     "保存钱包描述符到 SeedKeeper。",
     "main/pages/krux_shell/krux_shell.c", KRUX_FEATURE_ACTION,
     KRUX_FEATURE_UI_READY, KRUX_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_seedkeeper_clone",
     "smartcard_satochip_seedkeeper_tools", "克隆", "卡到卡",
     "克隆 SeedKeeper 条目。",
     "main/pages/krux_shell/krux_shell.c", KRUX_FEATURE_ACTION,
     KRUX_FEATURE_UI_READY, KRUX_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_seedkeeper_change_pin",
     "smartcard_satochip_seedkeeper_tools", "改PIN", "修改卡 PIN",
     "修改 SeedKeeper 卡 PIN。",
     "main/pages/krux_shell/krux_shell.c", KRUX_FEATURE_ACTION,
     KRUX_FEATURE_UI_READY, KRUX_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_seedkeeper_reset",
     "smartcard_satochip_seedkeeper_tools", "重置", "按 SID 删除",
     "重置或删除 SeedKeeper 条目。",
     "main/pages/krux_shell/krux_shell.c", KRUX_FEATURE_ACTION,
     KRUX_FEATURE_UI_READY, KRUX_FEATURE_RISK_SECRET_MATERIAL},
    {"new_seedkeeper_create_mnemonic",
     "new_mnemonic", "智能卡", "卡内真随机",
     "用 SeedKeeper 随机数创建助记词。",
     "main/pages/krux_shell/krux_shell.c", KRUX_FEATURE_ACTION,
     KRUX_FEATURE_UI_READY, KRUX_FEATURE_RISK_SECRET_MATERIAL},
    {"load_seedkeeper_mnemonic",
     "load_mnemonic", "智能卡", "从卡导入",
     "从 SeedKeeper 导入助记词。",
     "main/pages/krux_shell/krux_shell.c", KRUX_FEATURE_ACTION,
     KRUX_FEATURE_UI_READY, KRUX_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_satochip_certificate_tools", "smartcard_satochip_tools",
     "证书", "导出 / 导入", "个人证书导出和导入入口。",
     "main/pages/krux_shell/krux_shell.c", KRUX_FEATURE_GROUP,
     KRUX_FEATURE_SERVICE_STUB, KRUX_FEATURE_RISK_EXTERNAL_IO},
    {"smartcard_certificate_export", "smartcard_satochip_certificate_tools",
     "导出", "导出个人证书", "导出个人证书的 PEM 文本。",
     "main/pages/krux_shell/krux_shell.c", KRUX_FEATURE_ACTION,
     KRUX_FEATURE_UI_READY, KRUX_FEATURE_RISK_VIEW_ONLY},
    {"smartcard_certificate_import", "smartcard_satochip_certificate_tools",
     "导入", "导入个人证书", "导入个人证书 PEM 文本。",
     "main/pages/krux_shell/krux_shell.c", KRUX_FEATURE_ACTION,
     KRUX_FEATURE_UI_READY, KRUX_FEATURE_RISK_EXTERNAL_IO},
    {"smartcard_satochip_change_pin", "smartcard_satochip_maint", "改 PIN",
     "修改卡 PIN", "输入旧 PIN 和新 PIN。", "main/pages/krux_shell/krux_shell.c",
     KRUX_FEATURE_ACTION, KRUX_FEATURE_UI_READY,
     KRUX_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_satochip_unblock_pin", "smartcard_satochip_maint", "解锁",
     "PUK 解锁", "用 PUK 解锁被锁定的 PIN。",
     "main/pages/krux_shell/krux_shell.c", KRUX_FEATURE_ACTION,
     KRUX_FEATURE_UI_READY, KRUX_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_satochip_set_label", "smartcard_satochip_maint", "改标签",
     "修改卡标签", "写入卡片标签。", "main/pages/krux_shell/krux_shell.c",
     KRUX_FEATURE_ACTION, KRUX_FEATURE_UI_READY,
     KRUX_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_satochip_nfc_policy", "smartcard_satochip_maint", "NFC",
     "修改 NFC 策略", "写入 NFC 策略值。", "main/pages/krux_shell/krux_shell.c",
     KRUX_FEATURE_ACTION, KRUX_FEATURE_UI_READY,
     KRUX_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_satochip_feature_policy", "smartcard_satochip_maint",
     "功能", "修改功能策略", "写入 Schnorr / Nostr / Liquid 策略。",
     "main/pages/krux_shell/krux_shell.c", KRUX_FEATURE_ACTION,
     KRUX_FEATURE_UI_READY, KRUX_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_satochip_reset_seed", "smartcard_satochip_maint", "重置",
     "重置卡种子", "执行种子重置。", "main/pages/krux_shell/krux_shell.c",
     KRUX_FEATURE_ACTION, KRUX_FEATURE_UI_READY,
     KRUX_FEATURE_RISK_SECRET_MATERIAL},
    {"smartcard_satochip_reset_factory", "smartcard_satochip_maint", "出厂",
     "恢复出厂", "发送出厂复位信号。", "main/pages/krux_shell/krux_shell.c",
     KRUX_FEATURE_ACTION, KRUX_FEATURE_UI_READY,
     KRUX_FEATURE_RISK_DEVICE_CONTROL},
    {"smartcard_satochip_authenticity", "smartcard_satochip_maint", "真伪",
     "检查真伪", "读取个人证书和真伪状态。", "main/pages/krux_shell/krux_shell.c",
     KRUX_FEATURE_ACTION, KRUX_FEATURE_UI_READY, KRUX_FEATURE_RISK_VIEW_ONLY},
};

static const krux_menu_override_t KRUX_SATOCHIP_PUBKEY_MENU[] = {
    {"zpub", "satochip_btc_zpub"},
    {"ypub", "satochip_btc_ypub"},
    {"xpub", "satochip_btc_xpub"},
    {"vpub", "satochip_btc_vpub"},
    {"upub", "satochip_btc_upub"},
    {"tpub", "satochip_btc_tpub"},
};

static const krux_menu_override_t KRUX_BACKUP_MENU[] = {
    {"序号", "backup_seed_words"},
    {"原始熵", "backup_entropy"},
    {"二维码", "backup_seed_qr"},
    {"加密", "backup_kef"},
    {"点阵板", "backup_grid"},
    {"钢板", "backup_steel_punch"},
    {"1248", "backup_stackbit"},
};

static const krux_menu_override_t KRUX_ADDRESSES_MENU[] = {
    {"收款", "legacy_addresses"},
    {"找零", "legacy_addresses"},
    {"扫码", "legacy_addresses"},
    {"二维码", "legacy_addresses"},
};

static const krux_menu_override_t KRUX_SETTINGS_MENU[] = {
    {"PIN", "settings_pin"},
    {"亮度", "settings_display"},
    {"相机", "settings_camera"},
    {"钱包", "settings_wallet"},
    {"安全", "settings_security"},
    {"关于", "about"},
};

static const krux_menu_override_t KRUX_DEVICE_TESTS_MENU[] = {
    {"触摸", "test_screen_touch"},
    {"相机", "test_camera"},
    {"存储卡", "test_storage"},
    {"供电", "test_power"},
};

static const char *const PRODUCT_SCREEN_IDS[] = {
    "home",
    "pi_connect_wallet",
    "connect_okx",
    "connect_bitget",
    "connect_metamask",
    "connect_rabby",
    "connect_tokenpocket",
    "connect_address",
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
    "pi_mnemonic_tools",
    "smartcard_tools",
    "smartcard_satochip_tools",
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
    "smartcard_seedkeeper_logs_page",
    "smartcard_seedkeeper_advanced_tools",
    "smartcard_seedkeeper_create_mnemonic",
    "smartcard_seedkeeper_write_mnemonic",
    "smartcard_seedkeeper_view_mnemonic",
    "smartcard_seedkeeper_load_mnemonic",
    "smartcard_seedkeeper_save_password",
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
    "smartcard_certificate_export",
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
    "addresses",
    "addr_receive",
    "addr_change",
    "addr_scan_check",
    "addr_qr_view",
    "signing",
    "sign_okx",
    "sign_bitget",
    "sign_metamask",
    "sign_rabby",
    "sign_tokenpocket",
    "sign_btc",
    "sign_okx_mnemonic",
    "sign_okx_satochip",
    "sign_bitget_mnemonic",
    "sign_bitget_satochip",
    "sign_metamask_mnemonic",
    "sign_metamask_satochip",
    "sign_rabby_mnemonic",
    "sign_rabby_satochip",
    "sign_tokenpocket_mnemonic",
    "sign_tokenpocket_satochip",
    "sign_btc_mnemonic",
    "sign_btc_satochip",
    "smartcard_web3_scan",
    "smartcard_card_info",
    "satochip_path_address",
    "satochip_btc_zpub",
    "satochip_btc_xpub",
    "sign_psbt_qr",
    "tools_create_qr",
    "tools_qr_capture",
    "tools_file_manager",
    "settings_pin",
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

  return !krux_id_is_any(id, "connect_mnemonic", "connect_satochip") &&
         strcmp(id, "smartcard_seedkeeper") != 0 &&
         !krux_id_is_any(id, "smartcard_seedkeeper_status",
                         "smartcard_seedkeeper_list") &&
         strcmp(id, "smartcard_seedkeeper_logs") != 0 &&
         !krux_id_is_any(id, "seedkeeper_status", "seedkeeper_list") &&
         strcmp(id, "seedkeeper_logs") != 0 &&
         strcmp(id, "connect_web3") != 0 &&
         !krux_id_is_any(id, "sign_mnemonic", "sign_satochip") &&
         !krux_id_is_any(id, "web3", "web3_okx") &&
         !krux_id_is_any(id, "web3_bitget", "web3_metamask") &&
         !krux_id_is_any(id, "web3_rabby", "web3_tokenpocket") &&
         !krux_id_is_any(id, "web3_address", "web3_message_sign") &&
         strcmp(id, "web3_typed_data") != 0;
}

static const krux_feature_t *shell_local_feature_find(const char *id) {
  if (!id)
    return NULL;

  for (size_t i = 0; i < sizeof(KRUX_LOCAL_SMARTCARD_FEATURES) /
                           sizeof(KRUX_LOCAL_SMARTCARD_FEATURES[0]); i++) {
    if (strcmp(KRUX_LOCAL_SMARTCARD_FEATURES[i].id, id) == 0)
      return &KRUX_LOCAL_SMARTCARD_FEATURES[i];
  }
  return NULL;
}

static const krux_feature_t *shell_feature_find(const char *id) {
  const krux_feature_t *feature = krux_feature_find(id);
  if (feature)
    return feature;
  return shell_local_feature_find(id);
}

static int max_i(int a, int b) { return a > b ? a : b; }

static lv_color_t krux_canvas_color(void) { return lv_color_hex(0x000000); }
static lv_color_t krux_card_color(void) { return panel_color(); }
static lv_color_t krux_card_pressed_color(void) { return lv_color_hex(0x2A1A10); }
static lv_color_t krux_text_color(void) { return main_color(); }
static lv_color_t krux_muted_color(void) { return secondary_color(); }

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
  return shell_is_wave_43_portrait() ? 10 : 0;
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
             krux_smartcard_tools_group_id(id) ||
             krux_smartcard_seedkeeper_group_id(id) ||
             krux_smartcard_certificate_group_id(id)))
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

static lv_color_t status_color(krux_feature_status_t status) {
  switch (status) {
  case KRUX_FEATURE_VERIFIED:
    return yes_color();
  case KRUX_FEATURE_HARDWARE_WIRED:
    return cyan_color();
  case KRUX_FEATURE_SERVICE_STUB:
    return highlight_color();
  case KRUX_FEATURE_UI_READY:
    return main_color();
  case KRUX_FEATURE_NOT_STARTED:
  default:
    return secondary_color();
  }
}

static lv_color_t risk_color(krux_feature_risk_t risk) {
  switch (risk) {
  case KRUX_FEATURE_RISK_SIGNING:
  case KRUX_FEATURE_RISK_SECRET_MATERIAL:
    return error_color();
  case KRUX_FEATURE_RISK_EXTERNAL_IO:
  case KRUX_FEATURE_RISK_DEVICE_CONTROL:
    return cyan_color();
  case KRUX_FEATURE_RISK_VIEW_ONLY:
  default:
    return highlight_color();
  }
}

static bool use_two_column_cards(const krux_feature_t *feature) {
  return theme_get_screen_width() >= 420 &&
         krux_feature_child_count(feature->id) > 2;
}

static lv_obj_t *create_text(lv_obj_t *parent, const char *text, bool medium,
                             lv_color_t color) {
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, text ? text : "");
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
  lv_obj_t *panel = lv_obj_create(parent);
  lv_obj_set_width(panel, LV_PCT(100));
  lv_obj_set_height(panel, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(panel, panel_color(), 0);
  lv_obj_set_style_bg_opa(panel, LV_OPA_80, 0);
  lv_obj_set_style_border_color(panel, border_color, 0);
  lv_obj_set_style_border_width(panel, 1, 0);
  lv_obj_set_style_radius(panel, 18, 0);
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
  lv_obj_t *chip = lv_label_create(parent);
  lv_label_set_text(chip, text ? text : "");
  lv_label_set_long_mode(chip, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_font(chip, theme_font_small(), 0);
  lv_obj_set_style_text_color(chip, main_color(), 0);
  lv_obj_set_style_bg_color(chip, color, 0);
  lv_obj_set_style_bg_opa(chip, LV_OPA_40, 0);
  lv_obj_set_style_radius(chip, 10, 0);
  lv_obj_set_style_pad_left(chip, 8, 0);
  lv_obj_set_style_pad_right(chip, 8, 0);
  lv_obj_set_style_pad_top(chip, 4, 0);
  lv_obj_set_style_pad_bottom(chip, 4, 0);
  return chip;
}

static void legacy_wallet_return_to_krux_cb(void);
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
static void create_krux_header(lv_obj_t *root, const krux_feature_t *feature);
static void create_krux_child_menu(lv_obj_t *list,
                                   const krux_feature_t *feature);
static const krux_feature_t *shell_feature_find(const char *id);

#ifndef SIMULATOR
static bool wallet_ready_for_legacy_pages(void) {
  return key_is_loaded() && wallet_is_initialized();
}

static void legacy_wallet_intermediate_blocked_cb(void) {
  (void)krux_shell_show_screen("pi_loaded_mnemonic");
}

static bool legacy_wallet_require_signing_key(void) {
  if (key_has_signing_key() && wallet_is_initialized())
    return true;
  if (!key_is_loaded())
    legacy_wallet_launch_load();
  else
    dialog_show_error("临时助记词不能用于签名或派生地址。",
                      legacy_wallet_intermediate_blocked_cb, 0);
  return false;
}

static bool legacy_wallet_require_valid_mnemonic(void) {
  if (key_mnemonic_is_valid())
    return true;
  if (!key_is_loaded())
    legacy_wallet_launch_load();
  else
    dialog_show_error("临时助记词不能用于签名或派生地址。",
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

static void legacy_wallet_return_to_krux_cb(void) {
  (void)krux_shell_show_screen("home");
}

static void legacy_wallet_launch_login(void) {
  lv_obj_t *root = legacy_wallet_prepare_root();
  login_page_create(root);
  login_page_show();
}

static void legacy_wallet_launch_load(void) {
  lv_obj_t *root = legacy_wallet_prepare_root();
  load_menu_page_create(root, legacy_wallet_return_to_krux_cb);
  load_menu_page_show();
}

static void legacy_wallet_launch_new(void) {
  lv_obj_t *root = legacy_wallet_prepare_root();
  new_mnemonic_menu_page_create(root, legacy_wallet_return_to_krux_cb);
  new_mnemonic_menu_page_show();
}

static void shell_return_from_key_confirmation_cb(void) {
  key_confirmation_page_destroy();
  (void)krux_shell_show_screen("pi_mnemonic_tools");
}

static void shell_success_from_key_confirmation_cb(void) {
  key_confirmation_page_destroy();
  (void)krux_shell_show_screen("home");
}

static void shell_return_from_mnemonic_editor_cb(void) {
  mnemonic_editor_page_destroy();
  (void)krux_shell_show_screen("new_mnemonic");
}

static void shell_return_from_load_mnemonic_editor_cb(void) {
  mnemonic_editor_page_destroy();
  (void)krux_shell_show_screen("load_mnemonic");
}

static void shell_return_from_load_qr_cb(void) {
  qr_scanner_page_destroy();
  (void)krux_shell_show_screen("load_mnemonic");
}

static void shell_return_from_load_storage_cb(void) {
  load_storage_page_destroy();
  (void)krux_shell_show_screen("load_mnemonic");
}

static void shell_return_from_manual_input_cb(void) {
  manual_input_page_destroy();
  (void)krux_shell_show_screen("new_mnemonic");
}

static void shell_return_from_load_manual_input_cb(void) {
  manual_input_page_destroy();
  (void)krux_shell_show_screen("load_mnemonic");
}

static void shell_success_from_manual_input_cb(void) {
  key_confirmation_page_destroy();
  manual_input_page_destroy();
  (void)krux_shell_show_screen("home");
}

static void shell_show_generated_mnemonic(char *mnemonic) {
  if (!mnemonic) {
    (void)krux_shell_show_screen("new_mnemonic");
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
    (void)krux_shell_show_screen("load_mnemonic");
  }
}

static void shell_return_from_bip39_check_cb(void) {
  bip39_check_page_destroy();
  (void)krux_shell_show_screen("pi_mnemonic_tools");
}

static void shell_return_from_punch_grid_cb(void) {
  load_punch_grid_page_destroy();
  (void)krux_shell_show_screen("load_mnemonic");
}

static void shell_return_from_tinyseed_restore_cb(void) {
  tinyseed_restore_page_destroy();
  (void)krux_shell_show_screen("load_punch_grid");
}

static void shell_return_from_stackbit_restore_cb(void) {
  stackbit_restore_page_destroy();
  (void)krux_shell_show_screen("load_punch_grid");
}

static void shell_return_from_custom_derivation_cb(void) {
  custom_derivation_page_destroy();
  (void)krux_shell_show_screen("pi_connect_wallet");
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
  (void)krux_shell_show_screen("backup_export");
}

static void shell_return_from_backup_entropy_cb(void) {
  mnemonic_entropy_page_destroy();
  (void)krux_shell_show_screen("backup_export");
}

static void shell_return_from_backup_grid_cb(void) {
  mnemonic_grid_page_destroy();
  (void)krux_shell_show_screen("backup_export");
}

static void shell_return_from_backup_steel_cb(void) {
  mnemonic_steel_page_destroy();
  (void)krux_shell_show_screen("backup_export");
}

static void shell_return_from_backup_1248_cb(void) {
  mnemonic_1248_page_destroy();
  (void)krux_shell_show_screen("backup_export");
}

static void shell_return_from_backup_qr_cb(void) {
  mnemonic_qr_page_destroy();
  (void)krux_shell_show_screen("backup_export");
}

static void shell_return_from_backup_kef_cb(void) {
  store_mnemonic_page_destroy();
  (void)krux_shell_show_screen("backup_export");
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
  scan_page_create(root, legacy_wallet_return_to_krux_cb);
  scan_page_show();
}

static void legacy_wallet_return_from_slots_cb(void) {
  mnemonic_slots_page_destroy();
  (void)krux_shell_show_screen("pi_mnemonic_tools");
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
  (void)krux_shell_show_screen("settings");
}

static void legacy_system_launch_pin_settings(void) {
  lv_obj_t *root = legacy_wallet_prepare_root();
  login_settings_page_create(root, legacy_system_return_from_pin_settings_cb);
  login_settings_page_show();
}

static void legacy_wallet_passphrase_done_cb(void *user_data) {
  (void)user_data;
  (void)krux_shell_show_screen("pi_mnemonic_tools");
}

static void legacy_wallet_passphrase_error_cb(void) {
  legacy_wallet_passphrase_done_cb(NULL);
}

static void legacy_wallet_return_from_passphrase_cb(void) {
  passphrase_page_destroy();
  (void)krux_shell_show_screen("pi_mnemonic_tools");
}

static void legacy_wallet_passphrase_success_cb(const char *passphrase) {
  char *mnemonic = NULL;
  if (!key_get_mnemonic(&mnemonic)) {
    passphrase_page_destroy();
    dialog_show_error("请先导入助记词", legacy_wallet_passphrase_error_cb, 0);
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
    dialog_show_error("密码短语应用失败", legacy_wallet_passphrase_error_cb, 0);
    return;
  }

  (void)mnemonic_slots_add_current(NULL);
  dialog_show_info("密码短语已应用", "钱包指纹已更新",
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
static void legacy_wallet_return_to_krux_cb(void) {
  (void)krux_shell_show_screen("home");
}

static void legacy_wallet_return_to_home_cb(void) {
  (void)krux_shell_show_screen("wallet_home");
}

static void legacy_wallet_launch_home(void) {
  (void)krux_shell_show_screen("wallet_home");
}

static void legacy_wallet_launch_login(void) {
  (void)krux_shell_show_screen("wallet_home");
}

static void legacy_wallet_launch_load(void) {
  (void)krux_shell_show_screen("load_mnemonic");
}

static void legacy_wallet_launch_new(void) {
  (void)krux_shell_show_screen("new_mnemonic");
}

static void legacy_wallet_launch_public_key(void) {
  (void)krux_shell_show_screen("btc_bluewallet_zpub");
}

static void legacy_wallet_launch_addresses(void) {
  (void)krux_shell_show_screen("addresses");
}

static void legacy_wallet_launch_backup(void) {
  (void)krux_shell_show_screen("backup_export");
}

static void legacy_wallet_launch_scan(void) {
  (void)krux_shell_show_screen("sign_psbt_qr");
}

static void legacy_wallet_launch_scan_for_shell(void) {
  (void)krux_shell_show_screen("sign_psbt_qr");
}

static void legacy_wallet_launch_slots(void) {
  (void)krux_shell_show_screen("wallet_home");
}

static void legacy_wallet_launch_settings(void) {
  (void)krux_shell_show_screen("settings_wallet");
}

static void legacy_system_launch_pin_settings(void) {
  (void)krux_shell_show_screen("settings_pin");
}

static void legacy_wallet_launch_descriptor(void) {
  (void)krux_shell_show_screen("wallet_descriptor");
}

static void legacy_wallet_launch_logout(void) {
  (void)krux_shell_show_screen("boot_login");
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
                strcmp(id, "web3_address_mnemonic") == 0 ||
                strcmp(id, "btc_mnemonic") == 0 ||
                strcmp(id, "btc_bluewallet_zpub") == 0 ||
                strcmp(id, "btc_bluewallet_xpub") == 0 ||
                krux_sign_mnemonic_target_id(id));
}

static void legacy_wallet_resume_pending_mnemonic_screen(void) {
  char target[sizeof(s_pending_mnemonic_screen)];
  snprintf(target, sizeof(target), "%s", s_pending_mnemonic_screen);
  s_pending_mnemonic_screen[0] = '\0';
  mnemonic_slots_page_destroy();
  if (target[0] == '\0')
    return;

  s_allow_sensitive_render = true;
  (void)krux_shell_show_screen(target);
  s_allow_sensitive_render = false;
}

static void legacy_wallet_cancel_direct_sensitive(void) {
  s_pending_backup_launch = NULL;
  s_pending_sensitive_screen = NULL;
  s_pending_mnemonic_screen[0] = '\0';
  s_pending_public_key_mode = PUBLIC_KEY_EXPORT_STANDARD;
  (void)krux_shell_show_screen(s_current_screen_id ? s_current_screen_id
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
  lv_obj_t *root = legacy_wallet_prepare_root();
  custom_derivation_page_create(root, shell_return_from_custom_derivation_cb);
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
    (void)krux_shell_show_screen(target);
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
    if (!legacy_wallet_require_signing_key())
      return true;
    sensitive_pin_require(legacy_wallet_launch_custom_derivation_unlocked,
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
  if (krux_sign_mnemonic_target_id(target_id)) {
    legacy_wallet_launch_unified_scan_for_shell();
    return true;
  }
  if (krux_sign_satochip_target_id(target_id)) {
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
    return krux_shell_show_screen("bip85_mnemonic");
  if (strcmp(target_id, "load_camera") == 0 ||
      strcmp(target_id, "load_manual") == 0 ||
      strcmp(target_id, "load_digits") == 0 ||
      strcmp(target_id, "load_sd") == 0)
    return krux_shell_show_screen(target_id);
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
      krux_sign_mnemonic_target_id(target_id) ||
      krux_sign_satochip_target_id(target_id) ||
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
    return krux_shell_show_screen(target_id);
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
    return "打开钱包功能";
  if (strcmp(route, "legacy_login") == 0)
    return "打开钱包";
  if (strcmp(route, "legacy_load_wallet") == 0)
    return "导入钱包";
  if (strcmp(route, "legacy_new_wallet") == 0)
    return "打开创建钱包";
  if (strcmp(route, "legacy_wallet_home") == 0)
    return "进入钱包首页";
  if (strcmp(route, "legacy_public_key") == 0)
    return "查看公钥";
  if (strcmp(route, "legacy_descriptor") == 0)
    return "管理钱包描述符";
  if (strcmp(route, "legacy_addresses") == 0)
    return "查看和核对地址";
  if (strcmp(route, "legacy_backup_wallet") == 0)
    return "备份助记词";
  if (strcmp(route, "legacy_scan_sign") == 0)
    return "扫码签名";
  if (strcmp(route, "legacy_select_mnemonic") == 0)
    return "选择助记词";
  if (strcmp(route, "legacy_wallet_settings") == 0)
    return "打开设置";
  if (strcmp(route, "legacy_pin_settings") == 0)
    return "打开 PIN 码设置";
  if (strcmp(route, "legacy_logout") == 0)
    return "清除当前会话";
  return "打开钱包功能";
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
  (void)krux_shell_show_screen(s_current_screen_id ? s_current_screen_id
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

static void create_hero(lv_obj_t *root, const krux_feature_t *feature) {
  lv_obj_t *hero =
      create_panel(root,
                   feature->parent_id ? disabled_color() : highlight_color(),
                   max_i(14, theme_get_default_padding() / 2));

  lv_obj_t *top = create_row(hero);
  lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  char progress_text[80];
  if (!feature->parent_id) {
    snprintf(progress_text, sizeof(progress_text), "功能 %zu 项",
             krux_feature_action_count());
  } else {
    snprintf(progress_text, sizeof(progress_text), "子项 %zu 项",
             krux_feature_child_count(feature->id));
  }
  create_chip(top, progress_text, disabled_color());

  lv_obj_t *title = create_text(hero, feature->title, true, main_color());
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_LEFT, 0);

  lv_obj_t *chips = create_row(hero);
  create_chip(chips, krux_feature_status_name(feature->status),
              status_color(feature->status));
  create_chip(chips, krux_feature_risk_name(feature->risk),
              risk_color(feature->risk));
}

static void nav_event_cb(lv_event_t *event) {
  const char *target_id = (const char *)lv_event_get_user_data(event);
  const char *alias_target = shell_alias_target_for_id(target_id);
  if (alias_target)
    target_id = alias_target;
  if (satochip_legacy_seedkeeper_id(target_id)) {
    (void)krux_shell_show_screen("smartcard_satochip_seedkeeper_tools");
    return;
  }
#ifndef SIMULATOR
  if (target_requires_signing_key(target_id) &&
      !legacy_wallet_require_signing_key())
    return;
#endif
  if (legacy_wallet_handle_target(target_id))
    return;
  (void)krux_shell_show_screen(target_id);
}

static lv_obj_t *create_menu_card(lv_obj_t *parent,
                                  const krux_feature_t *feature, bool primary,
                                  bool two_columns) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_width(btn, two_columns ? LV_PCT(48) : LV_PCT(100));
  lv_obj_set_height(btn, max_i(two_columns ? 102 : 86,
                               theme_get_min_touch_size()));
  theme_apply_touch_button(btn, primary);
  lv_obj_set_style_bg_color(btn, panel_color(), 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_80, 0);
  lv_obj_set_style_pad_all(btn, 12, 0);
  lv_obj_set_style_pad_gap(btn, 7, 0);
  lv_obj_set_style_radius(btn, 16, 0);
  lv_obj_set_style_border_width(btn, 1, 0);
  lv_obj_set_style_border_color(btn,
                                primary ? highlight_color()
                                        : disabled_color(),
                                0);
  lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);
  lv_obj_add_event_cb(btn, nav_event_cb, LV_EVENT_CLICKED,
                      (void *)feature->id);

  lv_obj_t *title_row = create_row(btn);

  lv_obj_t *btn_label = lv_label_create(title_row);
  lv_label_set_text(btn_label, feature->title);
  lv_obj_set_width(btn_label, LV_PCT(58));
  lv_label_set_long_mode(btn_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(btn_label, theme_font_medium(), 0);
  lv_obj_set_style_text_color(btn_label,
                              primary ? highlight_color() : main_color(), 0);

  create_chip(title_row, krux_feature_status_name(feature->status),
              status_color(feature->status));
  return btn;
}

static lv_obj_t *create_nav_button(lv_obj_t *parent, const char *label,
                                   const char *target_id, bool primary) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_width(btn, LV_PCT(100));
  lv_obj_set_height(btn, max_i(58, theme_get_min_touch_size() * 2 / 3));
  theme_apply_touch_button(btn, primary);
  lv_obj_set_style_bg_color(btn, panel_color(), 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_80, 0);
  lv_obj_set_style_radius(btn, 16, 0);
  lv_obj_set_style_border_width(btn, 1, 0);
  lv_obj_set_style_border_color(btn,
                                primary ? highlight_color()
                                        : disabled_color(),
                                0);
  if (target_id && target_id[0]) {
    lv_obj_add_event_cb(btn, nav_event_cb, LV_EVENT_CLICKED, (void *)target_id);
  } else {
    lv_obj_add_state(btn, LV_STATE_DISABLED);
  }

  lv_obj_t *btn_label = lv_label_create(btn);
  lv_label_set_text(btn_label, label);
  lv_obj_set_style_text_font(btn_label, theme_font_medium(), 0);
  lv_obj_set_style_text_color(btn_label,
                              primary ? highlight_color() : main_color(), 0);
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
  lv_obj_set_style_bg_color(btn, panel_color(), 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_80, 0);
  lv_obj_set_style_radius(btn, 16, 0);
  lv_obj_set_style_border_width(btn, 1, 0);
  lv_obj_set_style_border_color(btn,
                                primary ? highlight_color()
                                        : disabled_color(),
                                0);
  if (cb) {
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
  } else {
    lv_obj_add_state(btn, LV_STATE_DISABLED);
  }

  lv_obj_t *btn_label = lv_label_create(btn);
  lv_label_set_text(btn_label, label);
  lv_obj_set_style_text_font(btn_label, theme_font_medium(), 0);
  lv_obj_set_style_text_color(btn_label,
                              primary ? highlight_color() : main_color(), 0);
  lv_obj_center(btn_label);
  return btn;
}

static void style_slider(lv_obj_t *slider) {
  lv_obj_set_width(slider, LV_PCT(100));
  lv_obj_set_height(slider, 14);
  lv_obj_set_style_bg_color(slider, panel_color(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
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
    snprintf(text, sizeof(text), "亮度：%ld%%", (long)val);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, yes_color(), 0);
  }
}

static void create_display_settings_block(lv_obj_t *parent) {
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(12, theme_get_small_padding()));
  create_text(panel, "亮度调节", false, highlight_color());

  uint8_t cur = settings_get_brightness();
  if (cur < 10)
    cur = 10;

  char status[128];
  snprintf(status, sizeof(status), "当前亮度：%u%%", (unsigned)cur);
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
    snprintf(text, sizeof(text), "曝光目标：%ld", (long)val);
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
    snprintf(text, sizeof(text), "焦点位置：%ld", (long)val);
    lv_label_set_text(label, text);
  }
}

static void create_camera_settings_block(lv_obj_t *parent) {
  lv_obj_t *panel =
      create_panel(parent, cyan_color(), max_i(12, theme_get_small_padding()));
  create_text(panel, "扫码参数", false, highlight_color());

  char line[160];
  snprintf(line, sizeof(line), "曝光目标：%u", (unsigned)settings_get_ae_target());
  lv_obj_t *ae_label = create_text(panel, line, false, main_color());

  lv_obj_t *ae_slider = lv_slider_create(panel);
  lv_slider_set_range(ae_slider, AE_TARGET_MIN, AE_TARGET_MAX);
  lv_slider_set_value(ae_slider, settings_get_ae_target(), LV_ANIM_OFF);
  style_slider(ae_slider);
  lv_obj_add_event_cb(ae_slider, ae_slider_event_cb, LV_EVENT_VALUE_CHANGED,
                      ae_label);

  snprintf(line, sizeof(line), "焦点位置：%u",
           (unsigned)settings_get_focus_position());
  lv_obj_t *focus_label = create_text(panel, line, false, main_color());

  lv_obj_t *focus_slider = lv_slider_create(panel);
  lv_slider_set_range(focus_slider, 0, FOCUS_POSITION_MAX);
  lv_slider_set_value(focus_slider, settings_get_focus_position(), LV_ANIM_OFF);
  style_slider(focus_slider);
  lv_obj_add_event_cb(focus_slider, focus_slider_event_cb,
                      LV_EVENT_VALUE_CHANGED, focus_label);

}

static void refresh_file_list_label(lv_obj_t *label) {
  if (!label)
    return;

  lv_label_set_text(label, "正在挂载并读取存储卡根目录...");
  lv_refr_now(NULL);

  static char text[4096];
  esp_err_t ret = krux_storage_browser_format_root(text, sizeof(text));
  if (ret != ESP_OK) {
    if (text[0]) {
      lv_label_set_text(label, text);
    } else {
      lv_label_set_text_fmt(label, "文件浏览失败：%s", esp_err_to_name(ret));
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
  create_text(panel, "存储卡文件浏览", false, highlight_color());

  lv_obj_t *result = create_text(panel, "等待刷新", false, main_color());
  create_action_button(panel, "刷新文件列表", file_list_event_cb, result, true);
}

static void create_delivery_status_block(lv_obj_t *parent) {
  lv_obj_t *panel =
      create_panel(parent, yes_color(), max_i(12, theme_get_small_padding()));
  create_text(panel, "钱包状态", false, highlight_color());
  create_text(panel, "离线", false, main_color());
}

static void create_build_identity_block(lv_obj_t *parent) {
  lv_obj_t *panel =
      create_panel(parent, cyan_color(), max_i(12, theme_get_small_padding()));
  create_text(panel, "版本与构建", false, highlight_color());

  const esp_app_desc_t *desc = esp_app_get_description();
  char line[384];
  snprintf(line, sizeof(line),
           "项目：%s\n版本：%s\nIDF：%s\n构建：%s %s\n目标板：wave_43 / 480x800\n定位：离线钱包",
           desc ? desc->project_name : "kern",
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
  create_text(panel, "安全", false, error_color());

  uint16_t timeout = shell_pin_session_timeout();
  uint16_t poweroff_timeout = shell_pin_poweroff_timeout();
  char timeout_text[32];
  if (timeout == 0) {
    snprintf(timeout_text, sizeof(timeout_text), "关闭");
  } else if (timeout % 60 == 0) {
    snprintf(timeout_text, sizeof(timeout_text), "%u 分钟",
             (unsigned)(timeout / 60));
  } else {
    snprintf(timeout_text, sizeof(timeout_text), "%u 秒", (unsigned)timeout);
  }
  char poweroff_text[32];
  if (poweroff_timeout % 60 == 0) {
    snprintf(poweroff_text, sizeof(poweroff_text), "%u 分钟",
             (unsigned)(poweroff_timeout / 60));
  } else {
    snprintf(poweroff_text, sizeof(poweroff_text), "%u 秒",
             (unsigned)poweroff_timeout);
  }

  char line[384];
  snprintf(line, sizeof(line),
           "开机 PIN 码：%s\n"
           "自动锁定：%s\n"
           "自动清除会话：%s\n"
           "PIN 错误保护：%u 次\n"
           "硬件密钥：%s",
           shell_pin_is_configured() ? "已启用" : "未设置",
           timeout_text, poweroff_text, (unsigned)shell_pin_max_failures(),
           shell_pin_has_anti_phishing() ? "已启用" : "未启用");
  create_text(panel, line, false, main_color());
}

static void create_wallet_entry_block(lv_obj_t *parent,
                                      const krux_feature_t *feature) {
  const char *route = legacy_wallet_route_for_target(feature ? feature->id : NULL);
  lv_obj_t *panel =
      create_panel(parent, route ? yes_color() : error_color(),
                   max_i(12, theme_get_small_padding()));

  if (route) {
    create_text(panel, "钱包入口", false, yes_color());
    create_nav_button(panel, legacy_wallet_route_label(route), route, true);
    return;
  }

  create_text(panel, "功能保护", false, error_color());
}

static bool is_web3_wallet_choice(const char *id) {
  return id && (strcmp(id, "web3_okx") == 0 ||
                strcmp(id, "web3_bitget") == 0 ||
                strcmp(id, "web3_metamask") == 0 ||
                strcmp(id, "web3_rabby") == 0 ||
                strcmp(id, "web3_tokenpocket") == 0 ||
                strcmp(id, "web3_address") == 0 ||
                strcmp(id, "web3_okx_mnemonic") == 0 ||
                strcmp(id, "web3_bitget_mnemonic") == 0 ||
                strcmp(id, "web3_metamask_mnemonic") == 0 ||
                strcmp(id, "web3_rabby_mnemonic") == 0 ||
                strcmp(id, "web3_tokenpocket_mnemonic") == 0 ||
                strcmp(id, "web3_address_mnemonic") == 0);
}

static bool is_web3_satochip_choice(const char *id) {
  return id && (strcmp(id, "web3_okx_satochip") == 0 ||
                strcmp(id, "web3_bitget_satochip") == 0 ||
                strcmp(id, "web3_metamask_satochip") == 0 ||
                strcmp(id, "web3_rabby_satochip") == 0 ||
                strcmp(id, "web3_tokenpocket_satochip") == 0 ||
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
                strcmp(id, "connect_address") == 0);
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
                strcmp(id, "legacy_addresses") == 0 ||
                strcmp(id, "custom_derivation") == 0);
}

static bool target_requires_shell_gate(const char *id) {
  if (!id)
    return false;
  return strcmp(id, "sign_mnemonic") == 0 ||
         krux_sign_mnemonic_target_id(id) ||
         krux_sign_satochip_target_id(id) ||
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
  if (s_web3_qr_page_index >= s_web3_qr_bundle.page_count ||
      !s_web3_qr_bundle.pages[s_web3_qr_page_index])
    return;
  web3_qr_timer_stop();
  if (!qr_viewer_page_create(lv_screen_active(),
                             s_web3_qr_bundle.pages[s_web3_qr_page_index],
                             "连接钱包", web3_qr_return_cb)) {
    dialog_show_error("连接码显示失败", NULL, 2000);
    return;
  }
  qr_viewer_page_show();
}
#endif

static evm_web3_profile_t web3_profile_for_feature(const char *id) {
  if (!id)
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
  return EVM_WEB3_PROFILE_ADDRESS;
}

static const char *web3_qr_kind(evm_web3_profile_t profile) {
  switch (profile) {
  case EVM_WEB3_PROFILE_OKX:
    return "多账户连接码";
  case EVM_WEB3_PROFILE_BITGET:
    return "多账户连接码";
  case EVM_WEB3_PROFILE_METAMASK:
  case EVM_WEB3_PROFILE_RABBY:
  case EVM_WEB3_PROFILE_TOKENPOCKET:
    return "账户连接码";
  case EVM_WEB3_PROFILE_ADDRESS:
  default:
    return "普通地址";
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
  snprintf(page_text, sizeof(page_text), "第 %u/%u 片",
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
                                        const krux_feature_t *feature) {
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? feature->title : "以太坊钱包", true,
              main_color());

  evm_web3_profile_t profile =
      web3_profile_for_feature(feature ? feature->id : NULL);

#ifndef SIMULATOR
  if (!key_has_signing_key() || !wallet_is_initialized()) {
    if (key_is_loaded())
      create_text(panel, "临时助记词不能用于签名或派生地址。",
                  false, error_color());
    else
      create_text(panel, "请先导入或创建助记词。",
                  false, main_color());
    create_nav_button(panel, "导入", "legacy_load_wallet", true);
    return;
  }
#endif

#ifndef SIMULATOR
  if (!evm_web3_build_connect_qr(profile, &s_web3_qr_bundle)) {
    create_text(panel, "连接码生成失败，请重试。",
                false, error_color());
    create_nav_button(panel, "重新导入", "legacy_load_wallet", true);
    return;
  }
#else
  memset(&s_web3_qr_bundle, 0, sizeof(s_web3_qr_bundle));
  snprintf(s_web3_qr_bundle.address, sizeof(s_web3_qr_bundle.address),
           "0x0000000000000000000000000000000000000000");
  snprintf(s_web3_qr_bundle.summary, sizeof(s_web3_qr_bundle.summary),
           "%s 连接码", web3_qr_kind(profile));
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
    s_web3_qr_timer = lv_timer_create(web3_auto_page_timer_cb, 1200, NULL);
    create_action_button(panel, "下一片", web3_next_page_event_cb, NULL, true);
  }
#ifndef SIMULATOR
  create_action_button(panel, "显示大码", web3_qr_event_cb, NULL, true);
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
    s_web3_qr_timer = lv_timer_create(web3_auto_page_timer_cb, 1200, NULL);
    create_action_button(panel, "下一片", web3_next_page_event_cb, NULL, true);
  }
#ifndef SIMULATOR
  create_action_button(panel, "显示大码", web3_qr_event_cb, NULL, true);
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
  (void)krux_shell_show_screen(s_satochip_pending_return_id[0]
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
  ESP_LOGD(TAG, "SATOCHIP_CONNECT begin");
  s_satochip_task_err = smartcard_satochip_get_web3_account(
      s_satochip_task_pin, &s_satochip_task_card_account, 20000);
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
    snprintf(msg, sizeof(msg), "智能卡连接失败：%s\n%s",
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
    dialog_show_error("智能卡连接码生成失败", NULL, 0);
    secure_memzero(&s_satochip_task_card_account,
                   sizeof(s_satochip_task_card_account));
    secure_memzero(&external_account, sizeof(external_account));
    return;
  }

  lv_obj_clean(s_parent);
  theme_apply_screen(s_parent);
  lv_obj_t *root = theme_create_page_container(s_parent);
  lv_obj_set_style_bg_color(root, krux_canvas_color(), 0);
  lv_obj_set_style_pad_top(root, shell_margin_v(), 0);
  lv_obj_set_style_pad_bottom(root, shell_margin_v(), 0);
  lv_obj_set_style_pad_left(root, shell_margin_h(), 0);
  lv_obj_set_style_pad_right(root, shell_margin_h(), 0);
  lv_obj_set_style_pad_gap(root, shell_root_gap(false), 0);
  lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  const krux_feature_t *feature = krux_feature_find(s_current_screen_id);
  if (feature)
    create_krux_header(root, feature);

  lv_obj_t *panel =
      create_panel(root, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, "智能卡连接码", true, main_color());
  create_satochip_qr_block(panel, s_satochip_pending_profile,
                           &s_web3_qr_bundle);
  create_nav_button(panel, "返回",
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
static char s_card_info_result[2048];

static void card_info_finish_ui(void);
static void card_info_poll_cb(lv_timer_t *timer);

static void card_info_task(void *arg) {
  (void)arg;
  const bool delete_with_caps = s_card_info_task_with_caps;
  ESP_LOGD(TAG, "CARD_INFO begin");

  s_card_info_task_err = ESP_OK;
  s_card_info_result[0] = '\0';

  char ccid_text[1024];
  char label_text[512];
  smartcard_satochip_label_t label;
  memset(&label, 0, sizeof(label));

  esp_err_t probe_err = smartcard_ccid_probe(12000);
  smartcard_ccid_format_report(ccid_text, sizeof(ccid_text));

  if (probe_err == ESP_OK) {
    esp_err_t label_err = smartcard_satochip_get_label(&label, 12000);
    smartcard_satochip_format_label(&label, label_text, sizeof(label_text));

    if (label_err != ESP_OK)
      probe_err = label_err;

    snprintf(s_card_info_result, sizeof(s_card_info_result),
             "读卡器\n%s\n\n卡标签\n%s", ccid_text, label_text);
  } else {
    snprintf(s_card_info_result, sizeof(s_card_info_result),
             "读卡器总览\n%s\n\n后续读取已跳过：读卡器或卡片未准备好。",
             ccid_text);
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
    dialog_show_error("智能卡忙", NULL, 1600);
    return;
  }

  s_card_info_result[0] = '\0';
  __atomic_store_n(&s_card_info_task_done, false, __ATOMIC_RELEASE);
  s_card_info_task_err = ESP_ERR_INVALID_STATE;
  s_card_info_task_with_caps = false;

  s_card_info_progress_dialog =
      dialog_show_progress("读取卡片信息", "读取中",
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
    dialog_show_error("卡片信息任务启动失败", NULL, 0);
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
  (void)krux_shell_show_screen(s_current_screen_id);
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
  create_text(panel, "卡片信息", false, highlight_color());

  if (s_card_info_result[0] != '\0') {
    create_text(panel,
                s_card_info_task_err == ESP_OK ? "已读取" : "读取失败",
                false, s_card_info_task_err == ESP_OK ? yes_color()
                                                     : error_color());
    create_text(panel, s_card_info_result, false, main_color());
    create_action_button(panel, "重新读取", card_info_start_event_cb, NULL,
                         true);
  } else {
    create_text(panel, "等待", false, main_color());
    create_action_button(panel, "开始读取", card_info_start_event_cb, NULL,
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
    return "BTC 原生隔离见证";
  if (purpose == 86)
    return "BTC Taproot";
  return "BTC 传统地址";
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
               "路径：%s\n类型：%s\n%s\n\n%s", xpub.path, xpub.xtype,
               xpub.xpub,
               xpub.has_descriptor ? xpub.descriptor : "描述符：未生成");
    } else {
      snprintf(s_satochip_tool_result, sizeof(s_satochip_tool_result),
               "%s\n错误：%s", xpub.detail,
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
               "路径格式无效：%s", s_satochip_tool_path);
    } else if (is_evm) {
      smartcard_satochip_account_t account;
      memset(&account, 0, sizeof(account));
      s_satochip_tool_task_err = smartcard_satochip_get_eth_account(
          s_satochip_tool_pin, s_satochip_tool_path, &account, 20000);
      if (s_satochip_tool_task_err == ESP_OK && account.has_address) {
        snprintf(s_satochip_tool_title, sizeof(s_satochip_tool_title),
                 "Satochip EVM 地址");
        snprintf(s_satochip_tool_qr_payload,
                 sizeof(s_satochip_tool_qr_payload), "%s", account.address);
        snprintf(s_satochip_tool_result, sizeof(s_satochip_tool_result),
                 "路径：%s\n类型：EVM\n地址：%s", account.path,
                 account.address);
      } else {
        snprintf(s_satochip_tool_result, sizeof(s_satochip_tool_result),
                 "%s\n错误：%s", account.detail,
                 esp_err_to_name(s_satochip_tool_task_err));
      }
      secure_memzero(&account, sizeof(account));
    } else {
      if (!satochip_btc_address_path_allowed(s_satochip_tool_path, purpose,
                                             coin)) {
        s_satochip_tool_task_err = ESP_ERR_INVALID_ARG;
        snprintf(s_satochip_tool_result, sizeof(s_satochip_tool_result),
                 "BTC 地址路径必须是完整地址路径，例如：\n"
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
                   "Satochip BTC 地址");
          snprintf(s_satochip_tool_qr_payload,
                   sizeof(s_satochip_tool_qr_payload), "%s", address.address);
          snprintf(s_satochip_tool_result, sizeof(s_satochip_tool_result),
                   "路径：%s\n类型：%s\n网络：%s\n地址：%s", address.path,
                   satochip_script_name(purpose),
                   coin == 1 ? "测试网" : "主网", address.address);
        } else {
          snprintf(s_satochip_tool_result, sizeof(s_satochip_tool_result),
                   "%s\n错误：%s", address.detail,
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
  lv_obj_set_style_bg_color(root, krux_canvas_color(), 0);
  lv_obj_set_style_pad_top(root, shell_margin_v(), 0);
  lv_obj_set_style_pad_bottom(root, shell_margin_v(), 0);
  lv_obj_set_style_pad_left(root, shell_margin_h(), 0);
  lv_obj_set_style_pad_right(root, shell_margin_h(), 0);
  lv_obj_set_style_pad_gap(root, shell_root_gap(false), 0);
  lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  const krux_feature_t *feature = krux_feature_find(s_current_screen_id);
  if (feature)
    create_krux_header(root, feature);

  lv_obj_t *panel =
      create_panel(root,
                   s_satochip_tool_task_err == ESP_OK ? yes_color()
                                                      : error_color(),
                   max_i(14, theme_get_small_padding()));
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
  create_nav_button(panel, "返回", return_id, true);
  create_nav_button(panel, "回到首页", "home", false);
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
    dialog_show_error("请输入智能卡 PIN", NULL, 1600);
    return;
  }
  if (smartcard_task_busy()) {
    dialog_show_error("智能卡忙", NULL, 1600);
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
      dialog_show_error("请输入派生路径", NULL, 1600);
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
      dialog_show_progress("读取智能卡", "读取中",
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
    dialog_show_error("智能卡任务启动失败", NULL, 0);
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
    dialog_show_error("请输入智能卡 PIN", NULL, 1600);
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
    dialog_show_error("智能卡忙", NULL, 1600);
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
      dialog_show_progress("读取智能卡", "读取中",
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
    dialog_show_error("智能卡任务启动失败", NULL, 0);
    return;
  }

  s_satochip_poll_timer = lv_timer_create(satochip_connect_poll_cb, 100, NULL);
}

static void create_satochip_connect_block(lv_obj_t *parent,
                                          const krux_feature_t *feature) {
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? feature->title : "智能卡", true, main_color());

  snprintf(s_satochip_pending_return_id, sizeof(s_satochip_pending_return_id),
           "%s", feature && feature->parent_id ? feature->parent_id
                                                : "pi_connect_wallet");
  s_satochip_pending_profile =
      web3_profile_for_feature(feature ? feature->id : NULL);
  s_satochip_pin_flow = SATOCHIP_PIN_FLOW_CONNECT;
  s_satochip_tool_mode = SATOCHIP_TOOL_NONE;

  satochip_pin_input_cleanup();
  ui_text_input_create(&s_satochip_pin_input, panel, "智能卡 PIN", true,
                       satochip_pin_ready_cb);
  s_satochip_pin_input_active = true;
  if (s_satochip_pin_input.keyboard)
    lv_obj_add_event_cb(s_satochip_pin_input.keyboard, satochip_pin_cancel_cb,
                        LV_EVENT_CANCEL, NULL);
  if (s_satochip_pin_input.textarea)
    lv_obj_add_event_cb(s_satochip_pin_input.textarea, satochip_pin_cancel_cb,
                        LV_EVENT_CANCEL, NULL);
  create_action_button(panel, "返回", satochip_pin_back_cb, NULL, false);
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
                                       const krux_feature_t *feature,
                                       satochip_tool_mode_t mode) {
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? feature->title : "Satochip", true,
              main_color());

  s_satochip_pin_flow = SATOCHIP_PIN_FLOW_TOOL;
  s_satochip_tool_mode = mode;
  snprintf(s_satochip_pending_return_id, sizeof(s_satochip_pending_return_id),
           "%s", feature && feature->parent_id ? feature->parent_id
                                                : "smartcard_tools");
  satochip_pin_input_cleanup();

  if (mode == SATOCHIP_TOOL_PATH_ADDRESS) {
    create_text(panel, "路径", false, highlight_color());
    s_satochip_path_textarea = lv_textarea_create(panel);
    lv_obj_set_width(s_satochip_path_textarea, LV_PCT(100));
    lv_obj_set_height(s_satochip_path_textarea, 64);
    lv_textarea_set_one_line(s_satochip_path_textarea, true);
    lv_textarea_set_placeholder_text(s_satochip_path_textarea,
                                     "m/44'/60'/0'/0/0");
    lv_textarea_set_text(s_satochip_path_textarea, "m/44'/60'/0'/0/0");
    lv_obj_set_style_bg_color(s_satochip_path_textarea, panel_color(), 0);
    lv_obj_set_style_text_color(s_satochip_path_textarea, main_color(), 0);
    lv_obj_set_style_border_color(s_satochip_path_textarea, secondary_color(),
                                  0);
    lv_obj_set_style_border_width(s_satochip_path_textarea, 1, 0);
    lv_obj_set_style_radius(s_satochip_path_textarea, 12, 0);
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

  create_text(panel, "智能卡 PIN", false, highlight_color());
  ui_text_input_create(&s_satochip_pin_input, panel, "智能卡 PIN", true,
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
  create_action_button(panel, "开始读取", satochip_pin_ready_cb, NULL, true);
  create_action_button(panel, "返回", satochip_pin_back_cb, NULL, false);
}

static void create_btc_wallet_block(lv_obj_t *parent,
                                    const krux_feature_t *feature) {
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? feature->title : "比特币钱包", true, main_color());
  create_nav_button(panel, "导出公钥", "legacy_public_key", true);
  create_nav_button(panel, "地址", "legacy_addresses", true);
}

static void create_direct_input_block(lv_obj_t *parent,
                                      const krux_feature_t *feature,
                                      const char *body) {
  (void)body;
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_nav_button(panel, "开始输入", feature ? feature->id : "home", true);
}

static const char *wallet_network_name(wallet_network_t network) {
  return network == WALLET_NETWORK_TESTNET ? "测试网" : "主网";
}

static const char *wallet_policy_name(wallet_policy_t policy) {
  return policy == WALLET_POLICY_MULTISIG ? "多签" : "单签";
}

static void update_wallet_settings_label(lv_obj_t *label) {
  if (!label)
    return;

  char text[192];
  snprintf(text, sizeof(text),
           "默认网络：%s\n默认策略：%s\n宽松签名：%s",
           wallet_network_name(settings_get_default_network()),
           wallet_policy_name(settings_get_default_policy()),
           settings_get_permissive_signing() ? "开启" : "关闭");
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
  create_text(panel, "钱包默认项", false, highlight_color());

  lv_obj_t *status = create_text(panel, "", false, main_color());
  s_wallet_settings_status_label = status;
  update_wallet_settings_label(status);

  create_action_button(panel, "默认主网", wallet_settings_event_cb,
                       (void *)"mainnet", false);
  create_action_button(panel, "默认测试网", wallet_settings_event_cb,
                       (void *)"testnet", false);
  create_action_button(panel, "默认单签", wallet_settings_event_cb,
                       (void *)"singlesig", false);
  create_action_button(panel, "默认多签", wallet_settings_event_cb,
                       (void *)"multisig", false);
  create_action_button(panel, "强制关闭宽松签名", strict_signing_event_cb,
                       status, true);
}

static void create_security_settings_block(lv_obj_t *parent) {
  lv_obj_t *panel =
      create_panel(parent, error_color(), max_i(12, theme_get_small_padding()));
  create_text(panel, "安全设置状态", false, error_color());

  lv_obj_t *status = create_text(panel, "", false, main_color());
  update_wallet_settings_label(status);
  create_action_button(panel, "强制关闭宽松签名", strict_signing_event_cb,
                       status, true);
}

static void create_status_block(lv_obj_t *parent,
                                const krux_feature_t *feature) {
  lv_obj_t *status =
      create_panel(parent, status_color(feature->status),
                   max_i(14, theme_get_default_padding() / 2));

  create_text(status, "功能状态", false, highlight_color());

  char line[192];
  snprintf(line, sizeof(line), "%s / %s",
           krux_feature_status_name(feature->status),
           krux_feature_risk_name(feature->risk));
  create_text(status, line, true, main_color());

  create_text(status, krux_feature_status_detail(feature->status), false,
              secondary_color());
  create_text(status, krux_feature_risk_detail(feature->risk), false,
              secondary_color());
  create_text(status, krux_service_guard_for_feature(feature), false,
              risk_color(feature->risk));
  create_text(status, krux_service_next_step_for_feature(feature), false,
              main_color());
}

static void create_source_block(lv_obj_t *parent,
                                const krux_feature_t *feature) {
  (void)parent;
  (void)feature;
}

static void create_service_block(lv_obj_t *parent) {
  lv_obj_t *panel =
      create_panel(parent, disabled_color(), max_i(12, theme_get_small_padding()));

  create_text(panel, "已启用底层服务", false, highlight_color());
  for (size_t i = 0; i < krux_service_status_count(); i++) {
    const krux_service_status_t *service = krux_service_status_at(i);
    if (!service)
      continue;
    if (service->state != KRUX_SERVICE_READY)
      continue;

    char line[224];
    snprintf(line, sizeof(line), "%s：%s\n%s", service->title,
             krux_service_state_name(service->state), service->summary);
    create_text(panel, line, false, yes_color());
  }
}

static void create_progress_block(lv_obj_t *parent) {
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(12, theme_get_small_padding()));

  char line[224];
  snprintf(line, sizeof(line),
           "可用功能：%zu\n设备项目：%zu\n\n首页只保留常用入口，设备检查里集中查看设备状态。",
           krux_feature_count_by_status(KRUX_FEATURE_VERIFIED),
           krux_feature_count_by_status(KRUX_FEATURE_HARDWARE_WIRED));

  create_text(panel, "可用功能范围", false, highlight_color());
  create_text(panel, line, false, main_color());
}

static void create_hardware_snapshot_block(lv_obj_t *parent,
                                           const char *title) {
  lv_obj_t *panel =
      create_panel(parent, cyan_color(), max_i(12, theme_get_small_padding()));
  create_text(panel, title ? title : "硬件状态", false, highlight_color());

  char text[512];
  krux_hardware_probe_format_snapshot(text, sizeof(text));
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
static char s_satochip_status_text[768];

static void refresh_smartcard_label(lv_obj_t *label) {
  if (!label)
    return;

  char text[768];
  smartcard_ccid_format_report(text, sizeof(text));
  lv_label_set_text(label, text);

  smartcard_ccid_report_t report;
  smartcard_ccid_snapshot(&report);
  lv_color_t color = main_color();
  if (report.state == SMARTCARD_CCID_STATE_PASS ||
      report.state == SMARTCARD_CCID_STATE_APPLET_OK ||
      report.state == SMARTCARD_CCID_STATE_ATR_OK) {
    color = yes_color();
  } else if (report.state == SMARTCARD_CCID_STATE_FAIL ||
             report.state == SMARTCARD_CCID_STATE_TIMEOUT) {
    color = error_color();
  } else if (report.state == SMARTCARD_CCID_STATE_UNSUPPORTED) {
    color = secondary_color();
  }
  lv_obj_set_style_text_color(label, color, 0);
}

static void smartcard_probe_task(void *arg) {
  (void)arg;
  const bool delete_with_caps = s_smartcard_probe_task_with_caps;
  s_smartcard_probe_task_err = smartcard_ccid_probe(12000);
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
    (void)krux_shell_show_screen("smartcard_probe");
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
    lv_label_set_text(label, "检测中");
    lv_obj_set_style_text_color(label, highlight_color(), 0);
  }
  (void)smartcard_ccid_probe(12000);
  refresh_smartcard_label(label);
  return;
#endif

  if (smartcard_task_busy()) {
    dialog_show_error("智能卡忙", NULL, 1600);
    return;
  }

  if (label) {
    lv_label_set_text(label, "检测中");
    lv_obj_set_style_text_color(label, highlight_color(), 0);
  }
  __atomic_store_n(&s_smartcard_probe_task_done, false, __ATOMIC_RELEASE);
  s_smartcard_probe_task_err = ESP_ERR_INVALID_STATE;
  s_smartcard_probe_task_with_caps = false;
  s_smartcard_probe_progress_dialog =
      dialog_show_progress("智能卡检测", "检测中",
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
    dialog_show_error("检测任务启动失败", NULL, 0);
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
  scan_page_create_smartcard_web3(root, legacy_wallet_return_to_krux_cb);
  scan_page_show();
}
#endif

static void legacy_wallet_launch_unified_scan_for_shell(void) {
#ifndef SIMULATOR
  lv_obj_t *root = legacy_wallet_prepare_root();
  scan_page_create_unified(root, legacy_wallet_return_to_krux_cb);
  scan_page_show();
#else
  (void)krux_shell_show_screen("sign_psbt_qr");
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
             "内存不足，读取已取消。");
    goto status_done;
  }

  s_satochip_status_task_err = smartcard_satochip_read_status(status, 12000);
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
    (void)krux_shell_show_screen(s_current_screen_id);
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
  esp_err_t err = smartcard_satochip_read_status(&status, 12000);
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
    dialog_show_error("智能卡忙", NULL, 1600);
    return;
  }

  if (label) {
    lv_label_set_text(label, "读取中");
    lv_obj_set_style_text_color(label, highlight_color(), 0);
  }
  s_satochip_status_text[0] = '\0';
  s_satochip_status_app_selected = false;
  __atomic_store_n(&s_satochip_status_task_done, false, __ATOMIC_RELEASE);
  s_satochip_status_task_err = ESP_ERR_INVALID_STATE;
  s_satochip_status_task_with_caps = false;
  s_satochip_status_progress_dialog =
      dialog_show_progress("读取状态", "读取中",
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
    dialog_show_error("读取任务启动失败", NULL, 0);
    return;
  }
  s_satochip_status_poll_timer =
      lv_timer_create(satochip_status_poll_cb, 100, NULL);
}

static void create_satochip_web3_block(lv_obj_t *parent) {
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(12, theme_get_small_padding()));
  create_text(panel, "Satochip", true, main_color());

  lv_obj_t *result = create_text(panel, "等待", false, main_color());
  if (s_satochip_status_text[0]) {
    lv_label_set_text(result, s_satochip_status_text);
    lv_obj_set_style_text_color(
        result,
        s_satochip_status_task_err == ESP_OK && s_satochip_status_app_selected
            ? yes_color()
            : error_color(),
        0);
  }
  create_action_button(panel, "读取状态", satochip_status_event_cb, result,
                       true);
#ifndef SIMULATOR
  create_action_button(panel, "扫码 Web3", smartcard_web3_scan_event_cb, NULL,
                       true);
#endif
}

static void create_smartcard_probe_block(lv_obj_t *parent) {
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(12, theme_get_small_padding()));
  create_text(panel, "智能卡检测", false, highlight_color());

  lv_obj_t *result = create_text(panel, "等待", false, main_color());
  create_action_button(panel, "开始检测", smartcard_probe_event_cb, result,
                       true);
  refresh_smartcard_label(result);
}

#define SATOCHIP_MAINT_TASK_STACK_SIZE 16384

typedef enum {
  SATOCHIP_MAINT_NONE = 0,
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
  SATOCHIP_MAINT_SEEDKEEPER_SAVE_PASSWORD,
  SATOCHIP_MAINT_SEEDKEEPER_LOAD_DESCRIPTOR,
  SATOCHIP_MAINT_SEEDKEEPER_SAVE_DESCRIPTOR,
  SATOCHIP_MAINT_SEEDKEEPER_CLONE,
  SATOCHIP_MAINT_SEEDKEEPER_CHANGE_PIN,
  SATOCHIP_MAINT_SEEDKEEPER_RESET,
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
static char s_satochip_maint_result[8192];
static char s_satochip_maint_pin[80];
static char s_satochip_maint_text_a[1024];
static char s_satochip_maint_text_b[4096];
static char s_satochip_maint_text_c[4096];

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

static void satochip_maint_zero_textarea(lv_obj_t *textarea) {
  if (!textarea)
    return;
  const char *text = lv_textarea_get_text(textarea);
  if (text && text[0]) {
    secure_memzero((void *)text, strlen(text));
    lv_textarea_set_text(textarea, "");
  }
}

static void satochip_maint_input_cleanup(void) {
  if (s_satochip_maint_input_active) {
    ui_text_input_destroy(&s_satochip_maint_input);
    memset(&s_satochip_maint_input, 0, sizeof(s_satochip_maint_input));
    s_satochip_maint_input_active = false;
  }
  satochip_maint_zero_textarea(s_satochip_maint_extra_a);
  satochip_maint_zero_textarea(s_satochip_maint_extra_b);
  satochip_maint_zero_textarea(s_satochip_maint_extra_c);
  s_satochip_maint_extra_a = NULL;
  s_satochip_maint_extra_b = NULL;
  s_satochip_maint_extra_c = NULL;
}

static void shell_transient_state_reset(void) {
  web3_qr_state_reset();
  satochip_pin_input_cleanup();
  satochip_maint_input_cleanup();
}

static void satochip_maint_result_clear(void) {
  s_satochip_maint_result[0] = '\0';
  s_satochip_maint_task_err = ESP_ERR_INVALID_STATE;
}

static void satochip_maint_prepare(satochip_maint_mode_t mode) {
  if (s_satochip_maint_mode != mode)
    satochip_maint_result_clear();
  s_satochip_maint_mode = mode;
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
    satochip_appendf(dst, dst_len, &pos, "没有结果。");
    return;
  }
  satochip_appendf(dst, dst_len, &pos, "SW：%04X\n", res->sw);
  if (res->detail[0])
    satochip_appendf(dst, dst_len, &pos, "%s\n", res->detail);
  if (res->response_len > 0) {
    char hex[1536];
    satochip_hex_bytes_spaced(res->response,
                              res->response_len > 96 ? 96 : res->response_len,
                              hex, sizeof(hex));
    satochip_appendf(dst, dst_len, &pos, "响应：%u 字节\n%s\n",
                     (unsigned)res->response_len, hex);
  }
}

static bool seedkeeper_build_plain_header(uint8_t type, uint8_t subtype,
                                          uint8_t export_rights,
                                          const char *label,
                                          const uint8_t fingerprint[4],
                                          uint8_t *out, size_t out_len,
                                          size_t *written_out) {
  if (!out || out_len < 15 || !written_out)
    return false;

  const char *safe_label = (label && label[0]) ? label : "Kern";
  size_t label_len = strnlen(safe_label, SEEDKEEPER_LABEL_MAX_BYTES + 1);
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
    snprintf(result, result_len, "%s内容太长或为空。", title ? title : "");
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
  if (header.type != SEEDKEEPER_TYPE_BIP39_MNEMONIC)
    return false;

  const uint8_t *payload = secret;
  size_t payload_len = secret_len;
  if (payload_len < 2)
    return false;

  size_t pos = 0;
  size_t mnemonic_len = payload[pos++];
  if (mnemonic_len == 0 || mnemonic_len >= mnemonic_out_len ||
      pos + mnemonic_len > payload_len)
    return false;
  memcpy(mnemonic_out, payload + pos, mnemonic_len);
  mnemonic_out[mnemonic_len] = '\0';
  pos += mnemonic_len;

  if (pos >= payload_len)
    return false;
  size_t passphrase_len = payload[pos++];
  if (pos + passphrase_len > payload_len)
    return false;
  if (passphrase_out && passphrase_out_len > 0) {
    size_t copy = passphrase_len;
    if (copy >= passphrase_out_len)
      copy = passphrase_out_len - 1;
    memcpy(passphrase_out, payload + pos, copy);
    passphrase_out[copy] = '\0';
  }

  if (header_out)
    *header_out = header;
  secure_memzero(secret, sizeof(secret));
  return true;
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
  if (header.type == SEEDKEEPER_TYPE_SECRET_KEY &&
      header.origin == SEEDKEEPER_ORIGIN_GENERATED_ON_CARD) {
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
    satochip_appendf(dst, dst_len, &pos, "SW：%04X\n", apdu->sw);
  if (apdu && apdu->detail[0])
    satochip_appendf(dst, dst_len, &pos, "%s\n", apdu->detail);
  if (!parsed) {
    satochip_appendf(dst, dst_len, &pos,
                     "没有解析到 BIP39 助记词。请确认 SID 和导出权限。");
    return;
  }

  satochip_appendf(dst, dst_len, &pos, "SID：%u\n标签：%s\n\n%s",
                   (unsigned)header.id,
                   header.label[0] ? header.label : "(无标签)", mnemonic);
  if (passphrase[0])
    satochip_appendf(dst, dst_len, &pos, "\n\n密语：%s", passphrase);
  if (load_result)
    satochip_appendf(dst, dst_len, &pos, "\n\n加载：%s%s%u",
                     loaded_ok ? "成功，槽位 " : "失败",
                     loaded_ok ? "" : "。",
                     loaded_ok ? (unsigned)(slot_index + 1U) : 0U);
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
                   title ? title : "智能卡创建");
  if (sid)
    satochip_appendf(dst, dst_len, &pos, "SID：%u\n", (unsigned)sid);
  if (fingerprint)
    satochip_appendf(dst, dst_len, &pos, "指纹：%02X%02X%02X%02X\n",
                     fingerprint[0], fingerprint[1], fingerprint[2],
                     fingerprint[3]);
  if (mnemonic && mnemonic[0])
    satochip_appendf(dst, dst_len, &pos, "\n%s", mnemonic);
  satochip_appendf(dst, dst_len, &pos, "\n\n加载：%s%s%u",
                   loaded_ok ? "成功，槽位 " : "失败",
                   loaded_ok ? "" : "。",
                   loaded_ok ? (unsigned)(slot_index + 1U) : 0U);
}

static void satochip_format_certificate_export(
    const smartcard_satochip_certificate_t *cert, char *dst, size_t dst_len) {
  if (!dst || dst_len == 0)
    return;
  dst[0] = '\0';
  size_t pos = 0;
  if (!cert) {
    satochip_appendf(dst, dst_len, &pos, "没有证书结果。");
    return;
  }

  satochip_appendf(dst, dst_len, &pos, "SW：%04X\n", cert->sw);
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
    satochip_appendf(dst, dst_len, &pos, "没有真伪结果。");
    return;
  }

  satochip_appendf(dst, dst_len, &pos, "SW：%04X\n", auth->sw);
  satochip_appendf(dst, dst_len, &pos, "结果：%s\n",
                   auth->authentic ? "通过" : "未通过");
  if (auth->subject_cn[0])
    satochip_appendf(dst, dst_len, &pos, "CN：%s\n", auth->subject_cn);
  if (auth->error[0])
    satochip_appendf(dst, dst_len, &pos, "%s\n", auth->error);
  if (auth->ca_text[0])
    satochip_appendf(dst, dst_len, &pos, "\nCA\n%s\n", auth->ca_text);
  if (auth->subca_text[0])
    satochip_appendf(dst, dst_len, &pos, "\nSubCA\n%s\n", auth->subca_text);
  if (auth->device_text[0])
    satochip_appendf(dst, dst_len, &pos, "\n设备\n%s\n", auth->device_text);
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
    satochip_appendf(dst, dst_len, &pos, "没有列表结果。");
    return;
  }

  satochip_appendf(dst, dst_len, &pos, "SeedKeeper 列表\n");
  satochip_appendf(dst, dst_len, &pos, "SW：%04X\n", list->sw);
  if (list->detail[0])
    satochip_appendf(dst, dst_len, &pos, "%s\n", list->detail);
  satochip_appendf(dst, dst_len, &pos, "条目：%u\n",
                   (unsigned)list->count);

  if (list->count == 0) {
    satochip_appendf(dst, dst_len, &pos, "无条目。");
    return;
  }

  for (size_t i = 0; i < list->count; i++) {
    const smartcard_seedkeeper_header_t *header = &list->headers[i];
    char fingerprint[16];
    satochip_hex_bytes_upper(header->fingerprint, sizeof(header->fingerprint),
                             fingerprint, sizeof(fingerprint));
    satochip_appendf(dst, dst_len, &pos,
                     "\n#%u ID=%u\n"
                     "标签：%s\n"
                     "类型：%u/%u 源：%u 权限：%u 计数：%u\n"
                     "指纹：%s\n",
                     (unsigned)(i + 1U), (unsigned)header->id,
                     header->label[0] ? header->label : "(无标签)",
                     (unsigned)header->type, (unsigned)header->subtype,
                     (unsigned)header->origin,
                     (unsigned)header->export_rights,
                     (unsigned)header->export_counter, fingerprint);
  }
}

static const char *
satochip_seedkeeper_stub_title(satochip_maint_mode_t mode) {
  switch (mode) {
  case SATOCHIP_MAINT_SEEDKEEPER_SAVE_PASSWORD:
    return "存密码";
  case SATOCHIP_MAINT_SEEDKEEPER_LOAD_DESCRIPTOR:
    return "读描述符";
  case SATOCHIP_MAINT_SEEDKEEPER_SAVE_DESCRIPTOR:
    return "存描述符";
  case SATOCHIP_MAINT_SEEDKEEPER_CLONE:
    return "克隆";
  case SATOCHIP_MAINT_SEEDKEEPER_RESET:
  case SATOCHIP_MAINT_SEEDKEEPER_RESET_SECRET:
    return "重置";
  case SATOCHIP_MAINT_SEEDKEEPER_GENERATE_MASTERSEED:
    return "主种子";
  case SATOCHIP_MAINT_SEEDKEEPER_GENERATE_2FA_SECRET:
    return "2FA";
  case SATOCHIP_MAINT_SEEDKEEPER_DERIVE_MASTER_PASSWORD:
    return "派生密码";
  default:
    return "SeedKeeper";
  }
}

static bool satochip_maint_mode_requires_pin(satochip_maint_mode_t mode) {
  return mode == SATOCHIP_MAINT_CHANGE_PIN ||
         mode == SATOCHIP_MAINT_SEEDKEEPER_CHANGE_PIN ||
         mode == SATOCHIP_MAINT_SET_LABEL ||
         mode == SATOCHIP_MAINT_NFC_POLICY ||
         mode == SATOCHIP_MAINT_FEATURE_POLICY ||
         mode == SATOCHIP_MAINT_RESET_SEED ||
         mode == SATOCHIP_MAINT_SEEDKEEPER_CREATE_MNEMONIC ||
         mode == SATOCHIP_MAINT_SEEDKEEPER_WRITE_MNEMONIC ||
         mode == SATOCHIP_MAINT_SEEDKEEPER_VIEW_MNEMONIC ||
         mode == SATOCHIP_MAINT_SEEDKEEPER_LOAD_MNEMONIC ||
         mode == SATOCHIP_MAINT_SEEDKEEPER_SAVE_PASSWORD ||
         mode == SATOCHIP_MAINT_SEEDKEEPER_LOAD_DESCRIPTOR ||
         mode == SATOCHIP_MAINT_SEEDKEEPER_SAVE_DESCRIPTOR ||
         mode == SATOCHIP_MAINT_SEEDKEEPER_CLONE ||
         mode == SATOCHIP_MAINT_SEEDKEEPER_RESET ||
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
          strcmp(id, "smartcard_satochip_change_pin") == 0 ||
          strcmp(id, "smartcard_satochip_unblock_pin") == 0 ||
          strcmp(id, "smartcard_satochip_set_label") == 0 ||
          strcmp(id, "smartcard_satochip_nfc_policy") == 0 ||
          strcmp(id, "smartcard_satochip_feature_policy") == 0 ||
          strcmp(id, "smartcard_satochip_reset_seed") == 0 ||
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
          strcmp(id, "smartcard_seedkeeper_save_password") == 0 ||
          strcmp(id, "smartcard_seedkeeper_load_descriptor") == 0 ||
          strcmp(id, "smartcard_seedkeeper_save_descriptor") == 0 ||
          strcmp(id, "smartcard_seedkeeper_clone") == 0 ||
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
  if (default_text && default_text[0])
    lv_textarea_set_text(textarea, default_text);
  if (max_len > 0)
    lv_textarea_set_max_length(textarea, max_len);
  lv_obj_set_style_text_font(textarea, theme_font_small(), 0);
  lv_obj_set_style_text_color(textarea, main_color(), 0);
  lv_obj_set_style_bg_color(textarea, panel_color(), 0);
  lv_obj_set_style_bg_opa(textarea, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(textarea, highlight_color(), 0);
  lv_obj_set_style_border_width(textarea, 1, 0);
  lv_obj_set_style_radius(textarea, 12, 0);
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
  lv_obj_set_style_bg_color(s_satochip_maint_input.keyboard, disabled_color(),
                            LV_PART_ITEMS);
  lv_obj_set_style_text_color(s_satochip_maint_input.keyboard, main_color(),
                              LV_PART_ITEMS);
  lv_obj_set_style_text_font(s_satochip_maint_input.keyboard,
                             theme_font_small(), LV_PART_ITEMS);
  lv_obj_set_style_border_width(s_satochip_maint_input.keyboard, 0,
                                LV_PART_ITEMS);
  lv_obj_set_style_radius(s_satochip_maint_input.keyboard, 6, LV_PART_ITEMS);
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
  if (default_text && default_text[0])
    lv_textarea_set_text(textarea, default_text);
  if (max_len > 0)
    lv_textarea_set_max_length(textarea, max_len);
  lv_obj_set_style_text_font(textarea, theme_font_small(), 0);
  lv_obj_set_style_text_color(textarea, main_color(), 0);
  lv_obj_set_style_bg_color(textarea, panel_color(), 0);
  lv_obj_set_style_bg_opa(textarea, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(textarea, highlight_color(), 0);
  lv_obj_set_style_border_width(textarea, 1, 0);
  lv_obj_set_style_radius(textarea, 12, 0);
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
             "内存不足，智能卡操作已取消。");
    goto satochip_maint_done;
  }

  switch (s_satochip_maint_mode) {
  case SATOCHIP_MAINT_CHANGE_PIN: {
    uint8_t pin_nbr = 0;
    if (!satochip_parse_u8_text(s_satochip_maint_text_c, &pin_nbr)) {
      s_satochip_maint_task_err = ESP_ERR_INVALID_ARG;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "PIN 编号无效。");
      break;
    }
    s_satochip_maint_task_err = smartcard_satochip_card_change_pin(
        pin_nbr, s_satochip_maint_pin, s_satochip_maint_text_a, apdu, 20000);
    satochip_format_apdu_result(s_satochip_maint_result,
                                sizeof(s_satochip_maint_result), "改 PIN",
                                apdu);
    break;
  }
  case SATOCHIP_MAINT_SEEDKEEPER_CHANGE_PIN: {
    uint8_t pin_nbr = 0;
    if (!satochip_parse_u8_text(s_satochip_maint_text_c, &pin_nbr)) {
      s_satochip_maint_task_err = ESP_ERR_INVALID_ARG;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "PIN 编号无效。");
      break;
    }
    s_satochip_maint_task_err = smartcard_seedkeeper_change_pin(
        pin_nbr, s_satochip_maint_pin, s_satochip_maint_text_a, apdu, 20000);
    satochip_format_apdu_result(s_satochip_maint_result,
                                sizeof(s_satochip_maint_result),
                                "SeedKeeper 改PIN", apdu);
    break;
  }
  case SATOCHIP_MAINT_UNBLOCK_PIN: {
    size_t puk_len = strlen(s_satochip_maint_text_a);
    if (puk_len < 4) {
      s_satochip_maint_task_err = ESP_ERR_INVALID_ARG;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "PUK 至少需要 4 个字符。");
      break;
    }
    s_satochip_maint_task_err = smartcard_satochip_card_unblock_pin(
        0, (const uint8_t *)s_satochip_maint_text_a, puk_len, apdu, 20000);
    satochip_format_apdu_result(s_satochip_maint_result,
                                sizeof(s_satochip_maint_result), "解锁 PIN",
                                apdu);
    break;
  }
  case SATOCHIP_MAINT_SET_LABEL:
    s_satochip_maint_task_err = smartcard_satochip_card_set_label(
        s_satochip_maint_pin, s_satochip_maint_text_a, apdu, 20000);
    satochip_format_apdu_result(s_satochip_maint_result,
                                sizeof(s_satochip_maint_result), "改标签",
                                apdu);
    break;
  case SATOCHIP_MAINT_NFC_POLICY: {
    uint8_t policy = 0;
    if (!satochip_parse_u8_text(s_satochip_maint_text_a, &policy)) {
      s_satochip_maint_task_err = ESP_ERR_INVALID_ARG;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "NFC 策略值无效。");
      break;
    }
    s_satochip_maint_task_err = smartcard_satochip_card_set_nfc_policy(
        s_satochip_maint_pin, policy, apdu, 20000);
    satochip_format_apdu_result(s_satochip_maint_result,
                                sizeof(s_satochip_maint_result), "NFC 策略",
                                apdu);
    break;
  }
  case SATOCHIP_MAINT_FEATURE_POLICY: {
    uint8_t feature_id = 0;
    uint8_t policy = 0;
    if (!satochip_parse_u8_text(s_satochip_maint_text_a, &feature_id) ||
        !satochip_parse_u8_text(s_satochip_maint_text_b, &policy)) {
      s_satochip_maint_task_err = ESP_ERR_INVALID_ARG;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "功能号或策略值无效。");
      break;
    }
    s_satochip_maint_task_err = smartcard_satochip_card_set_feature_policy(
        s_satochip_maint_pin, feature_id, policy, apdu, 20000);
    satochip_format_apdu_result(
        s_satochip_maint_result, sizeof(s_satochip_maint_result), "功能策略",
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
                                sizeof(s_satochip_maint_result), "重置种子",
                                apdu);
    break;
  }
  case SATOCHIP_MAINT_RESET_FACTORY:
    s_satochip_maint_task_err =
        smartcard_satochip_card_reset_factory_signal(apdu, 20000);
    satochip_format_apdu_result(s_satochip_maint_result,
                                sizeof(s_satochip_maint_result), "出厂复位",
                                apdu);
    break;
  case SATOCHIP_MAINT_EXPORT_AUTHENTIKEY:
    s_satochip_maint_task_err = smartcard_satochip_card_export_authentikey(
        apdu, 20000);
    satochip_format_apdu_result(s_satochip_maint_result,
                                sizeof(s_satochip_maint_result), "导主钥",
                                apdu);
    break;
  case SATOCHIP_MAINT_IMPORT_NDEF_AUTHENTIKEY: {
    uint8_t privkey[32];
    size_t privkey_len = satochip_parse_hex_bytes(s_satochip_maint_text_a,
                                                  privkey, sizeof(privkey));
    if (privkey_len != sizeof(privkey)) {
      s_satochip_maint_task_err = ESP_ERR_INVALID_ARG;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "私钥需要 32 字节十六进制。");
      secure_memzero(privkey, sizeof(privkey));
      break;
    }
    s_satochip_maint_task_err = smartcard_satochip_card_import_ndef_authentikey(
        privkey, apdu, 20000);
    secure_memzero(privkey, sizeof(privkey));
    satochip_format_apdu_result(s_satochip_maint_result,
                                sizeof(s_satochip_maint_result), "写NDEF",
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
               "公钥需要 65 字节十六进制。");
      secure_memzero(pubkey, sizeof(pubkey));
      break;
    }
    if (pubkey[0] != 0x04) {
      s_satochip_maint_task_err = ESP_ERR_INVALID_ARG;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "公钥首字节需为 04。");
      secure_memzero(pubkey, sizeof(pubkey));
      break;
    }
    s_satochip_maint_task_err = smartcard_satochip_card_import_trusted_pubkey(
        pubkey, apdu, 20000);
    secure_memzero(pubkey, sizeof(pubkey));
    satochip_format_apdu_result(s_satochip_maint_result,
                                sizeof(s_satochip_maint_result), "写信任钥",
                                apdu);
    break;
  }
  case SATOCHIP_MAINT_EXPORT_TRUSTED_PUBKEY:
    s_satochip_maint_task_err = smartcard_satochip_card_export_trusted_pubkey(
        apdu, 20000);
    satochip_format_apdu_result(s_satochip_maint_result,
                                sizeof(s_satochip_maint_result), "导信任钥",
                                apdu);
    break;
  case SATOCHIP_MAINT_SET_2FA_KEY: {
    uint8_t key[20];
    size_t key_len =
        satochip_parse_hex_bytes(s_satochip_maint_text_a, key, sizeof(key));
    uint64_t limit = 0;
    if (key_len != sizeof(key) ||
        !satochip_parse_u64_text(s_satochip_maint_text_b, &limit)) {
      s_satochip_maint_task_err = ESP_ERR_INVALID_ARG;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "2FA key 需要 20 字节，限额需为数字。");
      secure_memzero(key, sizeof(key));
      break;
    }
    s_satochip_maint_task_err = smartcard_satochip_card_set_2fa_key(
        key, sizeof(key), limit, apdu, 20000);
    secure_memzero(key, sizeof(key));
    satochip_format_apdu_result(s_satochip_maint_result,
                                sizeof(s_satochip_maint_result), "设2FA",
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
               "挑战值需要 20 字节十六进制。");
      secure_memzero(chal, sizeof(chal));
      break;
    }
    s_satochip_maint_task_err = smartcard_satochip_card_reset_2fa_key(
        chal, sizeof(chal), apdu, 20000);
    secure_memzero(chal, sizeof(chal));
    satochip_format_apdu_result(s_satochip_maint_result,
                                sizeof(s_satochip_maint_result), "清2FA",
                                apdu);
    break;
  }
  case SATOCHIP_MAINT_LOGOUT_ALL:
    s_satochip_maint_task_err = smartcard_satochip_card_logout_all(apdu, 20000);
    satochip_format_apdu_result(s_satochip_maint_result,
                                sizeof(s_satochip_maint_result), "登出",
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
               "内存不足，无法读取状态。");
      break;
    }
    s_satochip_maint_task_err = smartcard_seedkeeper_read_status(status, 12000);
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
               "内存不足，无法读取列表。");
      break;
    }
    s_satochip_maint_task_err = smartcard_seedkeeper_list_secret_headers(
        s_satochip_maint_pin[0] ? s_satochip_maint_pin : NULL, list, 12000);
    satochip_format_seedkeeper_header_list_result(
        list, s_satochip_maint_result, sizeof(s_satochip_maint_result));
    secure_memzero(list, sizeof(*list));
    free(list);
    break;
  }
  case SATOCHIP_MAINT_SEEDKEEPER_LOGS:
    s_satochip_maint_task_err = smartcard_seedkeeper_print_logs(
        s_satochip_maint_pin[0] ? s_satochip_maint_pin : NULL, apdu, 12000);
    satochip_format_apdu_result(s_satochip_maint_result,
                                sizeof(s_satochip_maint_result),
                                "SeedKeeper 日志", apdu);
    break;
  case SATOCHIP_MAINT_SEEDKEEPER_CREATE_MNEMONIC: {
    uint32_t words = 12;
    if (s_satochip_maint_text_a[0] &&
        !satochip_parse_u32_text(s_satochip_maint_text_a, &words)) {
      s_satochip_maint_task_err = ESP_ERR_INVALID_ARG;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "词数无效。");
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
               "词数只支持 12/15/18/21/24。");
      break;
    }

    const char *label =
        s_satochip_maint_text_b[0] ? s_satochip_maint_text_b : "BIP39-RNG-Kern";
    s_satochip_maint_task_err = smartcard_seedkeeper_generate_random_secret(
        s_satochip_maint_pin, SEEDKEEPER_TYPE_SECRET_KEY, 0x00,
        (uint8_t)entropy_len, SEEDKEEPER_EXPORT_PLAINTEXT_ALLOWED, label,
        false, NULL, 0, apdu, 30000);
    if (s_satochip_maint_task_err != ESP_OK || apdu->sw != 0x9000 ||
        apdu->response_len < 6) {
      satochip_format_apdu_result(s_satochip_maint_result,
                                  sizeof(s_satochip_maint_result),
                                  "智能卡创建失败", apdu);
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
                                  "读取随机数失败", apdu);
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
               "随机数解析失败。");
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
               "助记词生成失败。");
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
               "内存不足。");
      secure_memzero(entropy, sizeof(entropy));
      secure_memzero(fp, sizeof(fp));
      break;
    }
#endif

    size_t slot_index = 0;
    bool loaded_ok =
        seedkeeper_load_mnemonic_into_session(mnemonic, "", &slot_index);
    satochip_format_seedkeeper_create_result(
        s_satochip_maint_result, sizeof(s_satochip_maint_result),
        "智能卡创建", header.id ? header.id : sid, header.fingerprint[0] ||
                                              header.fingerprint[1] ||
                                              header.fingerprint[2] ||
                                              header.fingerprint[3]
                                          ? header.fingerprint
                                          : fp,
        mnemonic, loaded_ok, slot_index);
    s_satochip_maint_task_err = loaded_ok ? ESP_OK : ESP_ERR_INVALID_STATE;
    SECURE_FREE_STRING(mnemonic);
#ifndef SIMULATOR
    secure_memzero(entropy_hex, sizeof(entropy_hex));
#endif
    secure_memzero(entropy, sizeof(entropy));
    secure_memzero(fp, sizeof(fp));
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
               "请先导入或创建助记词。");
      break;
    }
    const char *mnemonic = mnemonic_owned;
#endif
    uint8_t header[15 + SEEDKEEPER_LABEL_MAX_BYTES];
    uint8_t secret[2 + SEEDKEEPER_MNEMONIC_MAX_BYTES * 2];
    uint8_t secret_hash[CRYPTO_SHA256_SIZE];
    size_t header_len = 0;
    size_t secret_len = 0;
    const char *label = s_satochip_maint_text_a[0] ? s_satochip_maint_text_a
                                                    : "Kern 助记词";
    const char *passphrase =
        s_satochip_maint_text_b[0] ? s_satochip_maint_text_b : "";
    if (!seedkeeper_build_mnemonic_secret(mnemonic, passphrase, secret,
                                          sizeof(secret), &secret_len) ||
        crypto_sha256(secret, secret_len, secret_hash) != CRYPTO_OK ||
        !seedkeeper_build_plain_header(
            SEEDKEEPER_TYPE_BIP39_MNEMONIC, 0x00,
            SEEDKEEPER_EXPORT_PLAINTEXT_ALLOWED, label, secret_hash, header,
            sizeof(header), &header_len)) {
      s_satochip_maint_task_err = ESP_ERR_INVALID_ARG;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "助记词或标签太长。");
#ifndef SIMULATOR
      SECURE_FREE_STRING(mnemonic_owned);
#endif
      secure_memzero(header, sizeof(header));
      secure_memzero(secret, sizeof(secret));
      secure_memzero(secret_hash, sizeof(secret_hash));
      break;
    }
    s_satochip_maint_task_err = smartcard_seedkeeper_import_secret(
        s_satochip_maint_pin, header, header_len, secret, secret_len, 0, NULL, 0,
        NULL, 0, false, apdu, 30000);
    satochip_format_apdu_result(s_satochip_maint_result,
                                sizeof(s_satochip_maint_result), "写入助记词",
                                apdu);
#ifndef SIMULATOR
    SECURE_FREE_STRING(mnemonic_owned);
#endif
    secure_memzero(header, sizeof(header));
    secure_memzero(secret, sizeof(secret));
    secure_memzero(secret_hash, sizeof(secret_hash));
    break;
  }
  case SATOCHIP_MAINT_SEEDKEEPER_VIEW_MNEMONIC:
  case SATOCHIP_MAINT_SEEDKEEPER_LOAD_MNEMONIC: {
    uint16_t sid = 0;
    if (!satochip_parse_u16_text(s_satochip_maint_text_a, &sid)) {
      s_satochip_maint_task_err = ESP_ERR_INVALID_ARG;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "SID 无效。");
      break;
    }
    if (sid == 0) {
      s_satochip_maint_task_err = ESP_ERR_INVALID_ARG;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "请填写 SID。");
      break;
    }

    s_satochip_maint_task_err = smartcard_seedkeeper_export_secret(
        s_satochip_maint_pin, sid, 0, false, apdu, 30000);
    bool loaded_ok = false;
    size_t slot_index = 0;
    if (s_satochip_maint_task_err == ESP_OK &&
        s_satochip_maint_mode == SATOCHIP_MAINT_SEEDKEEPER_LOAD_MNEMONIC) {
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
    satochip_format_seedkeeper_mnemonic_result(
        s_satochip_maint_result, sizeof(s_satochip_maint_result),
        s_satochip_maint_mode == SATOCHIP_MAINT_SEEDKEEPER_LOAD_MNEMONIC
            ? "加载助记词"
            : "查看助记词",
        apdu,
        s_satochip_maint_mode == SATOCHIP_MAINT_SEEDKEEPER_LOAD_MNEMONIC,
        loaded_ok, slot_index);
    break;
  }
  case SATOCHIP_MAINT_SEEDKEEPER_SAVE_PASSWORD: {
    const char *label =
        s_satochip_maint_text_b[0] ? s_satochip_maint_text_b : "密码";
    const char *password = s_satochip_maint_text_c;
    s_satochip_maint_task_err = ESP_ERR_INVALID_ARG;
    (void)seedkeeper_import_text_secret(
        s_satochip_maint_pin, SEEDKEEPER_TYPE_PASSWORD, 0x00, label, password,
        false, "保存密码", apdu, s_satochip_maint_result,
        sizeof(s_satochip_maint_result));
    break;
  }
  case SATOCHIP_MAINT_SEEDKEEPER_LOAD_DESCRIPTOR: {
    uint16_t sid = 0;
    if (!satochip_parse_u16_text(s_satochip_maint_text_b, &sid) || sid == 0) {
      s_satochip_maint_task_err = ESP_ERR_INVALID_ARG;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "请输入描述符 SID。");
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
                     &pos, "加载描述符\nSW：%04X\n", apdu->sw);
    if (apdu->detail[0])
      satochip_appendf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
                       &pos, "%s\n", apdu->detail);
    if (!parsed) {
      satochip_appendf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
                       &pos, "没有解析到描述符文本。");
      s_satochip_maint_task_err = ESP_ERR_INVALID_RESPONSE;
    } else {
      satochip_appendf(
          s_satochip_maint_result, sizeof(s_satochip_maint_result), &pos,
          "SID：%u\n标签：%s\n加载：%s\n\n%s",
          (unsigned)header.id, header.label[0] ? header.label : "(无标签)",
          loaded ? "成功" : "失败", descriptor);
      s_satochip_maint_task_err = loaded ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
    }
    secure_memzero(descriptor, sizeof(descriptor));
    break;
  }
  case SATOCHIP_MAINT_SEEDKEEPER_SAVE_DESCRIPTOR: {
    const char *label =
        s_satochip_maint_text_b[0] ? s_satochip_maint_text_b : "钱包描述符";
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
               "没有可保存的描述符。");
      break;
    }
    s_satochip_maint_task_err = ESP_ERR_INVALID_ARG;
    (void)seedkeeper_import_text_secret(
        s_satochip_maint_pin, SEEDKEEPER_TYPE_DATA, 0x00, label, descriptor,
        true, "保存描述符", apdu, s_satochip_maint_result,
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
    if (!satochip_parse_u16_text(s_satochip_maint_text_b, &sid) || sid == 0 ||
        !satochip_parse_u16_text(s_satochip_maint_text_c, &sid_pubkey) ||
        sid_pubkey == 0) {
      s_satochip_maint_task_err = ESP_ERR_INVALID_ARG;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "请输入 SID 和目标公钥 SID。");
      break;
    }
    s_satochip_maint_task_err = smartcard_seedkeeper_export_secret_to_satochip(
        s_satochip_maint_pin, sid, sid_pubkey, apdu, 30000);
    satochip_format_apdu_result(s_satochip_maint_result,
                                sizeof(s_satochip_maint_result), "克隆",
                                apdu);
    break;
  }
  case SATOCHIP_MAINT_SEEDKEEPER_RESET: {
    uint16_t sid = 0;
    if (!satochip_parse_u16_text(s_satochip_maint_text_b, &sid)) {
      s_satochip_maint_task_err = ESP_ERR_INVALID_ARG;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "SID 无效。");
      break;
    }
    if (sid == 0) {
      s_satochip_maint_task_err =
          smartcard_seedkeeper_reset_factory_signal(apdu, 30000);
      satochip_format_apdu_result(s_satochip_maint_result,
                                  sizeof(s_satochip_maint_result),
                                  "SeedKeeper 出厂", apdu);
    } else {
      s_satochip_maint_task_err =
          smartcard_seedkeeper_reset_secret(s_satochip_maint_pin, sid, apdu,
                                            30000);
      satochip_format_apdu_result(s_satochip_maint_result,
                                  sizeof(s_satochip_maint_result),
                                  "删除条目", apdu);
    }
    break;
  }
  case SATOCHIP_MAINT_SEEDKEEPER_GENERATE_MASTERSEED: {
    uint32_t seed_size = 64;
    if (s_satochip_maint_text_b[0] &&
        !satochip_parse_u32_text(s_satochip_maint_text_b, &seed_size)) {
      s_satochip_maint_task_err = ESP_ERR_INVALID_ARG;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "长度无效。");
      break;
    }
    if (seed_size != 16 && seed_size != 32 && seed_size != 64) {
      s_satochip_maint_task_err = ESP_ERR_INVALID_ARG;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "长度只支持 16/32/64。");
      break;
    }
    const char *label =
        s_satochip_maint_text_c[0] ? s_satochip_maint_text_c : "Masterseed";
    s_satochip_maint_task_err = smartcard_seedkeeper_generate_masterseed(
        s_satochip_maint_pin, (uint8_t)seed_size,
        SEEDKEEPER_EXPORT_PLAINTEXT_ALLOWED, label, apdu, 30000);
    satochip_format_apdu_result(s_satochip_maint_result,
                                sizeof(s_satochip_maint_result), "主种子",
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
    if (!satochip_parse_u16_text(s_satochip_maint_text_b, &sid) || sid == 0) {
      s_satochip_maint_task_err = ESP_ERR_INVALID_ARG;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "请输入 SID。");
      break;
    }
    const char *salt =
        s_satochip_maint_text_c[0] ? s_satochip_maint_text_c : "Kern";
    s_satochip_maint_task_err = smartcard_seedkeeper_derive_master_password(
        s_satochip_maint_pin, salt, sid, 0, false, apdu, 30000);
    satochip_format_apdu_result(s_satochip_maint_result,
                                sizeof(s_satochip_maint_result), "派生密码",
                                apdu);
    break;
  }
  case SATOCHIP_MAINT_SEEDKEEPER_RESET_SECRET: {
    uint16_t sid = 0;
    if (!satochip_parse_u16_text(s_satochip_maint_text_b, &sid) || sid == 0) {
      s_satochip_maint_task_err = ESP_ERR_INVALID_ARG;
      snprintf(s_satochip_maint_result, sizeof(s_satochip_maint_result),
               "请输入 SID。");
      break;
    }
    s_satochip_maint_task_err =
        smartcard_seedkeeper_reset_secret(s_satochip_maint_pin, sid, apdu,
                                          30000);
    satochip_format_apdu_result(s_satochip_maint_result,
                                sizeof(s_satochip_maint_result), "删除条目",
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
                                sizeof(s_satochip_maint_result), "导入证书",
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
             "没有可执行的任务。");
    break;
  }

satochip_maint_done:
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
  __atomic_store_n(&s_satochip_maint_task_done, true, __ATOMIC_RELEASE);
  if (delete_with_caps) {
    vTaskDeleteWithCaps(NULL);
    return;
  }
  vTaskDelete(NULL);
}

static void satochip_maint_finish_ui(void) {
  if (s_satochip_maint_progress_dialog) {
    lv_obj_del(s_satochip_maint_progress_dialog);
    s_satochip_maint_progress_dialog = NULL;
  }
  s_satochip_maint_task_handle = NULL;
  s_satochip_maint_task_with_caps = false;
  (void)krux_shell_show_screen(s_current_screen_id);
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
    dialog_show_error("智能卡忙", NULL, 1600);
    return;
  }

  if (satochip_maint_mode_requires_pin(s_satochip_maint_mode)) {
    if (!s_satochip_maint_input.textarea) {
      dialog_show_error("页面未准备好，请返回重进。", NULL, 1600);
      return;
    }
    satochip_maint_copy_text(s_satochip_maint_pin, sizeof(s_satochip_maint_pin),
                             s_satochip_maint_input.textarea);
    if (s_satochip_maint_pin[0] == '\0') {
      dialog_show_error("请输入 PIN", NULL, 1600);
      return;
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
    satochip_maint_copy_text(s_satochip_maint_text_c,
                             sizeof(s_satochip_maint_text_c),
                             s_satochip_maint_extra_b);
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
    break;
  case SATOCHIP_MAINT_FEATURE_POLICY:
    satochip_maint_copy_text(s_satochip_maint_text_a,
                             sizeof(s_satochip_maint_text_a),
                             s_satochip_maint_extra_a);
    satochip_maint_copy_text(s_satochip_maint_text_b,
                             sizeof(s_satochip_maint_text_b),
                             s_satochip_maint_extra_b);
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
    satochip_maint_copy_text(s_satochip_maint_text_b,
                             sizeof(s_satochip_maint_text_b),
                             s_satochip_maint_extra_b);
    break;
  case SATOCHIP_MAINT_SEEDKEEPER_VIEW_MNEMONIC:
  case SATOCHIP_MAINT_SEEDKEEPER_LOAD_MNEMONIC:
    satochip_maint_copy_text(s_satochip_maint_text_a,
                             sizeof(s_satochip_maint_text_a),
                             s_satochip_maint_extra_a);
    break;
  case SATOCHIP_MAINT_SEEDKEEPER_STATUS:
  case SATOCHIP_MAINT_SEEDKEEPER_FREE_SPACE:
  case SATOCHIP_MAINT_SEEDKEEPER_LIST:
  case SATOCHIP_MAINT_SEEDKEEPER_LOGS:
    satochip_maint_copy_text(s_satochip_maint_pin,
                             sizeof(s_satochip_maint_pin),
                             s_satochip_maint_input.textarea);
    break;
  case SATOCHIP_MAINT_SEEDKEEPER_SAVE_PASSWORD:
  case SATOCHIP_MAINT_SEEDKEEPER_LOAD_DESCRIPTOR:
  case SATOCHIP_MAINT_SEEDKEEPER_SAVE_DESCRIPTOR:
  case SATOCHIP_MAINT_SEEDKEEPER_CLONE:
  case SATOCHIP_MAINT_SEEDKEEPER_RESET:
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
  case SATOCHIP_MAINT_NONE:
  default:
    break;
  }

  s_satochip_maint_result[0] = '\0';
  __atomic_store_n(&s_satochip_maint_task_done, false, __ATOMIC_RELEASE);
  s_satochip_maint_task_err = ESP_ERR_INVALID_STATE;
  s_satochip_maint_task_with_caps = false;

  s_satochip_maint_progress_dialog =
      dialog_show_progress("处理智能卡", "处理中",
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
    dialog_show_error("智能卡任务启动失败", NULL, 0);
    return;
  }

  s_satochip_maint_poll_timer = lv_timer_create(satochip_maint_poll_cb, 100, NULL);
}

static void satochip_maint_start_event_cb(lv_event_t *event) {
  (void)event;
  satochip_maint_start();
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
  create_text(panel, "结果", false, highlight_color());
  if (s_satochip_maint_result[0])
    create_text(panel, s_satochip_maint_result, false, main_color());
  else
    create_text(panel, "等待", false, secondary_color());
  create_action_button(panel,
                       s_satochip_maint_result[0] ? "重新执行" : "执行",
                       satochip_maint_start_event_cb, NULL, true);
  return panel;
}

static void create_satochip_change_pin_block(lv_obj_t *parent,
                                             const krux_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_CHANGE_PIN);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? feature->title : "改 PIN", true, main_color());
  satochip_maint_attach_primary_input(panel, "旧 PIN", true, false, 64, "");
  create_text(panel, "新 PIN", false, highlight_color());
  satochip_maint_attach_extra_field(panel, NULL, "新 PIN", "", true, false, 64,
                                    &s_satochip_maint_extra_a);
  create_text(panel, "PIN 编号", false, highlight_color());
  satochip_maint_attach_extra_field(panel, NULL, "0", "0", false, false, 16,
                                    &s_satochip_maint_extra_b);
  satochip_maint_create_result_panel(parent, "执行结果");
}

static void create_satochip_seedkeeper_change_pin_block(
    lv_obj_t *parent, const krux_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_SEEDKEEPER_CHANGE_PIN);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? feature->title : "改 PIN", true, main_color());
  satochip_maint_attach_primary_input(panel, "旧 PIN", true, false, 64, "");
  create_text(panel, "新 PIN", false, highlight_color());
  satochip_maint_attach_extra_field(panel, NULL, "新 PIN", "", true, false, 64,
                                    &s_satochip_maint_extra_a);
  create_text(panel, "PIN 编号", false, highlight_color());
  satochip_maint_attach_extra_field(panel, NULL, "0", "0", false, false, 16,
                                    &s_satochip_maint_extra_b);
  satochip_maint_create_result_panel(parent, "执行结果");
}

static void create_satochip_unblock_pin_block(lv_obj_t *parent,
                                              const krux_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_UNBLOCK_PIN);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? feature->title : "解锁", true, main_color());
  satochip_maint_attach_primary_input(panel, "PUK", true, false, 64, "");
  satochip_maint_create_result_panel(parent, "执行结果");
}

static void create_satochip_set_label_block(lv_obj_t *parent,
                                            const krux_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_SET_LABEL);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? feature->title : "改标签", true, main_color());
  satochip_maint_attach_primary_input(panel, "PIN", true, false, 64, "");
  satochip_maint_attach_extra_field(panel, "标签", "输入标签", "", false,
                                    false, 64, &s_satochip_maint_extra_a);
  satochip_maint_create_result_panel(parent, "执行结果");
}

static void create_satochip_nfc_policy_block(lv_obj_t *parent,
                                             const krux_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_NFC_POLICY);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? feature->title : "NFC 策略", true,
              main_color());
  satochip_maint_attach_primary_input(panel, "PIN", true, false, 64, "");
  satochip_maint_attach_extra_field(panel, "策略", "0-255", "0", false, false,
                                    16, &s_satochip_maint_extra_a);
  satochip_maint_create_result_panel(parent, "执行结果");
}

static void create_satochip_feature_policy_block(lv_obj_t *parent,
                                                 const krux_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_FEATURE_POLICY);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? feature->title : "功能策略", true,
              main_color());
  satochip_maint_attach_primary_input(panel, "PIN", true, false, 64, "");
  satochip_maint_attach_extra_field(panel, "功能号", "0-255", "0", false,
                                    false, 16, &s_satochip_maint_extra_a);
  satochip_maint_attach_extra_field(panel, "策略", "0-255", "0", false, false,
                                    16, &s_satochip_maint_extra_b);
  satochip_maint_create_result_panel(parent, "执行结果");
}

static void create_satochip_export_authentikey_block(
    lv_obj_t *parent, const krux_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_EXPORT_AUTHENTIKEY);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? feature->title : "导主钥", true, main_color());
  satochip_maint_create_result_panel(parent, "执行结果");
}

static void create_satochip_import_ndef_authentikey_block(
    lv_obj_t *parent, const krux_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_IMPORT_NDEF_AUTHENTIKEY);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? feature->title : "写NDEF", true, main_color());
  satochip_maint_attach_primary_input(panel, "32 字节私钥", false, false, 128,
                                      "");
  satochip_maint_create_result_panel(parent, "执行结果");
}

static void create_satochip_import_trusted_pubkey_block(
    lv_obj_t *parent, const krux_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_IMPORT_TRUSTED_PUBKEY);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? feature->title : "写信任钥", true, main_color());
  satochip_maint_attach_primary_input(panel, "65 字节公钥", false, false, 256,
                                      "");
  satochip_maint_create_result_panel(parent, "执行结果");
}

static void create_satochip_export_trusted_pubkey_block(
    lv_obj_t *parent, const krux_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_EXPORT_TRUSTED_PUBKEY);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? feature->title : "导信任钥", true, main_color());
  satochip_maint_create_result_panel(parent, "执行结果");
}

static void create_satochip_set_2fa_key_block(lv_obj_t *parent,
                                               const krux_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_SET_2FA_KEY);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? feature->title : "设2FA", true, main_color());
  satochip_maint_attach_primary_input(panel, "20 字节 key", false, false, 128,
                                      "");
  satochip_maint_attach_extra_field(panel, "限额", "十进制", "0", false, false,
                                    32, &s_satochip_maint_extra_a);
  satochip_maint_create_result_panel(parent, "执行结果");
}

static void create_satochip_reset_2fa_key_block(
    lv_obj_t *parent, const krux_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_RESET_2FA_KEY);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? feature->title : "清2FA", true, main_color());
  satochip_maint_attach_primary_input(panel, "20 字节挑战", false, false, 128,
                                      "");
  satochip_maint_create_result_panel(parent, "执行结果");
}

static void create_satochip_logout_all_block(lv_obj_t *parent,
                                             const krux_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_LOGOUT_ALL);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? feature->title : "登出", true, main_color());
  satochip_maint_create_result_panel(parent, "执行结果");
}

static void create_satochip_seedkeeper_status_block(
    lv_obj_t *parent, const krux_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_SEEDKEEPER_STATUS);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? feature->title : "状态", true, main_color());
  satochip_maint_attach_primary_input(panel, "卡 PIN（可空）", true, false, 64,
                                      "");
  satochip_maint_create_result_panel(parent, "状态结果");
}

static void create_satochip_seedkeeper_free_space_block(
    lv_obj_t *parent, const krux_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_SEEDKEEPER_FREE_SPACE);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? feature->title : "剩余空间", true, main_color());
  satochip_maint_attach_primary_input(panel, "卡 PIN（可空）", true, false, 64,
                                      "");
  satochip_maint_create_result_panel(parent, "空间结果");
}

static void create_satochip_seedkeeper_list_block(
    lv_obj_t *parent, const krux_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_SEEDKEEPER_LIST);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? feature->title : "列表", true, main_color());
  satochip_maint_attach_primary_input(panel, "卡 PIN（可空）", true, false, 64,
                                      "");
  satochip_maint_create_result_panel(parent, "列表结果");
}

static void create_satochip_seedkeeper_logs_block(
    lv_obj_t *parent, const krux_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_SEEDKEEPER_LOGS);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? feature->title : "日志", true, main_color());
  satochip_maint_create_result_panel(parent, "日志结果");
}

static void create_satochip_seedkeeper_stub_block(
    lv_obj_t *parent, const krux_feature_t *feature, satochip_maint_mode_t mode,
    const char *primary_placeholder, bool primary_password,
    const char *extra_a_label, const char *extra_a_placeholder,
    const char *extra_b_label, const char *extra_b_placeholder) {
  satochip_maint_prepare(mode);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? feature->title : satochip_seedkeeper_stub_title(mode),
              true, main_color());
  satochip_maint_attach_primary_input(
      panel, primary_placeholder ? primary_placeholder : "输入", primary_password,
      false, 256, "");
  if (extra_a_label || extra_a_placeholder)
    satochip_maint_attach_extra_field(
        panel, extra_a_label, extra_a_placeholder ? extra_a_placeholder : "",
        "", false, false, 256, &s_satochip_maint_extra_a);
  if (extra_b_label || extra_b_placeholder)
    satochip_maint_attach_extra_field(
        panel, extra_b_label, extra_b_placeholder ? extra_b_placeholder : "",
        "", false, false, 256, &s_satochip_maint_extra_b);
  satochip_maint_create_result_panel(parent, "执行结果");
}

static void create_satochip_reset_seed_block(lv_obj_t *parent,
                                             const krux_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_RESET_SEED);
  lv_obj_t *panel =
      create_panel(parent, error_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? feature->title : "重置种子", true, error_color());
  satochip_maint_attach_primary_input(panel, "PIN", true, false, 64, "");
  satochip_maint_attach_extra_field(panel, "HMAC", "十六进制，可空", "",
                                    false, false, 128, &s_satochip_maint_extra_a);
  satochip_maint_create_result_panel(parent, "执行结果");
}

static void create_satochip_reset_factory_block(lv_obj_t *parent,
                                                const krux_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_RESET_FACTORY);
  lv_obj_t *panel =
      create_panel(parent, error_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? feature->title : "出厂", true, error_color());
  satochip_maint_create_result_panel(parent, "执行结果");
}

static void create_satochip_seedkeeper_create_mnemonic_block(
    lv_obj_t *parent, const krux_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_SEEDKEEPER_CREATE_MNEMONIC);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? feature->title : "智能卡创建", true,
              main_color());
  satochip_maint_attach_primary_input(panel, "卡 PIN", true, false, 64, "");
  satochip_maint_attach_extra_field(panel, "词数", "12/15/18/21/24", "12",
                                    false, false, 8,
                                    &s_satochip_maint_extra_a);
  satochip_maint_attach_extra_field(panel, "标签", "BIP39-RNG-Kern",
                                    "BIP39-RNG-Kern", false, false, 80,
                                    &s_satochip_maint_extra_b);
  satochip_maint_create_result_panel(parent, "创建结果");
}

static void create_satochip_seedkeeper_write_mnemonic_block(
    lv_obj_t *parent, const krux_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_SEEDKEEPER_WRITE_MNEMONIC);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? feature->title : "写入卡", true, main_color());
  satochip_maint_attach_primary_input(panel, "卡 PIN", true, false, 64, "");
  satochip_maint_attach_extra_field(panel, "标签", "Kern 助记词", "Kern 助记词",
                                    false, false, 80,
                                    &s_satochip_maint_extra_a);
  satochip_maint_attach_extra_field(panel, "密语", "可留空", "", true, false,
                                    128, &s_satochip_maint_extra_b);
  satochip_maint_create_result_panel(parent, "执行结果");
}

static void create_satochip_seedkeeper_view_mnemonic_block(
    lv_obj_t *parent, const krux_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_SEEDKEEPER_VIEW_MNEMONIC);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? feature->title : "查看卡", true, main_color());
  satochip_maint_attach_primary_input(panel, "卡 PIN", true, false, 64, "");
  satochip_maint_attach_extra_field(panel, "SID", "必填", "", false, false, 16,
                                    &s_satochip_maint_extra_a);
  satochip_maint_create_result_panel(parent, "查看结果");
}

static void create_satochip_seedkeeper_load_mnemonic_block(
    lv_obj_t *parent, const krux_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_SEEDKEEPER_LOAD_MNEMONIC);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? feature->title : "卡导入", true, main_color());
  satochip_maint_attach_primary_input(panel, "卡 PIN", true, false, 64, "");
  satochip_maint_attach_extra_field(panel, "SID", "必填", "", false, false, 16,
                                    &s_satochip_maint_extra_a);
  satochip_maint_create_result_panel(parent, "加载结果");
}

static void create_satochip_seedkeeper_save_password_block(
    lv_obj_t *parent, const krux_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_SEEDKEEPER_SAVE_PASSWORD);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? feature->title : "存密码", true,
              main_color());
  satochip_maint_attach_primary_input(panel, "卡 PIN", true, false, 64, "");
  satochip_maint_attach_extra_field(panel, "标签", "登录 / 应用名", "", false,
                                    false, 80, &s_satochip_maint_extra_a);
  satochip_maint_attach_extra_field(panel, "密码", "要保存的文本", "", true,
                                    false, 512, &s_satochip_maint_extra_b);
  satochip_maint_create_result_panel(parent, "执行结果");
}

static void create_satochip_seedkeeper_save_descriptor_block(
    lv_obj_t *parent, const krux_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_SEEDKEEPER_SAVE_DESCRIPTOR);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? feature->title : "存描述符", true,
              main_color());
  satochip_maint_attach_primary_input(panel, "卡 PIN", true, false, 64, "");
  satochip_maint_attach_extra_field(panel, "标签", "钱包描述符", "", false,
                                    false, 80, &s_satochip_maint_extra_a);
  satochip_maint_attach_extra_field(panel, "描述符", "留空保存当前钱包", "",
                                    false, true, 4096,
                                    &s_satochip_maint_extra_b);
  satochip_maint_create_result_panel(parent, "执行结果");
}

static void create_satochip_certificate_export_block(
    lv_obj_t *parent, const krux_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_CERT_EXPORT);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? feature->title : "证书导出", true,
              main_color());
  satochip_maint_create_result_panel(parent, "导出结果");
}

static void create_satochip_certificate_import_block(
    lv_obj_t *parent, const krux_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_CERT_IMPORT);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? feature->title : "证书导入", true,
              main_color());
  satochip_maint_attach_primary_input(panel, "证书 PEM", false, true, 4096,
                                      "");
  if (s_satochip_maint_input.textarea)
    lv_obj_set_height(s_satochip_maint_input.textarea, 160);
  satochip_maint_create_result_panel(parent, "导入结果");
}

static void create_satochip_authenticity_block(lv_obj_t *parent,
                                               const krux_feature_t *feature) {
  satochip_maint_prepare(SATOCHIP_MAINT_AUTHENTICITY);
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(14, theme_get_small_padding()));
  create_text(panel, feature ? feature->title : "真伪检查", true,
              main_color());
  satochip_maint_create_result_panel(parent, "检查结果");
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
    "相机检查",
    "",
    "打开相机检查",
    "设备检查：相机",
    "",
    true};

static const camera_preview_action_t CAMERA_ACTION_SEED_QR = {
    "助记词扫码",
    "",
    "打开扫码",
    "助记词扫码",
    "",
    false};

static const camera_preview_action_t CAMERA_ACTION_SIGN_QR = {
    "交易扫码",
    "",
    "打开扫码",
    "交易扫码",
    "",
    false};

static const camera_preview_action_t CAMERA_ACTION_TOOLS_QR = {
    "二维码识别",
    "",
    "打开扫码",
    "二维码识别",
    "",
    true};

static const camera_preview_action_t CAMERA_ACTION_ENTROPY = {
    "相机随机",
    "",
    "打开相机",
    "相机随机",
    "",
    false};

static const camera_preview_action_t CAMERA_ACTION_ADDRESS = {
    "地址扫码",
    "",
    "打开扫码",
    "地址扫码",
    "",
    true};

static void camera_preview_event_cb(lv_event_t *event) {
  const camera_preview_action_t *action =
      (const camera_preview_action_t *)lv_event_get_user_data(event);
  if (!action)
    action = &CAMERA_ACTION_TEST;

  (void)krux_camera_preview_open_ex(action->preview_title, action->notice,
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
  snprintf(text, sizeof(text), "触摸通过：第 %lu 次\n坐标：X=%ld，Y=%ld",
           (unsigned long)s_touch_count, (long)point.x, (long)point.y);
  lv_label_set_text(label, text);
}

static void create_touch_probe_block(lv_obj_t *parent) {
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(12, theme_get_small_padding()));
  create_text(panel, "触摸检查", false, highlight_color());

  lv_obj_t *result = create_text(panel, "等待", true, main_color());

  lv_obj_t *pad = lv_obj_create(panel);
  lv_obj_set_width(pad, LV_PCT(100));
  lv_obj_set_height(pad, 220);
  lv_obj_set_style_bg_color(pad, bg_color(), 0);
  lv_obj_set_style_bg_opa(pad, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(pad, highlight_color(), 0);
  lv_obj_set_style_border_width(pad, 2, 0);
  lv_obj_set_style_radius(pad, 18, 0);
  lv_obj_add_flag(pad, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(pad, touch_probe_event_cb, LV_EVENT_PRESSED, result);
  lv_obj_add_event_cb(pad, touch_probe_event_cb, LV_EVENT_PRESSING, result);

  lv_obj_t *pad_label = lv_label_create(pad);
  lv_label_set_text(pad_label, "触摸检查区");
  lv_obj_set_style_text_font(pad_label, theme_font_medium(), 0);
  lv_obj_set_style_text_color(pad_label, secondary_color(), 0);
  lv_obj_center(pad_label);
}

static void storage_probe_event_cb(lv_event_t *event) {
  lv_obj_t *label = (lv_obj_t *)lv_event_get_user_data(event);
  if (!label)
    return;

  lv_label_set_text(label, "检查中");
  lv_refr_now(NULL);

  char detail[256];
  esp_err_t ret = krux_hardware_probe_storage_rw(detail, sizeof(detail));
  char text[320];
  snprintf(text, sizeof(text), "%s\n结果：%s",
           ret == ESP_OK ? "存储卡读写检查通过" : "存储卡读写检查失败",
           detail);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, ret == ESP_OK ? yes_color() : error_color(),
                              0);
}

static void create_storage_probe_block(lv_obj_t *parent) {
  lv_obj_t *panel =
      create_panel(parent, highlight_color(), max_i(12, theme_get_small_padding()));
  create_text(panel, "存储卡检查", false, highlight_color());

  lv_obj_t *result = create_text(panel, "等待", true, main_color());
  create_action_button(panel, "开始存储卡检查", storage_probe_event_cb, result,
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
    text = "离线钱包二维码";

  lv_result_t res = lv_qrcode_update(s_qr_preview, text, (uint32_t)strlen(text));
  if (res == LV_RESULT_OK) {
    char status[160];
    snprintf(status, sizeof(status), "二维码已生成：%u 字节",
             (unsigned)strlen(text));
    lv_label_set_text(s_qr_status_label, status);
    lv_obj_set_style_text_color(s_qr_status_label, yes_color(), 0);
  } else {
    lv_label_set_text(s_qr_status_label,
                      "二维码生成失败：内容可能太长，请缩短文本。");
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

  create_text(panel, "文本生成二维码", false, highlight_color());

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
      create_text(panel, "等待", false, secondary_color());

  s_qr_textarea = lv_textarea_create(panel);
  lv_obj_set_width(s_qr_textarea, LV_PCT(100));
  lv_obj_set_height(s_qr_textarea, 74);
  lv_textarea_set_one_line(s_qr_textarea, false);
  lv_textarea_set_max_length(s_qr_textarea, 240);
  lv_textarea_set_placeholder_text(s_qr_textarea, "输入要显示成二维码的文本");
  lv_textarea_set_text(s_qr_textarea, "离线钱包二维码");
  lv_obj_set_style_text_font(s_qr_textarea, theme_font_small(), 0);
  lv_obj_set_style_text_color(s_qr_textarea, main_color(), 0);
  lv_obj_set_style_bg_color(s_qr_textarea, bg_color(), 0);
  lv_obj_set_style_bg_opa(s_qr_textarea, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(s_qr_textarea, highlight_color(), 0);
  lv_obj_set_style_border_width(s_qr_textarea, 2, 0);
  lv_obj_set_style_radius(s_qr_textarea, 12, 0);
#ifndef SIMULATOR
  ui_textarea_enable_safe_keyboard_shortcuts(s_qr_textarea);
#endif

  create_action_button(panel, "生成二维码", qr_text_generate_event_cb, NULL,
                       true);

  lv_obj_t *keyboard = lv_keyboard_create(panel);
  lv_obj_set_width(keyboard, LV_PCT(100));
  lv_obj_set_height(keyboard, 220);
  lv_keyboard_set_textarea(keyboard, s_qr_textarea);
  lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
#ifndef SIMULATOR
  ui_keyboard_apply_safe_text_map(keyboard);
#endif
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

  create_text(panel, title ? title : "二维码显示", false, highlight_color());
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

  const char *data = payload ? payload : "KERN-KRUX-QR";
  lv_result_t res = lv_qrcode_update(qr, data, (uint32_t)strlen(data));
  create_text(panel, res == LV_RESULT_OK ? "二维码编码成功" : "二维码编码失败",
              false, res == LV_RESULT_OK ? yes_color() : error_color());
}

static void create_delivery_acceptance_block(lv_obj_t *parent) {
  lv_obj_t *panel =
      create_panel(parent, yes_color(), max_i(12, theme_get_small_padding()));
  create_text(panel, "钱包检测总览", false, yes_color());

  create_build_identity_block(parent);
  create_hardware_snapshot_block(parent, "硬件快照");

  lv_obj_t *actions =
      create_panel(parent, highlight_color(), max_i(12, theme_get_small_padding()));
  create_text(actions, "设备检查", false, highlight_color());
  create_action_button(actions, "打开扫码", camera_preview_event_cb,
                       (void *)&CAMERA_ACTION_TOOLS_QR, true);
  create_nav_button(actions, "打开文本二维码", "tools_create_qr", false);
  create_nav_button(actions, "打开触摸检查", "test_screen_touch", false);
  create_nav_button(actions, "打开亮度设置", "settings_display", false);
  create_nav_button(actions, "打开钱包", "legacy_login", true);
  create_nav_button(actions, "导入或创建钱包", "legacy_load_wallet", false);
  create_nav_button(actions, "扫码签名", "legacy_scan_sign", false);

  lv_obj_t *storage_result =
      create_text(actions, "存储卡：还没有执行检查。", false, main_color());
  create_action_button(actions, "开始存储卡读写", storage_probe_event_cb,
                       storage_result, false);

  lv_obj_t *files_result =
      create_text(actions, "文件列表：还没有刷新。", false, main_color());
  create_action_button(actions, "刷新存储卡文件", file_list_event_cb,
                       files_result, false);

  lv_obj_t *wallet =
      create_panel(parent, yes_color(), max_i(12, theme_get_small_padding()));
  create_text(wallet, "钱包入口", false, yes_color());
  create_nav_button(wallet, "打开钱包", "legacy_login", true);
}

static bool create_special_detail_cards(lv_obj_t *parent,
                                        const krux_feature_t *feature) {
  if (strcmp(feature->id, "test_screen_touch") == 0) {
    create_touch_probe_block(parent);
    return true;
  }

  if (strcmp(feature->id, "test_storage") == 0) {
    create_hardware_snapshot_block(parent, "硬件快照");
    create_storage_probe_block(parent);
    return true;
  }

  if (strcmp(feature->id, "test_camera") == 0) {
    create_camera_probe_block(parent);
    create_hardware_snapshot_block(parent, "硬件快照");
    return true;
  }

  if (strcmp(feature->id, "smartcard_probe") == 0) {
    create_smartcard_probe_block(parent);
    create_hardware_snapshot_block(parent, "硬件快照");
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
    create_text(panel, "Web3 请求", true, main_color());
#ifndef SIMULATOR
    create_action_button(panel, "打开扫码", smartcard_web3_scan_event_cb, NULL,
                         true);
#else
    create_nav_button(panel, "打开扫码", "legacy_scan_sign", true);
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

  if (strcmp(feature->id, "smartcard_seedkeeper_view_mnemonic") == 0) {
    create_satochip_seedkeeper_view_mnemonic_block(parent, feature);
    return true;
  }

  if (strcmp(feature->id, "smartcard_seedkeeper_load_mnemonic") == 0 ||
      strcmp(feature->id, "load_seedkeeper_mnemonic") == 0) {
    create_satochip_seedkeeper_load_mnemonic_block(parent, feature);
    return true;
  }

  if (strcmp(feature->id, "smartcard_seedkeeper_save_password") == 0) {
    create_satochip_seedkeeper_save_password_block(parent, feature);
    return true;
  }

  if (strcmp(feature->id, "smartcard_seedkeeper_load_descriptor") == 0) {
    create_satochip_seedkeeper_stub_block(
        parent, feature, SATOCHIP_MAINT_SEEDKEEPER_LOAD_DESCRIPTOR, "卡 PIN",
        true, "SID", "必填", NULL, NULL);
    return true;
  }

  if (strcmp(feature->id, "smartcard_seedkeeper_save_descriptor") == 0) {
    create_satochip_seedkeeper_save_descriptor_block(parent, feature);
    return true;
  }

  if (strcmp(feature->id, "smartcard_seedkeeper_clone") == 0) {
    create_satochip_seedkeeper_stub_block(
        parent, feature, SATOCHIP_MAINT_SEEDKEEPER_CLONE, "源卡 PIN", true,
        "SID", "必填", "目标公钥SID", "必填");
    return true;
  }

  if (strcmp(feature->id, "smartcard_seedkeeper_change_pin") == 0) {
    create_satochip_seedkeeper_change_pin_block(parent, feature);
    return true;
  }

  if (strcmp(feature->id, "smartcard_seedkeeper_reset") == 0) {
    create_satochip_seedkeeper_stub_block(
        parent, feature, SATOCHIP_MAINT_SEEDKEEPER_RESET, "卡 PIN", true,
        "SID", "空=出厂", NULL, NULL);
    return true;
  }

  if (strcmp(feature->id, "smartcard_seedkeeper_generate_masterseed") == 0) {
    create_satochip_seedkeeper_stub_block(
        parent, feature, SATOCHIP_MAINT_SEEDKEEPER_GENERATE_MASTERSEED,
        "卡 PIN", true, "长度", "16/32/64", "标签", "Masterseed");
    return true;
  }

  if (strcmp(feature->id, "smartcard_seedkeeper_generate_2fa_secret") == 0) {
    create_satochip_seedkeeper_stub_block(
        parent, feature, SATOCHIP_MAINT_SEEDKEEPER_GENERATE_2FA_SECRET,
        "卡 PIN", true, "标签", "2FA", NULL, NULL);
    return true;
  }

  if (strcmp(feature->id, "smartcard_seedkeeper_derive_master_password") == 0) {
    create_satochip_seedkeeper_stub_block(
        parent, feature, SATOCHIP_MAINT_SEEDKEEPER_DERIVE_MASTER_PASSWORD,
        "卡 PIN", true, "SID", "必填", "盐", "可空");
    return true;
  }

  if (strcmp(feature->id, "smartcard_seedkeeper_reset_secret") == 0) {
    create_satochip_seedkeeper_stub_block(
        parent, feature, SATOCHIP_MAINT_SEEDKEEPER_RESET_SECRET, "卡 PIN", true,
        "SID", "必填", NULL, NULL);
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

  if (strcmp(feature->id, "satochip_path_address") == 0) {
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
    create_hardware_snapshot_block(parent, "硬件快照");
    return true;
  }

  if (strcmp(feature->id, "tools_create_qr") == 0) {
    create_qr_text_tool_block(parent);
    return true;
  }

  if (strcmp(feature->id, "system_overview") == 0) {
    create_build_identity_block(parent);
    create_hardware_snapshot_block(parent, "硬件快照");
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
    create_qr_demo_block(parent, "公开数据二维码",
                         "bitcoin:tb1qexample0000000000000000000000000000000",
                         "");
    return true;
  }

  if (strcmp(feature->id, "backup_seed_qr") == 0) {
    create_qr_demo_block(parent, "助记词二维码", "打开真实二维码", "");
    return true;
  }

  if (strcmp(feature->id, "backup_kef") == 0) {
    create_direct_input_block(parent, feature,
                              "加密当前助记词并保存到存储卡。");
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
                                const krux_feature_t *feature) {
  if (strcmp(feature->id, "load_mnemonic") == 0) {
    create_krux_child_menu(parent, feature);
    return;
  }
  if (strcmp(feature->id, "load_punch_grid") == 0) {
    create_krux_child_menu(parent, feature);
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

  if (feature->risk == KRUX_FEATURE_RISK_SECRET_MATERIAL ||
      feature->risk == KRUX_FEATURE_RISK_SIGNING) {
    create_wallet_entry_block(parent, feature);
  } else {
    create_build_identity_block(parent);
  }
}

static void style_krux_container(lv_obj_t *obj, lv_color_t color) {
  lv_obj_set_style_bg_color(obj, color, 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  lv_obj_set_style_outline_width(obj, 0, 0);
  lv_obj_set_style_shadow_width(obj, 0, 0);
  lv_obj_set_style_radius(obj, 0, 0);
}

static void create_krux_status_bar(lv_obj_t *root,
                                   const krux_feature_t *feature) {
  (void)feature;
  lv_obj_t *bar = lv_obj_create(root);
  lv_obj_set_width(bar, LV_PCT(100));
  lv_obj_set_height(bar, shell_status_height());
  style_krux_container(bar, krux_card_color());
  lv_obj_set_style_pad_all(bar, 0, 0);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *battery = lv_obj_create(bar);
  lv_obj_set_size(battery, 22, 10);
  lv_obj_align(battery, LV_ALIGN_RIGHT_MID, -6, 0);
  lv_obj_set_style_bg_color(battery, krux_card_color(), 0);
  lv_obj_set_style_bg_opa(battery, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(battery, krux_text_color(), 0);
  lv_obj_set_style_border_width(battery, 2, 0);
  lv_obj_set_style_radius(battery, 0, 0);
  lv_obj_set_style_pad_all(battery, 0, 0);

  lv_obj_t *tip = lv_obj_create(bar);
  lv_obj_set_size(tip, 3, 6);
  lv_obj_align_to(tip, battery, LV_ALIGN_OUT_RIGHT_MID, 0, 0);
  lv_obj_set_style_bg_color(tip, krux_text_color(), 0);
  lv_obj_set_style_bg_opa(tip, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(tip, 0, 0);
  lv_obj_set_style_radius(tip, 0, 0);
}

static void create_krux_header(lv_obj_t *root, const krux_feature_t *feature) {
  create_krux_status_bar(root, feature);

  if (!feature || strcmp(feature->id, "home") == 0)
    return;

  lv_obj_t *header = lv_obj_create(root);
  lv_obj_set_width(header, LV_PCT(100));
  lv_obj_set_height(header, LV_SIZE_CONTENT);
  style_krux_container(header, krux_canvas_color());
  lv_obj_set_style_pad_all(header, 0, 0);
  lv_obj_set_style_pad_top(header, shell_header_pad_y(), 0);
  lv_obj_set_style_pad_bottom(header, shell_header_pad_y(), 0);
  lv_obj_set_flex_flow(header, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(header, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  lv_obj_t *title_row = lv_obj_create(header);
  lv_obj_set_width(title_row, LV_PCT(100));
  lv_obj_set_height(title_row, shell_is_wave_43_portrait() ? 54 : 60);
  style_krux_container(title_row, krux_canvas_color());
  lv_obj_set_style_pad_all(title_row, 0, 0);
  lv_obj_set_style_pad_column(title_row, 8, 0);
  lv_obj_set_flex_flow(title_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(title_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  lv_obj_t *back = lv_btn_create(title_row);
  lv_obj_set_size(back, shell_is_wave_43_portrait() ? 92 : 100,
                  shell_is_wave_43_portrait() ? 52 : 54);
  lv_obj_set_style_bg_color(back, krux_card_color(), 0);
  lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(back, highlight_color(), 0);
  lv_obj_set_style_border_width(back, 2, 0);
  lv_obj_set_style_radius(back, shell_card_radius(), 0);
  lv_obj_set_style_shadow_width(back, 0, 0);
  lv_obj_add_event_cb(back, nav_event_cb, LV_EVENT_CLICKED,
                      (void *)shell_back_target_for_feature(feature));

  lv_obj_t *back_label = lv_label_create(back);
  lv_label_set_text(back_label, "返回");
  lv_label_set_long_mode(back_label, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_font(back_label, theme_font_small(), 0);
  lv_obj_set_style_text_color(back_label, main_color(), 0);
  lv_obj_center(back_label);

  lv_obj_t *title = lv_label_create(title_row);
  lv_label_set_text(title, feature->title);
  lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
  lv_obj_set_flex_grow(title, 1);
  lv_obj_set_style_text_font(title, theme_font_medium(), 0);
  lv_obj_set_style_text_color(title, main_color(), 0);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_t *spacer = lv_obj_create(title_row);
  lv_obj_set_size(spacer, shell_is_wave_43_portrait() ? 86 : 96,
                  shell_is_wave_43_portrait() ? 50 : 54);
  style_krux_container(spacer, krux_canvas_color());
  lv_obj_clear_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);

}

static lv_obj_t *create_krux_list(lv_obj_t *root, bool center_items,
                                  bool grid_mode) {
  lv_obj_t *list = lv_obj_create(root);
  lv_obj_set_width(list, LV_PCT(100));
  lv_obj_set_flex_grow(list, 1);
  style_krux_container(list, krux_canvas_color());
  lv_obj_set_style_pad_all(list, 0, 0);
  lv_obj_set_style_pad_row(list, grid_mode ? shell_menu_gap()
                                           : (shell_is_wave_43_portrait() ? 8 : 0),
                           0);
  lv_obj_set_style_pad_column(list, grid_mode ? shell_menu_gap() : 0, 0);
  lv_obj_set_flex_flow(list, grid_mode ? LV_FLEX_FLOW_ROW_WRAP
                                       : LV_FLEX_FLOW_COLUMN);
  bool home_grid = grid_mode && s_rendering_home_grid;
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

static lv_obj_t *create_krux_menu_button(lv_obj_t *parent, const char *label,
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
  lv_obj_set_style_bg_color(btn, krux_card_color(), LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(btn, krux_card_pressed_color(), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(btn, 14, 0);
  lv_obj_set_style_border_width(btn, 1, 0);
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
  lv_obj_set_style_text_color(title, krux_text_color(), 0);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

  return btn;
}

static bool create_krux_override_menu(lv_obj_t *list,
                                      const krux_menu_override_t *items,
                                      size_t count) {
  if (!items || count == 0)
    return false;

  for (size_t i = 0; i < count; i++) {
    create_krux_menu_button(list, items[i].label, NULL, items[i].target_id,
                            true);
  }
  return true;
}

static bool krux_screen_has_override_menu(const char *id) {
  return id && (strcmp(id, "home") == 0 || strcmp(id, "load_mnemonic") == 0 ||
                strcmp(id, "new_mnemonic") == 0 ||
                strcmp(id, "tools") == 0 ||
                strcmp(id, "settings") == 0 ||
                strcmp(id, "wallet_home") == 0 ||
                strcmp(id, "signing") == 0 ||
                krux_sign_wallet_group_id(id) ||
                strcmp(id, "satochip_btc_pubkeys") == 0 ||
                strcmp(id, "smartcard_tools") == 0 ||
                krux_smartcard_tools_group_id(id) ||
                krux_smartcard_seedkeeper_group_id(id) ||
                krux_smartcard_certificate_group_id(id) ||
                is_connect_wallet_group_menu(id) ||
                strcmp(id, "backup_export") == 0 ||
                strcmp(id, "addresses") == 0 ||
                strcmp(id, "device_tests") == 0 ||
                strcmp(id, "pi_mnemonic_tools") == 0 ||
                strcmp(id, "pi_mnemonic_advanced") == 0 ||
                strcmp(id, "pi_connect_wallet") == 0 ||
                strcmp(id, "pi_self_check") == 0);
}

static size_t krux_override_menu_count(const char *id) {
  if (!id)
    return 0;
  if (strcmp(id, "home") == 0)
    return sizeof(KRUX_HOME_MENU) / sizeof(KRUX_HOME_MENU[0]);
  if (strcmp(id, "load_mnemonic") == 0)
    return sizeof(KRUX_LOAD_MENU) / sizeof(KRUX_LOAD_MENU[0]);
  if (strcmp(id, "new_mnemonic") == 0)
    return sizeof(KRUX_NEW_MENU) / sizeof(KRUX_NEW_MENU[0]);
  if (strcmp(id, "tools") == 0)
    return sizeof(KRUX_TOOLS_MENU) / sizeof(KRUX_TOOLS_MENU[0]);
  if (strcmp(id, "settings") == 0)
    return sizeof(KRUX_SETTINGS_MENU) / sizeof(KRUX_SETTINGS_MENU[0]);
  if (strcmp(id, "wallet_home") == 0)
    return sizeof(KRUX_WALLET_MENU) / sizeof(KRUX_WALLET_MENU[0]);
  if (strcmp(id, "signing") == 0)
    return sizeof(KRUX_SIGNING_MENU) / sizeof(KRUX_SIGNING_MENU[0]);
  if (strcmp(id, "sign_okx") == 0)
    return sizeof(KRUX_SIGN_OKX_MENU) / sizeof(KRUX_SIGN_OKX_MENU[0]);
  if (strcmp(id, "sign_bitget") == 0)
    return sizeof(KRUX_SIGN_BITGET_MENU) / sizeof(KRUX_SIGN_BITGET_MENU[0]);
  if (strcmp(id, "sign_metamask") == 0)
    return sizeof(KRUX_SIGN_METAMASK_MENU) /
           sizeof(KRUX_SIGN_METAMASK_MENU[0]);
  if (strcmp(id, "sign_rabby") == 0)
    return sizeof(KRUX_SIGN_RABBY_MENU) / sizeof(KRUX_SIGN_RABBY_MENU[0]);
  if (strcmp(id, "sign_tokenpocket") == 0)
    return sizeof(KRUX_SIGN_TOKENPOCKET_MENU) /
           sizeof(KRUX_SIGN_TOKENPOCKET_MENU[0]);
  if (strcmp(id, "sign_btc") == 0)
    return sizeof(KRUX_SIGN_BTC_MENU) / sizeof(KRUX_SIGN_BTC_MENU[0]);
  if (strcmp(id, "satochip_btc_pubkeys") == 0)
    return sizeof(KRUX_SATOCHIP_PUBKEY_MENU) /
           sizeof(KRUX_SATOCHIP_PUBKEY_MENU[0]);
  if (strcmp(id, "connect_web3") == 0)
    return sizeof(KRUX_CONNECT_WEB3_MENU) /
           sizeof(KRUX_CONNECT_WEB3_MENU[0]);
  if (strcmp(id, "connect_okx") == 0)
    return sizeof(KRUX_CONNECT_OKX_MENU) / sizeof(KRUX_CONNECT_OKX_MENU[0]);
  if (strcmp(id, "connect_bitget") == 0)
    return sizeof(KRUX_CONNECT_BITGET_MENU) /
           sizeof(KRUX_CONNECT_BITGET_MENU[0]);
  if (strcmp(id, "connect_metamask") == 0)
    return sizeof(KRUX_CONNECT_METAMASK_MENU) /
           sizeof(KRUX_CONNECT_METAMASK_MENU[0]);
  if (strcmp(id, "connect_rabby") == 0)
    return sizeof(KRUX_CONNECT_RABBY_MENU) /
           sizeof(KRUX_CONNECT_RABBY_MENU[0]);
  if (strcmp(id, "connect_tokenpocket") == 0)
    return sizeof(KRUX_CONNECT_TOKENPOCKET_MENU) /
           sizeof(KRUX_CONNECT_TOKENPOCKET_MENU[0]);
  if (strcmp(id, "connect_address") == 0)
    return sizeof(KRUX_CONNECT_ADDRESS_MENU) /
           sizeof(KRUX_CONNECT_ADDRESS_MENU[0]);
  if (strcmp(id, "btc_wallet") == 0)
    return sizeof(KRUX_BTC_WALLET_MENU) / sizeof(KRUX_BTC_WALLET_MENU[0]);
  if (strcmp(id, "btc_mnemonic") == 0)
    return sizeof(KRUX_BTC_MNEMONIC_MENU) / sizeof(KRUX_BTC_MNEMONIC_MENU[0]);
  if (strcmp(id, "btc_satochip") == 0)
    return sizeof(KRUX_BTC_SATOCHIP_MENU) /
           sizeof(KRUX_BTC_SATOCHIP_MENU[0]);
  if (strcmp(id, "backup_export") == 0)
    return sizeof(KRUX_BACKUP_MENU) / sizeof(KRUX_BACKUP_MENU[0]);
  if (strcmp(id, "addresses") == 0)
    return sizeof(KRUX_ADDRESSES_MENU) / sizeof(KRUX_ADDRESSES_MENU[0]);
  if (strcmp(id, "device_tests") == 0)
    return sizeof(KRUX_DEVICE_TESTS_MENU) / sizeof(KRUX_DEVICE_TESTS_MENU[0]);
  if (strcmp(id, "smartcard_tools") == 0)
    return sizeof(KRUX_SMARTCARD_MENU) / sizeof(KRUX_SMARTCARD_MENU[0]);
  if (strcmp(id, "smartcard_satochip_tools") == 0)
    return sizeof(KRUX_SMARTCARD_SATOCHIP_MENU) /
           sizeof(KRUX_SMARTCARD_SATOCHIP_MENU[0]);
  if (strcmp(id, "smartcard_satochip_maint") == 0)
    return sizeof(KRUX_SMARTCARD_MAINT_MENU) /
           sizeof(KRUX_SMARTCARD_MAINT_MENU[0]);
  if (strcmp(id, "smartcard_satochip_advanced_tools") == 0)
    return sizeof(KRUX_SMARTCARD_ADVANCED_MENU) /
           sizeof(KRUX_SMARTCARD_ADVANCED_MENU[0]);
  if (strcmp(id, "smartcard_satochip_pubkey_tools") == 0)
    return sizeof(KRUX_SMARTCARD_PUBKEY_MENU) /
           sizeof(KRUX_SMARTCARD_PUBKEY_MENU[0]);
  if (strcmp(id, "smartcard_satochip_2fa_tools") == 0)
    return sizeof(KRUX_SMARTCARD_2FA_MENU) / sizeof(KRUX_SMARTCARD_2FA_MENU[0]);
  if (strcmp(id, "smartcard_satochip_session_tools") == 0)
    return sizeof(KRUX_SMARTCARD_SESSION_MENU) /
           sizeof(KRUX_SMARTCARD_SESSION_MENU[0]);
  if (strcmp(id, "smartcard_seedkeeper_advanced_tools") == 0)
    return sizeof(KRUX_SMARTCARD_SEEDKEEPER_ADVANCED_MENU) /
           sizeof(KRUX_SMARTCARD_SEEDKEEPER_ADVANCED_MENU[0]);
  if (krux_smartcard_seedkeeper_group_id(id))
    return sizeof(KRUX_SMARTCARD_SEEDKEEPER_MENU) /
           sizeof(KRUX_SMARTCARD_SEEDKEEPER_MENU[0]);
  if (krux_smartcard_certificate_group_id(id))
    return sizeof(KRUX_SMARTCARD_CERTIFICATE_MENU) /
           sizeof(KRUX_SMARTCARD_CERTIFICATE_MENU[0]);
  if (strcmp(id, "pi_mnemonic_tools") == 0)
    return sizeof(KRUX_PI_MNEMONIC_MENU) / sizeof(KRUX_PI_MNEMONIC_MENU[0]);
  if (strcmp(id, "pi_mnemonic_advanced") == 0)
    return sizeof(KRUX_PI_MNEMONIC_ADVANCED_MENU) /
           sizeof(KRUX_PI_MNEMONIC_ADVANCED_MENU[0]);
  if (strcmp(id, "pi_connect_wallet") == 0)
    return sizeof(KRUX_PI_CONNECT_MENU) / sizeof(KRUX_PI_CONNECT_MENU[0]);
  if (strcmp(id, "pi_self_check") == 0)
    return sizeof(KRUX_PI_SELF_CHECK_MENU) / sizeof(KRUX_PI_SELF_CHECK_MENU[0]);
  return 0;
}

static void create_krux_home_menu(lv_obj_t *list) {
  (void)KRUX_HOME_MENU_IDS;
  for (size_t i = 0; i < sizeof(KRUX_HOME_MENU) / sizeof(KRUX_HOME_MENU[0]); i++) {
    create_krux_menu_button(list, KRUX_HOME_MENU[i].label, NULL,
                            KRUX_HOME_MENU[i].target_id, true);
  }
}

static void create_krux_child_menu(lv_obj_t *list,
                                   const krux_feature_t *feature) {
  if (strcmp(feature->id, "load_mnemonic") == 0 &&
      create_krux_override_menu(
          list, KRUX_LOAD_MENU,
          sizeof(KRUX_LOAD_MENU) / sizeof(KRUX_LOAD_MENU[0])))
    return;
  if (strcmp(feature->id, "new_mnemonic") == 0 &&
      create_krux_override_menu(list, KRUX_NEW_MENU,
                                sizeof(KRUX_NEW_MENU) / sizeof(KRUX_NEW_MENU[0])))
    return;
  if (strcmp(feature->id, "tools") == 0 &&
      create_krux_override_menu(
          list, KRUX_TOOLS_MENU,
          sizeof(KRUX_TOOLS_MENU) / sizeof(KRUX_TOOLS_MENU[0])))
    return;
  if (strcmp(feature->id, "settings") == 0 &&
      create_krux_override_menu(
          list, KRUX_SETTINGS_MENU,
          sizeof(KRUX_SETTINGS_MENU) / sizeof(KRUX_SETTINGS_MENU[0])))
    return;
  if (strcmp(feature->id, "wallet_home") == 0 &&
      create_krux_override_menu(
          list, KRUX_WALLET_MENU,
          sizeof(KRUX_WALLET_MENU) / sizeof(KRUX_WALLET_MENU[0])))
    return;
  if (strcmp(feature->id, "signing") == 0 &&
      create_krux_override_menu(
          list, KRUX_SIGNING_MENU,
          sizeof(KRUX_SIGNING_MENU) / sizeof(KRUX_SIGNING_MENU[0])))
    return;
  if (strcmp(feature->id, "sign_okx") == 0 &&
      create_krux_override_menu(
          list, KRUX_SIGN_OKX_MENU,
          sizeof(KRUX_SIGN_OKX_MENU) / sizeof(KRUX_SIGN_OKX_MENU[0])))
    return;
  if (strcmp(feature->id, "sign_bitget") == 0 &&
      create_krux_override_menu(
          list, KRUX_SIGN_BITGET_MENU,
          sizeof(KRUX_SIGN_BITGET_MENU) / sizeof(KRUX_SIGN_BITGET_MENU[0])))
    return;
  if (strcmp(feature->id, "sign_metamask") == 0 &&
      create_krux_override_menu(
          list, KRUX_SIGN_METAMASK_MENU,
          sizeof(KRUX_SIGN_METAMASK_MENU) /
              sizeof(KRUX_SIGN_METAMASK_MENU[0])))
    return;
  if (strcmp(feature->id, "sign_rabby") == 0 &&
      create_krux_override_menu(
          list, KRUX_SIGN_RABBY_MENU,
          sizeof(KRUX_SIGN_RABBY_MENU) / sizeof(KRUX_SIGN_RABBY_MENU[0])))
    return;
  if (strcmp(feature->id, "sign_tokenpocket") == 0 &&
      create_krux_override_menu(
          list, KRUX_SIGN_TOKENPOCKET_MENU,
          sizeof(KRUX_SIGN_TOKENPOCKET_MENU) /
              sizeof(KRUX_SIGN_TOKENPOCKET_MENU[0])))
    return;
  if (strcmp(feature->id, "sign_btc") == 0 &&
      create_krux_override_menu(
          list, KRUX_SIGN_BTC_MENU,
          sizeof(KRUX_SIGN_BTC_MENU) / sizeof(KRUX_SIGN_BTC_MENU[0])))
    return;
  if (strcmp(feature->id, "satochip_btc_pubkeys") == 0 &&
      create_krux_override_menu(
          list, KRUX_SATOCHIP_PUBKEY_MENU,
          sizeof(KRUX_SATOCHIP_PUBKEY_MENU) /
              sizeof(KRUX_SATOCHIP_PUBKEY_MENU[0])))
    return;
  if (strcmp(feature->id, "connect_web3") == 0 &&
      create_krux_override_menu(
          list, KRUX_CONNECT_WEB3_MENU,
          sizeof(KRUX_CONNECT_WEB3_MENU) / sizeof(KRUX_CONNECT_WEB3_MENU[0])))
    return;
  if (strcmp(feature->id, "connect_okx") == 0 &&
      create_krux_override_menu(
          list, KRUX_CONNECT_OKX_MENU,
          sizeof(KRUX_CONNECT_OKX_MENU) / sizeof(KRUX_CONNECT_OKX_MENU[0])))
    return;
  if (strcmp(feature->id, "connect_bitget") == 0 &&
      create_krux_override_menu(
          list, KRUX_CONNECT_BITGET_MENU,
          sizeof(KRUX_CONNECT_BITGET_MENU) /
              sizeof(KRUX_CONNECT_BITGET_MENU[0])))
    return;
  if (strcmp(feature->id, "connect_metamask") == 0 &&
      create_krux_override_menu(
          list, KRUX_CONNECT_METAMASK_MENU,
          sizeof(KRUX_CONNECT_METAMASK_MENU) /
              sizeof(KRUX_CONNECT_METAMASK_MENU[0])))
    return;
  if (strcmp(feature->id, "connect_rabby") == 0 &&
      create_krux_override_menu(
          list, KRUX_CONNECT_RABBY_MENU,
          sizeof(KRUX_CONNECT_RABBY_MENU) /
              sizeof(KRUX_CONNECT_RABBY_MENU[0])))
    return;
  if (strcmp(feature->id, "connect_tokenpocket") == 0 &&
      create_krux_override_menu(
          list, KRUX_CONNECT_TOKENPOCKET_MENU,
          sizeof(KRUX_CONNECT_TOKENPOCKET_MENU) /
              sizeof(KRUX_CONNECT_TOKENPOCKET_MENU[0])))
    return;
  if (strcmp(feature->id, "connect_address") == 0 &&
      create_krux_override_menu(
          list, KRUX_CONNECT_ADDRESS_MENU,
          sizeof(KRUX_CONNECT_ADDRESS_MENU) /
              sizeof(KRUX_CONNECT_ADDRESS_MENU[0])))
    return;
  if (strcmp(feature->id, "btc_wallet") == 0 &&
      create_krux_override_menu(
          list, KRUX_BTC_WALLET_MENU,
          sizeof(KRUX_BTC_WALLET_MENU) / sizeof(KRUX_BTC_WALLET_MENU[0])))
    return;
  if (strcmp(feature->id, "btc_mnemonic") == 0 &&
      create_krux_override_menu(
          list, KRUX_BTC_MNEMONIC_MENU,
          sizeof(KRUX_BTC_MNEMONIC_MENU) /
              sizeof(KRUX_BTC_MNEMONIC_MENU[0])))
    return;
  if (strcmp(feature->id, "btc_satochip") == 0 &&
      create_krux_override_menu(
          list, KRUX_BTC_SATOCHIP_MENU,
          sizeof(KRUX_BTC_SATOCHIP_MENU) /
              sizeof(KRUX_BTC_SATOCHIP_MENU[0])))
    return;
  if (strcmp(feature->id, "backup_export") == 0 &&
      create_krux_override_menu(
          list, KRUX_BACKUP_MENU,
          sizeof(KRUX_BACKUP_MENU) / sizeof(KRUX_BACKUP_MENU[0])))
    return;
  if (strcmp(feature->id, "addresses") == 0 &&
      create_krux_override_menu(
          list, KRUX_ADDRESSES_MENU,
          sizeof(KRUX_ADDRESSES_MENU) / sizeof(KRUX_ADDRESSES_MENU[0])))
    return;
  if (strcmp(feature->id, "device_tests") == 0 &&
      create_krux_override_menu(
          list, KRUX_DEVICE_TESTS_MENU,
          sizeof(KRUX_DEVICE_TESTS_MENU) / sizeof(KRUX_DEVICE_TESTS_MENU[0])))
    return;
  if (strcmp(feature->id, "smartcard_tools") == 0 &&
      create_krux_override_menu(
          list, KRUX_SMARTCARD_MENU,
          sizeof(KRUX_SMARTCARD_MENU) / sizeof(KRUX_SMARTCARD_MENU[0])))
    return;
  if (strcmp(feature->id, "smartcard_satochip_tools") == 0 &&
      create_krux_override_menu(
          list, KRUX_SMARTCARD_SATOCHIP_MENU,
          sizeof(KRUX_SMARTCARD_SATOCHIP_MENU) /
              sizeof(KRUX_SMARTCARD_SATOCHIP_MENU[0])))
    return;
  if (strcmp(feature->id, "smartcard_satochip_maint") == 0 &&
      create_krux_override_menu(
          list, KRUX_SMARTCARD_MAINT_MENU,
          sizeof(KRUX_SMARTCARD_MAINT_MENU) /
              sizeof(KRUX_SMARTCARD_MAINT_MENU[0])))
    return;
  if (strcmp(feature->id, "smartcard_satochip_advanced_tools") == 0 &&
      create_krux_override_menu(
          list, KRUX_SMARTCARD_ADVANCED_MENU,
          sizeof(KRUX_SMARTCARD_ADVANCED_MENU) /
              sizeof(KRUX_SMARTCARD_ADVANCED_MENU[0])))
    return;
  if (strcmp(feature->id, "smartcard_satochip_pubkey_tools") == 0 &&
      create_krux_override_menu(
          list, KRUX_SMARTCARD_PUBKEY_MENU,
          sizeof(KRUX_SMARTCARD_PUBKEY_MENU) /
              sizeof(KRUX_SMARTCARD_PUBKEY_MENU[0])))
    return;
  if (strcmp(feature->id, "smartcard_satochip_2fa_tools") == 0 &&
      create_krux_override_menu(
          list, KRUX_SMARTCARD_2FA_MENU,
          sizeof(KRUX_SMARTCARD_2FA_MENU) / sizeof(KRUX_SMARTCARD_2FA_MENU[0])))
    return;
  if (strcmp(feature->id, "smartcard_satochip_session_tools") == 0 &&
      create_krux_override_menu(
          list, KRUX_SMARTCARD_SESSION_MENU,
          sizeof(KRUX_SMARTCARD_SESSION_MENU) /
              sizeof(KRUX_SMARTCARD_SESSION_MENU[0])))
    return;
  if (strcmp(feature->id, "smartcard_seedkeeper_advanced_tools") == 0 &&
      create_krux_override_menu(
          list, KRUX_SMARTCARD_SEEDKEEPER_ADVANCED_MENU,
          sizeof(KRUX_SMARTCARD_SEEDKEEPER_ADVANCED_MENU) /
              sizeof(KRUX_SMARTCARD_SEEDKEEPER_ADVANCED_MENU[0])))
    return;
  if (strcmp(feature->id, "smartcard_satochip_seedkeeper_tools") == 0 &&
      create_krux_override_menu(
          list, KRUX_SMARTCARD_SEEDKEEPER_MENU,
          sizeof(KRUX_SMARTCARD_SEEDKEEPER_MENU) /
              sizeof(KRUX_SMARTCARD_SEEDKEEPER_MENU[0])))
    return;
  if (strcmp(feature->id, "smartcard_satochip_certificate_tools") == 0 &&
      create_krux_override_menu(
          list, KRUX_SMARTCARD_CERTIFICATE_MENU,
          sizeof(KRUX_SMARTCARD_CERTIFICATE_MENU) /
              sizeof(KRUX_SMARTCARD_CERTIFICATE_MENU[0])))
    return;
  if (strcmp(feature->id, "pi_mnemonic_tools") == 0 &&
      create_krux_override_menu(
          list, KRUX_PI_MNEMONIC_MENU,
          sizeof(KRUX_PI_MNEMONIC_MENU) / sizeof(KRUX_PI_MNEMONIC_MENU[0])))
    return;
  if (strcmp(feature->id, "pi_mnemonic_advanced") == 0 &&
      create_krux_override_menu(
          list, KRUX_PI_MNEMONIC_ADVANCED_MENU,
          sizeof(KRUX_PI_MNEMONIC_ADVANCED_MENU) /
              sizeof(KRUX_PI_MNEMONIC_ADVANCED_MENU[0])))
    return;
  if (strcmp(feature->id, "pi_connect_wallet") == 0 &&
      create_krux_override_menu(
          list, KRUX_PI_CONNECT_MENU,
          sizeof(KRUX_PI_CONNECT_MENU) / sizeof(KRUX_PI_CONNECT_MENU[0])))
    return;
  if (strcmp(feature->id, "pi_self_check") == 0 &&
      create_krux_override_menu(
          list, KRUX_PI_SELF_CHECK_MENU,
          sizeof(KRUX_PI_SELF_CHECK_MENU) / sizeof(KRUX_PI_SELF_CHECK_MENU[0])))
    return;

  size_t child_count = krux_feature_child_count(feature->id);
  for (size_t i = 0; i < child_count; i++) {
    const krux_feature_t *child = krux_feature_child_at(feature->id, i);
    if (child) {
      create_krux_menu_button(list, child->title, child->subtitle, child->id,
                              i == 0);
    }
  }
}

static void create_krux_leaf_content(lv_obj_t *list,
                                     const krux_feature_t *feature) {
  create_detail_cards(list, feature);
}

static void create_krux_bottom_nav(lv_obj_t *root,
                                   const krux_feature_t *feature) {
  if (!feature->parent_id)
    return;

  const char *back_target = shell_back_target_for_feature(feature);

  lv_obj_t *row = lv_obj_create(root);
  lv_obj_set_width(row, LV_PCT(100));
  lv_obj_set_height(row, LV_SIZE_CONTENT);
  style_krux_container(row, krux_canvas_color());
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_style_pad_column(row, 10, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  if (strcmp(back_target, "home") != 0) {
    lv_obj_t *home = create_krux_menu_button(row, "回到首页", NULL, "home", false);
    lv_obj_set_width(home, LV_PCT(48));
    lv_obj_set_height(home, shell_bottom_nav_height());
  }

  lv_obj_t *back =
      create_krux_menu_button(row, "返回", NULL, back_target, true);
  lv_obj_set_width(back, strcmp(back_target, "home") != 0 ? LV_PCT(48)
                                                          : LV_PCT(100));
  lv_obj_set_height(back, shell_bottom_nav_height());
}

static void render_screen(const krux_feature_t *feature) {
  if (!s_parent || !feature)
    return;

  shell_transient_state_reset();
  lv_obj_clean(s_parent);
  theme_apply_screen(s_parent);

  lv_obj_t *root = theme_create_page_container(s_parent);
  lv_obj_set_style_bg_color(root, krux_canvas_color(), 0);
  lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  bool is_home = strcmp(feature->id, "home") == 0;
  lv_obj_set_style_pad_top(root, shell_margin_v(), 0);
  lv_obj_set_style_pad_bottom(root, shell_margin_v(), 0);
  lv_obj_set_style_pad_left(root, shell_margin_h(), 0);
  lv_obj_set_style_pad_right(root, shell_margin_h(), 0);
  lv_obj_set_style_pad_gap(root, shell_root_gap(is_home), 0);

  create_krux_header(root, feature);

  size_t child_count = krux_feature_child_count(feature->id);
  bool has_override = krux_screen_has_override_menu(feature->id);
  size_t menu_count = has_override ? krux_override_menu_count(feature->id)
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
  lv_obj_t *list = create_krux_list(root, center_items, grid_mode);
  if (!menu_screen && satochip_screen_uses_onscreen_keyboard(feature->id)) {
    lv_obj_set_style_pad_bottom(list, LV_VER_RES * 38 / 100, 0);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
  }
  if (menu_screen) {
    if (is_home)
      create_krux_home_menu(list);
    else
      create_krux_child_menu(list, feature);
  } else {
    create_krux_leaf_content(list, feature);
  }

  s_menu_grid_mode = false;
  s_rendering_home_grid = false;
  s_compact_menu_grid = false;
  const char *back_target = shell_back_target_for_feature(feature);
  const krux_feature_t *parent = back_target ? krux_feature_find(back_target) : NULL;
  bool deep_menu_screen = menu_screen && feature->parent_id && parent &&
                          parent->parent_id &&
                          strcmp(parent->parent_id, "home") != 0;
  if (!menu_screen || deep_menu_screen)
    create_krux_bottom_nav(root, feature);
}

void krux_shell_create(lv_obj_t *parent) {
  s_parent = parent ? parent : lv_screen_active();
  (void)krux_shell_show_screen("home");
}

bool krux_shell_show_screen(const char *screen_id) {
  const char *requested_id = screen_id ? screen_id : "home";
  const char *alias_target = shell_alias_target_for_id(requested_id);
  if (alias_target)
    requested_id = alias_target;
  if (satochip_legacy_seedkeeper_id(requested_id))
    requested_id = "smartcard_satochip_seedkeeper_tools";
  if (!product_screen_is_visible(requested_id))
    return false;

#ifndef SIMULATOR
  if (!s_allow_sensitive_render && target_requires_shell_gate(requested_id) &&
      legacy_wallet_handle_target(requested_id)) {
    return true;
  }
#endif

  const krux_feature_t *feature = shell_feature_find(requested_id);
  if (!feature)
    return false;

  if (!s_parent)
    s_parent = lv_screen_active();

  s_current_screen_id = feature->id;
  render_screen(feature);
  return true;
}

size_t krux_shell_screen_count(void) {
  return sizeof(PRODUCT_SCREEN_IDS) / sizeof(PRODUCT_SCREEN_IDS[0]);
}

const char *krux_shell_screen_id_at(size_t index) {
  if (index >= krux_shell_screen_count())
    return NULL;
  return PRODUCT_SCREEN_IDS[index];
}

const char *krux_shell_screen_title_at(size_t index) {
  const char *id = krux_shell_screen_id_at(index);
  const krux_feature_t *feature = shell_feature_find(id);
  return feature ? feature->title : NULL;
}

const char *krux_shell_current_screen_id(void) { return s_current_screen_id; }
