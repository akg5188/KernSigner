#ifndef ZBAR_QR_H
#define ZBAR_QR_H

#include <stdbool.h>
#include <stdint.h>

#include <k_quirc.h>

typedef struct zbar_qr_decoder zbar_qr_decoder_t;

zbar_qr_decoder_t *zbar_qr_decoder_create(void);
void zbar_qr_decoder_destroy(zbar_qr_decoder_t *decoder);
void zbar_qr_decoder_set_dense_mode(zbar_qr_decoder_t *decoder, bool enabled);

bool zbar_qr_decode_grayscale(zbar_qr_decoder_t *decoder,
                              const uint8_t *grayscale, int width, int height,
                              k_quirc_result_t *result);

#endif /* ZBAR_QR_H */
