#include "smartcard_pn532.h"

#include "smartcard_ccid.h"

#ifndef SIMULATOR
#include "sdkconfig.h"
#endif

#ifndef CONFIG_KSIG_PN532_ENABLED
#define CONFIG_KSIG_PN532_ENABLED 1
#endif

#if defined(SIMULATOR) || !CONFIG_KSIG_PN532_ENABLED

#include <stdio.h>
#include <string.h>

#if defined(SIMULATOR)
#define PN532_UNAVAILABLE_DETAIL "Simulator does not access PN532 NFC hardware."
#else
#define PN532_UNAVAILABLE_DETAIL "PN532 NFC support is disabled in this build."
#endif

esp_err_t smartcard_pn532_start(void) { return ESP_ERR_NOT_SUPPORTED; }

esp_err_t smartcard_pn532_probe(uint32_t timeout_ms) {
  (void)timeout_ms;
  return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t smartcard_pn532_transmit_apdu(const uint8_t *apdu, size_t apdu_len,
                                        uint8_t *response,
                                        size_t response_cap,
                                        size_t *response_len, uint16_t *sw,
                                        uint32_t timeout_ms) {
  (void)apdu;
  (void)apdu_len;
  (void)response;
  (void)response_cap;
  (void)response_len;
  (void)sw;
  (void)timeout_ms;
  return ESP_ERR_NOT_SUPPORTED;
}

void smartcard_pn532_snapshot(smartcard_pn532_report_t *out) {
  if (!out)
    return;
  memset(out, 0, sizeof(*out));
  snprintf(out->detail, sizeof(out->detail), "%s", PN532_UNAVAILABLE_DETAIL);
}

void smartcard_pn532_format_report(char *out, size_t out_len) {
  if (!out || out_len == 0)
    return;
  snprintf(out, out_len, "%s", PN532_UNAVAILABLE_DETAIL);
}

#else

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_log_buffer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#ifndef ESP_ERR_INVALID_RESPONSE
#define ESP_ERR_INVALID_RESPONSE ESP_ERR_INVALID_STATE
#endif

#ifndef CONFIG_KSIG_PN532_I2C_HZ
#define CONFIG_KSIG_PN532_I2C_HZ 100000
#endif

static const char *TAG = "KSIG_PN532";

#define PN532_I2C_ADDRESS 0x24
#define PN532_I2C_FRAME_PREFIX 0x00
#define PN532_I2C_READY 0x01
#define PN532_I2C_BUS_FREE_MS 5

#define PN532_PREAMBLE 0x00
#define PN532_STARTCODE1 0x00
#define PN532_STARTCODE2 0xFF
#define PN532_POSTAMBLE 0x00
#define PN532_HOSTTOPN532 0xD4
#define PN532_PN532TOHOST 0xD5

#define PN532_CMD_GETFIRMWAREVERSION 0x02
#define PN532_CMD_SAMCONFIGURATION 0x14
#define PN532_CMD_INDATAEXCHANGE 0x40
#define PN532_CMD_INLISTPASSIVETARGET 0x4A

#define PN532_NORMAL_PAYLOAD_MAX 255
#define PN532_COMMAND_MAX_LEN (2 + SMARTCARD_CCID_APDU_MAX_LEN)
#define PN532_FRAME_MAX_LEN 320
#define PN532_RESPONSE_READ_RETRIES 3

static const uint8_t PN532_ACK_FRAME[] = {0x00, 0x00, 0xFF,
                                          0x00, 0xFF, 0x00};

static SemaphoreHandle_t s_lock;
static i2c_master_bus_handle_t s_pn532_bus;
static i2c_master_dev_handle_t s_pn532;
static bool s_own_i2c_bus;
static bool s_started;
static bool s_target_active;
static uint8_t s_target_number = 1;
static smartcard_pn532_report_t *s_report;

#define PN532_DEFAULT_TIMEOUT_MS 30000U

static uint32_t default_timeout(uint32_t timeout_ms) {
  return timeout_ms ? timeout_ms : PN532_DEFAULT_TIMEOUT_MS;
}

static void report_detail(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(s_report->detail, sizeof(s_report->detail), fmt, ap);
  va_end(ap);
}

static esp_err_t ensure_lock(void) {
  if (!s_lock) {
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock)
      return ESP_ERR_NO_MEM;
  }
  if (!s_report) {
    s_report = calloc(1, sizeof(*s_report));
    if (!s_report)
      return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

static esp_err_t take_lock(void) {
  ESP_RETURN_ON_ERROR(ensure_lock(), TAG, "mutex init failed");
  return xSemaphoreTake(s_lock, pdMS_TO_TICKS(30000)) == pdTRUE
             ? ESP_OK
             : ESP_ERR_TIMEOUT;
}

static void give_lock(void) {
  if (s_lock)
    xSemaphoreGive(s_lock);
}

static void pn532_clear_target_report(void) {
  s_target_active = false;
  s_target_number = 1;
  s_report->target_present = false;
  s_report->target_number = 0;
  s_report->uid_len = 0;
  memset(s_report->uid, 0, sizeof(s_report->uid));
  s_report->atqa[0] = 0;
  s_report->atqa[1] = 0;
  s_report->sak = 0;
  s_report->iso_dep = false;
}

static void pn532_drop_device_locked(void) {
  if (s_pn532) {
    i2c_master_bus_rm_device(s_pn532);
    s_pn532 = NULL;
  }
  if (s_own_i2c_bus && s_pn532_bus) {
    i2c_del_master_bus(s_pn532_bus);
    s_pn532_bus = NULL;
    s_own_i2c_bus = false;
  }
  s_started = false;
  pn532_clear_target_report();
}

static uint8_t pn532_checksum(const uint8_t *data, size_t len) {
  uint8_t sum = 0;
  for (size_t i = 0; i < len; i++)
    sum = (uint8_t)(sum + data[i]);
  return (uint8_t)(~sum + 1);
}

static void pn532_i2c_bus_free_delay(void) {
  vTaskDelay(pdMS_TO_TICKS(PN532_I2C_BUS_FREE_MS));
}

static bool pn532_i2c_busy_error(esp_err_t err) {
  return err == ESP_ERR_TIMEOUT || err == ESP_ERR_INVALID_STATE ||
         err == ESP_ERR_NOT_FOUND;
}

static esp_err_t pn532_wait_ready(uint32_t timeout_ms) {
  uint8_t status = 0;
  const TickType_t start = xTaskGetTickCount();
  const TickType_t timeout_ticks = pdMS_TO_TICKS(default_timeout(timeout_ms));
  esp_err_t last_err = ESP_ERR_TIMEOUT;

  do {
    status = 0;
    pn532_i2c_bus_free_delay();
    esp_err_t err = i2c_master_receive(s_pn532, &status, 1, 50);
    if (err == ESP_OK && (status & PN532_I2C_READY))
      return ESP_OK;
    if (err != ESP_OK && !pn532_i2c_busy_error(err))
      return err;
    if (err != ESP_OK)
      last_err = err;
    vTaskDelay(pdMS_TO_TICKS(10));
  } while ((xTaskGetTickCount() - start) <= timeout_ticks);

  return last_err == ESP_OK ? ESP_ERR_TIMEOUT : last_err;
}

static esp_err_t pn532_read_bytes(uint8_t *data, size_t len) {
  pn532_i2c_bus_free_delay();
  return i2c_master_receive(s_pn532, data, len, 100);
}

static esp_err_t pn532_write_command(const uint8_t *cmd, size_t cmd_len) {
  if (!cmd || cmd_len == 0 || cmd_len > PN532_COMMAND_MAX_LEN)
    return ESP_ERR_INVALID_ARG;

  const size_t payload_len = cmd_len + 1U;
  uint8_t frame[PN532_FRAME_MAX_LEN];
  uint8_t payload[1 + PN532_COMMAND_MAX_LEN];
  size_t pos = 0;

  if (payload_len > sizeof(payload))
    return ESP_ERR_INVALID_SIZE;

  payload[0] = PN532_HOSTTOPN532;
  memcpy(payload + 1, cmd, cmd_len);

  frame[pos++] = PN532_I2C_FRAME_PREFIX;
  frame[pos++] = PN532_PREAMBLE;
  frame[pos++] = PN532_STARTCODE1;
  frame[pos++] = PN532_STARTCODE2;

  if (payload_len <= PN532_NORMAL_PAYLOAD_MAX) {
    uint8_t len = (uint8_t)payload_len;
    frame[pos++] = len;
    frame[pos++] = (uint8_t)(~len + 1);
  } else {
    uint8_t len_hi = (uint8_t)(payload_len >> 8);
    uint8_t len_lo = (uint8_t)payload_len;
    frame[pos++] = 0xFF;
    frame[pos++] = 0xFF;
    frame[pos++] = len_hi;
    frame[pos++] = len_lo;
    frame[pos++] = (uint8_t)(~(uint8_t)(len_hi + len_lo) + 1);
  }

  if (pos + payload_len + 2U > sizeof(frame))
    return ESP_ERR_INVALID_SIZE;
  memcpy(frame + pos, payload, payload_len);
  pos += payload_len;
  frame[pos++] = pn532_checksum(payload, payload_len);
  frame[pos++] = PN532_POSTAMBLE;

  pn532_i2c_bus_free_delay();
  return i2c_master_transmit(s_pn532, frame, pos, 100);
}

static esp_err_t pn532_read_ack(uint32_t timeout_ms) {
  esp_err_t err = pn532_wait_ready(timeout_ms);
  if (err != ESP_OK)
    return err;

  uint8_t rx[sizeof(PN532_ACK_FRAME) + 1] = {0};
  err = pn532_read_bytes(rx, sizeof(rx));
  if (err != ESP_OK)
    return err;

  ESP_LOGI(TAG, "PN532 ACK raw (%u bytes)", (unsigned)sizeof(rx));
  ESP_LOG_BUFFER_HEX(TAG, rx, sizeof(rx));

  if ((rx[0] & PN532_I2C_READY) == 0)
    return ESP_ERR_INVALID_RESPONSE;
  if (memcmp(rx + 1, PN532_ACK_FRAME, sizeof(PN532_ACK_FRAME)) != 0)
    return ESP_ERR_INVALID_RESPONSE;

  return ESP_OK;
}

static esp_err_t pn532_find_frame(const uint8_t *rx, size_t rx_len,
                                  size_t *off_out) {
  if (!rx || !off_out || rx_len < 8)
    return ESP_ERR_INVALID_ARG;
  if ((rx[0] & PN532_I2C_READY) == 0)
    return ESP_ERR_INVALID_RESPONSE;

  size_t off = 1;
  while (off + 6 < rx_len &&
         !(rx[off] == PN532_PREAMBLE &&
           rx[off + 1] == PN532_STARTCODE1 &&
           rx[off + 2] == PN532_STARTCODE2)) {
    off++;
  }

  if (off + 6 >= rx_len)
    return ESP_ERR_INVALID_RESPONSE;
  *off_out = off;
  return ESP_OK;
}

static esp_err_t pn532_parse_response_frame(const uint8_t *rx, size_t rx_len,
                                            uint8_t cmd, uint8_t *out,
                                            size_t out_cap, size_t *out_len) {
  size_t off = 0;
  esp_err_t err = pn532_find_frame(rx, rx_len, &off);
  if (err != ESP_OK)
    return err;

  size_t payload_off = 0;
  size_t len = 0;
  if (rx[off + 3] == 0xFF && rx[off + 4] == 0xFF) {
    if (off + 10 >= rx_len)
      return ESP_ERR_INVALID_RESPONSE;
    len = ((size_t)rx[off + 5] << 8) | rx[off + 6];
    uint8_t lcs = rx[off + 7];
    if ((uint8_t)(rx[off + 5] + rx[off + 6] + lcs) != 0 || len < 2)
      return ESP_ERR_INVALID_RESPONSE;
    payload_off = off + 8;
  } else {
    uint8_t normal_len = rx[off + 3];
    uint8_t lcs = rx[off + 4];
    if ((uint8_t)(normal_len + lcs) != 0 || normal_len < 2)
      return ESP_ERR_INVALID_RESPONSE;
    len = normal_len;
    payload_off = off + 5;
  }

  if (payload_off + len + 2U > rx_len)
    return ESP_ERR_INVALID_SIZE;

  const uint8_t *payload = rx + payload_off;
  if (payload[0] != PN532_PN532TOHOST || payload[1] != (uint8_t)(cmd + 1))
    return ESP_ERR_INVALID_RESPONSE;

  uint8_t dcs = rx[payload_off + len];
  uint8_t sum = dcs;
  for (size_t i = 0; i < len; i++)
    sum = (uint8_t)(sum + payload[i]);
  if (sum != 0)
    return ESP_ERR_INVALID_RESPONSE;

  size_t data_len = len - 2U;
  if (data_len > out_cap) {
    if (out_len)
      *out_len = data_len;
    return ESP_ERR_INVALID_SIZE;
  }

  if (out && data_len)
    memcpy(out, payload + 2, data_len);
  if (out_len)
    *out_len = data_len;
  return ESP_OK;
}

static esp_err_t pn532_read_response(uint8_t cmd, uint8_t *out, size_t out_cap,
                                     size_t *out_len, uint32_t timeout_ms) {
  uint8_t rx[PN532_FRAME_MAX_LEN] = {0};
  size_t rx_len = out_cap + 16U;
  if (rx_len < 32U)
    rx_len = 32U;
  if (rx_len > sizeof(rx))
    rx_len = sizeof(rx);

  esp_err_t last_err = ESP_ERR_INVALID_RESPONSE;
  for (int attempt = 0; attempt < PN532_RESPONSE_READ_RETRIES; attempt++) {
    esp_err_t err = pn532_wait_ready(timeout_ms);
    if (err != ESP_OK) {
      last_err = err;
      if (err != ESP_ERR_INVALID_STATE && err != ESP_ERR_INVALID_RESPONSE)
        return err;
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }

    memset(rx, 0, rx_len);
    err = pn532_read_bytes(rx, rx_len);
    if (err != ESP_OK) {
      last_err = err;
      if (err != ESP_ERR_INVALID_STATE && err != ESP_ERR_INVALID_RESPONSE)
        return err;
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }

    ESP_LOGI(TAG, "PN532 response cmd=0x%02X raw (%u bytes) attempt=%d", cmd,
             (unsigned)rx_len, attempt + 1);
    ESP_LOG_BUFFER_HEX(TAG, rx, rx_len);

    err = pn532_parse_response_frame(rx, rx_len, cmd, out, out_cap, out_len);
    if (err == ESP_OK)
      return ESP_OK;

    last_err = err;
    if (err != ESP_ERR_INVALID_RESPONSE && err != ESP_ERR_INVALID_SIZE &&
        err != ESP_ERR_INVALID_STATE) {
      return err;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }

  return last_err;
}

static esp_err_t pn532_command(const uint8_t *cmd, size_t cmd_len, uint8_t *out,
                               size_t out_cap, size_t *out_len,
                               uint32_t timeout_ms) {
  ESP_LOGI(TAG, "PN532 command (%u bytes)", (unsigned)cmd_len);
  ESP_LOG_BUFFER_HEX(TAG, cmd, cmd_len);

  esp_err_t err = pn532_write_command(cmd, cmd_len);
  if (err != ESP_OK)
    return err;

  err = pn532_read_ack(1000);
  if (err != ESP_OK)
    return err;

  return pn532_read_response(cmd[0], out, out_cap, out_len, timeout_ms);
}

static esp_err_t pn532_read_firmware_locked(void) {
  uint8_t cmd[] = {PN532_CMD_GETFIRMWAREVERSION};
  uint8_t resp[16];
  size_t resp_len = 0;
  esp_err_t err =
      pn532_command(cmd, sizeof(cmd), resp, sizeof(resp), &resp_len, 1000);
  if (err != ESP_OK)
    return err;
  if (resp_len < 4)
    return ESP_ERR_INVALID_RESPONSE;

  ESP_LOGI(TAG, "PN532 firmware: IC=0x%02X Ver=%u.%u Support=0x%02X", resp[0],
           resp[1], resp[2], resp[3]);
  return ESP_OK;
}

static esp_err_t pn532_sam_config_locked(void) {
  uint8_t cmd[] = {PN532_CMD_SAMCONFIGURATION, 0x01, 0x14, 0x01};
  uint8_t resp[8];
  size_t resp_len = 0;
  return pn532_command(cmd, sizeof(cmd), resp, sizeof(resp), &resp_len, 1000);
}

static esp_err_t pn532_get_i2c_bus_locked(i2c_master_bus_handle_t *bus_out) {
  if (!bus_out)
    return ESP_ERR_INVALID_ARG;

#if CONFIG_KSIG_PN532_DEDICATED_I2C
  if (CONFIG_KSIG_PN532_SDA_GPIO < 0 || CONFIG_KSIG_PN532_SCL_GPIO < 0) {
    report_detail("PN532 dedicated I2C pins are not configured.");
    return ESP_ERR_INVALID_STATE;
  }

  if (!s_pn532_bus) {
    i2c_master_bus_config_t buscfg = {
        .i2c_port = CONFIG_KSIG_PN532_I2C_PORT,
        .sda_io_num = CONFIG_KSIG_PN532_SDA_GPIO,
        .scl_io_num = CONFIG_KSIG_PN532_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&buscfg, &s_pn532_bus);
    if (err != ESP_OK) {
      report_detail("PN532 dedicated I2C init failed: %s.", esp_err_to_name(err));
      return err;
    }
    s_own_i2c_bus = true;
    ESP_LOGI(TAG, "PN532 dedicated I2C bus SDA=%d SCL=%d Hz=%d",
             CONFIG_KSIG_PN532_SDA_GPIO, CONFIG_KSIG_PN532_SCL_GPIO,
             CONFIG_KSIG_PN532_I2C_HZ);
  }
  *bus_out = s_pn532_bus;
  return ESP_OK;
#else
  i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
  if (!bus) {
    report_detail("I2C bus is not initialized.");
    return ESP_ERR_INVALID_STATE;
  }
  *bus_out = bus;
  return ESP_OK;
#endif
}

static esp_err_t pn532_start_locked(void) {
  if (s_started)
    return ESP_OK;

  memset(s_report, 0, sizeof(*s_report));
  report_detail("PN532 not initialized.");

  i2c_master_bus_handle_t bus = NULL;
  esp_err_t err = pn532_get_i2c_bus_locked(&bus);
  if (err != ESP_OK)
    return err;

  err = i2c_master_probe(bus, PN532_I2C_ADDRESS, 200);
  if (err != ESP_OK) {
    report_detail("PN532 not found at I2C address 0x%02X: %s.",
                  PN532_I2C_ADDRESS, esp_err_to_name(err));
    if (s_own_i2c_bus && s_pn532_bus) {
      i2c_del_master_bus(s_pn532_bus);
      s_pn532_bus = NULL;
      s_own_i2c_bus = false;
    }
    return ESP_ERR_NOT_FOUND;
  }

  i2c_device_config_t devcfg = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = PN532_I2C_ADDRESS,
      .scl_speed_hz = CONFIG_KSIG_PN532_I2C_HZ,
  };
  err = i2c_master_bus_add_device(bus, &devcfg, &s_pn532);
  if (err != ESP_OK) {
    report_detail("PN532 I2C add device failed: %s.", esp_err_to_name(err));
    if (s_own_i2c_bus && s_pn532_bus) {
      i2c_del_master_bus(s_pn532_bus);
      s_pn532_bus = NULL;
      s_own_i2c_bus = false;
    }
    return err;
  }

  uint8_t wake = 0x00;
  (void)i2c_master_transmit(s_pn532, &wake, sizeof(wake), 100);
  vTaskDelay(pdMS_TO_TICKS(100));

  err = pn532_read_firmware_locked();
  if (err != ESP_OK) {
    report_detail("PN532 firmware read failed: %s.", esp_err_to_name(err));
    i2c_master_bus_rm_device(s_pn532);
    s_pn532 = NULL;
    if (s_own_i2c_bus && s_pn532_bus) {
      i2c_del_master_bus(s_pn532_bus);
      s_pn532_bus = NULL;
      s_own_i2c_bus = false;
    }
    return err;
  }

  err = pn532_sam_config_locked();
  if (err != ESP_OK) {
    report_detail("PN532 SAMConfiguration failed: %s.", esp_err_to_name(err));
    pn532_drop_device_locked();
    return err;
  }

  s_started = true;
  s_report->initialized = true;
  s_report->pn532_present = true;
  pn532_clear_target_report();
  report_detail("PN532 ready.");
  return ESP_OK;
}

static esp_err_t pn532_poll_target_locked(uint32_t timeout_ms) {
  uint8_t cmd[] = {PN532_CMD_INLISTPASSIVETARGET, 0x01, 0x00};
  uint8_t resp[96];
  size_t resp_len = 0;
  esp_err_t err =
      pn532_command(cmd, sizeof(cmd), resp, sizeof(resp), &resp_len,
                    timeout_ms ? timeout_ms : 1000);
  if (err != ESP_OK) {
    s_target_active = false;
    s_report->target_present = false;
    report_detail(err == ESP_ERR_TIMEOUT ? "No ISO14443A NFC card found."
                                          : "NFC card polling failed: %s.",
                  esp_err_to_name(err));
    return err == ESP_ERR_TIMEOUT ? ESP_ERR_NOT_FOUND : err;
  }

  if (resp_len < 6 || resp[0] == 0) {
    s_target_active = false;
    s_report->target_present = false;
    report_detail("No ISO14443A NFC card found.");
    return ESP_ERR_NOT_FOUND;
  }

  uint8_t uid_len = resp[5];
  if (6U + uid_len > resp_len || uid_len > SMARTCARD_PN532_UID_MAX_LEN) {
    s_target_active = false;
    s_report->target_present = false;
    report_detail("NFC target response has invalid UID length.");
    return ESP_ERR_INVALID_RESPONSE;
  }

  s_target_number = resp[1] ? resp[1] : 1;
  s_target_active = true;
  s_report->target_present = true;
  s_report->target_number = s_target_number;
  s_report->atqa[0] = resp[2];
  s_report->atqa[1] = resp[3];
  s_report->sak = resp[4];
  s_report->iso_dep = (s_report->sak & 0x20) != 0;
  s_report->uid_len = uid_len;
  memcpy(s_report->uid, resp + 6, uid_len);

  report_detail("NFC card detected; UID length=%u SAK=0x%02X%s.",
                (unsigned)uid_len, s_report->sak,
                s_report->iso_dep ? " ISO-DEP" : "");
  return ESP_OK;
}

static bool pn532_probe_retryable_error(esp_err_t err) {
  return err == ESP_ERR_NOT_FOUND || err == ESP_ERR_TIMEOUT ||
         err == ESP_ERR_INVALID_STATE || err == ESP_ERR_INVALID_RESPONSE ||
         err == ESP_ERR_INVALID_SIZE;
}

static esp_err_t pn532_probe_target_locked(uint32_t timeout_ms) {
  const uint32_t total_timeout = default_timeout(timeout_ms);
  const TickType_t start = xTaskGetTickCount();
  const TickType_t timeout_ticks = pdMS_TO_TICKS(total_timeout);
  esp_err_t last_err = ESP_ERR_NOT_FOUND;

  s_target_active = false;
  s_report->target_present = false;
  s_report->target_number = 0;
  s_report->uid_len = 0;
  memset(s_report->uid, 0, sizeof(s_report->uid));
  s_report->atqa[0] = 0;
  s_report->atqa[1] = 0;
  s_report->sak = 0;
  s_report->iso_dep = false;

  do {
    uint32_t remaining_ms = total_timeout;
    TickType_t elapsed = xTaskGetTickCount() - start;
    if (elapsed < timeout_ticks)
      remaining_ms = pdTICKS_TO_MS(timeout_ticks - elapsed);
    if (remaining_ms > 1000U)
      remaining_ms = 1000U;
    if (remaining_ms == 0)
      remaining_ms = 1;

    esp_err_t err = pn532_poll_target_locked(remaining_ms);
    if (err == ESP_OK)
      return ESP_OK;

    last_err = err;
    if (!pn532_probe_retryable_error(err))
      return err;

    vTaskDelay(pdMS_TO_TICKS(120));
  } while ((xTaskGetTickCount() - start) <= timeout_ticks);

  if (last_err == ESP_ERR_INVALID_STATE) {
    report_detail("No NFC card settled after repeated polls; move card and retry.");
    return ESP_ERR_NOT_FOUND;
  }

  return last_err;
}

static esp_err_t pn532_ensure_target_locked(uint32_t timeout_ms) {
  if (s_target_active)
    return ESP_OK;
  return pn532_probe_target_locked(timeout_ms);
}

esp_err_t smartcard_pn532_start(void) {
  esp_err_t err = take_lock();
  if (err != ESP_OK)
    return err;
  err = pn532_start_locked();
  give_lock();
  return err;
}

esp_err_t smartcard_pn532_probe(uint32_t timeout_ms) {
  esp_err_t err = take_lock();
  if (err != ESP_OK)
    return err;

  err = pn532_start_locked();
  if (err == ESP_OK)
    err = pn532_probe_target_locked(timeout_ms);

  give_lock();
  return err;
}

esp_err_t smartcard_pn532_transmit_apdu(const uint8_t *apdu, size_t apdu_len,
                                        uint8_t *response,
                                        size_t response_cap,
                                        size_t *response_len, uint16_t *sw,
                                        uint32_t timeout_ms) {
  if (!apdu || apdu_len == 0 || apdu_len > SMARTCARD_CCID_APDU_MAX_LEN ||
      !response_len || !sw)
    return ESP_ERR_INVALID_ARG;

  *response_len = 0;
  *sw = 0;

  esp_err_t err = take_lock();
  if (err != ESP_OK)
    return err;

  err = pn532_start_locked();
  if (err != ESP_OK)
    goto done;

  err = pn532_ensure_target_locked(timeout_ms);
  if (err != ESP_OK)
    goto done;

  uint8_t cmd[PN532_COMMAND_MAX_LEN];
  if (2U + apdu_len > sizeof(cmd)) {
    err = ESP_ERR_INVALID_SIZE;
    goto done;
  }
  cmd[0] = PN532_CMD_INDATAEXCHANGE;
  cmd[1] = s_target_number ? s_target_number : 1;
  memcpy(cmd + 2, apdu, apdu_len);

  uint8_t resp[1 + SMARTCARD_CCID_RESPONSE_MAX_LEN];
  size_t resp_len = 0;
  err = pn532_command(cmd, 2U + apdu_len, resp, sizeof(resp), &resp_len,
                      default_timeout(timeout_ms));
  if (err != ESP_OK) {
    pn532_clear_target_report();
    if (err == ESP_ERR_INVALID_RESPONSE || err == ESP_ERR_INVALID_STATE)
      pn532_drop_device_locked();
    report_detail("NFC APDU exchange failed: %s.", esp_err_to_name(err));
    goto done;
  }

  if (resp_len < 3) {
    err = ESP_ERR_INVALID_RESPONSE;
    report_detail("NFC APDU response is too short.");
    goto done;
  }

  if (resp[0] != 0x00) {
    err = ESP_FAIL;
    pn532_clear_target_report();
    report_detail("PN532 InDataExchange status=0x%02X.", resp[0]);
    goto done;
  }

  size_t card_len = resp_len - 1U;
  *response_len = card_len;
  if (card_len > response_cap) {
    err = ESP_ERR_INVALID_SIZE;
    goto done;
  }

  if (response && card_len)
    memcpy(response, resp + 1, card_len);
  *sw = ((uint16_t)resp[resp_len - 2] << 8) | resp[resp_len - 1];
  report_detail("NFC APDU OK; response=%u SW=%04X.", (unsigned)card_len, *sw);

done:
  give_lock();
  return err;
}

void smartcard_pn532_snapshot(smartcard_pn532_report_t *out) {
  if (!out)
    return;
  esp_err_t err = take_lock();
  if (err != ESP_OK) {
    memset(out, 0, sizeof(*out));
    snprintf(out->detail, sizeof(out->detail), "PN532 snapshot lock failed: %s.",
             esp_err_to_name(err));
    return;
  }
  *out = *s_report;
  give_lock();
}

void smartcard_pn532_format_report(char *out, size_t out_len) {
  if (!out || out_len == 0)
    return;

  smartcard_pn532_report_t report;
  smartcard_pn532_snapshot(&report);

  char uid_hex[SMARTCARD_PN532_UID_MAX_LEN * 2 + 1] = {0};
  for (size_t i = 0; i < report.uid_len && i < SMARTCARD_PN532_UID_MAX_LEN;
       i++) {
    char part[4];
    snprintf(part, sizeof(part), "%02X", report.uid[i]);
    strncat(uid_hex, part, sizeof(uid_hex) - strlen(uid_hex) - 1);
  }

  snprintf(out, out_len,
           "Initialized: %s\nPN532 present: %s\nCard present: %s\nISO-DEP/APDU: %s\nTarget: %u\nUID: %s\nATQA: %02X%02X\nSAK: %02X\nDetail: %s",
           report.initialized ? "yes" : "no",
           report.pn532_present ? "yes" : "no",
           report.target_present ? "yes" : "no",
           report.iso_dep ? "yes" : "unknown/no", report.target_number,
           uid_hex[0] ? uid_hex : "-", report.atqa[0], report.atqa[1],
           report.sak, report.detail[0] ? report.detail : "-");
}

#endif
