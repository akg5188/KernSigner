#include "key.h"
#include "../utils/secure_mem.h"
#include <esp_log.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wally_bip39.h>
#include <wally_core.h>
#include <wally_crypto.h>

static struct ext_key *master_key = NULL;
static unsigned char fingerprint[BIP32_KEY_FINGERPRINT_LEN];
static char *stored_mnemonic = NULL;
static char *source_material_label = NULL;
static char *source_material_text = NULL;
static char *pending_source_mnemonic = NULL;
static char *pending_source_label = NULL;
static char *pending_source_text = NULL;
static char *session_passphrase = NULL;
static bool key_loaded = false;
static bool signing_key_loaded = false;
static bool mnemonic_checksum_valid = false;

static bool valid_mnemonic_word_count(size_t count) {
  return count == 12 || count == 15 || count == 18 || count == 21 ||
         count == 24;
}

static bool word_in_bip39_list(struct words *wordlist, const char *word) {
  if (!wordlist || !word || !*word)
    return false;

  for (size_t i = 0; i < 2048; i++) {
    const char *candidate = bip39_get_word_by_index(wordlist, i);
    if (candidate && strcmp(candidate, word) == 0)
      return true;
  }
  return false;
}

static bool mnemonic_words_are_bip39(const char *mnemonic) {
  if (!mnemonic)
    return false;

  size_t len = strlen(mnemonic);
  if (len == 0 || len >= 512)
    return false;

  struct words *wordlist = NULL;
  if (bip39_get_wordlist(NULL, &wordlist) != WALLY_OK || !wordlist)
    return false;

  char copy[512];
  snprintf(copy, sizeof(copy), "%s", mnemonic);

  size_t count = 0;
  char *save = NULL;
  for (char *tok = strtok_r(copy, " \n\r\t", &save); tok;
       tok = strtok_r(NULL, " \n\r\t", &save)) {
    if (count >= 24 || !word_in_bip39_list(wordlist, tok)) {
      secure_memzero(copy, sizeof(copy));
      return false;
    }
    count++;
  }

  secure_memzero(copy, sizeof(copy));
  return valid_mnemonic_word_count(count);
}

bool key_init(void) {
  key_loaded = false;
  master_key = NULL;
  secure_memzero(fingerprint, sizeof(fingerprint));
  return true;
}

bool key_is_loaded(void) { return key_loaded; }

bool key_has_signing_key(void) { return key_loaded && signing_key_loaded; }

bool key_mnemonic_is_valid(void) {
  return key_loaded && mnemonic_checksum_valid;
}

bool key_can_backup_mnemonic(void) {
  return key_mnemonic_is_valid() && key_has_signing_key();
}

bool key_has_passphrase(void) {
  return key_has_signing_key() && session_passphrase &&
         session_passphrase[0] != '\0';
}

bool key_load_from_mnemonic(const char *mnemonic, const char *passphrase,
                            bool is_testnet) {
  if (!mnemonic) {
    return false;
  }

  int ret;
  unsigned char seed[BIP39_SEED_LEN_512];
  struct ext_key *new_master_key = NULL;
  unsigned char new_fingerprint[BIP32_KEY_FINGERPRINT_LEN];
  char *new_mnemonic = NULL;
  char *new_passphrase = NULL;

  ret = bip39_mnemonic_validate(NULL, mnemonic);
  if (ret != WALLY_OK) {
    return false;
  }

  ret = bip39_mnemonic_to_seed512(mnemonic, passphrase, seed, sizeof(seed));
  if (ret != WALLY_OK) {
    secure_memzero(seed, sizeof(seed));
    return false;
  }

  uint32_t bip32_version =
      is_testnet ? BIP32_VER_TEST_PRIVATE : BIP32_VER_MAIN_PRIVATE;
  ret = bip32_key_from_seed_alloc(seed, sizeof(seed), bip32_version, 0,
                                  &new_master_key);
  if (ret != WALLY_OK) {
    secure_memzero(seed, sizeof(seed));
    return false;
  }

  ret = bip32_key_get_fingerprint(new_master_key, new_fingerprint,
                                  BIP32_KEY_FINGERPRINT_LEN);
  if (ret != WALLY_OK) {
    bip32_key_free(new_master_key);
    secure_memzero(seed, sizeof(seed));
    secure_memzero(new_fingerprint, sizeof(new_fingerprint));
    return false;
  }

  new_mnemonic = strdup(mnemonic);
  if (!new_mnemonic) {
    bip32_key_free(new_master_key);
    secure_memzero(seed, sizeof(seed));
    secure_memzero(new_fingerprint, sizeof(new_fingerprint));
    return false;
  }

  if (passphrase && passphrase[0] != '\0') {
    new_passphrase = strdup(passphrase);
    if (!new_passphrase) {
      SECURE_FREE_STRING(new_mnemonic);
      bip32_key_free(new_master_key);
      secure_memzero(seed, sizeof(seed));
      secure_memzero(new_fingerprint, sizeof(new_fingerprint));
      return false;
    }
  }

  if (key_loaded)
    key_unload();

  master_key = new_master_key;
  stored_mnemonic = new_mnemonic;
  session_passphrase = new_passphrase;
  memcpy(fingerprint, new_fingerprint, sizeof(fingerprint));
  secure_memzero(seed, sizeof(seed));
  secure_memzero(new_fingerprint, sizeof(new_fingerprint));
  key_loaded = true;
  signing_key_loaded = true;
  mnemonic_checksum_valid = true;

  return true;
}

bool key_load_from_mnemonic_unchecked(const char *mnemonic) {
  if (!mnemonic)
    return false;

  if (bip39_mnemonic_validate(NULL, mnemonic) == WALLY_OK)
    return key_load_from_mnemonic(mnemonic, NULL, false);

  if (!mnemonic_words_are_bip39(mnemonic))
    return false;

  char *new_mnemonic = strdup(mnemonic);
  if (!new_mnemonic)
    return false;

  if (key_loaded)
    key_unload();

  stored_mnemonic = new_mnemonic;
  secure_memzero(fingerprint, sizeof(fingerprint));
  key_loaded = true;
  signing_key_loaded = false;
  mnemonic_checksum_valid = false;
  return true;
}

void key_unload(void) {
  if (master_key) {
    bip32_key_free(master_key);
    master_key = NULL;
  }
  SECURE_FREE_STRING(stored_mnemonic);
  SECURE_FREE_STRING(source_material_label);
  SECURE_FREE_STRING(source_material_text);
  SECURE_FREE_STRING(session_passphrase);
  secure_memzero(fingerprint, sizeof(fingerprint));
  key_loaded = false;
  signing_key_loaded = false;
  mnemonic_checksum_valid = false;
}

bool key_get_fingerprint(unsigned char *fingerprint_out) {
  if (!key_has_signing_key() || !fingerprint_out) {
    return false;
  }
  memcpy(fingerprint_out, fingerprint, BIP32_KEY_FINGERPRINT_LEN);
  return true;
}

bool key_get_fingerprint_hex(char *hex_out) {
  if (!hex_out) {
    return false;
  }
  if (!key_has_signing_key()) {
    snprintf(hex_out, BIP32_KEY_FINGERPRINT_LEN * 2 + 1, "--------");
    return true;
  }
  for (int i = 0; i < BIP32_KEY_FINGERPRINT_LEN; i++) {
    snprintf(hex_out + (i * 2), 3, "%02x", fingerprint[i]);
  }
  hex_out[BIP32_KEY_FINGERPRINT_LEN * 2] = '\0';
  return true;
}

// Parse BIP32 path like "m/84'/0'/0'" into uint32_t array
static bool parse_derivation_path(const char *path, uint32_t *indices_out,
                                  size_t *depth_out, size_t max_depth) {
  if (!path || !path[0] || !indices_out || !depth_out) {
    return false;
  }

  for (const char *c = path; *c; c++) {
    if (*c != 'm' && *c != '/' && *c != '\'' && *c != 'h' &&
        !(*c >= '0' && *c <= '9')) {
      return false;
    }
  }

  const char *p = path;
  if (p[0] == 'm') {
    p++;
    if (p[0] == '/') {
      p++;
      if (p[0] == '\0') {
        return false;
      }
    } else if (p[0] != '\0') {
      return false;
    }
  }
  size_t depth = 0;

  while (*p && depth < max_depth) {
    uint32_t value = 0;
    bool has_digits = false;

    if (*p == '0' && p[1] >= '0' && p[1] <= '9') {
      return false;
    }

    while (*p >= '0' && *p <= '9') {
      uint32_t digit = (uint32_t)(*p - '0');
      if (value > UINT32_MAX / 10 ||
          (value == UINT32_MAX / 10 && digit > UINT32_MAX % 10)) {
        return false;
      }
      value = value * 10 + digit;
      p++;
      has_digits = true;
    }

    if (!has_digits) {
      return false;
    }

    if (*p == '\'' || *p == 'h') {
      if (value >= BIP32_INITIAL_HARDENED_CHILD) {
        return false;
      }
      value |= BIP32_INITIAL_HARDENED_CHILD;
      p++;
    } else if (value >= BIP32_INITIAL_HARDENED_CHILD) {
      return false;
    }

    indices_out[depth++] = value;

    if (*p == '/') {
      p++;
      if (*p == '\0') {
        return false;
      }
    } else if (*p == '\0') {
      break;
    } else {
      return false;
    }
  }

  if (*p != '\0') {
    return false;
  }

  *depth_out = depth;
  return true;
}

bool key_get_xpub(const char *path, char **xpub_out) {
  if (!key_has_signing_key() || !path || !xpub_out) {
    return false;
  }

  uint32_t path_indices[10];
  size_t path_depth = 0;

  if (!parse_derivation_path(path, path_indices, &path_depth, 10)) {
    return false;
  }

  struct ext_key *derived_key = NULL;
  int ret =
      bip32_key_from_parent_path_alloc(master_key, path_indices, path_depth,
                                       BIP32_FLAG_KEY_PRIVATE, &derived_key);
  if (ret != WALLY_OK) {
    return false;
  }

  ret = bip32_key_to_base58(derived_key, BIP32_FLAG_KEY_PUBLIC, xpub_out);
  bip32_key_free(derived_key);

  return (ret == WALLY_OK);
}

bool key_get_xpub_versioned(const char *path, uint32_t version,
                            char **xpub_out) {
  if (!key_has_signing_key() || !path || !xpub_out) {
    return false;
  }

  char *standard_xpub = NULL;
  if (!key_get_xpub(path, &standard_xpub) || !standard_xpub) {
    return false;
  }

  unsigned char serialized[BIP32_SERIALIZED_LEN + BASE58_CHECKSUM_LEN] = {0};
  size_t written = 0;
  int ret = wally_base58_to_bytes(standard_xpub, BASE58_FLAG_CHECKSUM,
                                  serialized, sizeof(serialized), &written);
  wally_free_string(standard_xpub);
  if (ret != WALLY_OK || written != BIP32_SERIALIZED_LEN) {
    secure_memzero(serialized, sizeof(serialized));
    return false;
  }

  serialized[0] = (uint8_t)((version >> 24) & 0xff);
  serialized[1] = (uint8_t)((version >> 16) & 0xff);
  serialized[2] = (uint8_t)((version >> 8) & 0xff);
  serialized[3] = (uint8_t)(version & 0xff);

  ret = wally_base58_from_bytes(serialized, written, BASE58_FLAG_CHECKSUM,
                                xpub_out);
  secure_memzero(serialized, sizeof(serialized));
  return ret == WALLY_OK;
}

bool key_get_master_xpub(char **xpub_out) {
  if (!key_has_signing_key() || !xpub_out) {
    return false;
  }

  int ret = bip32_key_to_base58(master_key, BIP32_FLAG_KEY_PUBLIC, xpub_out);
  return (ret == WALLY_OK);
}

bool key_get_mnemonic(char **mnemonic_out) {
  if (!key_loaded || !stored_mnemonic || !mnemonic_out) {
    return false;
  }

  *mnemonic_out = strdup(stored_mnemonic);
  return (*mnemonic_out != NULL);
}

bool key_get_mnemonic_words(char ***words_out, size_t *word_count_out) {
  if (!key_loaded || !stored_mnemonic || !words_out || !word_count_out) {
    return false;
  }

  char *mnemonic_copy = strdup(stored_mnemonic);
  if (!mnemonic_copy) {
    return false;
  }

  size_t count = 0;
  char *token = strtok(mnemonic_copy, " ");
  while (token) {
    count++;
    token = strtok(NULL, " ");
  }

  if (count == 0) {
    SECURE_FREE_STRING(mnemonic_copy);
    return false;
  }

  char **words = (char **)malloc(count * sizeof(char *));
  if (!words) {
    SECURE_FREE_STRING(mnemonic_copy);
    return false;
  }

  strcpy(mnemonic_copy, stored_mnemonic);
  token = strtok(mnemonic_copy, " ");
  for (size_t i = 0; i < count && token; i++) {
    words[i] = strdup(token);
    if (!words[i]) {
      key_free_mnemonic_words(words, i);
      SECURE_FREE_STRING(mnemonic_copy);
      return false;
    }
    token = strtok(NULL, " ");
  }

  SECURE_FREE_STRING(mnemonic_copy);
  *words_out = words;
  *word_count_out = count;

  return true;
}

void key_free_mnemonic_words(char **words, size_t word_count) {
  if (!words) {
    return;
  }

  for (size_t i = 0; i < word_count; i++) {
    SECURE_FREE_STRING(words[i]);
  }
  free(words);
}

bool key_get_session_passphrase(char **passphrase_out) {
  if (!passphrase_out || !key_has_signing_key())
    return false;

  const char *current = session_passphrase ? session_passphrase : "";
  *passphrase_out = strdup(current);
  return *passphrase_out != NULL;
}

static char *dup_trimmed_source_text(const char *text) {
  if (!text)
    return NULL;

  while (*text && (*text == ' ' || *text == '\n' || *text == '\r' ||
                   *text == '\t'))
    text++;

  size_t len = strlen(text);
  while (len > 0 && (text[len - 1] == ' ' || text[len - 1] == '\n' ||
                     text[len - 1] == '\r' || text[len - 1] == '\t'))
    len--;

  if (len == 0)
    return NULL;

  char *copy = malloc(len + 1);
  if (!copy)
    return NULL;
  memcpy(copy, text, len);
  copy[len] = '\0';
  return copy;
}

void key_set_pending_source_material(const char *label, const char *text,
                                     const char *mnemonic) {
  key_clear_pending_source_material();
  if (!text || !mnemonic)
    return;

  pending_source_text = dup_trimmed_source_text(text);
  pending_source_mnemonic = strdup(mnemonic);
  pending_source_label = strdup(label && label[0] ? label : "原始输入");

  if (!pending_source_text || !pending_source_mnemonic ||
      !pending_source_label) {
    key_clear_pending_source_material();
  }
}

void key_clear_pending_source_material(void) {
  SECURE_FREE_STRING(pending_source_mnemonic);
  SECURE_FREE_STRING(pending_source_label);
  SECURE_FREE_STRING(pending_source_text);
}

void key_apply_pending_source_material(const char *mnemonic) {
  SECURE_FREE_STRING(source_material_label);
  SECURE_FREE_STRING(source_material_text);

  if (mnemonic && pending_source_mnemonic &&
      strcmp(mnemonic, pending_source_mnemonic) == 0 && pending_source_text) {
    source_material_label = pending_source_label;
    source_material_text = pending_source_text;
    pending_source_label = NULL;
    pending_source_text = NULL;
  }

  key_clear_pending_source_material();
}

bool key_get_source_material(char **label_out, char **text_out) {
  if (!label_out || !text_out || !source_material_text)
    return false;

  *label_out = source_material_label ? strdup(source_material_label) : NULL;
  *text_out = strdup(source_material_text);
  if (!*text_out || !*label_out) {
    SECURE_FREE_STRING(*label_out);
    SECURE_FREE_STRING(*text_out);
    return false;
  }
  return true;
}

bool key_get_derived_key(const char *path, struct ext_key **key_out) {
  if (!key_has_signing_key() || !path || !key_out) {
    return false;
  }

  uint32_t path_indices[10];
  size_t path_depth = 0;

  if (!parse_derivation_path(path, path_indices, &path_depth, 10)) {
    return false;
  }

  int ret = bip32_key_from_parent_path_alloc(
      master_key, path_indices, path_depth, BIP32_FLAG_KEY_PRIVATE, key_out);
  return (ret == WALLY_OK);
}

void key_cleanup(void) { key_unload(); }
