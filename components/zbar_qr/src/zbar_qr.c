#include "zbar_qr.h"

#include <stdlib.h>
#include <string.h>

#include "zbar.h"

#define ZBAR_FORMAT_Y800 0x30303859UL
#define ZBAR_QR_NORMAL_DENSITY 1
#define ZBAR_QR_DENSE_DENSITY 2

struct zbar_qr_decoder {
  zbar_image_scanner_t *scanner;
  zbar_image_t *image;
  int image_width;
  int image_height;
  bool dense_mode;
};

zbar_qr_decoder_t *zbar_qr_decoder_create(void) {
  zbar_qr_decoder_t *decoder = calloc(1, sizeof(*decoder));
  if (!decoder)
    return NULL;

  decoder->scanner = zbar_image_scanner_create();
  if (!decoder->scanner) {
    free(decoder);
    return NULL;
  }

  decoder->image = zbar_image_create();
  if (!decoder->image) {
    zbar_image_scanner_destroy(decoder->scanner);
    free(decoder);
    return NULL;
  }

  zbar_image_scanner_set_config(decoder->scanner, 0, ZBAR_CFG_ENABLE, 0);
  zbar_image_scanner_set_config(decoder->scanner, ZBAR_QRCODE,
                                ZBAR_CFG_ENABLE, 1);
  zbar_image_scanner_set_config(decoder->scanner, ZBAR_QRCODE,
                                ZBAR_CFG_BINARY, 1);
  zbar_image_scanner_set_config(decoder->scanner, 0, ZBAR_CFG_TEST_INVERTED, 1);
  zbar_image_scanner_set_config(decoder->scanner, 0, ZBAR_CFG_X_DENSITY,
                                ZBAR_QR_NORMAL_DENSITY);
  zbar_image_scanner_set_config(decoder->scanner, 0, ZBAR_CFG_Y_DENSITY,
                                ZBAR_QR_NORMAL_DENSITY);
  zbar_image_scanner_enable_cache(decoder->scanner, 0);

  return decoder;
}

void zbar_qr_decoder_set_dense_mode(zbar_qr_decoder_t *decoder, bool enabled) {
  if (!decoder || !decoder->scanner || decoder->dense_mode == enabled)
    return;

  int density = enabled ? ZBAR_QR_DENSE_DENSITY : ZBAR_QR_NORMAL_DENSITY;
  zbar_image_scanner_set_config(decoder->scanner, 0, ZBAR_CFG_X_DENSITY,
                                density);
  zbar_image_scanner_set_config(decoder->scanner, 0, ZBAR_CFG_Y_DENSITY,
                                density);
  decoder->dense_mode = enabled;
}

void zbar_qr_decoder_destroy(zbar_qr_decoder_t *decoder) {
  if (!decoder)
    return;
  if (decoder->image)
    zbar_image_destroy(decoder->image);
  if (decoder->scanner)
    zbar_image_scanner_destroy(decoder->scanner);
  free(decoder);
}

bool zbar_qr_decode_grayscale(zbar_qr_decoder_t *decoder,
                              const uint8_t *grayscale, int width, int height,
                              k_quirc_result_t *result) {
  if (!decoder || !decoder->scanner || !grayscale || width <= 0 ||
      height <= 0 || !result || !decoder->image)
    return false;

  zbar_image_t *image = decoder->image;
  if (decoder->image_width != width || decoder->image_height != height) {
    zbar_image_set_format(image, ZBAR_FORMAT_Y800);
    zbar_image_set_size(image, (unsigned)width, (unsigned)height);
    zbar_image_set_crop(image, 0, 0, (unsigned)width, (unsigned)height);
    decoder->image_width = width;
    decoder->image_height = height;
  }
  zbar_image_set_data(image, grayscale, (unsigned long)width * height, NULL);

  int found = zbar_scan_image(decoder->scanner, image);
  if (found <= 0)
    return false;

  bool decoded = false;
  const zbar_symbol_t *symbol = zbar_image_first_symbol(image);
  for (; symbol; symbol = zbar_symbol_next(symbol)) {
    if (zbar_symbol_get_type(symbol) != ZBAR_QRCODE)
      continue;

    memset(result, 0, sizeof(*result));
    result->valid = true;
    result->data.data_type = K_QUIRC_DATA_TYPE_BYTE;

    unsigned int len = zbar_symbol_get_data_length(symbol);
    if (len >= K_QUIRC_MAX_PAYLOAD)
      len = K_QUIRC_MAX_PAYLOAD - 1;
    result->data.payload_len = (int)len;
    memcpy(result->data.payload, zbar_symbol_get_data(symbol), len);
    result->data.payload[len] = 0;

    unsigned int loc_size = zbar_symbol_get_loc_size(symbol);
    unsigned int corner_count = loc_size < 4 ? loc_size : 4;
    for (unsigned int i = 0; i < corner_count; i++) {
      result->corners[i].x = zbar_symbol_get_loc_x(symbol, i);
      result->corners[i].y = zbar_symbol_get_loc_y(symbol, i);
    }

    decoded = true;
    break;
  }

  return decoded;
}
