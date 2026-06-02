#include "smartcard_transport.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "smartcard_ccid.h"
#include "smartcard_pn532.h"

static const char *TAG = "KSIG_CARD_TX";

static smartcard_transport_t s_selected = SMARTCARD_TRANSPORT_AUTO;
static smartcard_transport_t s_active = SMARTCARD_TRANSPORT_USB_CCID;
static bool s_active_valid;

#define SMARTCARD_TRANSPORT_DEFAULT_TIMEOUT_MS 30000U
#define SMARTCARD_TRANSPORT_AUTO_USB_TIMEOUT_MS 3000U

static uint32_t default_timeout(uint32_t timeout_ms) {
  return timeout_ms ? timeout_ms : SMARTCARD_TRANSPORT_DEFAULT_TIMEOUT_MS;
}

static uint32_t auto_usb_probe_timeout(uint32_t timeout_ms) {
  uint32_t timeout = default_timeout(timeout_ms);
  return timeout > SMARTCARD_TRANSPORT_AUTO_USB_TIMEOUT_MS
             ? SMARTCARD_TRANSPORT_AUTO_USB_TIMEOUT_MS
             : timeout;
}

const char *smartcard_transport_name(smartcard_transport_t transport) {
  switch (transport) {
  case SMARTCARD_TRANSPORT_AUTO:
    return "Auto";
  case SMARTCARD_TRANSPORT_USB_CCID:
    return "USB CCID";
  case SMARTCARD_TRANSPORT_NFC_PN532:
    return "NFC PN532";
  default:
    return "Unknown";
  }
}

void smartcard_transport_set(smartcard_transport_t transport) {
  if (transport > SMARTCARD_TRANSPORT_NFC_PN532)
    transport = SMARTCARD_TRANSPORT_AUTO;
  s_selected = transport;
  s_active_valid = false;
}

smartcard_transport_t smartcard_transport_get(void) { return s_selected; }

smartcard_transport_t smartcard_transport_active(void) {
  return s_active_valid ? s_active : s_selected;
}

esp_err_t smartcard_transport_probe(uint32_t timeout_ms) {
  const uint32_t timeout = default_timeout(timeout_ms);

  if (s_selected == SMARTCARD_TRANSPORT_USB_CCID) {
    esp_err_t err = smartcard_ccid_probe(timeout);
    if (err == ESP_OK) {
      s_active = SMARTCARD_TRANSPORT_USB_CCID;
      s_active_valid = true;
    } else {
      s_active_valid = false;
    }
    return err;
  }

  if (s_selected == SMARTCARD_TRANSPORT_NFC_PN532) {
    esp_err_t err = smartcard_pn532_probe(timeout);
    if (err == ESP_OK) {
      s_active = SMARTCARD_TRANSPORT_NFC_PN532;
      s_active_valid = true;
    } else {
      smartcard_pn532_report_t report;
      smartcard_pn532_snapshot(&report);
      if (report.initialized && report.pn532_present) {
        s_active = SMARTCARD_TRANSPORT_NFC_PN532;
        s_active_valid = true;
      } else {
        s_active_valid = false;
      }
    }
    return err;
  }

  esp_err_t usb_err = smartcard_ccid_probe(auto_usb_probe_timeout(timeout));
  if (usb_err == ESP_OK) {
    s_active = SMARTCARD_TRANSPORT_USB_CCID;
    s_active_valid = true;
    return ESP_OK;
  }

  esp_err_t nfc_err = smartcard_pn532_probe(timeout);
  if (nfc_err == ESP_OK) {
    s_active = SMARTCARD_TRANSPORT_NFC_PN532;
    s_active_valid = true;
    ESP_LOGI(TAG, "Auto-selected NFC PN532 smartcard transport");
    return ESP_OK;
  }

  smartcard_pn532_report_t nfc_report;
  smartcard_pn532_snapshot(&nfc_report);
  if (nfc_report.initialized && nfc_report.pn532_present) {
    s_active = SMARTCARD_TRANSPORT_NFC_PN532;
    s_active_valid = true;
  } else {
    s_active_valid = false;
  }

  ESP_LOGW(TAG, "No smartcard transport ready: USB=%s NFC=%s",
           esp_err_to_name(usb_err), esp_err_to_name(nfc_err));
  return nfc_err != ESP_ERR_NOT_FOUND ? nfc_err : usb_err;
}

esp_err_t smartcard_transport_transmit_apdu(const uint8_t *apdu,
                                            size_t apdu_len,
                                            uint8_t *response,
                                            size_t response_cap,
                                            size_t *response_len, uint16_t *sw,
                                            uint32_t timeout_ms) {
  if (!s_active_valid) {
    esp_err_t probe_err = smartcard_transport_probe(timeout_ms);
    if (probe_err != ESP_OK)
      return probe_err;
  }

  esp_err_t err = ESP_ERR_INVALID_STATE;
  if (s_active == SMARTCARD_TRANSPORT_NFC_PN532) {
    err = smartcard_pn532_transmit_apdu(apdu, apdu_len, response, response_cap,
                                        response_len, sw, timeout_ms);
  } else {
    err = smartcard_ccid_transmit_apdu(apdu, apdu_len, response, response_cap,
                                       response_len, sw, timeout_ms);
  }

  if (err != ESP_OK)
    s_active_valid = false;
  return err;
}

void smartcard_transport_format_report(char *out, size_t out_len) {
  if (!out || out_len == 0)
    return;

  char usb[512] = {0};
  char nfc[512] = {0};
  smartcard_ccid_format_report(usb, sizeof(usb));
  smartcard_pn532_format_report(nfc, sizeof(nfc));

  snprintf(out, out_len, "Selected: %s\nActive: %s%s\n\nUSB CCID\n%s\n\nNFC PN532\n%s",
           smartcard_transport_name(s_selected),
           s_active_valid ? smartcard_transport_name(s_active) : "None",
           s_active_valid ? "" : " (not probed)", usb, nfc);
}
