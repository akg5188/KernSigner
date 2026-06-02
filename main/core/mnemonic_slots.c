#include "mnemonic_slots.h"
#include "key.h"
#include "settings.h"
#include "wallet.h"
#include "../utils/secure_mem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wally_bip32.h>
#include <wally_bip39.h>
#include <wally_core.h>

#define MNEMONIC_SLOT_TEXT_MAX 256

typedef struct {
  char *mnemonic;
  char fingerprint[9];
  size_t word_count;
} mnemonic_slot_t;

static mnemonic_slot_t slots[MNEMONIC_SLOT_CAPACITY];

static size_t count_words(const char *mnemonic) {
  if (!mnemonic)
    return 0;
  size_t count = 0;
  bool in_word = false;
  for (const char *p = mnemonic; *p; p++) {
    bool sep = *p == ' ' || *p == '\n' || *p == '\r' || *p == '\t';
    if (sep) {
      in_word = false;
    } else if (!in_word) {
      count++;
      in_word = true;
    }
  }
  return count;
}

static bool fingerprint_from_mnemonic(const char *mnemonic, char out[9]) {
  if (!mnemonic || !out)
    return false;
  return key_compute_mnemonic_fingerprint_hex(out, mnemonic);
}

static void clear_slot(size_t index) {
  if (index >= MNEMONIC_SLOT_CAPACITY)
    return;
  SECURE_FREE_STRING(slots[index].mnemonic);
  secure_memzero(slots[index].fingerprint, sizeof(slots[index].fingerprint));
  slots[index].word_count = 0;
}

void mnemonic_slots_clear_all(void) {
  for (size_t i = 0; i < MNEMONIC_SLOT_CAPACITY; i++)
    clear_slot(i);
}

static size_t find_slot_by_fingerprint(const char *fingerprint) {
  if (!fingerprint)
    return MNEMONIC_SLOT_CAPACITY;
  for (size_t i = 0; i < MNEMONIC_SLOT_CAPACITY; i++) {
    if (slots[i].mnemonic && strcmp(slots[i].fingerprint, fingerprint) == 0)
      return i;
  }
  return MNEMONIC_SLOT_CAPACITY;
}

static size_t find_empty_slot(void) {
  for (size_t i = 0; i < MNEMONIC_SLOT_CAPACITY; i++) {
    if (!slots[i].mnemonic)
      return i;
  }
  return MNEMONIC_SLOT_CAPACITY;
}

bool mnemonic_slots_add_mnemonic(const char *mnemonic, size_t *slot_out) {
  if (!mnemonic || bip39_mnemonic_validate(NULL, mnemonic) != WALLY_OK)
    return false;

  char fingerprint[9];
  if (!fingerprint_from_mnemonic(mnemonic, fingerprint))
    return false;

  size_t slot = find_slot_by_fingerprint(fingerprint);
  if (slot == MNEMONIC_SLOT_CAPACITY)
    slot = find_empty_slot();
  if (slot == MNEMONIC_SLOT_CAPACITY)
    return false;

  char *copy = strndup(mnemonic, MNEMONIC_SLOT_TEXT_MAX - 1);
  if (!copy)
    return false;

  clear_slot(slot);
  slots[slot].mnemonic = copy;
  snprintf(slots[slot].fingerprint, sizeof(slots[slot].fingerprint), "%s",
           fingerprint);
  slots[slot].word_count = count_words(copy);

  if (slot_out)
    *slot_out = slot;
  return true;
}

bool mnemonic_slots_add_current(size_t *slot_out) {
  char *mnemonic = NULL;
  if (!key_get_mnemonic(&mnemonic))
    return false;
  bool ok = mnemonic_slots_add_mnemonic(mnemonic, slot_out);
  SECURE_FREE_STRING(mnemonic);
  return ok;
}

bool mnemonic_slots_load(size_t slot_index, const char *passphrase) {
  if (slot_index >= MNEMONIC_SLOT_CAPACITY || !slots[slot_index].mnemonic)
    return false;

  wallet_network_t net = settings_get_default_network();
  wallet_policy_t pol = settings_get_default_policy();
  if (!key_load_from_mnemonic(slots[slot_index].mnemonic, passphrase,
                              net == WALLET_NETWORK_TESTNET))
    return false;
  wallet_cleanup();
  wallet_set_policy(pol);
  return wallet_init(net);
}

bool mnemonic_slots_remove(size_t slot_index) {
  if (slot_index >= MNEMONIC_SLOT_CAPACITY || !slots[slot_index].mnemonic)
    return false;
  clear_slot(slot_index);
  return true;
}

bool mnemonic_slots_get_info(size_t slot_index,
                             mnemonic_slot_info_t *info_out) {
  if (slot_index >= MNEMONIC_SLOT_CAPACITY || !info_out)
    return false;

  info_out->used = slots[slot_index].mnemonic != NULL;
  snprintf(info_out->fingerprint, sizeof(info_out->fingerprint), "%s",
           info_out->used ? slots[slot_index].fingerprint : "");
  info_out->word_count = info_out->used ? slots[slot_index].word_count : 0;
  return true;
}

size_t mnemonic_slots_count(void) {
  size_t count = 0;
  for (size_t i = 0; i < MNEMONIC_SLOT_CAPACITY; i++) {
    if (slots[i].mnemonic)
      count++;
  }
  return count;
}
