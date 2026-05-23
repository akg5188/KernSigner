#include "evm.h"
#include "key.h"
#include "crypto_utils.h"
#include "../utils/secure_mem.h"
#include "i18n/i18n.h"
#include "../../components/cUR/src/types/cbor_data.h"
#include "../../components/cUR/src/types/cbor_encoder.h"
#include "../../components/cUR/src/ur_encoder.h"

#include <ctype.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <wally_bip32.h>
#include <wally_core.h>
#include <wally_crypto.h>

#define KECCAK_ROUNDS 24
#define WEB3_ETH_COIN_TYPE 60
#define WEB3_BTC_COIN_TYPE 0
#define WEB3_MAINNET_NETWORK 0
#define WEB3_OKX_FRAGMENT_LEN 200
#define WEB3_OKX_DEVICE_VERSION "2.4.2"
#define CRYPTO_HDKEY_TAG 303
#define CRYPTO_KEYPATH_TAG 304
#define CRYPTO_COIN_INFO_TAG 305
#define WEB3_KEYPATH_DEPTH_NONE -1
#define WEB3_KEYPATH_DEPTH_AUTO -2

typedef struct {
  uint32_t index;
  bool hardened;
  bool wildcard;
} web3_path_component_t;

typedef struct {
  struct ext_key *key;
  const uint8_t *external_pubkey;
  const uint8_t *external_chain_code;
  char path[48];
  char children_path[12];
  uint8_t parent_fingerprint[4];
  bool has_parent_fingerprint;
  uint32_t coin_type;
  bool has_coin_type;
  bool include_chain_code;
  bool include_children;
  int origin_depth;
  int children_depth;
  const char *note;
} web3_hdkey_entry_t;

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
  static const uint64_t rc[KECCAK_ROUNDS] = {
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

  for (int round = 0; round < KECCAK_ROUNDS; round++) {
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

void evm_keccak256(const uint8_t *input, size_t input_len, uint8_t out[32]) {
  if (!out)
    return;
  if (!input && input_len > 0) {
    memset(out, 0, 32);
    return;
  }
  keccak256(input, input_len, out);
}

static char hex_digit(uint8_t v) {
  static const char *hex = "0123456789abcdef";
  return hex[v & 0x0f];
}

static void checksum_address(char lower[41], const uint8_t hash[32]) {
  for (int i = 0; i < 40; i++) {
    uint8_t nibble = (i & 1) ? (hash[i / 2] & 0x0f) : (hash[i / 2] >> 4);
    if (nibble >= 8)
      lower[i] = (char)toupper((unsigned char)lower[i]);
  }
}

static bool web3_parse_path_components(const char *path,
                                       web3_path_component_t *components,
                                       size_t max_components,
                                       size_t *count_out) {
  if (!path || !components || !count_out)
    return false;

  const char *p = path;
  if (p[0] == 'm') {
    p++;
    if (p[0] == '/')
      p++;
    else if (p[0] != '\0')
      return false;
  }

  size_t count = 0;
  while (*p) {
    if (count >= max_components)
      return false;

    if (*p == '*') {
      components[count].index = 0;
      components[count].hardened = false;
      components[count].wildcard = true;
      p++;
    } else {
      uint32_t value = 0;
      bool has_digits = false;
      while (*p >= '0' && *p <= '9') {
        value = value * 10U + (uint32_t)(*p - '0');
        has_digits = true;
        p++;
      }
      if (!has_digits)
        return false;
      components[count].index = value;
      components[count].hardened = false;
      components[count].wildcard = false;
    }

    if (*p == '\'' || *p == 'h') {
      if (components[count].wildcard)
        return false;
      components[count].hardened = true;
      p++;
    }

    count++;
    if (*p == '/') {
      p++;
      if (*p == '\0')
        return false;
    } else if (*p != '\0') {
      return false;
    }
  }

  *count_out = count;
  return true;
}

static cbor_value_t *web3_keypath_value(const char *path,
                                        const uint8_t *source_fingerprint,
                                        int depth) {
  web3_path_component_t components[10];
  size_t count = 0;
  if (!web3_parse_path_components(path, components, 10, &count))
    return NULL;

  cbor_value_t *map = cbor_value_new_map();
  cbor_value_t *array = cbor_value_new_array();
  if (!map || !array) {
    cbor_value_free(map);
    cbor_value_free(array);
    return NULL;
  }

  for (size_t i = 0; i < count; i++) {
    cbor_value_t *index = components[i].wildcard
                              ? cbor_value_new_array()
                              : cbor_value_new_unsigned_int(components[i].index);
    cbor_value_t *hardened = cbor_value_new_bool(components[i].hardened);
    if (!index || !hardened) {
      cbor_value_free(index);
      cbor_value_free(hardened);
      cbor_value_free(map);
      cbor_value_free(array);
      return NULL;
    }
    if (!cbor_array_append(array, index)) {
      cbor_value_free(index);
      cbor_value_free(hardened);
      cbor_value_free(map);
      cbor_value_free(array);
      return NULL;
    }
    index = NULL;
    if (!cbor_array_append(array, hardened)) {
      cbor_value_free(index);
      cbor_value_free(hardened);
      cbor_value_free(map);
      cbor_value_free(array);
      return NULL;
    }
    hardened = NULL;
  }

  if (!cbor_map_set(map, cbor_value_new_unsigned_int(1), array)) {
    cbor_value_free(map);
    cbor_value_free(array);
    return NULL;
  }
  array = NULL;

  if (source_fingerprint) {
    uint32_t fp = ((uint32_t)source_fingerprint[0] << 24) |
                  ((uint32_t)source_fingerprint[1] << 16) |
                  ((uint32_t)source_fingerprint[2] << 8) |
                  (uint32_t)source_fingerprint[3];
    if (!cbor_map_set(map, cbor_value_new_unsigned_int(2),
                      cbor_value_new_unsigned_int(fp))) {
      cbor_value_free(map);
      return NULL;
    }
  }

  if (depth == WEB3_KEYPATH_DEPTH_AUTO)
    depth = (int)count;
  if (depth >= 0 &&
      !cbor_map_set(map, cbor_value_new_unsigned_int(3),
                    cbor_value_new_unsigned_int((uint64_t)depth))) {
    cbor_value_free(map);
    return NULL;
  }

  return map;
}

static cbor_value_t *web3_coin_info_value(uint32_t coin_type) {
  cbor_value_t *map = cbor_value_new_map();
  if (!map)
    return NULL;

  if (!cbor_map_set(map, cbor_value_new_unsigned_int(1),
                    cbor_value_new_unsigned_int(coin_type)) ||
      !cbor_map_set(map, cbor_value_new_unsigned_int(2),
                    cbor_value_new_unsigned_int(WEB3_MAINNET_NETWORK))) {
    cbor_value_free(map);
    return NULL;
  }
  return map;
}

static bool web3_pubkey_fingerprint(const uint8_t pubkey[EC_PUBLIC_KEY_LEN],
                                    uint8_t fp_out[4]) {
  uint8_t hash[HASH160_LEN];
  if (wally_hash160(pubkey, EC_PUBLIC_KEY_LEN, hash, sizeof(hash)) != WALLY_OK)
    return false;
  memcpy(fp_out, hash, 4);
  secure_memzero(hash, sizeof(hash));
  return true;
}

static cbor_value_t *web3_hdkey_value(const web3_hdkey_entry_t *entry,
                                      const uint8_t master_fingerprint[4]) {
  if (!entry || (!entry->key && !entry->external_pubkey))
    return NULL;

  const uint8_t *pubkey =
      entry->external_pubkey ? entry->external_pubkey : entry->key->pub_key;
  const uint8_t *chain_code =
      entry->external_chain_code ? entry->external_chain_code
                                 : entry->key->chain_code;

  cbor_value_t *map = cbor_value_new_map();
  if (!map)
    return NULL;

  if (!cbor_map_set(map, cbor_value_new_unsigned_int(2),
                    cbor_value_new_bool(false)) ||
      !cbor_map_set(map, cbor_value_new_unsigned_int(3),
                    cbor_value_new_bytes(pubkey, EC_PUBLIC_KEY_LEN))) {
    cbor_value_free(map);
    return NULL;
  }

  if (entry->include_chain_code &&
      !cbor_map_set(map, cbor_value_new_unsigned_int(4),
                    cbor_value_new_bytes(chain_code, 32))) {
    cbor_value_free(map);
    return NULL;
  }

  if (entry->has_coin_type) {
    cbor_value_t *coin_info = web3_coin_info_value(entry->coin_type);
    cbor_value_t *tagged_coin_info =
        coin_info ? cbor_value_new_tag(CRYPTO_COIN_INFO_TAG, coin_info) : NULL;
    if (!tagged_coin_info ||
        !cbor_map_set(map, cbor_value_new_unsigned_int(5),
                      tagged_coin_info)) {
      if (tagged_coin_info)
        cbor_value_free(tagged_coin_info);
      else
        cbor_value_free(coin_info);
      cbor_value_free(map);
      return NULL;
    }
    coin_info = NULL;
    tagged_coin_info = NULL;
  }

  cbor_value_t *origin = web3_keypath_value(entry->path, master_fingerprint,
                                           entry->origin_depth);
  cbor_value_t *tagged_origin =
      origin ? cbor_value_new_tag(CRYPTO_KEYPATH_TAG, origin) : NULL;
  if (!tagged_origin ||
      !cbor_map_set(map, cbor_value_new_unsigned_int(6), tagged_origin)) {
    if (tagged_origin)
      cbor_value_free(tagged_origin);
    else
      cbor_value_free(origin);
    cbor_value_free(map);
    return NULL;
  }
  origin = NULL;
  tagged_origin = NULL;

  if (entry->include_children && entry->children_path[0]) {
    cbor_value_t *children =
        web3_keypath_value(entry->children_path, NULL, entry->children_depth);
    cbor_value_t *tagged_children =
        children ? cbor_value_new_tag(CRYPTO_KEYPATH_TAG, children) : NULL;
    if (!tagged_children ||
        !cbor_map_set(map, cbor_value_new_unsigned_int(7), tagged_children)) {
      if (tagged_children)
        cbor_value_free(tagged_children);
      else
        cbor_value_free(children);
      cbor_value_free(map);
      return NULL;
    }
    children = NULL;
    tagged_children = NULL;
  }

  if (entry->has_parent_fingerprint) {
    uint32_t fp = ((uint32_t)entry->parent_fingerprint[0] << 24) |
                  ((uint32_t)entry->parent_fingerprint[1] << 16) |
                  ((uint32_t)entry->parent_fingerprint[2] << 8) |
                  (uint32_t)entry->parent_fingerprint[3];
    if (!cbor_map_set(map, cbor_value_new_unsigned_int(8),
                      cbor_value_new_unsigned_int(fp))) {
      cbor_value_free(map);
      return NULL;
    }
  }

  if (!cbor_map_set(map, cbor_value_new_unsigned_int(9),
                    cbor_value_new_string("Keystone"))) {
    cbor_value_free(map);
    return NULL;
  }

  if (entry->note && entry->note[0] &&
      !cbor_map_set(map, cbor_value_new_unsigned_int(10),
                    cbor_value_new_string(entry->note))) {
    cbor_value_free(map);
    return NULL;
  }

  return map;
}

static bool web3_uppercase_in_place(char *text) {
  if (!text)
    return false;
  for (char *p = text; *p; p++)
    *p = (char)toupper((unsigned char)*p);
  return true;
}

static bool web3_encode_single_ur(const char *type, cbor_value_t *value,
                                  char **page_out) {
  if (!type || !value || !page_out)
    return false;

  size_t cbor_len = 0;
  uint8_t *cbor = cbor_encode(value, &cbor_len);
  if (!cbor)
    return false;

  char *page = NULL;
  bool ok = ur_encoder_encode_single(type, cbor, cbor_len, &page);
  free(cbor);
  if (!ok || !page)
    return false;
  web3_uppercase_in_place(page);
  *page_out = page;
  return true;
}

static bool web3_encode_animated_ur(const char *type, cbor_value_t *value,
                                    evm_web3_qr_bundle_t *bundle) {
  if (!type || !value || !bundle)
    return false;

  size_t cbor_len = 0;
  uint8_t *cbor = cbor_encode(value, &cbor_len);
  if (!cbor)
    return false;

  ur_encoder_t *encoder =
      ur_encoder_new(type, cbor, cbor_len, WEB3_OKX_FRAGMENT_LEN, 0, 10);
  free(cbor);
  if (!encoder)
    return false;

  size_t seq_len = ur_encoder_seq_len(encoder);
  if (seq_len == 0 || seq_len > EVM_WEB3_MAX_QR_PAGES) {
    ur_encoder_free(encoder);
    return false;
  }

  for (size_t i = 0; i < seq_len; i++) {
    char *page = NULL;
    if (!ur_encoder_next_part(encoder, &page) || !page) {
      ur_encoder_free(encoder);
      return false;
    }
    web3_uppercase_in_place(page);
    bundle->pages[bundle->page_count++] = page;
  }

  bundle->animated = seq_len > 1;
  ur_encoder_free(encoder);
  return true;
}

static bool web3_encode_single_ur_bundle(const char *type, cbor_value_t *value,
                                         evm_web3_qr_bundle_t *bundle) {
  if (!bundle || bundle->page_count >= EVM_WEB3_MAX_QR_PAGES)
    return false;

  char *page = NULL;
  if (!web3_encode_single_ur(type, value, &page) || !page)
    return false;

  bundle->pages[bundle->page_count++] = page;
  bundle->animated = false;
  return true;
}

static bool web3_derive_entry(const char *path, const char *children_path,
                              bool include_chain_code, bool include_children,
                              bool include_parent_fingerprint,
                              uint32_t coin_type, bool has_coin_type,
                              int origin_depth, int children_depth,
                              const char *note,
                              web3_hdkey_entry_t *entry_out) {
  if (!path || !entry_out)
    return false;

  memset(entry_out, 0, sizeof(*entry_out));
  entry_out->include_chain_code = include_chain_code;
  entry_out->include_children = include_children;
  entry_out->coin_type = coin_type;
  entry_out->has_coin_type = has_coin_type;
  entry_out->origin_depth = origin_depth;
  entry_out->children_depth = children_depth;
  entry_out->note = note;
  snprintf(entry_out->path, sizeof(entry_out->path), "%s", path);
  snprintf(entry_out->children_path, sizeof(entry_out->children_path), "%s",
           children_path ? children_path : "");

  if (!key_get_derived_key(path, &entry_out->key) || !entry_out->key)
    return false;

  if (include_parent_fingerprint) {
    char parent_path[48];
    snprintf(parent_path, sizeof(parent_path), "%s", path);
    char *slash = strrchr(parent_path, '/');
    if (!slash || slash == parent_path) {
      bip32_key_free(entry_out->key);
      entry_out->key = NULL;
      return false;
    }
    *slash = '\0';
    struct ext_key *parent_key = NULL;
    if (!key_get_derived_key(parent_path, &parent_key) || !parent_key) {
      bip32_key_free(entry_out->key);
      entry_out->key = NULL;
      return false;
    }
    entry_out->has_parent_fingerprint =
        web3_pubkey_fingerprint(parent_key->pub_key,
                                entry_out->parent_fingerprint);
    bip32_key_free(parent_key);
    if (!entry_out->has_parent_fingerprint) {
      bip32_key_free(entry_out->key);
      entry_out->key = NULL;
      return false;
    }
  }

  return true;
}

static void web3_entry_clear(web3_hdkey_entry_t *entry) {
  if (entry && entry->key) {
    bip32_key_free(entry->key);
    entry->key = NULL;
  }
}

static bool web3_external_entry_from_account(
    const evm_web3_external_hdkey_t *source, web3_hdkey_entry_t *entry_out) {
  if (!source || !entry_out || source->path[0] == '\0')
    return false;

  memset(entry_out, 0, sizeof(*entry_out));
  entry_out->external_pubkey = source->pubkey;
  entry_out->external_chain_code =
      source->include_chain_code ? source->chain_code : NULL;
  entry_out->include_chain_code = source->include_chain_code;
  entry_out->include_children = source->include_children;
  entry_out->coin_type = source->coin_type;
  entry_out->has_coin_type = source->has_coin_type;
  entry_out->origin_depth = source->origin_depth;
  entry_out->children_depth = source->children_depth;
  entry_out->has_parent_fingerprint = source->has_parent_fingerprint;
  if (source->has_parent_fingerprint)
    memcpy(entry_out->parent_fingerprint, source->parent_fingerprint,
           sizeof(entry_out->parent_fingerprint));
  snprintf(entry_out->path, sizeof(entry_out->path), "%s", source->path);
  snprintf(entry_out->children_path, sizeof(entry_out->children_path), "%s",
           source->children_path);
  entry_out->note = source->note[0] ? source->note : NULL;
  return true;
}

static cbor_value_t *web3_tagged_hdkey(const web3_hdkey_entry_t *entry,
                                       const uint8_t master_fingerprint[4]) {
  cbor_value_t *hdkey = web3_hdkey_value(entry, master_fingerprint);
  if (!hdkey)
    return NULL;
  cbor_value_t *tagged = cbor_value_new_tag(CRYPTO_HDKEY_TAG, hdkey);
  if (!tagged)
    cbor_value_free(hdkey);
  return tagged;
}

static bool web3_build_hdkey_page(evm_web3_qr_bundle_t *bundle,
                                  const uint8_t master_fingerprint[4]) {
  web3_hdkey_entry_t standard;
  if (!web3_derive_entry("m/44'/60'/0'", "0/*", true, true, true,
                         WEB3_ETH_COIN_TYPE, true, WEB3_KEYPATH_DEPTH_AUTO, 0,
                         "account.standard", &standard))
    return false;

  cbor_value_t *hdkey = web3_hdkey_value(&standard, master_fingerprint);
  web3_entry_clear(&standard);
  if (!hdkey)
    return false;

  bool ok = web3_encode_single_ur("crypto-hdkey", hdkey, &bundle->pages[0]);
  cbor_value_free(hdkey);
  if (!ok)
    return false;

  bundle->page_count = 1;
  bundle->animated = false;
  return true;
}

static bool web3_append_bitget_entries(cbor_value_t *array,
                                       const uint8_t master_fingerprint[4]) {
  web3_hdkey_entry_t standard;
  web3_hdkey_entry_t btc84;
  memset(&standard, 0, sizeof(standard));
  memset(&btc84, 0, sizeof(btc84));

  bool ok = web3_derive_entry("m/44'/60'/0'", "0/*", true, true, true,
                              WEB3_ETH_COIN_TYPE, true,
                              WEB3_KEYPATH_DEPTH_AUTO, 0,
                              "account.standard",
                              &standard) &&
            web3_derive_entry("m/84'/0'/0'", NULL, true, false, true,
                              WEB3_BTC_COIN_TYPE, true,
                              WEB3_KEYPATH_DEPTH_AUTO,
                              WEB3_KEYPATH_DEPTH_NONE, "", &btc84);
  if (!ok)
    goto out;

  cbor_value_t *tagged_standard =
      web3_tagged_hdkey(&standard, master_fingerprint);
  cbor_value_t *tagged_btc = web3_tagged_hdkey(&btc84, master_fingerprint);
  if (!tagged_standard || !tagged_btc) {
    cbor_value_free(tagged_standard);
    cbor_value_free(tagged_btc);
    ok = false;
    goto out;
  }
  if (!cbor_array_append(array, tagged_standard)) {
    cbor_value_free(tagged_standard);
    cbor_value_free(tagged_btc);
    ok = false;
    goto out;
  }
  tagged_standard = NULL;
  if (!cbor_array_append(array, tagged_btc)) {
    cbor_value_free(tagged_standard);
    cbor_value_free(tagged_btc);
    ok = false;
    goto out;
  }
  tagged_btc = NULL;
  ok = true;

out:
  web3_entry_clear(&standard);
  web3_entry_clear(&btc84);
  return ok;
}

static bool web3_append_external_bitget_entries(
    cbor_value_t *array, const evm_web3_external_account_t *account,
    const uint8_t master_fingerprint[4]) {
  web3_hdkey_entry_t standard;
  if (!web3_external_entry_from_account(&account->standard, &standard))
    return false;
  standard.origin_depth = WEB3_KEYPATH_DEPTH_AUTO;
  standard.children_depth = 0;

  cbor_value_t *tagged_standard =
      web3_tagged_hdkey(&standard, master_fingerprint);
  if (!tagged_standard || !cbor_array_append(array, tagged_standard)) {
    cbor_value_free(tagged_standard);
    return false;
  }
  tagged_standard = NULL;

  for (size_t i = 0; i < account->btc_count &&
                     i < EVM_WEB3_MAX_EXTERNAL_BTC_KEYS;
       i++) {
    if (strcmp(account->btc[i].path, "m/84'/0'/0'") != 0)
      continue;
    web3_hdkey_entry_t btc;
    if (!web3_external_entry_from_account(&account->btc[i], &btc))
      continue;
    cbor_value_t *tagged_btc = web3_tagged_hdkey(&btc, master_fingerprint);
    if (!tagged_btc || !cbor_array_append(array, tagged_btc)) {
      cbor_value_free(tagged_btc);
      return false;
    }
    tagged_btc = NULL;
  }
  return true;
}

static bool web3_append_derived_entry(cbor_value_t *array, const char *path,
                                      const char *children_path,
                                      bool include_chain_code,
                                      bool include_children,
                                      bool include_parent_fingerprint,
                                      uint32_t coin_type, bool has_coin_type,
                                      int origin_depth, int children_depth,
                                      const char *note,
                                      const uint8_t master_fingerprint[4]) {
  web3_hdkey_entry_t entry;
  if (!web3_derive_entry(path, children_path, include_chain_code,
                         include_children, include_parent_fingerprint,
                         coin_type, has_coin_type, origin_depth,
                         children_depth, note, &entry))
    return false;

  cbor_value_t *tagged = web3_tagged_hdkey(&entry, master_fingerprint);
  web3_entry_clear(&entry);
  if (!tagged || !cbor_array_append(array, tagged)) {
    cbor_value_free(tagged);
    return false;
  }
  return true;
}

static bool web3_append_okx_entries(cbor_value_t *array,
                                    const uint8_t master_fingerprint[4]) {
  if (!web3_append_derived_entry(array, "m/44'/60'/0'", NULL, true, false,
                                 true, WEB3_ETH_COIN_TYPE, true,
                                 WEB3_KEYPATH_DEPTH_AUTO,
                                 WEB3_KEYPATH_DEPTH_NONE,
                                 "account.standard", master_fingerprint))
    return false;

  for (int i = 0; i < 10; i++) {
    char path[48];
    snprintf(path, sizeof(path), "m/44'/60'/%d'/0/0", i);
    if (!web3_append_derived_entry(array, path, NULL, false, false, false, 0,
                                   false, WEB3_KEYPATH_DEPTH_NONE,
                                   WEB3_KEYPATH_DEPTH_NONE,
                                   "account.ledger_live",
                                   master_fingerprint))
      return false;
  }

  const struct {
    const char *path;
    uint32_t coin_type;
    bool has_coin_type;
  } okx_paths[] = {
      {"m/84'/0'/0'", WEB3_BTC_COIN_TYPE, true},
      {"m/49'/0'/0'", WEB3_BTC_COIN_TYPE, true},
      {"m/44'/0'/0'", WEB3_BTC_COIN_TYPE, true},
      {"m/44'/195'/0'", 195, true},
      {"m/49'/2'/0'", 2, true},
      {"m/44'/5'/0'", 5, true},
      {"m/44'/145'/0'", 145, true},
      {"m/86'/0'/0'", WEB3_BTC_COIN_TYPE, true},
  };
  for (size_t i = 0; i < sizeof(okx_paths) / sizeof(okx_paths[0]); i++) {
    if (!web3_append_derived_entry(array, okx_paths[i].path, NULL, true,
                                   false, true, okx_paths[i].coin_type,
                                   okx_paths[i].has_coin_type,
                                   WEB3_KEYPATH_DEPTH_AUTO,
                                   WEB3_KEYPATH_DEPTH_NONE, "",
                                   master_fingerprint))
      return false;
  }

  return true;
}

static bool web3_append_external_okx_entries(
    cbor_value_t *array, const evm_web3_external_account_t *account,
    const uint8_t master_fingerprint[4]) {
  web3_hdkey_entry_t standard;
  if (!web3_external_entry_from_account(&account->standard, &standard))
    return false;

  cbor_value_t *tagged_standard =
      web3_tagged_hdkey(&standard, master_fingerprint);
  if (!tagged_standard || !cbor_array_append(array, tagged_standard)) {
    cbor_value_free(tagged_standard);
    return false;
  }
  tagged_standard = NULL;

  for (size_t i = 0; i < account->ledger_live_count &&
                     i < EVM_WEB3_MAX_EXTERNAL_LEDGER_KEYS;
       i++) {
    web3_hdkey_entry_t ledger;
    if (!web3_external_entry_from_account(&account->ledger_live[i], &ledger))
      continue;
    cbor_value_t *tagged_ledger =
        web3_tagged_hdkey(&ledger, master_fingerprint);
    if (!tagged_ledger || !cbor_array_append(array, tagged_ledger)) {
      cbor_value_free(tagged_ledger);
      return false;
    }
    tagged_ledger = NULL;
  }

  for (size_t i = 0; i < account->btc_count &&
                     i < EVM_WEB3_MAX_EXTERNAL_BTC_KEYS;
       i++) {
    web3_hdkey_entry_t btc;
    if (!web3_external_entry_from_account(&account->btc[i], &btc))
      continue;
    cbor_value_t *tagged_btc = web3_tagged_hdkey(&btc, master_fingerprint);
    if (!tagged_btc || !cbor_array_append(array, tagged_btc)) {
      cbor_value_free(tagged_btc);
      return false;
    }
    tagged_btc = NULL;
  }

  return true;
}

static bool web3_device_id(char *out, size_t out_len,
                           const uint8_t master_fingerprint[4],
                           const char *address) {
  if (!out || out_len < 41 || !address)
    return false;

  char fp_hex[9];
  snprintf(fp_hex, sizeof(fp_hex), "%02x%02x%02x%02x",
           master_fingerprint[0], master_fingerprint[1],
           master_fingerprint[2], master_fingerprint[3]);

  char serial[128];
  snprintf(serial, sizeof(serial), "keystonetp-keystone-%s-%s", fp_hex,
           address);
  uint8_t hash1[32];
  uint8_t hash2[32];
  if (crypto_sha256((const uint8_t *)serial, strlen(serial), hash1) != 0 ||
      crypto_sha256(hash1, sizeof(hash1), hash2) != 0) {
    secure_memzero(hash1, sizeof(hash1));
    secure_memzero(hash2, sizeof(hash2));
    return false;
  }

  for (int i = 0; i < 20; i++)
    snprintf(out + i * 2, out_len - (size_t)i * 2, "%02x", hash2[i]);
  out[40] = '\0';
  secure_memzero(hash1, sizeof(hash1));
  secure_memzero(hash2, sizeof(hash2));
  secure_memzero(serial, sizeof(serial));
  return true;
}

static bool web3_build_multi_accounts(evm_web3_profile_t profile,
                                      evm_web3_qr_bundle_t *bundle,
                                      const uint8_t master_fingerprint[4]) {
  cbor_value_t *map = cbor_value_new_map();
  cbor_value_t *array = cbor_value_new_array();
  if (!map || !array) {
    cbor_value_free(map);
    cbor_value_free(array);
    return false;
  }

  bool ok = false;
  uint32_t fp = ((uint32_t)master_fingerprint[0] << 24) |
                ((uint32_t)master_fingerprint[1] << 16) |
                ((uint32_t)master_fingerprint[2] << 8) |
                (uint32_t)master_fingerprint[3];

  if (profile == EVM_WEB3_PROFILE_BITGET)
    ok = web3_append_bitget_entries(array, master_fingerprint);
  else
    ok = web3_append_okx_entries(array, master_fingerprint);
  if (!ok)
    goto out;

  if (!cbor_map_set(map, cbor_value_new_unsigned_int(1),
                    cbor_value_new_unsigned_int(fp)) ||
      !cbor_map_set(map, cbor_value_new_unsigned_int(2), array)) {
    goto out;
  }
  array = NULL;

  if (profile == EVM_WEB3_PROFILE_BITGET) {
    if (!cbor_map_set(map, cbor_value_new_unsigned_int(3),
                      cbor_value_new_string("Keystone"))) {
      goto out;
    }
    ok = web3_encode_single_ur_bundle("crypto-multi-accounts", map, bundle);
    goto out;
  }

  char device_id[41];
  if (!web3_device_id(device_id, sizeof(device_id), master_fingerprint,
                      bundle->address))
    goto out;

  if (!cbor_map_set(map, cbor_value_new_unsigned_int(3),
                    cbor_value_new_string("Keystone 3 Pro")) ||
      !cbor_map_set(map, cbor_value_new_unsigned_int(4),
                    cbor_value_new_string(device_id)) ||
      !cbor_map_set(map, cbor_value_new_unsigned_int(5),
                    cbor_value_new_string(WEB3_OKX_DEVICE_VERSION))) {
    goto out;
  }
  ok = web3_encode_animated_ur("crypto-multi-accounts", map, bundle);

out:
  cbor_value_free(array);
  cbor_value_free(map);
  return ok;
}

static bool web3_build_external_multi_accounts(
    evm_web3_profile_t profile, const evm_web3_external_account_t *account,
    evm_web3_qr_bundle_t *bundle, const uint8_t master_fingerprint[4]) {
  if (!account || !bundle)
    return false;

  cbor_value_t *map = cbor_value_new_map();
  cbor_value_t *array = cbor_value_new_array();
  if (!map || !array) {
    cbor_value_free(map);
    cbor_value_free(array);
    return false;
  }

  bool ok = false;
  uint32_t fp = ((uint32_t)master_fingerprint[0] << 24) |
                ((uint32_t)master_fingerprint[1] << 16) |
                ((uint32_t)master_fingerprint[2] << 8) |
                (uint32_t)master_fingerprint[3];

  if (profile == EVM_WEB3_PROFILE_BITGET)
    ok = web3_append_external_bitget_entries(array, account, master_fingerprint);
  else
    ok = web3_append_external_okx_entries(array, account, master_fingerprint);
  if (!ok)
    goto out;

  if (!cbor_map_set(map, cbor_value_new_unsigned_int(1),
                    cbor_value_new_unsigned_int(fp)) ||
      !cbor_map_set(map, cbor_value_new_unsigned_int(2), array)) {
    goto out;
  }
  array = NULL;

  if (profile == EVM_WEB3_PROFILE_BITGET) {
    if (!cbor_map_set(map, cbor_value_new_unsigned_int(3),
                      cbor_value_new_string("Keystone"))) {
      goto out;
    }
    ok = web3_encode_single_ur_bundle("crypto-multi-accounts", map, bundle);
    goto out;
  }

  char device_id[41];
  if (!web3_device_id(device_id, sizeof(device_id), master_fingerprint,
                      bundle->address))
    goto out;

  if (!cbor_map_set(map, cbor_value_new_unsigned_int(3),
                    cbor_value_new_string("Keystone 3 Pro")) ||
      !cbor_map_set(map, cbor_value_new_unsigned_int(4),
                    cbor_value_new_string(device_id)) ||
      !cbor_map_set(map, cbor_value_new_unsigned_int(5),
                    cbor_value_new_string(WEB3_OKX_DEVICE_VERSION))) {
    goto out;
  }
  ok = web3_encode_animated_ur("crypto-multi-accounts", map, bundle);

out:
  cbor_value_free(array);
  cbor_value_free(map);
  return ok;
}

bool evm_get_address(char *address_out, size_t address_out_len) {
  if (!address_out || address_out_len < EVM_ADDRESS_HEX_LEN + 1)
    return false;

  struct ext_key *addr_key = NULL;
  if (!key_get_derived_key("m/44'/60'/0'/0/0", &addr_key))
    return false;

  uint8_t pub_uncompressed[EC_PUBLIC_KEY_UNCOMPRESSED_LEN];
  if (wally_ec_public_key_decompress(addr_key->pub_key, sizeof(addr_key->pub_key),
                                     pub_uncompressed,
                                     sizeof(pub_uncompressed)) != WALLY_OK) {
    bip32_key_free(addr_key);
    return false;
  }
  bip32_key_free(addr_key);

  uint8_t hash[32];
  keccak256(pub_uncompressed + 1, 64, hash);

  char lower[41];
  for (int i = 0; i < 20; i++) {
    uint8_t b = hash[12 + i];
    lower[i * 2] = hex_digit((uint8_t)(b >> 4));
    lower[i * 2 + 1] = hex_digit(b);
  }
  lower[40] = '\0';

  uint8_t checksum_hash[32];
  keccak256((const uint8_t *)lower, 40, checksum_hash);
  checksum_address(lower, checksum_hash);

  snprintf(address_out, address_out_len, "0x%s", lower);
  secure_memzero(pub_uncompressed, sizeof(pub_uncompressed));
  secure_memzero(hash, sizeof(hash));
  secure_memzero(checksum_hash, sizeof(checksum_hash));
  secure_memzero(lower, sizeof(lower));
  return true;
}

bool evm_address_from_uncompressed_pubkey(const uint8_t pubkey[65],
                                          char *address_out,
                                          size_t address_out_len) {
  if (!pubkey || pubkey[0] != 0x04 || !address_out ||
      address_out_len < EVM_ADDRESS_HEX_LEN + 1) {
    return false;
  }

  uint8_t hash[32];
  keccak256(pubkey + 1, 64, hash);

  char lower[41];
  for (int i = 0; i < 20; i++) {
    uint8_t b = hash[12 + i];
    lower[i * 2] = hex_digit((uint8_t)(b >> 4));
    lower[i * 2 + 1] = hex_digit(b);
  }
  lower[40] = '\0';

  uint8_t checksum_hash[32];
  keccak256((const uint8_t *)lower, 40, checksum_hash);
  checksum_address(lower, checksum_hash);

  snprintf(address_out, address_out_len, "0x%s", lower);
  secure_memzero(hash, sizeof(hash));
  secure_memzero(checksum_hash, sizeof(checksum_hash));
  secure_memzero(lower, sizeof(lower));
  return true;
}

bool evm_web3_build_connect_qr(evm_web3_profile_t profile,
                               evm_web3_qr_bundle_t *bundle_out) {
  if (!bundle_out)
    return false;

  evm_web3_qr_bundle_clear(bundle_out);
  if (!evm_get_address(bundle_out->address, sizeof(bundle_out->address)))
    return false;

  if (profile == EVM_WEB3_PROFILE_ADDRESS) {
    bundle_out->pages[0] = strdup(bundle_out->address);
    if (!bundle_out->pages[0])
      return false;
    bundle_out->page_count = 1;
    bundle_out->animated = false;
    snprintf(bundle_out->summary, sizeof(bundle_out->summary), "%s",
             i18n_tr_or("evm.qr.evm_address", "EVM address QR"));
    return true;
  }

  uint8_t master_fingerprint[4];
  if (!key_get_fingerprint(master_fingerprint))
    return false;

  bool ok = false;
  switch (profile) {
  case EVM_WEB3_PROFILE_OKX:
    ok = web3_build_multi_accounts(profile, bundle_out, master_fingerprint);
    snprintf(bundle_out->summary, sizeof(bundle_out->summary),
             i18n_tr_or("evm.qr.okx_connect_format",
                        "OKX animated connection QR / %u parts"),
             (unsigned)bundle_out->page_count);
    break;
  case EVM_WEB3_PROFILE_BITGET:
    ok = web3_build_multi_accounts(profile, bundle_out, master_fingerprint);
    snprintf(bundle_out->summary, sizeof(bundle_out->summary),
             i18n_tr_or("evm.qr.bitget_connect_format",
                        "Bitget static connection QR / %u parts"),
             (unsigned)bundle_out->page_count);
    break;
  case EVM_WEB3_PROFILE_METAMASK:
    ok = web3_build_hdkey_page(bundle_out, master_fingerprint);
    snprintf(bundle_out->summary, sizeof(bundle_out->summary), "%s",
             i18n_tr_or("evm.qr.metamask_account_connect",
                        "MetaMask account connection QR"));
    break;
  case EVM_WEB3_PROFILE_RABBY:
    ok = web3_build_hdkey_page(bundle_out, master_fingerprint);
    snprintf(bundle_out->summary, sizeof(bundle_out->summary), "%s",
             i18n_tr_or("evm.qr.rabby_static_connect",
                        "Rabby static connection QR"));
    break;
  case EVM_WEB3_PROFILE_TOKENPOCKET:
    ok = web3_build_hdkey_page(bundle_out, master_fingerprint);
    snprintf(bundle_out->summary, sizeof(bundle_out->summary), "%s",
             i18n_tr_or("evm.qr.tokenpocket_static_connect",
                        "TokenPocket static connection QR"));
    break;
  case EVM_WEB3_PROFILE_IMTOKEN:
    ok = web3_build_hdkey_page(bundle_out, master_fingerprint);
    snprintf(bundle_out->summary, sizeof(bundle_out->summary), "%s",
             i18n_tr_or("evm.qr.imtoken_static_connect",
                        "imToken static connection QR"));
    break;
  case EVM_WEB3_PROFILE_ADDRESS:
  default:
    ok = false;
    break;
  }

  if (!ok) {
    evm_web3_qr_bundle_clear(bundle_out);
    return false;
  }
  return true;
}

bool evm_web3_build_external_connect_qr(
    evm_web3_profile_t profile, const evm_web3_external_account_t *account,
    evm_web3_qr_bundle_t *bundle_out) {
  if (!bundle_out || !account || account->address[0] == '\0')
    return false;

  evm_web3_qr_bundle_clear(bundle_out);
  snprintf(bundle_out->address, sizeof(bundle_out->address), "%s",
           account->address);

  if (profile == EVM_WEB3_PROFILE_ADDRESS) {
    bundle_out->pages[0] = strdup(bundle_out->address);
    if (!bundle_out->pages[0])
      return false;
    bundle_out->page_count = 1;
    bundle_out->animated = false;
    snprintf(bundle_out->summary, sizeof(bundle_out->summary), "%s",
             i18n_tr_or("evm.qr.smartcard_evm_address",
                        "Smartcard EVM address QR"));
    return true;
  }

  uint8_t master_fingerprint[4] = {0};
  if (account->has_master_fingerprint)
    memcpy(master_fingerprint, account->master_fingerprint,
           sizeof(master_fingerprint));

  bool ok = false;
  switch (profile) {
  case EVM_WEB3_PROFILE_OKX:
    ok = web3_build_external_multi_accounts(profile, account, bundle_out,
                                            master_fingerprint);
    snprintf(bundle_out->summary, sizeof(bundle_out->summary),
             i18n_tr_or("evm.qr.okx_smartcard_connect_format",
                        "OKX smartcard connection QR / %u parts"),
             (unsigned)bundle_out->page_count);
    break;
  case EVM_WEB3_PROFILE_BITGET:
    ok = web3_build_external_multi_accounts(profile, account, bundle_out,
                                            master_fingerprint);
    snprintf(bundle_out->summary, sizeof(bundle_out->summary),
             i18n_tr_or("evm.qr.bitget_smartcard_connect_format",
                        "Bitget smartcard static connection QR / %u parts"),
             (unsigned)bundle_out->page_count);
    break;
  case EVM_WEB3_PROFILE_METAMASK:
  case EVM_WEB3_PROFILE_RABBY:
    /* fallthrough */
  case EVM_WEB3_PROFILE_TOKENPOCKET:
    /* fallthrough */
  case EVM_WEB3_PROFILE_IMTOKEN: {
    web3_hdkey_entry_t standard;
    if (!web3_external_entry_from_account(&account->standard, &standard))
      ok = false;
    else {
      standard.origin_depth = WEB3_KEYPATH_DEPTH_AUTO;
      standard.children_depth = 0;
      cbor_value_t *hdkey = web3_hdkey_value(&standard, master_fingerprint);
      if (hdkey) {
        ok = web3_encode_single_ur("crypto-hdkey", hdkey,
                                   &bundle_out->pages[0]);
        cbor_value_free(hdkey);
      }
    }
    if (ok) {
      bundle_out->page_count = 1;
      bundle_out->animated = false;
    }
    snprintf(bundle_out->summary, sizeof(bundle_out->summary), "%s",
             profile == EVM_WEB3_PROFILE_IMTOKEN
                 ? i18n_tr_or("evm.qr.imtoken_smartcard_connect",
                              "imToken smartcard connection QR")
                 : i18n_tr_or("evm.qr.smartcard_account_connect",
                              "Smartcard account connection QR"));
    break;
  }
  case EVM_WEB3_PROFILE_ADDRESS:
  default:
    ok = false;
    break;
  }

  if (!ok) {
    evm_web3_qr_bundle_clear(bundle_out);
    return false;
  }
  return true;
}

void evm_web3_qr_bundle_clear(evm_web3_qr_bundle_t *bundle) {
  if (!bundle)
    return;
  for (size_t i = 0; i < EVM_WEB3_MAX_QR_PAGES; i++) {
    free(bundle->pages[i]);
    bundle->pages[i] = NULL;
  }
  bundle->page_count = 0;
  bundle->animated = false;
  bundle->address[0] = '\0';
  bundle->summary[0] = '\0';
}
