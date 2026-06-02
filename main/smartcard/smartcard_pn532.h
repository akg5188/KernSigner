#ifndef SMARTCARD_PN532_H
#define SMARTCARD_PN532_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define SMARTCARD_PN532_UID_MAX_LEN 10

typedef struct {
  bool initialized;
  bool pn532_present;
  bool target_present;
  bool iso_dep;
  uint8_t target_number;
  uint8_t uid[SMARTCARD_PN532_UID_MAX_LEN];
  size_t uid_len;
  uint8_t atqa[2];
  uint8_t sak;
  char detail[160];
} smartcard_pn532_report_t;

esp_err_t smartcard_pn532_start(void);
esp_err_t smartcard_pn532_probe(uint32_t timeout_ms);
esp_err_t smartcard_pn532_transmit_apdu(const uint8_t *apdu, size_t apdu_len,
                                        uint8_t *response,
                                        size_t response_cap,
                                        size_t *response_len, uint16_t *sw,
                                        uint32_t timeout_ms);
void smartcard_pn532_snapshot(smartcard_pn532_report_t *out);
void smartcard_pn532_format_report(char *out, size_t out_len);

#endif // SMARTCARD_PN532_H
