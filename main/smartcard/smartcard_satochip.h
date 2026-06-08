#ifndef SMARTCARD_SATOCHIP_H
#define SMARTCARD_SATOCHIP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "smartcard_ccid.h"

typedef struct {
  bool app_selected;
  bool status_valid;
  esp_err_t transport_error;
  uint16_t select_sw;
  uint16_t status_sw;

  uint8_t raw_status[SMARTCARD_CCID_RESPONSE_MAX_LEN];
  size_t raw_status_len;

  uint8_t protocol_major;
  uint8_t protocol_minor;
  uint8_t applet_major;
  uint8_t applet_minor;
  uint8_t pin0_remaining;
  uint8_t puk0_remaining;
  uint8_t pin1_remaining;
  uint8_t puk1_remaining;
  bool needs_2fa;
  bool is_seeded;
  bool setup_done;
  bool needs_secure_channel;
  uint8_t nfc_policy;
  uint8_t feature_schnorr_policy;
  uint8_t feature_nostr_policy;
  uint8_t feature_liquid_policy;

  char detail[160];
} smartcard_satochip_status_t;

typedef struct {
  uint8_t compressed_pubkey[33];
  uint8_t uncompressed_pubkey[65];
  uint8_t chain_code[32];
  char address[43];
  char path[96];
  char detail[192];
  uint16_t sw;
  esp_err_t err;
  bool has_compressed_pubkey;
  bool has_uncompressed_pubkey;
  bool has_chain_code;
  bool has_address;
} smartcard_satochip_account_t;

typedef struct {
  smartcard_satochip_account_t address_key;
  smartcard_satochip_account_t account_key;
  smartcard_satochip_account_t parent_key;
  smartcard_satochip_account_t ledger_live[10];
  size_t ledger_live_count;
  smartcard_satochip_account_t btc[8];
  size_t btc_count;
  uint8_t master_fingerprint[4];
  bool has_master_fingerprint;
  uint8_t parent_fingerprint[4];
  bool has_parent_fingerprint;
  char detail[256];
  uint16_t sw;
  esp_err_t err;
} smartcard_satochip_web3_account_t;

typedef struct {
  char label[80];
  char applet[24];
  char detail[192];
  uint8_t raw_label[SMARTCARD_CCID_RESPONSE_MAX_LEN];
  size_t raw_label_len;
  uint16_t select_sw;
  uint16_t label_sw;
  esp_err_t err;
  bool app_selected;
  bool has_label;
} smartcard_satochip_label_t;

typedef struct {
  uint8_t compact[64];
  uint8_t signature[65];
  uint8_t recovery_id;
  uint8_t compressed_pubkey[33];
  char address[43];
  char path[96];
  char detail[256];
  uint16_t sw;
  esp_err_t err;
  bool has_signature;
  bool has_address;
  bool has_compressed_pubkey;
} smartcard_satochip_signature_t;

typedef enum {
  SMARTCARD_SATOCHIP_BTC_P2PKH = 0,
  SMARTCARD_SATOCHIP_BTC_P2SH_P2WPKH,
  SMARTCARD_SATOCHIP_BTC_P2WPKH,
  SMARTCARD_SATOCHIP_BTC_P2TR,
} smartcard_satochip_btc_script_t;

typedef struct {
  char path[96];
  char xtype[12];
  char xpub[128];
  char descriptor[256];
  char detail[256];
  uint16_t sw;
  esp_err_t err;
  bool has_xpub;
  bool has_descriptor;
} smartcard_satochip_btc_xpub_t;

typedef struct {
  char path[96];
  char address[128];
  char detail[256];
  uint16_t sw;
  esp_err_t err;
  bool has_address;
} smartcard_satochip_btc_address_t;

typedef struct {
  char detail[192];
  uint8_t raw_status[SMARTCARD_CCID_RESPONSE_MAX_LEN];
  size_t raw_status_len;
  uint16_t select_sw;
  uint16_t status_sw;
  uint16_t nb_secrets;
  uint16_t total_memory;
  uint16_t free_memory;
  uint16_t nb_logs_total;
  uint16_t nb_logs_avail;
  uint8_t last_log[7];
  size_t last_log_len;
  esp_err_t err;
  bool app_selected;
  bool status_valid;
} smartcard_seedkeeper_status_t;

#define SMARTCARD_SATOCHIP_APDU_RESULT_MAX_LEN 1024
typedef struct {
  uint8_t response[SMARTCARD_SATOCHIP_APDU_RESULT_MAX_LEN];
  size_t response_len;
  uint16_t sw;
  esp_err_t err;
  char detail[256];
} smartcard_satochip_apdu_result_t;

typedef struct {
  uint16_t id;
  uint8_t type;
  uint8_t subtype;
  uint8_t origin;
  uint8_t export_rights;
  uint8_t export_nbplain;
  uint8_t export_nbsecure;
  uint8_t export_counter;
  uint8_t fingerprint[4];
  uint8_t rfu1;
  uint8_t rfu2;
  char label[80];
  char header_hex[128];
} smartcard_seedkeeper_header_t;

typedef struct {
  smartcard_seedkeeper_header_t headers[64];
  size_t count;
  uint16_t sw;
  esp_err_t err;
  char detail[256];
} smartcard_seedkeeper_header_list_t;

typedef struct {
  uint8_t der[SMARTCARD_CCID_RESPONSE_MAX_LEN];
  size_t der_len;
  char pem[4096];
  uint16_t sw;
  esp_err_t err;
  bool has_certificate;
  char detail[256];
} smartcard_satochip_certificate_t;

typedef struct {
  bool authentic;
  char ca_text[1024];
  char subca_text[1024];
  char device_text[1536];
  char subject_cn[128];
  char error[256];
  uint16_t sw;
  esp_err_t err;
  bool has_certificate;
  char certificate_pem[4096];
} smartcard_satochip_authenticity_t;

esp_err_t smartcard_satochip_read_status(smartcard_satochip_status_t *out,
                                         uint32_t timeout_ms);
void smartcard_satochip_format_status(const smartcard_satochip_status_t *status,
                                      char *out, size_t out_len);
esp_err_t smartcard_satochip_get_label(smartcard_satochip_label_t *out,
                                       uint32_t timeout_ms);
void smartcard_satochip_format_label(const smartcard_satochip_label_t *label,
                                     char *out, size_t out_len);
esp_err_t smartcard_seedkeeper_read_status(const char *pin,
                                           smartcard_seedkeeper_status_t *out,
                                           uint32_t timeout_ms);
void smartcard_seedkeeper_format_status(
    const smartcard_seedkeeper_status_t *status, char *out, size_t out_len);
esp_err_t smartcard_satochip_get_eth_account(const char *pin, const char *path,
                                             smartcard_satochip_account_t *out,
                                             uint32_t timeout_ms);
esp_err_t smartcard_satochip_get_web3_account(
    const char *pin, smartcard_satochip_web3_account_t *out,
    uint32_t timeout_ms, bool include_okx_multi_accounts);
esp_err_t smartcard_satochip_sign_evm_digest(
    const char *pin, const char *path, const uint8_t digest[32],
    smartcard_satochip_signature_t *out, uint32_t timeout_ms);
esp_err_t smartcard_satochip_get_btc_xpub(
    const char *pin, const char *path, const char *xtype, bool is_testnet,
    smartcard_satochip_btc_xpub_t *out, uint32_t timeout_ms);
esp_err_t smartcard_satochip_get_btc_address(
    const char *pin, const char *path, smartcard_satochip_btc_script_t script,
    bool is_testnet, smartcard_satochip_btc_address_t *out,
    uint32_t timeout_ms);
esp_err_t smartcard_satochip_card_set_label(
    const char *pin, const char *label, smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms);
esp_err_t smartcard_satochip_card_set_nfc_policy(
    const char *pin, uint8_t policy,
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms);
esp_err_t smartcard_satochip_card_set_feature_policy(
    const char *pin, uint8_t feature_id, uint8_t policy,
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms);
esp_err_t smartcard_satochip_card_reset_factory_signal(
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms);
esp_err_t smartcard_satochip_card_setup_pin(
    const char *new_pin, smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms);
esp_err_t smartcard_satochip_card_reset_seed(
    const char *pin, const uint8_t *hmac, size_t hmac_len,
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms);
esp_err_t smartcard_satochip_card_import_mnemonic_seed(
    const char *pin, const char *mnemonic, const char *passphrase,
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms);
esp_err_t smartcard_satochip_card_change_pin(
    uint8_t pin_nbr, const char *old_pin, const char *new_pin,
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms);
esp_err_t smartcard_satochip_card_unblock_pin(
    uint8_t pin_nbr, const uint8_t *puk, size_t puk_len,
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms);
esp_err_t smartcard_satochip_card_set_2fa_key(
    const uint8_t *hmacsha160_key, size_t key_len, uint64_t amount_limit,
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms);
esp_err_t smartcard_satochip_card_reset_2fa_key(
    const uint8_t *chalresponse, size_t chal_len,
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms);
esp_err_t smartcard_satochip_card_export_perso_certificate(
    smartcard_satochip_certificate_t *out, uint32_t timeout_ms);
esp_err_t smartcard_satochip_card_import_perso_certificate(
    const char *cert_text, smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms);
esp_err_t smartcard_satochip_card_import_ndef_authentikey(
    const uint8_t privkey[32], smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms);
esp_err_t smartcard_satochip_card_export_authentikey(
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms);
esp_err_t smartcard_satochip_card_import_trusted_pubkey(
    const uint8_t pubkey[65], smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms);
esp_err_t smartcard_satochip_card_export_trusted_pubkey(
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms);
esp_err_t smartcard_satochip_card_challenge_response_pki(
    const uint8_t challenge_from_host[32], smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms);
esp_err_t smartcard_satochip_card_logout_all(
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms);
esp_err_t smartcard_satochip_card_verify_authenticity(
    smartcard_satochip_authenticity_t *out, uint32_t timeout_ms);
esp_err_t smartcard_seedkeeper_list_secret_headers(
    const char *pin, smartcard_seedkeeper_header_list_t *out,
    uint32_t timeout_ms);
esp_err_t smartcard_seedkeeper_generate_masterseed(
    const char *pin, uint8_t seed_size, uint8_t export_rights,
    const char *label, smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms);
esp_err_t smartcard_seedkeeper_generate_2fa_secret(
    const char *pin, uint8_t export_rights, const char *label,
    smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms);
esp_err_t smartcard_seedkeeper_generate_random_secret(
    const char *pin, uint8_t stype, uint8_t subtype, uint8_t size,
    uint8_t export_rights, const char *label, bool save_entropy,
    const uint8_t *entropy,
    size_t entropy_len, smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms);
esp_err_t smartcard_seedkeeper_derive_master_password(
    const char *pin, const char *salt, uint16_t sid, uint16_t sid_pubkey,
    bool has_sid_pubkey, smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms);
esp_err_t smartcard_seedkeeper_import_secret(
    const char *pin, const uint8_t *header, size_t header_len,
    const uint8_t *secret, size_t secret_len, uint16_t sid_pubkey,
    const uint8_t *iv, size_t iv_len, const uint8_t *hmac, size_t hmac_len,
    bool secure_import,
    smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms);
esp_err_t smartcard_seedkeeper_export_secret(
    const char *pin, uint16_t sid, uint16_t sid_pubkey, bool secure_export,
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms);
esp_err_t smartcard_seedkeeper_export_secret_to_satochip(
    const char *pin, uint16_t sid, uint16_t sid_pubkey,
    smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms);
esp_err_t smartcard_seedkeeper_reset_secret(
    const char *pin, uint16_t sid, smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms);
esp_err_t smartcard_seedkeeper_setup_pin(
    const char *new_pin, smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms);
esp_err_t smartcard_seedkeeper_change_pin(
    uint8_t pin_nbr, const char *old_pin, const char *new_pin,
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms);
esp_err_t smartcard_seedkeeper_reset_wrong_pin_step(
    const char *wrong_pin, smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms);
esp_err_t smartcard_seedkeeper_reset_wrong_puk_step(
    const char *wrong_puk, smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms);
esp_err_t smartcard_seedkeeper_reset_factory_signal(
    const char *pin, smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms);
esp_err_t smartcard_seedkeeper_print_logs(
    const char *pin, smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms);

#endif // SMARTCARD_SATOCHIP_H
