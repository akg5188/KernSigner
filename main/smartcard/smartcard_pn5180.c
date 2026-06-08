#include "smartcard_pn5180.h"

#include "smartcard_ccid.h"

#ifdef SIMULATOR

#include <stdio.h>
#include <string.h>

static smartcard_pn5180_report_t s_report = {
    .detail = "Simulator does not access PN5180 NFC hardware.",
};

esp_err_t smartcard_pn5180_start(void) { return ESP_ERR_NOT_SUPPORTED; }

esp_err_t smartcard_pn5180_probe(uint32_t timeout_ms) {
  (void)timeout_ms;
  snprintf(s_report.detail, sizeof(s_report.detail),
           "Simulator does not access PN5180 NFC hardware.");
  return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t smartcard_pn5180_transmit_apdu(const uint8_t *apdu, size_t apdu_len,
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

void smartcard_pn5180_snapshot(smartcard_pn5180_report_t *out) {
  if (out)
    *out = s_report;
}

void smartcard_pn5180_format_report(char *out, size_t out_len) {
  if (!out || out_len == 0)
    return;
  snprintf(out, out_len, "%s", s_report.detail);
}

#else

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#ifndef ESP_ERR_INVALID_RESPONSE
#define ESP_ERR_INVALID_RESPONSE ESP_ERR_INVALID_STATE
#endif

#ifndef CONFIG_KSIG_PN5180_ENABLED
#define CONFIG_KSIG_PN5180_ENABLED 0
#endif

#if !CONFIG_KSIG_PN5180_ENABLED

static smartcard_pn5180_report_t s_report = {
    .detail = "PN5180 NFC support is disabled in this build.",
};

esp_err_t smartcard_pn5180_start(void) { return ESP_ERR_NOT_SUPPORTED; }

esp_err_t smartcard_pn5180_probe(uint32_t timeout_ms) {
  (void)timeout_ms;
  snprintf(s_report.detail, sizeof(s_report.detail),
           "PN5180 NFC support is disabled in this build.");
  return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t smartcard_pn5180_transmit_apdu(const uint8_t *apdu, size_t apdu_len,
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

void smartcard_pn5180_snapshot(smartcard_pn5180_report_t *out) {
  if (out)
    *out = s_report;
}

void smartcard_pn5180_format_report(char *out, size_t out_len) {
  if (!out || out_len == 0)
    return;
  snprintf(out, out_len, "%s", s_report.detail);
}

#else

#ifndef CONFIG_KSIG_PN5180_SPI_HZ
#define CONFIG_KSIG_PN5180_SPI_HZ 1000000
#endif

#ifndef CONFIG_KSIG_PN5180_PIN_SCK
#define CONFIG_KSIG_PN5180_PIN_SCK -1
#endif

#ifndef CONFIG_KSIG_PN5180_PIN_MOSI
#define CONFIG_KSIG_PN5180_PIN_MOSI -1
#endif

#ifndef CONFIG_KSIG_PN5180_PIN_MISO
#define CONFIG_KSIG_PN5180_PIN_MISO -1
#endif

#ifndef CONFIG_KSIG_PN5180_PIN_NSS
#define CONFIG_KSIG_PN5180_PIN_NSS -1
#endif

#ifndef CONFIG_KSIG_PN5180_PIN_BUSY
#define CONFIG_KSIG_PN5180_PIN_BUSY -1
#endif

#ifndef CONFIG_KSIG_PN5180_PIN_RST
#define CONFIG_KSIG_PN5180_PIN_RST -1
#endif

#ifndef CONFIG_KSIG_PN5180_PIN_IRQ
#define CONFIG_KSIG_PN5180_PIN_IRQ -1
#endif

static const char *TAG = "KSIG_PN5180";

#define PN5180_SPI_HOST SPI2_HOST
#define PN5180_CMD_WRITE_REGISTER 0x00
#define PN5180_CMD_WRITE_REGISTER_OR_MASK 0x01
#define PN5180_CMD_WRITE_REGISTER_AND_MASK 0x02
#define PN5180_CMD_READ_REGISTER 0x04
#define PN5180_CMD_READ_EEPROM 0x07
#define PN5180_CMD_SEND_DATA 0x09
#define PN5180_CMD_READ_DATA 0x0A
#define PN5180_CMD_LOAD_RF_CONFIG 0x11
#define PN5180_CMD_RF_ON 0x16
#define PN5180_CMD_RF_OFF 0x17
#define PN5180_EEPROM_VERSION_ADDR 0x00
#define PN5180_EEPROM_VERSION_LEN 0x16

#define PN5180_REG_SYSTEM_CONFIG 0x00
#define PN5180_REG_IRQ_STATUS 0x02
#define PN5180_REG_IRQ_CLEAR 0x03
#define PN5180_REG_TRANSCEIVE_CONTROL 0x04
#define PN5180_REG_CRC_RX_CONFIG 0x12
#define PN5180_REG_RX_STATUS 0x13
#define PN5180_REG_TX_CONFIG 0x18
#define PN5180_REG_CRC_TX_CONFIG 0x19
#define PN5180_REG_RF_STATUS 0x1D

#define PN5180_IRQ_RX BIT(0)
#define PN5180_IRQ_TX_RFON BIT(9)
#define PN5180_IRQ_RF_ACTIVE_ERROR BIT(10)
#define PN5180_IRQ_TEMPSENS_ERROR BIT(16)
#define PN5180_IRQ_GENERAL_ERROR BIT(17)
#define PN5180_IRQ_HV_ERROR BIT(18)
#define PN5180_IRQ_ALL_STATUS 0x000FFFFFU

#define PN5180_SYSTEM_COMMAND_MASK 0x00000007U
#define PN5180_SYSTEM_COMMAND_IDLE 0x00000000U
#define PN5180_SYSTEM_COMMAND_TRANSCEIVE 0x00000003U

#define PN5180_TRANSCEIVE_INITIATOR BIT(0)
#define PN5180_RX_NUM_BYTES_MASK 0x000001FFU
#define PN5180_TX_DATA_ENABLE BIT(10)
#define PN5180_TX_PARITY_ENABLE BIT(11)
#define PN5180_RF_STATUS_TX_RF_STATUS BIT(17)

#define PN5180_RF_CONFIG_TX_ISO14443A_106 0x00
#define PN5180_RF_CONFIG_RX_ISO14443A_106 0x80
#define PN5180_RF_ON_DISABLE_COLLISION_AVOIDANCE BIT(0)

#define ISO14443A_CMD_REQA 0x26
#define ISO14443A_CMD_WUPA 0x52
#define ISO14443A_SEL_CL1 0x93
#define ISO14443A_SEL_CL2 0x95
#define ISO14443A_SEL_CL3 0x97
#define ISO14443A_NVB_ANTICOLLISION 0x20
#define ISO14443A_NVB_SELECT 0x70
#define ISO14443A_CASCADE_TAG 0x88
#define ISO14443A_SAK_CASCADE BIT(2)
#define ISO14443A_SAK_ISO_DEP BIT(5)
#define ISO14443A_CMD_RATS 0xE0
#define ISO14443A_RATS_FSDI_256 0x80

#define ISO14443_4_I_BLOCK_BASE 0x02
#define ISO14443_4_I_BLOCK_MASK 0xC0
#define ISO14443_4_I_BLOCK_TYPE 0x00
#define ISO14443_4_R_BLOCK_TYPE 0x80
#define ISO14443_4_S_BLOCK_TYPE 0xC0
#define ISO14443_4_I_BLOCK_CHAINING BIT(4)
#define ISO14443_4_R_ACK_BASE 0xA2
#define ISO14443_4_S_WTX 0xF2
#define ISO14443_4_S_CID_PRESENT BIT(3)

#define PN5180_DEFAULT_TIMEOUT_MS 30000U
#define PN5180_FRAME_MAX_LEN 300U
#define PN5180_ISO_DEP_MAX_FOLLOWUP_FRAMES 512U

typedef struct {
  uint8_t atqa[2];
  uint8_t uid[SMARTCARD_PN5180_UID_MAX_LEN];
  size_t uid_len;
  uint8_t sak;
  uint8_t ats[SMARTCARD_PN5180_ATS_MAX_LEN];
  size_t ats_len;
} iso14443a_card_t;

typedef struct {
  uint8_t block_number;
} iso14443_4_session_t;

static SemaphoreHandle_t s_lock;
static spi_device_handle_t s_pn5180;
static bool s_spi_bus_ready;
static bool s_own_spi_bus;
static bool s_started;
static bool s_target_active;
static bool s_next_poll_uses_wupa;
static iso14443_4_session_t s_session;
static smartcard_pn5180_report_t *s_report;

static uint32_t default_timeout(uint32_t timeout_ms) {
  return timeout_ms ? timeout_ms : PN5180_DEFAULT_TIMEOUT_MS;
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

static void format_hex_compact(const uint8_t *data, size_t len, char *out,
                               size_t out_len) {
  if (!out || out_len == 0)
    return;
  out[0] = '\0';
  if (!data)
    return;
  for (size_t i = 0; i < len; i++) {
    char part[4];
    snprintf(part, sizeof(part), "%02X", data[i]);
    strncat(out, part, out_len - strlen(out) - 1);
  }
}

static bool pins_configured(void) {
  return CONFIG_KSIG_PN5180_PIN_SCK >= 0 &&
         CONFIG_KSIG_PN5180_PIN_MOSI >= 0 &&
         CONFIG_KSIG_PN5180_PIN_MISO >= 0 &&
         CONFIG_KSIG_PN5180_PIN_NSS >= 0 &&
         CONFIG_KSIG_PN5180_PIN_BUSY >= 0 &&
         CONFIG_KSIG_PN5180_PIN_RST >= 0;
}

static void clear_target_report(void) {
  s_target_active = false;
  memset(&s_session, 0, sizeof(s_session));
  s_report->target_present = false;
  s_report->iso_dep = false;
  s_report->uid_len = 0;
  memset(s_report->uid, 0, sizeof(s_report->uid));
  s_report->atqa[0] = 0;
  s_report->atqa[1] = 0;
  s_report->sak = 0;
  s_report->ats_len = 0;
  memset(s_report->ats, 0, sizeof(s_report->ats));
}

static void drop_device_locked(void) {
  if (s_pn5180) {
    spi_bus_remove_device(s_pn5180);
    s_pn5180 = NULL;
  }
  if (s_own_spi_bus && s_spi_bus_ready) {
    spi_bus_free(PN5180_SPI_HOST);
  }
  s_spi_bus_ready = false;
  s_own_spi_bus = false;
  s_started = false;
  s_report->initialized = false;
  s_report->pn5180_present = false;
  s_report->field_on = false;
  clear_target_report();
}

static esp_err_t wait_busy_level(int level, uint32_t timeout_us) {
  const int64_t start_us = esp_timer_get_time();

  while ((esp_timer_get_time() - start_us) <= timeout_us) {
    if (gpio_get_level(CONFIG_KSIG_PN5180_PIN_BUSY) == level)
      return ESP_OK;
    esp_rom_delay_us(10);
  }

  return ESP_ERR_TIMEOUT;
}

static esp_err_t wait_busy_low(uint32_t timeout_ms) {
  return wait_busy_level(0, timeout_ms * 1000U);
}

static esp_err_t gpio_init_locked(void) {
  gpio_config_t input_cfg = {
      .pin_bit_mask = BIT64(CONFIG_KSIG_PN5180_PIN_BUSY),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
#if CONFIG_KSIG_PN5180_PIN_IRQ >= 0
  input_cfg.pin_bit_mask |= BIT64(CONFIG_KSIG_PN5180_PIN_IRQ);
#endif
  ESP_RETURN_ON_ERROR(gpio_config(&input_cfg), TAG, "PN5180 input GPIO init");

  gpio_config_t rst_cfg = {
      .pin_bit_mask = BIT64(CONFIG_KSIG_PN5180_PIN_RST),
      .mode = GPIO_MODE_INPUT_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  ESP_RETURN_ON_ERROR(gpio_config(&rst_cfg), TAG, "PN5180 reset GPIO init");
  return gpio_set_level(CONFIG_KSIG_PN5180_PIN_RST, 1);
}

static esp_err_t reset_locked(void) {
  ESP_RETURN_ON_ERROR(gpio_set_level(CONFIG_KSIG_PN5180_PIN_RST, 0), TAG,
                      "PN5180 reset low");
  vTaskDelay(pdMS_TO_TICKS(20));
  ESP_RETURN_ON_ERROR(gpio_set_level(CONFIG_KSIG_PN5180_PIN_RST, 1), TAG,
                      "PN5180 reset high");
  vTaskDelay(pdMS_TO_TICKS(20));
  return wait_busy_low(500);
}

static esp_err_t spi_init_locked(void) {
  if (!s_spi_bus_ready) {
    spi_bus_config_t buscfg = {
        .mosi_io_num = CONFIG_KSIG_PN5180_PIN_MOSI,
        .miso_io_num = CONFIG_KSIG_PN5180_PIN_MISO,
        .sclk_io_num = CONFIG_KSIG_PN5180_PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = PN5180_FRAME_MAX_LEN + 8,
    };
    esp_err_t err =
        spi_bus_initialize(PN5180_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err == ESP_OK) {
      s_spi_bus_ready = true;
      s_own_spi_bus = true;
    } else if (err == ESP_ERR_INVALID_STATE) {
      s_spi_bus_ready = true;
      s_own_spi_bus = false;
      ESP_LOGW(TAG, "SPI%d bus already initialized; adding PN5180 device",
               PN5180_SPI_HOST);
    } else {
      return err;
    }
  }

  if (!s_pn5180) {
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = CONFIG_KSIG_PN5180_SPI_HZ,
        .mode = 0,
        .spics_io_num = CONFIG_KSIG_PN5180_PIN_NSS,
        .queue_size = 1,
    };
    ESP_RETURN_ON_ERROR(
        spi_bus_add_device(PN5180_SPI_HOST, &devcfg, &s_pn5180),
        TAG, "SPI add PN5180");
  }
  return ESP_OK;
}

static esp_err_t spi_write_locked(const uint8_t *tx, size_t len) {
  if (!tx || len == 0 || len > PN5180_FRAME_MAX_LEN + 2)
    return ESP_ERR_INVALID_ARG;

  ESP_RETURN_ON_ERROR(wait_busy_low(500), TAG, "BUSY before write");
  spi_transaction_t trans = {
      .length = len * 8,
      .tx_buffer = tx,
  };
  ESP_RETURN_ON_ERROR(spi_device_transmit(s_pn5180, &trans), TAG,
                      "SPI write");
  return wait_busy_low(500);
}

static esp_err_t spi_write_no_response_locked(const uint8_t *tx, size_t len) {
  esp_err_t err = spi_write_locked(tx, len);
  if (err == ESP_OK)
    vTaskDelay(pdMS_TO_TICKS(2));
  return err;
}

static esp_err_t spi_read_locked(uint8_t *rx, size_t len) {
  if (!rx || len == 0 || len > PN5180_FRAME_MAX_LEN)
    return ESP_ERR_INVALID_ARG;

  uint8_t dummy[PN5180_FRAME_MAX_LEN] = {0};
  ESP_RETURN_ON_ERROR(wait_busy_low(500), TAG, "BUSY before read");
  spi_transaction_t trans = {
      .length = len * 8,
      .tx_buffer = dummy,
      .rx_buffer = rx,
  };
  ESP_RETURN_ON_ERROR(spi_device_transmit(s_pn5180, &trans), TAG, "SPI read");
  return wait_busy_low(500);
}

static esp_err_t read_eeprom_locked(uint8_t addr, uint8_t len, uint8_t *out) {
  uint8_t cmd[] = {PN5180_CMD_READ_EEPROM, addr, len};
  ESP_RETURN_ON_ERROR(spi_write_locked(cmd, sizeof(cmd)), TAG,
                      "READ_EEPROM command");
  return spi_read_locked(out, len);
}

static void put_u32_le(uint8_t *out, uint32_t value) {
  out[0] = (uint8_t)(value & 0xFFU);
  out[1] = (uint8_t)((value >> 8) & 0xFFU);
  out[2] = (uint8_t)((value >> 16) & 0xFFU);
  out[3] = (uint8_t)((value >> 24) & 0xFFU);
}

static uint32_t get_u32_le(const uint8_t *in) {
  return ((uint32_t)in[0]) | ((uint32_t)in[1] << 8) |
         ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
}

static esp_err_t write_register_locked(uint8_t reg, uint32_t value) {
  uint8_t cmd[6] = {PN5180_CMD_WRITE_REGISTER, reg};
  put_u32_le(&cmd[2], value);
  return spi_write_no_response_locked(cmd, sizeof(cmd));
}

static esp_err_t write_register_or_mask_locked(uint8_t reg, uint32_t mask) {
  uint8_t cmd[6] = {PN5180_CMD_WRITE_REGISTER_OR_MASK, reg};
  put_u32_le(&cmd[2], mask);
  return spi_write_no_response_locked(cmd, sizeof(cmd));
}

static esp_err_t write_register_and_mask_locked(uint8_t reg, uint32_t mask) {
  uint8_t cmd[6] = {PN5180_CMD_WRITE_REGISTER_AND_MASK, reg};
  put_u32_le(&cmd[2], mask);
  return spi_write_no_response_locked(cmd, sizeof(cmd));
}

static esp_err_t read_register_locked(uint8_t reg, uint32_t *value) {
  if (!value)
    return ESP_ERR_INVALID_ARG;

  uint8_t cmd[] = {PN5180_CMD_READ_REGISTER, reg};
  uint8_t rx[4] = {0};
  ESP_RETURN_ON_ERROR(spi_write_locked(cmd, sizeof(cmd)), TAG,
                      "READ_REGISTER command");
  ESP_RETURN_ON_ERROR(spi_read_locked(rx, sizeof(rx)), TAG,
                      "READ_REGISTER data");
  *value = get_u32_le(rx);
  return ESP_OK;
}

static esp_err_t set_system_command_locked(uint32_t command) {
  ESP_RETURN_ON_ERROR(
      write_register_and_mask_locked(PN5180_REG_SYSTEM_CONFIG,
                                     ~PN5180_SYSTEM_COMMAND_MASK),
      TAG, "Clear SYSTEM_CONFIG.COMMAND");
  if (command != PN5180_SYSTEM_COMMAND_IDLE) {
    ESP_RETURN_ON_ERROR(
        write_register_or_mask_locked(PN5180_REG_SYSTEM_CONFIG, command), TAG,
        "Set SYSTEM_CONFIG.COMMAND");
  }
  return ESP_OK;
}

static esp_err_t clear_irqs_locked(void) {
  return write_register_locked(PN5180_REG_IRQ_CLEAR, PN5180_IRQ_ALL_STATUS);
}

static esp_err_t load_rf_config_locked(void) {
  uint8_t cmd[] = {PN5180_CMD_LOAD_RF_CONFIG,
                   PN5180_RF_CONFIG_TX_ISO14443A_106,
                   PN5180_RF_CONFIG_RX_ISO14443A_106};
  return spi_write_no_response_locked(cmd, sizeof(cmd));
}

static esp_err_t rf_on_locked(void) {
  uint8_t cmd[] = {PN5180_CMD_RF_ON,
                   PN5180_RF_ON_DISABLE_COLLISION_AVOIDANCE};
  return spi_write_no_response_locked(cmd, sizeof(cmd));
}

static esp_err_t rf_off_locked(void) {
  uint8_t cmd[] = {PN5180_CMD_RF_OFF, 0x00};
  return spi_write_no_response_locked(cmd, sizeof(cmd));
}

static esp_err_t wait_rf_field_on_locked(void) {
  uint32_t rf_status = 0;
  uint32_t irq_status = 0;

  for (int i = 0; i < 10; i++) {
    vTaskDelay(pdMS_TO_TICKS(i == 0 ? 20 : 100));
    ESP_RETURN_ON_ERROR(read_register_locked(PN5180_REG_RF_STATUS, &rf_status),
                        TAG, "Read RF_STATUS");
    ESP_RETURN_ON_ERROR(
        read_register_locked(PN5180_REG_IRQ_STATUS, &irq_status), TAG,
        "Read IRQ_STATUS");
    if (rf_status & PN5180_RF_STATUS_TX_RF_STATUS) {
      s_report->field_on = true;
      return ESP_OK;
    }
    if (irq_status & (PN5180_IRQ_RF_ACTIVE_ERROR | PN5180_IRQ_TEMPSENS_ERROR |
                      PN5180_IRQ_HV_ERROR | PN5180_IRQ_GENERAL_ERROR)) {
      break;
    }
  }

  s_report->field_on = false;
  ESP_LOGW(TAG,
           "PN5180 RF field not active: RF_STATUS=0x%08" PRIX32
           " IRQ_STATUS=0x%08" PRIX32,
           rf_status, irq_status);
  return ESP_ERR_INVALID_STATE;
}

static esp_err_t restart_rf_field_locked(void) {
  ESP_RETURN_ON_ERROR(set_system_command_locked(PN5180_SYSTEM_COMMAND_IDLE),
                      TAG, "Idle before RF restart");
  ESP_RETURN_ON_ERROR(rf_off_locked(), TAG, "RF_OFF restart");
  vTaskDelay(pdMS_TO_TICKS(20));
  ESP_RETURN_ON_ERROR(clear_irqs_locked(), TAG,
                      "Clear IRQs before RF restart");
  ESP_RETURN_ON_ERROR(rf_on_locked(), TAG, "RF_ON restart");
  s_next_poll_uses_wupa = true;
  return wait_rf_field_on_locked();
}

static esp_err_t send_data_locked(const uint8_t *data, size_t len,
                                  uint8_t valid_last_bits) {
  if (!data || len == 0 || len > PN5180_FRAME_MAX_LEN || valid_last_bits > 7)
    return ESP_ERR_INVALID_ARG;

  uint8_t cmd[PN5180_FRAME_MAX_LEN + 2] = {PN5180_CMD_SEND_DATA,
                                           valid_last_bits};
  memcpy(&cmd[2], data, len);
  return spi_write_no_response_locked(cmd, len + 2);
}

static esp_err_t read_data_locked(uint8_t *rx, size_t len) {
  if (!rx || len == 0 || len > PN5180_FRAME_MAX_LEN)
    return ESP_ERR_INVALID_ARG;

  uint8_t cmd[] = {PN5180_CMD_READ_DATA, 0x00};
  ESP_RETURN_ON_ERROR(spi_write_locked(cmd, sizeof(cmd)), TAG,
                      "READ_DATA command");
  return spi_read_locked(rx, len);
}

static esp_err_t set_tx_parity_locked(bool enabled) {
  if (enabled) {
    return write_register_or_mask_locked(PN5180_REG_TX_CONFIG,
                                         PN5180_TX_PARITY_ENABLE);
  }
  return write_register_and_mask_locked(PN5180_REG_TX_CONFIG,
                                        ~PN5180_TX_PARITY_ENABLE);
}

static esp_err_t set_crc_locked(bool enabled) {
  if (enabled) {
    ESP_RETURN_ON_ERROR(
        write_register_or_mask_locked(PN5180_REG_CRC_TX_CONFIG, 1U), TAG,
        "Enable TX CRC");
    return write_register_or_mask_locked(PN5180_REG_CRC_RX_CONFIG, 1U);
  }

  ESP_RETURN_ON_ERROR(
      write_register_and_mask_locked(PN5180_REG_CRC_TX_CONFIG, ~1U), TAG,
      "Disable TX CRC");
  return write_register_and_mask_locked(PN5180_REG_CRC_RX_CONFIG, ~1U);
}

static esp_err_t config_typea_short_frame_locked(void) {
  ESP_RETURN_ON_ERROR(set_crc_locked(false), TAG, "Disable CRC short frame");
  ESP_RETURN_ON_ERROR(set_tx_parity_locked(false), TAG,
                      "Disable parity short frame");
  return write_register_or_mask_locked(PN5180_REG_TX_CONFIG,
                                       PN5180_TX_DATA_ENABLE);
}

static esp_err_t config_typea_no_crc_frame_locked(void) {
  ESP_RETURN_ON_ERROR(set_crc_locked(false), TAG, "Disable CRC frame");
  ESP_RETURN_ON_ERROR(set_tx_parity_locked(true), TAG, "Enable parity frame");
  return write_register_or_mask_locked(PN5180_REG_TX_CONFIG,
                                       PN5180_TX_DATA_ENABLE);
}

static esp_err_t config_typea_crc_frame_locked(void) {
  ESP_RETURN_ON_ERROR(set_crc_locked(true), TAG, "Enable CRC frame");
  ESP_RETURN_ON_ERROR(set_tx_parity_locked(true), TAG, "Enable parity frame");
  return write_register_or_mask_locked(PN5180_REG_TX_CONFIG,
                                       PN5180_TX_DATA_ENABLE);
}

static esp_err_t transceive_frame_locked(const uint8_t *tx, size_t tx_len,
                                         uint8_t valid_last_bits, uint8_t *rx,
                                         size_t rx_size, size_t *out_len,
                                         uint32_t timeout_ms) {
  if (!tx || tx_len == 0 || !rx || !out_len || rx_size == 0)
    return ESP_ERR_INVALID_ARG;
  *out_len = 0;

  ESP_RETURN_ON_ERROR(clear_irqs_locked(), TAG, "Clear IRQs before frame");
  ESP_RETURN_ON_ERROR(set_system_command_locked(PN5180_SYSTEM_COMMAND_IDLE),
                      TAG, "Idle before frame");
  ESP_RETURN_ON_ERROR(
      write_register_or_mask_locked(PN5180_REG_TRANSCEIVE_CONTROL,
                                    PN5180_TRANSCEIVE_INITIATOR),
      TAG, "Set initiator before frame");
  ESP_RETURN_ON_ERROR(
      set_system_command_locked(PN5180_SYSTEM_COMMAND_TRANSCEIVE), TAG,
      "Set transceive before frame");
  ESP_RETURN_ON_ERROR(send_data_locked(tx, tx_len, valid_last_bits), TAG,
                      "SEND_DATA frame");

  const TickType_t start = xTaskGetTickCount();
  const TickType_t timeout = pdMS_TO_TICKS(timeout_ms);
  uint32_t irq_status = 0;
  uint32_t rx_status = 0;
  size_t rx_len = 0;
  bool have_rx = false;

  do {
    ESP_RETURN_ON_ERROR(read_register_locked(PN5180_REG_IRQ_STATUS,
                                             &irq_status),
                        TAG, "Read IRQ_STATUS");
    if (irq_status & PN5180_IRQ_GENERAL_ERROR)
      break;
    if (irq_status & PN5180_IRQ_RX) {
      ESP_RETURN_ON_ERROR(read_register_locked(PN5180_REG_RX_STATUS,
                                               &rx_status),
                          TAG, "Read RX_STATUS");
      rx_len = rx_status & PN5180_RX_NUM_BYTES_MASK;
      if (rx_len > 0 && rx_status != UINT32_MAX) {
        have_rx = true;
        break;
      }
      (void)clear_irqs_locked();
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  } while ((xTaskGetTickCount() - start) <= timeout);

  ESP_RETURN_ON_ERROR(set_system_command_locked(PN5180_SYSTEM_COMMAND_IDLE),
                      TAG, "Idle after frame");

  if (!have_rx)
    return ESP_ERR_TIMEOUT;

  if (rx_len > rx_size)
    return ESP_ERR_INVALID_SIZE;

  ESP_RETURN_ON_ERROR(read_data_locked(rx, rx_len), TAG, "READ_DATA");
  *out_len = rx_len;
  return ESP_OK;
}

static esp_err_t iso14443a_select_level_locked(uint8_t sel_cmd, uint8_t *uid,
                                               size_t *uid_len,
                                               uint8_t *sak) {
  if (!uid || !uid_len || !sak || *uid_len > SMARTCARD_PN5180_UID_MAX_LEN)
    return ESP_ERR_INVALID_ARG;

  uint8_t rx[16] = {0};
  size_t rx_len = 0;
  uint8_t anticoll[] = {sel_cmd, ISO14443A_NVB_ANTICOLLISION};

  ESP_RETURN_ON_ERROR(config_typea_no_crc_frame_locked(), TAG,
                      "Configure anticollision frame");
  ESP_RETURN_ON_ERROR(
      transceive_frame_locked(anticoll, sizeof(anticoll), 0, rx, sizeof(rx),
                              &rx_len, 120),
      TAG, "Anticollision frame");
  if (rx_len < 5)
    return ESP_ERR_INVALID_RESPONSE;

  const uint8_t bcc = rx[0] ^ rx[1] ^ rx[2] ^ rx[3];
  if (bcc != rx[4])
    return ESP_ERR_INVALID_CRC;

  uint8_t select[] = {sel_cmd,
                      ISO14443A_NVB_SELECT,
                      rx[0],
                      rx[1],
                      rx[2],
                      rx[3],
                      rx[4]};
  uint8_t sak_rx[8] = {0};
  size_t sak_len = 0;

  ESP_RETURN_ON_ERROR(config_typea_crc_frame_locked(), TAG,
                      "Configure select frame");
  ESP_RETURN_ON_ERROR(
      transceive_frame_locked(select, sizeof(select), 0, sak_rx,
                              sizeof(sak_rx), &sak_len, 120),
      TAG, "Select frame");
  if (sak_len < 1)
    return ESP_ERR_INVALID_RESPONSE;

  const size_t append_len = (rx[0] == ISO14443A_CASCADE_TAG) ? 3 : 4;
  const size_t append_start = (rx[0] == ISO14443A_CASCADE_TAG) ? 1 : 0;
  if (*uid_len + append_len > SMARTCARD_PN5180_UID_MAX_LEN)
    return ESP_ERR_INVALID_SIZE;

  memcpy(&uid[*uid_len], &rx[append_start], append_len);
  *uid_len += append_len;
  *sak = sak_rx[0];
  return ESP_OK;
}

static esp_err_t iso14443a_read_uid_locked(iso14443a_card_t *card) {
  if (!card)
    return ESP_ERR_INVALID_ARG;

  uint8_t uid[SMARTCARD_PN5180_UID_MAX_LEN] = {0};
  size_t uid_len = 0;
  uint8_t sak = 0;

  ESP_RETURN_ON_ERROR(iso14443a_select_level_locked(
                          ISO14443A_SEL_CL1, uid, &uid_len, &sak),
                      TAG, "Select cascade level 1");
  if (sak & ISO14443A_SAK_CASCADE) {
    ESP_RETURN_ON_ERROR(iso14443a_select_level_locked(
                            ISO14443A_SEL_CL2, uid, &uid_len, &sak),
                        TAG, "Select cascade level 2");
  }
  if (sak & ISO14443A_SAK_CASCADE) {
    ESP_RETURN_ON_ERROR(iso14443a_select_level_locked(
                            ISO14443A_SEL_CL3, uid, &uid_len, &sak),
                        TAG, "Select cascade level 3");
  }

  memcpy(card->uid, uid, uid_len);
  card->uid_len = uid_len;
  card->sak = sak;
  return ESP_OK;
}

static esp_err_t iso14443_4_rats_locked(iso14443a_card_t *card) {
  if (!card)
    return ESP_ERR_INVALID_ARG;
  if ((card->sak & ISO14443A_SAK_ISO_DEP) == 0)
    return ESP_ERR_NOT_SUPPORTED;

  uint8_t rats[] = {ISO14443A_CMD_RATS, ISO14443A_RATS_FSDI_256};
  uint8_t ats[SMARTCARD_PN5180_ATS_MAX_LEN] = {0};
  size_t ats_len = 0;

  ESP_RETURN_ON_ERROR(config_typea_crc_frame_locked(), TAG,
                      "Configure RATS frame");
  ESP_RETURN_ON_ERROR(transceive_frame_locked(rats, sizeof(rats), 0, ats,
                                              sizeof(ats), &ats_len, 300),
                      TAG, "RATS frame");
  if (ats_len < 2)
    return ESP_ERR_INVALID_RESPONSE;

  memcpy(card->ats, ats, ats_len);
  card->ats_len = ats_len;
  return ESP_OK;
}

static esp_err_t poll_short_frame_locked(uint8_t short_frame, uint8_t *rx,
                                         size_t rx_size, size_t *out_len) {
  ESP_RETURN_ON_ERROR(config_typea_short_frame_locked(), TAG,
                      "Configure short frame");
  return transceive_frame_locked(&short_frame, 1, 7, rx, rx_size, out_len,
                                 250);
}

static esp_err_t poll_target_once_locked(void) {
  uint8_t rx[16] = {0};
  size_t rx_len = 0;
  const uint8_t short_frame =
      s_next_poll_uses_wupa ? ISO14443A_CMD_WUPA : ISO14443A_CMD_REQA;
  s_next_poll_uses_wupa = !s_next_poll_uses_wupa;

  esp_err_t err =
      poll_short_frame_locked(short_frame, rx, sizeof(rx), &rx_len);
  if (err == ESP_ERR_TIMEOUT)
    return ESP_ERR_NOT_FOUND;
  if (err != ESP_OK)
    return err;
  if (rx_len != 2)
    return ESP_ERR_INVALID_RESPONSE;

  iso14443a_card_t card = {
      .atqa = {rx[0], rx[1]},
  };
  ESP_RETURN_ON_ERROR(iso14443a_read_uid_locked(&card), TAG,
                      "ISO14443A UID/SAK read");
  ESP_RETURN_ON_ERROR(iso14443_4_rats_locked(&card), TAG,
                      "ISO14443-4 RATS");

  clear_target_report();
  s_target_active = true;
  s_report->target_present = true;
  s_report->iso_dep = true;
  s_report->atqa[0] = card.atqa[0];
  s_report->atqa[1] = card.atqa[1];
  s_report->sak = card.sak;
  s_report->uid_len = card.uid_len;
  memcpy(s_report->uid, card.uid, card.uid_len);
  s_report->ats_len = card.ats_len;
  memcpy(s_report->ats, card.ats, card.ats_len);
  memset(&s_session, 0, sizeof(s_session));

  char uid_hex[SMARTCARD_PN5180_UID_MAX_LEN * 2 + 1] = {0};
  format_hex_compact(card.uid, card.uid_len, uid_hex, sizeof(uid_hex));
  report_detail("PN5180 ISO14443A CPU card ready; UID=%s SAK=0x%02X.",
                uid_hex, card.sak);
  return ESP_OK;
}

static bool probe_retryable_error(esp_err_t err) {
  return err == ESP_ERR_NOT_FOUND || err == ESP_ERR_TIMEOUT ||
         err == ESP_ERR_INVALID_STATE || err == ESP_ERR_INVALID_RESPONSE ||
         err == ESP_ERR_INVALID_SIZE || err == ESP_ERR_INVALID_CRC;
}

static esp_err_t probe_target_locked(uint32_t timeout_ms) {
  const uint32_t total_timeout = default_timeout(timeout_ms);
  const TickType_t start = xTaskGetTickCount();
  const TickType_t timeout_ticks = pdMS_TO_TICKS(total_timeout);
  esp_err_t last_err = ESP_ERR_NOT_FOUND;

  clear_target_report();
  s_next_poll_uses_wupa = true;
  ESP_RETURN_ON_ERROR(restart_rf_field_locked(), TAG,
                      "RF restart before card probe");

  do {
    esp_err_t err = poll_target_once_locked();
    if (err == ESP_OK)
      return ESP_OK;

    last_err = err;
    if (!probe_retryable_error(err))
      return err;

    if (err != ESP_ERR_NOT_FOUND)
      (void)restart_rf_field_locked();
    vTaskDelay(pdMS_TO_TICKS(120));
  } while ((xTaskGetTickCount() - start) <= timeout_ticks);

  report_detail("PN5180 ready, but no ISO14443-4 NFC smartcard found.");
  return last_err == ESP_ERR_TIMEOUT ? ESP_ERR_NOT_FOUND : last_err;
}

static bool version_block_is_blank(const uint8_t *eeprom) {
  bool all_zero = true;
  bool all_ff = true;

  for (size_t i = 0; i < PN5180_EEPROM_VERSION_LEN; i++) {
    if (eeprom[i] != 0x00)
      all_zero = false;
    if (eeprom[i] != 0xFF)
      all_ff = false;
  }

  return all_zero || all_ff;
}

static esp_err_t reader_init_iso14443a_locked(void) {
  ESP_RETURN_ON_ERROR(rf_off_locked(), TAG, "RF_OFF");
  ESP_RETURN_ON_ERROR(set_system_command_locked(PN5180_SYSTEM_COMMAND_IDLE),
                      TAG, "Set idle");
  ESP_RETURN_ON_ERROR(clear_irqs_locked(), TAG, "Clear IRQs");
  ESP_RETURN_ON_ERROR(load_rf_config_locked(), TAG, "LOAD_RF_CONFIG A106");
  ESP_RETURN_ON_ERROR(
      write_register_or_mask_locked(PN5180_REG_TRANSCEIVE_CONTROL,
                                    PN5180_TRANSCEIVE_INITIATOR),
      TAG, "Set initiator");
  ESP_RETURN_ON_ERROR(
      write_register_and_mask_locked(PN5180_REG_CRC_TX_CONFIG, ~1U), TAG,
      "Disable TX CRC for REQA");
  ESP_RETURN_ON_ERROR(
      write_register_and_mask_locked(PN5180_REG_CRC_RX_CONFIG, ~1U), TAG,
      "Disable RX CRC for ATQA");
  ESP_RETURN_ON_ERROR(
      write_register_and_mask_locked(PN5180_REG_TX_CONFIG,
                                     ~PN5180_TX_PARITY_ENABLE),
      TAG, "Disable TX parity for REQA");
  ESP_RETURN_ON_ERROR(write_register_or_mask_locked(PN5180_REG_TX_CONFIG,
                                                   PN5180_TX_DATA_ENABLE),
                      TAG, "Enable TX data");
  ESP_RETURN_ON_ERROR(rf_on_locked(), TAG, "RF_ON");
  return wait_rf_field_on_locked();
}

static esp_err_t start_locked(void) {
  if (s_started)
    return ESP_OK;

  memset(s_report, 0, sizeof(*s_report));
  report_detail("PN5180 not initialized.");

  if (!pins_configured()) {
    report_detail("PN5180 SPI pins are not configured.");
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t err = gpio_init_locked();
  if (err != ESP_OK) {
    report_detail("PN5180 GPIO init failed: %s.", esp_err_to_name(err));
    return err;
  }

  err = spi_init_locked();
  if (err != ESP_OK) {
    report_detail("PN5180 SPI init failed: %s.", esp_err_to_name(err));
    drop_device_locked();
    return err;
  }

  err = reset_locked();
  if (err != ESP_OK) {
    report_detail("PN5180 reset/BUSY wait failed: %s.", esp_err_to_name(err));
    drop_device_locked();
    return ESP_ERR_NOT_FOUND;
  }

  uint8_t eeprom[PN5180_EEPROM_VERSION_LEN] = {0};
  err = read_eeprom_locked(PN5180_EEPROM_VERSION_ADDR,
                           PN5180_EEPROM_VERSION_LEN, eeprom);
  if (err != ESP_OK) {
    report_detail("PN5180 EEPROM read failed: %s.", esp_err_to_name(err));
    drop_device_locked();
    return ESP_ERR_NOT_FOUND;
  }
  if (version_block_is_blank(eeprom)) {
    report_detail("PN5180 EEPROM read was blank; check MISO/NSS/power.");
    drop_device_locked();
    return ESP_ERR_NOT_FOUND;
  }

  s_report->initialized = true;
  s_report->pn5180_present = true;

  err = reader_init_iso14443a_locked();
  if (err != ESP_OK) {
    report_detail("PN5180 RF init failed: %s.", esp_err_to_name(err));
    drop_device_locked();
    return err;
  }

  s_started = true;
  clear_target_report();
  report_detail("PN5180 ready; waiting for ISO14443-4 smartcard.");
  ESP_LOGI(TAG, "PN5180 ready on SPI%d SCK=%d MOSI=%d MISO=%d NSS=%d BUSY=%d RST=%d Hz=%d",
           PN5180_SPI_HOST, CONFIG_KSIG_PN5180_PIN_SCK,
           CONFIG_KSIG_PN5180_PIN_MOSI, CONFIG_KSIG_PN5180_PIN_MISO,
           CONFIG_KSIG_PN5180_PIN_NSS, CONFIG_KSIG_PN5180_PIN_BUSY,
           CONFIG_KSIG_PN5180_PIN_RST, CONFIG_KSIG_PN5180_SPI_HZ);
  return ESP_OK;
}

static esp_err_t ensure_target_locked(uint32_t timeout_ms) {
  if (s_target_active)
    return ESP_OK;
  return probe_target_locked(timeout_ms);
}

static esp_err_t iso_dep_transmit_apdu_locked(const uint8_t *apdu,
                                              size_t apdu_len,
                                              uint8_t *response,
                                              size_t response_cap,
                                              size_t *response_len,
                                              uint16_t *sw,
                                              uint32_t timeout_ms) {
  if (!apdu || apdu_len == 0 || apdu_len > SMARTCARD_CCID_APDU_MAX_LEN ||
      !response_len || !sw)
    return ESP_ERR_INVALID_ARG;
  if (apdu_len + 1U > PN5180_FRAME_MAX_LEN)
    return ESP_ERR_INVALID_SIZE;

  *response_len = 0;
  *sw = 0;

  uint8_t frame[PN5180_FRAME_MAX_LEN] = {0};
  frame[0] = ISO14443_4_I_BLOCK_BASE | (s_session.block_number & 0x01);
  memcpy(&frame[1], apdu, apdu_len);

  uint8_t ack[1] = {0};
  uint8_t wtx_response[PN5180_FRAME_MAX_LEN] = {0};
  uint8_t rx[PN5180_FRAME_MAX_LEN] = {0};
  size_t rx_len = 0;
  uint8_t assembled[SMARTCARD_CCID_RESPONSE_MAX_LEN] = {0};
  size_t assembled_len = 0;
  const uint8_t *tx = frame;
  size_t tx_len = apdu_len + 1U;

  ESP_RETURN_ON_ERROR(config_typea_crc_frame_locked(), TAG,
                      "Configure ISO-DEP APDU frame");

  for (unsigned attempt = 0; attempt < PN5180_ISO_DEP_MAX_FOLLOWUP_FRAMES;
       attempt++) {
    ESP_RETURN_ON_ERROR(
        transceive_frame_locked(tx, tx_len, 0, rx, sizeof(rx), &rx_len,
                                default_timeout(timeout_ms)),
        TAG, "ISO-DEP APDU frame");

    if (rx_len == 0) {
      ESP_LOGW(TAG, "ISO-DEP empty frame");
      return ESP_ERR_INVALID_RESPONSE;
    }

    const uint8_t pcb = rx[0];
    const uint8_t block_type = pcb & ISO14443_4_I_BLOCK_MASK;
    ESP_LOGD(TAG, "ISO-DEP RX PCB=0x%02X len=%u", pcb, (unsigned)rx_len);

    if (block_type == ISO14443_4_S_BLOCK_TYPE) {
      const bool cid_present = (pcb & ISO14443_4_S_CID_PRESENT) != 0;
      const size_t min_wtx_len = cid_present ? 3U : 2U;
      if ((pcb & ~ISO14443_4_S_CID_PRESENT) == ISO14443_4_S_WTX &&
          rx_len >= min_wtx_len) {
        memcpy(wtx_response, rx, rx_len);
        tx = wtx_response;
        tx_len = rx_len;
        ESP_LOGI(TAG, "ISO-DEP WTX requested, multiplier=%u",
                 (unsigned)rx[rx_len - 1U]);
        continue;
      }
      ESP_LOGW(TAG, "Unsupported ISO-DEP S-block PCB=0x%02X len=%u", pcb,
               (unsigned)rx_len);
      return ESP_ERR_INVALID_RESPONSE;
    }

    if (block_type == ISO14443_4_R_BLOCK_TYPE) {
      ESP_LOGW(TAG, "ISO-DEP R-block PCB=0x%02X, retransmitting I-block", pcb);
      tx = frame;
      tx_len = apdu_len + 1U;
      continue;
    }

    if (block_type != ISO14443_4_I_BLOCK_TYPE) {
      ESP_LOGW(TAG, "Unsupported ISO-DEP PCB=0x%02X len=%u", pcb,
               (unsigned)rx_len);
      return ESP_ERR_INVALID_RESPONSE;
    }

    if (rx_len < 2) {
      ESP_LOGW(TAG, "ISO-DEP I-block too short: PCB=0x%02X len=%u", pcb,
               (unsigned)rx_len);
      return ESP_ERR_INVALID_RESPONSE;
    }

    const size_t inf_len = rx_len - 1U;
    if (assembled_len + inf_len > sizeof(assembled)) {
      *response_len = assembled_len + inf_len;
      return ESP_ERR_INVALID_SIZE;
    }
    memcpy(assembled + assembled_len, rx + 1, inf_len);
    assembled_len += inf_len;

    if ((pcb & ISO14443_4_I_BLOCK_CHAINING) != 0) {
      const uint8_t next_block = (pcb ^ 1U) & 0x01U;
      ack[0] = ISO14443_4_R_ACK_BASE | next_block;
      tx = ack;
      tx_len = sizeof(ack);
      ESP_LOGI(TAG, "ISO-DEP chained response block, ACK next=%u",
               (unsigned)next_block);
      continue;
    }

    if (assembled_len < 2) {
      ESP_LOGW(TAG, "ISO-DEP APDU response too short: len=%u",
               (unsigned)assembled_len);
      return ESP_ERR_INVALID_RESPONSE;
    }

    *response_len = assembled_len;
    if (assembled_len > response_cap)
      return ESP_ERR_INVALID_SIZE;

    if (response && assembled_len)
      memcpy(response, assembled, assembled_len);
    *sw = ((uint16_t)assembled[assembled_len - 2U] << 8) |
          assembled[assembled_len - 1U];
    s_session.block_number ^= 1;
    ESP_LOGI(TAG, "ISO-DEP APDU OK: response=%u SW=%04X",
             (unsigned)assembled_len, *sw);
    report_detail("PN5180 APDU OK; response=%u SW=%04X.",
                  (unsigned)assembled_len, *sw);
    return ESP_OK;
  }

  ESP_LOGW(TAG, "ISO-DEP APDU exceeded follow-up frame limit");
  return ESP_ERR_TIMEOUT;
}

esp_err_t smartcard_pn5180_start(void) {
  esp_err_t err = take_lock();
  if (err != ESP_OK)
    return err;
  err = start_locked();
  give_lock();
  return err;
}

esp_err_t smartcard_pn5180_probe(uint32_t timeout_ms) {
  esp_err_t err = take_lock();
  if (err != ESP_OK)
    return err;

  err = start_locked();
  if (err == ESP_OK)
    err = probe_target_locked(timeout_ms);

  give_lock();
  return err;
}

esp_err_t smartcard_pn5180_transmit_apdu(const uint8_t *apdu, size_t apdu_len,
                                         uint8_t *response,
                                         size_t response_cap,
                                         size_t *response_len, uint16_t *sw,
                                         uint32_t timeout_ms) {
  if (!apdu || apdu_len == 0 || !response_len || !sw)
    return ESP_ERR_INVALID_ARG;

  *response_len = 0;
  *sw = 0;

  esp_err_t err = take_lock();
  if (err != ESP_OK)
    return err;

  err = start_locked();
  if (err != ESP_OK)
    goto done;

  err = ensure_target_locked(timeout_ms);
  if (err != ESP_OK)
    goto done;

  err = iso_dep_transmit_apdu_locked(apdu, apdu_len, response, response_cap,
                                     response_len, sw, timeout_ms);
  if (err != ESP_OK) {
    clear_target_report();
    (void)restart_rf_field_locked();
    report_detail("PN5180 APDU exchange failed: %s.", esp_err_to_name(err));
  }

done:
  give_lock();
  return err;
}

void smartcard_pn5180_snapshot(smartcard_pn5180_report_t *out) {
  if (!out)
    return;
  esp_err_t err = take_lock();
  if (err != ESP_OK) {
    memset(out, 0, sizeof(*out));
    snprintf(out->detail, sizeof(out->detail),
             "PN5180 snapshot lock failed: %s.", esp_err_to_name(err));
    return;
  }
  *out = *s_report;
  give_lock();
}

void smartcard_pn5180_format_report(char *out, size_t out_len) {
  if (!out || out_len == 0)
    return;

  smartcard_pn5180_report_t report;
  smartcard_pn5180_snapshot(&report);

  char uid_hex[SMARTCARD_PN5180_UID_MAX_LEN * 2 + 1] = {0};
  char ats_hex[SMARTCARD_PN5180_ATS_MAX_LEN * 2 + 1] = {0};
  format_hex_compact(report.uid, report.uid_len, uid_hex, sizeof(uid_hex));
  format_hex_compact(report.ats, report.ats_len, ats_hex, sizeof(ats_hex));

  snprintf(out, out_len,
           "Initialized: %s\nPN5180 present: %s\nRF field: %s\nCard present: %s\nISO-DEP/APDU: %s\nUID: %s\nATQA: %02X%02X\nSAK: %02X\nATS: %s\nDetail: %s",
           report.initialized ? "yes" : "no",
           report.pn5180_present ? "yes" : "no",
           report.field_on ? "on" : "off",
           report.target_present ? "yes" : "no",
           report.iso_dep ? "yes" : "unknown/no",
           uid_hex[0] ? uid_hex : "-", report.atqa[0], report.atqa[1],
           report.sak, ats_hex[0] ? ats_hex : "-",
           report.detail[0] ? report.detail : "-");
}

#endif // CONFIG_KSIG_PN5180_ENABLED
#endif // SIMULATOR
