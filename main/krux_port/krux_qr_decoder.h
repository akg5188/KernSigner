#ifndef KRUX_QR_DECODER_H
#define KRUX_QR_DECODER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KRUX_QR_RESULT_TEXT_MAX 160

typedef struct {
  bool found;
  bool printable;
  size_t payload_len;
  char text[KRUX_QR_RESULT_TEXT_MAX];
} krux_qr_decode_result_t;

bool krux_qr_decode_rgb565(const uint8_t *rgb565_data, uint32_t width,
                           uint32_t height,
                           krux_qr_decode_result_t *result);
void krux_qr_decoder_deinit(void);

#endif // KRUX_QR_DECODER_H
