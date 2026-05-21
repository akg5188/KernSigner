#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "core/wallet.h"

wallet_network_t wallet_get_network(void) { return WALLET_NETWORK_MAINNET; }
uint32_t wallet_get_account(void) { return 0; }
wallet_policy_t wallet_get_policy(void) { return WALLET_POLICY_SINGLESIG; }

#include "../message_sign.c"

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

static void expect_path(const char *name, const char *path, bool is_testnet,
                        bool expected) {
  TEST(name);
  bool got = message_sign_path_allowed(path, is_testnet);
  if (got != expected) {
    FAIL(got ? "unexpected accept" : "unexpected reject");
    return;
  }
  PASS();
}

static void test_parse_hardened_h(void) {
  TEST("message_sign_parse converts h to apostrophe");
  parsed_sign_message_t parsed = {0};
  if (!message_sign_parse("signmessage m/84h/0h/0h/0/0 ascii:hello",
                          &parsed)) {
    FAIL("parse failed");
    return;
  }
  if (!parsed.derivation_path ||
      strcmp(parsed.derivation_path, "m/84'/0'/0'/0/0") != 0 ||
      !parsed.message || strcmp(parsed.message, "hello") != 0) {
    message_sign_free_parsed(&parsed);
    FAIL("wrong parsed fields");
    return;
  }
  message_sign_free_parsed(&parsed);
  PASS();
}

int main(void) {
  printf("=== message_sign path policy tests ===\n\n");

  expect_path("mainnet BIP84 receive path accepted", "m/84'/0'/0'/0/0",
              false, true);
  expect_path("testnet BIP84 receive path accepted", "m/84'/1'/0'/0/0",
              true, true);
  expect_path("mainnet rejects testnet coin", "m/84'/1'/0'/0/0", false,
              false);
  expect_path("testnet rejects mainnet coin", "m/84'/0'/0'/0/0", true,
              false);
  expect_path("reject EVM coin type", "m/44'/60'/0'/0/0", false, false);
  expect_path("reject legacy BTC purpose", "m/44'/0'/0'/0/0", false, false);
  expect_path("reject taproot message signing for now", "m/86'/0'/0'/0/0",
              false, false);
  expect_path("reject hardened change/index", "m/84'/0'/0'/0'/0", false,
              false);
  expect_path("reject non-current account", "m/84'/0'/1'/0/0", false, false);
  expect_path("reject change-chain message signing", "m/84'/0'/0'/1/0",
              false, false);

  test_parse_hardened_h();

  printf("\n=== Results: %d passed, %d failed ===\n", tests_passed,
         tests_failed);
  return tests_failed > 0 ? 1 : 0;
}
