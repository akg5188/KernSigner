#ifndef MNEMONIC_TOOLS_H
#define MNEMONIC_TOOLS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

char *mnemonic_tools_from_indices(const uint16_t *indices, size_t count);
char *mnemonic_tools_from_indices_unchecked(const uint16_t *indices,
                                            size_t count);
char *mnemonic_tools_from_raw_bits(const char *bits);
char *mnemonic_tools_from_hex_entropy(const char *hex);
char *mnemonic_tools_from_d6_entropy(const char *text, size_t entropy_len,
                                     int *roll_count_out, int *bits_out);
char *mnemonic_tools_from_card_entropy(const char *text, size_t entropy_len,
                                       int *card_count_out, int *bits_out);
char *mnemonic_tools_from_sha256_text(const char *input, size_t entropy_len);
char *mnemonic_tools_from_labeled_hash(const char *label, const char *input,
                                       size_t entropy_len);
char *mnemonic_tools_bip85_child(uint32_t words, uint32_t index);
char *mnemonic_tools_xor_with_current(const char *other_mnemonic);
char *mnemonic_tools_secondary_shift(const char *source_mnemonic,
                                     const char *shift_entries,
                                     char default_operator);
bool mnemonic_tools_parse_uint32(const char *text, uint32_t *out);

#endif // MNEMONIC_TOOLS_H
