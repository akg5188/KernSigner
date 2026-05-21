#ifndef EVM_H
#define EVM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EVM_ADDRESS_HEX_LEN 42
#define EVM_WEB3_MAX_QR_PAGES 32
#define EVM_WEB3_COMPRESSED_PUBKEY_LEN 33
#define EVM_WEB3_UNCOMPRESSED_PUBKEY_LEN 65
#define EVM_WEB3_CHAIN_CODE_LEN 32
#define EVM_WEB3_MAX_EXTERNAL_LEDGER_KEYS 10
#define EVM_WEB3_MAX_EXTERNAL_BTC_KEYS 2

typedef enum {
  EVM_WEB3_PROFILE_ADDRESS = 0,
  EVM_WEB3_PROFILE_OKX,
  EVM_WEB3_PROFILE_BITGET,
  EVM_WEB3_PROFILE_METAMASK,
  EVM_WEB3_PROFILE_RABBY,
  EVM_WEB3_PROFILE_TOKENPOCKET,
} evm_web3_profile_t;

typedef struct {
  char address[EVM_ADDRESS_HEX_LEN + 1];
  char summary[96];
  char *pages[EVM_WEB3_MAX_QR_PAGES];
  size_t page_count;
  bool animated;
} evm_web3_qr_bundle_t;

typedef struct {
  uint8_t pubkey[EVM_WEB3_COMPRESSED_PUBKEY_LEN];
  uint8_t chain_code[EVM_WEB3_CHAIN_CODE_LEN];
  bool include_chain_code;
  bool include_children;
  char path[48];
  char children_path[12];
  uint8_t parent_fingerprint[4];
  bool has_parent_fingerprint;
  uint32_t coin_type;
  bool has_coin_type;
  int origin_depth;
  int children_depth;
  char note[32];
} evm_web3_external_hdkey_t;

typedef struct {
  char address[EVM_ADDRESS_HEX_LEN + 1];
  uint8_t master_fingerprint[4];
  bool has_master_fingerprint;
  evm_web3_external_hdkey_t standard;
  evm_web3_external_hdkey_t ledger_live[EVM_WEB3_MAX_EXTERNAL_LEDGER_KEYS];
  size_t ledger_live_count;
  evm_web3_external_hdkey_t btc[EVM_WEB3_MAX_EXTERNAL_BTC_KEYS];
  size_t btc_count;
} evm_web3_external_account_t;

bool evm_get_address(char *address_out, size_t address_out_len);
void evm_keccak256(const uint8_t *input, size_t input_len, uint8_t out[32]);
bool evm_address_from_uncompressed_pubkey(const uint8_t pubkey[65],
                                          char *address_out,
                                          size_t address_out_len);
bool evm_web3_build_connect_qr(evm_web3_profile_t profile,
                               evm_web3_qr_bundle_t *bundle_out);
bool evm_web3_build_external_connect_qr(
    evm_web3_profile_t profile, const evm_web3_external_account_t *account,
    evm_web3_qr_bundle_t *bundle_out);
void evm_web3_qr_bundle_clear(evm_web3_qr_bundle_t *bundle);

#endif // EVM_H
