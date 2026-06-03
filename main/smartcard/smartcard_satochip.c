#include "smartcard_satochip.h"

#include <stdarg.h>
#include <inttypes.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "core/crypto_utils.h"
#include "esp_log.h"
#include "esp_err.h"
#include "i18n/i18n.h"
#include "mbedtls/base64.h"
#include "mbedtls/aes.h"
#include "mbedtls/ecp.h"
#include "mbedtls/md.h"
#include "mbedtls/pk.h"
#include "mbedtls/x509_crt.h"
#include "wally_address.h"
#include "wally_bip39.h"
#include "wally_bip32.h"
#include "wally_core.h"
#include "wally_crypto.h"
#include "wally_script.h"
#include "smartcard_transport.h"

#ifndef ESP_ERR_INVALID_RESPONSE
#define ESP_ERR_INVALID_RESPONSE ESP_ERR_INVALID_STATE
#endif

static const char *TAG = "KSIG_SATOCHIP";

static const uint8_t k_select_satochip_apdu[] = {
    0x00, 0xA4, 0x04, 0x00, 0x08, 0x53, 0x61, 0x74,
    0x6f, 0x43, 0x68, 0x69, 0x70};
static const uint8_t k_select_seedkeeper_apdu[] = {
    0x00, 0xA4, 0x04, 0x00, 0x0A, 0x53, 0x65, 0x65,
    0x64, 0x4b, 0x65, 0x65, 0x70, 0x65, 0x72};
static const uint8_t k_get_status_apdu[] = {0xB0, 0x3C, 0x00, 0x00, 0x00};
static const uint8_t k_get_label_apdu[] = {0xB0, 0x3D, 0x00, 0x01, 0x00};
static const uint8_t k_seedkeeper_get_status_apdu[] = {
    0xB0, 0xA7, 0x00, 0x00, 0x00};

#define SATOCHIP_CLA 0xB0
#define SATOCHIP_INS_SETUP 0x2A
#define SATOCHIP_INS_CHANGE_PIN 0x44
#define SATOCHIP_INS_UNBLOCK_PIN 0x46
#define SATOCHIP_INS_VERIFY_PIN 0x42
#define SATOCHIP_INS_BIP32_IMPORT_SEED 0x6C
#define SATOCHIP_INS_BIP32_GET_AUTHENTIKEY 0x73
#define SATOCHIP_INS_BIP32_SET_AUTHENTIKEY_PUBKEY 0x75
#define SATOCHIP_INS_BIP32_GET_EXTENDED_KEY 0x6D
#define SATOCHIP_INS_SIGN_TRANSACTION_HASH 0x7A
#define SATOCHIP_INS_INIT_SECURE_CHANNEL 0x81
#define SATOCHIP_INS_PROCESS_SECURE_CHANNEL 0x82
#define SATOCHIP_INS_RESET_SEED 0x77
#define SATOCHIP_INS_RESET_2FA 0x78
#define SATOCHIP_INS_SET_2FA_KEY 0x79
#define SATOCHIP_INS_GET_SET_LABEL 0x3D
#define SATOCHIP_INS_SET_NFC_POLICY 0x3E
#define SATOCHIP_INS_GET_SET_NDEF 0x3F
#define SATOCHIP_INS_SET_FEATURE_POLICY 0x3A
#define SATOCHIP_INS_RESET_FACTORY_SIGNAL 0xFF
#define SATOCHIP_INS_IMPORT_PKI_CERTIFICATE 0x92
#define SATOCHIP_INS_EXPORT_PKI_CERTIFICATE 0x93
#define SATOCHIP_INS_IMPORT_PKI_NDEF_AUTHENTIKEY 0x9B
#define SATOCHIP_INS_CHALLENGE_RESPONSE_PKI 0x9A
#define SATOCHIP_INS_EXPORT_PKI_PUBKEY 0x98
#define SATOCHIP_INS_IMPORT_TRUSTED_PUBKEY 0xAA
#define SATOCHIP_INS_EXPORT_TRUSTED_PUBKEY 0xAB
#define SATOCHIP_INS_LOGOUT_ALL 0x60
#define SATOCHIP_INS_IMPORT_SECRET 0xA1
#define SATOCHIP_INS_EXPORT_SECRET 0xA2
#define SATOCHIP_INS_GENERATE_RANDOM_SECRET 0xA3
#define SATOCHIP_INS_GENERATE_MASTERSEED 0xA0
#define SATOCHIP_INS_GENERATE_2FA_SECRET 0xAE
#define SATOCHIP_INS_DERIVE_MASTER_PASSWORD 0xAF
#define SATOCHIP_INS_LIST_SECRET_HEADERS 0xA6
#define SATOCHIP_INS_SEEDKEEPER_STATUS 0xA7
#define SATOCHIP_INS_EXPORT_SECRET_TO_SATOCHIP 0xA8
#define SATOCHIP_INS_SEEDKEEPER_LOGS 0xA9
#define SATOCHIP_INS_RESET_SECRET 0xA5
#define SATOCHIP_MAX_PATH_COMPONENTS 10
#define SATOCHIP_DEFAULT_TIMEOUT_MS 30000
#define SATOCHIP_SC_MAC_LEN 20
#define KECCAK_ROUNDS 24

static const char k_satochip_ca_pem[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIICLTCCAbOgAwIBAgIBATAKBggqhkjOPQQDBDBeMQswCQYDVQQGEwJCRTERMA8G\n"
    "A1UECAwIQnJ1c3NlbHMxEDAOBgNVBAoMB1RvcG9yaW4xKjAoBgNVBAMMIVRvcG9y\n"
    "aW4gUGVyc29uYWxpemF0aW9uIFJvb3QgQ0EgMTAeFw0yMDEyMDExMjAwMDBaFw00\n"
    "MDEyMDExMjAwMDBaMF4xCzAJBgNVBAYTAkJFMREwDwYDVQQIDAhCcnVzc2VsczEQ\n"
    "MA4GA1UECgwHVG9wb3JpbjEqMCgGA1UEAwwhVG9wb3JpbiBQZXJzb25hbGl6YXRp\n"
    "b24gUm9vdCBDQSAxMHYwEAYHKoZIzj0CAQYFK4EEACIDYgAE5ZPgeQ54Y5NdTJM4\n"
    "uYU0f+vDK/D5XI9pY69i+EkgK4l++zKjsGMYY7gkEFzcqB1nQRn8ozss/0vYaMpk\n"
    "oePYNqVd0ZQxwFcb/NLDyZQq3NzfTAmQjw6KMSvPL57R24K1o0UwQzASBgNVHRMB\n"
    "Af8ECDAGAQH/AgEBMB0GA1UdDgQWBBRda9Hdoqr5lHwykRLBgkIVnUT6ITAOBgNV\n"
    "HQ8BAf8EBAMCAQYwCgYIKoZIzj0EAwQDaAAwZQIxAJTGtuRR9ceNqAXu5sAOp6yJ\n"
    "RLdnio7RVMmIZWLishPolJFN9vPt7FSBDgxRnqUJVwIwXJNx20XtJJl6NwKLfMIK\n"
    "nf1pE1NCQErS3wK9lLPZkr7uK/TpulrKJ+hc+1w7+UeU\n"
    "-----END CERTIFICATE-----\n";

static const char k_satochip_subca_pem[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIICIDCCAaWgAwIBAgIBAzAKBggqhkjOPQQDBDBeMQswCQYDVQQGEwJCRTERMA8G\n"
    "A1UECAwIQnJ1c3NlbHMxEDAOBgNVBAoMB1RvcG9yaW4xKjAoBgNVBAMMIVRvcG9y\n"
    "aW4gUGVyc29uYWxpemF0aW9uIFJvb3QgQ0EgMTAeFw0yMDEyMDExMjAwMDBaFw00\n"
    "MDEyMDExMjAwMDBaMHAxCzAJBgNVBAYTAkJFMREwDwYDVQQIDAhCcnVzc2VsczEQ\n"
    "MA4GA1UECgwHVG9wb3JpbjERMA8GA1UECwwIU2F0b2NoaXAxKTAnBgNVBAMMIFNh\n"
    "dG9jaGlwIFBlcnNvbmFsaXphdGlvbiBTdWJDQSAxMFYwEAYHKoZIzj0CAQYFK4EE\n"
    "AAoDQgAEcTVxDGkXKyV0kaRXu+9gTrg654Xam6ktgDA/n+28+egjMmZDWroaha/6\n"
    "+MPP/XBGeFAecmO0hppkgnUcwjRcw6NFMEMwEgYDVR0TAQH/BAgwBgEB/wIBADAd\n"
    "BgNVHQ4EFgQUx9Plfp8o9ZCoticLc5x7CnSBriUwDgYDVR0PAQH/BAQDAgEGMAoG\n"
    "CCqGSM49BAMEA2kAMGYCMQCmoDTdSMMSMogCm4l130drFFxC0wmbZwWE+xGjZIKL\n"
    "9tXosnIzUx5VUqbeNXbvJBUCMQDVVRt4c4veIxdwWOv2DhJ9Ri/3it8R0v/eQ/jU\n"
    "ppK2S2/ObbWSt5aAGi4EfpGZ93E=\n"
    "-----END CERTIFICATE-----\n";

static const char k_satochip_test_ca_pem[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIB/TCCAYOgAwIBAgIBATAKBggqhkjOPQQDBDBGMQswCQYDVQQGEwJCRTERMA8G\n"
    "A1UECAwIQnJ1c3NlbHMxEDAOBgNVBAoMB1RvcG9yaW4xEjAQBgNVBAMMCVRFU1Qg\n"
    "Q0EgMTAeFw0yMDEyMDExMjAwMDBaFw00MDEyMDExMjAwMDBaMEYxCzAJBgNVBAYT\n"
    "AkJFMREwDwYDVQQIDAhCcnVzc2VsczEQMA4GA1UECgwHVG9wb3JpbjESMBAGA1UE\n"
    "AwwJVEVTVCBDQSAxMHYwEAYHKoZIzj0CAQYFK4EEACIDYgAE/25Heq7rJvqxCcO4\n"
    "wq+y9bAA5YC3dJhDpH8AIBHbJ+w4g4neVmQLyrW0wO1xH/R4Ne8Bb7GTA+YYy0/x\n"
    "jtls6KHCgUFKBgCKtQK07CtSNWX6GViN8ZYDlvtBtZlWOi9Lo0UwQzASBgNVHRMB\n"
    "Af8ECDAGAQH/AgEBMB0GA1UdDgQWBBRSKn/TwpJFN3wDFDpdi59W/zkThzAOBgNV\n"
    "HQ8BAf8EBAMCAQYwCgYIKoZIzj0EAwQDaAAwZQIwTxLhCePwLKVm13ZdmJcRf5YI\n"
    "ePKRe+tk7Jew2oX7ZpoTVDbof4Glzv7aI31Csoa3AjEAg+EHbj8MgzJAcYvdbF8d\n"
    "dH8pu5JVYRp2iZPnB36gvjCdiRc8xfTbTYauH8DrWeCr\n"
    "-----END CERTIFICATE-----\n";

static const char k_satochip_test_subca_pem[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIICBzCCAY2gAwIBAgIBBzAKBggqhkjOPQQDBDBGMQswCQYDVQQGEwJCRTERMA8G\n"
    "A1UECAwIQnJ1c3NlbHMxEDAOBgNVBAoMB1RvcG9yaW4xEjAQBgNVBAMMCVRFU1Qg\n"
    "Q0EgMTAeFw0yMDEyMDExMjAwMDBaFw00MDEyMDExMjAwMDBaMHAxCzAJBgNVBAYT\n"
    "AkJFMREwDwYDVQQIDAhCcnVzc2VsczEQMA4GA1UECgwHVG9wb3JpbjERMA8GA1UE\n"
    "CwwIU2F0b2NoaXAxKTAnBgNVBAMMIFNhdG9jaGlwIFBlcnNvbmFsaXphdGlvbiBT\n"
    "dWJDQSAxMFYwEAYHKoZIzj0CAQYFK4EEAAoDQgAEgvUALg9mBMwQ5mxPMZY+9ltN\n"
    "+/Fb0wSHmTyBtZf/6lfUv4P0jV9G4l402HOLopZa92KYGReggh8GPjnK1itPTaNF\n"
    "MEMwEgYDVR0TAQH/BAgwBgEB/wIBADAdBgNVHQ4EFgQUQIym/hHeoi7IQeNqZKay\n"
    "TiZwgiUwDgYDVR0PAQH/BAQDAgEGMAoGCCqGSM49BAMEA2gAMGUCMQCuYiVJfOnV\n"
    "NnWhnP1zdEq9J+/2QvPa5U4xhPiOKxY3F/AvGdYHnuJoVyYIBAd5B4wCMGhQ8Cyo\n"
    "ZQfFcG9vih3KSrn0rgXqpY+k+ceOHoYc9/DsNyG8ycHZWZQ5dOWVfVPLCA==\n"
    "-----END CERTIFICATE-----\n";

typedef struct {
  uint8_t authentikey[33];
  uint8_t authentikey_coordx[32];
  uint8_t pubkey[33];
  uint8_t chain_code[32];
} satochip_extended_key_t;

typedef struct {
  bool active;
  uint32_t iv_counter;
  uint8_t host_privkey[32];
  uint8_t host_pubkey[65];
  uint8_t card_pubkey[65];
  uint8_t derived_key[16];
  uint8_t mac_key[SATOCHIP_SC_MAC_LEN];
} satochip_secure_channel_t;

typedef struct {
  satochip_secure_channel_t sc;
  smartcard_satochip_status_t status;
  uint8_t authentikey[33];
  uint8_t authentikey_coordx[32];
  bool has_authentikey;
  bool pin_verified;
  uint16_t sw;
} satochip_session_t;

static const char *yes_no_text(bool value) {
  return value ? i18n_tr_or("common.yes", "Yes")
               : i18n_tr_or("common.no", "No");
}

static const char *sc_fmt_not_selected(void) {
  return i18n_tr_or("smartcard.ccid.applet.not_detected", "Not selected");
}

static const char *sc_fmt_detected(void) {
  return i18n_tr_or("smartcard.ccid.reader.detected", "Detected");
}

static const char *sc_fmt_empty(void) {
  return i18n_tr_or("dialog.empty_value", "(empty)");
}

static const char *sc_fmt_no_status(void) {
  return i18n_tr_or("dialog.empty_value", "No status.");
}

static const char *sc_fmt_no_label(void) {
  return i18n_tr_or("dialog.empty_value", "No label.");
}

static const char *sc_fmt_detail(const char *detail) {
  if (!detail || !detail[0])
    return "";
  if (strcmp(detail, "Satochip detected; the card is not initialized.") == 0)
    return i18n_tr_or("smartcard.ccid.card_not_initialized", detail);
  if (strcmp(detail, "Satochip status fields are incomplete.") == 0 ||
      strcmp(detail, "SeedKeeper status response is too short.") == 0)
    return i18n_tr_or("dialog.invalid_format", detail);
  if (strcmp(detail, "Satochip status read successfully.") == 0 ||
      strcmp(detail, "SeedKeeper status read successfully.") == 0)
    return i18n_tr_or("smartcard.ccid.status_read_passed", detail);
  if (strcmp(detail, "Label read successfully.") == 0)
    return i18n_tr_or("dialog.success", detail);
  if (strcmp(detail, "This card does not support labels.") == 0)
    return i18n_tr_or("dialog.not_supported", detail);
  if (strcmp(detail, "SeedKeeper is not initialized.") == 0)
    return i18n_tr_or("sign.card_not_initialized", detail);
  if (strcmp(detail, "SeedKeeper requires a PIN.") == 0)
    return i18n_tr_or("dialog.pin_required", detail);
  return detail;
}

static uint64_t rotl64(uint64_t x, unsigned s) {
  return (x << s) | (x >> (64U - s));
}

static uint64_t load64_le(const uint8_t *x) {
  uint64_t r = 0;
  for (int i = 0; i < 8; i++)
    r |= ((uint64_t)x[i]) << (8U * (unsigned)i);
  return r;
}

static void store64_le(uint8_t *x, uint64_t u) {
  for (int i = 0; i < 8; i++)
    x[i] = (uint8_t)(u >> (8U * (unsigned)i));
}

static void keccakf(uint64_t st[25]) {
  static const uint64_t rc[KECCAK_ROUNDS] = {
      0x0000000000000001ULL, 0x0000000000008082ULL,
      0x800000000000808aULL, 0x8000000080008000ULL,
      0x000000000000808bULL, 0x0000000080000001ULL,
      0x8000000080008081ULL, 0x8000000000008009ULL,
      0x000000000000008aULL, 0x0000000000000088ULL,
      0x0000000080008009ULL, 0x000000008000000aULL,
      0x000000008000808bULL, 0x800000000000008bULL,
      0x8000000000008089ULL, 0x8000000000008003ULL,
      0x8000000000008002ULL, 0x8000000000000080ULL,
      0x000000000000800aULL, 0x800000008000000aULL,
      0x8000000080008081ULL, 0x8000000000008080ULL,
      0x0000000080000001ULL, 0x8000000080008008ULL};
  static const uint8_t r[25] = {
      0,  1,  62, 28, 27, 36, 44, 6,  55, 20, 3,  10, 43,
      25, 39, 41, 45, 15, 21, 8,  18, 2,  61, 56, 14};

  for (int round = 0; round < KECCAK_ROUNDS; round++) {
    uint64_t c[5], d[5], b[25];

    for (int x = 0; x < 5; x++)
      c[x] = st[x] ^ st[x + 5] ^ st[x + 10] ^ st[x + 15] ^ st[x + 20];
    for (int x = 0; x < 5; x++)
      d[x] = c[(x + 4) % 5] ^ rotl64(c[(x + 1) % 5], 1);
    for (int x = 0; x < 5; x++) {
      for (int y = 0; y < 5; y++)
        st[x + 5 * y] ^= d[x];
    }

    for (int x = 0; x < 5; x++) {
      for (int y = 0; y < 5; y++) {
        int src = x + 5 * y;
        int dst = y + 5 * ((2 * x + 3 * y) % 5);
        b[dst] = rotl64(st[src], r[src]);
      }
    }

    for (int x = 0; x < 5; x++) {
      for (int y = 0; y < 5; y++)
        st[x + 5 * y] =
            b[x + 5 * y] ^ ((~b[((x + 1) % 5) + 5 * y]) &
                            b[((x + 2) % 5) + 5 * y]);
    }

    st[0] ^= rc[round];
  }
}

static void keccak256(const uint8_t *input, size_t input_len,
                      uint8_t out[32]) {
  uint64_t st[25] = {0};
  uint8_t block[136];
  const size_t rate = sizeof(block);

  while (input_len >= rate) {
    for (size_t i = 0; i < rate / 8; i++)
      st[i] ^= load64_le(input + i * 8);
    keccakf(st);
    input += rate;
    input_len -= rate;
  }

  memset(block, 0, sizeof(block));
  if (input_len > 0)
    memcpy(block, input, input_len);
  block[input_len] = 0x01;
  block[rate - 1] |= 0x80;
  for (size_t i = 0; i < rate / 8; i++)
    st[i] ^= load64_le(block + i * 8);
  keccakf(st);

  for (size_t i = 0; i < 4; i++)
    store64_le(out + i * 8, st[i]);

  memset(block, 0, sizeof(block));
  memset(st, 0, sizeof(st));
}

static char hex_digit(uint8_t v) {
  static const char *hex = "0123456789abcdef";
  return hex[v & 0x0f];
}

static void satochip_log_response(const char *stage, esp_err_t err,
                                  uint16_t sw, size_t payload_len) {
  ESP_LOGD(TAG, "%s: err=%s SW=%04X payload=%u", stage, esp_err_to_name(err),
           sw, (unsigned)payload_len);
}

static void satochip_log_parse_error(const char *stage, esp_err_t err,
                                     size_t payload_len) {
  ESP_LOGE(TAG, "%s: parse/decode failed err=%s payload=%u", stage,
           esp_err_to_name(err), (unsigned)payload_len);
}

static const char *satochip_ins_name(uint8_t ins) {
  switch (ins) {
  case 0xA4:
    return "SELECT";
  case 0x3C:
    return "GET_STATUS";
  case SATOCHIP_INS_LOGOUT_ALL:
    return "LOGOUT_ALL";
  case SATOCHIP_INS_VERIFY_PIN:
    return "VERIFY_PIN";
  case SATOCHIP_INS_BIP32_IMPORT_SEED:
    return "BIP32_IMPORT_SEED";
  case SATOCHIP_INS_BIP32_GET_AUTHENTIKEY:
    return "GET_AUTHENTIKEY";
  case SATOCHIP_INS_BIP32_SET_AUTHENTIKEY_PUBKEY:
    return "SET_AUTHENTIKEY_PUBKEY";
  case SATOCHIP_INS_BIP32_GET_EXTENDED_KEY:
    return "GET_EXTENDED_KEY";
  case SATOCHIP_INS_SIGN_TRANSACTION_HASH:
    return "SIGN_HASH";
  case SATOCHIP_INS_INIT_SECURE_CHANNEL:
    return "INIT_SECURE_CHANNEL";
  case SATOCHIP_INS_PROCESS_SECURE_CHANNEL:
    return "PROCESS_SECURE_CHANNEL";
  case SATOCHIP_INS_CHALLENGE_RESPONSE_PKI:
    return "CHALLENGE_RESPONSE_PKI";
  case SATOCHIP_INS_IMPORT_TRUSTED_PUBKEY:
    return "IMPORT_TRUSTED_PUBKEY";
  case SATOCHIP_INS_EXPORT_TRUSTED_PUBKEY:
    return "EXPORT_TRUSTED_PUBKEY";
  default:
    return "APDU";
  }
}

static void checksum_address(char lower[41], const uint8_t hash[32]) {
  for (int i = 0; i < 40; i++) {
    uint8_t nibble = (i & 1) ? (hash[i / 2] & 0x0f) : (hash[i / 2] >> 4);
    if (nibble >= 8)
      lower[i] = (char)toupper((unsigned char)lower[i]);
  }
}

static esp_err_t evm_address_from_uncompressed(const uint8_t pubkey[65],
                                               char out[43]) {
  if (!pubkey || !out || pubkey[0] != 0x04)
    return ESP_ERR_INVALID_ARG;

  uint8_t hash[32];
  keccak256(pubkey + 1, 64, hash);

  char lower[41];
  for (int i = 0; i < 20; i++) {
    lower[i * 2] = hex_digit(hash[12 + i] >> 4);
    lower[i * 2 + 1] = hex_digit(hash[12 + i]);
  }
  lower[40] = '\0';

  uint8_t checksum[32];
  keccak256((const uint8_t *)lower, 40, checksum);
  checksum_address(lower, checksum);

  out[0] = '0';
  out[1] = 'x';
  memcpy(out + 2, lower, 41);
  memset(hash, 0, sizeof(hash));
  memset(checksum, 0, sizeof(checksum));
  return ESP_OK;
}

static void write_be32(uint8_t *out, uint32_t value) {
  out[0] = (uint8_t)(value >> 24);
  out[1] = (uint8_t)(value >> 16);
  out[2] = (uint8_t)(value >> 8);
  out[3] = (uint8_t)value;
}

static uint16_t read_be16(const uint8_t *buf) {
  return ((uint16_t)buf[0] << 8) | (uint16_t)buf[1];
}

static uint32_t read_be32(const uint8_t *buf) {
  return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
         ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
}

static void secure_zero(void *ptr, size_t len) {
  if (!ptr || len == 0)
    return;
  volatile uint8_t *p = (volatile uint8_t *)ptr;
  while (len--)
    *p++ = 0;
}

static void copy_trimmed_ascii(char *dst, size_t dst_len, const char *src) {
  if (!dst || dst_len == 0)
    return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  snprintf(dst, dst_len, "%s", src);
}

static void satochip_apdu_result_reset(smartcard_satochip_apdu_result_t *out) {
  if (!out)
    return;
  memset(out, 0, sizeof(*out));
  out->err = ESP_ERR_INVALID_STATE;
}

static void satochip_apdu_result_fill(smartcard_satochip_apdu_result_t *out,
                                      const uint8_t *response,
                                      size_t response_len, uint16_t sw,
                                      esp_err_t err, const char *detail) {
  if (!out)
    return;
  if (response && response_len > 0) {
    if (response_len > sizeof(out->response))
      response_len = sizeof(out->response);
    memcpy(out->response, response, response_len);
    out->response_len = response_len;
  }
  out->sw = sw;
  out->err = err;
  if (detail && detail[0])
    snprintf(out->detail, sizeof(out->detail), "%s", detail);
}

static void satochip_apdu_result_append(
    smartcard_satochip_apdu_result_t *out, const uint8_t *response,
    size_t response_len) {
  if (!out || !response || response_len == 0)
    return;
  if (out->response_len >= sizeof(out->response))
    return;

  size_t copy_len = response_len;
  size_t space = sizeof(out->response) - out->response_len;
  if (copy_len > space)
    copy_len = space;
  memcpy(out->response + out->response_len, response, copy_len);
  out->response_len += copy_len;
}

static void satochip_bytes_to_hex(const uint8_t *data, size_t len, char *out,
                                  size_t out_len) {
  if (!out || out_len == 0)
    return;
  out[0] = '\0';
  if (!data || len == 0)
    return;
  size_t pos = 0;
  for (size_t i = 0; i < len && pos + 2 < out_len; i++) {
    int written = snprintf(out + pos, out_len - pos, "%02x", data[i]);
    if (written <= 0)
      break;
    pos += (size_t)written;
  }
}

static void satochip_bytes_to_hex_spaced(const uint8_t *data, size_t len,
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

static bool satochip_text_contains_pem_markers(const char *text) {
  return text && strstr(text, "BEGIN CERTIFICATE") != NULL;
}

static esp_err_t satochip_base64_decode_blob(const char *input, uint8_t *out,
                                             size_t out_cap, size_t *out_len) {
  if (!input || !out || !out_len)
    return ESP_ERR_INVALID_ARG;

  size_t input_len = strlen(input);
  size_t needed = 0;
  int ret = mbedtls_base64_decode(out, out_cap, &needed,
                                  (const unsigned char *)input, input_len);
  if (ret == 0) {
    *out_len = needed;
    return ESP_OK;
  }
  return ESP_FAIL;
}

static esp_err_t satochip_pem_to_der(const char *pem_or_b64, uint8_t *out,
                                     size_t out_cap, size_t *out_len) {
  if (!pem_or_b64 || !out || !out_len)
    return ESP_ERR_INVALID_ARG;

  if (!satochip_text_contains_pem_markers(pem_or_b64))
    return satochip_base64_decode_blob(pem_or_b64, out, out_cap, out_len);

  const char *begin = strstr(pem_or_b64, "-----BEGIN CERTIFICATE-----");
  const char *end = strstr(pem_or_b64, "-----END CERTIFICATE-----");
  if (!begin || !end || end <= begin)
    return ESP_ERR_INVALID_ARG;
  begin += strlen("-----BEGIN CERTIFICATE-----");

  char compact[4096];
  size_t pos = 0;
  while (begin < end && pos + 1 < sizeof(compact)) {
    unsigned char ch = (unsigned char)*begin++;
    if (ch == '\r' || ch == '\n' || ch == ' ' || ch == '\t')
      continue;
    compact[pos++] = (char)ch;
  }
  compact[pos] = '\0';
  esp_err_t err = satochip_base64_decode_blob(compact, out, out_cap, out_len);
  secure_zero(compact, sizeof(compact));
  return err;
}

static esp_err_t satochip_der_to_pem(const uint8_t *der, size_t der_len,
                                     char *pem, size_t pem_len) {
  if (!der || !pem || pem_len == 0)
    return ESP_ERR_INVALID_ARG;

  size_t base64_len = 0;
  int ret = mbedtls_base64_encode(NULL, 0, &base64_len, der, der_len);
  if (ret != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL)
    return ESP_FAIL;

  unsigned char *base64 = calloc(1, base64_len + 1);
  if (!base64)
    return ESP_ERR_NO_MEM;

  size_t actual_len = 0;
  ret = mbedtls_base64_encode(base64, base64_len, &actual_len, der, der_len);
  if (ret != 0) {
    free(base64);
    return ESP_FAIL;
  }

  size_t pos = 0;
  int written = snprintf(pem + pos, pem_len - pos,
                         "-----BEGIN CERTIFICATE-----\n");
  if (written < 0 || (size_t)written >= pem_len) {
    free(base64);
    return ESP_ERR_INVALID_SIZE;
  }
  pos += (size_t)written;

  for (size_t i = 0; i < actual_len; i += 64) {
    size_t line_len = actual_len - i;
    if (line_len > 64)
      line_len = 64;
    if (pos + line_len + 2 >= pem_len) {
      free(base64);
      return ESP_ERR_INVALID_SIZE;
    }
    memcpy(pem + pos, base64 + i, line_len);
    pos += line_len;
    pem[pos++] = '\n';
  }

  written = snprintf(pem + pos, pem_len - pos, "-----END CERTIFICATE-----\n");
  if (written < 0 || (size_t)written >= pem_len - pos) {
    free(base64);
    return ESP_ERR_INVALID_SIZE;
  }
  free(base64);
  return ESP_OK;
}

static bool is_secure_channel_plain_ins(uint8_t ins) {
  return ins == 0xA4 || ins == SATOCHIP_INS_INIT_SECURE_CHANNEL ||
         ins == SATOCHIP_INS_PROCESS_SECURE_CHANNEL || ins == 0xFF ||
         ins == 0x3C;
}

static int satochip_mbedtls_rng(void *ctx, unsigned char *buf, size_t len) {
  (void)ctx;
  crypto_random_bytes(buf, len);
  return 0;
}

static esp_err_t satochip_hmac_sha1(const uint8_t *key, size_t key_len,
                                    const uint8_t *data, size_t data_len,
                                    uint8_t out[SATOCHIP_SC_MAC_LEN]) {
  if (!key || !data || !out)
    return ESP_ERR_INVALID_ARG;
  const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
  if (!info)
    return ESP_FAIL;
  return mbedtls_md_hmac(info, key, key_len, data, data_len, out) == 0
             ? ESP_OK
             : ESP_FAIL;
}

static esp_err_t satochip_aes128_cbc_crypt(const uint8_t key[16],
                                           const uint8_t iv[16],
                                           const uint8_t *input,
                                           size_t input_len, bool encrypt,
                                           uint8_t *output) {
  if (!key || !iv || !input || !output || input_len == 0 ||
      (input_len % 16U) != 0) {
    return ESP_ERR_INVALID_ARG;
  }

  uint8_t iv_copy[16];
  memcpy(iv_copy, iv, sizeof(iv_copy));
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  int ret = encrypt ? mbedtls_aes_setkey_enc(&aes, key, 128)
                    : mbedtls_aes_setkey_dec(&aes, key, 128);
  if (ret == 0) {
    ret = mbedtls_aes_crypt_cbc(&aes, encrypt ? MBEDTLS_AES_ENCRYPT
                                              : MBEDTLS_AES_DECRYPT,
                                input_len, iv_copy, input, output);
  }
  mbedtls_aes_free(&aes);
  secure_zero(iv_copy, sizeof(iv_copy));
  return ret == 0 ? ESP_OK : ESP_FAIL;
}

static esp_err_t satochip_pkcs7_pad(const uint8_t *input, size_t input_len,
                                    uint8_t *output, size_t output_cap,
                                    size_t *output_len) {
  if (!input || !output || !output_len)
    return ESP_ERR_INVALID_ARG;
  size_t pad = 16U - (input_len % 16U);
  if (pad == 0)
    pad = 16;
  if (input_len + pad > output_cap)
    return ESP_ERR_INVALID_SIZE;
  if (input_len > 0)
    memcpy(output, input, input_len);
  memset(output + input_len, (int)pad, pad);
  *output_len = input_len + pad;
  return ESP_OK;
}

static esp_err_t satochip_pkcs7_unpad(uint8_t *data, size_t data_len,
                                      size_t *unpadded_len) {
  if (!data || !unpadded_len || data_len == 0 || (data_len % 16U) != 0)
    return ESP_ERR_INVALID_ARG;
  uint8_t pad = data[data_len - 1];
  if (pad == 0 || pad > 16 || pad > data_len)
    return ESP_ERR_INVALID_RESPONSE;
  for (size_t i = data_len - pad; i < data_len; i++) {
    if (data[i] != pad)
      return ESP_ERR_INVALID_RESPONSE;
  }
  *unpadded_len = data_len - pad;
  return ESP_OK;
}

static esp_err_t satochip_ecdh_shared_secret_x(
    const uint8_t privkey[32], const uint8_t peer_pubkey[65],
    uint8_t shared_x[32]) {
  if (!privkey || !peer_pubkey || !shared_x || peer_pubkey[0] != 0x04)
    return ESP_ERR_INVALID_ARG;

  esp_err_t err = ESP_FAIL;
  mbedtls_ecp_group grp;
  mbedtls_ecp_point peer;
  mbedtls_ecp_point shared;
  mbedtls_mpi d;
  mbedtls_ecp_group_init(&grp);
  mbedtls_ecp_point_init(&peer);
  mbedtls_ecp_point_init(&shared);
  mbedtls_mpi_init(&d);

  if (mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256K1) != 0)
    goto out;
  if (mbedtls_ecp_point_read_binary(&grp, &peer, peer_pubkey, 65) != 0)
    goto out;
  if (mbedtls_ecp_check_pubkey(&grp, &peer) != 0)
    goto out;
  if (mbedtls_mpi_read_binary(&d, privkey, 32) != 0)
    goto out;
  if (mbedtls_ecp_check_privkey(&grp, &d) != 0)
    goto out;
  if (mbedtls_ecp_mul(&grp, &shared, &d, &peer, satochip_mbedtls_rng, NULL) !=
      0)
    goto out;
  if (mbedtls_mpi_write_binary(&shared.MBEDTLS_PRIVATE(X), shared_x, 32) != 0)
    goto out;
  err = ESP_OK;

out:
  mbedtls_mpi_free(&d);
  mbedtls_ecp_point_free(&shared);
  mbedtls_ecp_point_free(&peer);
  mbedtls_ecp_group_free(&grp);
  return err;
}

static esp_err_t satochip_make_host_keypair(uint8_t privkey[32],
                                            uint8_t pubkey[65]) {
  if (!privkey || !pubkey)
    return ESP_ERR_INVALID_ARG;

  for (int attempts = 0; attempts < 32; attempts++) {
    crypto_random_bytes(privkey, 32);
    if (wally_ec_private_key_verify(privkey, 32) != WALLY_OK)
      continue;

    uint8_t compressed[33];
    if (wally_ec_public_key_from_private_key(privkey, 32, compressed,
                                             sizeof(compressed)) == WALLY_OK &&
        wally_ec_public_key_decompress(compressed, sizeof(compressed), pubkey,
                                       65) == WALLY_OK) {
      secure_zero(compressed, sizeof(compressed));
      return ESP_OK;
    }
    secure_zero(compressed, sizeof(compressed));
  }
  secure_zero(privkey, 32);
  return ESP_FAIL;
}

static esp_err_t satochip_path_to_bytes(const char *path, uint8_t *out,
                                        size_t out_cap, size_t *out_len) {
  if (!path || !out || !out_len)
    return ESP_ERR_INVALID_ARG;

  const char *p = path;
  if (p[0] == 'm') {
    p++;
    if (*p == '/')
      p++;
    else if (*p != '\0')
      return ESP_ERR_INVALID_ARG;
  }

  size_t count = 0;
  while (*p) {
    if (count >= SATOCHIP_MAX_PATH_COMPONENTS || (count + 1) * 4 > out_cap)
      return ESP_ERR_INVALID_SIZE;

    uint32_t value = 0;
    bool has_digits = false;
    while (*p >= '0' && *p <= '9') {
      if (value > (UINT32_MAX - (uint32_t)(*p - '0')) / 10U)
        return ESP_ERR_INVALID_ARG;
      value = value * 10U + (uint32_t)(*p - '0');
      has_digits = true;
      p++;
    }
    if (!has_digits)
      return ESP_ERR_INVALID_ARG;

    if (*p == '\'' || *p == 'h' || *p == 'H') {
      if (value >= 0x80000000U)
        return ESP_ERR_INVALID_ARG;
      value |= 0x80000000U;
      p++;
    }

    write_be32(out + count * 4, value);
    count++;

    if (*p == '/') {
      p++;
      if (*p == '\0')
        return ESP_ERR_INVALID_ARG;
    } else if (*p != '\0') {
      return ESP_ERR_INVALID_ARG;
    }
  }

  *out_len = count * 4;
  return ESP_OK;
}

static esp_err_t satochip_transmit_checked(const uint8_t *apdu, size_t apdu_len,
                                           uint8_t *response,
                                           size_t response_cap,
                                           size_t *payload_len, uint16_t *sw,
                                           uint32_t timeout_ms) {
  uint8_t ins = apdu_len > 1 ? apdu[1] : 0;
  size_t response_len = 0;
  esp_err_t err = smartcard_transport_transmit_apdu(
      apdu, apdu_len, response, response_cap, &response_len, sw,
      timeout_ms ? timeout_ms : SATOCHIP_DEFAULT_TIMEOUT_MS);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "%s plain transmit failed: %s", satochip_ins_name(ins),
             esp_err_to_name(err));
    return err;
  }
  if (response_len < 2) {
    ESP_LOGE(TAG, "%s response too short: %u", satochip_ins_name(ins),
             (unsigned)response_len);
    return ESP_ERR_INVALID_RESPONSE;
  }
  if (payload_len)
    *payload_len = response_len - 2;
  ESP_LOGD(TAG, "%s plain response SW=%04X payload=%u",
           satochip_ins_name(ins), sw ? *sw : 0, (unsigned)(response_len - 2));
  return ESP_OK;
}

static esp_err_t satochip_recover_pubkey_from_der_sig(
    const uint8_t coordx[32], const uint8_t *msg, size_t msg_len,
    const uint8_t *der_sig, size_t der_sig_len, uint8_t compressed_out[33]);

static esp_err_t satochip_parse_secure_channel_response(
    const uint8_t *payload, size_t payload_len, const uint8_t authentikey[33],
    const uint8_t authentikey_coordx[32], uint8_t card_pubkey_out[65]) {
  if (!payload || !card_pubkey_out || payload_len < 2)
    return ESP_ERR_INVALID_RESPONSE;

  uint16_t data_size = read_be16(payload);
  size_t msg_size = 2U + data_size;
  if (data_size != 32 || payload_len < msg_size + 2)
    return ESP_ERR_INVALID_RESPONSE;

  uint16_t sig_size = read_be16(payload + msg_size);
  if (sig_size == 0 || payload_len < msg_size + 2U + sig_size)
    return ESP_ERR_INVALID_RESPONSE;

  uint8_t card_compressed[33];
  const uint8_t *coordx = payload + 2;
  esp_err_t err = satochip_recover_pubkey_from_der_sig(
      coordx, payload, msg_size, payload + msg_size + 2, sig_size,
      card_compressed);
  if (err != ESP_OK)
    return err;

  size_t msg2_size = msg_size + 2U + sig_size;
  uint16_t sig2_size = 0;
  if (payload_len >= msg2_size + 2U)
    sig2_size = read_be16(payload + msg2_size);
  if (sig2_size > 0 && authentikey && authentikey_coordx &&
      authentikey[0] != 0) {
    if (payload_len < msg2_size + 2U + sig2_size) {
      secure_zero(card_compressed, sizeof(card_compressed));
      return ESP_ERR_INVALID_RESPONSE;
    }
    uint8_t recovered_authentikey[33];
    err = satochip_recover_pubkey_from_der_sig(
        authentikey_coordx, payload, msg2_size, payload + msg2_size + 2,
        sig2_size, recovered_authentikey);
    if (err != ESP_OK ||
        memcmp(recovered_authentikey, authentikey, 33) != 0) {
      secure_zero(recovered_authentikey, sizeof(recovered_authentikey));
      secure_zero(card_compressed, sizeof(card_compressed));
      return ESP_ERR_INVALID_RESPONSE;
    }
    secure_zero(recovered_authentikey, sizeof(recovered_authentikey));
  }

  if (wally_ec_public_key_decompress(card_compressed, sizeof(card_compressed),
                                     card_pubkey_out, 65) != WALLY_OK) {
    secure_zero(card_compressed, sizeof(card_compressed));
    return ESP_ERR_INVALID_RESPONSE;
  }
  secure_zero(card_compressed, sizeof(card_compressed));
  return ESP_OK;
}

static esp_err_t satochip_secure_channel_begin(
    satochip_secure_channel_t *sc, const uint8_t authentikey[33],
    const uint8_t authentikey_coordx[32], uint8_t *response,
    size_t response_cap, size_t *payload_len, uint16_t *sw,
    uint32_t timeout_ms) {
  if (!sc || !response || !payload_len || !sw)
    return ESP_ERR_INVALID_ARG;

  memset(sc, 0, sizeof(*sc));
  esp_err_t err = satochip_make_host_keypair(sc->host_privkey, sc->host_pubkey);
  if (err != ESP_OK)
    return err;

  uint8_t init_apdu[5 + 65];
  init_apdu[0] = SATOCHIP_CLA;
  init_apdu[1] = SATOCHIP_INS_INIT_SECURE_CHANNEL;
  init_apdu[2] = 0x00;
  init_apdu[3] = 0x00;
  init_apdu[4] = 65;
  memcpy(init_apdu + 5, sc->host_pubkey, 65);

  err = satochip_transmit_checked(init_apdu, sizeof(init_apdu), response,
                                  response_cap, payload_len, sw, timeout_ms);
  secure_zero(init_apdu, sizeof(init_apdu));
  if (err != ESP_OK)
    return err;
  if (*sw != 0x9000)
    return ESP_ERR_INVALID_RESPONSE;

  err = satochip_parse_secure_channel_response(
      response, *payload_len, authentikey, authentikey_coordx,
      sc->card_pubkey);
  if (err != ESP_OK)
    return err;

  uint8_t shared_x[32];
  uint8_t mac[SATOCHIP_SC_MAC_LEN];
  err = satochip_ecdh_shared_secret_x(sc->host_privkey, sc->card_pubkey,
                                      shared_x);
  if (err != ESP_OK) {
    secure_zero(shared_x, sizeof(shared_x));
    return err;
  }

  static const uint8_t sc_key_label[] = {'s', 'c', '_', 'k', 'e', 'y'};
  static const uint8_t sc_mac_label[] = {'s', 'c', '_', 'm', 'a', 'c'};
  err = satochip_hmac_sha1(shared_x, sizeof(shared_x), sc_key_label,
                           sizeof(sc_key_label), mac);
  if (err == ESP_OK) {
    memcpy(sc->derived_key, mac, sizeof(sc->derived_key));
    err = satochip_hmac_sha1(shared_x, sizeof(shared_x), sc_mac_label,
                             sizeof(sc_mac_label), sc->mac_key);
  }
  secure_zero(mac, sizeof(mac));
  secure_zero(shared_x, sizeof(shared_x));
  if (err != ESP_OK)
    return err;

  sc->iv_counter = 1;
  sc->active = true;
  return ESP_OK;
}

static esp_err_t satochip_secure_channel_encrypt_apdu(
    satochip_secure_channel_t *sc, const uint8_t *plain_apdu,
    size_t plain_apdu_len, uint8_t *encrypted_apdu, size_t encrypted_apdu_cap,
    size_t *encrypted_apdu_len) {
  if (!sc || !sc->active || !plain_apdu || !encrypted_apdu ||
      !encrypted_apdu_len)
    return ESP_ERR_INVALID_ARG;

  uint8_t padded[SMARTCARD_CCID_APDU_MAX_LEN + 16];
  uint8_t ciphertext[sizeof(padded)];
  size_t padded_len = 0;
  esp_err_t err = satochip_pkcs7_pad(plain_apdu, plain_apdu_len, padded,
                                     sizeof(padded), &padded_len);
  if (err != ESP_OK)
    return err;

  uint8_t iv[16];
  crypto_random_bytes(iv, 12);
  write_be32(iv + 12, sc->iv_counter);
  sc->iv_counter += 2;

  err = satochip_aes128_cbc_crypt(sc->derived_key, iv, padded, padded_len,
                                  true, ciphertext);
  secure_zero(padded, sizeof(padded));
  if (err != ESP_OK) {
    secure_zero(iv, sizeof(iv));
    secure_zero(ciphertext, sizeof(ciphertext));
    return err;
  }

  size_t data_len = 16U + 2U + padded_len + 2U + SATOCHIP_SC_MAC_LEN;
  if (data_len > 255 || 5U + data_len > encrypted_apdu_cap) {
    secure_zero(iv, sizeof(iv));
    secure_zero(ciphertext, sizeof(ciphertext));
    return ESP_ERR_INVALID_SIZE;
  }

  uint8_t mac_input[16 + 2 + sizeof(ciphertext)];
  memcpy(mac_input, iv, 16);
  mac_input[16] = (uint8_t)(padded_len >> 8);
  mac_input[17] = (uint8_t)padded_len;
  memcpy(mac_input + 18, ciphertext, padded_len);

  uint8_t mac[SATOCHIP_SC_MAC_LEN];
  err = satochip_hmac_sha1(sc->mac_key, sizeof(sc->mac_key), mac_input,
                           18U + padded_len, mac);
  secure_zero(mac_input, sizeof(mac_input));
  if (err != ESP_OK) {
    secure_zero(iv, sizeof(iv));
    secure_zero(ciphertext, sizeof(ciphertext));
    return err;
  }

  encrypted_apdu[0] = SATOCHIP_CLA;
  encrypted_apdu[1] = SATOCHIP_INS_PROCESS_SECURE_CHANNEL;
  encrypted_apdu[2] = 0x00;
  encrypted_apdu[3] = 0x00;
  encrypted_apdu[4] = (uint8_t)data_len;
  memcpy(encrypted_apdu + 5, iv, 16);
  encrypted_apdu[21] = (uint8_t)(padded_len >> 8);
  encrypted_apdu[22] = (uint8_t)padded_len;
  memcpy(encrypted_apdu + 23, ciphertext, padded_len);
  encrypted_apdu[23 + padded_len] = 0x00;
  encrypted_apdu[24 + padded_len] = SATOCHIP_SC_MAC_LEN;
  memcpy(encrypted_apdu + 25 + padded_len, mac, SATOCHIP_SC_MAC_LEN);
  *encrypted_apdu_len = 5U + data_len;

  secure_zero(iv, sizeof(iv));
  secure_zero(ciphertext, sizeof(ciphertext));
  secure_zero(mac, sizeof(mac));
  return ESP_OK;
}

static esp_err_t satochip_secure_channel_decrypt_response(
    satochip_secure_channel_t *sc, uint8_t *response, size_t response_cap,
    size_t *payload_len) {
  if (!sc || !sc->active || !response || !payload_len)
    return ESP_ERR_INVALID_ARG;
  if (*payload_len == 0)
    return ESP_OK;
  if (*payload_len < 18) {
    ESP_LOGE(TAG, "secure response too short: payload=%u",
             (unsigned)*payload_len);
    return ESP_ERR_INVALID_RESPONSE;
  }

  uint8_t iv[16];
  memcpy(iv, response, sizeof(iv));
  size_t ciphertext_len = read_be16(response + 16);
  if (ciphertext_len == 0 || (*payload_len != 18U + ciphertext_len) ||
      ciphertext_len > response_cap || (ciphertext_len % 16U) != 0) {
    ESP_LOGE(TAG, "secure response length mismatch: payload=%u cipher=%u",
             (unsigned)*payload_len, (unsigned)ciphertext_len);
    secure_zero(iv, sizeof(iv));
    return ESP_ERR_INVALID_RESPONSE;
  }

  uint8_t plaintext[SMARTCARD_CCID_RESPONSE_MAX_LEN];
  if (ciphertext_len > sizeof(plaintext)) {
    secure_zero(iv, sizeof(iv));
    return ESP_ERR_INVALID_SIZE;
  }
  esp_err_t err = satochip_aes128_cbc_crypt(sc->derived_key, iv, response + 18,
                                            ciphertext_len, false, plaintext);
  secure_zero(iv, sizeof(iv));
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "secure response AES decrypt failed: %s",
             esp_err_to_name(err));
    secure_zero(plaintext, sizeof(plaintext));
    return err;
  }

  size_t plain_len = 0;
  err = satochip_pkcs7_unpad(plaintext, ciphertext_len, &plain_len);
  if (err != ESP_OK || plain_len > response_cap) {
    ESP_LOGE(TAG, "secure response unpad failed: %s plain=%u cap=%u",
             esp_err_to_name(err), (unsigned)plain_len,
             (unsigned)response_cap);
    secure_zero(plaintext, sizeof(plaintext));
    return err == ESP_OK ? ESP_ERR_INVALID_SIZE : err;
  }
  memcpy(response, plaintext, plain_len);
  *payload_len = plain_len;
  secure_zero(plaintext, sizeof(plaintext));
  ESP_LOGD(TAG, "secure response decrypted payload=%u", (unsigned)plain_len);
  return ESP_OK;
}

static esp_err_t satochip_transmit_card(
    satochip_secure_channel_t *sc, const uint8_t *apdu, size_t apdu_len,
    uint8_t *response, size_t response_cap, size_t *payload_len, uint16_t *sw,
    uint32_t timeout_ms) {
  if (!apdu || apdu_len < 2)
    return ESP_ERR_INVALID_ARG;

  if (!sc || !sc->active || is_secure_channel_plain_ins(apdu[1])) {
    return satochip_transmit_checked(apdu, apdu_len, response, response_cap,
                                     payload_len, sw, timeout_ms);
  }

  uint8_t encrypted_apdu[SMARTCARD_CCID_APDU_MAX_LEN];
  size_t encrypted_apdu_len = 0;
  esp_err_t err = satochip_secure_channel_encrypt_apdu(
      sc, apdu, apdu_len, encrypted_apdu, sizeof(encrypted_apdu),
      &encrypted_apdu_len);
  if (err != ESP_OK)
    return err;

  err = satochip_transmit_checked(encrypted_apdu, encrypted_apdu_len, response,
                                  response_cap, payload_len, sw, timeout_ms);
  secure_zero(encrypted_apdu, sizeof(encrypted_apdu));
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "%s secure transmit failed: %s", satochip_ins_name(apdu[1]),
             esp_err_to_name(err));
    return err;
  }
  ESP_LOGD(TAG, "%s secure envelope response SW=%04X encrypted_payload=%u",
           satochip_ins_name(apdu[1]), sw ? *sw : 0,
           (unsigned)(payload_len ? *payload_len : 0));
  if (*sw != 0x9000)
    return ESP_OK;
  err = satochip_secure_channel_decrypt_response(sc, response, response_cap,
                                                 payload_len);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "%s secure decrypt failed: %s", satochip_ins_name(apdu[1]),
             esp_err_to_name(err));
  } else {
    ESP_LOGD(TAG, "%s secure plaintext payload=%u", satochip_ins_name(apdu[1]),
             (unsigned)(payload_len ? *payload_len : 0));
  }
  return err;
}

static esp_err_t smartcard_select_applet(const uint8_t *select_apdu,
                                         size_t select_apdu_len,
                                         const char *applet_name,
                                         uint16_t *select_sw,
                                         bool *selected,
                                         uint32_t timeout_ms) {
  if (!select_apdu || select_apdu_len == 0)
    return ESP_ERR_INVALID_ARG;

  uint8_t response[SMARTCARD_CCID_RESPONSE_MAX_LEN];
  size_t response_len = 0;
  uint16_t sw = 0;
  esp_err_t err = smartcard_transport_transmit_apdu(
      select_apdu, select_apdu_len, response, sizeof(response), &response_len,
      &sw, timeout_ms ? timeout_ms : SATOCHIP_DEFAULT_TIMEOUT_MS);
  if (select_sw)
    *select_sw = sw;
  if (selected)
    *selected = (err == ESP_OK && sw == 0x9000);
  if (err == ESP_OK && sw == 0x9000 && applet_name)
    ESP_LOGD(TAG, "SELECT %s ok", applet_name);
  return err;
}

static esp_err_t satochip_recover_pubkey_from_der_sig(
    const uint8_t coordx[32], const uint8_t *msg, size_t msg_len,
    const uint8_t *der_sig, size_t der_sig_len, uint8_t compressed_out[33]) {
  if (!coordx || !msg || !der_sig || !compressed_out)
    return ESP_ERR_INVALID_ARG;

  uint8_t digest[SHA256_LEN];
  uint8_t compact[EC_SIGNATURE_LEN];
  if (wally_sha256(msg, msg_len, digest, sizeof(digest)) != WALLY_OK)
    return ESP_FAIL;
  if (wally_ec_sig_from_der(der_sig, der_sig_len, compact, sizeof(compact)) !=
      WALLY_OK) {
    memset(digest, 0, sizeof(digest));
    return ESP_ERR_INVALID_RESPONSE;
  }

  for (uint8_t recid = 0; recid < 4; recid++) {
    uint8_t recoverable[EC_SIGNATURE_RECOVERABLE_LEN];
    uint8_t recovered[EC_PUBLIC_KEY_LEN];
    recoverable[0] = (uint8_t)(27 + recid + 4);
    memcpy(recoverable + 1, compact, sizeof(compact));
    if (wally_ec_sig_to_public_key(digest, sizeof(digest), recoverable,
                                   sizeof(recoverable), recovered,
                                   sizeof(recovered)) == WALLY_OK &&
        memcmp(recovered + 1, coordx, 32) == 0) {
      memcpy(compressed_out, recovered, 33);
      memset(digest, 0, sizeof(digest));
      memset(compact, 0, sizeof(compact));
      memset(recoverable, 0, sizeof(recoverable));
      memset(recovered, 0, sizeof(recovered));
      return ESP_OK;
    }
    memset(recoverable, 0, sizeof(recoverable));
    memset(recovered, 0, sizeof(recovered));
  }

  memset(digest, 0, sizeof(digest));
  memset(compact, 0, sizeof(compact));
  return ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t satochip_parse_der_signature_compact(
    const uint8_t *der_sig, size_t der_sig_len, uint8_t compact_out[64]) {
  if (!der_sig || der_sig_len == 0 || !compact_out)
    return ESP_ERR_INVALID_ARG;

  uint8_t compact[EC_SIGNATURE_LEN];
  if (wally_ec_sig_from_der(der_sig, der_sig_len, compact, sizeof(compact)) !=
      WALLY_OK) {
    secure_zero(compact, sizeof(compact));
    return ESP_ERR_INVALID_RESPONSE;
  }

  if (wally_ec_sig_normalize(compact, sizeof(compact), compact_out, 64) !=
      WALLY_OK) {
    secure_zero(compact, sizeof(compact));
    secure_zero(compact_out, 64);
    return ESP_ERR_INVALID_RESPONSE;
  }

  secure_zero(compact, sizeof(compact));
  return ESP_OK;
}

static esp_err_t satochip_recover_signature_recid(
    const uint8_t digest[32], const uint8_t compact_sig[64],
    const uint8_t expected_compressed_pubkey[33], uint8_t *recid_out) {
  if (!digest || !compact_sig || !expected_compressed_pubkey || !recid_out)
    return ESP_ERR_INVALID_ARG;

  for (uint8_t recid = 0; recid < 4; recid++) {
    uint8_t recoverable[EC_SIGNATURE_RECOVERABLE_LEN];
    uint8_t recovered[EC_PUBLIC_KEY_LEN];
    recoverable[0] = (uint8_t)(27 + recid + 4);
    memcpy(recoverable + 1, compact_sig, 64);
    if (wally_ec_sig_to_public_key(digest, 32, recoverable,
                                   sizeof(recoverable), recovered,
                                   sizeof(recovered)) == WALLY_OK &&
        memcmp(recovered, expected_compressed_pubkey, 33) == 0) {
      *recid_out = recid & 1U;
      secure_zero(recoverable, sizeof(recoverable));
      secure_zero(recovered, sizeof(recovered));
      return ESP_OK;
    }
    secure_zero(recoverable, sizeof(recoverable));
    secure_zero(recovered, sizeof(recovered));
  }

  return ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t satochip_sha1_digest(const uint8_t *data, size_t data_len,
                                      uint8_t out[20]) {
  if (!data || !out)
    return ESP_ERR_INVALID_ARG;
  const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
  if (!info)
    return ESP_FAIL;
  return mbedtls_md(info, data, data_len, out) == 0 ? ESP_OK : ESP_FAIL;
}

static bool satochip_ascii_ieq(const char *a, const char *b) {
  if (!a || !b)
    return false;
  while (*a && *b) {
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
      return false;
    a++;
    b++;
  }
  return *a == '\0' && *b == '\0';
}

static bool satochip_extract_subject_cn(const mbedtls_x509_crt *crt,
                                        char *out, size_t out_len) {
  if (!crt || !out || out_len == 0)
    return false;

  char dn[512];
  int ret = mbedtls_x509_dn_gets(dn, sizeof(dn), &crt->subject);
  if (ret < 0)
    return false;

  const char *p = dn;
  while (p && *p) {
    const char *token_end = strchr(p, ',');
    size_t token_len = token_end ? (size_t)(token_end - p) : strlen(p);
    while (token_len > 0 && isspace((unsigned char)p[0])) {
      p++;
      token_len--;
    }
    while (token_len > 0 && isspace((unsigned char)p[token_len - 1]))
      token_len--;

    if (token_len >= 2 && p[0] == 'C' && p[1] == 'N') {
      size_t i = 2;
      while (i < token_len && isspace((unsigned char)p[i]))
        i++;
      if (i < token_len && (p[i] == '=' || p[i] == ':')) {
        i++;
        while (i < token_len && isspace((unsigned char)p[i]))
          i++;
        size_t copy_len = token_len - i;
        while (copy_len > 0 &&
               isspace((unsigned char)p[i + copy_len - 1]))
          copy_len--;
        if (copy_len >= out_len)
          copy_len = out_len - 1;
        if (copy_len > 0)
          memcpy(out, p + i, copy_len);
        out[copy_len] = '\0';
        secure_zero(dn, sizeof(dn));
        return true;
      }
    }

    if (!token_end)
      break;
    p = token_end + 1;
    while (*p == ' ')
      p++;
  }

  secure_zero(dn, sizeof(dn));
  return false;
}

static esp_err_t satochip_x509_crt_info_text(const mbedtls_x509_crt *crt,
                                             char *out, size_t out_len) {
  if (!crt || !out || out_len == 0)
    return ESP_ERR_INVALID_ARG;
  out[0] = '\0';
  int ret = mbedtls_x509_crt_info(out, out_len, "", crt);
  return ret < 0 ? ESP_FAIL : ESP_OK;
}

static esp_err_t satochip_extract_x509_pubkey(
    const mbedtls_x509_crt *crt, uint8_t pubkey_out[65]) {
  if (!crt || !pubkey_out)
    return ESP_ERR_INVALID_ARG;

  mbedtls_ecp_keypair *ec = mbedtls_pk_ec(crt->pk);
  if (!ec)
    return ESP_ERR_INVALID_RESPONSE;

  size_t olen = 0;
  int ret = mbedtls_ecp_point_write_binary(
      &ec->MBEDTLS_PRIVATE(grp), &ec->MBEDTLS_PRIVATE(Q),
      MBEDTLS_ECP_PF_UNCOMPRESSED, &olen, pubkey_out, 65);
  if (ret != 0 || olen != 65)
    return ESP_ERR_INVALID_RESPONSE;
  return ESP_OK;
}

static void satochip_compress_uncompressed_pubkey(const uint8_t uncompressed[65],
                                                  uint8_t compressed[33]) {
  if (!uncompressed || !compressed)
    return;
  compressed[0] = (uncompressed[64] & 1U) ? 0x03 : 0x02;
  memcpy(compressed + 1, uncompressed + 1, 32);
}

static esp_err_t satochip_verify_der_signature_against_compressed_pubkey(
    const uint8_t *message, size_t message_len, const uint8_t *der_sig,
    size_t der_sig_len, const uint8_t expected_compressed_pubkey[33],
    char *error, size_t error_len) {
  if (error && error_len > 0)
    error[0] = '\0';
  if (!message || !der_sig || !expected_compressed_pubkey)
    return ESP_ERR_INVALID_ARG;

  uint8_t digest[32];
  esp_err_t err =
      crypto_sha256(message, message_len, digest) == CRYPTO_OK ? ESP_OK
                                                               : ESP_FAIL;
  if (err != ESP_OK) {
    if (error && error_len > 0)
      snprintf(error, error_len, "Message digest calculation failed.");
    return err;
  }

  uint8_t compact[64];
  err = satochip_parse_der_signature_compact(der_sig, der_sig_len, compact);
  if (err != ESP_OK) {
    if (error && error_len > 0)
      snprintf(error, error_len, "DER signature parsing failed.");
    secure_zero(digest, sizeof(digest));
    secure_zero(compact, sizeof(compact));
    return err;
  }

  uint8_t recid = 0;
  err = satochip_recover_signature_recid(digest, compact,
                                         expected_compressed_pubkey, &recid);
  secure_zero(digest, sizeof(digest));
  secure_zero(compact, sizeof(compact));
  if (err != ESP_OK && error && error_len > 0)
    snprintf(error, error_len, "Signature verification failed.");
  return err;
}

static esp_err_t ensure_reader_ready(uint32_t timeout_ms, char *detail,
                                     size_t detail_len);

static esp_err_t satochip_extract_uid_sha1_via_apdu(char out_hex[41],
                                                    uint32_t timeout_ms) {
  if (!out_hex)
    return ESP_ERR_INVALID_ARG;
  out_hex[0] = '\0';

  esp_err_t err = ensure_reader_ready(timeout_ms, NULL, 0);
  if (err != ESP_OK)
    return err;

  static const uint8_t cplc_apdu[] = {0x80, 0xCA, 0x9F, 0x7F};
  static const uint8_t iin_apdu[] = {0x80, 0xCA, 0x00, 0x42};
  static const uint8_t cin_apdu[] = {0x80, 0xCA, 0x00, 0x45};
  const uint8_t *apdus[] = {cplc_apdu, iin_apdu, cin_apdu};
  const size_t apdu_lens[] = {sizeof(cplc_apdu), sizeof(iin_apdu),
                              sizeof(cin_apdu)};

  uint8_t uid_bytes[128];
  size_t uid_len = 0;
  for (size_t i = 0; i < 3; i++) {
    uint8_t response[SMARTCARD_CCID_RESPONSE_MAX_LEN];
    size_t payload_len = 0;
    uint16_t sw = 0;
    err = satochip_transmit_checked(apdus[i], apdu_lens[i], response,
                                    sizeof(response), &payload_len, &sw,
                                    timeout_ms);
    if (err != ESP_OK)
      return err;
    if (sw != 0x9000)
      return ESP_ERR_INVALID_RESPONSE;
    if (uid_len + payload_len > sizeof(uid_bytes))
      return ESP_ERR_INVALID_SIZE;
    if (payload_len > 0) {
      memcpy(uid_bytes + uid_len, response, payload_len);
      uid_len += payload_len;
    }
  }

  uint8_t sha1[20];
  err = satochip_sha1_digest(uid_bytes, uid_len, sha1);
  secure_zero(uid_bytes, sizeof(uid_bytes));
  if (err != ESP_OK)
    return err;

  satochip_bytes_to_hex(sha1, sizeof(sha1), out_hex, 41);
  secure_zero(sha1, sizeof(sha1));
  return ESP_OK;
}

static esp_err_t satochip_parse_pem_certificate_subject_cn(
    const char *pem, char out_cn[128], uint8_t device_pubkey[65],
    char *ca_text, size_t ca_text_len, char *subca_text, size_t subca_text_len,
    char *device_text, size_t device_text_len) {
  if (!pem || !out_cn)
    return ESP_ERR_INVALID_ARG;

  mbedtls_x509_crt crt;
  mbedtls_x509_crt_init(&crt);

  esp_err_t err = ESP_FAIL;
  int ret = mbedtls_x509_crt_parse(&crt, (const unsigned char *)pem,
                                   strlen(pem) + 1);
  if (ret != 0) {
    err = ESP_ERR_INVALID_RESPONSE;
    goto out;
  }

  if (!satochip_extract_subject_cn(&crt, out_cn, 128)) {
    err = ESP_ERR_INVALID_RESPONSE;
    goto out;
  }

  if (device_pubkey) {
    err = satochip_extract_x509_pubkey(&crt, device_pubkey);
    if (err != ESP_OK)
      goto out;
  }

  if (device_text && device_text_len > 0) {
    err = satochip_x509_crt_info_text(&crt, device_text, device_text_len);
    if (err != ESP_OK)
      goto out;
  }

  if (ca_text && ca_text_len > 0)
    ca_text[0] = '\0';
  if (subca_text && subca_text_len > 0)
    subca_text[0] = '\0';
  err = ESP_OK;

out:
  mbedtls_x509_crt_free(&crt);
  return err;
}

static esp_err_t satochip_x509_parse_text_and_pubkey(const char *pem,
                                                     char *subject_cn,
                                                     size_t subject_cn_len,
                                                     char *device_text,
                                                     size_t device_text_len,
                                                     uint8_t pubkey_out[65]) {
  if (!pem || !subject_cn)
    return ESP_ERR_INVALID_ARG;

  mbedtls_x509_crt crt;
  mbedtls_x509_crt_init(&crt);
  esp_err_t err = ESP_FAIL;
  int ret = mbedtls_x509_crt_parse(&crt, (const unsigned char *)pem,
                                   strlen(pem) + 1);
  if (ret != 0) {
    err = ESP_ERR_INVALID_RESPONSE;
    goto out;
  }

  if (!satochip_extract_subject_cn(&crt, subject_cn, subject_cn_len)) {
    err = ESP_ERR_INVALID_RESPONSE;
    goto out;
  }
  if (device_text && device_text_len > 0) {
    err = satochip_x509_crt_info_text(&crt, device_text, device_text_len);
    if (err != ESP_OK)
      goto out;
  }
  if (pubkey_out) {
    err = satochip_extract_x509_pubkey(&crt, pubkey_out);
    if (err != ESP_OK)
      goto out;
  }

  err = ESP_OK;
out:
  mbedtls_x509_crt_free(&crt);
  return err;
}

typedef struct {
  const char *ca_pem;
  const char *subca_pem;
  const char *label;
} satochip_cert_chain_candidate_t;

static esp_err_t satochip_verify_certificate_chain_candidate(
    const char *device_pem, const satochip_cert_chain_candidate_t *candidate,
    char *ca_text, size_t ca_text_len, char *subca_text, size_t subca_text_len) {
  if (!device_pem || !candidate || !candidate->ca_pem || !candidate->subca_pem)
    return ESP_ERR_INVALID_ARG;

  esp_err_t err = ESP_FAIL;
  mbedtls_x509_crt ca_crt;
  mbedtls_x509_crt subca_crt;
  mbedtls_x509_crt chain_crt;
  mbedtls_x509_crt_init(&ca_crt);
  mbedtls_x509_crt_init(&subca_crt);
  mbedtls_x509_crt_init(&chain_crt);

  int ret = mbedtls_x509_crt_parse(&ca_crt,
                                   (const unsigned char *)candidate->ca_pem,
                                   strlen(candidate->ca_pem) + 1);
  if (ret != 0)
    goto out;

  ret = mbedtls_x509_crt_parse(&subca_crt,
                               (const unsigned char *)candidate->subca_pem,
                               strlen(candidate->subca_pem) + 1);
  if (ret != 0)
    goto out;

  ret = mbedtls_x509_crt_parse(&chain_crt, (const unsigned char *)device_pem,
                               strlen(device_pem) + 1);
  if (ret != 0)
    goto out;

  ret = mbedtls_x509_crt_parse(&chain_crt,
                               (const unsigned char *)candidate->subca_pem,
                               strlen(candidate->subca_pem) + 1);
  if (ret != 0)
    goto out;

  if (ca_text && ca_text_len > 0) {
    err = satochip_x509_crt_info_text(&ca_crt, ca_text, ca_text_len);
    if (err != ESP_OK)
      goto out;
  }
  if (subca_text && subca_text_len > 0) {
    err = satochip_x509_crt_info_text(&subca_crt, subca_text, subca_text_len);
    if (err != ESP_OK)
      goto out;
  }

  uint32_t flags = 0;
  ret = mbedtls_x509_crt_verify(&chain_crt, &ca_crt, NULL, NULL, &flags, NULL,
                                NULL);
  if (ret != 0) {
    char verify_info[256] = {0};
    if (flags != 0) {
      (void)mbedtls_x509_crt_verify_info(verify_info, sizeof(verify_info), "",
                                         flags);
    }
    ESP_LOGW(TAG, "%s chain verify failed ret=%d flags=0x%08" PRIx32 "%s%s",
             candidate->label ? candidate->label : "cert",
             ret, flags, verify_info[0] ? " info=" : "",
             verify_info[0] ? verify_info : "");
    err = ESP_ERR_INVALID_RESPONSE;
    goto out;
  }

  err = ESP_OK;

out:
  mbedtls_x509_crt_free(&chain_crt);
  mbedtls_x509_crt_free(&subca_crt);
  mbedtls_x509_crt_free(&ca_crt);
  return err;
}

static esp_err_t satochip_compute_uid_sha1(char out_hex[41],
                                           uint32_t timeout_ms) {
  if (!out_hex)
    return ESP_ERR_INVALID_ARG;
  out_hex[0] = '\0';

  esp_err_t err = ensure_reader_ready(timeout_ms, NULL, 0);
  if (err != ESP_OK)
    return err;

  static const uint8_t cplc_apdu[] = {0x80, 0xCA, 0x9F, 0x7F};
  static const uint8_t iin_apdu[] = {0x80, 0xCA, 0x00, 0x42};
  static const uint8_t cin_apdu[] = {0x80, 0xCA, 0x00, 0x45};

  uint8_t uid_bytes[128];
  size_t uid_len = 0;
  const uint8_t *apdus[] = {cplc_apdu, iin_apdu, cin_apdu};
  const size_t apdu_lens[] = {sizeof(cplc_apdu), sizeof(iin_apdu),
                              sizeof(cin_apdu)};

  for (size_t i = 0; i < 3; i++) {
    uint8_t response[SMARTCARD_CCID_RESPONSE_MAX_LEN];
    size_t payload_len = 0;
    uint16_t sw = 0;
    err = satochip_transmit_checked(apdus[i], apdu_lens[i], response,
                                    sizeof(response), &payload_len, &sw,
                                    timeout_ms);
    if (err != ESP_OK)
      return err;
    if (sw != 0x9000) {
      ESP_LOGE(TAG, "UID source APDU %u returned SW=%04X", (unsigned)i, sw);
      return ESP_ERR_INVALID_RESPONSE;
    }
    if (uid_len + payload_len > sizeof(uid_bytes))
      return ESP_ERR_INVALID_SIZE;
    if (payload_len > 0) {
      memcpy(uid_bytes + uid_len, response, payload_len);
      uid_len += payload_len;
    }
  }

  uint8_t sha1[20];
  err = satochip_sha1_digest(uid_bytes, uid_len, sha1);
  secure_zero(uid_bytes, sizeof(uid_bytes));
  if (err != ESP_OK)
    return err;

  satochip_bytes_to_hex(sha1, sizeof(sha1), out_hex, 41);
  secure_zero(sha1, sizeof(sha1));
  return ESP_OK;
}

static esp_err_t satochip_parse_trusted_pubkey_response(
    const uint8_t *response, size_t response_len,
    const uint8_t authentikey[33], char *pubkey_hex, size_t pubkey_hex_len,
    char *error, size_t error_len) {
  if (error && error_len > 0)
    error[0] = '\0';
  if (!response || response_len < 2U + 65U + 2U)
    return ESP_ERR_INVALID_RESPONSE;

  uint16_t pubkey_size = read_be16(response);
  if (pubkey_size != 65 || response_len < 2U + pubkey_size + 2U)
    return ESP_ERR_INVALID_RESPONSE;

  const uint8_t *pubkey = response + 2;
  uint16_t sig_size = read_be16(response + 2U + pubkey_size);
  if (sig_size == 0 || response_len < 2U + pubkey_size + 2U + sig_size)
    return ESP_ERR_INVALID_RESPONSE;

  if (pubkey_hex && pubkey_hex_len > 0)
    satochip_bytes_to_hex(pubkey, pubkey_size, pubkey_hex, pubkey_hex_len);

  if (authentikey && authentikey[0] != 0) {
    esp_err_t err = satochip_verify_der_signature_against_compressed_pubkey(
        response, 2U + pubkey_size, response + 2U + pubkey_size + 2U, sig_size,
        authentikey, error, error_len);
    if (err != ESP_OK)
      return err;
  }

  return ESP_OK;
}

static esp_err_t satochip_parse_authentikey_response(const uint8_t *payload,
                                                     size_t payload_len,
                                                     uint8_t pubkey_out[33],
                                                     uint8_t coordx_out[32]) {
  if (!payload || !pubkey_out || !coordx_out || payload_len < 2)
    return ESP_ERR_INVALID_RESPONSE;

  uint16_t data_size = read_be16(payload);
  size_t msg_size = 2U + data_size;
  if (data_size != 32 || payload_len < msg_size + 2) {
    ESP_LOGE(TAG, "authentikey invalid coordx block: data=%u payload=%u",
             (unsigned)data_size, (unsigned)payload_len);
    return ESP_ERR_INVALID_RESPONSE;
  }

  uint16_t sig_size = read_be16(payload + msg_size);
  if (sig_size == 0 || payload_len < msg_size + 2U + sig_size) {
    ESP_LOGE(TAG, "authentikey invalid sig block: sig=%u payload=%u",
             (unsigned)sig_size, (unsigned)payload_len);
    return ESP_ERR_INVALID_RESPONSE;
  }

  const uint8_t *coordx = payload + 2;
  const uint8_t *signature = payload + msg_size + 2;
  esp_err_t err = satochip_recover_pubkey_from_der_sig(
      coordx, payload, msg_size, signature, sig_size, pubkey_out);
  if (err != ESP_OK) {
    satochip_log_parse_error("GET_AUTHENTIKEY signature", err, payload_len);
    return err;
  }

  memcpy(coordx_out, coordx, 32);
  ESP_LOGD(TAG, "GET_AUTHENTIKEY parsed sig=%u", (unsigned)sig_size);
  return ESP_OK;
}

static esp_err_t satochip_session_set_authentikey_pubkey(
    satochip_session_t *session, const uint8_t *authentikey_response,
    size_t authentikey_response_len, uint32_t timeout_ms) {
  if (!session || !session->has_authentikey || !authentikey_response ||
      authentikey_response_len == 0)
    return ESP_ERR_INVALID_ARG;

  uint8_t authentikey_uncompressed[65];
  if (wally_ec_public_key_decompress(session->authentikey, 33,
                                     authentikey_uncompressed,
                                     sizeof(authentikey_uncompressed)) !=
      WALLY_OK) {
    ESP_LOGE(TAG, "SET_AUTHENTIKEY_PUBKEY cannot decompress authentikey");
    secure_zero(authentikey_uncompressed, sizeof(authentikey_uncompressed));
    return ESP_ERR_INVALID_RESPONSE;
  }

  const size_t coordy_len = 32;
  const size_t data_len = authentikey_response_len + 2U + coordy_len;
  if (data_len > 255 || 5U + data_len > SMARTCARD_CCID_APDU_MAX_LEN) {
    secure_zero(authentikey_uncompressed, sizeof(authentikey_uncompressed));
    return ESP_ERR_INVALID_SIZE;
  }

  uint8_t apdu[SMARTCARD_CCID_APDU_MAX_LEN];
  apdu[0] = SATOCHIP_CLA;
  apdu[1] = SATOCHIP_INS_BIP32_SET_AUTHENTIKEY_PUBKEY;
  apdu[2] = 0x00;
  apdu[3] = 0x00;
  apdu[4] = (uint8_t)data_len;
  memcpy(apdu + 5, authentikey_response, authentikey_response_len);
  apdu[5 + authentikey_response_len] = 0x00;
  apdu[6 + authentikey_response_len] = (uint8_t)coordy_len;
  memcpy(apdu + 7 + authentikey_response_len, authentikey_uncompressed + 33,
         coordy_len);

  uint8_t response[SMARTCARD_CCID_RESPONSE_MAX_LEN];
  size_t payload_len = 0;
  uint16_t sw = 0;
  esp_err_t err = satochip_transmit_card(
      &session->sc, apdu, 5U + data_len, response, sizeof(response),
      &payload_len, &sw, timeout_ms);
  session->sw = sw;
  secure_zero(apdu, sizeof(apdu));
  secure_zero(authentikey_uncompressed, sizeof(authentikey_uncompressed));
  satochip_log_response("SET_AUTHENTIKEY_PUBKEY", err, sw, payload_len);

  if (err != ESP_OK)
    return err;
  if (sw != 0x9000)
    return ESP_ERR_INVALID_RESPONSE;
  return ESP_OK;
}

static esp_err_t
satochip_parse_extended_key_response(const uint8_t *payload, size_t payload_len,
                                     const uint8_t authentikey[33],
                                     const uint8_t authentikey_coordx[32],
                                     satochip_extended_key_t *out) {
  if (!payload || !authentikey || !authentikey_coordx || !out ||
      payload_len < 34)
    return ESP_ERR_INVALID_RESPONSE;

  memset(out, 0, sizeof(*out));
  memcpy(out->authentikey, authentikey, 33);
  memcpy(out->authentikey_coordx, authentikey_coordx, 32);
  memcpy(out->chain_code, payload, 32);

  uint16_t data_size =
      (uint16_t)(((payload[32] & 0x7fU) << 8) | payload[33]);
  size_t msg_size = 32U + 2U + data_size;
  if (data_size != 32 || payload_len < msg_size + 2) {
    ESP_LOGE(TAG, "extended key invalid coordx block: data=%u payload=%u",
             (unsigned)data_size, (unsigned)payload_len);
    return ESP_ERR_INVALID_RESPONSE;
  }

  uint16_t sig_size = read_be16(payload + msg_size);
  if (sig_size == 0 || payload_len < msg_size + 2U + sig_size + 2U) {
    ESP_LOGE(TAG, "extended key invalid first sig: sig=%u payload=%u",
             (unsigned)sig_size, (unsigned)payload_len);
    return ESP_ERR_INVALID_RESPONSE;
  }

  const uint8_t *coordx = payload + 34;
  const uint8_t *signature = payload + msg_size + 2;
  esp_err_t err = satochip_recover_pubkey_from_der_sig(
      coordx, payload, msg_size, signature, sig_size, out->pubkey);
  if (err != ESP_OK) {
    satochip_log_parse_error("GET_EXTENDED_KEY first signature", err,
                             payload_len);
    return err;
  }

  size_t msg2_size = msg_size + 2U + sig_size;
  uint16_t sig2_size = read_be16(payload + msg2_size);
  if (sig2_size == 0 || payload_len < msg2_size + 2U + sig2_size) {
    ESP_LOGE(TAG, "extended key invalid authentikey sig: sig=%u payload=%u",
             (unsigned)sig2_size, (unsigned)payload_len);
    return ESP_ERR_INVALID_RESPONSE;
  }

  uint8_t recovered_authentikey[33];
  err = satochip_recover_pubkey_from_der_sig(
      authentikey_coordx, payload, msg2_size, payload + msg2_size + 2,
      sig2_size, recovered_authentikey);
  if (err != ESP_OK) {
    satochip_log_parse_error("GET_EXTENDED_KEY authentikey signature", err,
                             payload_len);
    return err;
  }
  if (memcmp(recovered_authentikey, authentikey, 33) != 0) {
    ESP_LOGE(TAG, "extended key authentikey mismatch");
    memset(recovered_authentikey, 0, sizeof(recovered_authentikey));
    return ESP_ERR_INVALID_RESPONSE;
  }

  memset(recovered_authentikey, 0, sizeof(recovered_authentikey));
  return ESP_OK;
}

static esp_err_t ensure_reader_ready(uint32_t timeout_ms,
                                     char *detail, size_t detail_len) {
  esp_err_t probe_err =
      smartcard_transport_probe(timeout_ms ? timeout_ms : SATOCHIP_DEFAULT_TIMEOUT_MS);
  if (probe_err != ESP_OK && detail && detail_len > 0) {
    snprintf(detail, detail_len, "Reader or card is not ready: %s.",
             esp_err_to_name(probe_err));
  }
  return probe_err;
}

static void parse_status_payload(smartcard_satochip_status_t *status,
                                 const uint8_t *payload, size_t payload_len);

static esp_err_t satochip_session_load_authentikey(satochip_session_t *session,
                                                   uint32_t timeout_ms,
                                                   char *detail,
                                                   size_t detail_len);

static esp_err_t satochip_session_open(satochip_session_t *session,
                                       uint32_t timeout_ms, char *detail,
                                       size_t detail_len) {
  if (!session)
    return ESP_ERR_INVALID_ARG;
  memset(session, 0, sizeof(*session));

  esp_err_t err = ensure_reader_ready(timeout_ms, detail, detail_len);
  if (err != ESP_OK)
    return err;

  uint8_t response[SMARTCARD_CCID_RESPONSE_MAX_LEN];
  size_t payload_len = 0;
  uint16_t sw = 0;
  err = satochip_transmit_checked(
      k_select_satochip_apdu, sizeof(k_select_satochip_apdu), response,
      sizeof(response), &payload_len, &sw, timeout_ms);
  session->sw = sw;
  satochip_log_response("SELECT", err, sw, payload_len);
  if (err != ESP_OK) {
    if (detail && detail_len)
      snprintf(detail, detail_len, "SELECT Satochip send failed: %s.",
               esp_err_to_name(err));
    return err;
  }
  if (sw != 0x9000) {
    if (detail && detail_len)
      snprintf(detail, detail_len, "Satochip applet was not selected, SW=%04X.", sw);
    return ESP_ERR_NOT_FOUND;
  }

  err = satochip_transmit_checked(k_get_status_apdu, sizeof(k_get_status_apdu),
                                  response, sizeof(response), &payload_len, &sw,
                                  timeout_ms);
  session->sw = sw;
  satochip_log_response("GET_STATUS", err, sw, payload_len);
  if (err != ESP_OK) {
    if (detail && detail_len)
      snprintf(detail, detail_len, "GET_STATUS send failed: %s.",
               esp_err_to_name(err));
    return err;
  }
  if (sw == 0x9C04) {
    if (detail && detail_len)
      snprintf(detail, detail_len, "Satochip is not set up; card write initialization is refused.");
    return ESP_ERR_INVALID_STATE;
  }
  if (sw != 0x9000) {
    if (detail && detail_len)
      snprintf(detail, detail_len, "GET_STATUS returned SW=%04X.", sw);
    return ESP_ERR_INVALID_RESPONSE;
  }

  parse_status_payload(&session->status, response, payload_len);
  if (!session->status.status_valid) {
    if (detail && detail_len)
      snprintf(detail, detail_len, "Satochip status fields are incomplete.");
    return ESP_ERR_INVALID_RESPONSE;
  }
  if (!session->status.setup_done) {
    if (detail && detail_len)
      snprintf(detail, detail_len, "Satochip is not set up; card write initialization is refused.");
    return ESP_ERR_INVALID_STATE;
  }
  if (!session->status.is_seeded) {
    if (detail && detail_len)
      snprintf(detail, detail_len, "Satochip has no seed; import seed is refused.");
    return ESP_ERR_INVALID_STATE;
  }
  if (session->status.needs_secure_channel) {
    err = satochip_secure_channel_begin(
        &session->sc, NULL, NULL, response, sizeof(response), &payload_len, &sw,
        timeout_ms);
    session->sw = sw;
    satochip_log_response("INIT_SECURE_CHANNEL", err, sw, payload_len);
    if (err != ESP_OK) {
      if (detail && detail_len)
        snprintf(detail, detail_len, "Secure channel initialization failed: %s SW=%04X.",
                 esp_err_to_name(err), sw);
      return err;
    }
  }

  if (detail && detail_len)
    snprintf(detail, detail_len, "Satochip session established.");
  return ESP_OK;
}

static esp_err_t satochip_seed_import_session_open(satochip_session_t *session,
                                                   uint32_t timeout_ms,
                                                   char *detail,
                                                   size_t detail_len) {
  if (!session)
    return ESP_ERR_INVALID_ARG;
  memset(session, 0, sizeof(*session));

  esp_err_t err = ensure_reader_ready(timeout_ms, detail, detail_len);
  if (err != ESP_OK)
    return err;

  uint8_t response[SMARTCARD_CCID_RESPONSE_MAX_LEN];
  size_t payload_len = 0;
  uint16_t sw = 0;
  err = satochip_transmit_checked(
      k_select_satochip_apdu, sizeof(k_select_satochip_apdu), response,
      sizeof(response), &payload_len, &sw, timeout_ms);
  session->sw = sw;
  satochip_log_response("SELECT", err, sw, payload_len);
  if (err != ESP_OK) {
    if (detail && detail_len)
      snprintf(detail, detail_len, "SELECT Satochip failed: %s.",
               esp_err_to_name(err));
    return err;
  }
  if (sw != 0x9000) {
    if (detail && detail_len)
      snprintf(detail, detail_len, "Satochip was not selected, SW=%04X.", sw);
    return ESP_ERR_NOT_FOUND;
  }

  err = satochip_transmit_checked(k_get_status_apdu, sizeof(k_get_status_apdu),
                                  response, sizeof(response), &payload_len, &sw,
                                  timeout_ms);
  session->sw = sw;
  satochip_log_response("GET_STATUS", err, sw, payload_len);
  if (err != ESP_OK) {
    if (detail && detail_len)
      snprintf(detail, detail_len, "Status read failed: %s.", esp_err_to_name(err));
    return err;
  }
  if (sw == 0x9C04) {
    if (detail && detail_len)
      snprintf(detail, detail_len, "Satochip is not initialized. Run setup first.");
    return ESP_ERR_INVALID_STATE;
  }
  if (sw != 0x9000) {
    if (detail && detail_len)
      snprintf(detail, detail_len, "Status returned SW=%04X.", sw);
    return ESP_ERR_INVALID_RESPONSE;
  }

  parse_status_payload(&session->status, response, payload_len);
  if (!session->status.status_valid) {
    if (detail && detail_len)
      snprintf(detail, detail_len, "Satochip status fields are incomplete.");
    return ESP_ERR_INVALID_RESPONSE;
  }
  if (!session->status.setup_done) {
    if (detail && detail_len)
      snprintf(detail, detail_len, "Satochip is not initialized. Run setup first.");
    return ESP_ERR_INVALID_STATE;
  }

  if (session->status.needs_secure_channel) {
    err = satochip_secure_channel_begin(
        &session->sc, NULL, NULL, response, sizeof(response), &payload_len, &sw,
        timeout_ms);
    session->sw = sw;
    satochip_log_response("INIT_SECURE_CHANNEL", err, sw, payload_len);
    if (err != ESP_OK) {
      if (detail && detail_len)
        snprintf(detail, detail_len, "Secure channel failed: %s SW=%04X.",
                 esp_err_to_name(err), sw);
      return err;
    }
  }

  if (detail && detail_len)
    snprintf(detail, detail_len, "Satochip write session established.");
  return ESP_OK;
}

static esp_err_t satochip_session_load_authentikey(satochip_session_t *session,
                                                   uint32_t timeout_ms,
                                                   char *detail,
                                                   size_t detail_len) {
  if (!session)
    return ESP_ERR_INVALID_ARG;
  if (session->has_authentikey)
    return ESP_OK;

  uint8_t response[SMARTCARD_CCID_RESPONSE_MAX_LEN];
  size_t payload_len = 0;
  uint16_t sw = 0;
  const uint8_t get_authentikey_apdu[] = {
      SATOCHIP_CLA, SATOCHIP_INS_BIP32_GET_AUTHENTIKEY, 0x00, 0x00};
  esp_err_t err = satochip_transmit_card(
      &session->sc, get_authentikey_apdu, sizeof(get_authentikey_apdu),
      response, sizeof(response), &payload_len, &sw, timeout_ms);
  session->sw = sw;
  satochip_log_response("GET_AUTHENTIKEY", err, sw, payload_len);
  if (err != ESP_OK) {
    if (detail && detail_len)
      snprintf(detail, detail_len, "BIP32_GET_AUTHENTIKEY send failed: %s.",
               esp_err_to_name(err));
    return err;
  }
  if (sw == 0x9C06) {
    if (detail && detail_len)
      snprintf(detail, detail_len, "Card requires PIN verification first, SW=%04X.", sw);
    return ESP_ERR_INVALID_STATE;
  }
  if (sw != 0x9000) {
    if (detail && detail_len)
      snprintf(detail, detail_len, "BIP32_GET_AUTHENTIKEY returned SW=%04X.", sw);
    return ESP_ERR_INVALID_RESPONSE;
  }

  err = satochip_parse_authentikey_response(response, payload_len,
                                            session->authentikey,
                                            session->authentikey_coordx);
  if (err != ESP_OK) {
    if (detail && detail_len)
      snprintf(detail, detail_len, "BIP32_GET_AUTHENTIKEY response signature recovery failed.");
    return err;
  }
  session->has_authentikey = true;

  uint8_t authentikey_payload[SMARTCARD_CCID_RESPONSE_MAX_LEN];
  size_t authentikey_payload_len = payload_len;
  if (authentikey_payload_len > sizeof(authentikey_payload))
    authentikey_payload_len = sizeof(authentikey_payload);
  memcpy(authentikey_payload, response, authentikey_payload_len);

  err = satochip_session_set_authentikey_pubkey(
      session, authentikey_payload, authentikey_payload_len, timeout_ms);
  secure_zero(authentikey_payload, sizeof(authentikey_payload));
  if (err != ESP_OK) {
    ESP_LOGW(TAG,
             "SET_AUTHENTIKEY_PUBKEY skipped/failed: %s SW=%04X, continuing",
             esp_err_to_name(err), session->sw);
    err = ESP_OK;
  }

  return ESP_OK;
}

static bool satochip_sw_needs_secure_channel(uint16_t sw);
static esp_err_t satochip_session_verify_pin(satochip_session_t *session,
                                             const char *pin,
                                             uint32_t timeout_ms,
                                             char *detail,
                                             size_t detail_len) {
  if (!session || !pin)
    return ESP_ERR_INVALID_ARG;
  size_t pin_len = strlen(pin);
  if (pin_len == 0 || pin_len > 64) {
    if (detail && detail_len)
      snprintf(detail, detail_len, "Invalid PIN length.");
    return ESP_ERR_INVALID_ARG;
  }

  uint8_t response[SMARTCARD_CCID_RESPONSE_MAX_LEN];
  size_t payload_len = 0;
  uint16_t sw = 0;
  uint8_t verify_apdu[5 + 64];
  verify_apdu[0] = SATOCHIP_CLA;
  verify_apdu[1] = SATOCHIP_INS_VERIFY_PIN;
  verify_apdu[2] = 0x00;
  verify_apdu[3] = 0x00;
  verify_apdu[4] = (uint8_t)pin_len;
  memcpy(verify_apdu + 5, pin, pin_len);

  esp_err_t err = satochip_transmit_card(
      &session->sc, verify_apdu, 5 + pin_len, response, sizeof(response),
      &payload_len, &sw, timeout_ms);
  if (err == ESP_OK && satochip_sw_needs_secure_channel(sw)) {
    err = satochip_secure_channel_begin(&session->sc, NULL, NULL, response,
                                        sizeof(response), &payload_len, &sw,
                                        timeout_ms);
    if (err == ESP_OK) {
      err = satochip_transmit_card(&session->sc, verify_apdu, 5 + pin_len,
                                   response, sizeof(response), &payload_len,
                                   &sw, timeout_ms);
    }
  }
  secure_zero(verify_apdu, sizeof(verify_apdu));
  session->sw = sw;
  satochip_log_response("VERIFY_PIN", err, sw, payload_len);
  if (err != ESP_OK) {
    if (detail && detail_len)
      snprintf(detail, detail_len, "VERIFY_PIN send failed: %s.",
               esp_err_to_name(err));
    return err;
  }
  if (sw == 0x9C0C) {
    if (detail && detail_len)
      snprintf(detail, detail_len, "PIN is locked, SW=%04X.", sw);
    return ESP_ERR_INVALID_STATE;
  }
  if ((sw & 0xFFC0U) == 0x63C0U) {
    if (detail && detail_len)
      snprintf(detail, detail_len, "Wrong PIN, %u attempts remaining, SW=%04X.",
               (unsigned)(sw & 0x000FU), sw);
    return ESP_ERR_INVALID_STATE;
  }
  if (sw != 0x9000) {
    if (detail && detail_len)
      snprintf(detail, detail_len, "VERIFY_PIN returned SW=%04X.", sw);
    return ESP_ERR_INVALID_RESPONSE;
  }
  if (detail && detail_len)
    snprintf(detail, detail_len, "PIN verified.");
  session->pin_verified = true;
  return ESP_OK;
}

static bool satochip_sw_needs_secure_channel(uint16_t sw);
static esp_err_t seedkeeper_session_open(satochip_session_t *session,
                                         uint32_t timeout_ms, char *detail,
                                         size_t detail_len);

static satochip_secure_channel_t s_seedkeeper_compat_sc;
static bool s_seedkeeper_compat_sc_active;

static void seedkeeper_compat_session_clear(void) {
  if (s_seedkeeper_compat_sc_active) {
    secure_zero(&s_seedkeeper_compat_sc, sizeof(s_seedkeeper_compat_sc));
    s_seedkeeper_compat_sc_active = false;
  }
}

static esp_err_t seedkeeper_select_and_verify(const char *pin, bool verify_pin,
                                              uint32_t timeout_ms,
                                              char *detail,
                                              size_t detail_len) {
  seedkeeper_compat_session_clear();

  satochip_session_t session;
  esp_err_t err =
      seedkeeper_session_open(&session, timeout_ms, detail, detail_len);
  if (err != ESP_OK) {
    return err;
  }

  if (verify_pin) {
    if (!pin || pin[0] == '\0') {
      secure_zero(&session, sizeof(session));
      if (detail && detail_len)
        snprintf(detail, detail_len, "Enter the SeedKeeper PIN.");
      return ESP_ERR_INVALID_ARG;
    }
    err = satochip_session_verify_pin(&session, pin, timeout_ms, detail,
                                      detail_len);
    if (err != ESP_OK) {
      secure_zero(&session, sizeof(session));
      return err;
    }
  }

  if (session.sc.active) {
    memcpy(&s_seedkeeper_compat_sc, &session.sc, sizeof(s_seedkeeper_compat_sc));
    s_seedkeeper_compat_sc_active = true;
  }

  if (detail && detail_len)
    snprintf(detail, detail_len, "SeedKeeper session established.");
  secure_zero(&session, sizeof(session));
  return ESP_OK;
}

static bool satochip_sw_needs_secure_channel(uint16_t sw) {
  return sw == 0x9C20 || sw == 0x9C21;
}

static esp_err_t seedkeeper_transmit_checked_compat(
    const uint8_t *apdu, size_t apdu_len, uint8_t *response,
    size_t response_cap, size_t *payload_len, uint16_t *sw,
    uint32_t timeout_ms, const char *stage, char *detail, size_t detail_len) {
  if (!apdu || apdu_len < 4 || !response || !payload_len || !sw)
    return ESP_ERR_INVALID_ARG;

  esp_err_t err;
  if (s_seedkeeper_compat_sc_active) {
    err = satochip_transmit_card(&s_seedkeeper_compat_sc, apdu, apdu_len,
                                 response, response_cap, payload_len, sw,
                                 timeout_ms);
  } else {
    err = satochip_transmit_checked(apdu, apdu_len, response, response_cap,
                                    payload_len, sw, timeout_ms);
    if (err == ESP_OK && satochip_sw_needs_secure_channel(*sw)) {
      err = satochip_secure_channel_begin(
          &s_seedkeeper_compat_sc, NULL, NULL, response, response_cap,
          payload_len, sw, timeout_ms);
      if (err == ESP_OK) {
        s_seedkeeper_compat_sc_active = true;
        err = satochip_transmit_card(&s_seedkeeper_compat_sc, apdu, apdu_len,
                                     response, response_cap, payload_len, sw,
                                     timeout_ms);
      }
    }
  }

  if (err != ESP_OK && detail && detail_len) {
    snprintf(detail, detail_len, "%s send failed: %s.",
             stage ? stage : "SeedKeeper", esp_err_to_name(err));
  }
  return err;
}

static esp_err_t seedkeeper_session_open(satochip_session_t *session,
                                         uint32_t timeout_ms, char *detail,
                                         size_t detail_len) {
  if (!session)
    return ESP_ERR_INVALID_ARG;
  memset(session, 0, sizeof(*session));

  esp_err_t err = ensure_reader_ready(timeout_ms, detail, detail_len);
  if (err != ESP_OK)
    return err;

  uint16_t sw = 0;
  bool selected = false;
  err = smartcard_select_applet(k_select_seedkeeper_apdu,
                                sizeof(k_select_seedkeeper_apdu),
                                "SeedKeeper", &sw, &selected, timeout_ms);
  session->sw = sw;
  if (err != ESP_OK) {
    if (detail && detail_len)
      snprintf(detail, detail_len, "Selecting SeedKeeper failed: %s.",
               esp_err_to_name(err));
    return err;
  }
  if (!selected) {
    if (detail && detail_len)
      snprintf(detail, detail_len, "SeedKeeper was not selected, SW=%04X.", sw);
    return ESP_ERR_NOT_FOUND;
  }

  uint8_t response[SMARTCARD_CCID_RESPONSE_MAX_LEN];
  size_t payload_len = 0;
  err = satochip_transmit_checked(k_seedkeeper_get_status_apdu,
                                  sizeof(k_seedkeeper_get_status_apdu),
                                  response, sizeof(response), &payload_len, &sw,
                                  timeout_ms);
  if (err == ESP_OK && satochip_sw_needs_secure_channel(sw)) {
    err = satochip_secure_channel_begin(
        &session->sc, NULL, NULL, response, sizeof(response), &payload_len, &sw,
        timeout_ms);
    session->sw = sw;
    if (err == ESP_OK) {
      err = satochip_transmit_card(&session->sc, k_seedkeeper_get_status_apdu,
                                   sizeof(k_seedkeeper_get_status_apdu),
                                   response, sizeof(response), &payload_len,
                                   &sw, timeout_ms);
    }
  }
  session->sw = sw;
  if (err != ESP_OK) {
    if (detail && detail_len)
      snprintf(detail, detail_len, "SeedKeeper status read failed: %s.",
               esp_err_to_name(err));
    return err;
  }
  if (sw == 0x9C04) {
    session->status.setup_done = false;
    if (detail && detail_len)
      snprintf(detail, detail_len, "SeedKeeper is not initialized.");
    return ESP_OK;
  }
  if (sw == 0x9C06) {
    session->status.setup_done = true;
    session->status.status_valid = false;
    if (detail && detail_len)
      snprintf(detail, detail_len, "SeedKeeper requires a PIN.");
    return ESP_OK;
  }
  if (sw != 0x9000) {
    if (detail && detail_len)
      snprintf(detail, detail_len, "SeedKeeper status returned SW=%04X.", sw);
    return ESP_ERR_INVALID_RESPONSE;
  }

  session->status.setup_done = true;
  session->status.status_valid = true;
  session->status.needs_secure_channel = session->sc.active;

  if (detail && detail_len)
    snprintf(detail, detail_len, "SeedKeeper session established.");
  return ESP_OK;
}

static esp_err_t seedkeeper_transmit_card(
    satochip_session_t *session, const uint8_t *apdu, size_t apdu_len,
    uint8_t *response, size_t response_cap, size_t *payload_len, uint16_t *sw,
    uint32_t timeout_ms, const char *stage, char *detail, size_t detail_len) {
  if (!session || !apdu || !response || !payload_len || !sw)
    return ESP_ERR_INVALID_ARG;

  esp_err_t err = satochip_transmit_card(&session->sc, apdu, apdu_len, response,
                                         response_cap, payload_len, sw,
                                         timeout_ms);
  if (err == ESP_OK && satochip_sw_needs_secure_channel(*sw)) {
    err = satochip_secure_channel_begin(&session->sc, NULL, NULL, response,
                                        response_cap, payload_len, sw,
                                        timeout_ms);
    if (err != ESP_OK) {
      if (detail && detail_len)
        snprintf(detail, detail_len, "%s secure channel failed: %s SW=%04X.",
                 stage ? stage : "SeedKeeper", esp_err_to_name(err), *sw);
      return err;
    }
    err = satochip_transmit_card(&session->sc, apdu, apdu_len, response,
                                 response_cap, payload_len, sw, timeout_ms);
  }
  session->sw = *sw;
  return err;
}

static esp_err_t seedkeeper_execute_apdu(
    const char *pin, bool verify_pin, const uint8_t *apdu, size_t apdu_len,
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms,
    const char *stage) {
  if (!apdu || apdu_len < 4)
    return ESP_ERR_INVALID_ARG;

  smartcard_satochip_apdu_result_t local;
  if (!out)
    out = &local;
  satochip_apdu_result_reset(out);

  satochip_session_t session;
  char detail[256] = {0};
  esp_err_t err =
      seedkeeper_session_open(&session, timeout_ms, detail, sizeof(detail));
  if (err == ESP_OK && verify_pin) {
    if (!pin)
      err = ESP_ERR_INVALID_ARG;
    else
      err = satochip_session_verify_pin(&session, pin, timeout_ms, detail,
                                        sizeof(detail));
  }
  if (err == ESP_OK) {
    uint8_t response[SMARTCARD_CCID_RESPONSE_MAX_LEN];
    size_t payload_len = 0;
    uint16_t sw = 0;
    err = seedkeeper_transmit_card(&session, apdu, apdu_len, response,
                                   sizeof(response), &payload_len, &sw,
                                   timeout_ms, stage, detail, sizeof(detail));
    if (err == ESP_OK) {
      satochip_apdu_result_fill(out, response, payload_len, sw, ESP_OK,
                                detail);
    } else {
      snprintf(detail, sizeof(detail), "%s send failed: %s.",
               stage ? stage : "SeedKeeper", esp_err_to_name(err));
      satochip_apdu_result_fill(out, NULL, 0, sw, err, detail);
    }
  } else {
    char final_detail[256];
    snprintf(final_detail, sizeof(final_detail), "%s session failed: %s.%s%s",
             stage ? stage : "SeedKeeper", esp_err_to_name(err),
             detail[0] ? "\n" : "", detail);
    satochip_apdu_result_fill(out, NULL, 0, 0, err, final_detail);
  }

  secure_zero(&session, sizeof(session));
  return err;
}

static esp_err_t satochip_execute_apdu(const char *pin, bool verify_pin,
                                       const uint8_t *apdu, size_t apdu_len,
                                       smartcard_satochip_apdu_result_t *out,
                                       uint32_t timeout_ms,
                                       const char *stage) {
  if (!apdu || apdu_len < 4)
    return ESP_ERR_INVALID_ARG;

  smartcard_satochip_apdu_result_t local;
  if (!out)
    out = &local;
  satochip_apdu_result_reset(out);

  satochip_session_t session;
  char detail[256] = {0};
  esp_err_t err = satochip_seed_import_session_open(&session, timeout_ms,
                                                    detail, sizeof(detail));
  if (err == ESP_OK && verify_pin) {
    if (!pin)
      err = ESP_ERR_INVALID_ARG;
    else
      err = satochip_session_verify_pin(&session, pin, timeout_ms, detail,
                                        sizeof(detail));
  }
  if (err == ESP_OK) {
    uint8_t response[SMARTCARD_CCID_RESPONSE_MAX_LEN];
    size_t payload_len = 0;
    uint16_t sw = 0;
    err = satochip_transmit_card(&session.sc, apdu, apdu_len, response,
                                 sizeof(response), &payload_len, &sw,
                                 timeout_ms);
    if (err == ESP_OK && satochip_sw_needs_secure_channel(sw)) {
      err = satochip_secure_channel_begin(&session.sc, NULL, NULL, response,
                                          sizeof(response), &payload_len, &sw,
                                          timeout_ms);
      if (err == ESP_OK) {
        err = satochip_transmit_card(&session.sc, apdu, apdu_len, response,
                                     sizeof(response), &payload_len, &sw,
                                     timeout_ms);
      }
    }
    session.sw = sw;
    if (err == ESP_OK) {
      satochip_apdu_result_fill(out, response, payload_len, sw, ESP_OK, detail);
    } else {
      snprintf(detail, sizeof(detail), "%s send failed: %s.",
               stage ? stage : "APDU", esp_err_to_name(err));
      satochip_apdu_result_fill(out, NULL, 0, sw, err, detail);
    }
  } else {
    snprintf(detail, sizeof(detail), "%s session failed: %s.",
             stage ? stage : "APDU", esp_err_to_name(err));
    satochip_apdu_result_fill(out, NULL, 0, 0, err, detail);
  }

  secure_zero(&session, sizeof(session));
  return err;
}

static void satochip_fill_simple_detail(char *dst, size_t dst_len,
                                        const char *prefix, uint16_t sw) {
  if (!dst || dst_len == 0)
    return;
  if (prefix && prefix[0])
    snprintf(dst, dst_len, "%s SW=%04X.", prefix, sw);
  else
    snprintf(dst, dst_len, "SW=%04X.", sw);
}

static bool satochip_pubkey_fingerprint(const uint8_t pubkey[33],
                                        uint8_t fp_out[4]) {
  if (!pubkey || !fp_out)
    return false;
  uint8_t hash[HASH160_LEN];
  if (wally_hash160(pubkey, 33, hash, sizeof(hash)) != WALLY_OK)
    return false;
  memcpy(fp_out, hash, 4);
  secure_zero(hash, sizeof(hash));
  return true;
}

static esp_err_t satochip_path_to_indices(const char *path, uint32_t *indices,
                                          size_t max_indices,
                                          size_t *depth_out) {
  if (!path || !indices || !depth_out)
    return ESP_ERR_INVALID_ARG;

  uint8_t path_bytes[SATOCHIP_MAX_PATH_COMPONENTS * 4];
  size_t path_len = 0;
  esp_err_t err =
      satochip_path_to_bytes(path, path_bytes, sizeof(path_bytes), &path_len);
  if (err != ESP_OK)
    return err;
  size_t depth = path_len / 4U;
  if (depth > max_indices)
    return ESP_ERR_INVALID_SIZE;
  for (size_t i = 0; i < depth; i++)
    indices[i] = read_be32(path_bytes + i * 4U);
  *depth_out = depth;
  secure_zero(path_bytes, sizeof(path_bytes));
  return ESP_OK;
}

static bool satochip_parent_path(const char *path, char *out, size_t out_len) {
  if (!path || !out || out_len == 0)
    return false;
  snprintf(out, out_len, "%s", path);
  char *slash = strrchr(out, '/');
  if (!slash) {
    snprintf(out, out_len, "m");
    return true;
  }
  if (slash == out || (slash == out + 1 && out[0] == 'm')) {
    snprintf(out, out_len, "m");
    return true;
  }
  *slash = '\0';
  return true;
}

static uint32_t satochip_xpub_version_for(const char *xtype,
                                          bool is_testnet) {
  if (!xtype || xtype[0] == '\0' || strcmp(xtype, "xpub") == 0)
    return is_testnet ? BIP32_VER_TEST_PUBLIC : BIP32_VER_MAIN_PUBLIC;
  if (strcmp(xtype, "tpub") == 0)
    return BIP32_VER_TEST_PUBLIC;
  if (strcmp(xtype, "ypub") == 0)
    return is_testnet ? 0x044a5262U : 0x049d7cb2U;
  if (strcmp(xtype, "upub") == 0)
    return 0x044a5262U;
  if (strcmp(xtype, "zpub") == 0)
    return is_testnet ? 0x045f1cf6U : 0x04b24746U;
  if (strcmp(xtype, "vpub") == 0)
    return 0x045f1cf6U;
  if (strcmp(xtype, "Ypub") == 0)
    return is_testnet ? 0x024289efU : 0x0295b43fU;
  if (strcmp(xtype, "Zpub") == 0)
    return is_testnet ? 0x02575483U : 0x02aa7ed3U;
  return is_testnet ? BIP32_VER_TEST_PUBLIC : BIP32_VER_MAIN_PUBLIC;
}

static esp_err_t satochip_xpub_from_key(
    const smartcard_satochip_account_t *account, const uint8_t parent160[20],
    uint32_t depth, uint32_t child_num, uint32_t version, char *out,
    size_t out_len) {
  if (!account || !account->has_compressed_pubkey || !account->has_chain_code ||
      !parent160 || !out || out_len == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  struct ext_key key;
  int ret = bip32_key_init(
      version == BIP32_VER_TEST_PUBLIC ? BIP32_VER_TEST_PUBLIC
                                       : BIP32_VER_MAIN_PUBLIC,
      depth, child_num, account->chain_code, sizeof(account->chain_code),
      account->compressed_pubkey, sizeof(account->compressed_pubkey), NULL, 0,
      NULL, 0, parent160, 20, &key);
  if (ret != WALLY_OK)
    return ESP_ERR_INVALID_RESPONSE;

  uint8_t serialized[BIP32_SERIALIZED_LEN];
  ret = bip32_key_serialize(&key, BIP32_FLAG_KEY_PUBLIC, serialized,
                            sizeof(serialized));
  secure_zero(&key, sizeof(key));
  if (ret != WALLY_OK) {
    secure_zero(serialized, sizeof(serialized));
    return ESP_ERR_INVALID_RESPONSE;
  }

  serialized[0] = (uint8_t)(version >> 24);
  serialized[1] = (uint8_t)(version >> 16);
  serialized[2] = (uint8_t)(version >> 8);
  serialized[3] = (uint8_t)version;

  char *tmp = NULL;
  ret = wally_base58_from_bytes(serialized, sizeof(serialized),
                                BASE58_FLAG_CHECKSUM, &tmp);
  secure_zero(serialized, sizeof(serialized));
  if (ret != WALLY_OK || !tmp)
    return ESP_ERR_INVALID_RESPONSE;

  size_t len = strlen(tmp);
  if (len + 1 > out_len) {
    wally_free_string(tmp);
    return ESP_ERR_INVALID_SIZE;
  }
  memcpy(out, tmp, len + 1);
  wally_free_string(tmp);
  return ESP_OK;
}

static esp_err_t satochip_btc_address_from_pubkey(
    const uint8_t pubkey[33], smartcard_satochip_btc_script_t script_type,
    bool is_testnet, char *address_out, size_t address_out_len) {
  if (!pubkey || !address_out || address_out_len == 0)
    return ESP_ERR_INVALID_ARG;

  uint8_t script[WALLY_WITNESSSCRIPT_MAX_LEN] = {0};
  size_t script_len = 0;
  char *alloc = NULL;
  int ret = WALLY_EINVAL;

  switch (script_type) {
  case SMARTCARD_SATOCHIP_BTC_P2WPKH:
    ret = wally_witness_program_from_bytes(pubkey, EC_PUBLIC_KEY_LEN,
                                           WALLY_SCRIPT_HASH160, script,
                                           sizeof(script), &script_len);
    if (ret == WALLY_OK)
      ret = wally_addr_segwit_from_bytes(script, script_len,
                                         is_testnet ? "tb" : "bc", 0, &alloc);
    break;

  case SMARTCARD_SATOCHIP_BTC_P2PKH: {
    uint8_t pkh20[HASH160_LEN] = {0};
    ret = wally_hash160(pubkey, EC_PUBLIC_KEY_LEN, pkh20, sizeof(pkh20));
    if (ret == WALLY_OK) {
      script[0] = 0x76;
      script[1] = 0xa9;
      script[2] = 0x14;
      memcpy(script + 3, pkh20, 20);
      script[23] = 0x88;
      script[24] = 0xac;
      script_len = 25;
      ret = wally_scriptpubkey_to_address(
          script, script_len,
          is_testnet ? WALLY_NETWORK_BITCOIN_TESTNET
                     : WALLY_NETWORK_BITCOIN_MAINNET,
          &alloc);
    }
    secure_zero(pkh20, sizeof(pkh20));
    break;
  }

  case SMARTCARD_SATOCHIP_BTC_P2SH_P2WPKH: {
    uint8_t witness_prog[22] = {0};
    size_t witness_prog_len = 0;
    uint8_t sh20[HASH160_LEN] = {0};
    ret = wally_witness_program_from_bytes(pubkey, EC_PUBLIC_KEY_LEN,
                                           WALLY_SCRIPT_HASH160, witness_prog,
                                           sizeof(witness_prog),
                                           &witness_prog_len);
    if (ret == WALLY_OK && witness_prog_len == sizeof(witness_prog))
      ret = wally_hash160(witness_prog, witness_prog_len, sh20, sizeof(sh20));
    if (ret == WALLY_OK) {
      script[0] = 0xa9;
      script[1] = 0x14;
      memcpy(script + 2, sh20, 20);
      script[22] = 0x87;
      script_len = 23;
      ret = wally_scriptpubkey_to_address(
          script, script_len,
          is_testnet ? WALLY_NETWORK_BITCOIN_TESTNET
                     : WALLY_NETWORK_BITCOIN_MAINNET,
          &alloc);
    }
    secure_zero(witness_prog, sizeof(witness_prog));
    secure_zero(sh20, sizeof(sh20));
    break;
  }

  case SMARTCARD_SATOCHIP_BTC_P2TR: {
    uint8_t tweaked_pk33[EC_PUBLIC_KEY_LEN] = {0};
    script[0] = 0x51;
    script[1] = 0x20;
    ret = wally_ec_public_key_bip341_tweak(pubkey, EC_PUBLIC_KEY_LEN, NULL, 0,
                                           0, tweaked_pk33,
                                           EC_PUBLIC_KEY_LEN);
    if (ret == WALLY_OK) {
      memcpy(script + 2, tweaked_pk33 + 1, 32);
      script_len = 34;
      ret = wally_addr_segwit_from_bytes(script, script_len,
                                         is_testnet ? "tb" : "bc", 0, &alloc);
    }
    secure_zero(tweaked_pk33, sizeof(tweaked_pk33));
    break;
  }

  default:
    return ESP_ERR_INVALID_ARG;
  }

  if (ret != WALLY_OK || !alloc) {
    secure_zero(script, sizeof(script));
    return ESP_ERR_INVALID_RESPONSE;
  }
  size_t len = strlen(alloc);
  if (len + 1 > address_out_len) {
    wally_free_string(alloc);
    secure_zero(script, sizeof(script));
    return ESP_ERR_INVALID_SIZE;
  }
  memcpy(address_out, alloc, len + 1);
  wally_free_string(alloc);
  secure_zero(script, sizeof(script));
  return ESP_OK;
}

static esp_err_t satochip_session_read_key(satochip_session_t *session,
                                           const char *path,
                                           smartcard_satochip_account_t *out,
                                           uint32_t timeout_ms) {
  if (!session || !session->has_authentikey || !path || !out)
    return ESP_ERR_INVALID_ARG;

  memset(out, 0, sizeof(*out));
  out->err = ESP_ERR_INVALID_STATE;
  snprintf(out->path, sizeof(out->path), "%s", path);

  uint8_t path_bytes[SATOCHIP_MAX_PATH_COMPONENTS * 4];
  size_t path_len = 0;
  esp_err_t err =
      satochip_path_to_bytes(path, path_bytes, sizeof(path_bytes), &path_len);
  if (err != ESP_OK) {
    out->err = err;
    snprintf(out->detail, sizeof(out->detail),
             "BIP32 path is invalid or too long: %s.", path);
    return err;
  }

  uint8_t response[SMARTCARD_CCID_RESPONSE_MAX_LEN];
  size_t payload_len = 0;
  uint16_t sw = 0;
  uint8_t get_ext_apdu[5 + sizeof(path_bytes)];
  get_ext_apdu[0] = SATOCHIP_CLA;
  get_ext_apdu[1] = SATOCHIP_INS_BIP32_GET_EXTENDED_KEY;
  get_ext_apdu[2] = (uint8_t)(path_len / 4U);
  get_ext_apdu[3] = 0x40;
  get_ext_apdu[4] = (uint8_t)path_len;
  if (path_len > 0)
    memcpy(get_ext_apdu + 5, path_bytes, path_len);

  err = satochip_transmit_card(&session->sc, get_ext_apdu, 5 + path_len,
                               response, sizeof(response), &payload_len, &sw,
                               timeout_ms);
  satochip_log_response("GET_EXTENDED_KEY", err, sw, payload_len);
  if (err == ESP_OK && sw == 0x9C01) {
    ESP_LOGW(TAG, "GET_EXTENDED_KEY no memory for %s, retrying with reset flag",
             path);
    get_ext_apdu[3] ^= 0x80;
    err = satochip_transmit_card(&session->sc, get_ext_apdu, 5 + path_len,
                                 response, sizeof(response), &payload_len, &sw,
                                 timeout_ms);
    satochip_log_response("GET_EXTENDED_KEY retry", err, sw, payload_len);
  }
  secure_zero(get_ext_apdu, sizeof(get_ext_apdu));
  session->sw = sw;
  out->sw = sw;
  if (err != ESP_OK) {
    out->err = err;
    snprintf(out->detail, sizeof(out->detail),
             "BIP32_GET_EXTENDED_KEY send failed: %s.", esp_err_to_name(err));
    return err;
  }
  if (sw != 0x9000) {
    out->err = ESP_ERR_INVALID_RESPONSE;
    snprintf(out->detail, sizeof(out->detail),
             "BIP32_GET_EXTENDED_KEY returned SW=%04X.", sw);
    return out->err;
  }

  satochip_extended_key_t ext;
  err = satochip_parse_extended_key_response(
      response, payload_len, session->authentikey, session->authentikey_coordx,
      &ext);
  if (err != ESP_OK) {
    satochip_log_parse_error("GET_EXTENDED_KEY", err, payload_len);
    out->err = err;
    snprintf(out->detail, sizeof(out->detail),
             "Extended key response parsing failed: coordx/DER signature cannot be recovered or verified.");
    secure_zero(&ext, sizeof(ext));
    return err;
  }

  memcpy(out->compressed_pubkey, ext.pubkey, sizeof(out->compressed_pubkey));
  memcpy(out->chain_code, ext.chain_code, sizeof(out->chain_code));
  out->has_compressed_pubkey = true;
  out->has_chain_code = true;

  if (wally_ec_public_key_decompress(out->compressed_pubkey,
                                     sizeof(out->compressed_pubkey),
                                     out->uncompressed_pubkey,
                                     sizeof(out->uncompressed_pubkey)) !=
      WALLY_OK) {
    out->err = ESP_ERR_INVALID_RESPONSE;
    snprintf(out->detail, sizeof(out->detail), "Public key decompression failed.");
    secure_zero(&ext, sizeof(ext));
    return out->err;
  }
  out->has_uncompressed_pubkey = true;

  err = evm_address_from_uncompressed(out->uncompressed_pubkey, out->address);
  if (err != ESP_OK) {
    out->err = err;
    snprintf(out->detail, sizeof(out->detail), "EVM address calculation failed.");
    secure_zero(&ext, sizeof(ext));
    return err;
  }
  out->has_address = true;
  out->err = ESP_OK;
  snprintf(out->detail, sizeof(out->detail), "Satochip key read successfully: %s.",
           path);
  secure_zero(&ext, sizeof(ext));
  return ESP_OK;
}

static void parse_status_payload(smartcard_satochip_status_t *status,
                                 const uint8_t *payload, size_t payload_len) {
  if (!status)
    return;

  status->status_valid = false;
  if (!payload || payload_len < 4)
    return;

  status->protocol_major = payload[0];
  status->protocol_minor = payload[1];
  status->applet_major = payload[2];
  status->applet_minor = payload[3];

  if (payload_len >= 8) {
    status->pin0_remaining = payload[4];
    status->puk0_remaining = payload[5];
    status->pin1_remaining = payload[6];
    status->puk1_remaining = payload[7];
  }
  if (payload_len >= 9)
    status->needs_2fa = payload[8] != 0;
  if (payload_len >= 10)
    status->is_seeded = payload[9] != 0;
  if (payload_len >= 11)
    status->setup_done = payload[10] != 0;
  else
    status->setup_done = true;
  if (payload_len >= 12)
    status->needs_secure_channel = payload[11] != 0;
  if (payload_len >= 13)
    status->nfc_policy = payload[12];
  if (payload_len >= 16) {
    status->feature_schnorr_policy = payload[13];
    status->feature_nostr_policy = payload[14];
    status->feature_liquid_policy = payload[15];
  }

  status->status_valid = true;
}

esp_err_t smartcard_satochip_read_status(smartcard_satochip_status_t *out,
                                         uint32_t timeout_ms) {
  if (!out)
    return ESP_ERR_INVALID_ARG;

  memset(out, 0, sizeof(*out));
  out->transport_error = ESP_ERR_INVALID_STATE;
  snprintf(out->detail, sizeof(out->detail), "Connecting to Satochip.");

  esp_err_t probe_err = smartcard_transport_probe(timeout_ms ? timeout_ms : 30000);
  if (probe_err != ESP_OK) {
    out->transport_error = probe_err;
    snprintf(out->detail, sizeof(out->detail),
             "Reader or card is not ready: %s.", esp_err_to_name(probe_err));
    return probe_err;
  }

  uint8_t response[SMARTCARD_CCID_RESPONSE_MAX_LEN];
  size_t response_len = 0;
  uint16_t sw = 0;
  esp_err_t err =
      smartcard_transport_transmit_apdu(k_select_satochip_apdu,
                                        sizeof(k_select_satochip_apdu),
                                        response, sizeof(response),
                                        &response_len, &sw,
                                        timeout_ms ? timeout_ms : 30000);
  out->transport_error = err;
  out->select_sw = sw;
  if (err != ESP_OK) {
    snprintf(out->detail, sizeof(out->detail), "SELECT Satochip send failed: %s.",
             esp_err_to_name(err));
    return err;
  }
  if (sw != 0x9000) {
    snprintf(out->detail, sizeof(out->detail),
             "Satochip applet was not selected, SW=%04X.", sw);
    return ESP_ERR_NOT_FOUND;
  }
  out->app_selected = true;

  memset(response, 0, sizeof(response));
  response_len = 0;
  sw = 0;
  err = smartcard_transport_transmit_apdu(k_get_status_apdu,
                                          sizeof(k_get_status_apdu), response,
                                          sizeof(response), &response_len, &sw,
                                          timeout_ms ? timeout_ms : 30000);
  out->transport_error = err;
  out->status_sw = sw;
  if (err != ESP_OK) {
    snprintf(out->detail, sizeof(out->detail), "GET STATUS send failed: %s.",
             esp_err_to_name(err));
    return err;
  }

  size_t copy_len = response_len;
  if (copy_len > sizeof(out->raw_status))
    copy_len = sizeof(out->raw_status);
  if (copy_len > 0)
    memcpy(out->raw_status, response, copy_len);
  out->raw_status_len = copy_len;

  if (sw == 0x9C04) {
    out->setup_done = false;
    snprintf(out->detail, sizeof(out->detail), "Satochip detected; the card is not initialized.");
    return ESP_OK;
  }
  if (sw != 0x9000) {
    snprintf(out->detail, sizeof(out->detail),
             "Satochip status returned a non-success code, SW=%04X.", sw);
    return ESP_OK;
  }
  if (response_len < 2) {
    snprintf(out->detail, sizeof(out->detail), "Satochip status response is too short.");
    return ESP_ERR_INVALID_RESPONSE;
  }

  parse_status_payload(out, response, response_len - 2);
  if (!out->status_valid) {
    snprintf(out->detail, sizeof(out->detail), "Satochip status fields are incomplete.");
    return ESP_ERR_INVALID_RESPONSE;
  }

  snprintf(out->detail, sizeof(out->detail), "Satochip status read successfully.");
  return ESP_OK;
}

esp_err_t smartcard_satochip_get_label(smartcard_satochip_label_t *out,
                                       uint32_t timeout_ms) {
  if (!out)
    return ESP_ERR_INVALID_ARG;

  memset(out, 0, sizeof(*out));
  out->err = ESP_ERR_INVALID_STATE;
  snprintf(out->detail, sizeof(out->detail), "Reading card label.");

  esp_err_t probe_err = smartcard_transport_probe(timeout_ms ? timeout_ms : 30000);
  if (probe_err != ESP_OK) {
    out->err = probe_err;
    snprintf(out->detail, sizeof(out->detail),
             "Reader or card is not ready: %s.", esp_err_to_name(probe_err));
    return probe_err;
  }

  uint16_t sw = 0;
  bool selected = false;
  esp_err_t err = smartcard_select_applet(
      k_select_satochip_apdu, sizeof(k_select_satochip_apdu), "Satochip",
      &sw, &selected, timeout_ms);
  out->select_sw = sw;
  if (err != ESP_OK) {
    out->err = err;
    snprintf(out->detail, sizeof(out->detail), "Selecting Satochip failed: %s.",
             esp_err_to_name(err));
    return err;
  }
  if (selected) {
    copy_trimmed_ascii(out->applet, sizeof(out->applet), "Satochip");
    out->app_selected = true;
  } else {
    err = smartcard_select_applet(
        k_select_seedkeeper_apdu, sizeof(k_select_seedkeeper_apdu),
        "SeedKeeper", &sw, &selected, timeout_ms);
    out->select_sw = sw;
    if (err != ESP_OK) {
      out->err = err;
      snprintf(out->detail, sizeof(out->detail),
               "Selecting SeedKeeper failed: %s.", esp_err_to_name(err));
      return err;
    }
    if (selected) {
      copy_trimmed_ascii(out->applet, sizeof(out->applet), "SeedKeeper");
      out->app_selected = true;
    }
  }

  if (!out->app_selected) {
    snprintf(out->detail, sizeof(out->detail),
             "Could not select Satochip or SeedKeeper, SW=%04X.", out->select_sw);
    out->err = ESP_ERR_NOT_FOUND;
    return out->err;
  }

  uint8_t response[SMARTCARD_CCID_RESPONSE_MAX_LEN];
  size_t response_len = 0;
  uint16_t label_sw = 0;
  satochip_secure_channel_t sc;
  memset(&sc, 0, sizeof(sc));
  err = satochip_transmit_checked(k_get_label_apdu, sizeof(k_get_label_apdu),
                                  response, sizeof(response), &response_len,
                                  &label_sw, timeout_ms ? timeout_ms : 30000);
  if (err == ESP_OK && satochip_sw_needs_secure_channel(label_sw)) {
    err = satochip_secure_channel_begin(
        &sc, NULL, NULL, response, sizeof(response), &response_len, &label_sw,
        timeout_ms ? timeout_ms : 30000);
    if (err == ESP_OK) {
      err = satochip_transmit_card(&sc, k_get_label_apdu,
                                   sizeof(k_get_label_apdu), response,
                                   sizeof(response), &response_len, &label_sw,
                                   timeout_ms ? timeout_ms : 30000);
    }
  }
  secure_zero(&sc, sizeof(sc));
  out->label_sw = label_sw;

  if (err == ESP_OK && label_sw == 0x9C06) {
    out->err = ESP_OK;
    snprintf(out->detail, sizeof(out->detail),
             "Card requires Smartcard PIN before reading the label.");
    return ESP_OK;
  }

  if (err != ESP_OK) {
    out->err = err;
    snprintf(out->detail, sizeof(out->detail), "Label read failed: %s.",
             esp_err_to_name(err));
    return err;
  }

  size_t payload_len = response_len >= 2 ? response_len - 2 : 0;
  if (label_sw == 0x9000 && payload_len >= 1) {
    size_t label_len = response[0];
    size_t copy_len = payload_len - 1;
    if (label_len < copy_len)
      copy_len = label_len;
    if (copy_len >= sizeof(out->label))
      copy_len = sizeof(out->label) - 1;
    if (copy_len > 0)
      memcpy(out->label, response + 1, copy_len);
    out->label[copy_len] = '\0';
    out->has_label = true;
    out->raw_label_len = payload_len < sizeof(out->raw_label) ? payload_len
                                                              : sizeof(out->raw_label);
    memcpy(out->raw_label, response, out->raw_label_len);
    snprintf(out->detail, sizeof(out->detail), "Label read successfully.");
    out->err = ESP_OK;
    return ESP_OK;
  }

  if (label_sw == 0x6D00) {
    snprintf(out->label, sizeof(out->label), "(none)");
    out->has_label = true;
    snprintf(out->detail, sizeof(out->detail), "This card does not support labels.");
    out->err = ESP_OK;
    return ESP_OK;
  }

  snprintf(out->detail, sizeof(out->detail), "Label read failed, SW=%04X.",
           label_sw);
  out->err = ESP_OK;
  return ESP_OK;
}

esp_err_t smartcard_seedkeeper_read_status(const char *pin,
                                           smartcard_seedkeeper_status_t *out,
                                           uint32_t timeout_ms) {
  if (!out)
    return ESP_ERR_INVALID_ARG;

  memset(out, 0, sizeof(*out));
  out->err = ESP_ERR_INVALID_STATE;
  snprintf(out->detail, sizeof(out->detail), "Reading SeedKeeper.");

  satochip_session_t session;
  char detail[192] = {0};
  esp_err_t err =
      seedkeeper_session_open(&session, timeout_ms, detail, sizeof(detail));
  if (err != ESP_OK) {
    out->err = err;
    snprintf(out->detail, sizeof(out->detail), "%s",
             detail[0] ? detail : esp_err_to_name(err));
    return err;
  }
  out->app_selected = true;
  out->select_sw = 0x9000;
  if (pin && pin[0] != '\0') {
    err = satochip_session_verify_pin(&session, pin, timeout_ms, detail,
                                      sizeof(detail));
    if (err != ESP_OK) {
      out->err = err;
      snprintf(out->detail, sizeof(out->detail), "%s",
               detail[0] ? detail : esp_err_to_name(err));
      secure_zero(&session, sizeof(session));
      return err;
    }
  }
  if (session.sw == 0x9C04) {
    out->status_sw = session.sw;
    out->err = ESP_OK;
    snprintf(out->detail, sizeof(out->detail), "SeedKeeper is not initialized.");
    out->status_valid = false;
    secure_zero(&session, sizeof(session));
    return ESP_OK;
  }
  if (session.sw == 0x9C06 && (!pin || pin[0] == '\0')) {
    out->status_sw = session.sw;
    out->err = ESP_OK;
    snprintf(out->detail, sizeof(out->detail), "SeedKeeper requires a PIN.");
    out->status_valid = false;
    secure_zero(&session, sizeof(session));
    return ESP_OK;
  }

  uint8_t response[SMARTCARD_CCID_RESPONSE_MAX_LEN];
  size_t payload_len = 0;
  uint16_t status_sw = 0;
  err = seedkeeper_transmit_card(
      &session, k_seedkeeper_get_status_apdu,
      sizeof(k_seedkeeper_get_status_apdu), response, sizeof(response),
      &payload_len, &status_sw, timeout_ms, "SeedKeeper status", detail,
      sizeof(detail));
  out->status_sw = status_sw;
  if (err != ESP_OK) {
    out->err = err;
    snprintf(out->detail, sizeof(out->detail), "%s",
             detail[0] ? detail : esp_err_to_name(err));
    secure_zero(&session, sizeof(session));
    return err;
  }
  if (status_sw != 0x9000) {
    out->err = ESP_OK;
    if (status_sw == 0x9C06)
      snprintf(out->detail, sizeof(out->detail), "SeedKeeper requires a PIN.");
    else
      snprintf(out->detail, sizeof(out->detail),
               "SeedKeeper status returned SW=%04X.", status_sw);
    secure_zero(&session, sizeof(session));
    return ESP_OK;
  }
  size_t copy_len = payload_len < sizeof(out->raw_status) ? payload_len
                                                          : sizeof(out->raw_status);
  if (copy_len > 0)
    memcpy(out->raw_status, response, copy_len);
  out->raw_status_len = copy_len;
  if (payload_len < 17) {
    out->err = ESP_ERR_INVALID_RESPONSE;
    snprintf(out->detail, sizeof(out->detail), "SeedKeeper status response is too short.");
    secure_zero(&session, sizeof(session));
    return out->err;
  }

  out->nb_secrets = read_be16(response);
  out->total_memory = read_be16(response + 2);
  out->free_memory = read_be16(response + 4);
  out->nb_logs_total = read_be16(response + 6);
  out->nb_logs_avail = read_be16(response + 8);
  memcpy(out->last_log, response + 10, sizeof(out->last_log));
  out->last_log_len = sizeof(out->last_log);
  out->status_valid = true;
  out->err = ESP_OK;
  snprintf(out->detail, sizeof(out->detail), "SeedKeeper status read successfully.");
  secure_zero(&session, sizeof(session));
  return ESP_OK;
}

esp_err_t smartcard_satochip_get_eth_account(const char *pin, const char *path,
                                             smartcard_satochip_account_t *out,
                                             uint32_t timeout_ms) {
  if (!pin || !path || !out)
    return ESP_ERR_INVALID_ARG;

  memset(out, 0, sizeof(*out));
  out->err = ESP_ERR_INVALID_STATE;
  snprintf(out->path, sizeof(out->path), "%s", path);
  snprintf(out->detail, sizeof(out->detail), "Reading Satochip ETH account.");

  satochip_session_t session;
  esp_err_t err =
      satochip_session_open(&session, timeout_ms, out->detail,
                            sizeof(out->detail));
  if (err == ESP_OK) {
    err = satochip_session_verify_pin(&session, pin, timeout_ms, out->detail,
                                      sizeof(out->detail));
  }
  if (err == ESP_OK) {
    err = satochip_session_load_authentikey(&session, timeout_ms, out->detail,
                                            sizeof(out->detail));
  }
  if (err == ESP_OK)
    err = satochip_session_read_key(&session, path, out, timeout_ms);
  out->sw = session.sw;
  out->err = err;
  secure_zero(&session, sizeof(session));
  return err;
}

static bool satochip_read_optional_web3_key(
    satochip_session_t *session, const char *path,
    smartcard_satochip_account_t *out, uint32_t timeout_ms) {
  if (!session || !path || !out)
    return false;

  esp_err_t err = satochip_session_read_key(session, path, out, timeout_ms);
  if (err == ESP_OK)
    return true;

  ESP_LOGW(TAG, "optional Web3 key skipped: path=%s err=%s SW=%04X detail=%s",
           path, esp_err_to_name(err), out->sw, out->detail);
  secure_zero(out, sizeof(*out));
  return false;
}

esp_err_t smartcard_satochip_get_web3_account(
    const char *pin, smartcard_satochip_web3_account_t *out,
    uint32_t timeout_ms) {
  if (!pin || !out)
    return ESP_ERR_INVALID_ARG;

  memset(out, 0, sizeof(*out));
  out->err = ESP_ERR_INVALID_STATE;
  snprintf(out->detail, sizeof(out->detail), "Reading Satochip Web3 account.");

  satochip_session_t session;
  esp_err_t err =
      satochip_session_open(&session, timeout_ms, out->detail,
                            sizeof(out->detail));
  if (err == ESP_OK)
    err = satochip_session_verify_pin(&session, pin, timeout_ms, out->detail,
                                      sizeof(out->detail));
  if (err == ESP_OK)
    err = satochip_session_load_authentikey(&session, timeout_ms, out->detail,
                                            sizeof(out->detail));
  if (err != ESP_OK)
    goto out;

  err = satochip_session_read_key(&session, "m/44'/60'/0'/0/0",
                                  &out->address_key, timeout_ms);
  if (err != ESP_OK) {
    snprintf(out->detail, sizeof(out->detail), "Address path read failed: %s",
             out->address_key.detail);
    goto out;
  }
  err = satochip_session_read_key(&session, "m/44'/60'/0'",
                                  &out->account_key, timeout_ms);
  if (err != ESP_OK) {
    snprintf(out->detail, sizeof(out->detail), "Account path read failed: %s",
             out->account_key.detail);
    goto out;
  }

  smartcard_satochip_account_t master;
  memset(&master, 0, sizeof(master));
  if (satochip_read_optional_web3_key(&session, "m", &master, timeout_ms) &&
      master.has_compressed_pubkey) {
    out->has_master_fingerprint = satochip_pubkey_fingerprint(
        master.compressed_pubkey, out->master_fingerprint);
  }
  secure_zero(&master, sizeof(master));

  if (satochip_read_optional_web3_key(&session, "m/44'/60'",
                                      &out->parent_key, timeout_ms) &&
      out->parent_key.has_compressed_pubkey) {
    out->has_parent_fingerprint = satochip_pubkey_fingerprint(
        out->parent_key.compressed_pubkey, out->parent_fingerprint);
  }

  for (size_t i = 0; i < 10; i++) {
    char path[48];
    snprintf(path, sizeof(path), "m/44'/60'/%u'/0/0", (unsigned)i);
    if (satochip_read_optional_web3_key(
            &session, path, &out->ledger_live[out->ledger_live_count],
            timeout_ms)) {
      out->ledger_live_count++;
    }
  }

  const char *btc_paths[] = {"m/84'/0'/0'",  "m/49'/0'/0'",
                             "m/44'/0'/0'",  "m/44'/195'/0'",
                             "m/49'/2'/0'",  "m/44'/5'/0'",
                             "m/44'/145'/0'", "m/86'/0'/0'"};
  for (size_t i = 0; i < sizeof(btc_paths) / sizeof(btc_paths[0]); i++) {
    if (satochip_read_optional_web3_key(
            &session, btc_paths[i], &out->btc[out->btc_count], timeout_ms)) {
      out->btc_count++;
    }
  }

  out->err = ESP_OK;
  snprintf(out->detail, sizeof(out->detail),
           "Satochip Web3 account read successfully: EVM %s, OKX %u, BTC %u.",
           out->address_key.address, (unsigned)out->ledger_live_count,
           (unsigned)out->btc_count);

out:
  out->sw = err == ESP_OK && out->account_key.sw ? out->account_key.sw
                                                 : session.sw;
  if (err != ESP_OK)
    out->err = err;
  secure_zero(&session, sizeof(session));
  return err;
}

static const char *satochip_descriptor_wrapper_for_xtype(const char *xtype) {
  if (xtype && (strcmp(xtype, "zpub") == 0 || strcmp(xtype, "vpub") == 0))
    return "wpkh";
  if (xtype && (strcmp(xtype, "ypub") == 0 || strcmp(xtype, "upub") == 0))
    return "sh-wpkh";
  if (xtype && strcmp(xtype, "Zpub") == 0)
    return "wsh";
  if (xtype && strcmp(xtype, "Ypub") == 0)
    return "sh-wsh";
  return "pkh";
}

static void satochip_origin_without_m(const char *path, char *out,
                                      size_t out_len) {
  if (!out || out_len == 0)
    return;
  if (!path) {
    out[0] = '\0';
    return;
  }
  if (path[0] == 'm' && path[1] == '/')
    snprintf(out, out_len, "%s", path + 2);
  else if (strcmp(path, "m") == 0)
    out[0] = '\0';
  else
    snprintf(out, out_len, "%s", path);
}

static void satochip_fp_hex(const uint8_t fp[4], char out[9]) {
  snprintf(out, 9, "%02x%02x%02x%02x", fp[0], fp[1], fp[2], fp[3]);
}

esp_err_t smartcard_satochip_get_btc_xpub(
    const char *pin, const char *path, const char *xtype, bool is_testnet,
    smartcard_satochip_btc_xpub_t *out, uint32_t timeout_ms) {
  if (!pin || !path || !out)
    return ESP_ERR_INVALID_ARG;

  memset(out, 0, sizeof(*out));
  out->err = ESP_ERR_INVALID_STATE;
  snprintf(out->path, sizeof(out->path), "%s", path);
  snprintf(out->xtype, sizeof(out->xtype), "%s", xtype ? xtype : "xpub");
  snprintf(out->detail, sizeof(out->detail), "Reading Satochip BTC public key.");

  uint32_t indices[SATOCHIP_MAX_PATH_COMPONENTS];
  size_t depth = 0;
  esp_err_t err =
      satochip_path_to_indices(path, indices,
                               sizeof(indices) / sizeof(indices[0]), &depth);
  if (err != ESP_OK || depth == 0) {
    out->err = err == ESP_OK ? ESP_ERR_INVALID_ARG : err;
    snprintf(out->detail, sizeof(out->detail), "Invalid BTC account path: %s.", path);
    return out->err;
  }

  satochip_session_t session;
  memset(&session, 0, sizeof(session));
  smartcard_satochip_account_t account;
  smartcard_satochip_account_t parent;
  smartcard_satochip_account_t master;
  memset(&account, 0, sizeof(account));
  memset(&parent, 0, sizeof(parent));
  memset(&master, 0, sizeof(master));

  err = satochip_session_open(&session, timeout_ms, out->detail,
                              sizeof(out->detail));
  if (err == ESP_OK)
    err = satochip_session_verify_pin(&session, pin, timeout_ms, out->detail,
                                      sizeof(out->detail));
  if (err == ESP_OK)
    err = satochip_session_load_authentikey(&session, timeout_ms, out->detail,
                                            sizeof(out->detail));
  if (err != ESP_OK)
    goto done;

  err = satochip_session_read_key(&session, path, &account, timeout_ms);
  if (err != ESP_OK) {
    snprintf(out->detail, sizeof(out->detail), "Account public key read failed: %s",
             account.detail);
    goto done;
  }

  uint8_t parent160[HASH160_LEN] = {0};
  char parent_path[96];
  if (!satochip_parent_path(path, parent_path, sizeof(parent_path))) {
    err = ESP_ERR_INVALID_ARG;
    snprintf(out->detail, sizeof(out->detail), "Parent path parsing failed.");
    goto done;
  }
  err = satochip_session_read_key(&session, parent_path, &parent, timeout_ms);
  if (err != ESP_OK || !parent.has_compressed_pubkey ||
      wally_hash160(parent.compressed_pubkey, EC_PUBLIC_KEY_LEN, parent160,
                   sizeof(parent160)) != WALLY_OK) {
    err = err == ESP_OK ? ESP_ERR_INVALID_RESPONSE : err;
    snprintf(out->detail, sizeof(out->detail), "Parent public key read failed: %s",
             parent.detail);
    goto done;
  }

  uint32_t version = satochip_xpub_version_for(out->xtype, is_testnet);
  err = satochip_xpub_from_key(&account, parent160, (uint32_t)depth,
                               indices[depth - 1], version, out->xpub,
                               sizeof(out->xpub));
  secure_zero(parent160, sizeof(parent160));
  if (err != ESP_OK) {
    snprintf(out->detail, sizeof(out->detail), "Extended public key encoding failed.");
    goto done;
  }
  out->has_xpub = true;

  uint8_t master_fp[4] = {0};
  bool has_master_fp = false;
  if (satochip_session_read_key(&session, "m", &master, timeout_ms) == ESP_OK &&
      master.has_compressed_pubkey) {
    has_master_fp = satochip_pubkey_fingerprint(master.compressed_pubkey,
                                                master_fp);
  }

  if (has_master_fp) {
    char fp_hex[9];
    char origin[96];
    satochip_fp_hex(master_fp, fp_hex);
    satochip_origin_without_m(path, origin, sizeof(origin));
    const char *wrapper = satochip_descriptor_wrapper_for_xtype(out->xtype);
    if (strcmp(wrapper, "wpkh") == 0) {
      snprintf(out->descriptor, sizeof(out->descriptor),
               "wpkh([%s/%s]%s/0/*)", fp_hex, origin, out->xpub);
      out->has_descriptor = true;
    } else if (strcmp(wrapper, "sh-wpkh") == 0) {
      snprintf(out->descriptor, sizeof(out->descriptor),
               "sh(wpkh([%s/%s]%s/0/*))", fp_hex, origin, out->xpub);
      out->has_descriptor = true;
    } else if (strcmp(wrapper, "pkh") == 0) {
      snprintf(out->descriptor, sizeof(out->descriptor),
               "pkh([%s/%s]%s/0/*)", fp_hex, origin, out->xpub);
      out->has_descriptor = true;
    }
  }

  snprintf(out->detail, sizeof(out->detail), "%s exported successfully: %s.", out->xtype,
           path);
  err = ESP_OK;

done:
  out->sw = session.sw;
  out->err = err;
  if (err != ESP_OK && out->detail[0] == '\0')
    snprintf(out->detail, sizeof(out->detail), "Satochip BTC public key read failed.");
  secure_zero(&account, sizeof(account));
  secure_zero(&parent, sizeof(parent));
  secure_zero(&master, sizeof(master));
  secure_zero(&session, sizeof(session));
  return err;
}

esp_err_t smartcard_satochip_get_btc_address(
    const char *pin, const char *path, smartcard_satochip_btc_script_t script,
    bool is_testnet, smartcard_satochip_btc_address_t *out,
    uint32_t timeout_ms) {
  if (!pin || !path || !out)
    return ESP_ERR_INVALID_ARG;

  memset(out, 0, sizeof(*out));
  out->err = ESP_ERR_INVALID_STATE;
  snprintf(out->path, sizeof(out->path), "%s", path);
  snprintf(out->detail, sizeof(out->detail), "Reading Satochip BTC address.");

  satochip_session_t session;
  memset(&session, 0, sizeof(session));
  smartcard_satochip_account_t account;
  memset(&account, 0, sizeof(account));

  esp_err_t err = satochip_session_open(&session, timeout_ms, out->detail,
                                        sizeof(out->detail));
  if (err == ESP_OK)
    err = satochip_session_verify_pin(&session, pin, timeout_ms, out->detail,
                                      sizeof(out->detail));
  if (err == ESP_OK)
    err = satochip_session_load_authentikey(&session, timeout_ms, out->detail,
                                            sizeof(out->detail));
  if (err == ESP_OK) {
    err = satochip_session_read_key(&session, path, &account, timeout_ms);
    if (err != ESP_OK) {
      snprintf(out->detail, sizeof(out->detail), "Address public key read failed: %s",
               account.detail);
    }
  }
  if (err == ESP_OK) {
    err = satochip_btc_address_from_pubkey(account.compressed_pubkey, script,
                                           is_testnet, out->address,
                                           sizeof(out->address));
    if (err != ESP_OK)
      snprintf(out->detail, sizeof(out->detail), "BTC address encoding failed.");
  }
  if (err == ESP_OK) {
    out->has_address = true;
    snprintf(out->detail, sizeof(out->detail), "BTC address read successfully: %s.",
             out->address);
  }

  out->sw = session.sw;
  out->err = err;
  secure_zero(&account, sizeof(account));
  secure_zero(&session, sizeof(session));
  return err;
}

esp_err_t smartcard_satochip_sign_evm_digest(
    const char *pin, const char *path, const uint8_t digest[32],
    smartcard_satochip_signature_t *out, uint32_t timeout_ms) {
  if (!pin || !path || !digest || !out)
    return ESP_ERR_INVALID_ARG;

  memset(out, 0, sizeof(*out));
  out->err = ESP_ERR_INVALID_STATE;
  snprintf(out->path, sizeof(out->path), "%s", path);
  snprintf(out->detail, sizeof(out->detail),
           "Signing EVM hash with Satochip.");

  satochip_session_t session;
  memset(&session, 0, sizeof(session));
  smartcard_satochip_account_t account;
  memset(&account, 0, sizeof(account));

  esp_err_t err =
      satochip_session_open(&session, timeout_ms, out->detail,
                            sizeof(out->detail));
  if (err == ESP_OK) {
    err = satochip_session_verify_pin(&session, pin, timeout_ms, out->detail,
                                      sizeof(out->detail));
  }
  if (err == ESP_OK) {
    err = satochip_session_load_authentikey(&session, timeout_ms, out->detail,
                                            sizeof(out->detail));
  }
  if (err == ESP_OK) {
    err = satochip_session_read_key(&session, path, &account, timeout_ms);
    if (err != ESP_OK) {
      snprintf(out->detail, sizeof(out->detail), "Signing path read failed: %s",
               account.detail);
    }
  }
  if (err != ESP_OK)
    goto done;

  uint8_t sign_apdu[5 + 32];
  sign_apdu[0] = SATOCHIP_CLA;
  sign_apdu[1] = SATOCHIP_INS_SIGN_TRANSACTION_HASH;
  sign_apdu[2] = 0xFF;
  sign_apdu[3] = 0x00;
  sign_apdu[4] = 32;
  memcpy(sign_apdu + 5, digest, 32);

  uint8_t response[SMARTCARD_CCID_RESPONSE_MAX_LEN];
  size_t payload_len = 0;
  uint16_t sw = 0;
  err = satochip_transmit_card(&session.sc, sign_apdu, sizeof(sign_apdu),
                               response, sizeof(response), &payload_len, &sw,
                               timeout_ms);
  secure_zero(sign_apdu, sizeof(sign_apdu));
  session.sw = sw;
  out->sw = sw;
  satochip_log_response("SIGN_HASH", err, sw, payload_len);
  if (err != ESP_OK) {
    snprintf(out->detail, sizeof(out->detail), "SIGN_HASH send failed: %s.",
             esp_err_to_name(err));
    goto done;
  }
  if (sw != 0x9000) {
    err = ESP_ERR_INVALID_RESPONSE;
    snprintf(out->detail, sizeof(out->detail), "SIGN_HASH returned SW=%04X.", sw);
    goto done;
  }

  err = satochip_parse_der_signature_compact(response, payload_len,
                                             out->compact);
  if (err != ESP_OK) {
    snprintf(out->detail, sizeof(out->detail), "Signature DER parsing failed.");
    goto done;
  }

  err = satochip_recover_signature_recid(
      digest, out->compact, account.compressed_pubkey, &out->recovery_id);
  if (err != ESP_OK) {
    snprintf(out->detail, sizeof(out->detail),
             "Signature recovery failed; could not confirm it comes from the current path.");
    goto done;
  }

  memcpy(out->signature, out->compact, 64);
  out->signature[64] = out->recovery_id;
  memcpy(out->compressed_pubkey, account.compressed_pubkey,
         sizeof(out->compressed_pubkey));
  snprintf(out->address, sizeof(out->address), "%s", account.address);
  out->has_signature = true;
  out->has_address = account.has_address;
  out->has_compressed_pubkey = account.has_compressed_pubkey;
  snprintf(out->detail, sizeof(out->detail), "Satochip signing succeeded: %s.",
           out->address);
  err = ESP_OK;

done:
  out->sw = session.sw;
  out->err = err;
  secure_zero(&account, sizeof(account));
  secure_zero(&session, sizeof(session));
  return err;
}

void smartcard_satochip_format_status(const smartcard_satochip_status_t *status,
                                      char *out, size_t out_len) {
  if (!out || out_len == 0)
    return;
  out[0] = '\0';

  if (!status) {
    snprintf(out, out_len, "%s", sc_fmt_no_status());
    return;
  }

  snprintf(out, out_len,
           "%s: %s\n"
           "SELECT SW: %04X\n"
           "Status SW: %04X\n"
           "%s: %u.%u-%u.%u\n"
           "PIN remaining: PIN0=%u PUK0=%u PIN1=%u PUK1=%u\n"
           "%s: %s\n"
           "%s: %s\n"
           "2FA: %s\n"
           "%s: %s\n"
           "NFC %s: %u\n"
           "Schnorr: %u Nostr: %u Liquid: %u\n"
           "%s",
           i18n_tr_or("smartcard.smartcard", "App"),
           status->app_selected ? "Satochip" : sc_fmt_not_selected(),
           status->select_sw, status->status_sw,
           i18n_tr_or("tools.version", "Version"),
           (unsigned)status->protocol_major,
           (unsigned)status->protocol_minor,
           (unsigned)status->applet_major,
           (unsigned)status->applet_minor,
           (unsigned)status->pin0_remaining,
           (unsigned)status->puk0_remaining,
           (unsigned)status->pin1_remaining,
           (unsigned)status->puk1_remaining,
           i18n_tr_or("sign.card_initialized", "Initialized"),
           yes_no_text(status->setup_done),
           i18n_tr_or("wallet.loaded_mnemonic", "Seeded"),
           yes_no_text(status->is_seeded), yes_no_text(status->needs_2fa),
           i18n_tr_or("settings.security", "Secure channel"),
           yes_no_text(status->needs_secure_channel),
           i18n_tr_or("settings.policy", "policy"),
           (unsigned)status->nfc_policy,
           (unsigned)status->feature_schnorr_policy,
           (unsigned)status->feature_nostr_policy,
           (unsigned)status->feature_liquid_policy,
           sc_fmt_detail(status->detail));
}

void smartcard_satochip_format_label(const smartcard_satochip_label_t *label,
                                     char *out, size_t out_len) {
  if (!out || out_len == 0)
    return;
  out[0] = '\0';

  if (!label) {
    snprintf(out, out_len, "%s", sc_fmt_no_label());
    return;
  }

  snprintf(out, out_len,
           "%s: %s\n"
           "SELECT SW: %04X\n"
           "Label SW: %04X\n"
           "%s: %s\n"
           "%s",
           i18n_tr_or("smartcard.smartcard", "App"),
           label->app_selected ? (label->applet[0] ? label->applet
                                                   : sc_fmt_detected())
                               : sc_fmt_not_selected(),
           label->select_sw, label->label_sw,
           i18n_tr_or("sign.secret_label", "Label"),
           label->has_label ? label->label : sc_fmt_empty(),
           sc_fmt_detail(label->detail));
}

static void seedkeeper_last_log_hex(const smartcard_seedkeeper_status_t *status,
                                    char *out, size_t out_len) {
  if (!out || out_len == 0) {
    return;
  }
  out[0] = '\0';
  if (!status || status->last_log_len == 0) {
    snprintf(out, out_len, "%s", sc_fmt_empty());
    return;
  }

  size_t pos = 0;
  for (size_t i = 0; i < status->last_log_len && pos + 3 < out_len; i++) {
    int written = snprintf(out + pos, out_len - pos, "%02X%s",
                           status->last_log[i],
                           (i + 1 < status->last_log_len) ? " " : "");
    if (written <= 0)
      break;
    pos += (size_t)written;
    if (pos >= out_len)
      break;
  }
}

void smartcard_seedkeeper_format_status(
    const smartcard_seedkeeper_status_t *status, char *out, size_t out_len) {
  if (!out || out_len == 0)
    return;
  out[0] = '\0';

  if (!status) {
    snprintf(out, out_len, "%s", sc_fmt_no_status());
    return;
  }

  char last_log_hex[32];
  seedkeeper_last_log_hex(status, last_log_hex, sizeof(last_log_hex));

  snprintf(out, out_len,
           "%s: %s\n"
           "SELECT SW: %04X\n"
           "Status SW: %04X\n"
           "%s: %u\n"
           "%s: %u\n"
           "%s: %u\n"
           "%s: %u\n"
           "%s: %u\n"
           "%s: %s\n"
           "%s",
           i18n_tr_or("smartcard.smartcard", "App"),
           status->app_selected ? "SeedKeeper" : sc_fmt_not_selected(),
           status->select_sw, status->status_sw,
           i18n_tr_or("dialog.sensitive_data", "Secrets"),
           (unsigned)status->nb_secrets,
           i18n_tr_or("tools.memory", "Total memory"),
           (unsigned)status->total_memory,
           i18n_tr_or("tools.free_space", "Free memory"),
           (unsigned)status->free_memory,
           i18n_tr_or("tools.report", "Total logs"),
           (unsigned)status->nb_logs_total,
           i18n_tr_or("feature.status.available", "Available logs"),
           (unsigned)status->nb_logs_avail,
           i18n_tr_or("tools.report", "Last log"),
           last_log_hex, sc_fmt_detail(status->detail));
}

static bool satochip_parse_seedkeeper_header_block(
    const uint8_t *payload, size_t payload_len,
    smartcard_seedkeeper_header_t *header) {
  if (!payload || !header || payload_len < 15)
    return false;

  memset(header, 0, sizeof(*header));
  header->id = read_be16(payload);
  header->type = payload[2];
  header->subtype = payload[12];
  header->origin = payload[3];
  header->export_rights = payload[4];
  header->export_nbplain = payload[5];
  header->export_nbsecure = payload[6];
  header->export_counter = payload[7];
  memcpy(header->fingerprint, payload + 8, sizeof(header->fingerprint));
  header->rfu1 = payload[12];
  header->rfu2 = payload[13];
  size_t label_size = payload[14];
  if (payload_len < 15U + label_size)
    return false;
  if (label_size >= sizeof(header->label))
    label_size = sizeof(header->label) - 1;
  if (label_size > 0)
    memcpy(header->label, payload + 15, label_size);
  header->label[label_size] = '\0';
  satochip_bytes_to_hex(payload, 15U + label_size, header->header_hex,
                        sizeof(header->header_hex));
  return true;
}

static void satochip_seedkeeper_header_text(
    const smartcard_seedkeeper_header_t *header, char *out, size_t out_len) {
  if (!out || out_len == 0) {
    return;
  }
  if (!header) {
    snprintf(out, out_len, "No entries.");
    return;
  }

  snprintf(out, out_len,
           "ID=%u\n"
           "Title=%s\n"
           "Type=%u subtype=%u\n"
           "Origin=%u rights=%u\n"
           "Plain exports=%u secure exports=%u counter=%u\n"
           "Fingerprint=%02X%02X%02X%02X",
           (unsigned)header->id, header->label, (unsigned)header->type,
           (unsigned)header->subtype, (unsigned)header->origin,
           (unsigned)header->export_rights, (unsigned)header->export_nbplain,
           (unsigned)header->export_nbsecure, (unsigned)header->export_counter,
           header->fingerprint[0], header->fingerprint[1],
           header->fingerprint[2], header->fingerprint[3]);
}

static void satochip_append_detail(char *detail, size_t detail_len,
                                   const char *fmt, ...) {
  if (!detail || detail_len == 0 || !fmt)
    return;
  size_t used = strnlen(detail, detail_len);
  if (used >= detail_len)
    return;
  va_list args;
  va_start(args, fmt);
  vsnprintf(detail + used, detail_len - used, fmt, args);
  va_end(args);
}

static int satochip_seedkeeper_header_id_cmp(const void *lhs,
                                             const void *rhs) {
  const smartcard_seedkeeper_header_t *a =
      (const smartcard_seedkeeper_header_t *)lhs;
  const smartcard_seedkeeper_header_t *b =
      (const smartcard_seedkeeper_header_t *)rhs;
  if (a->id < b->id)
    return -1;
  if (a->id > b->id)
    return 1;
  return 0;
}

static bool satochip_fill_apdu_ok(smartcard_satochip_apdu_result_t *out,
                                  uint16_t sw, const char *ok_text) {
  if (!out)
    return false;
  out->sw = sw;
  out->err = ESP_OK;
  if (ok_text && ok_text[0])
    snprintf(out->detail, sizeof(out->detail), "%s", ok_text);
  return true;
}

esp_err_t smartcard_satochip_card_set_label(
    const char *pin, const char *label, smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms) {
  uint8_t label_bytes[96];
  size_t label_len = 0;
  if (label && label[0]) {
    label_len = strlen(label);
    if (label_len > sizeof(label_bytes) - 1)
      label_len = sizeof(label_bytes) - 1;
    memcpy(label_bytes, label, label_len);
  }

  uint8_t apdu[5 + 1 + sizeof(label_bytes)];
  apdu[0] = SATOCHIP_CLA;
  apdu[1] = SATOCHIP_INS_GET_SET_LABEL;
  apdu[2] = 0x00;
  apdu[3] = 0x00;
  apdu[4] = (uint8_t)(1 + label_len);
  apdu[5] = (uint8_t)label_len;
  if (label_len > 0)
    memcpy(apdu + 6, label_bytes, label_len);

  esp_err_t err = satochip_execute_apdu(pin, pin && pin[0] != '\0', apdu,
                                        6U + label_len, out, timeout_ms,
                                        "SET_LABEL");
  if (err != ESP_OK)
    return err;
  if (out->sw == 0x9000) {
    snprintf(out->detail, sizeof(out->detail), "Card label updated.");
  } else if (out->sw == 0x6D00) {
    snprintf(out->detail, sizeof(out->detail), "Card does not support labels.");
  } else {
    satochip_fill_simple_detail(out->detail, sizeof(out->detail), "Write label",
                                out->sw);
  }
  return ESP_OK;
}

esp_err_t smartcard_satochip_card_set_nfc_policy(
    const char *pin, uint8_t policy, smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms) {
  if (policy > 2) {
    if (out) {
      satochip_apdu_result_reset(out);
      out->err = ESP_ERR_INVALID_ARG;
      snprintf(out->detail, sizeof(out->detail),
               "NFC policy must be 0, 1, or 2.");
    }
    return ESP_ERR_INVALID_ARG;
  }
  uint8_t apdu[] = {SATOCHIP_CLA, SATOCHIP_INS_SET_NFC_POLICY, policy, 0x00,
                    0x00};
  esp_err_t err = satochip_execute_apdu(pin, pin && pin[0] != '\0', apdu,
                                        sizeof(apdu), out, timeout_ms,
                                        "SET_NFC_POLICY");
  if (err != ESP_OK)
    return err;
  if (out->sw == 0x9000) {
    snprintf(out->detail, sizeof(out->detail), "NFC policy updated: %u.",
             (unsigned)policy);
  } else if (out->sw == 0x9C48) {
    snprintf(out->detail, sizeof(out->detail), "A contact interface is required to change the NFC policy.");
  } else if (out->sw == 0x9C49) {
    snprintf(out->detail, sizeof(out->detail), "NFC is blocked; only factory reset can recover it.");
  } else {
    satochip_fill_simple_detail(out->detail, sizeof(out->detail),
                                "Set NFC policy", out->sw);
  }
  return ESP_OK;
}

esp_err_t smartcard_satochip_card_set_feature_policy(
    const char *pin, uint8_t feature_id, uint8_t policy,
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms) {
  if (feature_id > 2 || policy > 2) {
    if (out) {
      satochip_apdu_result_reset(out);
      out->err = ESP_ERR_INVALID_ARG;
      snprintf(out->detail, sizeof(out->detail),
               "Feature and policy must be 0, 1, or 2.");
    }
    return ESP_ERR_INVALID_ARG;
  }
  uint8_t apdu[] = {SATOCHIP_CLA, SATOCHIP_INS_SET_FEATURE_POLICY, feature_id,
                    policy, 0x00};
  esp_err_t err = satochip_execute_apdu(pin, pin && pin[0] != '\0', apdu,
                                        sizeof(apdu), out, timeout_ms,
                                        "SET_FEATURE_POLICY");
  if (err != ESP_OK)
    return err;
  if (out->sw == 0x9000) {
    snprintf(out->detail, sizeof(out->detail), "Feature policy updated: %u/%u.",
             (unsigned)feature_id, (unsigned)policy);
  } else if (out->sw == 0x9C4B) {
    snprintf(out->detail, sizeof(out->detail), "This feature is blocked; only factory reset can recover it.");
  } else {
    satochip_fill_simple_detail(out->detail, sizeof(out->detail),
                                "Set feature policy", out->sw);
  }
  return ESP_OK;
}

static bool smartcard_reset_factory_needs_more_unplug(uint16_t sw) {
  if ((sw & 0xFF00U) == 0xFF00U && sw != 0xFF00U && sw != 0xFFFFU)
    return true;
  return sw == 0x9C20 || sw == 0x9C21;
}

static esp_err_t smartcard_reset_factory_select_plain(
    const uint8_t *primary_apdu, size_t primary_apdu_len,
    const char *primary_name, const uint8_t *fallback_apdu,
    size_t fallback_apdu_len, const char *fallback_name, char *selected_name,
    size_t selected_name_len, uint16_t *select_sw, char *detail,
    size_t detail_len, uint32_t timeout_ms) {
  if (selected_name && selected_name_len > 0)
    selected_name[0] = '\0';
  if (select_sw)
    *select_sw = 0;

  uint16_t primary_sw = 0;
  bool selected = false;
  esp_err_t err =
      smartcard_select_applet(primary_apdu, primary_apdu_len, primary_name,
                              &primary_sw, &selected, timeout_ms);
  if (select_sw)
    *select_sw = primary_sw;
  if (err != ESP_OK) {
    if (detail && detail_len > 0) {
      snprintf(detail, detail_len, "Selecting %s failed: %s.",
               primary_name ? primary_name : "Smartcard", esp_err_to_name(err));
    }
    return err;
  }
  if (selected) {
    if (selected_name && selected_name_len > 0) {
      snprintf(selected_name, selected_name_len, "%s",
               primary_name ? primary_name : "Smartcard");
    }
    return ESP_OK;
  }

  uint16_t fallback_sw = 0;
  if (fallback_apdu && fallback_apdu_len > 0) {
    err = smartcard_select_applet(fallback_apdu, fallback_apdu_len,
                                  fallback_name, &fallback_sw, &selected,
                                  timeout_ms);
    if (select_sw)
      *select_sw = fallback_sw;
    if (err != ESP_OK) {
      if (detail && detail_len > 0) {
        snprintf(detail, detail_len, "Selecting %s failed: %s.",
                 fallback_name ? fallback_name : "fallback app",
                 esp_err_to_name(err));
      }
      return err;
    }
    if (selected) {
      if (selected_name && selected_name_len > 0) {
        snprintf(selected_name, selected_name_len, "%s",
                 fallback_name ? fallback_name : "Smartcard");
      }
      return ESP_OK;
    }
  }

  if (detail && detail_len > 0) {
    snprintf(detail, detail_len, "No smartcard app selected, SW=%04X/%04X.",
             primary_sw, fallback_sw);
  }
  return ESP_ERR_NOT_FOUND;
}

static esp_err_t smartcard_reset_factory_signal_plain(
    const uint8_t *primary_apdu, size_t primary_apdu_len,
    const char *primary_name, const uint8_t *fallback_apdu,
    size_t fallback_apdu_len, const char *fallback_name,
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms) {
  if (!out)
    return ESP_ERR_INVALID_ARG;
  satochip_apdu_result_reset(out);
  smartcard_ccid_set_factory_reset_mode(true);

  char detail[256] = {0};
  esp_err_t err = ensure_reader_ready(timeout_ms, detail, sizeof(detail));
  if (err != ESP_OK) {
    satochip_apdu_result_fill(out, NULL, 0, 0, err, detail);
    smartcard_ccid_set_factory_reset_mode(false);
    return err;
  }

  uint16_t select_sw = 0;
  char selected_name[24] = {0};
  err = smartcard_reset_factory_select_plain(
      primary_apdu, primary_apdu_len, primary_name, fallback_apdu,
      fallback_apdu_len, fallback_name, selected_name, sizeof(selected_name),
      &select_sw, detail, sizeof(detail), timeout_ms);
  if (err != ESP_OK) {
    satochip_apdu_result_fill(out, NULL, 0, select_sw, err, detail);
    smartcard_ccid_set_factory_reset_mode(false);
    return err;
  }

  uint8_t apdu[] = {SATOCHIP_CLA, SATOCHIP_INS_RESET_FACTORY_SIGNAL, 0x00,
                    0x00, 0x00};
  uint8_t response[SMARTCARD_CCID_RESPONSE_MAX_LEN];
  size_t payload_len = 0;
  uint16_t sw = 0;
  err = satochip_transmit_checked(apdu, sizeof(apdu), response,
                                  sizeof(response), &payload_len, &sw,
                                  timeout_ms);
  if (err != ESP_OK) {
    snprintf(detail, sizeof(detail), "Factory reset send failed: %s.",
             esp_err_to_name(err));
    satochip_apdu_result_fill(out, NULL, 0, sw, err, detail);
    smartcard_ccid_set_factory_reset_mode(false);
    return err;
  }

  satochip_apdu_result_fill(out, response, payload_len, sw, ESP_OK, NULL);
  if (!smartcard_reset_factory_needs_more_unplug(sw))
    smartcard_ccid_set_factory_reset_mode(false);
  if (sw == 0xFF00) {
    snprintf(out->detail, sizeof(out->detail),
             "%s has been restored to a blank card. Reinsert it, then set the PIN again.",
             selected_name[0] ? selected_name : "Smartcard");
  } else if (sw == 0xFFFF) {
    snprintf(out->detail, sizeof(out->detail),
             "Aborted: the card was not reinserted after the last step. Start again from the first reinsertion.");
  } else if ((sw & 0xFF00U) == 0xFF00U) {
    snprintf(out->detail, sizeof(out->detail),
             "%u attempts remaining. Remove the card, insert it again, then run the step again.",
             (unsigned)(sw & 0x00FFU));
  } else if (sw == 0x9C04) {
    snprintf(out->detail, sizeof(out->detail),
             "The card is not initialized; factory reset is not needed.");
  } else if (sw == 0x9C20 || sw == 0x9C21) {
    snprintf(out->detail, sizeof(out->detail),
             "Reset sequence is incomplete. Remove and reinsert the card, then retry before any other card operation.");
  } else if (sw == 0x6D00) {
    snprintf(out->detail, sizeof(out->detail),
             "The card does not support this reset command, SW=6D00.");
  } else if (sw == 0x6F00) {
    snprintf(out->detail, sizeof(out->detail),
             "Card internal error, SW=6F00. Reinsert the card and retry.");
  } else {
    satochip_fill_simple_detail(out->detail, sizeof(out->detail), "Factory reset",
                                sw);
  }
  return ESP_OK;
}

esp_err_t smartcard_satochip_card_reset_factory_signal(
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms) {
  return smartcard_reset_factory_signal_plain(
      k_select_satochip_apdu, sizeof(k_select_satochip_apdu), "Satochip",
      k_select_seedkeeper_apdu, sizeof(k_select_seedkeeper_apdu), "SeedKeeper",
      out, timeout_ms);
}

esp_err_t smartcard_satochip_card_setup_pin(
    const char *new_pin, smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms) {
  if (!out)
    return ESP_ERR_INVALID_ARG;
  satochip_apdu_result_reset(out);

  size_t new_pin_len = new_pin ? strlen(new_pin) : 0;
  if (new_pin_len < 4 || new_pin_len > 16) {
    out->err = ESP_ERR_INVALID_ARG;
    snprintf(out->detail, sizeof(out->detail), "PIN must be 4-16 characters.");
    return ESP_ERR_INVALID_ARG;
  }

  smartcard_satochip_status_t status;
  esp_err_t err = smartcard_satochip_read_status(&status, timeout_ms);
  if (err != ESP_OK) {
    out->err = err;
    snprintf(out->detail, sizeof(out->detail), "Reading Satochip status failed: %s.",
             esp_err_to_name(err));
    return err;
  }

  static const uint8_t default_pin[] = {'M', 'u', 's', 'c',
                                        'l', 'e', '0', '0'};
  uint8_t unblock_pin0[16];
  uint8_t pin1[16];
  uint8_t unblock_pin1[16];
  crypto_random_bytes(unblock_pin0, sizeof(unblock_pin0));
  crypto_random_bytes(pin1, sizeof(pin1));
  crypto_random_bytes(unblock_pin1, sizeof(unblock_pin1));

  uint8_t apdu[5 + 96];
  size_t pos = 0;
  apdu[pos++] = SATOCHIP_CLA;
  apdu[pos++] = SATOCHIP_INS_SETUP;
  apdu[pos++] = 0x00;
  apdu[pos++] = 0x00;
  size_t lc_pos = pos++;
  size_t data_start = pos;

  apdu[pos++] = (uint8_t)sizeof(default_pin);
  memcpy(apdu + pos, default_pin, sizeof(default_pin));
  pos += sizeof(default_pin);

  apdu[pos++] = 0x05;
  apdu[pos++] = 0x01;
  apdu[pos++] = (uint8_t)new_pin_len;
  memcpy(apdu + pos, new_pin, new_pin_len);
  pos += new_pin_len;
  apdu[pos++] = (uint8_t)sizeof(unblock_pin0);
  memcpy(apdu + pos, unblock_pin0, sizeof(unblock_pin0));
  pos += sizeof(unblock_pin0);

  apdu[pos++] = 0x01;
  apdu[pos++] = 0x01;
  apdu[pos++] = (uint8_t)sizeof(pin1);
  memcpy(apdu + pos, pin1, sizeof(pin1));
  pos += sizeof(pin1);
  apdu[pos++] = (uint8_t)sizeof(unblock_pin1);
  memcpy(apdu + pos, unblock_pin1, sizeof(unblock_pin1));
  pos += sizeof(unblock_pin1);

  apdu[pos++] = 0x00;
  apdu[pos++] = 0x20;
  apdu[pos++] = 0x00;
  apdu[pos++] = 0x00;
  apdu[pos++] = 0x01;
  apdu[pos++] = 0x01;
  apdu[pos++] = 0x01;
  apdu[lc_pos] = (uint8_t)(pos - data_start);

  uint8_t response[SMARTCARD_CCID_RESPONSE_MAX_LEN];
  size_t payload_len = 0;
  uint16_t sw = 0;
  satochip_secure_channel_t sc;
  memset(&sc, 0, sizeof(sc));
  bool sc_active = false;

  if (status.needs_secure_channel) {
    uint8_t sc_response[SMARTCARD_CCID_RESPONSE_MAX_LEN];
    size_t sc_payload_len = 0;
    uint16_t sc_sw = 0;
    esp_err_t sc_err = satochip_secure_channel_begin(
        &sc, NULL, NULL, sc_response, sizeof(sc_response), &sc_payload_len,
        &sc_sw, timeout_ms);
    secure_zero(sc_response, sizeof(sc_response));
    if (sc_err == ESP_OK) {
      sc_active = true;
    }
  }

  err = satochip_transmit_card(&sc, apdu, pos, response, sizeof(response),
                               &payload_len, &sw, timeout_ms);
  if (err == ESP_OK && !sc_active && satochip_sw_needs_secure_channel(sw)) {
    uint8_t sc_response[SMARTCARD_CCID_RESPONSE_MAX_LEN];
    size_t sc_payload_len = 0;
    uint16_t sc_sw = 0;
    err = satochip_secure_channel_begin(&sc, NULL, NULL, sc_response,
                                        sizeof(sc_response), &sc_payload_len,
                                        &sc_sw, timeout_ms);
    secure_zero(sc_response, sizeof(sc_response));
    if (err == ESP_OK) {
      sc_active = true;
      err = satochip_transmit_card(&sc, apdu, pos, response, sizeof(response),
                                   &payload_len, &sw, timeout_ms);
    }
  }
  secure_zero(unblock_pin0, sizeof(unblock_pin0));
  secure_zero(pin1, sizeof(pin1));
  secure_zero(unblock_pin1, sizeof(unblock_pin1));
  if (err != ESP_OK) {
    satochip_apdu_result_fill(out, NULL, 0, sw, err, "Satochip setup PIN send failed.");
    secure_zero(apdu, sizeof(apdu));
    secure_zero(&sc, sizeof(sc));
    return err;
  }

  satochip_apdu_result_fill(out, response, payload_len, sw, ESP_OK, NULL);
  if (sw == 0x9000) {
    snprintf(out->detail, sizeof(out->detail),
             "Setup complete. Continue by writing the mnemonic.");
  } else if (satochip_sw_needs_secure_channel(sw)) {
    snprintf(out->detail, sizeof(out->detail),
             "Setup PIN requires a secure channel. Reinsert the card and retry. SW=%04X.", sw);
  } else if (sw == 0x9C03) {
    snprintf(out->detail, sizeof(out->detail),
             "Card is already initialized. Use Change PIN to change it.");
  } else if (sw == 0x9C0F || sw == 0x6700) {
    snprintf(out->detail, sizeof(out->detail), "PIN or parameter is invalid.");
  } else {
    satochip_fill_simple_detail(out->detail, sizeof(out->detail), "Set PIN",
                                sw);
  }
  out->err = ESP_OK;
  secure_zero(apdu, sizeof(apdu));
  secure_zero(&sc, sizeof(sc));
  return ESP_OK;
}

esp_err_t smartcard_satochip_card_reset_seed(
    const char *pin, const uint8_t *hmac, size_t hmac_len,
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms) {
  if (!pin || pin[0] == '\0')
    return ESP_ERR_INVALID_ARG;
  if (hmac_len > 20)
    return ESP_ERR_INVALID_ARG;

  size_t pin_len = strlen(pin);
  uint8_t apdu[5 + 64 + 20];
  apdu[0] = SATOCHIP_CLA;
  apdu[1] = SATOCHIP_INS_RESET_SEED;
  apdu[2] = (uint8_t)pin_len;
  apdu[3] = 0x00;
  apdu[4] = (uint8_t)(pin_len + hmac_len);
  memcpy(apdu + 5, pin, pin_len);
  if (hmac && hmac_len > 0)
    memcpy(apdu + 5 + pin_len, hmac, hmac_len);

  esp_err_t err = satochip_execute_apdu(NULL, false, apdu,
                                        5U + pin_len + hmac_len, out,
                                        timeout_ms, "RESET_SEED");
  if (err != ESP_OK)
    return err;
  if (out->sw == 0x9000)
    snprintf(out->detail, sizeof(out->detail),
             "Seed reset. Set the PIN again and write the mnemonic.");
  else
    satochip_fill_simple_detail(out->detail, sizeof(out->detail), "Reset seed",
                                out->sw);
  return ESP_OK;
}

esp_err_t smartcard_satochip_card_import_mnemonic_seed(
    const char *pin, const char *mnemonic, const char *passphrase,
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms) {
  if (!out)
    return ESP_ERR_INVALID_ARG;
  satochip_apdu_result_reset(out);

  if (!pin || pin[0] == '\0' || !mnemonic || mnemonic[0] == '\0') {
    out->err = ESP_ERR_INVALID_ARG;
    snprintf(out->detail, sizeof(out->detail), "Enter the PIN and mnemonic.");
    return ESP_ERR_INVALID_ARG;
  }

  unsigned char seed[BIP39_SEED_LEN_512];
  if (bip39_mnemonic_to_seed512(mnemonic, passphrase ? passphrase : "", seed,
                                sizeof(seed)) != WALLY_OK) {
    secure_zero(seed, sizeof(seed));
    out->err = ESP_ERR_INVALID_ARG;
    snprintf(out->detail, sizeof(out->detail), "Mnemonic-to-seed conversion failed.");
    return ESP_ERR_INVALID_ARG;
  }

  satochip_session_t session;
  char detail[256] = {0};
  esp_err_t err = satochip_seed_import_session_open(&session, timeout_ms,
                                                    detail, sizeof(detail));
  if (err != ESP_OK) {
    secure_zero(seed, sizeof(seed));
    satochip_apdu_result_fill(out, NULL, 0, 0, err, detail);
    secure_zero(&session, sizeof(session));
    return err;
  }

  if (session.status.is_seeded) {
    secure_zero(seed, sizeof(seed));
    snprintf(out->detail, sizeof(out->detail),
             "Satochip already has a seed. Reset the seed before writing.");
    out->sw = session.sw;
    out->err = ESP_OK;
    secure_zero(&session, sizeof(session));
    return ESP_OK;
  }

  err = satochip_session_verify_pin(&session, pin, timeout_ms, detail,
                                    sizeof(detail));
  if (err != ESP_OK) {
    secure_zero(seed, sizeof(seed));
    satochip_apdu_result_fill(out, NULL, 0, session.sw, err,
                              detail[0] ? detail : "PIN verification failed.");
    secure_zero(&session, sizeof(session));
    return err;
  }

  uint8_t apdu[5 + BIP39_SEED_LEN_512];
  apdu[0] = SATOCHIP_CLA;
  apdu[1] = SATOCHIP_INS_BIP32_IMPORT_SEED;
  apdu[2] = BIP39_SEED_LEN_512;
  apdu[3] = 0x00;
  apdu[4] = BIP39_SEED_LEN_512;
  memcpy(apdu + 5, seed, BIP39_SEED_LEN_512);

  uint8_t response[SMARTCARD_CCID_RESPONSE_MAX_LEN];
  size_t payload_len = 0;
  uint16_t sw = 0;
  err = satochip_transmit_card(&session.sc, apdu, sizeof(apdu), response,
                               sizeof(response), &payload_len, &sw,
                               timeout_ms);
  secure_zero(apdu, sizeof(apdu));
  secure_zero(seed, sizeof(seed));
  session.sw = sw;
  if (err != ESP_OK) {
    satochip_apdu_result_fill(out, NULL, 0, sw, err, "Write seed send failed.");
    secure_zero(&session, sizeof(session));
    return err;
  }

  satochip_apdu_result_fill(out, response, payload_len, sw, ESP_OK, NULL);
  if (sw == 0x9000) {
    esp_err_t auth_err = satochip_parse_authentikey_response(
        response, payload_len, session.authentikey, session.authentikey_coordx);
    if (auth_err == ESP_OK) {
      session.has_authentikey = true;
      auth_err = satochip_session_set_authentikey_pubkey(
          &session, response, payload_len, timeout_ms);
    }
    if (auth_err == ESP_OK) {
      snprintf(out->detail, sizeof(out->detail),
               "Satochip mnemonic written.");
    } else {
      snprintf(out->detail, sizeof(out->detail),
               "Seed written; authentikey cache failed. Reinsert the card before reading addresses.");
    }
  } else if (sw == 0x9C17) {
    snprintf(out->detail, sizeof(out->detail),
             "Satochip already has a seed. Reset the seed before writing.");
  } else if (sw == 0x9C0F || sw == 0x6700) {
    snprintf(out->detail, sizeof(out->detail), "Write parameters are invalid.");
  } else {
    satochip_fill_simple_detail(out->detail, sizeof(out->detail),
                                "Write Satochip", sw);
  }

  out->err = ESP_OK;
  secure_zero(&session, sizeof(session));
  return ESP_OK;
}

esp_err_t smartcard_satochip_card_change_pin(
    uint8_t pin_nbr, const char *old_pin, const char *new_pin,
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms) {
  if (!old_pin || !new_pin)
    return ESP_ERR_INVALID_ARG;
  size_t old_len = strlen(old_pin);
  size_t new_len = strlen(new_pin);
  if (old_len == 0 || new_len == 0 || old_len > 64 || new_len > 64)
    return ESP_ERR_INVALID_ARG;

  uint8_t apdu[5 + 1 + 64 + 1 + 64];
  size_t pos = 0;
  apdu[pos++] = SATOCHIP_CLA;
  apdu[pos++] = SATOCHIP_INS_CHANGE_PIN;
  apdu[pos++] = pin_nbr;
  apdu[pos++] = 0x00;
  apdu[pos++] = (uint8_t)(1 + old_len + 1 + new_len);
  apdu[pos++] = (uint8_t)old_len;
  memcpy(apdu + pos, old_pin, old_len);
  pos += old_len;
  apdu[pos++] = (uint8_t)new_len;
  memcpy(apdu + pos, new_pin, new_len);
  pos += new_len;

  esp_err_t err = satochip_execute_apdu(NULL, false, apdu, pos, out,
                                        timeout_ms, "CHANGE_PIN");
  if (err != ESP_OK)
    return err;
  if (out->sw == 0x9000) {
    snprintf(out->detail, sizeof(out->detail), "PIN changed.");
  } else if ((out->sw & 0xFFC0U) == 0x63C0U) {
    snprintf(out->detail, sizeof(out->detail), "Wrong PIN, %u attempts remaining.",
             (unsigned)(out->sw & 0x000FU));
  } else {
    satochip_fill_simple_detail(out->detail, sizeof(out->detail), "Change PIN",
                                out->sw);
  }
  return ESP_OK;
}

esp_err_t smartcard_satochip_card_unblock_pin(
    uint8_t pin_nbr, const uint8_t *puk, size_t puk_len,
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms) {
  if (!puk || puk_len == 0 || puk_len > 64)
    return ESP_ERR_INVALID_ARG;

  uint8_t apdu[5 + 64];
  apdu[0] = SATOCHIP_CLA;
  apdu[1] = SATOCHIP_INS_UNBLOCK_PIN;
  apdu[2] = pin_nbr;
  apdu[3] = 0x00;
  apdu[4] = (uint8_t)puk_len;
  memcpy(apdu + 5, puk, puk_len);

  esp_err_t err = satochip_execute_apdu(NULL, false, apdu, 5U + puk_len, out,
                                        timeout_ms, "UNBLOCK_PIN");
  if (err != ESP_OK)
    return err;
  if (out->sw == 0x9000) {
    snprintf(out->detail, sizeof(out->detail), "PIN unlocked.");
  } else if ((out->sw & 0xFFC0U) == 0x63C0U) {
    snprintf(out->detail, sizeof(out->detail), "Wrong PUK, %u attempts remaining.",
             (unsigned)(out->sw & 0x000FU));
  } else if (out->sw == 0xFF00) {
    snprintf(out->detail, sizeof(out->detail),
             "Card has been factory reset. Set the PIN again.");
  } else {
    satochip_fill_simple_detail(out->detail, sizeof(out->detail),
                                "Unlock PIN", out->sw);
  }
  return ESP_OK;
}

esp_err_t smartcard_satochip_card_set_2fa_key(
    const uint8_t *hmacsha160_key, size_t key_len, uint64_t amount_limit,
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms) {
  if (!hmacsha160_key || key_len != 20)
    return ESP_ERR_INVALID_ARG;

  uint8_t apdu[5 + 20 + 8];
  apdu[0] = SATOCHIP_CLA;
  apdu[1] = SATOCHIP_INS_SET_2FA_KEY;
  apdu[2] = 0x00;
  apdu[3] = 0x00;
  apdu[4] = 28;
  memcpy(apdu + 5, hmacsha160_key, 20);
  for (int i = 0; i < 8; i++)
    apdu[25 + i] = (uint8_t)(amount_limit >> (56 - 8 * i));

  esp_err_t err = satochip_execute_apdu(NULL, false, apdu, sizeof(apdu), out,
                                        timeout_ms, "SET_2FA");
  if (err != ESP_OK)
    return err;
  if (out->sw == 0x9000)
    snprintf(out->detail, sizeof(out->detail), "2FA enabled.");
  else
    satochip_fill_simple_detail(out->detail, sizeof(out->detail), "Set 2FA",
                                out->sw);
  return ESP_OK;
}

esp_err_t smartcard_satochip_card_reset_2fa_key(
    const uint8_t *chalresponse, size_t chal_len,
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms) {
  if (!chalresponse || chal_len != 20)
    return ESP_ERR_INVALID_ARG;

  uint8_t apdu[5 + 20];
  apdu[0] = SATOCHIP_CLA;
  apdu[1] = SATOCHIP_INS_RESET_2FA;
  apdu[2] = 0x00;
  apdu[3] = 0x00;
  apdu[4] = 20;
  memcpy(apdu + 5, chalresponse, 20);

  esp_err_t err = satochip_execute_apdu(NULL, false, apdu, sizeof(apdu), out,
                                        timeout_ms, "RESET_2FA");
  if (err != ESP_OK)
    return err;
  if (out->sw == 0x9000)
    snprintf(out->detail, sizeof(out->detail), "2FA disabled.");
  else
    satochip_fill_simple_detail(out->detail, sizeof(out->detail), "Disable 2FA",
                                out->sw);
  return ESP_OK;
}

static esp_err_t satochip_export_pem_certificate(const uint8_t *der,
                                                 size_t der_len,
                                                 smartcard_satochip_certificate_t *out) {
  if (!out)
    return ESP_ERR_INVALID_ARG;
  memset(out->pem, 0, sizeof(out->pem));
  if (!der || der_len == 0)
    return ESP_ERR_INVALID_ARG;
  esp_err_t err = satochip_der_to_pem(der, der_len, out->pem,
                                      sizeof(out->pem));
  if (err == ESP_OK) {
    memcpy(out->der, der, der_len < sizeof(out->der) ? der_len : sizeof(out->der));
    out->der_len = der_len < sizeof(out->der) ? der_len : sizeof(out->der);
    out->has_certificate = true;
  }
  return err;
}

esp_err_t smartcard_satochip_card_export_perso_certificate(
    smartcard_satochip_certificate_t *out, uint32_t timeout_ms) {
  if (!out)
    return ESP_ERR_INVALID_ARG;
  memset(out, 0, sizeof(*out));
  out->err = ESP_ERR_INVALID_STATE;

  satochip_session_t session;
  char detail[128] = {0};
  esp_err_t err =
      satochip_session_open(&session, timeout_ms, detail, sizeof(detail));
  if (err != ESP_OK) {
    snprintf(out->detail, sizeof(out->detail), "%s",
             detail[0] ? detail : "Device initialization must be completed first.");
    out->err = err;
    return err;
  }

  uint8_t response[SMARTCARD_CCID_RESPONSE_MAX_LEN];
  size_t response_len = 0;
  uint16_t sw = 0;
  uint8_t init_apdu[] = {SATOCHIP_CLA, SATOCHIP_INS_EXPORT_PKI_CERTIFICATE,
                         0x00, 0x01, 0x00};
  err = satochip_transmit_card(&session.sc, init_apdu, sizeof(init_apdu),
                               response, sizeof(response), &response_len, &sw,
                               timeout_ms);
  out->sw = sw;
  if (err != ESP_OK) {
    snprintf(out->detail, sizeof(out->detail), "Certificate export failed: %s.",
             esp_err_to_name(err));
    out->err = err;
    secure_zero(&session, sizeof(session));
    return err;
  }
  if (sw != 0x9000 || response_len < 2) {
    snprintf(out->detail, sizeof(out->detail), "Certificate export returned SW=%04X.", sw);
    out->err = ESP_OK;
    secure_zero(&session, sizeof(session));
    return ESP_OK;
  }

  size_t certificate_size = ((size_t)response[0] << 8) | response[1];
  if (certificate_size == 0) {
    snprintf(out->detail, sizeof(out->detail), "Certificate is empty.");
    out->err = ESP_OK;
    return ESP_OK;
  }

  if (certificate_size > sizeof(out->der))
    certificate_size = sizeof(out->der);
  size_t copied = 0;
  size_t chunk_size = 128;
  while (copied < certificate_size) {
    size_t this_chunk = certificate_size - copied;
    if (this_chunk > chunk_size)
      this_chunk = chunk_size;
    uint8_t chunk_apdu[] = {
        SATOCHIP_CLA, SATOCHIP_INS_EXPORT_PKI_CERTIFICATE, 0x00, 0x02,
        0x04,        0x00, 0x00, 0x00, 0x00};
    chunk_apdu[5] = (uint8_t)(copied >> 8);
    chunk_apdu[6] = (uint8_t)copied;
    chunk_apdu[7] = 0x00;
    chunk_apdu[8] = (uint8_t)this_chunk;
    response_len = 0;
    sw = 0;
    err = satochip_transmit_card(&session.sc, chunk_apdu, sizeof(chunk_apdu),
                                 response, sizeof(response), &response_len,
                                 &sw, timeout_ms);
    out->sw = sw;
    if (err != ESP_OK || sw != 0x9000) {
      snprintf(out->detail, sizeof(out->detail), "Certificate block read failed, SW=%04X.",
               sw);
      out->err = err;
      secure_zero(&session, sizeof(session));
      return err;
    }
    if (response_len < this_chunk) {
      snprintf(out->detail, sizeof(out->detail), "Certificate block is too short.");
      out->err = ESP_ERR_INVALID_RESPONSE;
      secure_zero(&session, sizeof(session));
      return out->err;
    }
    memcpy(out->der + copied, response, this_chunk);
    copied += this_chunk;
  }

  out->der_len = copied;
  err = satochip_export_pem_certificate(out->der, out->der_len, out);
  if (err != ESP_OK) {
    snprintf(out->detail, sizeof(out->detail), "Certificate PEM conversion failed.");
    out->err = err;
    secure_zero(&session, sizeof(session));
    return err;
  }
  snprintf(out->detail, sizeof(out->detail), "Certificate exported.");
  out->err = ESP_OK;
  secure_zero(&session, sizeof(session));
  return ESP_OK;
}

esp_err_t smartcard_satochip_card_import_perso_certificate(
    const char *cert_text, smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms) {
  if (!cert_text || !out)
    return ESP_ERR_INVALID_ARG;
  memset(out, 0, sizeof(*out));
  out->err = ESP_ERR_INVALID_STATE;

  uint8_t der[SMARTCARD_CCID_RESPONSE_MAX_LEN];
  size_t der_len = 0;
  esp_err_t err = satochip_pem_to_der(cert_text, der, sizeof(der), &der_len);
  if (err != ESP_OK) {
    snprintf(out->detail, sizeof(out->detail), "Certificate parsing failed.");
    out->err = err;
    return err;
  }

  uint8_t init_apdu[7];
  init_apdu[0] = SATOCHIP_CLA;
  init_apdu[1] = SATOCHIP_INS_IMPORT_PKI_CERTIFICATE;
  init_apdu[2] = 0x00;
  init_apdu[3] = 0x01;
  init_apdu[4] = 0x02;
  init_apdu[5] = (uint8_t)(der_len >> 8);
  init_apdu[6] = (uint8_t)der_len;
  err = satochip_execute_apdu(NULL, false, init_apdu, sizeof(init_apdu), out,
                              timeout_ms, "IMPORT_CERT_INIT");
  if (err != ESP_OK)
    return err;
  if (out->sw != 0x9000) {
    satochip_fill_simple_detail(out->detail, sizeof(out->detail),
                                "Certificate import init", out->sw);
    out->err = ESP_OK;
    return ESP_OK;
  }

  size_t offset = 0;
  while (offset < der_len) {
    size_t chunk = der_len - offset;
    if (chunk > 128)
      chunk = 128;
    uint8_t apdu[5 + 4 + 128];
    apdu[0] = SATOCHIP_CLA;
    apdu[1] = SATOCHIP_INS_IMPORT_PKI_CERTIFICATE;
    apdu[2] = 0x00;
    apdu[3] = 0x02;
    apdu[4] = (uint8_t)(4 + chunk);
    apdu[5] = (uint8_t)(offset >> 8);
    apdu[6] = (uint8_t)offset;
    apdu[7] = (uint8_t)(chunk >> 8);
    apdu[8] = (uint8_t)chunk;
    memcpy(apdu + 9, der + offset, chunk);
    err = satochip_execute_apdu(NULL, false, apdu, 9U + chunk, out, timeout_ms,
                                "IMPORT_CERT");
    if (err != ESP_OK)
      return err;
    if (out->sw != 0x9000) {
      satochip_fill_simple_detail(out->detail, sizeof(out->detail),
                                  "Certificate import", out->sw);
      out->err = ESP_OK;
      return ESP_OK;
    }
    offset += chunk;
  }

  out->err = ESP_OK;
  snprintf(out->detail, sizeof(out->detail), "Certificate imported.");
  return ESP_OK;
}

esp_err_t smartcard_satochip_card_import_ndef_authentikey(
    const uint8_t privkey[32], smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms) {
  if (!privkey || !out)
    return ESP_ERR_INVALID_ARG;
  uint8_t apdu[5 + 32];
  apdu[0] = SATOCHIP_CLA;
  apdu[1] = SATOCHIP_INS_IMPORT_PKI_NDEF_AUTHENTIKEY;
  apdu[2] = 0x00;
  apdu[3] = 0x00;
  apdu[4] = 32;
  memcpy(apdu + 5, privkey, 32);
  esp_err_t err = satochip_execute_apdu(NULL, false, apdu, sizeof(apdu), out,
                                        timeout_ms, "IMPORT_NDEF_AUTHENTIKEY");
  if (err != ESP_OK)
    return err;
  if (out->sw == 0x9000)
    snprintf(out->detail, sizeof(out->detail), "NDEF authentikey imported.");
  else
    satochip_fill_simple_detail(out->detail, sizeof(out->detail),
                                "Import NDEF authentikey", out->sw);
  return ESP_OK;
}

esp_err_t smartcard_satochip_card_export_authentikey(
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms) {
  if (!out)
    return ESP_ERR_INVALID_ARG;
  memset(out, 0, sizeof(*out));
  out->err = ESP_ERR_INVALID_STATE;

  satochip_session_t session;
  char detail[128] = {0};
  esp_err_t err = satochip_session_open(&session, timeout_ms, detail,
                                        sizeof(detail));
  if (err == ESP_OK)
    err = satochip_session_load_authentikey(&session, timeout_ms, detail,
                                            sizeof(detail));
  if (err != ESP_OK) {
    snprintf(out->detail, sizeof(out->detail), "%s",
             detail[0] ? detail : "Authentikey export failed.");
    secure_zero(&session, sizeof(session));
    out->err = err;
    return err;
  }

  uint8_t apdu[] = {SATOCHIP_CLA, SATOCHIP_INS_BIP32_GET_AUTHENTIKEY, 0x00,
                    0x00};
  uint8_t response[SMARTCARD_CCID_RESPONSE_MAX_LEN];
  size_t response_len = 0;
  uint16_t sw = 0;
  err = satochip_transmit_card(&session.sc, apdu, sizeof(apdu), response,
                               sizeof(response), &response_len, &sw,
                               timeout_ms);
  out->sw = sw;
  if (err != ESP_OK) {
    snprintf(out->detail, sizeof(out->detail), "Exporting authentikey failed: %s.",
             esp_err_to_name(err));
    secure_zero(&session, sizeof(session));
    out->err = err;
    return err;
  }
  if (sw != 0x9000) {
    if (sw == 0x9C04)
      snprintf(out->detail, sizeof(out->detail), "Device is not initialized.");
    else
      satochip_fill_simple_detail(out->detail, sizeof(out->detail),
                                  "Export authentikey", sw);
    secure_zero(&session, sizeof(session));
    out->err = ESP_OK;
    return ESP_OK;
  }

  satochip_apdu_result_fill(out, response, response_len, sw, ESP_OK,
                            "Authentikey exported.");
  out->err = ESP_OK;
  secure_zero(&session, sizeof(session));
  return ESP_OK;
}

esp_err_t smartcard_satochip_card_import_trusted_pubkey(
    const uint8_t pubkey[65], smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms) {
  if (!pubkey || !out)
    return ESP_ERR_INVALID_ARG;
  if (pubkey[0] != 0x04)
    return ESP_ERR_INVALID_ARG;

  uint8_t apdu[5 + 2 + 65];
  apdu[0] = SATOCHIP_CLA;
  apdu[1] = SATOCHIP_INS_IMPORT_TRUSTED_PUBKEY;
  apdu[2] = 0x00;
  apdu[3] = 0x00;
  apdu[4] = 67;
  apdu[5] = 0x00;
  apdu[6] = 0x41;
  memcpy(apdu + 7, pubkey, 65);

  esp_err_t err =
      satochip_execute_apdu(NULL, false, apdu, sizeof(apdu), out, timeout_ms,
                            "IMPORT_TRUSTED_PUBKEY");
  if (err != ESP_OK)
    return err;
  if (out->sw == 0x9000)
    snprintf(out->detail, sizeof(out->detail), "Trusted pubkey imported.");
  else if (out->sw == 0x6D00)
    snprintf(out->detail, sizeof(out->detail), "Card does not support trusted pubkey.");
  else
    satochip_fill_simple_detail(out->detail, sizeof(out->detail),
                                "Import trusted pubkey", out->sw);
  return ESP_OK;
}

esp_err_t smartcard_satochip_card_export_trusted_pubkey(
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms) {
  if (!out)
    return ESP_ERR_INVALID_ARG;
  memset(out, 0, sizeof(*out));
  out->err = ESP_ERR_INVALID_STATE;

  satochip_session_t session;
  char detail[128] = {0};
  esp_err_t err = satochip_session_open(&session, timeout_ms, detail,
                                        sizeof(detail));
  if (err == ESP_OK)
    err = satochip_session_load_authentikey(&session, timeout_ms, detail,
                                            sizeof(detail));
  if (err != ESP_OK) {
    snprintf(out->detail, sizeof(out->detail), "%s",
             detail[0] ? detail : "Trusted pubkey export failed.");
    secure_zero(&session, sizeof(session));
    out->err = err;
    return err;
  }

  uint8_t apdu[] = {SATOCHIP_CLA, SATOCHIP_INS_EXPORT_TRUSTED_PUBKEY, 0x00,
                    0x00};
  uint8_t response[SMARTCARD_CCID_RESPONSE_MAX_LEN];
  size_t response_len = 0;
  uint16_t sw = 0;
  err = satochip_transmit_card(&session.sc, apdu, sizeof(apdu), response,
                               sizeof(response), &response_len, &sw,
                               timeout_ms);
  out->sw = sw;
  if (err != ESP_OK) {
    snprintf(out->detail, sizeof(out->detail), "Exporting trusted pubkey failed: %s.",
             esp_err_to_name(err));
    secure_zero(&session, sizeof(session));
    out->err = err;
    return err;
  }
  if (sw == 0x9C35) {
    snprintf(out->detail, sizeof(out->detail), "Trusted pubkey is not configured.");
    secure_zero(&session, sizeof(session));
    out->err = ESP_OK;
    return ESP_OK;
  }
  if (sw == 0x6D00) {
    snprintf(out->detail, sizeof(out->detail), "Card does not support trusted pubkey.");
    secure_zero(&session, sizeof(session));
    out->err = ESP_OK;
    return ESP_OK;
  }
  if (sw != 0x9000) {
    satochip_fill_simple_detail(out->detail, sizeof(out->detail),
                                "Export trusted pubkey", sw);
    secure_zero(&session, sizeof(session));
    out->err = ESP_OK;
    return ESP_OK;
  }

  satochip_apdu_result_fill(out, response, response_len, sw, ESP_OK,
                            "Trusted pubkey exported.");
  secure_zero(&session, sizeof(session));
  out->err = ESP_OK;
  return ESP_OK;
}

esp_err_t smartcard_satochip_card_logout_all(
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms) {
  if (!out)
    return ESP_ERR_INVALID_ARG;
  uint8_t apdu[] = {SATOCHIP_CLA, SATOCHIP_INS_LOGOUT_ALL, 0x00, 0x00, 0x00};
  esp_err_t err =
      satochip_execute_apdu(NULL, false, apdu, sizeof(apdu), out, timeout_ms,
                            "LOGOUT_ALL");
  if (err != ESP_OK)
    return err;
  if (out->sw == 0x9000)
    snprintf(out->detail, sizeof(out->detail), "Session cleared.");
  else
    satochip_fill_simple_detail(out->detail, sizeof(out->detail),
                                "Clear session", out->sw);
  return ESP_OK;
}

esp_err_t smartcard_satochip_card_challenge_response_pki(
    const uint8_t challenge_from_host[32], smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms) {
  if (!challenge_from_host || !out)
    return ESP_ERR_INVALID_ARG;

  satochip_apdu_result_reset(out);
  out->err = ESP_ERR_INVALID_STATE;

  satochip_session_t session;
  char detail[128] = {0};
  esp_err_t err = satochip_session_open(&session, timeout_ms, detail,
                                        sizeof(detail));
  if (err == ESP_OK)
    err = satochip_session_load_authentikey(&session, timeout_ms, detail,
                                            sizeof(detail));
  if (err != ESP_OK) {
    snprintf(out->detail, sizeof(out->detail), "%s",
             detail[0] ? detail : "Challenge-response session failed.");
    secure_zero(&session, sizeof(session));
    out->err = err;
    return err;
  }

  uint8_t apdu[5 + 32];
  apdu[0] = SATOCHIP_CLA;
  apdu[1] = SATOCHIP_INS_CHALLENGE_RESPONSE_PKI;
  apdu[2] = 0x00;
  apdu[3] = 0x00;
  apdu[4] = 32;
  memcpy(apdu + 5, challenge_from_host, 32);

  uint8_t response[SMARTCARD_CCID_RESPONSE_MAX_LEN];
  size_t response_len = 0;
  uint16_t sw = 0;
  err = satochip_transmit_card(&session.sc, apdu, sizeof(apdu), response,
                               sizeof(response), &response_len, &sw,
                               timeout_ms);
  out->sw = sw;
  if (err != ESP_OK) {
    snprintf(out->detail, sizeof(out->detail), "Challenge-response send failed: %s.",
             esp_err_to_name(err));
    secure_zero(&session, sizeof(session));
    out->err = err;
    return err;
  }
  if (sw != 0x9000 || response_len < 34) {
    satochip_fill_simple_detail(out->detail, sizeof(out->detail),
                                "Challenge response", sw);
    secure_zero(&session, sizeof(session));
    out->err = ESP_OK;
    return ESP_OK;
  }

  uint16_t sig_len = read_be16(response + 32);
  if (sig_len == 0 || response_len != (size_t)(34U + sig_len)) {
    snprintf(out->detail, sizeof(out->detail),
             "Challenge-response length mismatch: expected %u, got %u.", (unsigned)(34U + sig_len),
             (unsigned)response_len);
    secure_zero(&session, sizeof(session));
    out->err = ESP_ERR_INVALID_RESPONSE;
    return ESP_ERR_INVALID_RESPONSE;
  }

  satochip_apdu_result_fill(out, response, response_len, sw, ESP_OK,
                            "Challenge response read.");
  secure_zero(&session, sizeof(session));
  out->err = ESP_OK;
  return ESP_OK;
}

esp_err_t smartcard_satochip_card_verify_authenticity(
    smartcard_satochip_authenticity_t *out, uint32_t timeout_ms) {
  if (!out)
    return ESP_ERR_INVALID_ARG;
  memset(out, 0, sizeof(*out));
  out->err = ESP_ERR_INVALID_STATE;

  smartcard_satochip_certificate_t *cert = calloc(1, sizeof(*cert));
  char *ca_text = calloc(1, sizeof(out->ca_text));
  char *subca_text = calloc(1, sizeof(out->subca_text));
  smartcard_satochip_apdu_result_t *chal = calloc(1, sizeof(*chal));
  if (!cert || !ca_text || !subca_text || !chal) {
    snprintf(out->error, sizeof(out->error), "Out of memory; authenticity check canceled.");
    out->err = ESP_ERR_NO_MEM;
    free(cert);
    free(ca_text);
    free(subca_text);
    free(chal);
    return out->err;
  }

  esp_err_t err = smartcard_satochip_card_export_perso_certificate(cert,
                                                                    timeout_ms);
  if (err != ESP_OK) {
    snprintf(out->error, sizeof(out->error), "Certificate export failed: %s.",
             esp_err_to_name(err));
    out->err = err;
    secure_zero(cert, sizeof(*cert));
    free(cert);
    free(ca_text);
    free(subca_text);
    free(chal);
    return err;
  }
  if (!cert->has_certificate) {
    snprintf(out->error, sizeof(out->error), "Device certificate is empty.");
    out->err = ESP_OK;
    secure_zero(cert, sizeof(*cert));
    free(cert);
    free(ca_text);
    free(subca_text);
    free(chal);
    return ESP_OK;
  }

  snprintf(out->certificate_pem, sizeof(out->certificate_pem), "%s",
           cert->pem);
  out->has_certificate = true;
  out->sw = cert->sw;

  char subject_cn[128] = {0};
  uint8_t device_pubkey[65] = {0};
  err = satochip_x509_parse_text_and_pubkey(
      cert->pem, subject_cn, sizeof(subject_cn), out->device_text,
      sizeof(out->device_text), device_pubkey);
  if (err != ESP_OK) {
    snprintf(out->error, sizeof(out->error), "Device certificate parsing failed.");
    out->err = err;
    secure_zero(device_pubkey, sizeof(device_pubkey));
    secure_zero(cert, sizeof(*cert));
    free(cert);
    free(ca_text);
    free(subca_text);
    free(chal);
    return err;
  }
  snprintf(out->subject_cn, sizeof(out->subject_cn), "%s", subject_cn);

  char uid_sha1[41] = {0};
  err = satochip_extract_uid_sha1_via_apdu(uid_sha1, timeout_ms);
  if (err != ESP_OK) {
    snprintf(out->error, sizeof(out->error), "UID read failed.");
    secure_zero(device_pubkey, sizeof(device_pubkey));
    out->err = err;
    secure_zero(cert, sizeof(*cert));
    free(cert);
    free(ca_text);
    free(subca_text);
    free(chal);
    return err;
  }

  if (!satochip_ascii_ieq(subject_cn, uid_sha1)) {
    snprintf(out->error, sizeof(out->error),
             "Certificate CN does not match UID_SHA1.");
    out->authentic = false;
    secure_zero(device_pubkey, sizeof(device_pubkey));
    out->err = ESP_OK;
    secure_zero(cert, sizeof(*cert));
    free(cert);
    free(ca_text);
    free(subca_text);
    free(chal);
    return ESP_OK;
  }

  const satochip_cert_chain_candidate_t candidates[] = {
      {.ca_pem = k_satochip_ca_pem,
       .subca_pem = k_satochip_subca_pem,
       .label = "production"},
      {.ca_pem = k_satochip_test_ca_pem,
       .subca_pem = k_satochip_test_subca_pem,
       .label = "test"}};
  char chain_error[256] = {0};
  bool chain_ok = false;
  bool test_chain = false;
  for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
    ca_text[0] = '\0';
    subca_text[0] = '\0';
    err = satochip_verify_certificate_chain_candidate(
        cert->pem, &candidates[i], ca_text, sizeof(out->ca_text), subca_text,
        sizeof(out->subca_text));
    if (err == ESP_OK) {
      snprintf(out->ca_text, sizeof(out->ca_text), "%s", ca_text);
      snprintf(out->subca_text, sizeof(out->subca_text), "%s", subca_text);
      chain_ok = true;
      test_chain = (i != 0);
      break;
    }
    if (chain_error[0] == '\0') {
      snprintf(chain_error, sizeof(chain_error), "%s certificate chain verification failed.",
               candidates[i].label ? candidates[i].label : "cert");
    }
  }

  if (!chain_ok) {
    snprintf(out->error, sizeof(out->error), "%s",
             chain_error[0] ? chain_error : "Certificate chain verification failed.");
    out->authentic = false;
    secure_zero(device_pubkey, sizeof(device_pubkey));
    out->err = ESP_OK;
    secure_zero(cert, sizeof(*cert));
    free(cert);
    free(ca_text);
    free(subca_text);
    free(chal);
    return ESP_OK;
  }

  uint8_t challenge_from_host[32];
  crypto_random_bytes(challenge_from_host, sizeof(challenge_from_host));
  err = smartcard_satochip_card_challenge_response_pki(challenge_from_host,
                                                       chal, timeout_ms);
  if (err != ESP_OK || chal->sw != 0x9000 || chal->response_len < 34) {
    snprintf(out->error, sizeof(out->error), "Challenge response failed.");
    secure_zero(challenge_from_host, sizeof(challenge_from_host));
    secure_zero(device_pubkey, sizeof(device_pubkey));
    out->err = err != ESP_OK ? err : ESP_ERR_INVALID_RESPONSE;
    secure_zero(cert, sizeof(*cert));
    secure_zero(chal, sizeof(*chal));
    free(cert);
    free(ca_text);
    free(subca_text);
    free(chal);
    return out->err;
  }

  uint16_t device_sig_len = read_be16(chal->response + 32);
  const uint8_t *device_challenge = chal->response;
  const uint8_t *device_sig = chal->response + 34;
  if (device_sig_len == 0 ||
      chal->response_len != (size_t)(34U + device_sig_len)) {
    snprintf(out->error, sizeof(out->error), "Challenge-response length mismatch.");
    out->authentic = false;
    secure_zero(challenge_from_host, sizeof(challenge_from_host));
    secure_zero(device_pubkey, sizeof(device_pubkey));
    out->err = ESP_OK;
    secure_zero(cert, sizeof(*cert));
    secure_zero(chal, sizeof(*chal));
    free(cert);
    free(ca_text);
    free(subca_text);
    free(chal);
    return ESP_OK;
  }

  uint8_t device_pubkey_compressed[33];
  satochip_compress_uncompressed_pubkey(device_pubkey,
                                         device_pubkey_compressed);

  uint8_t challenge_msg[10 + 32 + 32];
  memcpy(challenge_msg, "Challenge:", 10);
  memcpy(challenge_msg + 10, device_challenge, 32);
  memcpy(challenge_msg + 42, challenge_from_host, 32);

  char chal_error[128] = {0};
  err = satochip_verify_der_signature_against_compressed_pubkey(
      challenge_msg, sizeof(challenge_msg), device_sig, device_sig_len,
      device_pubkey_compressed, chal_error, sizeof(chal_error));
  if (err != ESP_OK) {
    snprintf(out->error, sizeof(out->error), "%s",
             chal_error[0] ? chal_error : "Challenge signature verification failed.");
    out->authentic = false;
    secure_zero(challenge_from_host, sizeof(challenge_from_host));
    secure_zero(device_pubkey, sizeof(device_pubkey));
    out->err = err;
    secure_zero(cert, sizeof(*cert));
    secure_zero(chal, sizeof(*chal));
    free(cert);
    free(ca_text);
    free(subca_text);
    free(chal);
    return err;
  }

  if (test_chain) {
    snprintf(out->error, sizeof(out->error),
             "Certificate chain was verified by the test CA; this is not a production certificate.");
    out->authentic = false;
  } else {
    snprintf(out->error, sizeof(out->error), "Certificate chain and challenge response verified.");
    out->authentic = true;
  }
  out->err = ESP_OK;
  secure_zero(challenge_from_host, sizeof(challenge_from_host));
  secure_zero(device_pubkey, sizeof(device_pubkey));
  secure_zero(device_pubkey_compressed, sizeof(device_pubkey_compressed));
  secure_zero(cert, sizeof(*cert));
  secure_zero(chal, sizeof(*chal));
  free(cert);
  free(ca_text);
  free(subca_text);
  free(chal);
  return ESP_OK;
}

esp_err_t smartcard_seedkeeper_list_secret_headers(
    const char *pin, smartcard_seedkeeper_header_list_t *out,
    uint32_t timeout_ms) {
  if (!out)
    return ESP_ERR_INVALID_ARG;
  memset(out, 0, sizeof(*out));
  out->err = ESP_ERR_INVALID_STATE;

  char detail[128] = {0};
  esp_err_t err =
      seedkeeper_select_and_verify(pin, pin && pin[0] != '\0', timeout_ms,
                                   detail, sizeof(detail));
  if (err != ESP_OK) {
    snprintf(out->detail, sizeof(out->detail), "%s", detail[0] ? detail
                                                                : "SeedKeeper connection failed.");
    out->err = err;
    return err;
  }

  uint8_t response[SMARTCARD_CCID_RESPONSE_MAX_LEN];
  size_t response_len = 0;
  uint16_t sw = 0;
  uint8_t init_apdu[] = {SATOCHIP_CLA, SATOCHIP_INS_LIST_SECRET_HEADERS, 0x00,
                         0x01};
  err = seedkeeper_transmit_checked_compat(
      init_apdu, sizeof(init_apdu), response, sizeof(response), &response_len,
      &sw, timeout_ms, "List init", detail, sizeof(detail));
  out->sw = sw;
  if (err != ESP_OK) {
    snprintf(out->detail, sizeof(out->detail), "List initialization failed.");
    out->err = err;
    seedkeeper_compat_session_clear();
    return err;
  }
  if (sw == 0x9C12) {
    snprintf(out->detail, sizeof(out->detail), "No more entries.");
    out->err = ESP_OK;
    seedkeeper_compat_session_clear();
    return ESP_OK;
  }
  if (sw != 0x9000) {
    snprintf(out->detail, sizeof(out->detail), "List returned SW=%04X.", sw);
    out->err = ESP_OK;
    seedkeeper_compat_session_clear();
    return ESP_OK;
  }

  while (sw == 0x9000) {
    if (response_len > 0 && out->count < sizeof(out->headers) /
                                         sizeof(out->headers[0])) {
      smartcard_seedkeeper_header_t *header = &out->headers[out->count];
      if (satochip_parse_seedkeeper_header_block(response, response_len,
                                                 header)) {
        out->count++;
      }
    }

    uint8_t next_apdu[] = {SATOCHIP_CLA, SATOCHIP_INS_LIST_SECRET_HEADERS,
                           0x00, 0x02};
    response_len = 0;
    sw = 0;
    err = seedkeeper_transmit_checked_compat(
        next_apdu, sizeof(next_apdu), response, sizeof(response),
        &response_len, &sw, timeout_ms, "List read", detail, sizeof(detail));
    out->sw = sw;
    if (err != ESP_OK)
      break;
  }

  if (out->count > 1) {
    qsort(out->headers, out->count, sizeof(out->headers[0]),
          satochip_seedkeeper_header_id_cmp);
  }

  if (sw != 0x9C12 && sw != 0x9000 && err == ESP_OK)
    snprintf(out->detail, sizeof(out->detail), "List read complete, SW=%04X.", sw);
  else if (sw == 0x9C12)
    snprintf(out->detail, sizeof(out->detail), "SeedKeeper list read complete.");
  else
    snprintf(out->detail, sizeof(out->detail), "SeedKeeper list read complete.");
  out->err = err;
  seedkeeper_compat_session_clear();
  return err;
}

esp_err_t smartcard_seedkeeper_generate_masterseed(
    const char *pin, uint8_t seed_size, uint8_t export_rights,
    const char *label, smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms) {
  if (!out || !label)
    return ESP_ERR_INVALID_ARG;
  uint8_t label_bytes[80];
  size_t label_len = strlen(label);
  if (label_len > sizeof(label_bytes))
    label_len = sizeof(label_bytes);
  memcpy(label_bytes, label, label_len);

  uint8_t apdu[5 + 1 + 80];
  apdu[0] = SATOCHIP_CLA;
  apdu[1] = SATOCHIP_INS_GENERATE_MASTERSEED;
  apdu[2] = seed_size;
  apdu[3] = export_rights;
  apdu[4] = (uint8_t)(1 + label_len);
  apdu[5] = (uint8_t)label_len;
  memcpy(apdu + 6, label_bytes, label_len);

  esp_err_t err = seedkeeper_execute_apdu(pin, pin && pin[0] != '\0', apdu,
                                          6U + label_len, out, timeout_ms,
                                          "GEN_MASTERSEED");
  if (err != ESP_OK)
    return err;
  if (out->sw == 0x9000 && out->response_len >= 6) {
    uint16_t sid = read_be16(out->response);
    snprintf(out->detail, sizeof(out->detail),
             "Master seed generated: SID=%u.", (unsigned)sid);
  } else {
    satochip_fill_simple_detail(out->detail, sizeof(out->detail),
                                "Generate master seed", out->sw);
  }
  return ESP_OK;
}

esp_err_t smartcard_seedkeeper_generate_2fa_secret(
    const char *pin, uint8_t export_rights, const char *label,
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms) {
  if (!out || !label)
    return ESP_ERR_INVALID_ARG;
  uint8_t label_bytes[80];
  size_t label_len = strlen(label);
  if (label_len > sizeof(label_bytes))
    label_len = sizeof(label_bytes);
  memcpy(label_bytes, label, label_len);

  uint8_t apdu[5 + 1 + 80];
  apdu[0] = SATOCHIP_CLA;
  apdu[1] = SATOCHIP_INS_GENERATE_2FA_SECRET;
  apdu[2] = 0x00;
  apdu[3] = export_rights;
  apdu[4] = (uint8_t)(1 + label_len);
  apdu[5] = (uint8_t)label_len;
  memcpy(apdu + 6, label_bytes, label_len);

  esp_err_t err = seedkeeper_execute_apdu(pin, pin && pin[0] != '\0', apdu,
                                          6U + label_len, out, timeout_ms,
                                          "GEN_2FA");
  if (err != ESP_OK)
    return err;
  if (out->sw == 0x9000 && out->response_len >= 6) {
    uint16_t sid = read_be16(out->response);
    snprintf(out->detail, sizeof(out->detail),
             "2FA secret generated: SID=%u.", (unsigned)sid);
  } else {
    satochip_fill_simple_detail(out->detail, sizeof(out->detail), "Generate 2FA",
                                out->sw);
  }
  return ESP_OK;
}

esp_err_t smartcard_seedkeeper_generate_random_secret(
    const char *pin, uint8_t stype, uint8_t subtype, uint8_t size,
    uint8_t export_rights, const char *label, bool save_entropy,
    const uint8_t *entropy, size_t entropy_len,
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms) {
  if (!out || !label)
    return ESP_ERR_INVALID_ARG;
  if (size < 16 || size > 64)
    return ESP_ERR_INVALID_ARG;
  if ((entropy_len > 0 && !entropy) || entropy_len > 128)
    return ESP_ERR_INVALID_ARG;

  uint8_t label_bytes[96];
  size_t label_len = strlen(label);
  if (label_len > sizeof(label_bytes))
    label_len = sizeof(label_bytes);
  memcpy(label_bytes, label, label_len);

  uint8_t apdu[5 + 3 + 1 + 96 + 1 + 128];
  size_t pos = 0;
  apdu[pos++] = SATOCHIP_CLA;
  apdu[pos++] = SATOCHIP_INS_GENERATE_RANDOM_SECRET;
  apdu[pos++] = size;
  apdu[pos++] = export_rights;
  apdu[pos++] = 0x00; // lc placeholder, filled after payload is built
  size_t data_start = pos;
  apdu[pos++] = stype;
  apdu[pos++] = subtype;
  apdu[pos++] = save_entropy ? 0x01 : 0x00;
  apdu[pos++] = (uint8_t)label_len;
  memcpy(apdu + pos, label_bytes, label_len);
  pos += label_len;
  apdu[pos++] = (uint8_t)entropy_len;
  if (entropy_len > 0) {
    memcpy(apdu + pos, entropy, entropy_len);
    pos += entropy_len;
  }
  apdu[4] = (uint8_t)(pos - data_start);

  esp_err_t err = seedkeeper_execute_apdu(pin, pin && pin[0] != '\0', apdu,
                                          pos, out, timeout_ms,
                                          "GEN_RANDOM_SECRET");
  if (err != ESP_OK)
    return err;
  if (out->sw == 0x9000 && out->response_len >= 6) {
    uint16_t sid = read_be16(out->response);
    snprintf(out->detail, sizeof(out->detail),
             "Random secret generated: SID=%u.", (unsigned)sid);
  } else {
    satochip_fill_simple_detail(out->detail, sizeof(out->detail),
                                "Generate random secret", out->sw);
  }
  return ESP_OK;
}

esp_err_t smartcard_seedkeeper_derive_master_password(
    const char *pin, const char *salt, uint16_t sid, uint16_t sid_pubkey,
    bool has_sid_pubkey, smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms) {
  if (!out || !salt)
    return ESP_ERR_INVALID_ARG;
  if (has_sid_pubkey)
    return ESP_ERR_NOT_SUPPORTED;

  size_t salt_len = strlen(salt);
  if (salt_len > 128)
    return ESP_ERR_INVALID_ARG;

  uint8_t apdu[5 + 2 + 1 + 128];
  apdu[0] = SATOCHIP_CLA;
  apdu[1] = SATOCHIP_INS_DERIVE_MASTER_PASSWORD;
  apdu[2] = 0x01;
  apdu[3] = 0x00;
  apdu[4] = (uint8_t)(3 + salt_len);
  apdu[5] = (uint8_t)(sid >> 8);
  apdu[6] = (uint8_t)sid;
  apdu[7] = (uint8_t)salt_len;
  memcpy(apdu + 8, salt, salt_len);

  esp_err_t err = seedkeeper_execute_apdu(pin, pin && pin[0] != '\0', apdu,
                                          8U + salt_len, out, timeout_ms,
                                          "DERIVE_MASTER_PASSWORD");
  if (err != ESP_OK)
    return err;
  if (out->sw == 0x9000 && out->response_len >= 4) {
    uint16_t derived_len = read_be16(out->response);
    if (out->response_len >= 2U + derived_len + 2U) {
      char derived_hex[512];
      satochip_bytes_to_hex(out->response + 2, derived_len, derived_hex,
                            sizeof(derived_hex));
      snprintf(out->detail, sizeof(out->detail),
               "Derivation succeeded: %s.", derived_hex);
    } else {
      snprintf(out->detail, sizeof(out->detail), "Master password derived.");
    }
  } else {
    satochip_fill_simple_detail(out->detail, sizeof(out->detail), "Derive master password",
                                out->sw);
  }
  return ESP_OK;
}

esp_err_t smartcard_seedkeeper_import_secret(
    const char *pin, const uint8_t *header, size_t header_len,
    const uint8_t *secret, size_t secret_len, uint16_t sid_pubkey,
    const uint8_t *iv, size_t iv_len, const uint8_t *hmac, size_t hmac_len,
    bool secure_import, smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms) {
  if (!out || !header || !secret || secret_len == 0)
    return ESP_ERR_INVALID_ARG;
  if (!pin || pin[0] == '\0')
    return ESP_ERR_INVALID_ARG;
  if (header_len < 2)
    return ESP_ERR_INVALID_ARG;
  if (secure_import) {
    if (!iv || iv_len != 16 || !hmac || hmac_len != 20)
      return ESP_ERR_INVALID_ARG;
  }

  memset(out, 0, sizeof(*out));
  out->err = ESP_ERR_INVALID_STATE;

  smartcard_seedkeeper_header_t header_meta;
  bool has_header_meta =
      satochip_parse_seedkeeper_header_block(header, header_len, &header_meta);

  char detail[128] = {0};
  esp_err_t err = seedkeeper_select_and_verify(pin, true, timeout_ms, detail,
                                               sizeof(detail));
  if (err != ESP_OK) {
    snprintf(out->detail, sizeof(out->detail), "%s", detail[0] ? detail
                                                                : "PIN verification failed.");
    return err;
  }

  const uint8_t *seedkeeper_header = header + 2;
  size_t seedkeeper_header_len = header_len - 2;
  if (seedkeeper_header_len > 128) {
    snprintf(out->detail, sizeof(out->detail), "SeedKeeper header is too long.");
    seedkeeper_compat_session_clear();
    return ESP_ERR_INVALID_SIZE;
  }

  size_t padded_secret_size = secret_len;
  if (!secure_import) {
    size_t pad = 16U - (secret_len % 16U);
    if (pad == 0)
      pad = 16U;
    padded_secret_size += pad;
  }
  if (padded_secret_size > 0xFFFFU) {
    snprintf(out->detail, sizeof(out->detail), "Secret is too large.");
    seedkeeper_compat_session_clear();
    return ESP_ERR_INVALID_SIZE;
  }

  uint8_t apdu[5 + 255];
  size_t apdu_len = 0;
  uint8_t p1 = secure_import ? 0x02 : 0x01;

  apdu[0] = SATOCHIP_CLA;
  apdu[1] = SATOCHIP_INS_IMPORT_SECRET;
  apdu[2] = p1;
  apdu[3] = 0x01;
  size_t init_lc = seedkeeper_header_len + 2U;
  if (secure_import)
    init_lc += 2U + iv_len;
  if (init_lc > 255U) {
    snprintf(out->detail, sizeof(out->detail), "Import initialization data is too long.");
    seedkeeper_compat_session_clear();
    return ESP_ERR_INVALID_SIZE;
  }

  apdu[4] = (uint8_t)init_lc;
  memcpy(apdu + 5, seedkeeper_header, seedkeeper_header_len);
  apdu_len = 5 + seedkeeper_header_len;
  if (secure_import) {
    apdu[apdu_len++] = (uint8_t)(sid_pubkey >> 8);
    apdu[apdu_len++] = (uint8_t)sid_pubkey;
    memcpy(apdu + apdu_len, iv, iv_len);
    apdu_len += iv_len;
  }
  apdu[apdu_len++] = (uint8_t)(padded_secret_size >> 8);
  apdu[apdu_len++] = (uint8_t)padded_secret_size;

  uint8_t response[SMARTCARD_CCID_RESPONSE_MAX_LEN];
  size_t response_len = 0;
  uint16_t sw = 0;
  err = seedkeeper_transmit_checked_compat(
      apdu, apdu_len, response, sizeof(response), &response_len, &sw,
      timeout_ms, "Import init", detail, sizeof(detail));
  out->sw = sw;
  if (err != ESP_OK) {
    snprintf(out->detail, sizeof(out->detail), "Import initialization failed: %s.",
             esp_err_to_name(err));
    out->err = err;
    seedkeeper_compat_session_clear();
    return err;
  }
  if (sw != 0x9000) {
    if (sw == 0x9C33)
      snprintf(out->detail, sizeof(out->detail), "Import failed: MAC check error.");
    else
      satochip_fill_simple_detail(out->detail, sizeof(out->detail), "Import init",
                                  sw);
    out->err = ESP_OK;
    seedkeeper_compat_session_clear();
    return ESP_OK;
  }

  size_t offset = 0;
  size_t remaining = secret_len;
  while (remaining > 128U) {
    size_t chunk = 128U;
    apdu[0] = SATOCHIP_CLA;
    apdu[1] = SATOCHIP_INS_IMPORT_SECRET;
    apdu[2] = p1;
    apdu[3] = 0x02;
    apdu[4] = (uint8_t)(2 + chunk);
    apdu[5] = (uint8_t)(chunk >> 8);
    apdu[6] = (uint8_t)chunk;
    memcpy(apdu + 7, secret + offset, chunk);
    err = seedkeeper_transmit_checked_compat(
        apdu, 7 + chunk, response, sizeof(response), &response_len, &sw,
        timeout_ms, "Import chunk", detail, sizeof(detail));
    out->sw = sw;
    if (err != ESP_OK) {
      snprintf(out->detail, sizeof(out->detail), "Import chunk failed: %s.",
               esp_err_to_name(err));
      out->err = err;
      seedkeeper_compat_session_clear();
      return err;
    }
    if (sw != 0x9000) {
      satochip_fill_simple_detail(out->detail, sizeof(out->detail), "Import chunk",
                                  sw);
      out->err = ESP_OK;
      seedkeeper_compat_session_clear();
      return ESP_OK;
    }
    offset += chunk;
    remaining -= chunk;
  }

  apdu[0] = SATOCHIP_CLA;
  apdu[1] = SATOCHIP_INS_IMPORT_SECRET;
  apdu[2] = p1;
  apdu[3] = 0x03;
  apdu[4] = (uint8_t)(2 + remaining + (secure_import ? (1U + hmac_len) : 0U));
  apdu[5] = (uint8_t)(remaining >> 8);
  apdu[6] = (uint8_t)remaining;
  memcpy(apdu + 7, secret + offset, remaining);
  apdu_len = 7 + remaining;
  if (secure_import) {
    apdu[apdu_len++] = (uint8_t)hmac_len;
    memcpy(apdu + apdu_len, hmac, hmac_len);
    apdu_len += hmac_len;
  }
  err = seedkeeper_transmit_checked_compat(
      apdu, apdu_len, response, sizeof(response), &response_len, &sw,
      timeout_ms, "Import finish", detail, sizeof(detail));
  out->sw = sw;
  if (err != ESP_OK) {
    snprintf(out->detail, sizeof(out->detail), "Import finish failed: %s.",
             esp_err_to_name(err));
    out->err = err;
    seedkeeper_compat_session_clear();
    return err;
  }
  if (response_len > 0)
    satochip_apdu_result_fill(out, response, response_len, sw, ESP_OK, NULL);

  if (sw == 0x9000 && response_len >= 6) {
    uint16_t sid = read_be16(response);
    if (has_header_meta && header_meta.label[0]) {
      snprintf(out->detail, sizeof(out->detail),
               "Import succeeded: %s SID=%u.", header_meta.label, (unsigned)sid);
    } else {
      snprintf(out->detail, sizeof(out->detail),
               "Import succeeded: SID=%u.", (unsigned)sid);
    }
  } else {
    satochip_fill_simple_detail(out->detail, sizeof(out->detail), "Import finish",
                                sw);
  }

  out->err = ESP_OK;
  seedkeeper_compat_session_clear();
  return ESP_OK;
}

esp_err_t smartcard_seedkeeper_export_secret(
    const char *pin, uint16_t sid, uint16_t sid_pubkey, bool secure_export,
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms) {
  if (!out)
    return ESP_ERR_INVALID_ARG;
  if (!pin || pin[0] == '\0')
    return ESP_ERR_INVALID_ARG;

  memset(out, 0, sizeof(*out));
  out->err = ESP_ERR_INVALID_STATE;

  char detail[128] = {0};
  esp_err_t err =
      seedkeeper_select_and_verify(pin, true, timeout_ms, detail,
                                   sizeof(detail));
  if (err != ESP_OK) {
    snprintf(out->detail, sizeof(out->detail), "%s", detail[0] ? detail
                                                                : "PIN verification failed.");
    return err;
  }

  uint8_t response[SMARTCARD_CCID_RESPONSE_MAX_LEN];
  size_t response_len = 0;
  uint16_t sw = 0;
  uint8_t p1 = secure_export ? 0x02 : 0x01;
  uint8_t apdu[5 + 4];
  size_t apdu_len = 0;
  apdu[0] = SATOCHIP_CLA;
  apdu[1] = SATOCHIP_INS_EXPORT_SECRET;
  apdu[2] = p1;
  apdu[3] = 0x01;
  apdu[4] = (uint8_t)(secure_export ? 4 : 2);
  apdu[5] = (uint8_t)(sid >> 8);
  apdu[6] = (uint8_t)sid;
  apdu_len = 7;
  if (secure_export) {
    apdu[7] = (uint8_t)(sid_pubkey >> 8);
    apdu[8] = (uint8_t)sid_pubkey;
    apdu_len = 9;
  }

  err = seedkeeper_transmit_checked_compat(
      apdu, apdu_len, response, sizeof(response), &response_len, &sw,
      timeout_ms, "Export init", detail, sizeof(detail));
  out->sw = sw;
  if (err != ESP_OK) {
    snprintf(out->detail, sizeof(out->detail), "Export initialization failed: %s.",
             esp_err_to_name(err));
    out->err = err;
    seedkeeper_compat_session_clear();
    return err;
  }
  if (sw != 0x9000) {
    if (sw == 0x9C31)
      snprintf(out->detail, sizeof(out->detail), "Export blocked by policy.");
    else if (sw == 0x9C08)
      snprintf(out->detail, sizeof(out->detail), "Secret does not exist.");
    else if (sw == 0x9C30)
      snprintf(out->detail, sizeof(out->detail), "Card is busy. Retry.");
    else if (sw == 0x9C0F)
      snprintf(out->detail, sizeof(out->detail), "Invalid parameter.");
    else
      satochip_fill_simple_detail(out->detail, sizeof(out->detail), "Export init",
                                  sw);
    out->err = ESP_OK;
    seedkeeper_compat_session_clear();
    return ESP_OK;
  }

  smartcard_seedkeeper_header_t header_meta;
  bool has_header_meta =
      satochip_parse_seedkeeper_header_block(response, response_len,
                                             &header_meta);
  satochip_apdu_result_append(out, response, response_len);

  uint8_t export_apdu[5 + 4];
  memcpy(export_apdu, apdu, apdu_len);
  export_apdu[3] = 0x02;
  size_t total_payload = 0;
  bool done = false;
  while (!done) {
    response_len = 0;
    sw = 0;
    err = seedkeeper_transmit_checked_compat(
        export_apdu, apdu_len, response, sizeof(response), &response_len, &sw,
        timeout_ms, "Export chunk", detail, sizeof(detail));
    out->sw = sw;
    if (err != ESP_OK) {
      snprintf(out->detail, sizeof(out->detail), "Export chunk failed: %s.",
               esp_err_to_name(err));
      out->err = err;
      seedkeeper_compat_session_clear();
      return err;
    }
    if (sw != 0x9000) {
      satochip_fill_simple_detail(out->detail, sizeof(out->detail), "Export chunk",
                                  sw);
      out->err = ESP_OK;
      seedkeeper_compat_session_clear();
      return ESP_OK;
    }

    satochip_apdu_result_append(out, response, response_len);
    if (response_len < 2) {
      done = true;
      break;
    }
    size_t chunk_size = read_be16(response);
    if (response_len >= 2U + chunk_size)
      total_payload += chunk_size;
    if (response_len > 2U + chunk_size)
      done = true;
  }

  if (has_header_meta && header_meta.label[0]) {
    snprintf(out->detail, sizeof(out->detail),
             "Export complete: %s, read %u bytes.", header_meta.label,
             (unsigned)total_payload);
  } else {
    snprintf(out->detail, sizeof(out->detail), "Export complete, read %u bytes.",
             (unsigned)total_payload);
  }

  out->err = ESP_OK;
  seedkeeper_compat_session_clear();
  return ESP_OK;
}

esp_err_t smartcard_seedkeeper_export_secret_to_satochip(
    const char *pin, uint16_t sid, uint16_t sid_pubkey,
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms) {
  if (!out)
    return ESP_ERR_INVALID_ARG;
  if (!pin || pin[0] == '\0')
    return ESP_ERR_INVALID_ARG;

  memset(out, 0, sizeof(*out));
  out->err = ESP_ERR_INVALID_STATE;

  char detail[128] = {0};
  esp_err_t err =
      seedkeeper_select_and_verify(pin, true, timeout_ms, detail,
                                   sizeof(detail));
  if (err != ESP_OK) {
    snprintf(out->detail, sizeof(out->detail), "%s", detail[0] ? detail
                                                                : "PIN verification failed.");
    return err;
  }

  uint8_t apdu[] = {SATOCHIP_CLA, SATOCHIP_INS_EXPORT_SECRET_TO_SATOCHIP,
                    0x00, 0x00, 0x04, (uint8_t)(sid >> 8), (uint8_t)sid,
                    (uint8_t)(sid_pubkey >> 8), (uint8_t)sid_pubkey};
  uint8_t response[SMARTCARD_CCID_RESPONSE_MAX_LEN];
  size_t response_len = 0;
  uint16_t sw = 0;
  err = seedkeeper_transmit_checked_compat(
      apdu, sizeof(apdu), response, sizeof(response), &response_len, &sw,
      timeout_ms, "Export to Satochip", detail, sizeof(detail));
  out->sw = sw;
  if (err != ESP_OK) {
    snprintf(out->detail, sizeof(out->detail), "Export to Satochip failed: %s.",
             esp_err_to_name(err));
    out->err = err;
    seedkeeper_compat_session_clear();
    return err;
  }
  if (sw != 0x9000) {
    if (sw == 0x9C31)
      snprintf(out->detail, sizeof(out->detail), "Export blocked by policy.");
    else if (sw == 0x9C08)
      snprintf(out->detail, sizeof(out->detail), "Secret does not exist.");
    else if (sw == 0x9C0F)
      snprintf(out->detail, sizeof(out->detail), "Invalid parameter.");
    else
      satochip_fill_simple_detail(out->detail, sizeof(out->detail),
                                  "Export to Satochip", sw);
    out->err = ESP_OK;
    seedkeeper_compat_session_clear();
    return ESP_OK;
  }

  satochip_apdu_result_append(out, response, response_len);
  smartcard_seedkeeper_header_t header_meta;
  if (satochip_parse_seedkeeper_header_block(response, response_len,
                                             &header_meta) &&
      header_meta.label[0]) {
    snprintf(out->detail, sizeof(out->detail),
             "Exported to Satochip: %s.", header_meta.label);
  } else {
    snprintf(out->detail, sizeof(out->detail), "Exported to Satochip.");
  }
  out->err = ESP_OK;
  seedkeeper_compat_session_clear();
  return ESP_OK;
}

esp_err_t smartcard_seedkeeper_reset_secret(
    const char *pin, uint16_t sid, smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms) {
  if (!out)
    return ESP_ERR_INVALID_ARG;
  if (!pin || pin[0] == '\0')
    return ESP_ERR_INVALID_ARG;

  uint8_t apdu[] = {SATOCHIP_CLA, SATOCHIP_INS_RESET_SECRET, 0x00, 0x00, 0x02,
                    (uint8_t)(sid >> 8), (uint8_t)sid};
  esp_err_t err = seedkeeper_execute_apdu(pin, true, apdu, sizeof(apdu), out,
                                          timeout_ms, "RESET_SECRET");
  if (err != ESP_OK) {
    snprintf(out->detail, sizeof(out->detail), "Reset secret failed: %s.",
             esp_err_to_name(err));
    out->err = err;
    return err;
  }
  if (out->sw == 0x9000)
    snprintf(out->detail, sizeof(out->detail), "Secret reset.");
  else
    satochip_fill_simple_detail(out->detail, sizeof(out->detail), "Reset secret",
                                out->sw);
  out->err = ESP_OK;
  return ESP_OK;
}

esp_err_t smartcard_seedkeeper_setup_pin(
    const char *new_pin, smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms) {
  if (!out)
    return ESP_ERR_INVALID_ARG;
  satochip_apdu_result_reset(out);

  size_t new_pin_len = new_pin ? strlen(new_pin) : 0;
  if (new_pin_len < 4 || new_pin_len > 16) {
    out->err = ESP_ERR_INVALID_ARG;
    snprintf(out->detail, sizeof(out->detail), "PIN must be 4-16 characters.");
    return ESP_ERR_INVALID_ARG;
  }

  static const uint8_t default_pin[] = {'M', 'u', 's', 'c',
                                        'l', 'e', '0', '0'};
  uint8_t unblock_pin0[16];
  uint8_t pin1[16];
  uint8_t unblock_pin1[16];
  crypto_random_bytes(unblock_pin0, sizeof(unblock_pin0));
  crypto_random_bytes(pin1, sizeof(pin1));
  crypto_random_bytes(unblock_pin1, sizeof(unblock_pin1));

  uint8_t apdu[5 + 96];
  size_t pos = 0;
  apdu[pos++] = SATOCHIP_CLA;
  apdu[pos++] = SATOCHIP_INS_SETUP;
  apdu[pos++] = 0x00;
  apdu[pos++] = 0x00;
  size_t lc_pos = pos++;
  size_t data_start = pos;

  apdu[pos++] = (uint8_t)sizeof(default_pin);
  memcpy(apdu + pos, default_pin, sizeof(default_pin));
  pos += sizeof(default_pin);

  apdu[pos++] = 0x05;
  apdu[pos++] = 0x01;
  apdu[pos++] = (uint8_t)new_pin_len;
  memcpy(apdu + pos, new_pin, new_pin_len);
  pos += new_pin_len;
  apdu[pos++] = (uint8_t)sizeof(unblock_pin0);
  memcpy(apdu + pos, unblock_pin0, sizeof(unblock_pin0));
  pos += sizeof(unblock_pin0);

  apdu[pos++] = 0x01;
  apdu[pos++] = 0x01;
  apdu[pos++] = (uint8_t)sizeof(pin1);
  memcpy(apdu + pos, pin1, sizeof(pin1));
  pos += sizeof(pin1);
  apdu[pos++] = (uint8_t)sizeof(unblock_pin1);
  memcpy(apdu + pos, unblock_pin1, sizeof(unblock_pin1));
  pos += sizeof(unblock_pin1);

  apdu[pos++] = 0x00;
  apdu[pos++] = 0x20;
  apdu[pos++] = 0x00;
  apdu[pos++] = 0x00;
  apdu[pos++] = 0x01;
  apdu[pos++] = 0x01;
  apdu[pos++] = 0x01;
  apdu[lc_pos] = (uint8_t)(pos - data_start);

  esp_err_t err = seedkeeper_execute_apdu(NULL, false, apdu, pos, out,
                                          timeout_ms, "SETUP");
  secure_zero(unblock_pin0, sizeof(unblock_pin0));
  secure_zero(pin1, sizeof(pin1));
  secure_zero(unblock_pin1, sizeof(unblock_pin1));
  if (err != ESP_OK) {
    secure_zero(apdu, sizeof(apdu));
    return err;
  }

  if (out->sw == 0x9000) {
    snprintf(out->detail, sizeof(out->detail), "Setup complete.");
  } else if (out->sw == 0x9C03) {
    snprintf(out->detail, sizeof(out->detail),
             "Card is already initialized. Use Change PIN to change it.");
  } else if (out->sw == 0x9C0F || out->sw == 0x6700) {
    snprintf(out->detail, sizeof(out->detail), "PIN or parameter is invalid.");
  } else {
    satochip_fill_simple_detail(out->detail, sizeof(out->detail), "Set PIN",
                                out->sw);
  }
  secure_zero(apdu, sizeof(apdu));
  return ESP_OK;
}

esp_err_t smartcard_seedkeeper_change_pin(
    uint8_t pin_nbr, const char *old_pin, const char *new_pin,
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms) {
  if (!old_pin || !new_pin)
    return ESP_ERR_INVALID_ARG;
  size_t old_len = strlen(old_pin);
  size_t new_len = strlen(new_pin);
  if (old_len == 0 || new_len == 0 || old_len > 64 || new_len > 64)
    return ESP_ERR_INVALID_ARG;

  uint8_t apdu[5 + 1 + 64 + 1 + 64];
  size_t pos = 0;
  apdu[pos++] = SATOCHIP_CLA;
  apdu[pos++] = SATOCHIP_INS_CHANGE_PIN;
  apdu[pos++] = pin_nbr;
  apdu[pos++] = 0x00;
  apdu[pos++] = (uint8_t)(1 + old_len + 1 + new_len);
  apdu[pos++] = (uint8_t)old_len;
  memcpy(apdu + pos, old_pin, old_len);
  pos += old_len;
  apdu[pos++] = (uint8_t)new_len;
  memcpy(apdu + pos, new_pin, new_len);
  pos += new_len;

  esp_err_t err = seedkeeper_execute_apdu(old_pin, true, apdu, pos, out,
                                          timeout_ms, "SEEDKEEPER_CHANGE_PIN");
  secure_zero(apdu, sizeof(apdu));
  if (err != ESP_OK)
    return err;
  if (out->sw == 0x9000) {
    snprintf(out->detail, sizeof(out->detail), "PIN changed.");
  } else if ((out->sw & 0xFFC0U) == 0x63C0U) {
    snprintf(out->detail, sizeof(out->detail), "Wrong PIN, %u attempts remaining.",
             (unsigned)(out->sw & 0x000FU));
  } else {
    satochip_fill_simple_detail(out->detail, sizeof(out->detail), "Change PIN",
                                out->sw);
  }
  return ESP_OK;
}

static void seedkeeper_format_reset_lock_step_detail(
    smartcard_satochip_apdu_result_t *out, const char *step_name,
    const char *next_hint) {
  if (!out)
    return;
  if (out->sw == 0xFF00) {
    snprintf(out->detail, sizeof(out->detail),
             "Restored to a blank card. Reinsert it, then set the PIN again.");
  } else if (out->sw == 0x9C0C) {
    snprintf(out->detail, sizeof(out->detail),
             "%s is locked. Next step: %s.", step_name ? step_name : "Current step",
             next_hint ? next_hint : "Continue");
  } else if ((out->sw & 0xFFC0U) == 0x63C0U) {
    snprintf(out->detail, sizeof(out->detail), "Wrong %s, %u attempts remaining.",
             step_name ? step_name : "input",
             (unsigned)(out->sw & 0x000FU));
  } else if (out->sw == 0x9000) {
    snprintf(out->detail, sizeof(out->detail),
             "Input was correct, so reset was aborted. Re-enter and use the wrong value.");
  } else if (out->sw == 0x9C03) {
    snprintf(out->detail, sizeof(out->detail),
             "PIN is not locked yet; the PUK step cannot run. Run the wrong-PIN step first.");
  } else if (out->sw == 0x9C04) {
    snprintf(out->detail, sizeof(out->detail),
             "The card is not initialized; reset is not needed.");
  } else {
    satochip_fill_simple_detail(out->detail, sizeof(out->detail),
                                step_name ? step_name : "Reset", out->sw);
  }
}

esp_err_t smartcard_seedkeeper_reset_wrong_pin_step(
    const char *wrong_pin, smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms) {
  if (!out)
    return ESP_ERR_INVALID_ARG;
  satochip_apdu_result_reset(out);

  size_t pin_len = wrong_pin ? strlen(wrong_pin) : 0;
  if (pin_len < 4 || pin_len > 16) {
    out->err = ESP_ERR_INVALID_ARG;
    snprintf(out->detail, sizeof(out->detail), "Wrong PIN must be 4-16 characters.");
    return ESP_ERR_INVALID_ARG;
  }

  uint8_t apdu[5 + 16];
  apdu[0] = SATOCHIP_CLA;
  apdu[1] = SATOCHIP_INS_VERIFY_PIN;
  apdu[2] = 0x00;
  apdu[3] = 0x00;
  apdu[4] = (uint8_t)pin_len;
  memcpy(apdu + 5, wrong_pin, pin_len);

  esp_err_t err = seedkeeper_execute_apdu(NULL, false, apdu, 5U + pin_len, out,
                                          timeout_ms, "SEEDKEEPER_WRONG_PIN");
  secure_zero(apdu, sizeof(apdu));
  if (err != ESP_OK)
    return err;

  seedkeeper_format_reset_lock_step_detail(out, "PIN", "wrong PUK");
  out->err = ESP_OK;
  return ESP_OK;
}

esp_err_t smartcard_seedkeeper_reset_wrong_puk_step(
    const char *wrong_puk, smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms) {
  if (!out)
    return ESP_ERR_INVALID_ARG;
  satochip_apdu_result_reset(out);

  size_t puk_len = wrong_puk ? strlen(wrong_puk) : 0;
  if (puk_len < 4 || puk_len > 16) {
    out->err = ESP_ERR_INVALID_ARG;
    snprintf(out->detail, sizeof(out->detail), "Wrong PUK must be 4-16 characters.");
    return ESP_ERR_INVALID_ARG;
  }

  uint8_t apdu[5 + 16];
  apdu[0] = SATOCHIP_CLA;
  apdu[1] = SATOCHIP_INS_UNBLOCK_PIN;
  apdu[2] = 0x00;
  apdu[3] = 0x00;
  apdu[4] = (uint8_t)puk_len;
  memcpy(apdu + 5, wrong_puk, puk_len);

  esp_err_t err = seedkeeper_execute_apdu(NULL, false, apdu, 5U + puk_len, out,
                                          timeout_ms, "SEEDKEEPER_WRONG_PUK");
  secure_zero(apdu, sizeof(apdu));
  if (err != ESP_OK)
    return err;

  seedkeeper_format_reset_lock_step_detail(out, "PUK", "wait for reset completion");
  out->err = ESP_OK;
  return ESP_OK;
}

esp_err_t smartcard_seedkeeper_reset_factory_signal(
    const char *pin, smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms) {
  (void)pin;
  (void)timeout_ms;
  if (!out)
    return ESP_ERR_INVALID_ARG;
  satochip_apdu_result_reset(out);
  out->err = ESP_ERR_NOT_SUPPORTED;
  out->sw = 0x9C20;
  snprintf(out->detail, sizeof(out->detail),
           "New SeedKeeper cards do not support B0FF reset by reinsertion. Use the wrong PIN / wrong PUK flow.");
  return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t smartcard_seedkeeper_print_logs(const char *pin,
                                          smartcard_satochip_apdu_result_t *out,
                                          uint32_t timeout_ms) {
  if (!out)
    return ESP_ERR_INVALID_ARG;
  memset(out, 0, sizeof(*out));

  char detail[128] = {0};
  esp_err_t err =
      seedkeeper_select_and_verify(pin, pin && pin[0] != '\0', timeout_ms,
                                   detail, sizeof(detail));
  if (err != ESP_OK) {
    snprintf(out->detail, sizeof(out->detail), "%s", detail[0] ? detail
                                                                : "PIN verification failed.");
    return err;
  }

  uint8_t response[SMARTCARD_CCID_RESPONSE_MAX_LEN];
  size_t response_len = 0;
  uint16_t sw = 0;
  uint8_t init_apdu[] = {SATOCHIP_CLA, SATOCHIP_INS_SEEDKEEPER_LOGS, 0x00,
                         0x01};
  err = seedkeeper_transmit_checked_compat(
      init_apdu, sizeof(init_apdu), response, sizeof(response), &response_len,
      &sw, timeout_ms, "Log read", detail, sizeof(detail));
  if (err != ESP_OK) {
    snprintf(out->detail, sizeof(out->detail), "Log read failed.");
    out->err = err;
    seedkeeper_compat_session_clear();
    return err;
  }

  size_t total_logs = 0;
  size_t avail_logs = 0;
  if (response_len >= 4) {
    total_logs = read_be16(response);
    avail_logs = read_be16(response + 2);
  }

  size_t pos = 0;
  pos += (size_t)snprintf(out->detail + pos, sizeof(out->detail) - pos,
                          "Total logs=%u available=%u\n", (unsigned)total_logs,
                          (unsigned)avail_logs);
  while (sw == 0x9000) {
    size_t copy = response_len;
    if (copy > sizeof(out->response) - out->response_len)
      copy = sizeof(out->response) - out->response_len;
    if (copy > 0) {
      memcpy(out->response + out->response_len, response, copy);
      out->response_len += copy;
    }
    if (response_len >= 7) {
      uint8_t line[7];
      memcpy(line, response, 7);
      pos += (size_t)snprintf(out->detail + pos, sizeof(out->detail) - pos,
                              "%02X %02X %02X %02X %02X %02X %02X\n",
                              line[0], line[1], line[2], line[3], line[4],
                              line[5], line[6]);
      if (pos >= sizeof(out->detail))
        break;
    }

    uint8_t next_apdu[] = {SATOCHIP_CLA, SATOCHIP_INS_SEEDKEEPER_LOGS, 0x00,
                           0x02};
    response_len = 0;
    sw = 0;
    err = seedkeeper_transmit_checked_compat(
        next_apdu, sizeof(next_apdu), response, sizeof(response),
        &response_len, &sw, timeout_ms, "Log read", detail, sizeof(detail));
    if (err != ESP_OK)
      break;
  }

  out->sw = sw;
  out->err = err;
  seedkeeper_compat_session_clear();
  if (sw == 0x9000 || sw == 0x9C12)
    return ESP_OK;
  return err;
}
