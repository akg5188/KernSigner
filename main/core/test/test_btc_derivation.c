#include "../btc_derivation.h"

#include <stdio.h>
#include <string.h>

#include <wally_bip32.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) printf("Testing: %s... ", name)
#define PASS()                                                                 \
  do {                                                                         \
    printf("PASS\n");                                                          \
    tests_passed++;                                                            \
  } while (0)
#define FAIL(msg)                                                              \
  do {                                                                         \
    printf("FAIL: %s\n", msg);                                                 \
    tests_failed++;                                                            \
  } while (0)

static uint32_t hardened(uint32_t value) {
  return value | BIP32_INITIAL_HARDENED_CHILD;
}

static void write_u32_le(unsigned char *out, uint32_t value) {
  out[0] = (unsigned char)(value & 0xffU);
  out[1] = (unsigned char)((value >> 8) & 0xffU);
  out[2] = (unsigned char)((value >> 16) & 0xffU);
  out[3] = (unsigned char)((value >> 24) & 0xffU);
}

static size_t make_keypath(unsigned char *out, const uint32_t *path,
                           size_t depth) {
  out[0] = 0xaa;
  out[1] = 0xbb;
  out[2] = 0xcc;
  out[3] = 0xdd;
  for (size_t i = 0; i < depth; i++)
    write_u32_le(out + BIP32_KEY_FINGERPRINT_LEN + i * 4U, path[i]);
  return BIP32_KEY_FINGERPRINT_LEN + depth * 4U;
}

static void expect_path(const char *name, const uint32_t *path, size_t depth,
                        btc_derivation_script_t script, bool is_testnet,
                        uint32_t account, const char *expected) {
  TEST(name);
  unsigned char keypath[32];
  char out[96];
  size_t keypath_len = make_keypath(keypath, path, depth);
  if (!btc_derivation_satochip_sign_path(keypath, keypath_len, script,
                                         is_testnet, account, out,
                                         sizeof(out))) {
    FAIL("path conversion returned false");
    return;
  }
  if (strcmp(out, expected) != 0) {
    char msg[160];
    snprintf(msg, sizeof(msg), "'%s' != '%s'", out, expected);
    FAIL(msg);
    return;
  }
  PASS();
}

static void expect_reject(const char *name, const uint32_t *path, size_t depth,
                          btc_derivation_script_t script) {
  TEST(name);
  unsigned char keypath[32];
  char out[96];
  size_t keypath_len = make_keypath(keypath, path, depth);
  if (btc_derivation_satochip_sign_path(keypath, keypath_len, script, false, 0,
                                        out, sizeof(out))) {
    FAIL("path conversion should have failed");
    return;
  }
  PASS();
}

int main(void) {
  printf("=== BTC derivation path tests ===\n\n");

  {
    uint32_t path[] = {hardened(84), hardened(0), hardened(0), 0, 5};
    expect_path("full BIP84 path is preserved", path, 5,
                BTC_DERIVATION_SCRIPT_P2WPKH, false, 0,
                "m/84'/0'/0'/0/5");
  }

  {
    uint32_t path[] = {0, 0};
    expect_path("Electrum zpub relative receive path", path, 2,
                BTC_DERIVATION_SCRIPT_P2WPKH, false, 0,
                "m/84'/0'/0'/0/0");
  }

  {
    uint32_t path[] = {1, 23};
    expect_path("Electrum nested segwit relative change path", path, 2,
                BTC_DERIVATION_SCRIPT_P2SH_P2WPKH, true, 7,
                "m/49'/1'/7'/1/23");
  }

  {
    uint32_t path[] = {0, 3};
    expect_path("Electrum legacy relative path", path, 2,
                BTC_DERIVATION_SCRIPT_P2PKH, false, 0,
                "m/44'/0'/0'/0/3");
  }

  {
    uint32_t path[] = {0, 0};
    expect_reject("relative path without script type is rejected", path, 2,
                  BTC_DERIVATION_SCRIPT_UNKNOWN);
  }

  {
    uint32_t path[] = {hardened(0), 0};
    expect_reject("relative hardened branch is rejected", path, 2,
                  BTC_DERIVATION_SCRIPT_P2WPKH);
  }

  printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
  return tests_failed ? 1 : 0;
}
