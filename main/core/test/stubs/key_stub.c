#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <wally_bip32.h>

bool key_get_derived_key(const char *path, struct ext_key **key_out) {
  (void)path;
  (void)key_out;
  return false;
}

bool key_has_signing_key(void) { return false; }

bool key_mnemonic_is_valid(void) { return false; }

bool key_has_passphrase(void) { return false; }

bool key_get_session_passphrase(char **passphrase_out) {
  (void)passphrase_out;
  return false;
}

bool key_get_mnemonic(char **mnemonic_out) {
  (void)mnemonic_out;
  return false;
}

bool key_get_mnemonic_words(char ***words_out, size_t *word_count_out) {
  (void)words_out;
  (void)word_count_out;
  return false;
}

void key_free_mnemonic_words(char **words, size_t word_count) {
  (void)words;
  (void)word_count;
}
