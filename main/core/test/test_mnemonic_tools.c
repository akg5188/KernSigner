#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wally_bip39.h>
#include <wally_core.h>
#include <wally_crypto.h>

#include "../mnemonic_tools.h"

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

static char *dup_wally_string(char *text) {
  if (!text)
    return NULL;
  char *copy = strdup(text);
  wally_free_string(text);
  return copy;
}

static char *expected_from_sha256_text(const char *text, size_t entropy_len) {
  unsigned char hash[SHA256_LEN];
  char *mnemonic = NULL;
  if (wally_sha256((const unsigned char *)text, strlen(text), hash,
                   sizeof(hash)) != WALLY_OK)
    return NULL;
  if (bip39_mnemonic_from_bytes(NULL, hash, entropy_len, &mnemonic) != WALLY_OK)
    return NULL;
  return dup_wally_string(mnemonic);
}

static char *expected_from_raw_bits(const char *bits) {
  size_t bit_len = strlen(bits);
  unsigned char entropy[32] = {0};
  char *mnemonic = NULL;
  if (bit_len == 0 || bit_len % 8 != 0 || bit_len > sizeof(entropy) * 8)
    return NULL;
  for (size_t i = 0; i < bit_len; i++) {
    if (bits[i] == '1')
      entropy[i / 8] |= (unsigned char)(1u << (7 - (i % 8)));
    else if (bits[i] != '0')
      return NULL;
  }
  if (bip39_mnemonic_from_bytes(NULL, entropy, bit_len / 8, &mnemonic) !=
      WALLY_OK)
    return NULL;
  return dup_wally_string(mnemonic);
}

static void test_string_eq(const char *name, char *got, const char *expected) {
  TEST(name);
  if (!got) {
    FAIL("got NULL");
    return;
  }
  if (strcmp(got, expected) != 0) {
    char msg[160];
    snprintf(msg, sizeof(msg), "got '%s'", got);
    FAIL(msg);
    free(got);
    return;
  }
  free(got);
  PASS();
}

static void test_generated_eq(const char *name, char *got, char *expected) {
  TEST(name);
  if (!got || !expected) {
    FAIL("got or expected NULL");
    free(got);
    free(expected);
    return;
  }
  if (strcmp(got, expected) != 0) {
    FAIL("mnemonic mismatch");
    free(got);
    free(expected);
    return;
  }
  free(got);
  free(expected);
  PASS();
}

static void test_raw_hex_vector(void) {
  test_string_eq("raw hex BIP39 vector",
                 mnemonic_tools_from_hex_entropy(
                     "00000000000000000000000000000000"),
                 "abandon abandon abandon abandon abandon abandon abandon "
                 "abandon abandon abandon abandon about");
}

static void test_indices_unchecked(void) {
  uint16_t indices[12] = {0};
  indices[11] = 1;

  TEST("unchecked indices can create temporary mnemonic");
  char *mnemonic = mnemonic_tools_from_indices_unchecked(indices, 12);
  if (!mnemonic) {
    FAIL("got NULL");
    return;
  }
  if (strstr(mnemonic, "ability") == NULL) {
    FAIL("expected index 1 word");
    free(mnemonic);
    return;
  }
  if (bip39_mnemonic_validate(NULL, mnemonic) == WALLY_OK) {
    FAIL("temporary mnemonic unexpectedly has a valid checksum");
    free(mnemonic);
    return;
  }
  free(mnemonic);
  PASS();

  TEST("checked indices reject temporary mnemonic checksum");
  mnemonic = mnemonic_tools_from_indices(indices, 12);
  if (mnemonic) {
    FAIL("expected NULL");
    free(mnemonic);
    return;
  }
  PASS();
}

static void test_entropy_source_vectors(void) {
  test_string_eq("binary coin-flip BIP39 vector",
                 mnemonic_tools_from_raw_bits(
                     "00000000000000000000000000000000"
                     "00000000000000000000000000000000"
                     "00000000000000000000000000000000"
                     "00000000000000000000000000000000"),
                 "abandon abandon abandon abandon abandon abandon abandon "
                 "abandon abandon abandon abandon about");

  test_generated_eq("raw bits vector",
                    mnemonic_tools_from_raw_bits(
                        "00000000000000000000000000000000"
                        "00000000000000000000000000000000"
                        "00000000000000000000000000000000"
                        "00000000000000000000000000000000"),
                    expected_from_raw_bits(
                        "00000000000000000000000000000000"
                        "00000000000000000000000000000000"
                        "00000000000000000000000000000000"
                        "00000000000000000000000000000000"));

  test_generated_eq("D20 KernSigner text hash",
                    mnemonic_tools_from_sha256_text(
                        "1-2-3-4-5-6-7-8-9-10-11-12-13-14-15-16-17-18-19-20-"
                        "1-2-3-4-5-6-7-8-9-10",
                        16),
                    expected_from_sha256_text(
                        "1-2-3-4-5-6-7-8-9-10-11-12-13-14-15-16-17-18-19-20-"
                        "1-2-3-4-5-6-7-8-9-10",
                        16));

  int d6_roll_count = 0;
  int d6_bits = 0;
  test_string_eq("BIP39 website D6 entropy vector",
                 mnemonic_tools_from_d6_entropy(
                     "35146 21543 62145 36251 41623 54126 35412 36541 "
                     "23564 12354 62514 32156 43521 64352 14635 212",
                     16, &d6_roll_count, &d6_bits),
                 "spell rather torch entire shrimp raven ignore tired indoor "
                 "vendor tongue slush");

  TEST("D6 entropy stats match BIP39 website");
  if (d6_roll_count != 78 || d6_bits != 129) {
    char msg[96];
    snprintf(msg, sizeof(msg), "rolls=%d bits=%d", d6_roll_count, d6_bits);
    FAIL(msg);
  } else {
    PASS();
  }

  test_string_eq("hex raw entropy vector",
                 mnemonic_tools_from_hex_entropy(
                     "605517821146416f605517821146416f"),
                 "gate post they card goat response life pepper link mechanic "
                 "motion tenant");

  test_generated_eq("legacy card hash helper",
                    mnemonic_tools_from_sha256_text(
                        "A\xE2\x99\xA5 Q\xE2\x99\xA0 9\xE2\x99\xA6 "
                        "T\xE2\x99\xA3",
                        16),
                    expected_from_sha256_text(
                        "A\xE2\x99\xA5 Q\xE2\x99\xA0 9\xE2\x99\xA6 "
                        "T\xE2\x99\xA3",
                        16));

  int card_count = 0;
  int bits = 0;
  test_string_eq("BIP39 website card entropy vector",
                 mnemonic_tools_from_card_entropy(
                     "Ad Js 4h Kc 9d 2s Qh 7c Td 8s 5h Jc As 2h 3c 4d "
                     "5s 6h 7c 8d 9s Th Jc Qd Ks Ah 2c 3d 6s",
                     16, &card_count, &bits),
                 "gadget night claim humble neutral suffer armor snap keen "
                 "flight pass weather");

  TEST("card entropy stats match BIP39 website");
  if (card_count != 29 || bits != 131) {
    char msg[96];
    snprintf(msg, sizeof(msg), "cards=%d bits=%d", card_count, bits);
    FAIL(msg);
  } else {
    PASS();
  }
}

static void test_secondary_shift_vectors(void) {
  const char *source =
      "abandon abandon abandon abandon abandon abandon abandon abandon abandon "
      "abandon abandon about";

  test_string_eq("secondary shift current mnemonic",
                 mnemonic_tools_secondary_shift(
                     source, "+1 -1 +2048 -2049 +3 -3 +7 -7 +30 -30 +0 +2",
                     '+'),
                 "ability zoo abandon zoo about zero abstract you adult wisdom "
                 "abandon absent");

  TEST("secondary shift rejects wrong count");
  char *mnemonic = mnemonic_tools_secondary_shift(source, "+1 -1", '+');
  if (mnemonic) {
    FAIL("expected NULL");
    free(mnemonic);
    return;
  }
  PASS();
}

int main(void) {
  printf("=== mnemonic_tools tests ===\n\n");
  test_raw_hex_vector();
  test_indices_unchecked();
  test_entropy_source_vectors();
  test_secondary_shift_vectors();
  printf("\n=== Results: %d passed, %d failed ===\n", tests_passed,
         tests_failed);
  return tests_failed > 0 ? 1 : 0;
}
