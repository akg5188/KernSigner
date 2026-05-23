#ifndef BTC_DERIVATION_H
#define BTC_DERIVATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
  BTC_DERIVATION_SCRIPT_UNKNOWN = 0,
  BTC_DERIVATION_SCRIPT_P2PKH,
  BTC_DERIVATION_SCRIPT_P2SH_P2WPKH,
  BTC_DERIVATION_SCRIPT_P2WPKH,
} btc_derivation_script_t;

bool btc_derivation_keypath_to_path(const unsigned char *keypath,
                                    size_t keypath_len, char *out,
                                    size_t out_len);

bool btc_derivation_satochip_sign_path(const unsigned char *keypath,
                                       size_t keypath_len,
                                       btc_derivation_script_t script,
                                       bool is_testnet, uint32_t account,
                                       char *out, size_t out_len);

#endif // BTC_DERIVATION_H
