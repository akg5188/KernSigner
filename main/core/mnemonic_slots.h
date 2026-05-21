#ifndef MNEMONIC_SLOTS_H
#define MNEMONIC_SLOTS_H

#include <stdbool.h>
#include <stddef.h>

#define MNEMONIC_SLOT_CAPACITY 5

typedef struct {
  bool used;
  char fingerprint[9];
  size_t word_count;
} mnemonic_slot_info_t;

void mnemonic_slots_clear_all(void);
bool mnemonic_slots_add_mnemonic(const char *mnemonic, size_t *slot_out);
bool mnemonic_slots_add_current(size_t *slot_out);
bool mnemonic_slots_load(size_t slot_index, const char *passphrase);
bool mnemonic_slots_remove(size_t slot_index);
bool mnemonic_slots_get_info(size_t slot_index, mnemonic_slot_info_t *info_out);
size_t mnemonic_slots_count(void);

#endif // MNEMONIC_SLOTS_H
