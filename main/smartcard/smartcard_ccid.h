#ifndef SMARTCARD_CCID_H
#define SMARTCARD_CCID_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define SMARTCARD_CCID_ATR_MAX_LEN 64
#define SMARTCARD_CCID_RESPONSE_MAX_LEN 256
#define SMARTCARD_CCID_APDU_MAX_LEN 260

typedef enum {
  SMARTCARD_CCID_STATE_IDLE,
  SMARTCARD_CCID_STATE_STARTING,
  SMARTCARD_CCID_STATE_WAITING,
  SMARTCARD_CCID_STATE_READER_READY,
  SMARTCARD_CCID_STATE_ATR_OK,
  SMARTCARD_CCID_STATE_APPLET_OK,
  SMARTCARD_CCID_STATE_PASS,
  SMARTCARD_CCID_STATE_TIMEOUT,
  SMARTCARD_CCID_STATE_FAIL,
  SMARTCARD_CCID_STATE_UNSUPPORTED,
} smartcard_ccid_state_t;

typedef struct {
  smartcard_ccid_state_t state;
  bool terminal;
  bool host_started;
  bool reader_present;
  bool card_present;
  uint8_t dev_addr;
  uint16_t vid;
  uint16_t pid;
  uint8_t interface_num;
  uint32_t ccid_protocols;
  uint32_t ccid_features;
  uint32_t ccid_max_msg_len;
  uint32_t ccid_max_ifsd;
  bool t1_present;
  bool tpdu_mode;
  char applet[24];
  char detail[160];
  uint8_t atr[SMARTCARD_CCID_ATR_MAX_LEN];
  size_t atr_len;
  uint8_t response[SMARTCARD_CCID_RESPONSE_MAX_LEN];
  size_t response_len;
  uint16_t sw;
} smartcard_ccid_report_t;

esp_err_t smartcard_ccid_start(void);
esp_err_t smartcard_ccid_probe(uint32_t timeout_ms);
/**
 * Transmit one APDU through the currently ready CCID reader/card.
 *
 * Returns ESP_OK when the APDU exchange completed and SW was parsed, even if
 * SW is not 0x9000. The response buffer receives the full APDU response,
 * including SW1/SW2. response_len is set to the required/actual response size.
 * If response_cap is too small, ESP_ERR_INVALID_SIZE is returned and
 * response_len contains the required size.
 */
esp_err_t smartcard_ccid_transmit_apdu(const uint8_t *apdu, size_t apdu_len,
                                       uint8_t *response,
                                       size_t response_cap,
                                       size_t *response_len, uint16_t *sw,
                                       uint32_t timeout_ms);
void smartcard_ccid_set_factory_reset_mode(bool enabled);
void smartcard_ccid_snapshot(smartcard_ccid_report_t *out);
void smartcard_ccid_format_report(char *out, size_t out_len);
const char *smartcard_ccid_state_name(smartcard_ccid_state_t state);
bool smartcard_ccid_report_is_success(const smartcard_ccid_report_t *report);

#endif // SMARTCARD_CCID_H
