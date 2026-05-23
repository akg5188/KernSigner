#include "btc_derivation.h"

#include <inttypes.h>
#include <stdio.h>

#include <wally_bip32.h>

static bool read_child_le(const unsigned char *p, uint32_t *out) {
  if (!p || !out)
    return false;
  *out = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
  return true;
}

static bool keypath_depth(size_t keypath_len, size_t *depth_out) {
  if (keypath_len < BIP32_KEY_FINGERPRINT_LEN ||
      ((keypath_len - BIP32_KEY_FINGERPRINT_LEN) % 4U) != 0 ||
      !depth_out) {
    return false;
  }

  *depth_out = (keypath_len - BIP32_KEY_FINGERPRINT_LEN) / 4U;
  return true;
}

bool btc_derivation_keypath_to_path(const unsigned char *keypath,
                                    size_t keypath_len, char *out,
                                    size_t out_len) {
  size_t depth = 0;
  if (!keypath || !keypath_depth(keypath_len, &depth) || !out || out_len < 2) {
    return false;
  }

  size_t pos = 0;
  int written = snprintf(out, out_len, "m");
  if (written < 0 || (size_t)written >= out_len)
    return false;
  pos = (size_t)written;

  for (size_t i = 0; i < depth; i++) {
    uint32_t child = 0;
    if (!read_child_le(keypath + BIP32_KEY_FINGERPRINT_LEN + i * 4U, &child))
      return false;

    bool hardened = (child & BIP32_INITIAL_HARDENED_CHILD) != 0;
    child &= ~BIP32_INITIAL_HARDENED_CHILD;
    written = snprintf(out + pos, out_len - pos, "/%" PRIu32 "%s", child,
                       hardened ? "'" : "");
    if (written < 0 || (size_t)written >= out_len - pos)
      return false;
    pos += (size_t)written;
  }

  return true;
}

static bool purpose_for_script(btc_derivation_script_t script,
                               uint32_t *purpose_out) {
  if (!purpose_out)
    return false;

  switch (script) {
  case BTC_DERIVATION_SCRIPT_P2PKH:
    *purpose_out = 44;
    return true;
  case BTC_DERIVATION_SCRIPT_P2SH_P2WPKH:
    *purpose_out = 49;
    return true;
  case BTC_DERIVATION_SCRIPT_P2WPKH:
    *purpose_out = 84;
    return true;
  default:
    return false;
  }
}

bool btc_derivation_satochip_sign_path(const unsigned char *keypath,
                                       size_t keypath_len,
                                       btc_derivation_script_t script,
                                       bool is_testnet, uint32_t account,
                                       char *out, size_t out_len) {
  size_t depth = 0;
  if (!keypath || !keypath_depth(keypath_len, &depth) || !out || out_len < 2)
    return false;

  if (depth != 2)
    return btc_derivation_keypath_to_path(keypath, keypath_len, out, out_len);

  uint32_t branch = 0;
  uint32_t index = 0;
  if (!read_child_le(keypath + BIP32_KEY_FINGERPRINT_LEN, &branch) ||
      !read_child_le(keypath + BIP32_KEY_FINGERPRINT_LEN + 4U, &index)) {
    return false;
  }

  if ((branch & BIP32_INITIAL_HARDENED_CHILD) ||
      (index & BIP32_INITIAL_HARDENED_CHILD)) {
    return false;
  }

  uint32_t purpose = 0;
  if (!purpose_for_script(script, &purpose))
    return false;

  const uint32_t coin = is_testnet ? 1U : 0U;
  int written =
      snprintf(out, out_len, "m/%" PRIu32 "'/%" PRIu32 "'/%" PRIu32
                              "'/%" PRIu32 "/%" PRIu32,
               purpose, coin, account, branch, index);
  return written >= 0 && (size_t)written < out_len;
}
