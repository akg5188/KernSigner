#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* Pull in full implementations directly so this test exercises the real key
 * derivation and custom address conversion path end-to-end. */
#include "../key.c"
#include "../custom_derivation.c"

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

static const char *TEST_MNEMONIC =
    "abandon abandon abandon abandon abandon abandon "
    "abandon abandon abandon abandon abandon about";

typedef struct {
  const char *name;
  const char *path;
  custom_address_type_t type;
  bool is_testnet;
  const char *expected;
} address_vector_t;

static void test_address_vector(const address_vector_t *vector) {
  TEST(vector->name);

  char address[128] = {0};
  if (!custom_derivation_get_address(vector->path, vector->type,
                                     vector->is_testnet, address,
                                     sizeof(address))) {
    FAIL("custom_derivation_get_address returned false");
    return;
  }

  if (strcmp(address, vector->expected) != 0) {
    char msg[180];
    snprintf(msg, sizeof(msg), "got '%s'", address);
    FAIL(msg);
    return;
  }

  PASS();
}

static void test_tiny_buffer_rejected(void) {
  TEST("P2TR rejects undersized output buffer");

  char tiny[8] = {0};
  if (custom_derivation_get_address("m/86'/0'/0'/0/0", CUSTOM_ADDR_BTC_P2TR,
                                    false, tiny, sizeof(tiny))) {
    FAIL("should reject when buffer is too small");
    return;
  }

  PASS();
}

int main(void) {
  printf("=== custom_derivation address tests ===\n\n");

  printf("--- Setup: loading BIP39 test mnemonic ---\n");
  TEST("key_load_from_mnemonic");
  if (!key_load_from_mnemonic(TEST_MNEMONIC, "", false)) {
    FAIL("failed to load mnemonic");
    printf("\n=== ABORT: key load failed ===\n");
    return 1;
  }
  PASS();

  printf("\n--- Published BTC path vectors ---\n");
  const address_vector_t vectors[] = {
      {
          "BIP44 P2PKH m/44'/0'/0'/0/0",
          "m/44'/0'/0'/0/0",
          CUSTOM_ADDR_BTC_P2PKH,
          false,
          "1LqBGSKuX5yYUonjxT5qGfpUsXKYYWeabA",
      },
      {
          "BIP49 P2SH-P2WPKH m/49'/0'/0'/0/0",
          "m/49'/0'/0'/0/0",
          CUSTOM_ADDR_BTC_P2SH_P2WPKH,
          false,
          "37VucYSaXLCAsxYyAPfbSi9eh4iEcbShgf",
      },
      {
          "BIP84 P2WPKH m/84'/0'/0'/0/0",
          "m/84'/0'/0'/0/0",
          CUSTOM_ADDR_BTC_P2WPKH,
          false,
          "bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu",
      },
      {
          "BIP86 P2TR m/86'/0'/0'/0/0",
          "m/86'/0'/0'/0/0",
          CUSTOM_ADDR_BTC_P2TR,
          false,
          "bc1p5cyxnuxmeuwuvkwfem96lqzszd02n6xdcjrs20cac6yqjjwudpxqkedrcr",
      },
      {
          "EVM m/44'/60'/0'/0/0",
          "m/44'/60'/0'/0/0",
          CUSTOM_ADDR_EVM,
          false,
          "0x9858EfFD232B4033E47d90003D41EC34EcaEda94",
      },
  };

  for (size_t i = 0; i < sizeof(vectors) / sizeof(vectors[0]); i++) {
    test_address_vector(&vectors[i]);
  }

  printf("\n--- Error handling ---\n");
  test_tiny_buffer_rejected();

  key_unload();

  printf("\n=== Results: %d passed, %d failed ===\n", tests_passed,
         tests_failed);
  return tests_failed > 0 ? 1 : 0;
}
