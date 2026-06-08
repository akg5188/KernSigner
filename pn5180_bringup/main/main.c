#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "PN5180_BRINGUP";

#define KSIG_C6_CHIP_PU_GPIO GPIO_NUM_54
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
#define PN5180_REG_RF_CONTROL_TX 0x20
#define PN5180_REG_SYSTEM_STATUS 0x24
#define PN5180_REG_ANT_CONTROL 0x29

#define PN5180_IRQ_RX BIT(0)
#define PN5180_IRQ_TX BIT(1)
#define PN5180_IRQ_IDLE BIT(2)
#define PN5180_IRQ_TX_RFON BIT(9)
#define PN5180_IRQ_RF_ACTIVE_ERROR BIT(10)
#define PN5180_IRQ_TEMPSENS_ERROR BIT(16)
#define PN5180_IRQ_GENERAL_ERROR BIT(17)
#define PN5180_IRQ_HV_ERROR BIT(18)
#define PN5180_IRQ_ALL_STATUS 0x000FFFFF

#define PN5180_SYSTEM_COMMAND_MASK 0x00000007U
#define PN5180_SYSTEM_COMMAND_IDLE 0x00000000U
#define PN5180_SYSTEM_COMMAND_TRANSCEIVE 0x00000003U

#define PN5180_TRANSCEIVE_INITIATOR BIT(0)
#define PN5180_RX_NUM_BYTES_MASK 0x000001FFU
#define PN5180_TX_DATA_ENABLE BIT(10)
#define PN5180_TX_PARITY_ENABLE BIT(11)
#define PN5180_RF_STATUS_RX_ACTIVE BIT(10)
#define PN5180_RF_STATUS_TX_ACTIVE BIT(11)
#define PN5180_RF_STATUS_RX_ENABLE BIT(12)
#define PN5180_RF_STATUS_RF_DET_STATUS BIT(16)
#define PN5180_RF_STATUS_TX_RF_STATUS BIT(17)
#define PN5180_RF_STATUS_DPLL_ENABLE BIT(19)

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

#define TYPE4_NDEF_CC_FILE_ID 0xE103
#define TYPE4_NDEF_MAX_READ 240
#define TYPE4_NDEF_CHUNK_READ 48

#if CONFIG_PN5180_BRINGUP_PIN_IRQ >= 0
#define PN5180_BRINGUP_IRQ_ENABLED 1
#else
#define PN5180_BRINGUP_IRQ_ENABLED 0
#endif

static spi_device_handle_t s_pn5180;

typedef struct {
  uint32_t rf_status;
  uint32_t irq_status;
  uint32_t system_config;
  uint32_t transceive_control;
  uint32_t tx_config;
  uint32_t rf_control_tx;
  uint32_t ant_control;
  uint32_t system_status;
} pn5180_rf_diag_t;

typedef struct {
  uint8_t atqa[2];
  uint8_t uid[10];
  size_t uid_len;
  uint8_t sak;
} iso14443a_card_t;

typedef struct {
  uint8_t block_number;
} iso14443_4_session_t;

static void format_hex(const uint8_t *data, size_t len, char *out,
                       size_t out_len);

static void disable_wireless_companion(void) {
  gpio_config_t cfg = {
      .pin_bit_mask = BIT64(KSIG_C6_CHIP_PU_GPIO),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_ENABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  ESP_ERROR_CHECK(gpio_config(&cfg));
  ESP_ERROR_CHECK(gpio_set_level(KSIG_C6_CHIP_PU_GPIO, 0));
  ESP_LOGI(TAG, "ESP32-C6 wireless companion held disabled on GPIO%d",
           KSIG_C6_CHIP_PU_GPIO);
}

static void log_pin_levels(const char *stage) {
#if PN5180_BRINGUP_IRQ_ENABLED
  ESP_LOGI(TAG, "%s: BUSY=%d IRQ=%d RST=%d", stage,
           gpio_get_level(CONFIG_PN5180_BRINGUP_PIN_BUSY),
           gpio_get_level(CONFIG_PN5180_BRINGUP_PIN_IRQ),
           gpio_get_level(CONFIG_PN5180_BRINGUP_PIN_RST));
#else
  ESP_LOGI(TAG, "%s: BUSY=%d RST=%d", stage,
           gpio_get_level(CONFIG_PN5180_BRINGUP_PIN_BUSY),
           gpio_get_level(CONFIG_PN5180_BRINGUP_PIN_RST));
#endif
}

static esp_err_t pn5180_wait_busy_level(int level, uint32_t timeout_us) {
  const int64_t start_us = esp_timer_get_time();

  while ((esp_timer_get_time() - start_us) <= timeout_us) {
    if (gpio_get_level(CONFIG_PN5180_BRINGUP_PIN_BUSY) == level) {
      return ESP_OK;
    }
    esp_rom_delay_us(10);
  }

  return ESP_ERR_TIMEOUT;
}

static esp_err_t pn5180_wait_busy_low(uint32_t timeout_ms) {
  return pn5180_wait_busy_level(0, timeout_ms * 1000U);
}

static esp_err_t pn5180_gpio_init(void) {
  gpio_config_t input_cfg = {
      .pin_bit_mask = BIT64(CONFIG_PN5180_BRINGUP_PIN_BUSY),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
#if PN5180_BRINGUP_IRQ_ENABLED
  input_cfg.pin_bit_mask |= BIT64(CONFIG_PN5180_BRINGUP_PIN_IRQ);
#endif
  ESP_RETURN_ON_ERROR(gpio_config(&input_cfg), TAG, "input GPIO init");

  gpio_config_t rst_cfg = {
      .pin_bit_mask = BIT64(CONFIG_PN5180_BRINGUP_PIN_RST),
      .mode = GPIO_MODE_INPUT_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  ESP_RETURN_ON_ERROR(gpio_config(&rst_cfg), TAG, "reset GPIO init");
  ESP_RETURN_ON_ERROR(gpio_set_level(CONFIG_PN5180_BRINGUP_PIN_RST, 1), TAG,
                      "reset high");

  return ESP_OK;
}

static esp_err_t pn5180_reset(void) {
  ESP_LOGI(TAG, "Resetting PN5180...");
  ESP_RETURN_ON_ERROR(gpio_set_level(CONFIG_PN5180_BRINGUP_PIN_RST, 0), TAG,
                      "reset low");
  vTaskDelay(pdMS_TO_TICKS(20));
  ESP_RETURN_ON_ERROR(gpio_set_level(CONFIG_PN5180_BRINGUP_PIN_RST, 1), TAG,
                      "reset high");
  vTaskDelay(pdMS_TO_TICKS(20));
  log_pin_levels("after reset");
  return pn5180_wait_busy_low(500);
}

static esp_err_t pn5180_spi_init(void) {
  spi_bus_config_t buscfg = {
      .mosi_io_num = CONFIG_PN5180_BRINGUP_PIN_MOSI,
      .miso_io_num = CONFIG_PN5180_BRINGUP_PIN_MISO,
      .sclk_io_num = CONFIG_PN5180_BRINGUP_PIN_SCK,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = 96,
  };
  ESP_RETURN_ON_ERROR(spi_bus_initialize(PN5180_SPI_HOST, &buscfg,
                                         SPI_DMA_CH_AUTO),
                      TAG, "SPI bus init");

  spi_device_interface_config_t devcfg = {
      .clock_speed_hz = CONFIG_PN5180_BRINGUP_SPI_HZ,
      .mode = 0,
      .spics_io_num = CONFIG_PN5180_BRINGUP_PIN_NSS,
      .queue_size = 1,
  };
  ESP_RETURN_ON_ERROR(spi_bus_add_device(PN5180_SPI_HOST, &devcfg, &s_pn5180),
                      TAG, "SPI add PN5180");
  return ESP_OK;
}

static esp_err_t pn5180_spi_write(const uint8_t *tx, size_t len) {
  if (!tx || len == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  ESP_RETURN_ON_ERROR(pn5180_wait_busy_low(500), TAG, "BUSY before write");
  spi_transaction_t trans = {
      .length = len * 8,
      .tx_buffer = tx,
  };
  ESP_RETURN_ON_ERROR(spi_device_transmit(s_pn5180, &trans), TAG, "SPI write");
  return pn5180_wait_busy_low(500);
}

static esp_err_t pn5180_spi_write_no_response(const uint8_t *tx, size_t len) {
  esp_err_t err = pn5180_spi_write(tx, len);
  if (err == ESP_OK) {
    vTaskDelay(pdMS_TO_TICKS(2));
  }
  return err;
}

static esp_err_t pn5180_spi_read(uint8_t *rx, size_t len) {
  if (!rx || len == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  uint8_t dummy[64] = {0};
  if (len > sizeof(dummy)) {
    return ESP_ERR_INVALID_SIZE;
  }

  ESP_RETURN_ON_ERROR(pn5180_wait_busy_low(500), TAG, "BUSY before read");
  spi_transaction_t trans = {
      .length = len * 8,
      .tx_buffer = dummy,
      .rx_buffer = rx,
  };
  ESP_RETURN_ON_ERROR(spi_device_transmit(s_pn5180, &trans), TAG, "SPI read");
  return pn5180_wait_busy_low(500);
}

static esp_err_t pn5180_read_eeprom(uint8_t addr, uint8_t len, uint8_t *out) {
  uint8_t cmd[] = {PN5180_CMD_READ_EEPROM, addr, len};
  ESP_RETURN_ON_ERROR(pn5180_spi_write(cmd, sizeof(cmd)), TAG,
                      "READ_EEPROM command");
  return pn5180_spi_read(out, len);
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

static esp_err_t pn5180_write_register(uint8_t reg, uint32_t value) {
  uint8_t cmd[6] = {PN5180_CMD_WRITE_REGISTER, reg};
  put_u32_le(&cmd[2], value);
  return pn5180_spi_write_no_response(cmd, sizeof(cmd));
}

static esp_err_t pn5180_write_register_or_mask(uint8_t reg, uint32_t mask) {
  uint8_t cmd[6] = {PN5180_CMD_WRITE_REGISTER_OR_MASK, reg};
  put_u32_le(&cmd[2], mask);
  return pn5180_spi_write_no_response(cmd, sizeof(cmd));
}

static esp_err_t pn5180_write_register_and_mask(uint8_t reg, uint32_t mask) {
  uint8_t cmd[6] = {PN5180_CMD_WRITE_REGISTER_AND_MASK, reg};
  put_u32_le(&cmd[2], mask);
  return pn5180_spi_write_no_response(cmd, sizeof(cmd));
}

static esp_err_t pn5180_read_register(uint8_t reg, uint32_t *value) {
  if (!value) {
    return ESP_ERR_INVALID_ARG;
  }

  uint8_t cmd[] = {PN5180_CMD_READ_REGISTER, reg};
  uint8_t rx[4] = {0};
  ESP_RETURN_ON_ERROR(pn5180_spi_write(cmd, sizeof(cmd)), TAG,
                      "READ_REGISTER command");
  ESP_RETURN_ON_ERROR(pn5180_spi_read(rx, sizeof(rx)), TAG,
                      "READ_REGISTER data");
  *value = get_u32_le(rx);
  return ESP_OK;
}

static esp_err_t pn5180_set_system_command(uint32_t command) {
  ESP_RETURN_ON_ERROR(
      pn5180_write_register_and_mask(PN5180_REG_SYSTEM_CONFIG,
                                     ~PN5180_SYSTEM_COMMAND_MASK),
      TAG, "Clear SYSTEM_CONFIG.COMMAND");
  if (command != PN5180_SYSTEM_COMMAND_IDLE) {
    ESP_RETURN_ON_ERROR(
        pn5180_write_register_or_mask(PN5180_REG_SYSTEM_CONFIG, command),
        TAG, "Set SYSTEM_CONFIG.COMMAND");
  }
  return ESP_OK;
}

static esp_err_t pn5180_clear_irqs(void) {
  return pn5180_write_register(PN5180_REG_IRQ_CLEAR, PN5180_IRQ_ALL_STATUS);
}

static esp_err_t pn5180_load_rf_config(void) {
  uint8_t cmd[] = {PN5180_CMD_LOAD_RF_CONFIG,
                   PN5180_RF_CONFIG_TX_ISO14443A_106,
                   PN5180_RF_CONFIG_RX_ISO14443A_106};
  return pn5180_spi_write_no_response(cmd, sizeof(cmd));
}

static esp_err_t pn5180_rf_on(void) {
  uint8_t cmd[] = {PN5180_CMD_RF_ON,
                   PN5180_RF_ON_DISABLE_COLLISION_AVOIDANCE};
  return pn5180_spi_write_no_response(cmd, sizeof(cmd));
}

static esp_err_t pn5180_rf_off(void) {
  uint8_t cmd[] = {PN5180_CMD_RF_OFF, 0x00};
  return pn5180_spi_write_no_response(cmd, sizeof(cmd));
}

static esp_err_t pn5180_read_rf_diag(pn5180_rf_diag_t *diag) {
  if (!diag) {
    return ESP_ERR_INVALID_ARG;
  }
  memset(diag, 0, sizeof(*diag));
  ESP_RETURN_ON_ERROR(
      pn5180_read_register(PN5180_REG_RF_STATUS, &diag->rf_status), TAG,
      "Read RF_STATUS");
  ESP_RETURN_ON_ERROR(
      pn5180_read_register(PN5180_REG_IRQ_STATUS, &diag->irq_status), TAG,
      "Read IRQ_STATUS");
  ESP_RETURN_ON_ERROR(
      pn5180_read_register(PN5180_REG_SYSTEM_CONFIG, &diag->system_config),
      TAG, "Read SYSTEM_CONFIG");
  ESP_RETURN_ON_ERROR(pn5180_read_register(PN5180_REG_TRANSCEIVE_CONTROL,
                                           &diag->transceive_control),
                      TAG, "Read TRANSCEIVE_CONTROL");
  ESP_RETURN_ON_ERROR(
      pn5180_read_register(PN5180_REG_TX_CONFIG, &diag->tx_config), TAG,
      "Read TX_CONFIG");
  ESP_RETURN_ON_ERROR(pn5180_read_register(PN5180_REG_RF_CONTROL_TX,
                                           &diag->rf_control_tx),
                      TAG, "Read RF_CONTROL_TX");
  ESP_RETURN_ON_ERROR(
      pn5180_read_register(PN5180_REG_ANT_CONTROL, &diag->ant_control), TAG,
      "Read ANT_CONTROL");
  ESP_RETURN_ON_ERROR(pn5180_read_register(PN5180_REG_SYSTEM_STATUS,
                                           &diag->system_status),
                      TAG, "Read SYSTEM_STATUS");
  return ESP_OK;
}

static bool pn5180_rf_diag_field_on(const pn5180_rf_diag_t *diag) {
  return diag && (diag->rf_status & PN5180_RF_STATUS_TX_RF_STATUS);
}

static esp_err_t pn5180_restart_rf_field(void) {
  ESP_RETURN_ON_ERROR(pn5180_set_system_command(PN5180_SYSTEM_COMMAND_IDLE),
                      TAG, "Idle before RF restart");
  ESP_RETURN_ON_ERROR(pn5180_rf_off(), TAG, "RF_OFF restart");
  vTaskDelay(pdMS_TO_TICKS(20));
  ESP_RETURN_ON_ERROR(pn5180_clear_irqs(), TAG, "Clear IRQs before RF restart");
  ESP_RETURN_ON_ERROR(pn5180_rf_on(), TAG, "RF_ON restart");
  vTaskDelay(pdMS_TO_TICKS(20));

  pn5180_rf_diag_t diag = {0};
  ESP_RETURN_ON_ERROR(pn5180_read_rf_diag(&diag), TAG,
                      "Read RF diag after restart");
  if (!pn5180_rf_diag_field_on(&diag)) {
    return ESP_ERR_INVALID_STATE;
  }
  return ESP_OK;
}

static void pn5180_log_rf_diag(const char *stage,
                               const pn5180_rf_diag_t *diag) {
  ESP_LOGI(TAG,
           "%s: RF_STATUS=0x%08" PRIX32
           " TX_RF=%d DPLL=%d RF_DET=%d RX_EN=%d TX_ACTIVE=%d RX_ACTIVE=%d "
           "IRQ=0x%08" PRIX32
           " TX_RFON_IRQ=%d RF_ACTIVE_ERR=%d TEMP_ERR=%d HV_ERR=%d "
           "GEN_ERR=%d",
           stage, diag->rf_status,
           !!(diag->rf_status & PN5180_RF_STATUS_TX_RF_STATUS),
           !!(diag->rf_status & PN5180_RF_STATUS_DPLL_ENABLE),
           !!(diag->rf_status & PN5180_RF_STATUS_RF_DET_STATUS),
           !!(diag->rf_status & PN5180_RF_STATUS_RX_ENABLE),
           !!(diag->rf_status & PN5180_RF_STATUS_TX_ACTIVE),
           !!(diag->rf_status & PN5180_RF_STATUS_RX_ACTIVE),
           diag->irq_status, !!(diag->irq_status & PN5180_IRQ_TX_RFON),
           !!(diag->irq_status & PN5180_IRQ_RF_ACTIVE_ERROR),
           !!(diag->irq_status & PN5180_IRQ_TEMPSENS_ERROR),
           !!(diag->irq_status & PN5180_IRQ_HV_ERROR),
           !!(diag->irq_status & PN5180_IRQ_GENERAL_ERROR));
  ESP_LOGI(TAG,
           "%s regs: SYSTEM_CONFIG=0x%08" PRIX32
           " TRANSCEIVE_CONTROL=0x%08" PRIX32
           " TX_CONFIG=0x%08" PRIX32
           " RF_CONTROL_TX=0x%08" PRIX32
           " ANT_CONTROL=0x%08" PRIX32
           " SYSTEM_STATUS=0x%08" PRIX32,
           stage, diag->system_config, diag->transceive_control,
           diag->tx_config, diag->rf_control_tx, diag->ant_control,
           diag->system_status);
}

static esp_err_t pn5180_wait_rf_field_on(void) {
  pn5180_rf_diag_t diag = {0};

  for (int i = 0; i < 10; i++) {
    vTaskDelay(pdMS_TO_TICKS(i == 0 ? 20 : 100));
    ESP_RETURN_ON_ERROR(pn5180_read_rf_diag(&diag), TAG, "Read RF diag");

    char stage[32] = {0};
    snprintf(stage, sizeof(stage), "RF_ON +%ums", 20 + (i * 100));
    pn5180_log_rf_diag(stage, &diag);

    if (pn5180_rf_diag_field_on(&diag)) {
      ESP_LOGI(TAG,
               "RF field verified ON by TX_RF_STATUS=1. DPLL may stay 0 "
               "until receiver/transceive activity.");
      return ESP_OK;
    }

    if (diag.irq_status & (PN5180_IRQ_RF_ACTIVE_ERROR |
                           PN5180_IRQ_TEMPSENS_ERROR |
                           PN5180_IRQ_HV_ERROR |
                           PN5180_IRQ_GENERAL_ERROR)) {
      break;
    }
  }

  ESP_LOGE(TAG,
           "RF_ON command was sent, but the RF field is NOT active. "
           "TX_RF=%d DPLL=%d TX_RFON_IRQ=%d RF_ACTIVE_ERR=%d TEMP_ERR=%d "
           "HV_ERR=%d GEN_ERR=%d",
           !!(diag.rf_status & PN5180_RF_STATUS_TX_RF_STATUS),
           !!(diag.rf_status & PN5180_RF_STATUS_DPLL_ENABLE),
           !!(diag.irq_status & PN5180_IRQ_TX_RFON),
           !!(diag.irq_status & PN5180_IRQ_RF_ACTIVE_ERROR),
           !!(diag.irq_status & PN5180_IRQ_TEMPSENS_ERROR),
           !!(diag.irq_status & PN5180_IRQ_HV_ERROR),
           !!(diag.irq_status & PN5180_IRQ_GENERAL_ERROR));
  ESP_LOGE(TAG,
           "SPI/EEPROM is OK, so focus on RF power/antenna: module VCC/TVDD "
           "supply, common GND, antenna coil/solder joints, and any power "
           "jumper on the PN5180 board.");
  return ESP_ERR_INVALID_STATE;
}

static esp_err_t pn5180_send_data(const uint8_t *data, size_t len,
                                  uint8_t valid_last_bits) {
  if (!data || len == 0 || len > 60 || valid_last_bits > 7) {
    return ESP_ERR_INVALID_ARG;
  }

  uint8_t cmd[62] = {PN5180_CMD_SEND_DATA, valid_last_bits};
  memcpy(&cmd[2], data, len);
  return pn5180_spi_write_no_response(cmd, len + 2);
}

static esp_err_t pn5180_read_data(uint8_t *rx, size_t len) {
  if (!rx || len == 0 || len > 60) {
    return ESP_ERR_INVALID_ARG;
  }

  uint8_t cmd[] = {PN5180_CMD_READ_DATA, 0x00};
  ESP_RETURN_ON_ERROR(pn5180_spi_write(cmd, sizeof(cmd)), TAG,
                      "READ_DATA command");
  return pn5180_spi_read(rx, len);
}

static esp_err_t pn5180_set_tx_parity(bool enabled) {
  if (enabled) {
    return pn5180_write_register_or_mask(PN5180_REG_TX_CONFIG,
                                         PN5180_TX_PARITY_ENABLE);
  }
  return pn5180_write_register_and_mask(PN5180_REG_TX_CONFIG,
                                        ~PN5180_TX_PARITY_ENABLE);
}

static esp_err_t pn5180_set_crc(bool enabled) {
  if (enabled) {
    ESP_RETURN_ON_ERROR(
        pn5180_write_register_or_mask(PN5180_REG_CRC_TX_CONFIG, 1U), TAG,
        "Enable TX CRC");
    return pn5180_write_register_or_mask(PN5180_REG_CRC_RX_CONFIG, 1U);
  }

  ESP_RETURN_ON_ERROR(
      pn5180_write_register_and_mask(PN5180_REG_CRC_TX_CONFIG, ~1U), TAG,
      "Disable TX CRC");
  return pn5180_write_register_and_mask(PN5180_REG_CRC_RX_CONFIG, ~1U);
}

static esp_err_t pn5180_config_typea_short_frame(void) {
  ESP_RETURN_ON_ERROR(pn5180_set_crc(false), TAG, "Disable CRC short frame");
  ESP_RETURN_ON_ERROR(pn5180_set_tx_parity(false), TAG,
                      "Disable parity short frame");
  return pn5180_write_register_or_mask(PN5180_REG_TX_CONFIG,
                                       PN5180_TX_DATA_ENABLE);
}

static esp_err_t pn5180_config_typea_no_crc_frame(void) {
  ESP_RETURN_ON_ERROR(pn5180_set_crc(false), TAG, "Disable CRC frame");
  ESP_RETURN_ON_ERROR(pn5180_set_tx_parity(true), TAG, "Enable parity frame");
  return pn5180_write_register_or_mask(PN5180_REG_TX_CONFIG,
                                       PN5180_TX_DATA_ENABLE);
}

static esp_err_t pn5180_config_typea_crc_frame(void) {
  ESP_RETURN_ON_ERROR(pn5180_set_crc(true), TAG, "Enable CRC frame");
  ESP_RETURN_ON_ERROR(pn5180_set_tx_parity(true), TAG, "Enable parity frame");
  return pn5180_write_register_or_mask(PN5180_REG_TX_CONFIG,
                                       PN5180_TX_DATA_ENABLE);
}

static esp_err_t pn5180_transceive_frame(const uint8_t *tx, size_t tx_len,
                                         uint8_t valid_last_bits,
                                         const char *frame_name, uint8_t *rx,
                                         size_t rx_size, size_t *out_len,
                                         uint32_t timeout_ms) {
  if (!tx || tx_len == 0 || !rx || !out_len || rx_size == 0) {
    return ESP_ERR_INVALID_ARG;
  }
  *out_len = 0;

  ESP_RETURN_ON_ERROR(pn5180_clear_irqs(), TAG, "Clear IRQs before frame");
  ESP_RETURN_ON_ERROR(pn5180_set_system_command(PN5180_SYSTEM_COMMAND_IDLE),
                      TAG, "Idle before frame");
  ESP_RETURN_ON_ERROR(
      pn5180_write_register_or_mask(PN5180_REG_TRANSCEIVE_CONTROL,
                                    PN5180_TRANSCEIVE_INITIATOR),
      TAG, "Set initiator before frame");
  ESP_RETURN_ON_ERROR(
      pn5180_set_system_command(PN5180_SYSTEM_COMMAND_TRANSCEIVE), TAG,
      "Set transceive before frame");
  ESP_LOGI(TAG, "ISO14443A %s...", frame_name);
  ESP_RETURN_ON_ERROR(pn5180_send_data(tx, tx_len, valid_last_bits), TAG,
                      "SEND_DATA frame");

  const TickType_t start = xTaskGetTickCount();
  const TickType_t timeout = pdMS_TO_TICKS(timeout_ms);
  uint32_t irq_status = 0;

  do {
    ESP_RETURN_ON_ERROR(pn5180_read_register(PN5180_REG_IRQ_STATUS,
                                             &irq_status),
                        TAG, "Read IRQ_STATUS");
    if (irq_status & PN5180_IRQ_RX) {
      break;
    }
    if (irq_status & PN5180_IRQ_GENERAL_ERROR) {
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  } while ((xTaskGetTickCount() - start) <= timeout);

  ESP_RETURN_ON_ERROR(pn5180_set_system_command(PN5180_SYSTEM_COMMAND_IDLE),
                      TAG, "Idle after frame");

  if ((irq_status & PN5180_IRQ_RX) == 0) {
    return ESP_ERR_TIMEOUT;
  }

  uint32_t rx_status = 0;
  ESP_RETURN_ON_ERROR(pn5180_read_register(PN5180_REG_RX_STATUS, &rx_status),
                      TAG, "Read RX_STATUS");
  size_t rx_len = rx_status & PN5180_RX_NUM_BYTES_MASK;
  if (rx_len == 0 || rx_status == UINT32_MAX) {
    return ESP_ERR_TIMEOUT;
  }
  if (rx_len > rx_size) {
    ESP_LOGW(TAG, "%s invalid RX length: %u RX_STATUS=0x%08" PRIX32,
             frame_name, (unsigned)rx_len, rx_status);
    return ESP_ERR_INVALID_SIZE;
  }

  ESP_RETURN_ON_ERROR(pn5180_read_data(rx, rx_len), TAG, "READ_DATA");
  *out_len = rx_len;
  return ESP_OK;
}

static esp_err_t pn5180_iso14443a_select_level(uint8_t sel_cmd,
                                               const char *level_name,
                                               uint8_t *uid, size_t *uid_len,
                                               uint8_t *sak) {
  if (!uid || !uid_len || !sak || *uid_len > 10) {
    return ESP_ERR_INVALID_ARG;
  }

  uint8_t rx[16] = {0};
  size_t rx_len = 0;
  uint8_t anticoll[] = {sel_cmd, ISO14443A_NVB_ANTICOLLISION};

  ESP_RETURN_ON_ERROR(pn5180_config_typea_no_crc_frame(), TAG,
                      "Configure anticollision frame");
  ESP_RETURN_ON_ERROR(
      pn5180_transceive_frame(anticoll, sizeof(anticoll), 0, level_name, rx,
                              sizeof(rx), &rx_len, 120),
      TAG, "Anticollision frame");

  char raw[3 * sizeof(rx)] = {0};
  format_hex(rx, rx_len, raw, sizeof(raw));
  if (rx_len < 5) {
    ESP_LOGW(TAG, "%s anticollision response too short: raw=%s", level_name,
             raw);
    return ESP_ERR_INVALID_RESPONSE;
  }

  const uint8_t bcc = rx[0] ^ rx[1] ^ rx[2] ^ rx[3];
  if (bcc != rx[4]) {
    ESP_LOGW(TAG, "%s BCC mismatch: got=0x%02X expected=0x%02X raw=%s",
             level_name, rx[4], bcc, raw);
    return ESP_ERR_INVALID_CRC;
  }
  ESP_LOGI(TAG, "%s anticollision raw=%s", level_name, raw);

  uint8_t select[] = {sel_cmd,
                      ISO14443A_NVB_SELECT,
                      rx[0],
                      rx[1],
                      rx[2],
                      rx[3],
                      rx[4]};
  uint8_t sak_rx[8] = {0};
  size_t sak_len = 0;

  ESP_RETURN_ON_ERROR(pn5180_config_typea_crc_frame(), TAG,
                      "Configure select frame");
  ESP_RETURN_ON_ERROR(
      pn5180_transceive_frame(select, sizeof(select), 0, level_name, sak_rx,
                              sizeof(sak_rx), &sak_len, 120),
      TAG, "Select frame");
  if (sak_len < 1) {
    ESP_LOGW(TAG, "%s SELECT returned no SAK", level_name);
    return ESP_ERR_INVALID_RESPONSE;
  }

  const size_t append_len = (rx[0] == ISO14443A_CASCADE_TAG) ? 3 : 4;
  const size_t append_start = (rx[0] == ISO14443A_CASCADE_TAG) ? 1 : 0;
  if (*uid_len + append_len > 10) {
    return ESP_ERR_INVALID_SIZE;
  }
  memcpy(&uid[*uid_len], &rx[append_start], append_len);
  *uid_len += append_len;
  *sak = sak_rx[0];

  char uid_hex[3 * 10] = {0};
  char sak_hex[3 * sizeof(sak_rx)] = {0};
  format_hex(uid, *uid_len, uid_hex, sizeof(uid_hex));
  format_hex(sak_rx, sak_len, sak_hex, sizeof(sak_hex));
  ESP_LOGI(TAG, "%s selected: SAK=0x%02X raw=%s UID-so-far=%s", level_name,
           *sak, sak_hex, uid_hex);
  return ESP_OK;
}

static esp_err_t pn5180_iso14443a_read_uid(iso14443a_card_t *card) {
  if (!card) {
    return ESP_ERR_INVALID_ARG;
  }

  uint8_t uid[10] = {0};
  size_t uid_len = 0;
  uint8_t sak = 0;

  ESP_RETURN_ON_ERROR(pn5180_iso14443a_select_level(
                          ISO14443A_SEL_CL1, "ANTICOLL/SELECT CL1", uid,
                          &uid_len, &sak),
                      TAG, "Select cascade level 1");
  if (sak & ISO14443A_SAK_CASCADE) {
    ESP_RETURN_ON_ERROR(pn5180_iso14443a_select_level(
                            ISO14443A_SEL_CL2, "ANTICOLL/SELECT CL2", uid,
                            &uid_len, &sak),
                        TAG, "Select cascade level 2");
  }
  if (sak & ISO14443A_SAK_CASCADE) {
    ESP_RETURN_ON_ERROR(pn5180_iso14443a_select_level(
                            ISO14443A_SEL_CL3, "ANTICOLL/SELECT CL3", uid,
                            &uid_len, &sak),
                        TAG, "Select cascade level 3");
  }

  char uid_hex[3 * sizeof(uid)] = {0};
  format_hex(uid, uid_len, uid_hex, sizeof(uid_hex));
  ESP_LOGI(TAG, "ISO14443A UID read OK: UID=%s SAK=0x%02X", uid_hex, sak);
  memcpy(card->uid, uid, uid_len);
  card->uid_len = uid_len;
  card->sak = sak;
  return ESP_OK;
}

static esp_err_t pn5180_iso14443_4_rats(const iso14443a_card_t *card) {
  if (!card) {
    return ESP_ERR_INVALID_ARG;
  }

  if ((card->sak & ISO14443A_SAK_ISO_DEP) == 0) {
    ESP_LOGI(TAG, "SAK=0x%02X does not advertise ISO14443-4; skipping RATS.",
             card->sak);
    return ESP_ERR_NOT_SUPPORTED;
  }

  uint8_t rats[] = {ISO14443A_CMD_RATS, ISO14443A_RATS_FSDI_256};
  uint8_t ats[32] = {0};
  size_t ats_len = 0;

  ESP_LOGI(TAG, "ISO14443-4 bit is set in SAK. Sending RATS...");
  ESP_RETURN_ON_ERROR(pn5180_config_typea_crc_frame(), TAG,
                      "Configure RATS frame");
  ESP_RETURN_ON_ERROR(
      pn5180_transceive_frame(rats, sizeof(rats), 0, "RATS", ats,
                              sizeof(ats), &ats_len, 300),
      TAG, "RATS frame");

  char ats_hex[3 * sizeof(ats)] = {0};
  format_hex(ats, ats_len, ats_hex, sizeof(ats_hex));
  if (ats_len < 2) {
    ESP_LOGW(TAG, "ISO14443-4 ATS too short: raw=%s", ats_hex);
    return ESP_ERR_INVALID_RESPONSE;
  }

  const uint8_t tl = ats[0];
  const uint8_t t0 = ats[1];
  const uint8_t fsci = t0 & 0x0F;
  const bool has_ta = (t0 & 0x10) != 0;
  const bool has_tb = (t0 & 0x20) != 0;
  const bool has_tc = (t0 & 0x40) != 0;

  ESP_LOGI(TAG,
           "ISO14443-4 ATS received: raw=%s TL=%u T0=0x%02X FSCI=%u "
           "TA=%d TB=%d TC=%d",
           ats_hex, tl, t0, fsci, has_ta, has_tb, has_tc);

  size_t pos = 2;
  if (has_ta && pos < ats_len) {
    ESP_LOGI(TAG, "ATS TA(1)=0x%02X", ats[pos++]);
  }
  if (has_tb && pos < ats_len) {
    ESP_LOGI(TAG, "ATS TB(1)=0x%02X", ats[pos++]);
  }
  if (has_tc && pos < ats_len) {
    ESP_LOGI(TAG, "ATS TC(1)=0x%02X", ats[pos++]);
  }
  if (pos < ats_len) {
    char hist_hex[3 * sizeof(ats)] = {0};
    format_hex(&ats[pos], ats_len - pos, hist_hex, sizeof(hist_hex));
    ESP_LOGI(TAG, "ATS historical bytes: %s", hist_hex);
  }

  return ESP_OK;
}

static void format_ascii(const uint8_t *data, size_t len, char *out,
                         size_t out_len) {
  if (!out || out_len == 0) {
    return;
  }
  out[0] = '\0';
  if (!data) {
    return;
  }

  size_t pos = 0;
  for (size_t i = 0; i < len && pos + 1 < out_len; i++) {
    const uint8_t ch = data[i];
    out[pos++] = (ch >= 0x20 && ch <= 0x7E) ? (char)ch : '.';
  }
  out[pos] = '\0';
}

static esp_err_t pn5180_iso14443_4_exchange_apdu(
    iso14443_4_session_t *session, const char *name, const uint8_t *apdu,
    size_t apdu_len, uint8_t *out, size_t out_size, size_t *out_len,
    uint16_t *sw) {
  if (!session || !name || !apdu || apdu_len == 0 || apdu_len + 1 > 60 ||
      !out_len || !sw || (!out && out_size != 0)) {
    return ESP_ERR_INVALID_ARG;
  }

  *out_len = 0;
  *sw = 0;

  uint8_t frame[60] = {0};
  frame[0] = ISO14443_4_I_BLOCK_BASE | (session->block_number & 0x01);
  memcpy(&frame[1], apdu, apdu_len);

  uint8_t rx[60] = {0};
  size_t rx_len = 0;

  ESP_RETURN_ON_ERROR(pn5180_config_typea_crc_frame(), TAG,
                      "Configure ISO-DEP APDU frame");
  ESP_RETURN_ON_ERROR(
      pn5180_transceive_frame(frame, apdu_len + 1, 0, name, rx, sizeof(rx),
                              &rx_len, 500),
      TAG, "ISO-DEP APDU frame");

  char raw[3 * sizeof(rx)] = {0};
  format_hex(rx, rx_len, raw, sizeof(raw));
  if (rx_len < 3) {
    ESP_LOGW(TAG, "APDU %s response too short: raw=%s", name, raw);
    return ESP_ERR_INVALID_RESPONSE;
  }

  const uint8_t pcb = rx[0];
  if ((pcb & ISO14443_4_I_BLOCK_MASK) != ISO14443_4_I_BLOCK_TYPE) {
    ESP_LOGW(TAG, "APDU %s got unsupported ISO-DEP PCB=0x%02X raw=%s", name,
             pcb, raw);
    return ESP_ERR_INVALID_RESPONSE;
  }

  const size_t payload_len = rx_len - 1;
  const size_t data_len = payload_len - 2;
  if (data_len > out_size) {
    ESP_LOGW(TAG, "APDU %s response data too large: %u > %u raw=%s", name,
             (unsigned)data_len, (unsigned)out_size, raw);
    return ESP_ERR_INVALID_SIZE;
  }

  if (data_len > 0 && out) {
    memcpy(out, &rx[1], data_len);
  }
  *out_len = data_len;
  *sw = ((uint16_t)rx[rx_len - 2] << 8) | rx[rx_len - 1];
  session->block_number ^= 1;

  ESP_LOGI(TAG, "APDU %s response: PCB=0x%02X SW=0x%04X data_len=%u raw=%s",
           name, pcb, *sw, (unsigned)data_len, raw);
  return ESP_OK;
}

static esp_err_t pn5180_crypto_card_exchange(iso14443_4_session_t *session,
                                             const char *name,
                                             const uint8_t *apdu,
                                             size_t apdu_len, uint16_t *sw) {
  uint8_t data[60] = {0};
  size_t data_len = 0;
  uint16_t local_sw = 0;

  esp_err_t err = pn5180_iso14443_4_exchange_apdu(
      session, name, apdu, apdu_len, data, sizeof(data), &data_len, &local_sw);
  if (sw) {
    *sw = local_sw;
  }
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Crypto card APDU %s failed: %s", name,
             esp_err_to_name(err));
    return err;
  }

  char data_hex[3 * sizeof(data)] = {0};
  char data_ascii[sizeof(data) + 1] = {0};
  format_hex(data, data_len, data_hex, sizeof(data_hex));
  format_ascii(data, data_len, data_ascii, sizeof(data_ascii));
  ESP_LOGI(TAG, "Crypto card APDU %s: SW=0x%04X data=%s ascii=%s", name,
           local_sw, data_hex, data_ascii);
  return ESP_OK;
}

static esp_err_t pn5180_satochip_seedkeeper_probe(
    iso14443_4_session_t *session) {
  if (!session) {
    return ESP_ERR_INVALID_ARG;
  }

  static const uint8_t select_satochip[] = {
      0x00, 0xA4, 0x04, 0x00, 0x08, 0x53, 0x61, 0x74, 0x6F, 0x43, 0x68,
      0x69, 0x70};
  static const uint8_t select_seedkeeper[] = {
      0x00, 0xA4, 0x04, 0x00, 0x0A, 0x53, 0x65, 0x65, 0x64,
      0x4B, 0x65, 0x65, 0x70, 0x65, 0x72};
  static const uint8_t get_satochip_status[] = {0xB0, 0x3C, 0x00, 0x00,
                                                0x00};
  static const uint8_t get_seedkeeper_status[] = {0xB0, 0xA7, 0x00, 0x00,
                                                  0x00};
  static const uint8_t get_label[] = {0xB0, 0x3D, 0x00, 0x01, 0x00};

  uint16_t sw = 0;
  ESP_LOGI(TAG, "Crypto CPU card probe: SELECT Satochip...");
  esp_err_t err = pn5180_crypto_card_exchange(
      session, "SELECT Satochip", select_satochip, sizeof(select_satochip),
      &sw);
  if (err == ESP_OK && sw == 0x9000) {
    ESP_LOGI(TAG, "Satochip applet selected. Reading read-only status...");
    pn5180_crypto_card_exchange(session, "Satochip GET_STATUS",
                                get_satochip_status,
                                sizeof(get_satochip_status), &sw);
    pn5180_crypto_card_exchange(session, "Satochip GET_LABEL", get_label,
                                sizeof(get_label), &sw);
    return ESP_OK;
  }
  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Satochip applet not selected: SW=0x%04X", sw);
  }

  ESP_LOGI(TAG, "Crypto CPU card probe: SELECT SeedKeeper...");
  err = pn5180_crypto_card_exchange(session, "SELECT SeedKeeper",
                                    select_seedkeeper,
                                    sizeof(select_seedkeeper), &sw);
  if (err == ESP_OK && sw == 0x9000) {
    ESP_LOGI(TAG, "SeedKeeper applet selected. Reading read-only status...");
    pn5180_crypto_card_exchange(session, "SeedKeeper GET_STATUS",
                                get_seedkeeper_status,
                                sizeof(get_seedkeeper_status), &sw);
    pn5180_crypto_card_exchange(session, "SeedKeeper GET_LABEL", get_label,
                                sizeof(get_label), &sw);
    return ESP_OK;
  }
  if (err == ESP_OK) {
    ESP_LOGI(TAG, "SeedKeeper applet not selected: SW=0x%04X", sw);
  }

  ESP_LOGW(TAG, "No Satochip/SeedKeeper applet selected.");
  return ESP_ERR_NOT_FOUND;
}

static esp_err_t pn5180_reader_init_iso14443a(void) {
  ESP_LOGI(TAG, "Configuring PN5180 RF for ISO14443A 106 kbit/s...");
  ESP_RETURN_ON_ERROR(pn5180_rf_off(), TAG, "RF_OFF");
  ESP_RETURN_ON_ERROR(pn5180_set_system_command(PN5180_SYSTEM_COMMAND_IDLE),
                      TAG, "Set idle");
  ESP_RETURN_ON_ERROR(pn5180_clear_irqs(), TAG, "Clear IRQs");
  ESP_RETURN_ON_ERROR(pn5180_load_rf_config(), TAG, "LOAD_RF_CONFIG A106");

  ESP_RETURN_ON_ERROR(
      pn5180_write_register_or_mask(PN5180_REG_TRANSCEIVE_CONTROL,
                                    PN5180_TRANSCEIVE_INITIATOR),
      TAG, "Set initiator");
  ESP_RETURN_ON_ERROR(pn5180_write_register_and_mask(
                          PN5180_REG_CRC_TX_CONFIG, ~1U),
                      TAG, "Disable TX CRC for REQA");
  ESP_RETURN_ON_ERROR(pn5180_write_register_and_mask(
                          PN5180_REG_CRC_RX_CONFIG, ~1U),
                      TAG, "Disable RX CRC for ATQA");
  ESP_RETURN_ON_ERROR(pn5180_write_register_and_mask(
                          PN5180_REG_TX_CONFIG, ~(uint32_t)BIT(11)),
                      TAG, "Disable TX parity for REQA");
  ESP_RETURN_ON_ERROR(
      pn5180_write_register_or_mask(PN5180_REG_TX_CONFIG,
                                    PN5180_TX_DATA_ENABLE),
      TAG, "Enable TX data for REQA/WUPA");

  pn5180_rf_diag_t diag = {0};
  ESP_RETURN_ON_ERROR(pn5180_read_rf_diag(&diag), TAG,
                      "Read RF diag before RF_ON");
  pn5180_log_rf_diag("before RF_ON", &diag);

  ESP_RETURN_ON_ERROR(pn5180_rf_on(), TAG, "RF_ON");
  ESP_RETURN_ON_ERROR(pn5180_wait_rf_field_on(), TAG, "Verify RF field on");
  ESP_LOGI(TAG, "RF field is on. Waiting for ISO14443A card...");
  return ESP_OK;
}

static esp_err_t pn5180_poll_short_frame(uint8_t short_frame,
                                         const char *frame_name, uint8_t *rx,
                                         size_t rx_size, size_t *out_len) {
  ESP_RETURN_ON_ERROR(pn5180_config_typea_short_frame(), TAG,
                      "Configure short frame");
  return pn5180_transceive_frame(&short_frame, 1, 7, frame_name, rx, rx_size,
                                 out_len, 250);
}

static void pn5180_poll_card_once(void) {
  static bool use_wupa = false;
  static bool deep_probe_done = false;
  static uint8_t deep_probe_uid[10] = {0};
  static size_t deep_probe_uid_len = 0;
  uint8_t rx[16] = {0};
  size_t rx_len = 0;
  const uint8_t short_frame =
      use_wupa ? ISO14443A_CMD_WUPA : ISO14443A_CMD_REQA;
  const char *frame_name = use_wupa ? "WUPA" : "REQA";
  use_wupa = !use_wupa;

  esp_err_t err =
      pn5180_poll_short_frame(short_frame, frame_name, rx, sizeof(rx), &rx_len);
  if (err == ESP_ERR_TIMEOUT) {
    ESP_LOGI(TAG, "No ISO14443A card.");
    return;
  }
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "%s poll failed: %s", frame_name, esp_err_to_name(err));
    return;
  }

  char rx_hex[3 * sizeof(rx)] = {0};
  format_hex(rx, rx_len, rx_hex, sizeof(rx_hex));
  if (rx_len != 2) {
    ESP_LOGI(TAG, "ISO14443A non-ATQA response from %s: raw=%s", frame_name,
             rx_hex);
    return;
  }

  ESP_LOGI(TAG, "ISO14443A card detected: ATQA=%02X%02X raw=%s", rx[0], rx[1],
           rx_hex);
  iso14443a_card_t card = {
      .atqa = {rx[0], rx[1]},
  };
  err = pn5180_iso14443a_read_uid(&card);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "ISO14443A UID/SAK read failed: %s", esp_err_to_name(err));
    return;
  }

  err = pn5180_iso14443_4_rats(&card);
  if (err == ESP_ERR_NOT_SUPPORTED) {
    err = ESP_OK;
  } else if (err != ESP_OK) {
    ESP_LOGW(TAG, "ISO14443-4 RATS failed: %s", esp_err_to_name(err));
  } else {
    const bool same_deep_probe_uid =
        deep_probe_done && deep_probe_uid_len == card.uid_len &&
        memcmp(deep_probe_uid, card.uid, card.uid_len) == 0;
    if (same_deep_probe_uid) {
      ESP_LOGI(TAG, "ISO14443-4 deep APDU probe already done for this UID.");
    } else {
      iso14443_4_session_t session = {0};
      memcpy(deep_probe_uid, card.uid, card.uid_len);
      deep_probe_uid_len = card.uid_len;
      deep_probe_done = true;

      esp_err_t card_err = pn5180_satochip_seedkeeper_probe(&session);
      if (card_err != ESP_OK) {
        ESP_LOGW(TAG, "Satochip/SeedKeeper probe failed: %s",
                 esp_err_to_name(card_err));
      }
    }
  }

  esp_err_t restart_err = pn5180_restart_rf_field();
  if (restart_err != ESP_OK) {
    ESP_LOGW(TAG, "RF field restart after card test failed: %s",
             esp_err_to_name(restart_err));
  }
}

static void format_hex(const uint8_t *data, size_t len, char *out,
                       size_t out_len) {
  if (!out || out_len == 0) {
    return;
  }
  out[0] = '\0';
  for (size_t i = 0; i < len; i++) {
    char part[4];
    snprintf(part, sizeof(part), "%02X", data[i]);
    strncat(out, part, out_len - strlen(out) - 1);
    if (i + 1 < len) {
      strncat(out, " ", out_len - strlen(out) - 1);
    }
  }
}

static void log_pn5180_version_block(const uint8_t *eeprom) {
  char die_id[3 * 16] = {0};
  char raw[3 * PN5180_EEPROM_VERSION_LEN] = {0};
  format_hex(eeprom, PN5180_EEPROM_VERSION_LEN, raw, sizeof(raw));
  format_hex(eeprom, 16, die_id, sizeof(die_id));

  ESP_LOGI(TAG, "EEPROM[00..15] raw: %s", raw);
  ESP_LOGI(TAG, "Die identifier: %s", die_id);
  ESP_LOGI(TAG, "Product version raw: 0x%02X 0x%02X", eeprom[0x10],
           eeprom[0x11]);
  ESP_LOGI(TAG, "Firmware version raw: 0x%02X 0x%02X", eeprom[0x12],
           eeprom[0x13]);
  ESP_LOGI(TAG, "EEPROM version raw: 0x%02X 0x%02X", eeprom[0x14],
           eeprom[0x15]);
}

static bool pn5180_version_block_is_blank(const uint8_t *eeprom) {
  bool all_zero = true;
  bool all_ff = true;

  for (size_t i = 0; i < PN5180_EEPROM_VERSION_LEN; i++) {
    if (eeprom[i] != 0x00) {
      all_zero = false;
    }
    if (eeprom[i] != 0xFF) {
      all_ff = false;
    }
  }

  return all_zero || all_ff;
}

void app_main(void) {
  disable_wireless_companion();

  ESP_LOGI(TAG, "KernSigner PN5180 SPI bring-up");
  ESP_LOGI(TAG, "SPI host=%d SCK=%d MOSI=%d MISO=%d NSS=%d Hz=%d",
           PN5180_SPI_HOST, CONFIG_PN5180_BRINGUP_PIN_SCK,
           CONFIG_PN5180_BRINGUP_PIN_MOSI, CONFIG_PN5180_BRINGUP_PIN_MISO,
           CONFIG_PN5180_BRINGUP_PIN_NSS, CONFIG_PN5180_BRINGUP_SPI_HZ);
  ESP_LOGI(TAG, "GPIO BUSY=%d RST=%d IRQ=%d",
           CONFIG_PN5180_BRINGUP_PIN_BUSY, CONFIG_PN5180_BRINGUP_PIN_RST,
           CONFIG_PN5180_BRINGUP_PIN_IRQ);

  esp_err_t err = pn5180_gpio_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "GPIO init failed: %s", esp_err_to_name(err));
    return;
  }
  log_pin_levels("before reset");

  err = pn5180_spi_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "SPI init failed: %s", esp_err_to_name(err));
    return;
  }

  err = pn5180_reset();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "PN5180 reset/BUSY wait failed: %s", esp_err_to_name(err));
    ESP_LOGE(TAG, "Check 3V3, GND, RST GPIO%d, and BUSY GPIO%d.",
             CONFIG_PN5180_BRINGUP_PIN_RST, CONFIG_PN5180_BRINGUP_PIN_BUSY);
    return;
  }

  uint8_t eeprom[PN5180_EEPROM_VERSION_LEN] = {0};
  err = pn5180_read_eeprom(PN5180_EEPROM_VERSION_ADDR,
                           PN5180_EEPROM_VERSION_LEN, eeprom);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "PN5180 READ_EEPROM failed: %s", esp_err_to_name(err));
    ESP_LOGE(TAG, "Check SCK/MOSI/MISO/NSS wiring and keep wires short.");
    return;
  }

  log_pn5180_version_block(eeprom);
  if (pn5180_version_block_is_blank(eeprom)) {
    ESP_LOGE(TAG, "PN5180 EEPROM read is blank; SPI transaction completed but "
                  "the chip did not provide a valid response.");
    ESP_LOGE(TAG, "Check PN5180 power, common GND, MISO GPIO%d, NSS GPIO%d, "
                  "and SPI-mode jumper/solder setting.",
             CONFIG_PN5180_BRINGUP_PIN_MISO, CONFIG_PN5180_BRINGUP_PIN_NSS);
    return;
  }

  ESP_LOGI(TAG, "PN5180 SPI communication OK");

  err = pn5180_reader_init_iso14443a();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "PN5180 RF/card polling init failed: %s",
             esp_err_to_name(err));
    ESP_LOGE(TAG, "SPI is OK. Check antenna area, PN5180 module RF parts, and "
                  "keep the card away until RF init succeeds.");
    return;
  }

  while (true) {
    pn5180_poll_card_once();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
