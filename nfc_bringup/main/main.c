#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "NFC_BRINGUP";

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
#define PN532_CMD_INLISTPASSIVETARGET 0x4A

static const uint8_t PN532_ACK_FRAME[] = {0x00, 0x00, 0xFF,
                                          0x00, 0xFF, 0x00};

static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_pn532;

static bool pins_configured(void) {
  return CONFIG_NFC_BRINGUP_PIN_SDA >= 0 &&
         CONFIG_NFC_BRINGUP_PIN_SCL >= 0;
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
  const TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
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
  if (!cmd || cmd_len == 0 || cmd_len > 252)
    return ESP_ERR_INVALID_ARG;

  uint8_t frame[271];
  size_t pos = 0;
  frame[pos++] = PN532_I2C_FRAME_PREFIX;
  const size_t frame_start = pos;
  frame[pos++] = PN532_PREAMBLE;
  frame[pos++] = PN532_STARTCODE1;
  frame[pos++] = PN532_STARTCODE2;

  uint8_t len = (uint8_t)(cmd_len + 1);
  frame[pos++] = len;
  frame[pos++] = (uint8_t)(~len + 1);
  frame[pos++] = PN532_HOSTTOPN532;

  memcpy(frame + pos, cmd, cmd_len);
  pos += cmd_len;

  frame[pos++] = pn532_checksum(frame + frame_start + 5, cmd_len + 1);
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

  if ((rx[0] & PN532_I2C_READY) == 0)
    return ESP_ERR_INVALID_RESPONSE;
  if (memcmp(rx + 1, PN532_ACK_FRAME, sizeof(PN532_ACK_FRAME)) != 0)
    return ESP_ERR_INVALID_RESPONSE;

  return ESP_OK;
}

static esp_err_t pn532_read_response(uint8_t cmd, uint8_t *out, size_t out_cap,
                                     size_t *out_len, uint32_t timeout_ms) {
  esp_err_t err = pn532_wait_ready(timeout_ms);
  if (err != ESP_OK)
    return err;

  uint8_t rx[128] = {0};
  err = pn532_read_bytes(rx, sizeof(rx));
  if (err != ESP_OK)
    return err;

  if ((rx[0] & PN532_I2C_READY) == 0)
    return ESP_ERR_INVALID_RESPONSE;

  size_t off = 1;
  while (off + 6 < sizeof(rx) &&
         !(rx[off] == PN532_PREAMBLE &&
           rx[off + 1] == PN532_STARTCODE1 &&
           rx[off + 2] == PN532_STARTCODE2)) {
    off++;
  }

  if (off + 6 >= sizeof(rx) || rx[off] != PN532_PREAMBLE ||
      rx[off + 1] != PN532_STARTCODE1 || rx[off + 2] != PN532_STARTCODE2)
    return ESP_ERR_INVALID_RESPONSE;

  uint8_t len = rx[off + 3];
  uint8_t lcs = rx[off + 4];
  if ((uint8_t)(len + lcs) != 0 || len < 2)
    return ESP_ERR_INVALID_RESPONSE;

  if (off + 5U + len + 2U > sizeof(rx))
    return ESP_ERR_INVALID_SIZE;

  const uint8_t *payload = rx + off + 5;
  if (payload[0] != PN532_PN532TOHOST || payload[1] != (uint8_t)(cmd + 1))
    return ESP_ERR_INVALID_RESPONSE;

  uint8_t dcs = rx[off + 5 + len];
  uint8_t sum = dcs;
  for (uint8_t i = 0; i < len; i++)
    sum = (uint8_t)(sum + payload[i]);
  if (sum != 0)
    return ESP_ERR_INVALID_RESPONSE;

  size_t data_len = len - 2;
  if (data_len > out_cap)
    return ESP_ERR_INVALID_SIZE;

  if (out && data_len)
    memcpy(out, payload + 2, data_len);
  if (out_len)
    *out_len = data_len;

  return ESP_OK;
}

static esp_err_t pn532_command(const uint8_t *cmd, size_t cmd_len, uint8_t *out,
                               size_t out_cap, size_t *out_len,
                               uint32_t timeout_ms) {
  esp_err_t err = pn532_write_command(cmd, cmd_len);
  if (err != ESP_OK)
    return err;

  err = pn532_read_ack(1000);
  if (err != ESP_OK)
    return err;

  return pn532_read_response(cmd[0], out, out_cap, out_len, timeout_ms);
}

static esp_err_t i2c_bus_init(void) {
  if (!pins_configured()) {
    ESP_LOGE(TAG, "I2C GPIOs are not configured.");
    ESP_LOGE(TAG, "Current SDA=%d SCL=%d", CONFIG_NFC_BRINGUP_PIN_SDA,
             CONFIG_NFC_BRINGUP_PIN_SCL);
    return ESP_ERR_INVALID_STATE;
  }

  i2c_master_bus_config_t buscfg = {
      .i2c_port = CONFIG_NFC_BRINGUP_I2C_PORT,
      .sda_io_num = CONFIG_NFC_BRINGUP_PIN_SDA,
      .scl_io_num = CONFIG_NFC_BRINGUP_PIN_SCL,
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .glitch_ignore_cnt = 7,
      .flags.enable_internal_pullup = true,
  };
  ESP_RETURN_ON_ERROR(i2c_new_master_bus(&buscfg, &s_i2c_bus), TAG,
                      "i2c bus init failed");

  return ESP_OK;
}

static void i2c_scan_bus(void) {
  bool found_any = false;

  ESP_LOGI(TAG, "Scanning I2C bus...");
  for (uint8_t address = 0x03; address <= 0x77; address++) {
    esp_err_t err = i2c_master_probe(s_i2c_bus, address, 50);
    if (err == ESP_OK) {
      ESP_LOGI(TAG, "I2C device found at 7-bit address 0x%02X", address);
      found_any = true;
    }
  }

  if (!found_any) {
    ESP_LOGW(TAG, "No I2C devices found on SDA=%d SCL=%d",
             CONFIG_NFC_BRINGUP_PIN_SDA, CONFIG_NFC_BRINGUP_PIN_SCL);
  }
}

static esp_err_t pn532_init_i2c(void) {
  esp_err_t err = i2c_master_probe(s_i2c_bus, PN532_I2C_ADDRESS, 200);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "PN532 not found at I2C address 0x%02X: %s",
             PN532_I2C_ADDRESS, esp_err_to_name(err));
    ESP_LOGE(TAG, "For PN532 I2C, 0x24 is the 7-bit address. Some docs print "
                  "0x48 as the 8-bit write address.");
    return err;
  }

  i2c_device_config_t devcfg = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = PN532_I2C_ADDRESS,
      .scl_speed_hz = CONFIG_NFC_BRINGUP_I2C_HZ,
  };
  ESP_RETURN_ON_ERROR(
      i2c_master_bus_add_device(s_i2c_bus, &devcfg, &s_pn532), TAG,
      "i2c add device failed");

  uint8_t wake = 0x00;
  (void)i2c_master_transmit(s_pn532, &wake, sizeof(wake), 100);
  vTaskDelay(pdMS_TO_TICKS(100));
  return ESP_OK;
}

static esp_err_t pn532_read_firmware(void) {
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

static void pn532_sam_config(void) {
  uint8_t cmd[] = {PN532_CMD_SAMCONFIGURATION, 0x01, 0x14, 0x01};
  uint8_t resp[8];
  size_t resp_len = 0;
  esp_err_t err =
      pn532_command(cmd, sizeof(cmd), resp, sizeof(resp), &resp_len, 1000);
  if (err == ESP_OK)
    ESP_LOGI(TAG, "SAMConfiguration OK");
  else
    ESP_LOGW(TAG, "SAMConfiguration failed: %s", esp_err_to_name(err));
}

static void pn532_poll_card_once(void) {
  uint8_t cmd[] = {PN532_CMD_INLISTPASSIVETARGET, 0x01, 0x00};
  uint8_t resp[64];
  size_t resp_len = 0;
  esp_err_t err =
      pn532_command(cmd, sizeof(cmd), resp, sizeof(resp), &resp_len, 1000);
  if (err != ESP_OK) {
    if (err == ESP_ERR_TIMEOUT)
      ESP_LOGI(TAG, "No ISO14443A card.");
    else
      ESP_LOGW(TAG, "Card poll failed: %s", esp_err_to_name(err));
    return;
  }

  if (resp_len < 7 || resp[0] == 0) {
    ESP_LOGI(TAG, "No ISO14443A card.");
    return;
  }

  uint8_t uid_len = resp[5];
  if (6U + uid_len > resp_len || uid_len > 10) {
    ESP_LOGW(TAG, "Card response has invalid UID length: %u", uid_len);
    return;
  }

  char uid_hex[32] = {0};
  for (uint8_t i = 0; i < uid_len; i++) {
    char part[4];
    snprintf(part, sizeof(part), "%02X", resp[6 + i]);
    strncat(uid_hex, part, sizeof(uid_hex) - strlen(uid_hex) - 1);
  }

  ESP_LOGI(TAG, "Card detected: UID=%s ATQA=%02X%02X SAK=%02X", uid_hex,
           resp[2], resp[3], resp[4]);
}

void app_main(void) {
  ESP_LOGI(TAG, "KernSigner NFC PN532 I2C bring-up");
  ESP_LOGI(TAG, "I2C port=%d SDA=%d SCL=%d Hz=%d address=0x%02X",
           CONFIG_NFC_BRINGUP_I2C_PORT, CONFIG_NFC_BRINGUP_PIN_SDA,
           CONFIG_NFC_BRINGUP_PIN_SCL, CONFIG_NFC_BRINGUP_I2C_HZ,
           PN532_I2C_ADDRESS);

  esp_err_t err = i2c_bus_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "I2C bus init failed: %s", esp_err_to_name(err));
    return;
  }

  i2c_scan_bus();

  err = pn532_init_i2c();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "PN532 I2C init failed: %s", esp_err_to_name(err));
    ESP_LOGE(TAG, "Check PN532 I2C mode, 3.3V, GND, SDA, and SCL.");
    return;
  }

  err = pn532_read_firmware();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "PN532 firmware read failed: %s", esp_err_to_name(err));
    ESP_LOGE(TAG, "Check I2C mode, 3.3V, GND, SDA, and SCL.");
    return;
  }

  pn532_sam_config();
  ESP_LOGI(TAG, "Waiting for ISO14443A card...");

  while (true) {
    pn532_poll_card_once();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
