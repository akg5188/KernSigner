#include "custom_derivation.h"
#include "key.h"
#include "../utils/secure_mem.h"

#include <string.h>
#include <stdio.h>
#include <wally_address.h>
#include <wally_bip32.h>
#include <wally_core.h>
#include <wally_crypto.h>
#include <wally_script.h>

// Local Keccak-256 implementation reused for EVM address derivation.
static uint64_t rotl64(uint64_t x, unsigned s) {
  return (x << s) | (x >> (64U - s));
}

static uint64_t load64_le(const uint8_t *x) {
  uint64_t r = 0;
  for (int i = 0; i < 8; i++)
    r |= ((uint64_t)x[i]) << (8U * (unsigned)i);
  return r;
}

static void store64_le(uint8_t *x, uint64_t u) {
  for (int i = 0; i < 8; i++)
    x[i] = (uint8_t)(u >> (8U * (unsigned)i));
}

static void keccakf(uint64_t st[25]) {
  static const uint64_t rc[24] = {
      0x0000000000000001ULL, 0x0000000000008082ULL,
      0x800000000000808aULL, 0x8000000080008000ULL,
      0x000000000000808bULL, 0x0000000080000001ULL,
      0x8000000080008081ULL, 0x8000000000008009ULL,
      0x000000000000008aULL, 0x0000000000000088ULL,
      0x0000000080008009ULL, 0x000000008000000aULL,
      0x000000008000808bULL, 0x800000000000008bULL,
      0x8000000000008089ULL, 0x8000000000008003ULL,
      0x8000000000008002ULL, 0x8000000000000080ULL,
      0x000000000000800aULL, 0x800000008000000aULL,
      0x8000000080008081ULL, 0x8000000000008080ULL,
      0x0000000080000001ULL, 0x8000000080008008ULL};
  static const uint8_t r[25] = {
      0,  1,  62, 28, 27, 36, 44, 6,  55, 20, 3,  10, 43,
      25, 39, 41, 45, 15, 21, 8,  18, 2,  61, 56, 14};

  for (int round = 0; round < 24; round++) {
    uint64_t c[5], d[5], b[25];
    for (int x = 0; x < 5; x++)
      c[x] = st[x] ^ st[x + 5] ^ st[x + 10] ^ st[x + 15] ^ st[x + 20];
    for (int x = 0; x < 5; x++)
      d[x] = c[(x + 4) % 5] ^ rotl64(c[(x + 1) % 5], 1);
    for (int x = 0; x < 5; x++) {
      for (int y = 0; y < 5; y++)
        st[x + 5 * y] ^= d[x];
    }
    for (int x = 0; x < 5; x++) {
      for (int y = 0; y < 5; y++) {
        int src = x + 5 * y;
        int dst = y + 5 * ((2 * x + 3 * y) % 5);
        b[dst] = rotl64(st[src], r[src]);
      }
    }
    for (int x = 0; x < 5; x++) {
      for (int y = 0; y < 5; y++)
        st[x + 5 * y] =
            b[x + 5 * y] ^ ((~b[((x + 1) % 5) + 5 * y]) &
                            b[((x + 2) % 5) + 5 * y]);
    }
    st[0] ^= rc[round];
  }
}

static void keccak256(const uint8_t *input, size_t input_len,
                      uint8_t out[32]) {
  uint64_t st[25] = {0};
  uint8_t block[136];
  const size_t rate = sizeof(block);

  while (input_len >= rate) {
    for (size_t i = 0; i < rate / 8; i++)
      st[i] ^= load64_le(input + i * 8);
    keccakf(st);
    input += rate;
    input_len -= rate;
  }

  memset(block, 0, sizeof(block));
  if (input_len > 0)
    memcpy(block, input, input_len);
  block[input_len] = 0x01;
  block[rate - 1] |= 0x80;
  for (size_t i = 0; i < rate / 8; i++)
    st[i] ^= load64_le(block + i * 8);
  keccakf(st);

  for (size_t i = 0; i < 4; i++)
    store64_le(out + i * 8, st[i]);

  secure_memzero(block, sizeof(block));
  secure_memzero(st, sizeof(st));
}

static bool address_from_pubkey(const unsigned char *pub_key, size_t pub_key_len,
                                custom_address_type_t type, bool is_testnet,
                                char *address_out, size_t address_out_len) {
  if (!pub_key || !address_out || address_out_len == 0)
    return false;

  unsigned char script[WALLY_WITNESSSCRIPT_MAX_LEN] = {0};
  size_t script_len = 0;
  char *alloc = NULL;
  int ret = WALLY_EINVAL;

  switch (type) {
  case CUSTOM_ADDR_BTC_P2WPKH:
    ret = wally_witness_program_from_bytes(pub_key, pub_key_len,
                                           WALLY_SCRIPT_HASH160, script,
                                           sizeof(script), &script_len);
    if (ret != WALLY_OK)
      return false;
    ret = wally_addr_segwit_from_bytes(script, script_len,
                                       is_testnet ? "tb" : "bc", 0, &alloc);
    break;

  case CUSTOM_ADDR_BTC_P2PKH: {
    unsigned char pkh20[HASH160_LEN] = {0};
    ret = wally_hash160(pub_key, pub_key_len, pkh20, HASH160_LEN);
    if (ret != WALLY_OK)
      return false;
    script[0] = 0x76;
    script[1] = 0xa9;
    script[2] = 0x14;
    memcpy(script + 3, pkh20, 20);
    script[23] = 0x88;
    script[24] = 0xac;
    script_len = 25;
    ret = wally_scriptpubkey_to_address(
        script, script_len,
        is_testnet ? WALLY_NETWORK_BITCOIN_TESTNET
                   : WALLY_NETWORK_BITCOIN_MAINNET,
        &alloc);
    secure_memzero(pkh20, sizeof(pkh20));
    break;
  }

  case CUSTOM_ADDR_BTC_P2SH_P2WPKH: {
    unsigned char witness_prog[22] = {0};
    size_t witness_prog_len = 0;
    unsigned char sh20[HASH160_LEN] = {0};
    ret = wally_witness_program_from_bytes(pub_key, pub_key_len,
                                           WALLY_SCRIPT_HASH160, witness_prog,
                                           sizeof(witness_prog),
                                           &witness_prog_len);
    if (ret != WALLY_OK || witness_prog_len != sizeof(witness_prog))
      return false;
    ret = wally_hash160(witness_prog, witness_prog_len, sh20, HASH160_LEN);
    if (ret != WALLY_OK) {
      secure_memzero(witness_prog, sizeof(witness_prog));
      return false;
    }
    script[0] = 0xa9;
    script[1] = 0x14;
    memcpy(script + 2, sh20, 20);
    script[22] = 0x87;
    script_len = 23;
    ret = wally_scriptpubkey_to_address(
        script, script_len,
        is_testnet ? WALLY_NETWORK_BITCOIN_TESTNET
                   : WALLY_NETWORK_BITCOIN_MAINNET,
        &alloc);
    secure_memzero(witness_prog, sizeof(witness_prog));
    secure_memzero(sh20, sizeof(sh20));
    break;
  }

  case CUSTOM_ADDR_BTC_P2TR: {
    unsigned char tweaked_pk33[EC_PUBLIC_KEY_LEN] = {0};
    script[0] = 0x51;
    script[1] = 0x20;
    ret = wally_ec_public_key_bip341_tweak(pub_key, pub_key_len, NULL, 0, 0,
                                           tweaked_pk33, EC_PUBLIC_KEY_LEN);
    if (ret != WALLY_OK)
      return false;
    memcpy(script + 2, tweaked_pk33 + 1, 32);
    script_len = 34;
    ret = wally_addr_segwit_from_bytes(script, script_len,
                                       is_testnet ? "tb" : "bc", 0, &alloc);
    secure_memzero(tweaked_pk33, sizeof(tweaked_pk33));
    break;
  }

  case CUSTOM_ADDR_EVM:
  default:
    return false;
  }

  if (ret != WALLY_OK || !alloc)
    return false;

  size_t len = strlen(alloc);
  if (len + 1 > address_out_len) {
    wally_free_string(alloc);
    return false;
  }
  memcpy(address_out, alloc, len + 1);
  wally_free_string(alloc);
  secure_memzero(script, sizeof(script));
  return true;
}

static bool evm_address_from_key(struct ext_key *derived_key, char *address_out,
                                 size_t address_out_len) {
  if (!derived_key || !address_out || address_out_len < 43)
    return false;

  unsigned char pub_uncompressed[EC_PUBLIC_KEY_UNCOMPRESSED_LEN] = {0};
  unsigned char hash[32] = {0};
  char lower[41] = {0};
  static const char *hex = "0123456789abcdef";

  if (wally_ec_public_key_decompress(derived_key->pub_key,
                                     sizeof(derived_key->pub_key),
                                     pub_uncompressed,
                                     sizeof(pub_uncompressed)) != WALLY_OK) {
    return false;
  }

  keccak256(pub_uncompressed + 1, 64, hash);

  for (int i = 0; i < 20; i++) {
    unsigned char b = hash[12 + i];
    lower[i * 2] = hex[(b >> 4) & 0x0f];
    lower[i * 2 + 1] = hex[b & 0x0f];
  }
  lower[40] = '\0';

  unsigned char checksum_hash[32] = {0};
  keccak256((const unsigned char *)lower, 40, checksum_hash);

  for (int i = 0; i < 40; i++) {
    unsigned char nibble =
        (i & 1) ? (checksum_hash[i / 2] & 0x0f) : (checksum_hash[i / 2] >> 4);
    if (nibble >= 8 && lower[i] >= 'a' && lower[i] <= 'f')
      lower[i] = (char)(lower[i] - 32);
  }

  snprintf(address_out, address_out_len, "0x%s", lower);
  secure_memzero(pub_uncompressed, sizeof(pub_uncompressed));
  secure_memzero(hash, sizeof(hash));
  secure_memzero(checksum_hash, sizeof(checksum_hash));
  secure_memzero(lower, sizeof(lower));
  return true;
}

bool custom_derivation_get_address(const char *path, custom_address_type_t type,
                                   bool is_testnet, char *address_out,
                                   size_t address_out_len) {
  if (!path || !address_out)
    return false;

  struct ext_key *derived_key = NULL;
  if (!key_get_derived_key(path, &derived_key))
    return false;

  bool ok = false;
  if (type == CUSTOM_ADDR_EVM)
    ok = evm_address_from_key(derived_key, address_out, address_out_len);
  else
    ok = address_from_pubkey(derived_key->pub_key, sizeof(derived_key->pub_key),
                             type, is_testnet, address_out, address_out_len);

  bip32_key_free(derived_key);
  return ok;
}
