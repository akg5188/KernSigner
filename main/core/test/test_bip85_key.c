#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wally_bip32.h>
#include <wally_bip39.h>
#include <wally_bip85.h>
#include <wally_core.h>
#include <wally_crypto.h>

#include "../key.h"
#include "../mnemonic_tools.h"
#include "../../utils/secure_mem.h"

static int tests_passed;
static int tests_failed;

#define TEST(name) printf("Testing: %s... ", name)
#define PASS()                                                                \
  do {                                                                        \
    puts("PASS");                                                             \
    tests_passed++;                                                           \
  } while (0)
#define FAIL(message)                                                         \
  do {                                                                        \
    printf("FAIL: %s\n", message);                                            \
    tests_failed++;                                                           \
  } while (0)

static void test_bip85_from_loaded_mnemonic(const char *name,
                                            const char *mnemonic,
                                            const char *passphrase) {
  TEST(name);
  if (bip39_mnemonic_validate(NULL, mnemonic) != WALLY_OK ||
      !key_load_from_mnemonic(mnemonic, passphrase, false)) {
    FAIL("valid mnemonic did not load");
    return;
  }

  struct ext_key *root = NULL;
  if (!key_get_derived_key("m", &root) || !root) {
    FAIL("root path m could not be copied");
    return;
  }

  unsigned char seed[BIP39_SEED_LEN_512] = {0};
  unsigned char entropy[HMAC_SHA512_LEN] = {0};
  size_t entropy_written = 0;
  struct ext_key *expected_root = NULL;
  char *expected_wally = NULL;
  char *expected = NULL;
  char *actual = NULL;

  bool ok = bip39_mnemonic_to_seed512(mnemonic, passphrase, seed,
                                      sizeof(seed)) == WALLY_OK &&
            bip32_key_from_seed_alloc(seed, sizeof(seed),
                                      BIP32_VER_MAIN_PRIVATE, 0,
                                      &expected_root) == WALLY_OK &&
            bip85_get_bip39_entropy(expected_root, NULL, 12, 158, entropy,
                                    sizeof(entropy),
                                    &entropy_written) == WALLY_OK &&
            bip39_mnemonic_from_bytes(NULL, entropy, entropy_written,
                                      &expected_wally) == WALLY_OK;
  if (ok)
    expected = strdup(expected_wally);
  actual = mnemonic_tools_bip85_child(12, 158);

  if (!ok || !expected || !actual || strcmp(actual, expected) != 0) {
    FAIL("BIP85 index 158 did not match libwally derivation");
  } else {
    PASS();
  }

  free(actual);
  free(expected);
  if (expected_wally)
    wally_free_string(expected_wally);
  if (expected_root)
    bip32_key_free(expected_root);
  bip32_key_free(root);
  secure_memzero(seed, sizeof(seed));
  secure_memzero(entropy, sizeof(entropy));
  key_unload();
}

int main(void) {
  const unsigned char camera_entropy[16] = {
      0x60, 0x55, 0x17, 0x82, 0x11, 0x46, 0x41, 0x6f,
      0x60, 0x55, 0x17, 0x82, 0x11, 0x46, 0x41, 0x6f};
  char *camera_wally = NULL;
  if (bip39_mnemonic_from_bytes(NULL, camera_entropy, sizeof(camera_entropy),
                                &camera_wally) != WALLY_OK ||
      !camera_wally) {
    fprintf(stderr, "Could not create camera entropy fixture\n");
    return 1;
  }
  char *camera_mnemonic = strdup(camera_wally);
  wally_free_string(camera_wally);
  if (!camera_mnemonic)
    return 1;

  puts("=== loaded mnemonic BIP85 root tests ===");
  test_bip85_from_loaded_mnemonic("camera-generated mnemonic, index 158",
                                  camera_mnemonic, NULL);
  test_bip85_from_loaded_mnemonic("imported mnemonic with passphrase, index 158",
                                  "abandon abandon abandon abandon abandon "
                                  "abandon abandon abandon abandon abandon "
                                  "abandon about",
                                  "test passphrase");

  free(camera_mnemonic);
  printf("\n=== Results: %d passed, %d failed ===\n", tests_passed,
         tests_failed);
  return tests_failed ? 1 : 0;
}
