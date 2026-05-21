#include "../../main/ui/dialog.h"
#include "../../main/core/evm.h"
#include "../../main/smartcard/smartcard_satochip.h"
#include "../../main/ui/input_helpers.h"
#include <esp_err.h>
#include <lvgl.h>
#include <stdlib.h>
#include <string.h>

static void fill_string(char *dst, size_t dst_len, const char *src) {
  if (!dst || dst_len == 0)
    return;
  if (!src)
    src = "";
  strncpy(dst, src, dst_len - 1);
  dst[dst_len - 1] = '\0';
}

void dialog_show_error(const char *message, dialog_simple_callback_t callback,
                       int timeout_ms) {
  (void)timeout_ms;
  if (callback)
    callback();
  (void)message;
}

void dialog_show_info(const char *title, const char *message,
                      dialog_callback_t callback, void *user_data,
                      dialog_style_t style) {
  (void)title;
  (void)message;
  (void)style;
  if (callback)
    callback(user_data);
}

void dialog_show_confirm(const char *message,
                         dialog_confirm_callback_t callback, void *user_data,
                         dialog_style_t style) {
  (void)message;
  (void)style;
  if (callback)
    callback(true, user_data);
}

void dialog_show_danger_confirm(const char *message,
                                dialog_confirm_callback_t callback,
                                void *user_data, dialog_style_t style) {
  dialog_show_confirm(message, callback, user_data, style);
}

void dialog_show_message(const char *title, const char *message) {
  (void)title;
  (void)message;
}

lv_obj_t *dialog_show_progress(const char *title, const char *message,
                               dialog_style_t style) {
  (void)title;
  (void)message;
  (void)style;
  return lv_obj_create(lv_screen_active());
}

void ui_text_input_create(ui_text_input_t *input, lv_obj_t *parent,
                          const char *placeholder, bool password_mode,
                          lv_event_cb_t ready_cb) {
  (void)parent;
  (void)placeholder;
  (void)password_mode;
  (void)ready_cb;
  memset(input, 0, sizeof(*input));
}

void ui_text_input_show(ui_text_input_t *input) { (void)input; }
void ui_text_input_hide(ui_text_input_t *input) { (void)input; }
void ui_text_input_destroy(ui_text_input_t *input) { (void)input; }
void ui_keyboard_apply_safe_text_map(lv_obj_t *keyboard) { (void)keyboard; }
void ui_textarea_enable_safe_keyboard_shortcuts(lv_obj_t *textarea) {
  (void)textarea;
}
lv_obj_t *ui_create_back_button(lv_obj_t *parent, lv_event_cb_t event_cb) {
  (void)event_cb;
  return lv_btn_create(parent);
}
lv_obj_t *ui_create_home_button(lv_obj_t *parent, lv_event_cb_t event_cb) {
  (void)event_cb;
  return lv_btn_create(parent);
}
lv_obj_t *ui_create_power_button(lv_obj_t *parent, lv_event_cb_t event_cb) {
  (void)event_cb;
  return lv_btn_create(parent);
}
lv_obj_t *ui_create_settings_button(lv_obj_t *parent,
                                    lv_event_cb_t event_cb) {
  (void)event_cb;
  return lv_btn_create(parent);
}
void ui_power_off_confirmed_cb(bool confirmed, void *user_data) {
  (void)confirmed;
  (void)user_data;
}

bool evm_get_address(char *address_out, size_t address_out_len) {
  fill_string(address_out, address_out_len,
              "0x0000000000000000000000000000000000000000");
  return true;
}
void evm_keccak256(const uint8_t *input, size_t input_len, uint8_t out[32]) {
  (void)input;
  (void)input_len;
  memset(out, 0, 32);
}
bool evm_address_from_uncompressed_pubkey(const uint8_t pubkey[65],
                                          char *address_out,
                                          size_t address_out_len) {
  (void)pubkey;
  fill_string(address_out, address_out_len,
              "0x0000000000000000000000000000000000000000");
  return true;
}
bool evm_web3_build_connect_qr(evm_web3_profile_t profile,
                               evm_web3_qr_bundle_t *bundle_out) {
  if (!bundle_out)
    return false;
  memset(bundle_out, 0, sizeof(*bundle_out));
  fill_string(bundle_out->address, sizeof(bundle_out->address),
              "0x0000000000000000000000000000000000000000");
  fill_string(bundle_out->summary, sizeof(bundle_out->summary),
              profile == EVM_WEB3_PROFILE_ADDRESS ? "地址" : "连接码");
  bundle_out->pages[0] = strdup("ethereum:0x0000000000000000000000000000000000000000");
  bundle_out->page_count = 1;
  return bundle_out->pages[0] != NULL;
}
bool evm_web3_build_external_connect_qr(
    evm_web3_profile_t profile, const evm_web3_external_account_t *account,
    evm_web3_qr_bundle_t *bundle_out) {
  (void)profile;
  (void)account;
  return evm_web3_build_connect_qr(EVM_WEB3_PROFILE_ADDRESS, bundle_out);
}
void evm_web3_qr_bundle_clear(evm_web3_qr_bundle_t *bundle) {
  if (!bundle)
    return;
  free(bundle->pages[0]);
  memset(bundle, 0, sizeof(*bundle));
}

esp_err_t smartcard_satochip_read_status(smartcard_satochip_status_t *out,
                                         uint32_t timeout_ms) {
  (void)timeout_ms;
  if (out) {
    memset(out, 0, sizeof(*out));
    out->status_valid = true;
    fill_string(out->detail, sizeof(out->detail), "模拟器占位");
  }
  return ESP_OK;
}
void smartcard_satochip_format_status(const smartcard_satochip_status_t *status,
                                      char *out, size_t out_len) {
  (void)status;
  fill_string(out, out_len, "模拟器占位");
}
esp_err_t smartcard_satochip_get_label(smartcard_satochip_label_t *out,
                                       uint32_t timeout_ms) {
  (void)timeout_ms;
  if (out) {
    memset(out, 0, sizeof(*out));
    fill_string(out->label, sizeof(out->label), "模拟器");
    fill_string(out->detail, sizeof(out->detail), "模拟器占位");
    out->has_label = true;
  }
  return ESP_OK;
}
void smartcard_satochip_format_label(const smartcard_satochip_label_t *label,
                                     char *out, size_t out_len) {
  (void)label;
  fill_string(out, out_len, "模拟器");
}
esp_err_t smartcard_seedkeeper_read_status(
    smartcard_seedkeeper_status_t *out, uint32_t timeout_ms) {
  (void)timeout_ms;
  if (out)
    memset(out, 0, sizeof(*out));
  return ESP_OK;
}
void smartcard_seedkeeper_format_status(
    const smartcard_seedkeeper_status_t *status, char *out, size_t out_len) {
  (void)status;
  fill_string(out, out_len, "模拟器");
}
esp_err_t smartcard_satochip_get_eth_account(const char *pin, const char *path,
                                             smartcard_satochip_account_t *out,
                                             uint32_t timeout_ms) {
  (void)pin; (void)path; (void)timeout_ms;
  if (out) memset(out, 0, sizeof(*out));
  return ESP_OK;
}
esp_err_t smartcard_satochip_get_web3_account(
    const char *pin, smartcard_satochip_web3_account_t *out,
    uint32_t timeout_ms) {
  (void)pin; (void)timeout_ms;
  if (out) memset(out, 0, sizeof(*out));
  return ESP_OK;
}
esp_err_t smartcard_satochip_sign_evm_digest(
    const char *pin, const char *path, const uint8_t digest[32],
    smartcard_satochip_signature_t *out, uint32_t timeout_ms) {
  (void)pin; (void)path; (void)digest; (void)timeout_ms;
  if (out) memset(out, 0, sizeof(*out));
  return ESP_OK;
}
esp_err_t smartcard_satochip_get_btc_xpub(
    const char *pin, const char *path, const char *xtype, bool is_testnet,
    smartcard_satochip_btc_xpub_t *out, uint32_t timeout_ms) {
  (void)pin; (void)path; (void)xtype; (void)is_testnet; (void)timeout_ms;
  if (out) memset(out, 0, sizeof(*out));
  return ESP_OK;
}
esp_err_t smartcard_satochip_get_btc_address(
    const char *pin, const char *path, smartcard_satochip_btc_script_t script,
    bool is_testnet, smartcard_satochip_btc_address_t *out,
    uint32_t timeout_ms) {
  (void)pin; (void)path; (void)script; (void)is_testnet; (void)timeout_ms;
  if (out) memset(out, 0, sizeof(*out));
  return ESP_OK;
}
esp_err_t smartcard_satochip_card_set_label(
    const char *pin, const char *label, smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms) {
  (void)pin; (void)label; (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_satochip_card_set_nfc_policy(
    const char *pin, uint8_t policy,
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms) {
  (void)pin; (void)policy; (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_satochip_card_set_feature_policy(
    const char *pin, uint8_t feature_id, uint8_t policy,
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms) {
  (void)pin; (void)feature_id; (void)policy; (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_satochip_card_reset_factory_signal(
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms) {
  (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_seedkeeper_reset_factory_signal(
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms) {
  (void)timeout_ms;
  if (out) memset(out, 0, sizeof(*out));
  return ESP_OK;
}
esp_err_t smartcard_satochip_card_reset_seed(
    const char *pin, const uint8_t *hmac, size_t hmac_len,
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms) {
  (void)pin; (void)hmac; (void)hmac_len; (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_satochip_card_change_pin(
    uint8_t pin_nbr, const char *old_pin, const char *new_pin,
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms) {
  (void)pin_nbr; (void)old_pin; (void)new_pin; (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_seedkeeper_change_pin(
    uint8_t pin_nbr, const char *old_pin, const char *new_pin,
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms) {
  (void)pin_nbr; (void)old_pin; (void)new_pin; (void)timeout_ms;
  if (out) memset(out, 0, sizeof(*out));
  return ESP_OK;
}
esp_err_t smartcard_satochip_card_unblock_pin(
    uint8_t pin_nbr, const uint8_t *puk, size_t puk_len,
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms) {
  (void)pin_nbr; (void)puk; (void)puk_len; (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_satochip_card_set_2fa_key(
    const uint8_t *hmacsha160_key, size_t key_len, uint64_t amount_limit,
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms) {
  (void)hmacsha160_key; (void)key_len; (void)amount_limit; (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_satochip_card_reset_2fa_key(
    const uint8_t *chalresponse, size_t chal_len,
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms) {
  (void)chalresponse; (void)chal_len; (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_satochip_card_export_perso_certificate(
    smartcard_satochip_certificate_t *out, uint32_t timeout_ms) {
  (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_satochip_card_import_perso_certificate(
    const char *cert_text, smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms) {
  (void)cert_text; (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_satochip_card_import_ndef_authentikey(
    const uint8_t privkey[32], smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms) {
  (void)privkey; (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_satochip_card_export_authentikey(
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms) {
  (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_satochip_card_import_trusted_pubkey(
    const uint8_t pubkey[65], smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms) {
  (void)pubkey; (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_satochip_card_export_trusted_pubkey(
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms) {
  (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_satochip_card_challenge_response_pki(
    const uint8_t challenge_from_host[32], smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms) {
  (void)challenge_from_host; (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_satochip_card_logout_all(
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms) {
  (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_satochip_card_verify_authenticity(
    smartcard_satochip_authenticity_t *out, uint32_t timeout_ms) {
  (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_seedkeeper_list_secret_headers(
    const char *pin, smartcard_seedkeeper_header_list_t *out,
    uint32_t timeout_ms) {
  (void)pin; (void)timeout_ms;
  if (out) {
    memset(out, 0, sizeof(*out));
    out->count = 1;
    out->headers[0].id = 1;
    out->headers[0].type = 0x30;
    out->headers[0].export_rights = 0x01;
    memcpy(out->headers[0].fingerprint, "\x12\x34\x56\x78", 4);
    snprintf(out->headers[0].label, sizeof(out->headers[0].label),
             "Kern 模拟助记词");
    out->sw = 0x9000;
    fill_string(out->detail, sizeof(out->detail), "模拟器 SeedKeeper 列表");
  }
  return ESP_OK;
}
esp_err_t smartcard_seedkeeper_generate_masterseed(
    const char *pin, uint8_t seed_size, uint8_t export_rights,
    const char *label, smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms) {
  (void)pin; (void)seed_size; (void)export_rights; (void)label; (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_seedkeeper_generate_2fa_secret(
    const char *pin, uint8_t export_rights, const char *label,
    smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms) {
  (void)pin; (void)export_rights; (void)label; (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_seedkeeper_generate_random_secret(
    const char *pin, uint8_t stype, uint8_t subtype, uint8_t size,
    uint8_t export_rights, const char *label, bool save_entropy,
    const uint8_t *entropy,
    size_t entropy_len, smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms) {
  (void)pin; (void)stype; (void)subtype; (void)size; (void)export_rights; (void)label; (void)save_entropy; (void)entropy; (void)entropy_len; (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_seedkeeper_derive_master_password(
    const char *pin, const char *salt, uint16_t sid, uint16_t sid_pubkey,
    bool has_sid_pubkey, smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms) {
  (void)pin; (void)salt; (void)sid; (void)sid_pubkey; (void)has_sid_pubkey; (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_seedkeeper_import_secret(
    const char *pin, const uint8_t *header, size_t header_len,
    const uint8_t *secret, size_t secret_len, uint16_t sid_pubkey,
    const uint8_t *iv, size_t iv_len, const uint8_t *hmac, size_t hmac_len,
    bool secure_import,
    smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms) {
  (void)pin; (void)header; (void)header_len; (void)secret; (void)secret_len; (void)sid_pubkey; (void)iv; (void)iv_len; (void)hmac; (void)hmac_len; (void)secure_import; (void)timeout_ms;
  if (out) {
    memset(out, 0, sizeof(*out));
    out->sw = 0x9000;
    out->response_len = 6;
    out->response[1] = 1;
    out->response[2] = 0x12;
    out->response[3] = 0x34;
    out->response[4] = 0x56;
    out->response[5] = 0x78;
    fill_string(out->detail, sizeof(out->detail),
                "导入成功：Kern 模拟助记词 SID=1 指纹=12345678。");
  }
  return ESP_OK;
}
esp_err_t smartcard_seedkeeper_export_secret(
    const char *pin, uint16_t sid, uint16_t sid_pubkey, bool secure_export,
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms) {
  (void)pin; (void)sid; (void)sid_pubkey; (void)secure_export; (void)timeout_ms;
  if (out) {
    static const char mnemonic[] =
        "abandon abandon abandon abandon abandon abandon abandon abandon "
        "abandon abandon abandon about";
    const char label[] = "Kern 模拟助记词";
    size_t label_len = strlen(label);
    size_t mnemonic_len = strlen(mnemonic);
    memset(out, 0, sizeof(*out));
    out->sw = 0x9000;
    size_t pos = 0;
    out->response[pos++] = 0x00;
    out->response[pos++] = 0x01;
    out->response[pos++] = 0x30;
    out->response[pos++] = 0x01;
    out->response[pos++] = 0x01;
    out->response[pos++] = 0x00;
    out->response[pos++] = 0x00;
    out->response[pos++] = 0x00;
    out->response[pos++] = 0x12;
    out->response[pos++] = 0x34;
    out->response[pos++] = 0x56;
    out->response[pos++] = 0x78;
    out->response[pos++] = 0x00;
    out->response[pos++] = 0x00;
    out->response[pos++] = (uint8_t)label_len;
    memcpy(out->response + pos, label, label_len);
    pos += label_len;
    size_t secret_len = mnemonic_len + 2;
    out->response[pos++] = (uint8_t)(secret_len >> 8);
    out->response[pos++] = (uint8_t)secret_len;
    out->response[pos++] = (uint8_t)mnemonic_len;
    memcpy(out->response + pos, mnemonic, mnemonic_len);
    pos += mnemonic_len;
    out->response[pos++] = 0x00;
    out->response_len = pos;
    fill_string(out->detail, sizeof(out->detail),
                "导出完成：Kern 模拟助记词。");
  }
  return ESP_OK;
}
esp_err_t smartcard_seedkeeper_export_secret_to_satochip(
    const char *pin, uint16_t sid, uint16_t sid_pubkey,
    smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms) {
  (void)pin; (void)sid; (void)sid_pubkey; (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_seedkeeper_reset_secret(
    const char *pin, uint16_t sid, smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms) {
  (void)pin; (void)sid; (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_seedkeeper_print_logs(
    const char *pin, smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms) {
  (void)pin; (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
