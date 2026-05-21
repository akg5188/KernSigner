#ifndef CUSTOM_DERIVATION_H
#define CUSTOM_DERIVATION_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
  CUSTOM_ADDR_BTC_P2PKH = 0,
  CUSTOM_ADDR_BTC_P2SH_P2WPKH,
  CUSTOM_ADDR_BTC_P2WPKH,
  CUSTOM_ADDR_BTC_P2TR,
  CUSTOM_ADDR_EVM,
} custom_address_type_t;

bool custom_derivation_get_address(const char *path, custom_address_type_t type,
                                   bool is_testnet, char *address_out,
                                   size_t address_out_len);

#endif // CUSTOM_DERIVATION_H
