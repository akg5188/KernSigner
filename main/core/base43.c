/*
 * Base43 encoding/decoding
 *
 * Charset: "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ$*+-./:"
 * Compatible with KernSigner's base43 implementation for QR code transport.
 *
 * The algorithm treats input as a big-endian number in the given base
 * and converts between bases (43 <-> 256). Leading zero-characters in
 * the encoded string map to leading 0x00 bytes in the decoded output.
 */

#include "base43.h"
#include <stdlib.h>
#include <string.h>

static const char B43CHARS[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ$*+-./:";
#define B43_BASE 43

/* Returns digit value 0-42, or -1 if invalid */
static int char_to_digit(char c) {
  for (int i = 0; i < B43_BASE; i++) {
    if (B43CHARS[i] == c)
      return i;
  }
  return -1;
}

bool base43_encode(const uint8_t *data, size_t data_len, char **out,
                   size_t *out_len) {
  if (!out || !out_len)
    return false;

  if (!data || data_len == 0) {
    *out = calloc(1, 1);
    *out_len = 0;
    return *out != NULL;
  }

  /*
   * Big-integer repeated division by 43.
   * Maximum encoded size is roughly data_len * log(256)/log(43) ≈ data_len *
   * 1.48 We allocate data_len * 2 for safety.
   */
  size_t buf_cap = data_len * 2 + 1;
  char *buf = malloc(buf_cap);
  if (!buf)
    return false;

  size_t buf_len = 0; /* digits stored in buf (reverse order) */

  /*
   * Work on a copy of the input as a big-endian integer.
   * Process: repeatedly divide by 43, collect remainders.
   */
  uint8_t *num = malloc(data_len);
  if (!num) {
    free(buf);
    return false;
  }
  memcpy(num, data, data_len);
  size_t num_len = data_len;

  while (num_len > 0) {
    uint32_t remainder = 0;
    size_t write_pos = 0;

    for (size_t i = 0; i < num_len; i++) {
      uint32_t val = remainder * 256 + num[i];
      uint8_t quot = (uint8_t)(val / B43_BASE);
      remainder = val % B43_BASE;
      if (write_pos > 0 || quot > 0) {
        num[write_pos++] = quot;
      }
    }

    if (buf_len >= buf_cap) {
      free(num);
      free(buf);
      return false;
    }
    buf[buf_len++] = B43CHARS[remainder];
    num_len = write_pos;
  }

  free(num);

  /* Count leading 0x00 bytes → leading '0' chars */
  size_t n_pad = 0;
  for (size_t i = 0; i < data_len; i++) {
    if (data[i] == 0)
      n_pad++;
    else
      break;
  }

  size_t total_len = n_pad + buf_len;
  char *result = malloc(total_len + 1);
  if (!result) {
    free(buf);
    return false;
  }

  /* Leading '0' chars for zero-padded bytes */
  for (size_t i = 0; i < n_pad; i++)
    result[i] = B43CHARS[0];

  /* Reverse the digits into the result */
  for (size_t i = 0; i < buf_len; i++)
    result[n_pad + i] = buf[buf_len - 1 - i];

  result[total_len] = '\0';
  free(buf);

  *out = result;
  *out_len = total_len;
  return true;
}

bool base43_decode(const char *str, size_t str_len, uint8_t **out,
                   size_t *out_len) {
  if (!str || str_len == 0 || !out || !out_len)
    return false;

  /*
   * Big-integer arithmetic using a byte array.
   * Maximum decoded size is roughly str_len * log(43)/log(256) ≈ str_len * 0.68
   * but we allocate str_len bytes for safety.
   */
  size_t buf_cap = str_len; /* generous upper bound */
  uint8_t *buf = calloc(buf_cap, 1);
  if (!buf)
    return false;

  size_t buf_len = 0; /* number of significant bytes in buf (big-endian) */

  /* Process each input character: bigint = bigint * 43 + digit */
  for (size_t i = 0; i < str_len; i++) {
    int digit = char_to_digit(str[i]);
    if (digit < 0) {
      free(buf);
      return false;
    }

    /* Multiply buf by 43 and add digit */
    uint32_t carry = (uint32_t)digit;
    for (size_t j = buf_len; j > 0; j--) {
      uint32_t val = (uint32_t)buf[j - 1] * B43_BASE + carry;
      buf[j - 1] = (uint8_t)(val & 0xFF);
      carry = val >> 8;
    }

    /* Extend buf if carry remains */
    while (carry > 0) {
      if (buf_len >= buf_cap) {
        free(buf);
        return false;
      }
      /* Shift existing bytes right to make room at front */
      memmove(buf + 1, buf, buf_len);
      buf[0] = (uint8_t)(carry & 0xFF);
      carry >>= 8;
      buf_len++;
    }
  }

  /* Count leading '0' characters → leading 0x00 bytes */
  size_t n_pad = 0;
  for (size_t i = 0; i < str_len; i++) {
    if (str[i] == B43CHARS[0])
      n_pad++;
    else
      break;
  }

  size_t total_len = n_pad + buf_len;
  if (total_len == 0) {
    /* Edge case: empty string or all zeros with no significant digits */
    free(buf);
    *out = calloc(1, 1);
    *out_len = 0;
    return *out != NULL;
  }

  uint8_t *result = malloc(total_len);
  if (!result) {
    free(buf);
    return false;
  }

  /* Leading zero bytes */
  memset(result, 0, n_pad);
  /* Copy significant bytes */
  memcpy(result + n_pad, buf, buf_len);

  free(buf);
  *out = result;
  *out_len = total_len;
  return true;
}
