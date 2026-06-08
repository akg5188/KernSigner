#ifndef SMARTCARD_PN5180_H
#define SMARTCARD_PN5180_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define SMARTCARD_PN5180_UID_MAX_LEN 10
#define SMARTCARD_PN5180_ATS_MAX_LEN 32

typedef struct {
  bool initialized;
  bool pn5180_present;
  bool field_on;
  bool target_present;
  bool iso_dep;
  uint8_t uid[SMARTCARD_PN5180_UID_MAX_LEN];
  size_t uid_len;
  uint8_t atqa[2];
  uint8_t sak;
  uint8_t ats[SMARTCARD_PN5180_ATS_MAX_LEN];
  size_t ats_len;
  char detail[160];
} smartcard_pn5180_report_t;

esp_err_t smartcard_pn5180_start(void);
esp_err_t smartcard_pn5180_probe(uint32_t timeout_ms);
esp_err_t smartcard_pn5180_transmit_apdu(const uint8_t *apdu,
                                         size_t apdu_len, uint8_t *response,
                                         size_t response_cap,
                                         size_t *response_len, uint16_t *sw,
                                         uint32_t timeout_ms);
void smartcard_pn5180_snapshot(smartcard_pn5180_report_t *out);
void smartcard_pn5180_format_report(char *out, size_t out_len);

#endif // SMARTCARD_PN5180_H
