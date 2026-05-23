#ifndef SIGNER_QR_DECODER_H
#define SIGNER_QR_DECODER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SIGNER_QR_RESULT_TEXT_MAX 160

typedef struct {
  bool found;
  bool printable;
  size_t payload_len;
  char text[SIGNER_QR_RESULT_TEXT_MAX];
} signer_qr_decode_result_t;

bool signer_qr_decode_rgb565(const uint8_t *rgb565_data, uint32_t width,
                           uint32_t height,
                           signer_qr_decode_result_t *result);
void signer_qr_decoder_deinit(void);

#endif // SIGNER_QR_DECODER_H
