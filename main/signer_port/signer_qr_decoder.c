#include "signer_qr_decoder.h"

#include <k_quirc.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static k_quirc_t *s_decoder;
static uint32_t s_width;
static uint32_t s_height;
static uint8_t *s_rgb565_gray_lut;

static bool ensure_lut(void) {
  if (s_rgb565_gray_lut)
    return true;

  s_rgb565_gray_lut = malloc(65536);
  if (!s_rgb565_gray_lut)
    return false;

  for (uint32_t i = 0; i < 65536; i++) {
    uint8_t r5 = (i >> 11) & 0x1F;
    uint8_t g6 = (i >> 5) & 0x3F;
    uint8_t b5 = i & 0x1F;
    uint8_t r8 = (r5 * 255 + 15) / 31;
    uint8_t g8 = (g6 * 255 + 31) / 63;
    uint8_t b8 = (b5 * 255 + 15) / 31;
    s_rgb565_gray_lut[i] = (uint8_t)((77 * r8 + 150 * g8 + 29 * b8) >> 8);
  }
  return true;
}

static bool ensure_decoder(uint32_t width, uint32_t height) {
  if (width == 0 || height == 0)
    return false;

  if (!s_decoder) {
    s_decoder = k_quirc_new();
    if (!s_decoder)
      return false;
  }

  if (s_width != width || s_height != height) {
    if (k_quirc_resize(s_decoder, (int)width, (int)height) < 0)
      return false;
    s_width = width;
    s_height = height;
  }

  return ensure_lut();
}

static void rgb565_to_grayscale(const uint8_t *rgb565_data, uint8_t *gray_data,
                                uint32_t width, uint32_t height) {
  const uint16_t *pixels = (const uint16_t *)rgb565_data;
  uint32_t total = width * height;

  for (uint32_t i = 0; i < total; i++)
    gray_data[i] = s_rgb565_gray_lut[pixels[i]];
}

static size_t utf8_sequence_len(uint8_t lead) {
  if (lead < 0x80)
    return 1;
  if ((lead & 0xe0) == 0xc0)
    return 2;
  if ((lead & 0xf0) == 0xe0)
    return 3;
  if ((lead & 0xf8) == 0xf0)
    return 4;
  return 0;
}

static bool valid_utf8_text_prefix(const uint8_t *payload, size_t len,
                                   size_t *safe_len) {
  if (safe_len)
    *safe_len = 0;
  if (!payload)
    return false;

  size_t i = 0;
  while (i < len) {
    uint8_t ch = payload[i];
    if (ch < 0x80) {
      if ((ch < 0x20 && ch != '\n' && ch != '\r' && ch != '\t') ||
          ch == 0x7f) {
        return false;
      }
      i++;
      if (safe_len)
        *safe_len = i;
      continue;
    }

    size_t seq_len = utf8_sequence_len(ch);
    if (seq_len == 0 || i + seq_len > len)
      return false;

    for (size_t j = 1; j < seq_len; j++) {
      if ((payload[i + j] & 0xc0) != 0x80)
        return false;
    }

    if (seq_len == 2 && ch < 0xc2)
      return false;
    if (seq_len == 3) {
      uint8_t b1 = payload[i + 1];
      if ((ch == 0xe0 && b1 < 0xa0) || (ch == 0xed && b1 >= 0xa0))
        return false;
    }
    if (seq_len == 4) {
      uint8_t b1 = payload[i + 1];
      if (ch > 0xf4 || (ch == 0xf0 && b1 < 0x90) ||
          (ch == 0xf4 && b1 >= 0x90))
        return false;
    }

    i += seq_len;
    if (safe_len)
      *safe_len = i;
  }

  return true;
}

static void fill_result_text(signer_qr_decode_result_t *result,
                             const uint8_t *payload, size_t len) {
  size_t safe_len = 0;
  bool printable = valid_utf8_text_prefix(payload, len, &safe_len);
  size_t out = 0;

  if (printable) {
    size_t limit = safe_len < (SIGNER_QR_RESULT_TEXT_MAX - 1)
                       ? safe_len
                       : (SIGNER_QR_RESULT_TEXT_MAX - 1);
    while (limit > 0 && limit < safe_len && (payload[limit] & 0xc0) == 0x80)
      limit--;

    if (limit > 0) {
      memcpy(result->text, payload, limit);
      out = limit;
    }
  } else {
    size_t limit = len < (SIGNER_QR_RESULT_TEXT_MAX - 1)
                       ? len
                       : (SIGNER_QR_RESULT_TEXT_MAX - 1);
    for (size_t i = 0; i < limit; i++) {
      uint8_t ch = payload[i];
      result->text[out++] = (ch >= 0x20 && ch <= 0x7e) ? (char)ch : '.';
    }
  }

  if (len >= SIGNER_QR_RESULT_TEXT_MAX && out < SIGNER_QR_RESULT_TEXT_MAX - 4) {
    result->text[out++] = '.';
    result->text[out++] = '.';
    result->text[out++] = '.';
  }
  result->text[out] = '\0';
  result->printable = printable;
}

bool signer_qr_decode_rgb565(const uint8_t *rgb565_data, uint32_t width,
                           uint32_t height,
                           signer_qr_decode_result_t *result) {
  if (result)
    memset(result, 0, sizeof(*result));

  if (!rgb565_data || !result || !ensure_decoder(width, height))
    return false;

  uint8_t *gray = k_quirc_begin(s_decoder, NULL, NULL);
  if (!gray)
    return false;

  rgb565_to_grayscale(rgb565_data, gray, width, height);
  k_quirc_end(s_decoder, false);

  int count = k_quirc_count(s_decoder);
  for (int i = 0; i < count; i++) {
    k_quirc_result_t qr_result;
    k_quirc_error_t err = k_quirc_decode(s_decoder, i, &qr_result);
    if (err == K_QUIRC_SUCCESS && qr_result.valid) {
      result->found = true;
      result->payload_len = (size_t)qr_result.data.payload_len;
      fill_result_text(result, qr_result.data.payload, result->payload_len);
      return true;
    }
  }

  return true;
}

void signer_qr_decoder_deinit(void) {
  if (s_decoder) {
    k_quirc_destroy(s_decoder);
    s_decoder = NULL;
  }
  free(s_rgb565_gray_lut);
  s_rgb565_gray_lut = NULL;
  s_width = 0;
  s_height = 0;
}
