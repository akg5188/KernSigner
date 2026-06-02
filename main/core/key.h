#ifndef KEY_H
#define KEY_H

#include <stdbool.h>
#include <stddef.h>
#include <wally_bip32.h>

bool key_init(void);
bool key_is_loaded(void);
bool key_has_signing_key(void);
bool key_mnemonic_is_valid(void);
bool key_can_backup_mnemonic(void);
bool key_has_passphrase(void);
bool key_load_from_mnemonic(const char *mnemonic, const char *passphrase,
                            bool is_testnet);
bool key_load_from_mnemonic_unchecked(const char *mnemonic);
void key_unload(void);
bool key_get_fingerprint(unsigned char *fingerprint_out);
bool key_get_fingerprint_hex(char *hex_out);
bool key_compute_mnemonic_fingerprint(unsigned char *fingerprint_out,
                                      const char *mnemonic);
bool key_compute_mnemonic_fingerprint_hex(char *hex_out, const char *mnemonic);
bool key_get_mnemonic_fingerprint_hex(char *hex_out);
bool key_get_xpub(const char *path, char **xpub_out);
bool key_get_xpub_versioned(const char *path, uint32_t version,
                            char **xpub_out);
bool key_get_master_xpub(char **xpub_out);
bool key_get_mnemonic(char **mnemonic_out);
bool key_get_mnemonic_words(char ***words_out, size_t *word_count_out);
void key_free_mnemonic_words(char **words, size_t word_count);
bool key_get_session_passphrase(char **passphrase_out);
bool key_get_derived_key(const char *path, struct ext_key **key_out);
void key_set_pending_source_material(const char *label, const char *text,
                                     const char *mnemonic);
void key_clear_pending_source_material(void);
void key_apply_pending_source_material(const char *mnemonic);
bool key_get_source_material(char **label_out, char **text_out);
void key_cleanup(void);

#endif // KEY_H
