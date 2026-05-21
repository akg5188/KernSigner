#include "mnemonic_tools.h"
#include "key.h"
#include "../utils/secure_mem.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wally_bip32.h>
#include <wally_bip39.h>
#include <wally_bip85.h>
#include <wally_core.h>
#include <wally_crypto.h>

#define MNEMONIC_MAX_TEXT 256
#define MNEMONIC_MAX_WORDS 24
#define CARD_ENTROPY_MAX_TEXT 768
#define CARD_ENTROPY_MAX_BITS 4096
#define D6_ENTROPY_MAX_BITS 4096

static bool valid_entropy_len(size_t len) {
  return len >= BIP39_ENTROPY_LEN_128 && len <= BIP39_ENTROPY_LEN_256 &&
         len % 4 == 0;
}

static bool valid_mnemonic_word_count(size_t count) {
  return count == 12 || count == 15 || count == 18 || count == 21 ||
         count == 24;
}

static bool normalize_card_token_core(const char *tok, char out[3]) {
  if (!tok || !out)
    return false;

  char compact[3] = {0};
  size_t pos = 0;
  for (const char *p = tok; *p; p++) {
    if (isspace((unsigned char)*p))
      continue;
    if (pos >= 2)
      return false;
    compact[pos++] = *p;
  }
  if (pos != 2)
    return false;

  char rank = (char)tolower((unsigned char)compact[0]);
  char suit = (char)tolower((unsigned char)compact[1]);
  const char *ranks = "a23456789tjqk";
  const char *suits = "cdhs";
  if (!strchr(ranks, rank) || !strchr(suits, suit))
    return false;

  out[0] = rank;
  out[1] = suit;
  out[2] = '\0';
  return true;
}

static int card_index_from_token_core(const char card[3]) {
  const char *ranks = "a23456789tjqk";
  const char *suits = "cdhs";
  const char *rank = strchr(ranks, card[0]);
  const char *suit = strchr(suits, card[1]);
  if (!rank || !suit)
    return -1;
  return (int)((suit - suits) * 13 + (rank - ranks));
}

static const char *card_entropy_bits_str_core(const char card[3]) {
  static const char *const bits_by_card[52] = {
      "00000", "00001", "00010", "00011", "00100", "00101", "00110",
      "00111", "01000", "01001", "01010", "01011", "01100", "01101",
      "01110", "01111", "10000", "10001", "10010", "10011", "10100",
      "10101", "10110", "10111", "11000", "11001", "11010", "11011",
      "11100", "11101", "11110", "11111", "0000",  "0001",  "0010",
      "0011",  "0100",  "0101",  "0110",  "0111",  "1000",  "1001",
      "1010",  "1011",  "1100",  "1101",  "1110",  "1111",  "00",
      "01",    "10",    "11",
  };
  int index = card_index_from_token_core(card);
  return index >= 0 ? bits_by_card[index] : NULL;
}

static char *dup_wally_string(char *wally_str) {
  if (!wally_str)
    return NULL;
  char *copy = strdup(wally_str);
  wally_free_string(wally_str);
  return copy;
}

static int word_index_from_word(struct words *wordlist, const char *word) {
  if (!wordlist || !word || !*word)
    return -1;

  for (int i = 0; i < 2048; i++) {
    const char *candidate = bip39_get_word_by_index(wordlist, (size_t)i);
    if (candidate && strcmp(candidate, word) == 0)
      return i;
  }
  return -1;
}

static bool parse_word_index_token(const char *token, int *out) {
  if (!token || !out)
    return false;

  int base = 10;
  const char *p = token;
  if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
    base = 16;
    p += 2;
  } else if (p[0] == '0' && (p[1] == 'o' || p[1] == 'O')) {
    base = 8;
    p += 2;
  }

  char *end = NULL;
  const char *digits_start = p;
  unsigned long value = strtoul(p, &end, base);
  if (!end || end == digits_start || *end != '\0' || value > 2047)
    return false;

  *out = (int)value;
  return true;
}

static int source_index_from_token(struct words *wordlist, const char *token) {
  int index = word_index_from_word(wordlist, token);
  if (index >= 0)
    return index;
  if (parse_word_index_token(token, &index))
    return index;
  return -1;
}

static char *mnemonic_tools_from_indices_impl(const uint16_t *indices,
                                              size_t count,
                                              bool validate_checksum) {
  if (!indices || (count != 12 && count != 15 && count != 18 && count != 21 &&
                   count != 24))
    return NULL;

  struct words *wordlist = NULL;
  if (bip39_get_wordlist(NULL, &wordlist) != WALLY_OK || !wordlist)
    return NULL;

  char mnemonic[MNEMONIC_MAX_TEXT];
  mnemonic[0] = '\0';
  for (size_t i = 0; i < count; i++) {
    if (indices[i] > 2047)
      return NULL;
    const char *word = bip39_get_word_by_index(wordlist, indices[i]);
    if (!word)
      return NULL;
    if (i > 0)
      strncat(mnemonic, " ", sizeof(mnemonic) - strlen(mnemonic) - 1);
    strncat(mnemonic, word, sizeof(mnemonic) - strlen(mnemonic) - 1);
  }

  if (validate_checksum &&
      bip39_mnemonic_validate(NULL, mnemonic) != WALLY_OK) {
    secure_memzero(mnemonic, sizeof(mnemonic));
    return NULL;
  }

  char *out = strdup(mnemonic);
  secure_memzero(mnemonic, sizeof(mnemonic));
  return out;
}

char *mnemonic_tools_from_indices(const uint16_t *indices, size_t count) {
  return mnemonic_tools_from_indices_impl(indices, count, true);
}

char *mnemonic_tools_from_indices_unchecked(const uint16_t *indices,
                                            size_t count) {
  return mnemonic_tools_from_indices_impl(indices, count, false);
}

char *mnemonic_tools_from_raw_bits(const char *bits) {
  if (!bits)
    return NULL;

  size_t bit_len = strlen(bits);
  if (bit_len == 0 || bit_len % 32 != 0)
    return NULL;

  size_t bytes_len = bit_len / 8;
  if (!valid_entropy_len(bytes_len))
    return NULL;

  unsigned char entropy[BIP39_ENTROPY_MAX_LEN] = {0};
  for (size_t i = 0; i < bit_len; i++) {
    if (bits[i] != '0' && bits[i] != '1') {
      secure_memzero(entropy, sizeof(entropy));
      return NULL;
    }
    if (bits[i] == '1')
      entropy[i / 8] |= (uint8_t)(1u << (7 - (i % 8)));
  }

  char *mnemonic = NULL;
  int ret = bip39_mnemonic_from_bytes(NULL, entropy, bytes_len, &mnemonic);
  secure_memzero(entropy, sizeof(entropy));
  return ret == WALLY_OK ? dup_wally_string(mnemonic) : NULL;
}

char *mnemonic_tools_from_hex_entropy(const char *hex) {
  if (!hex)
    return NULL;

  size_t hex_len = strlen(hex);
  if (hex_len == 0 || (hex_len % 2) != 0)
    return NULL;

  size_t bytes_len = hex_len / 2;
  if (!valid_entropy_len(bytes_len))
    return NULL;

  unsigned char entropy[BIP39_ENTROPY_MAX_LEN] = {0};
  size_t written = 0;
  if (wally_hex_to_bytes(hex, entropy, sizeof(entropy), &written) != WALLY_OK ||
      written != bytes_len) {
    secure_memzero(entropy, sizeof(entropy));
    return NULL;
  }

  char *mnemonic = NULL;
  if (bip39_mnemonic_from_bytes(NULL, entropy, written, &mnemonic) != WALLY_OK) {
    secure_memzero(entropy, sizeof(entropy));
    return NULL;
  }

  secure_memzero(entropy, sizeof(entropy));
  return dup_wally_string(mnemonic);
}

char *mnemonic_tools_from_d6_entropy(const char *text, size_t entropy_len,
                                     int *roll_count_out, int *bits_out) {
  if (!text || !valid_entropy_len(entropy_len))
    return NULL;

  char bits[D6_ENTROPY_MAX_BITS];
  size_t bits_pos = 0;
  int roll_count = 0;
  bits[0] = '\0';

  for (const char *p = text; *p; p++) {
    unsigned char c = (unsigned char)*p;
    if (isspace(c) || c == ',' || c == ';' || c == '-' || c == '|')
      continue;

    const char *part = NULL;
    switch (c) {
    case '1':
      part = "01";
      break;
    case '2':
      part = "10";
      break;
    case '3':
      part = "11";
      break;
    case '4':
      part = "0";
      break;
    case '5':
      part = "1";
      break;
    case '6':
      part = "00";
      break;
    default:
      secure_memzero(bits, sizeof(bits));
      return NULL;
    }

    size_t part_len = strlen(part);
    if (bits_pos + part_len + 1 > sizeof(bits)) {
      secure_memzero(bits, sizeof(bits));
      return NULL;
    }
    memcpy(bits + bits_pos, part, part_len);
    bits_pos += part_len;
    bits[bits_pos] = '\0';
    roll_count++;
  }

  if (roll_count_out)
    *roll_count_out = roll_count;
  if (bits_out)
    *bits_out = (int)bits_pos;

  size_t target_bits = entropy_len * 8;
  if (bits_pos < target_bits) {
    secure_memzero(bits, sizeof(bits));
    return NULL;
  }

  const char *selected_bits = bits + bits_pos - target_bits;
  char *mnemonic = mnemonic_tools_from_raw_bits(selected_bits);
  secure_memzero(bits, sizeof(bits));
  return mnemonic;
}

char *mnemonic_tools_from_card_entropy(const char *text, size_t entropy_len,
                                       int *card_count_out, int *bits_out) {
  if (!text || !valid_entropy_len(entropy_len))
    return NULL;

  char compact[CARD_ENTROPY_MAX_TEXT];
  size_t compact_len = 0;
  for (const char *p = text; *p; p++) {
    unsigned char c = (unsigned char)*p;
    if (isspace(c) || c == ',' || c == ';' || c == '-' || c == '|')
      continue;
    if (compact_len + 1 >= sizeof(compact)) {
      secure_memzero(compact, sizeof(compact));
      return NULL;
    }
    compact[compact_len++] = (char)c;
  }
  compact[compact_len] = '\0';

  if (compact_len == 0 || (compact_len % 2) != 0) {
    secure_memzero(compact, sizeof(compact));
    return NULL;
  }

  char bits[CARD_ENTROPY_MAX_BITS];
  size_t bits_pos = 0;
  int card_count = 0;
  bits[0] = '\0';

  for (size_t offset = 0; offset < compact_len; offset += 2) {
    char tok[3] = {compact[offset], compact[offset + 1], '\0'};
    char card[3];
    if (!normalize_card_token_core(tok, card)) {
      secure_memzero(compact, sizeof(compact));
      secure_memzero(bits, sizeof(bits));
      return NULL;
    }

    const char *part = card_entropy_bits_str_core(card);
    if (!part) {
      secure_memzero(compact, sizeof(compact));
      secure_memzero(bits, sizeof(bits));
      return NULL;
    }

    size_t part_len = strlen(part);
    if (bits_pos + part_len + 1 > sizeof(bits)) {
      secure_memzero(compact, sizeof(compact));
      secure_memzero(bits, sizeof(bits));
      return NULL;
    }
    memcpy(bits + bits_pos, part, part_len);
    bits_pos += part_len;
    bits[bits_pos] = '\0';
    card_count++;
  }

  if (card_count_out)
    *card_count_out = card_count;
  if (bits_out)
    *bits_out = (int)bits_pos;

  size_t target_bits = entropy_len * 8;
  if (bits_pos < target_bits) {
    secure_memzero(compact, sizeof(compact));
    secure_memzero(bits, sizeof(bits));
    return NULL;
  }

  // iancoleman/bip39 discards leading overflow bits and keeps the last
  // 32-bit-aligned entropy block before converting to BIP39 words.
  const char *selected_bits = bits + bits_pos - target_bits;
  char *mnemonic = mnemonic_tools_from_raw_bits(selected_bits);
  secure_memzero(compact, sizeof(compact));
  secure_memzero(bits, sizeof(bits));
  return mnemonic;
}

char *mnemonic_tools_from_sha256_text(const char *input, size_t entropy_len) {
  if (!input || !valid_entropy_len(entropy_len))
    return NULL;

  unsigned char hash[SHA256_LEN];
  int ret = wally_sha256((const unsigned char *)input, strlen(input), hash,
                         sizeof(hash));
  if (ret != WALLY_OK)
    return NULL;

  char *mnemonic = NULL;
  ret = bip39_mnemonic_from_bytes(NULL, hash, entropy_len, &mnemonic);
  secure_memzero(hash, sizeof(hash));
  return ret == WALLY_OK ? dup_wally_string(mnemonic) : NULL;
}

char *mnemonic_tools_from_labeled_hash(const char *label, const char *input,
                                       size_t entropy_len) {
  if (!label || !input || !valid_entropy_len(entropy_len))
    return NULL;

  unsigned char hash[SHA256_LEN];
  size_t label_len = strlen(label);
  size_t input_len = strlen(input);
  size_t total = label_len + 1 + input_len;
  char *buf = malloc(total + 1);
  if (!buf)
    return NULL;
  snprintf(buf, total + 1, "%s:%s", label, input);

  int ret = wally_sha256((const unsigned char *)buf, total, hash, sizeof(hash));
  secure_memzero(buf, total);
  free(buf);
  if (ret != WALLY_OK)
    return NULL;

  char *mnemonic = NULL;
  ret = bip39_mnemonic_from_bytes(NULL, hash, entropy_len, &mnemonic);
  secure_memzero(hash, sizeof(hash));
  return ret == WALLY_OK ? dup_wally_string(mnemonic) : NULL;
}

char *mnemonic_tools_bip85_child(uint32_t words, uint32_t index) {
  if (words != 12 && words != 18 && words != 24)
    return NULL;

  struct ext_key *root = NULL;
  if (!key_get_derived_key("m", &root))
    return NULL;

  unsigned char entropy[HMAC_SHA512_LEN] = {0};
  size_t written = 0;
  int ret = bip85_get_bip39_entropy(root, NULL, words, index, entropy,
                                    sizeof(entropy), &written);
  bip32_key_free(root);
  if (ret != WALLY_OK || !valid_entropy_len(written)) {
    secure_memzero(entropy, sizeof(entropy));
    return NULL;
  }

  char *mnemonic = NULL;
  ret = bip39_mnemonic_from_bytes(NULL, entropy, written, &mnemonic);
  secure_memzero(entropy, sizeof(entropy));
  return ret == WALLY_OK ? dup_wally_string(mnemonic) : NULL;
}

char *mnemonic_tools_xor_with_current(const char *other_mnemonic) {
  if (!other_mnemonic || bip39_mnemonic_validate(NULL, other_mnemonic) != WALLY_OK)
    return NULL;

  char *current = NULL;
  if (!key_get_mnemonic(&current))
    return NULL;

  unsigned char left[BIP39_ENTROPY_MAX_LEN] = {0};
  unsigned char right[BIP39_ENTROPY_MAX_LEN] = {0};
  size_t left_len = 0;
  size_t right_len = 0;
  bool ok = bip39_mnemonic_to_bytes(NULL, current, left, sizeof(left),
                                    &left_len) == WALLY_OK &&
            bip39_mnemonic_to_bytes(NULL, other_mnemonic, right, sizeof(right),
                                    &right_len) == WALLY_OK &&
            left_len == right_len && valid_entropy_len(left_len);
  SECURE_FREE_STRING(current);
  if (!ok) {
    secure_memzero(left, sizeof(left));
    secure_memzero(right, sizeof(right));
    return NULL;
  }

  for (size_t i = 0; i < left_len; i++)
    left[i] ^= right[i];

  char *mnemonic = NULL;
  int ret = bip39_mnemonic_from_bytes(NULL, left, left_len, &mnemonic);
  secure_memzero(left, sizeof(left));
  secure_memzero(right, sizeof(right));
  return ret == WALLY_OK ? dup_wally_string(mnemonic) : NULL;
}

bool mnemonic_tools_parse_uint32(const char *text, uint32_t *out) {
  if (!text || !out)
    return false;

  while (isspace((unsigned char)*text))
    text++;
  if (!*text)
    return false;

  char *end = NULL;
  unsigned long value = strtoul(text, &end, 10);
  while (end && isspace((unsigned char)*end))
    end++;
  if (!end || *end != '\0' || value > UINT32_MAX)
    return false;

  *out = (uint32_t)value;
  return true;
}

static bool parse_shift_token(const char *token, char default_operator,
                              int *out_shift) {
  if (!token || !*token || !out_shift)
    return false;

  char op = default_operator == '-' ? '-' : '+';
  const char *p = token;
  if (*p == '+' || *p == '-') {
    op = *p;
    p++;
  }

  if (!*p)
    return false;

  uint32_t value = 0;
  if (!mnemonic_tools_parse_uint32(p, &value))
    return false;

  int shift = (int)(value % 2048U);
  *out_shift = op == '-' ? -shift : shift;
  return true;
}

char *mnemonic_tools_secondary_shift(const char *source_mnemonic,
                                     const char *shift_entries,
                                     char default_operator) {
  if (!source_mnemonic || !shift_entries)
    return NULL;

  if (strlen(source_mnemonic) >= MNEMONIC_MAX_TEXT ||
      strlen(shift_entries) >= MNEMONIC_MAX_TEXT)
    return NULL;

  struct words *wordlist = NULL;
  if (bip39_get_wordlist(NULL, &wordlist) != WALLY_OK || !wordlist)
    return NULL;

  char words_copy[MNEMONIC_MAX_TEXT];
  char shifts_copy[MNEMONIC_MAX_TEXT];
  snprintf(words_copy, sizeof(words_copy), "%s", source_mnemonic);
  snprintf(shifts_copy, sizeof(shifts_copy), "%s", shift_entries);

  const char *delims = " ,;\n\r\t|";
  int indices[MNEMONIC_MAX_WORDS] = {0};
  int shifts[MNEMONIC_MAX_WORDS] = {0};
  size_t word_count = 0;
  size_t shift_count = 0;

  char *save = NULL;
  for (char *tok = strtok_r(words_copy, delims, &save); tok;
       tok = strtok_r(NULL, delims, &save)) {
    if (word_count >= MNEMONIC_MAX_WORDS) {
      secure_memzero(words_copy, sizeof(words_copy));
      secure_memzero(shifts_copy, sizeof(shifts_copy));
      return NULL;
    }
    int idx = source_index_from_token(wordlist, tok);
    if (idx < 0) {
      secure_memzero(words_copy, sizeof(words_copy));
      secure_memzero(shifts_copy, sizeof(shifts_copy));
      return NULL;
    }
    indices[word_count++] = idx;
  }

  save = NULL;
  for (char *tok = strtok_r(shifts_copy, delims, &save); tok;
       tok = strtok_r(NULL, delims, &save)) {
    if (shift_count >= MNEMONIC_MAX_WORDS ||
        !parse_shift_token(tok, default_operator, &shifts[shift_count])) {
      secure_memzero(words_copy, sizeof(words_copy));
      secure_memzero(shifts_copy, sizeof(shifts_copy));
      secure_memzero(indices, sizeof(indices));
      secure_memzero(shifts, sizeof(shifts));
      return NULL;
    }
    shift_count++;
  }

  if (!valid_mnemonic_word_count(word_count) || shift_count != word_count) {
    secure_memzero(words_copy, sizeof(words_copy));
    secure_memzero(shifts_copy, sizeof(shifts_copy));
    secure_memzero(indices, sizeof(indices));
    secure_memzero(shifts, sizeof(shifts));
    return NULL;
  }

  char result[MNEMONIC_MAX_TEXT];
  result[0] = '\0';
  for (size_t i = 0; i < word_count; i++) {
    int shifted = (indices[i] + shifts[i]) % 2048;
    if (shifted < 0)
      shifted += 2048;
    const char *word = bip39_get_word_by_index(wordlist, (size_t)shifted);
    if (!word) {
      secure_memzero(words_copy, sizeof(words_copy));
      secure_memzero(shifts_copy, sizeof(shifts_copy));
      secure_memzero(indices, sizeof(indices));
      secure_memzero(shifts, sizeof(shifts));
      secure_memzero(result, sizeof(result));
      return NULL;
    }
    if (i > 0)
      strncat(result, " ", sizeof(result) - strlen(result) - 1);
    strncat(result, word, sizeof(result) - strlen(result) - 1);
  }

  char *out = strdup(result);
  secure_memzero(words_copy, sizeof(words_copy));
  secure_memzero(shifts_copy, sizeof(shifts_copy));
  secure_memzero(indices, sizeof(indices));
  secure_memzero(shifts, sizeof(shifts));
  secure_memzero(result, sizeof(result));
  return out;
}
