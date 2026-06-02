#ifndef SMARTCARD_TRANSPORT_H
#define SMARTCARD_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
  SMARTCARD_TRANSPORT_AUTO = 0,
  SMARTCARD_TRANSPORT_USB_CCID,
  SMARTCARD_TRANSPORT_NFC_PN532,
} smartcard_transport_t;

void smartcard_transport_set(smartcard_transport_t transport);
smartcard_transport_t smartcard_transport_get(void);
smartcard_transport_t smartcard_transport_active(void);
const char *smartcard_transport_name(smartcard_transport_t transport);
esp_err_t smartcard_transport_probe(uint32_t timeout_ms);
esp_err_t smartcard_transport_transmit_apdu(const uint8_t *apdu,
                                            size_t apdu_len,
                                            uint8_t *response,
                                            size_t response_cap,
                                            size_t *response_len, uint16_t *sw,
                                            uint32_t timeout_ms);
void smartcard_transport_format_report(char *out, size_t out_len);

#endif // SMARTCARD_TRANSPORT_H
