#include "smartcard_ccid.h"

#include "i18n/i18n.h"

#define SC_TR(key, fallback) i18n_tr_or("smartcard.ccid." key, fallback)

#ifdef SIMULATOR

#include <stdio.h>
#include <string.h>

static smartcard_ccid_report_t s_report = {
    .state = SMARTCARD_CCID_STATE_UNSUPPORTED,
    .terminal = true,
    .detail = "Simulator does not access USB Host; use powered hardware to "
              "probe.",
};

esp_err_t smartcard_ccid_start(void) { return ESP_ERR_NOT_SUPPORTED; }

esp_err_t smartcard_ccid_probe(uint32_t timeout_ms) {
  (void)timeout_ms;
  s_report.state = SMARTCARD_CCID_STATE_UNSUPPORTED;
  s_report.terminal = true;
  snprintf(s_report.detail, sizeof(s_report.detail),
           "%s",
           SC_TR("simulator_no_usb",
                 "Simulator does not access USB Host; CCID readers are "
                 "enumerated only on hardware."));
  return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t smartcard_ccid_transmit_apdu(const uint8_t *apdu, size_t apdu_len,
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

void smartcard_ccid_set_factory_reset_mode(bool enabled) { (void)enabled; }

void smartcard_ccid_snapshot(smartcard_ccid_report_t *out) {
  if (out)
    *out = s_report;
}

#else

#include <inttypes.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "usb/usb_helpers.h"
#include "usb/usb_host.h"

#define TAG "KSIG_CCID"

#define USB_DESC_TYPE_INTERFACE 0x04
#define USB_DESC_TYPE_ENDPOINT 0x05
#define CCID_DESC_TYPE_FUNCTIONAL 0x21
#define USB_CLASS_CCID 0x0B

#define USB_EP_ATTR_BULK 0x02
#define USB_EP_ATTR_INTR 0x03

#define CCID_MSG_NOTIFY_SLOT_CHANGE 0x50
#define CCID_MSG_SET_PARAMETERS 0x61
#define CCID_MSG_POWER_ON 0x62
#define CCID_MSG_XFR_BLOCK 0x6F
#define CCID_MSG_RDR_TO_PC_DATA_BLOCK 0x80
#define CCID_MSG_RDR_TO_PC_PARAMETERS 0x82
#define CCID_MSG_MIN_LEN 10

#define CCID_BULK_TRANSFER_SIZE 1024
#define CCID_INTERRUPT_TRANSFER_SIZE 64

#define CCID_PROTOCOL_T0 0x00000001U
#define CCID_PROTOCOL_T1 0x00000002U

#define CCID_FEATURE_TPDU_EXCHANGE 0x00010000U
#define CCID_FEATURE_SHORT_APDU_EXCHANGE 0x00020000U
#define CCID_FEATURE_EXTENDED_APDU_EXCHANGE 0x00040000U

#define CCID_CMD_STATUS_PROCESSED 0
#define CCID_CMD_STATUS_TIME_EXTENSION 2

#define CCID_T1_MAX_INF_LEN 254
#define CCID_T1_MAX_BLOCK_LEN (3 + CCID_T1_MAX_INF_LEN + 1)
#define CCID_T1_MAX_EXCHANGE_STEPS 24

#define CARD_SW_OK 0x9000

static const uint8_t k_select_mf_apdu[] = {0x00, 0xA4, 0x00, 0x00, 0x00};
static const uint8_t k_select_satochip_apdu[] = {
    0x00, 0xA4, 0x04, 0x00, 0x08, 0x53, 0x61, 0x74,
    0x6f, 0x43, 0x68, 0x69, 0x70};
static const uint8_t k_select_seedkeeper_apdu[] = {
    0x00, 0xA4, 0x04, 0x00, 0x0A, 0x53, 0x65, 0x65, 0x64,
    0x4b, 0x65, 0x65, 0x70, 0x65, 0x72};
static const uint8_t k_get_status_apdu[] = {0xB0, 0x3C, 0x00, 0x00, 0x00};

typedef struct __attribute__((packed)) {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint16_t wTotalLength;
  uint8_t bNumInterfaces;
  uint8_t bConfigurationValue;
  uint8_t iConfiguration;
  uint8_t bmAttributes;
  uint8_t bMaxPower;
} usb_config_desc_min_t;

typedef struct __attribute__((packed)) {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint8_t bInterfaceNumber;
  uint8_t bAlternateSetting;
  uint8_t bNumEndpoints;
  uint8_t bInterfaceClass;
  uint8_t bInterfaceSubClass;
  uint8_t bInterfaceProtocol;
  uint8_t iInterface;
} usb_intf_desc_min_t;

typedef struct __attribute__((packed)) {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint8_t bEndpointAddress;
  uint8_t bmAttributes;
  uint16_t wMaxPacketSize;
  uint8_t bInterval;
} usb_ep_desc_min_t;

typedef struct __attribute__((packed)) {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint16_t bcdCCID;
  uint8_t bMaxSlotIndex;
  uint8_t bVoltageSupport;
  uint32_t dwProtocols;
  uint32_t dwDefaultClock;
  uint32_t dwMaximumClock;
  uint8_t bNumClockSupported;
  uint32_t dwDataRate;
  uint32_t dwMaxDataRate;
  uint8_t bNumDataRatesSupported;
  uint32_t dwMaxIFSD;
  uint32_t dwSynchProtocols;
  uint32_t dwMechanical;
  uint32_t dwFeatures;
  uint32_t dwMaxCCIDMessageLength;
  uint8_t bClassGetResponse;
  uint8_t bClassEnvelope;
  uint16_t wLcdLayout;
  uint8_t bPinSupport;
  uint8_t bMaxCCIDBusySlots;
} ccid_functional_desc_t;

typedef enum {
  BULK_STAGE_IDLE = 0,
  BULK_STAGE_WAIT_POWER_ON_OUT,
  BULK_STAGE_WAIT_POWER_ON_IN,
  BULK_STAGE_WAIT_SET_PARAMETERS_OUT,
  BULK_STAGE_WAIT_SET_PARAMETERS_IN,
  BULK_STAGE_WAIT_APDU_OUT,
  BULK_STAGE_WAIT_APDU_IN,
} bulk_stage_t;

typedef struct {
  uint8_t nad;
  uint8_t pcb;
  const uint8_t *inf;
  size_t inf_len;
} t1_block_view_t;

typedef struct {
  usb_host_client_handle_t client_hdl;
  usb_device_handle_t dev_hdl;
  uint8_t dev_addr;
  uint16_t vid;
  uint16_t pid;
  uint8_t dev_class;
  bool device_open;
  bool interface_claimed;
  uint8_t interface_num;
  uint8_t interface_alt_setting;
  uint8_t bulk_in_ep;
  uint8_t bulk_out_ep;
  uint8_t intr_in_ep;
  uint16_t bulk_in_mps;
  uint16_t bulk_out_mps;
  uint16_t intr_in_mps;
  bool ccid_descriptor_found;
  uint32_t dw_protocols;
  uint32_t dw_default_clock;
  uint32_t dw_data_rate;
  uint32_t dw_max_ifsd;
  uint32_t dw_features;
  uint32_t dw_max_ccid_message_length;
  uint8_t b_class_get_response;
  uint8_t b_class_envelope;
  bool new_dev_pending;
  bool dev_gone_pending;
  bool card_present;
  bool intr_in_flight;
  bool bulk_out_in_flight;
  bool bulk_in_in_flight;
  bulk_stage_t bulk_stage;
  uint8_t seq;
  usb_transfer_t *intr_in_xfer;
  usb_transfer_t *bulk_out_xfer;
  usb_transfer_t *bulk_in_xfer;
  TaskHandle_t client_task;
  TaskHandle_t host_task;
  SemaphoreHandle_t lock;
  SemaphoreHandle_t done;
  SemaphoreHandle_t apdu_done;
  bool host_installed;
  bool client_registered;
  bool probe_active;
  bool factory_reset_mode;
  bool want_applet_identify;
  size_t apdu_step;
  bool custom_apdu_active;
  bool custom_apdu_completed;
  bool custom_apdu_abandoned;
  uint8_t custom_apdu_buf[SMARTCARD_CCID_APDU_MAX_LEN];
  size_t custom_apdu_len;
  uint8_t *custom_resp;
  size_t custom_resp_cap;
  size_t custom_resp_len;
  uint16_t custom_sw;
  esp_err_t custom_result;
  bool atr_t1_present;
  bool atr_inverse_convention;
  uint8_t atr_ta1;
  uint8_t atr_tc1;
  uint8_t atr_t1_waiting;
  uint8_t atr_t1_ifsc;
  uint8_t atr_t1_tccks;
  bool tpdu_exchange;
  uint8_t t1_host_ns;
  uint8_t t1_expected_card_ns;
  bool t1_command_accepted;
  uint8_t t1_exchange_steps;
  uint8_t t1_last_i_block[CCID_T1_MAX_BLOCK_LEN];
  size_t t1_last_i_block_len;
  uint8_t t1_response[SMARTCARD_CCID_RESPONSE_MAX_LEN];
  size_t t1_response_len;
  smartcard_ccid_report_t report;
} ccid_ctx_t;

static ccid_ctx_t s_ctx = {
    .seq = 1,
};

static void ccid_finish_factory_reset_ready(ccid_ctx_t *ctx);

static bool transfer_status_is_ok(usb_transfer_status_t status) {
  return status == USB_TRANSFER_STATUS_COMPLETED ||
         status == USB_TRANSFER_STATUS_OVERFLOW;
}

static uint8_t ccid_command_status(uint8_t status_byte) {
  return (status_byte >> 6) & 0x03;
}

static uint16_t read_le16(const uint8_t *buf) {
  return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

static uint32_t read_le32(const uint8_t *buf) {
  return (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) |
         ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
}

static void write_le32(uint8_t *buf, uint32_t value) {
  buf[0] = value & 0xFF;
  buf[1] = (value >> 8) & 0xFF;
  buf[2] = (value >> 16) & 0xFF;
  buf[3] = (value >> 24) & 0xFF;
}

static const char *ccid_exchange_level_name(uint32_t features) {
  if ((features & CCID_FEATURE_EXTENDED_APDU_EXCHANGE) != 0)
    return SC_TR("exchange.extended_apdu", "Extended APDU");
  if ((features & CCID_FEATURE_SHORT_APDU_EXCHANGE) != 0)
    return SC_TR("exchange.short_apdu", "Short APDU");
  if ((features & CCID_FEATURE_TPDU_EXCHANGE) != 0)
    return "TPDU";
  return i18n_tr_or("common.unknown", "Unknown");
}

static bool ccid_reader_uses_tpdu(const ccid_ctx_t *ctx) {
  if (!ctx)
    return false;
  if ((ctx->dw_features & (CCID_FEATURE_SHORT_APDU_EXCHANGE |
                           CCID_FEATURE_EXTENDED_APDU_EXCHANGE)) != 0) {
    return false;
  }
  if ((ctx->dw_features & CCID_FEATURE_TPDU_EXCHANGE) != 0)
    return true;

  // ACS ACR39U-NF PocketMate II on this board has behaved like TPDU-level
  // CCID. Prefer the safe T=1 wrapper when the descriptor is incomplete.
  return ctx->vid == 0x072F && ctx->pid == 0xB100;
}

static bool ccid_should_set_t1_parameters(const ccid_ctx_t *ctx) {
  if (!ctx || !ctx->atr_t1_present)
    return false;
  return ctx->dw_protocols == 0 || (ctx->dw_protocols & CCID_PROTOCOL_T1) != 0;
}

static void report_update_reader_locked(void);

static void report_set_locked(smartcard_ccid_state_t state, bool terminal,
                              const char *detail) {
  s_ctx.report.state = state;
  s_ctx.report.terminal = terminal;
  if (detail) {
    snprintf(s_ctx.report.detail, sizeof(s_ctx.report.detail), "%s", detail);
  }
}

static void report_setf(smartcard_ccid_state_t state, bool terminal,
                        const char *fmt, ...) {
  if (s_ctx.lock)
    xSemaphoreTake(s_ctx.lock, portMAX_DELAY);
  s_ctx.report.state = state;
  s_ctx.report.terminal = terminal;
  report_update_reader_locked();
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(s_ctx.report.detail, sizeof(s_ctx.report.detail), fmt, ap);
  va_end(ap);
  if (s_ctx.lock)
    xSemaphoreGive(s_ctx.lock);

  ESP_LOGI(TAG, "Report: %s", s_ctx.report.detail);
  if (terminal && s_ctx.done)
    xSemaphoreGive(s_ctx.done);
}

static void report_reset_for_probe(void) {
  if (s_ctx.lock)
    xSemaphoreTake(s_ctx.lock, portMAX_DELAY);
  memset(&s_ctx.report, 0, sizeof(s_ctx.report));
  s_ctx.report.state = SMARTCARD_CCID_STATE_STARTING;
  s_ctx.report.host_started = s_ctx.host_installed;
  s_ctx.report.terminal = false;
  snprintf(s_ctx.report.detail, sizeof(s_ctx.report.detail),
           "%s",
           SC_TR("waiting_reader",
                 "Waiting for a CCID reader. Use a powered USB hub or "
                 "powered adapter."));
  if (s_ctx.lock)
    xSemaphoreGive(s_ctx.lock);
}

static void report_update_reader_locked(void) {
  s_ctx.report.reader_present = s_ctx.device_open && s_ctx.interface_claimed;
  s_ctx.report.card_present = s_ctx.card_present;
  s_ctx.report.dev_addr = s_ctx.dev_addr;
  s_ctx.report.vid = s_ctx.vid;
  s_ctx.report.pid = s_ctx.pid;
  s_ctx.report.interface_num = s_ctx.interface_num;
  s_ctx.report.ccid_protocols = s_ctx.dw_protocols;
  s_ctx.report.ccid_features = s_ctx.dw_features;
  s_ctx.report.ccid_max_msg_len = s_ctx.dw_max_ccid_message_length;
  s_ctx.report.ccid_max_ifsd = s_ctx.dw_max_ifsd;
  s_ctx.report.t1_present = s_ctx.atr_t1_present;
  s_ctx.report.tpdu_mode = s_ctx.tpdu_exchange;
  s_ctx.report.host_started = s_ctx.host_installed;
}

static void report_update_reader(void) {
  if (s_ctx.lock)
    xSemaphoreTake(s_ctx.lock, portMAX_DELAY);
  report_update_reader_locked();
  if (s_ctx.lock)
    xSemaphoreGive(s_ctx.lock);
}

static void report_store_atr(const uint8_t *atr, size_t atr_len) {
  if (s_ctx.lock)
    xSemaphoreTake(s_ctx.lock, portMAX_DELAY);
  if (atr_len > sizeof(s_ctx.report.atr))
    atr_len = sizeof(s_ctx.report.atr);
  memcpy(s_ctx.report.atr, atr, atr_len);
  s_ctx.report.atr_len = atr_len;
  s_ctx.report.card_present = true;
  s_ctx.report.reader_present = true;
  report_update_reader_locked();
  s_ctx.report.state = SMARTCARD_CCID_STATE_ATR_OK;
  s_ctx.report.terminal = false;
  snprintf(s_ctx.report.detail, sizeof(s_ctx.report.detail),
           "%s", SC_TR("atr_read_identifying",
                       "ATR read. Identifying Satochip/SeedKeeper."));
  if (s_ctx.lock)
    xSemaphoreGive(s_ctx.lock);
}

static void report_store_response(const uint8_t *buf, size_t len, uint16_t sw,
                                  const char *applet,
                                  smartcard_ccid_state_t state,
                                  bool terminal, const char *detail) {
  if (s_ctx.lock)
    xSemaphoreTake(s_ctx.lock, portMAX_DELAY);
  if (len > sizeof(s_ctx.report.response))
    len = sizeof(s_ctx.report.response);
  memcpy(s_ctx.report.response, buf, len);
  s_ctx.report.response_len = len;
  s_ctx.report.sw = sw;
  if (applet)
    snprintf(s_ctx.report.applet, sizeof(s_ctx.report.applet), "%s", applet);
  s_ctx.report.state = state;
  s_ctx.report.terminal = terminal;
  report_update_reader_locked();
  if (detail)
    snprintf(s_ctx.report.detail, sizeof(s_ctx.report.detail), "%s", detail);
  if (s_ctx.lock)
    xSemaphoreGive(s_ctx.lock);
  if (terminal && s_ctx.done)
    xSemaphoreGive(s_ctx.done);
}

static void ccid_custom_apdu_clear(ccid_ctx_t *ctx) {
  ctx->custom_apdu_active = false;
  ctx->custom_apdu_completed = false;
  ctx->custom_apdu_abandoned = false;
  memset(ctx->custom_apdu_buf, 0, sizeof(ctx->custom_apdu_buf));
  ctx->custom_apdu_len = 0;
  ctx->custom_resp = NULL;
  ctx->custom_resp_cap = 0;
  ctx->custom_resp_len = 0;
  ctx->custom_sw = 0;
  ctx->custom_result = ESP_ERR_INVALID_STATE;
}

static void ccid_drain_binary_semaphore(SemaphoreHandle_t sem) {
  if (!sem)
    return;
  while (xSemaphoreTake(sem, 0) == pdTRUE) {
  }
}

static void ccid_custom_apdu_finish(ccid_ctx_t *ctx, esp_err_t result,
                                    const uint8_t *resp, size_t resp_len,
                                    uint16_t sw) {
  if (!ctx || !ctx->custom_apdu_active)
    return;

  bool abandoned = ctx->custom_apdu_abandoned;
  ctx->custom_resp_len = abandoned ? 0 : resp_len;
  ctx->custom_sw = abandoned ? 0 : sw;
  ctx->custom_result = abandoned ? ESP_ERR_TIMEOUT : result;

  if (!abandoned && result == ESP_OK) {
    if (resp_len > ctx->custom_resp_cap) {
      ctx->custom_result = ESP_ERR_INVALID_SIZE;
    } else if (resp_len > 0 && ctx->custom_resp && resp) {
      memcpy(ctx->custom_resp, resp, resp_len);
    }
  }

  ctx->custom_apdu_completed = true;
  ctx->custom_apdu_active = false;
  memset(ctx->custom_apdu_buf, 0, sizeof(ctx->custom_apdu_buf));
  ctx->custom_apdu_len = 0;
  ctx->custom_resp = NULL;
  ctx->custom_resp_cap = 0;
  ctx->bulk_stage = BULK_STAGE_IDLE;

  if (!ctx->custom_apdu_abandoned && ctx->apdu_done)
    xSemaphoreGive(ctx->apdu_done);
}

static void ccid_custom_apdu_fail(ccid_ctx_t *ctx, esp_err_t result) {
  ccid_custom_apdu_finish(ctx, result, NULL, 0, 0);
}

static bool ccid_custom_apdu_fail_if_active(ccid_ctx_t *ctx,
                                            esp_err_t result) {
  if (!ctx || !ctx->custom_apdu_active)
    return false;
  ccid_custom_apdu_fail(ctx, result);
  return true;
}

static void ccid_reset_t1_state(ccid_ctx_t *ctx) {
  ctx->t1_host_ns = 0;
  ctx->t1_expected_card_ns = 0;
  ctx->t1_command_accepted = false;
  ctx->t1_exchange_steps = 0;
  ctx->t1_last_i_block_len = 0;
  ctx->t1_response_len = 0;
}

static void ccid_parse_atr_t_params(ccid_ctx_t *ctx, const uint8_t *atr,
                                    size_t atr_len) {
  ctx->atr_t1_present = false;
  ctx->atr_inverse_convention = false;
  ctx->atr_ta1 = 0x11;
  ctx->atr_tc1 = 0x00;
  ctx->atr_t1_waiting = 0x4D;
  ctx->atr_t1_ifsc = 0x20;
  ctx->atr_t1_tccks = 0x10;

  if (!atr || atr_len < 2)
    return;

  ctx->atr_inverse_convention = atr[0] == 0x3F;
  if (ctx->atr_inverse_convention)
    ctx->atr_t1_tccks = 0x12;

  size_t offset = 2;
  uint8_t y = (atr[1] >> 4) & 0x0F;
  uint8_t protocol_for_group = 0;

  for (uint8_t group = 1; group < 8 && offset <= atr_len; group++) {
    uint8_t ta = 0, tb = 0, tc = 0, td = 0;
    bool have_ta = (y & 0x01U) != 0;
    bool have_tb = (y & 0x02U) != 0;
    bool have_tc = (y & 0x04U) != 0;
    bool have_td = (y & 0x08U) != 0;

    if (have_ta) {
      if (offset >= atr_len)
        return;
      ta = atr[offset++];
      if (group == 1)
        ctx->atr_ta1 = ta;
    }
    if (have_tb) {
      if (offset >= atr_len)
        return;
      tb = atr[offset++];
    }
    if (have_tc) {
      if (offset >= atr_len)
        return;
      tc = atr[offset++];
      if (group == 1)
        ctx->atr_tc1 = tc;
    }

    if (protocol_for_group == 1) {
      ctx->atr_t1_present = true;
      if (have_ta && group >= 2 && ta != 0)
        ctx->atr_t1_ifsc = ta;
      if (have_tb && group >= 2)
        ctx->atr_t1_waiting = tb;
      if (have_tc && group >= 2)
        ctx->atr_t1_tccks = tc;
    }

    if (have_td) {
      if (offset >= atr_len)
        return;
      td = atr[offset++];
      protocol_for_group = td & 0x0F;
      if (protocol_for_group == 1)
        ctx->atr_t1_present = true;
      y = (td >> 4) & 0x0F;
    } else {
      y = 0;
    }

    if (y == 0)
      break;
  }
}

static uint8_t ccid_t1_lrc(const uint8_t *data, size_t len) {
  uint8_t lrc = 0;
  for (size_t i = 0; i < len; i++)
    lrc ^= data[i];
  return lrc;
}

static size_t ccid_build_t1_block(uint8_t *out, uint8_t pcb,
                                  const uint8_t *inf, size_t inf_len) {
  if (!out || inf_len > CCID_T1_MAX_INF_LEN)
    return 0;
  out[0] = 0x00;
  out[1] = pcb;
  out[2] = (uint8_t)inf_len;
  if (inf_len > 0 && inf)
    memcpy(&out[3], inf, inf_len);
  out[3 + inf_len] = ccid_t1_lrc(out, 3 + inf_len);
  return 4 + inf_len;
}

static size_t ccid_build_t1_i_block(ccid_ctx_t *ctx, const uint8_t *apdu,
                                    size_t apdu_len, uint8_t *out,
                                    size_t out_cap) {
  if (!ctx || !apdu || !out || apdu_len > CCID_T1_MAX_INF_LEN ||
      out_cap < apdu_len + 4) {
    return 0;
  }
  uint8_t pcb = (ctx->t1_host_ns & 0x01U) ? 0x40 : 0x00;
  return ccid_build_t1_block(out, pcb, apdu, apdu_len);
}

static size_t ccid_build_t1_r_block(uint8_t *out, size_t out_cap, uint8_t nr) {
  if (!out || out_cap < 4)
    return 0;
  uint8_t pcb = 0x80 | ((nr & 0x01U) << 4);
  return ccid_build_t1_block(out, pcb, NULL, 0);
}

static size_t ccid_build_t1_wtx_response(uint8_t *out, size_t out_cap,
                                         uint8_t wtxm) {
  if (!out || out_cap < 5)
    return 0;
  return ccid_build_t1_block(out, 0xE3, &wtxm, 1);
}

static bool ccid_parse_t1_block(const uint8_t *raw, size_t raw_len,
                                t1_block_view_t *out) {
  if (!raw || !out || raw_len < 4)
    return false;
  size_t inf_len = raw[2];
  if (raw_len != inf_len + 4)
    return false;
  if (ccid_t1_lrc(raw, raw_len) != 0)
    return false;
  out->nad = raw[0];
  out->pcb = raw[1];
  out->inf = inf_len ? &raw[3] : NULL;
  out->inf_len = inf_len;
  return true;
}

static bool t1_is_i_block(uint8_t pcb) { return (pcb & 0x80U) == 0x00U; }
static bool t1_is_r_block(uint8_t pcb) { return (pcb & 0xC0U) == 0x80U; }
static bool t1_is_s_block(uint8_t pcb) { return (pcb & 0xC0U) == 0xC0U; }
static bool t1_i_more(uint8_t pcb) { return t1_is_i_block(pcb) && (pcb & 0x20U); }
static uint8_t t1_i_ns(uint8_t pcb) { return (pcb >> 6) & 0x01U; }
static bool t1_is_wtx_request(uint8_t pcb) {
  return t1_is_s_block(pcb) && ((pcb & 0x20U) == 0) && ((pcb & 0x1FU) == 0x03U);
}

static void client_event_cb(const usb_host_client_event_msg_t *event_msg,
                            void *arg) {
  ccid_ctx_t *ctx = (ccid_ctx_t *)arg;
  switch (event_msg->event) {
  case USB_HOST_CLIENT_EVENT_NEW_DEV:
    if (!ctx->device_open && !ctx->new_dev_pending) {
      ctx->dev_addr = event_msg->new_dev.address;
      ctx->new_dev_pending = true;
      report_setf(SMARTCARD_CCID_STATE_WAITING, false,
                  SC_TR("usb_device_found_format",
                        "USB device found at address %u. Checking for CCID."),
                  ctx->dev_addr);
    }
    break;
  case USB_HOST_CLIENT_EVENT_DEV_GONE:
    if (ctx->dev_hdl && event_msg->dev_gone.dev_hdl == ctx->dev_hdl) {
      ctx->dev_gone_pending = true;
      report_setf(SMARTCARD_CCID_STATE_WAITING, false,
                  SC_TR("usb_device_removed",
                        "USB device removed. Waiting for reinsertion."));
    }
    break;
  default:
    break;
  }
}

static void transfer_cb(usb_transfer_t *transfer) {
  ccid_ctx_t *ctx = (ccid_ctx_t *)transfer->context;
  if (!ctx)
    return;

  if (transfer == ctx->intr_in_xfer) {
    ctx->intr_in_flight = false;
    if (ctx->client_task)
      xTaskNotify(ctx->client_task, 1U << 0, eSetBits);
  } else if (transfer == ctx->bulk_out_xfer) {
    ctx->bulk_out_in_flight = false;
    if (ctx->client_task)
      xTaskNotify(ctx->client_task, 1U << 1, eSetBits);
  } else if (transfer == ctx->bulk_in_xfer) {
    ctx->bulk_in_in_flight = false;
    if (ctx->client_task)
      xTaskNotify(ctx->client_task, 1U << 2, eSetBits);
  }
}

static bool ccid_parse_descriptors(ccid_ctx_t *ctx,
                                   const usb_config_desc_t *config_desc) {
  const usb_config_desc_min_t *cfg = (const usb_config_desc_min_t *)config_desc;
  const uint8_t *raw = (const uint8_t *)config_desc;
  const uint16_t total_len = cfg->wTotalLength;
  size_t offset = cfg->bLength;
  bool in_ccid_interface = false;
  bool have_bulk_in = false;
  bool have_bulk_out = false;
  bool have_intr_in = false;
  bool have_interface = false;

  ctx->bulk_in_ep = 0;
  ctx->bulk_out_ep = 0;
  ctx->intr_in_ep = 0;
  ctx->bulk_in_mps = 0;
  ctx->bulk_out_mps = 0;
  ctx->intr_in_mps = 0;
  ctx->ccid_descriptor_found = false;
  ctx->dw_protocols = 0;
  ctx->dw_default_clock = 0;
  ctx->dw_data_rate = 0;
  ctx->dw_max_ifsd = 0;
  ctx->dw_features = 0;
  ctx->dw_max_ccid_message_length = CCID_BULK_TRANSFER_SIZE;
  ctx->b_class_get_response = 0;
  ctx->b_class_envelope = 0;

  while (offset + 2 <= total_len) {
    const uint8_t *desc = raw + offset;
    const uint8_t len = desc[0];
    const uint8_t type = desc[1];
    if (len == 0 || offset + len > total_len)
      break;

    if (type == USB_DESC_TYPE_INTERFACE && len >= sizeof(usb_intf_desc_min_t)) {
      const usb_intf_desc_min_t *intf = (const usb_intf_desc_min_t *)desc;
      in_ccid_interface = (intf->bInterfaceClass == USB_CLASS_CCID);
      if (in_ccid_interface) {
        have_interface = true;
        ctx->interface_num = intf->bInterfaceNumber;
        ctx->interface_alt_setting = intf->bAlternateSetting;
      }
    } else if (in_ccid_interface && type == CCID_DESC_TYPE_FUNCTIONAL) {
      if (len >= sizeof(ccid_functional_desc_t)) {
        const ccid_functional_desc_t *func =
            (const ccid_functional_desc_t *)desc;
        ctx->ccid_descriptor_found = true;
        ctx->dw_protocols = func->dwProtocols;
        ctx->dw_default_clock = func->dwDefaultClock;
        ctx->dw_data_rate = func->dwDataRate;
        ctx->dw_max_ifsd = func->dwMaxIFSD;
        ctx->dw_features = func->dwFeatures;
        ctx->dw_max_ccid_message_length = func->dwMaxCCIDMessageLength;
        ctx->b_class_get_response = func->bClassGetResponse;
        ctx->b_class_envelope = func->bClassEnvelope;
        ESP_LOGI(TAG,
                 "CCID descriptor: slots=%u voltage=0x%02X protocols=0x%08"
                 PRIx32 " features=0x%08" PRIx32 " level=%s clock=%" PRIu32
                 " data_rate=%" PRIu32 " ifsd=%" PRIu32 " max_msg=%" PRIu32,
                 func->bMaxSlotIndex + 1, func->bVoltageSupport,
                 ctx->dw_protocols, ctx->dw_features,
                 ccid_exchange_level_name(ctx->dw_features),
                 ctx->dw_default_clock, ctx->dw_data_rate, ctx->dw_max_ifsd,
                 ctx->dw_max_ccid_message_length);
      }
    } else if (in_ccid_interface && type == USB_DESC_TYPE_ENDPOINT &&
               len >= sizeof(usb_ep_desc_min_t)) {
      const usb_ep_desc_min_t *ep = (const usb_ep_desc_min_t *)desc;
      const uint16_t mps = read_le16((const uint8_t *)&ep->wMaxPacketSize);
      const bool is_in = (ep->bEndpointAddress & 0x80U) != 0;
      const uint8_t transfer_type = ep->bmAttributes & 0x03U;

      if (transfer_type == USB_EP_ATTR_BULK && is_in && !have_bulk_in) {
        have_bulk_in = true;
        ctx->bulk_in_ep = ep->bEndpointAddress;
        ctx->bulk_in_mps = mps;
      } else if (transfer_type == USB_EP_ATTR_BULK && !is_in &&
                 !have_bulk_out) {
        have_bulk_out = true;
        ctx->bulk_out_ep = ep->bEndpointAddress;
        ctx->bulk_out_mps = mps;
      } else if (transfer_type == USB_EP_ATTR_INTR && is_in &&
                 !have_intr_in) {
        have_intr_in = true;
        ctx->intr_in_ep = ep->bEndpointAddress;
        ctx->intr_in_mps = mps;
      }
    }

    if (have_interface && have_bulk_in && have_bulk_out && have_intr_in)
      break;
    offset += len;
  }

  return have_interface && have_bulk_in && have_bulk_out && have_intr_in;
}

static esp_err_t ccid_alloc_transfers(ccid_ctx_t *ctx) {
  if (!ctx->intr_in_xfer) {
    esp_err_t err = usb_host_transfer_alloc(CCID_INTERRUPT_TRANSFER_SIZE, 0,
                                            &ctx->intr_in_xfer);
    if (err != ESP_OK)
      return err;
  }
  if (!ctx->bulk_out_xfer) {
    esp_err_t err = usb_host_transfer_alloc(CCID_BULK_TRANSFER_SIZE, 0,
                                            &ctx->bulk_out_xfer);
    if (err != ESP_OK)
      return err;
  }
  if (!ctx->bulk_in_xfer) {
    esp_err_t err = usb_host_transfer_alloc(CCID_BULK_TRANSFER_SIZE, 0,
                                            &ctx->bulk_in_xfer);
    if (err != ESP_OK)
      return err;
  }
  return ESP_OK;
}

static size_t ccid_build_power_on(uint8_t *buf, uint8_t slot, uint8_t seq) {
  memset(buf, 0, CCID_MSG_MIN_LEN);
  buf[0] = CCID_MSG_POWER_ON;
  write_le32(&buf[1], 0);
  buf[5] = slot;
  buf[6] = seq;
  buf[7] = 0x00;
  return CCID_MSG_MIN_LEN;
}

static size_t ccid_build_set_parameters_t1(ccid_ctx_t *ctx, uint8_t *buf,
                                           uint8_t slot, uint8_t seq) {
  uint8_t params[7] = {0};
  uint8_t ifsc = ctx->atr_t1_ifsc ? ctx->atr_t1_ifsc : 0x20;
  if (ctx->dw_max_ifsd > 0 && ifsc > ctx->dw_max_ifsd)
    ifsc = (uint8_t)ctx->dw_max_ifsd;
  if (ifsc == 0)
    ifsc = 0x20;

  params[0] = ctx->atr_ta1 ? ctx->atr_ta1 : 0x11;
  params[1] = ctx->atr_t1_tccks ? ctx->atr_t1_tccks : 0x10;
  params[2] = ctx->atr_tc1;
  params[3] = ctx->atr_t1_waiting ? ctx->atr_t1_waiting : 0x4D;
  params[4] = 0x00;
  params[5] = ifsc;
  params[6] = 0x00;

  memset(buf, 0, CCID_MSG_MIN_LEN + sizeof(params));
  buf[0] = CCID_MSG_SET_PARAMETERS;
  write_le32(&buf[1], sizeof(params));
  buf[5] = slot;
  buf[6] = seq;
  buf[7] = 0x01;
  memcpy(&buf[CCID_MSG_MIN_LEN], params, sizeof(params));

  ESP_LOGI(TAG,
           "SetParameters T=1: F/D=0x%02X TCCKS=0x%02X GT=%u WI=0x%02X "
           "IFSC=%u NAD=0",
           params[0], params[1], params[2], params[3], params[5]);
  return CCID_MSG_MIN_LEN + sizeof(params);
}

static size_t ccid_build_xfr_block(uint8_t *buf, uint8_t slot, uint8_t seq,
                                   const uint8_t *payload,
                                   size_t payload_len) {
  memset(buf, 0, CCID_MSG_MIN_LEN + payload_len);
  buf[0] = CCID_MSG_XFR_BLOCK;
  write_le32(&buf[1], payload_len);
  buf[5] = slot;
  buf[6] = seq;
  buf[7] = 0x00;
  memcpy(&buf[CCID_MSG_MIN_LEN], payload, payload_len);
  return CCID_MSG_MIN_LEN + payload_len;
}

static esp_err_t ccid_submit_interrupt_listener(ccid_ctx_t *ctx) {
  if (!ctx->device_open || !ctx->intr_in_xfer)
    return ESP_ERR_INVALID_STATE;

  ctx->intr_in_xfer->device_handle = ctx->dev_hdl;
  ctx->intr_in_xfer->bEndpointAddress = ctx->intr_in_ep;
  ctx->intr_in_xfer->num_bytes = ctx->intr_in_xfer->data_buffer_size;
  ctx->intr_in_xfer->timeout_ms = 0;
  ctx->intr_in_xfer->callback = transfer_cb;
  ctx->intr_in_xfer->context = ctx;

  esp_err_t err = usb_host_transfer_submit(ctx->intr_in_xfer);
  if (err == ESP_OK)
    ctx->intr_in_flight = true;
  return err;
}

static esp_err_t ccid_submit_bulk_in(ccid_ctx_t *ctx, bulk_stage_t stage) {
  if (!ctx->device_open || !ctx->bulk_in_xfer || !ctx->dev_hdl)
    return ESP_ERR_INVALID_STATE;

  ctx->bulk_in_xfer->device_handle = ctx->dev_hdl;
  ctx->bulk_in_xfer->bEndpointAddress = ctx->bulk_in_ep;
  ctx->bulk_in_xfer->num_bytes = ctx->bulk_in_xfer->data_buffer_size;
  ctx->bulk_in_xfer->timeout_ms = 0;
  ctx->bulk_in_xfer->callback = transfer_cb;
  ctx->bulk_in_xfer->context = ctx;
  ctx->bulk_stage = stage;

  esp_err_t err = usb_host_transfer_submit(ctx->bulk_in_xfer);
  if (err == ESP_OK)
    ctx->bulk_in_in_flight = true;
  else
    ctx->bulk_stage = BULK_STAGE_IDLE;
  return err;
}

static esp_err_t ccid_submit_power_on(ccid_ctx_t *ctx) {
  if (ctx->bulk_out_in_flight || ctx->bulk_stage != BULK_STAGE_IDLE ||
      !ctx->bulk_out_xfer || !ctx->dev_hdl)
    return ESP_ERR_INVALID_STATE;

  size_t len = ccid_build_power_on(ctx->bulk_out_xfer->data_buffer, 0,
                                   ctx->seq++);
  ctx->bulk_out_xfer->device_handle = ctx->dev_hdl;
  ctx->bulk_out_xfer->bEndpointAddress = ctx->bulk_out_ep;
  ctx->bulk_out_xfer->num_bytes = len;
  ctx->bulk_out_xfer->timeout_ms = 0;
  ctx->bulk_out_xfer->callback = transfer_cb;
  ctx->bulk_out_xfer->context = ctx;

  ctx->bulk_stage = BULK_STAGE_WAIT_POWER_ON_OUT;
  esp_err_t err = usb_host_transfer_submit(ctx->bulk_out_xfer);
  if (err == ESP_OK)
    ctx->bulk_out_in_flight = true;
  else
    ctx->bulk_stage = BULK_STAGE_IDLE;
  return err;
}

static esp_err_t ccid_submit_set_parameters_t1(ccid_ctx_t *ctx) {
  if (ctx->bulk_out_in_flight ||
      ctx->bulk_stage != BULK_STAGE_WAIT_SET_PARAMETERS_OUT ||
      !ctx->bulk_out_xfer || !ctx->dev_hdl) {
    return ESP_ERR_INVALID_STATE;
  }

  size_t len = ccid_build_set_parameters_t1(ctx, ctx->bulk_out_xfer->data_buffer,
                                            0, ctx->seq++);
  ctx->bulk_out_xfer->device_handle = ctx->dev_hdl;
  ctx->bulk_out_xfer->bEndpointAddress = ctx->bulk_out_ep;
  ctx->bulk_out_xfer->num_bytes = len;
  ctx->bulk_out_xfer->timeout_ms = 0;
  ctx->bulk_out_xfer->callback = transfer_cb;
  ctx->bulk_out_xfer->context = ctx;

  esp_err_t err = usb_host_transfer_submit(ctx->bulk_out_xfer);
  if (err == ESP_OK)
    ctx->bulk_out_in_flight = true;
  else
    ctx->bulk_stage = BULK_STAGE_IDLE;
  return err;
}

static const uint8_t *apdu_for_step(size_t step, size_t *len,
                                    const char **name) {
  switch (step) {
  case 0:
    *len = sizeof(k_select_satochip_apdu);
    *name = "Satochip";
    return k_select_satochip_apdu;
  case 1:
    *len = sizeof(k_select_seedkeeper_apdu);
    *name = "SeedKeeper";
    return k_select_seedkeeper_apdu;
  case 2:
    *len = sizeof(k_select_mf_apdu);
    *name = "SELECT MF";
    return k_select_mf_apdu;
  case 3:
    *len = sizeof(k_get_status_apdu);
    *name = "GET STATUS";
    return k_get_status_apdu;
  default:
    *len = 0;
    *name = NULL;
    return NULL;
  }
}

static esp_err_t ccid_submit_apdu_step(ccid_ctx_t *ctx) {
  if (ctx->bulk_out_in_flight || ctx->bulk_stage != BULK_STAGE_WAIT_APDU_OUT ||
      !ctx->bulk_out_xfer || !ctx->dev_hdl)
    return ESP_ERR_INVALID_STATE;

  size_t payload_len = 0;
  const char *name = NULL;
  const bool custom_apdu = ctx->custom_apdu_active;
  const uint8_t *payload = NULL;
  if (custom_apdu) {
    payload = ctx->custom_apdu_buf;
    payload_len = ctx->custom_apdu_len;
    name = "custom";
  } else {
    payload = apdu_for_step(ctx->apdu_step, &payload_len, &name);
  }
  if (!payload || payload_len == 0) {
    if (custom_apdu)
      ccid_custom_apdu_fail(ctx, ESP_ERR_INVALID_ARG);
    return ESP_ERR_NOT_FOUND;
  }

  uint8_t t1_payload[CCID_T1_MAX_BLOCK_LEN];
  if (ctx->tpdu_exchange) {
    size_t t1_len = ccid_build_t1_i_block(ctx, payload, payload_len, t1_payload,
                                          sizeof(t1_payload));
    if (t1_len == 0) {
      if (custom_apdu) {
        ccid_custom_apdu_fail(ctx, ESP_ERR_INVALID_SIZE);
      } else {
        report_setf(SMARTCARD_CCID_STATE_FAIL, true,
                    SC_TR("apdu_too_long_tpdu",
                          "APDU is too long for the current T=1 TPDU "
                          "channel."));
      }
      return ESP_ERR_INVALID_SIZE;
    }
    memcpy(ctx->t1_last_i_block, t1_payload, t1_len);
    ctx->t1_last_i_block_len = t1_len;
    ctx->t1_response_len = 0;
    ctx->t1_command_accepted = false;
    ctx->t1_exchange_steps = 0;
    payload = t1_payload;
    payload_len = t1_len;
  } else if (payload_len > 261) {
    if (custom_apdu) {
      ccid_custom_apdu_fail(ctx, ESP_ERR_INVALID_SIZE);
      } else {
        report_setf(SMARTCARD_CCID_STATE_FAIL, true,
                    SC_TR("apdu_too_long_short",
                          "APDU is too long; this reader only supports short "
                          "APDU tests."));
    }
    return ESP_ERR_INVALID_SIZE;
  }

  uint32_t max_msg = ctx->dw_max_ccid_message_length;
  if (max_msg > 0 && payload_len + CCID_MSG_MIN_LEN > max_msg) {
    if (custom_apdu) {
      ccid_custom_apdu_fail(ctx, ESP_ERR_INVALID_SIZE);
      } else {
        report_setf(SMARTCARD_CCID_STATE_FAIL, true,
                  SC_TR("ccid_message_too_large_format",
                        "CCID message exceeds reader limit: %u/%u."),
                  (unsigned)(payload_len + CCID_MSG_MIN_LEN),
                  (unsigned)max_msg);
    }
    return ESP_ERR_INVALID_SIZE;
  }

  size_t len = ccid_build_xfr_block(ctx->bulk_out_xfer->data_buffer, 0,
                                    ctx->seq++, payload, payload_len);
  ctx->bulk_out_xfer->device_handle = ctx->dev_hdl;
  ctx->bulk_out_xfer->bEndpointAddress = ctx->bulk_out_ep;
  ctx->bulk_out_xfer->num_bytes = len;
  ctx->bulk_out_xfer->timeout_ms = 0;
  ctx->bulk_out_xfer->callback = transfer_cb;
  ctx->bulk_out_xfer->context = ctx;

  ESP_LOGI(TAG, "Submitted APDU %s%u: %s (%s payload=%u)",
           custom_apdu ? "" : "step ", custom_apdu ? 0 : (unsigned)ctx->apdu_step,
           name ? name : "unknown",
           ctx->tpdu_exchange ? "T=1 TPDU" : "APDU",
           (unsigned)payload_len);
  esp_err_t err = usb_host_transfer_submit(ctx->bulk_out_xfer);
  if (err == ESP_OK)
    ctx->bulk_out_in_flight = true;
  else {
    if (custom_apdu)
      ccid_custom_apdu_fail(ctx, err);
    ctx->bulk_stage = BULK_STAGE_IDLE;
  }
  return err;
}

static esp_err_t ccid_submit_custom_apdu(ccid_ctx_t *ctx) {
  if (!ctx || !ctx->custom_apdu_active)
    return ESP_ERR_INVALID_STATE;

  if (!ctx->device_open || !ctx->interface_claimed || !ctx->card_present ||
      ctx->bulk_stage != BULK_STAGE_IDLE || ctx->bulk_out_in_flight ||
      ctx->bulk_in_in_flight) {
    return ESP_ERR_INVALID_STATE;
  }

  ctx->bulk_stage = BULK_STAGE_WAIT_APDU_OUT;
  esp_err_t err = ccid_submit_apdu_step(ctx);
  if (err != ESP_OK) {
    ctx->bulk_stage = BULK_STAGE_IDLE;
    ccid_custom_apdu_fail(ctx, err);
  }
  return err;
}

static void ccid_teardown_device(ccid_ctx_t *ctx) {
  (void)ccid_custom_apdu_fail_if_active(ctx, ESP_ERR_INVALID_STATE);
  if (ctx->device_open && ctx->interface_claimed) {
    (void)usb_host_interface_release(ctx->client_hdl, ctx->dev_hdl,
                                     ctx->interface_num);
  }
  if (ctx->device_open && ctx->dev_hdl) {
    (void)usb_host_device_close(ctx->client_hdl, ctx->dev_hdl);
  }
  ctx->dev_hdl = NULL;
  ctx->dev_addr = 0;
  ctx->device_open = false;
  ctx->interface_claimed = false;
  ctx->bulk_stage = BULK_STAGE_IDLE;
  ctx->intr_in_flight = false;
  ctx->bulk_out_in_flight = false;
  ctx->bulk_in_in_flight = false;
  ctx->new_dev_pending = false;
  ctx->dev_gone_pending = false;
  ctx->card_present = false;
  ctx->interface_num = 0;
  ctx->interface_alt_setting = 0;
  ctx->bulk_in_ep = 0;
  ctx->bulk_out_ep = 0;
  ctx->intr_in_ep = 0;
  ctx->bulk_in_mps = 0;
  ctx->bulk_out_mps = 0;
  ctx->intr_in_mps = 0;
  report_update_reader();
}

static esp_err_t ccid_open_pending_device(ccid_ctx_t *ctx) {
  esp_err_t err =
      usb_host_device_open(ctx->client_hdl, ctx->dev_addr, &ctx->dev_hdl);
  if (err != ESP_OK)
    return err;

  ctx->device_open = true;

  const usb_device_desc_t *dev_desc = NULL;
  err = usb_host_get_device_descriptor(ctx->dev_hdl, &dev_desc);
  if (err == ESP_OK && dev_desc) {
    ctx->vid = dev_desc->idVendor;
    ctx->pid = dev_desc->idProduct;
    ctx->dev_class = dev_desc->bDeviceClass;
    usb_print_device_descriptor(dev_desc);
  }

  const usb_config_desc_t *config_desc = NULL;
  err = usb_host_get_active_config_descriptor(ctx->dev_hdl, &config_desc);
  if (err != ESP_OK) {
    ccid_teardown_device(ctx);
    return err;
  }
  usb_print_config_descriptor(config_desc, NULL);

  if (!ccid_parse_descriptors(ctx, config_desc)) {
    if (ctx->dev_class == 0x09) {
      report_setf(SMARTCARD_CCID_STATE_WAITING, false,
                  SC_TR("usb_hub_only_format",
                        "Only a USB hub was seen: VID=%04x PID=%04x. Confirm "
                        "the reader is downstream and externally powered."),
                  ctx->vid, ctx->pid);
    } else {
      report_setf(SMARTCARD_CCID_STATE_FAIL, true,
                  SC_TR("not_ccid_reader_format",
                        "USB device is not a CCID reader: VID=%04x PID=%04x "
                        "class=%02x."),
                  ctx->vid, ctx->pid, ctx->dev_class);
    }
    ccid_teardown_device(ctx);
    return ESP_FAIL;
  }

  err = usb_host_interface_claim(ctx->client_hdl, ctx->dev_hdl,
                                 ctx->interface_num,
                                 ctx->interface_alt_setting);
  if (err != ESP_OK) {
    report_setf(SMARTCARD_CCID_STATE_FAIL, true,
                SC_TR("claim_interface_failed_format",
                      "Failed to claim CCID interface: %s."),
                esp_err_to_name(err));
    ccid_teardown_device(ctx);
    return err;
  }

  ctx->interface_claimed = true;
  err = ccid_alloc_transfers(ctx);
  if (err != ESP_OK) {
    report_setf(SMARTCARD_CCID_STATE_FAIL, true,
                SC_TR("transfer_alloc_failed_format",
                      "Failed to allocate USB transfer buffers: %s."),
                esp_err_to_name(err));
    ccid_teardown_device(ctx);
    return err;
  }

  if (ctx->lock)
    xSemaphoreTake(ctx->lock, portMAX_DELAY);
  report_update_reader_locked();
  report_set_locked(SMARTCARD_CCID_STATE_READER_READY, false,
                    SC_TR("reader_detected_powering_card",
                          "CCID reader detected. Powering on the card."));
  if (ctx->lock)
    xSemaphoreGive(ctx->lock);

  (void)ccid_submit_interrupt_listener(ctx);
  err = ccid_submit_power_on(ctx);
  if (err != ESP_OK) {
    report_setf(SMARTCARD_CCID_STATE_READER_READY, false,
                SC_TR("reader_detected_wait_card",
                      "Reader detected. Insert or reinsert the card."));
  }

  ctx->new_dev_pending = false;
  return ESP_OK;
}

static void ccid_handle_interrupt_completion(ccid_ctx_t *ctx) {
  usb_transfer_t *transfer = ctx->intr_in_xfer;
  const uint8_t *buf = transfer->data_buffer;
  const size_t actual = transfer->actual_num_bytes;

  if (actual >= 2 && buf[0] == CCID_MSG_NOTIFY_SLOT_CHANGE) {
    const bool present = (buf[1] & 0x01U) != 0;
    ctx->card_present = present;
    report_update_reader();
    if (present && ctx->bulk_stage == BULK_STAGE_IDLE)
      (void)ccid_submit_power_on(ctx);
  }

  if (ctx->device_open && !ctx->dev_gone_pending)
    (void)ccid_submit_interrupt_listener(ctx);
}

static void ccid_handle_power_on_completion(ccid_ctx_t *ctx) {
  usb_transfer_t *transfer = ctx->bulk_in_xfer;
  const uint8_t *buf = transfer->data_buffer;
  const size_t actual = transfer->actual_num_bytes;

  if (!transfer_status_is_ok(transfer->status)) {
    report_setf(SMARTCARD_CCID_STATE_FAIL, true,
                SC_TR("power_on_response_failed_format",
                      "Card power-on response failed: USB status=%d."),
                transfer->status);
    ctx->bulk_stage = BULK_STAGE_IDLE;
    return;
  }
  if (actual < CCID_MSG_MIN_LEN) {
    report_setf(SMARTCARD_CCID_STATE_FAIL, true,
                SC_TR("power_on_response_short_format",
                      "Card power-on response is too short: %u bytes."),
                (unsigned)actual);
    ctx->bulk_stage = BULK_STAGE_IDLE;
    return;
  }

  const uint32_t dw_length = read_le32(&buf[1]);
  const uint8_t cmd_status = ccid_command_status(buf[7]);
  const uint8_t error = buf[8];
  const size_t available = actual > CCID_MSG_MIN_LEN ? actual - CCID_MSG_MIN_LEN : 0;
  size_t atr_len = dw_length;
  if (atr_len > available)
    atr_len = available;

  if (cmd_status != 0 || atr_len == 0) {
    report_setf(SMARTCARD_CCID_STATE_FAIL, true,
                SC_TR("power_on_failed_format",
                      "Card power-on failed: cmd=%u err=%02x. Check card "
                      "seating and external power."),
                cmd_status, error);
    ctx->bulk_stage = BULK_STAGE_IDLE;
    return;
  }

  ctx->card_present = true;
  ccid_parse_atr_t_params(ctx, &buf[CCID_MSG_MIN_LEN], atr_len);
  ctx->tpdu_exchange = ccid_reader_uses_tpdu(ctx);
  ccid_reset_t1_state(ctx);
  ESP_LOGI(TAG,
           "ATR parsed: T=1=%u inverse=%u TA1=0x%02X TC1=0x%02X "
           "BWI/CWI=0x%02X IFSC=%u TPDU=%u",
           ctx->atr_t1_present, ctx->atr_inverse_convention, ctx->atr_ta1,
           ctx->atr_tc1, ctx->atr_t1_waiting, ctx->atr_t1_ifsc,
           ctx->tpdu_exchange);
  report_store_atr(&buf[CCID_MSG_MIN_LEN], atr_len);

  ctx->apdu_step = 0;
  if (ccid_should_set_t1_parameters(ctx)) {
    ctx->bulk_stage = BULK_STAGE_WAIT_SET_PARAMETERS_OUT;
    if (ccid_submit_set_parameters_t1(ctx) != ESP_OK) {
      report_setf(SMARTCARD_CCID_STATE_ATR_OK, true,
                  SC_TR("t1_setup_start_failed",
                        "ATR read, but T=1 parameter setup could not start."));
      ctx->bulk_stage = BULK_STAGE_IDLE;
    }
    return;
  }

  if (ctx->factory_reset_mode) {
    ccid_finish_factory_reset_ready(ctx);
    return;
  }

  ctx->bulk_stage = BULK_STAGE_WAIT_APDU_OUT;
  if (ccid_submit_apdu_step(ctx) != ESP_OK) {
    report_setf(SMARTCARD_CCID_STATE_ATR_OK, true,
                SC_TR("apdu_test_start_failed",
                      "ATR read, but APDU test could not start."));
    ctx->bulk_stage = BULK_STAGE_IDLE;
  }
}

static bool ccid_handle_t1_payload(ccid_ctx_t *ctx, const uint8_t *payload,
                                   size_t payload_len, const uint8_t **resp,
                                   size_t *resp_len) {
  t1_block_view_t block;
  if (!ccid_parse_t1_block(payload, payload_len, &block)) {
    if (ccid_custom_apdu_fail_if_active(ctx, ESP_ERR_INVALID_RESPONSE))
      return false;
    report_setf(SMARTCARD_CCID_STATE_FAIL, true,
                SC_TR("t1_invalid_block_format",
                      "Invalid T=1 response block, length %u."),
                (unsigned)payload_len);
    ctx->bulk_stage = BULK_STAGE_IDLE;
    return false;
  }

  ctx->t1_exchange_steps++;
  if (ctx->t1_exchange_steps > CCID_T1_MAX_EXCHANGE_STEPS) {
    if (ccid_custom_apdu_fail_if_active(ctx, ESP_ERR_TIMEOUT))
      return false;
    report_setf(SMARTCARD_CCID_STATE_FAIL, true,
                SC_TR("t1_exchange_timeout", "T=1 exchange timed out."));
    ctx->bulk_stage = BULK_STAGE_IDLE;
    return false;
  }

  if (t1_is_r_block(block.pcb)) {
    if (ctx->t1_last_i_block_len == 0) {
      if (ccid_custom_apdu_fail_if_active(ctx, ESP_ERR_INVALID_RESPONSE))
        return false;
      report_setf(SMARTCARD_CCID_STATE_FAIL, true,
                  SC_TR("t1_retransmit_without_previous",
                        "The card requested T=1 retransmit, but no previous "
                        "block is available."));
      ctx->bulk_stage = BULK_STAGE_IDLE;
      return false;
    }
    size_t len = ccid_build_xfr_block(ctx->bulk_out_xfer->data_buffer, 0,
                                      ctx->seq++, ctx->t1_last_i_block,
                                      ctx->t1_last_i_block_len);
    ctx->bulk_out_xfer->device_handle = ctx->dev_hdl;
    ctx->bulk_out_xfer->bEndpointAddress = ctx->bulk_out_ep;
    ctx->bulk_out_xfer->num_bytes = len;
    ctx->bulk_out_xfer->timeout_ms = 0;
    ctx->bulk_out_xfer->callback = transfer_cb;
    ctx->bulk_out_xfer->context = ctx;
    ctx->bulk_stage = BULK_STAGE_WAIT_APDU_OUT;
    if (usb_host_transfer_submit(ctx->bulk_out_xfer) == ESP_OK) {
      ctx->bulk_out_in_flight = true;
    } else {
      if (ccid_custom_apdu_fail_if_active(ctx, ESP_FAIL))
        return false;
      report_setf(SMARTCARD_CCID_STATE_FAIL, true,
                  SC_TR("t1_retransmit_submit_failed",
                        "T=1 retransmit submit failed."));
      ctx->bulk_stage = BULK_STAGE_IDLE;
    }
    return false;
  }

  if (t1_is_s_block(block.pcb)) {
    if (!t1_is_wtx_request(block.pcb)) {
      if (ccid_custom_apdu_fail_if_active(ctx, ESP_ERR_NOT_SUPPORTED))
        return false;
      report_setf(SMARTCARD_CCID_STATE_FAIL, true,
                  SC_TR("t1_unsupported_s_block_format",
                        "Unsupported T=1 S-Block: PCB=%02X."),
                  block.pcb);
      ctx->bulk_stage = BULK_STAGE_IDLE;
      return false;
    }
    uint8_t wtxm = block.inf_len > 0 ? block.inf[0] : 1;
    uint8_t wtx_block[CCID_T1_MAX_BLOCK_LEN];
    size_t wtx_len = ccid_build_t1_wtx_response(wtx_block, sizeof(wtx_block),
                                                wtxm);
    size_t len = ccid_build_xfr_block(ctx->bulk_out_xfer->data_buffer, 0,
                                      ctx->seq++, wtx_block, wtx_len);
    ctx->bulk_out_xfer->device_handle = ctx->dev_hdl;
    ctx->bulk_out_xfer->bEndpointAddress = ctx->bulk_out_ep;
    ctx->bulk_out_xfer->num_bytes = len;
    ctx->bulk_out_xfer->timeout_ms = 0;
    ctx->bulk_out_xfer->callback = transfer_cb;
    ctx->bulk_out_xfer->context = ctx;
    ctx->bulk_stage = BULK_STAGE_WAIT_APDU_OUT;
    if (usb_host_transfer_submit(ctx->bulk_out_xfer) == ESP_OK) {
      ctx->bulk_out_in_flight = true;
    } else {
      if (ccid_custom_apdu_fail_if_active(ctx, ESP_FAIL))
        return false;
      report_setf(SMARTCARD_CCID_STATE_FAIL, true,
                  SC_TR("t1_wtx_submit_failed",
                        "T=1 WTX response submit failed."));
      ctx->bulk_stage = BULK_STAGE_IDLE;
    }
    return false;
  }

  if (!t1_is_i_block(block.pcb)) {
    if (ccid_custom_apdu_fail_if_active(ctx, ESP_ERR_NOT_SUPPORTED))
      return false;
    report_setf(SMARTCARD_CCID_STATE_FAIL, true,
                SC_TR("t1_unsupported_response_block_format",
                      "Unsupported T=1 response block: PCB=%02X."),
                block.pcb);
    ctx->bulk_stage = BULK_STAGE_IDLE;
    return false;
  }

  if (!ctx->t1_command_accepted) {
    ctx->t1_command_accepted = true;
    ctx->t1_host_ns ^= 1U;
  }

  uint8_t card_ns = t1_i_ns(block.pcb);
  if (card_ns != ctx->t1_expected_card_ns) {
    uint8_t nack[CCID_T1_MAX_BLOCK_LEN];
    size_t nack_len = ccid_build_t1_r_block(nack, sizeof(nack),
                                            ctx->t1_expected_card_ns);
    size_t len = ccid_build_xfr_block(ctx->bulk_out_xfer->data_buffer, 0,
                                      ctx->seq++, nack, nack_len);
    ctx->bulk_out_xfer->device_handle = ctx->dev_hdl;
    ctx->bulk_out_xfer->bEndpointAddress = ctx->bulk_out_ep;
    ctx->bulk_out_xfer->num_bytes = len;
    ctx->bulk_out_xfer->timeout_ms = 0;
    ctx->bulk_out_xfer->callback = transfer_cb;
    ctx->bulk_out_xfer->context = ctx;
    ctx->bulk_stage = BULK_STAGE_WAIT_APDU_OUT;
    if (usb_host_transfer_submit(ctx->bulk_out_xfer) == ESP_OK) {
      ctx->bulk_out_in_flight = true;
    } else {
      if (ccid_custom_apdu_fail_if_active(ctx, ESP_FAIL))
        return false;
      report_setf(SMARTCARD_CCID_STATE_FAIL, true,
                  SC_TR("t1_nack_submit_failed", "T=1 NACK submit failed."));
      ctx->bulk_stage = BULK_STAGE_IDLE;
    }
    return false;
  }

  if (block.inf_len > 0) {
    if (ctx->t1_response_len + block.inf_len > sizeof(ctx->t1_response)) {
      if (ccid_custom_apdu_fail_if_active(ctx, ESP_ERR_INVALID_SIZE))
        return false;
      report_setf(SMARTCARD_CCID_STATE_FAIL, true,
                  SC_TR("t1_response_too_long", "T=1 response is too long."));
      ctx->bulk_stage = BULK_STAGE_IDLE;
      return false;
    }
    memcpy(&ctx->t1_response[ctx->t1_response_len], block.inf, block.inf_len);
    ctx->t1_response_len += block.inf_len;
  }
  ctx->t1_expected_card_ns ^= 1U;

  if (t1_i_more(block.pcb)) {
    uint8_t ack[CCID_T1_MAX_BLOCK_LEN];
    size_t ack_len = ccid_build_t1_r_block(ack, sizeof(ack),
                                           ctx->t1_expected_card_ns);
    size_t len = ccid_build_xfr_block(ctx->bulk_out_xfer->data_buffer, 0,
                                      ctx->seq++, ack, ack_len);
    ctx->bulk_out_xfer->device_handle = ctx->dev_hdl;
    ctx->bulk_out_xfer->bEndpointAddress = ctx->bulk_out_ep;
    ctx->bulk_out_xfer->num_bytes = len;
    ctx->bulk_out_xfer->timeout_ms = 0;
    ctx->bulk_out_xfer->callback = transfer_cb;
    ctx->bulk_out_xfer->context = ctx;
    ctx->bulk_stage = BULK_STAGE_WAIT_APDU_OUT;
    if (usb_host_transfer_submit(ctx->bulk_out_xfer) == ESP_OK) {
      ctx->bulk_out_in_flight = true;
    } else {
      if (ccid_custom_apdu_fail_if_active(ctx, ESP_FAIL))
        return false;
      report_setf(SMARTCARD_CCID_STATE_FAIL, true,
                  SC_TR("t1_ack_submit_failed", "T=1 ACK submit failed."));
      ctx->bulk_stage = BULK_STAGE_IDLE;
    }
    return false;
  }

  *resp = ctx->t1_response;
  *resp_len = ctx->t1_response_len;
  return true;
}

static void ccid_finish_with_generic_apdu(ccid_ctx_t *ctx,
                                          const uint8_t *resp,
                                          size_t resp_len,
                                          uint16_t sw) {
  report_store_response(resp, resp_len, sw, NULL, SMARTCARD_CCID_STATE_PASS,
                        true,
                        SC_TR("generic_apdu_passed",
                              "CCID, ATR, and safe APDU test passed."));
  ctx->bulk_stage = BULK_STAGE_IDLE;
}

static void ccid_finish_factory_reset_ready(ccid_ctx_t *ctx) {
  if (!ctx)
    return;
  ctx->bulk_stage = BULK_STAGE_IDLE;
  report_setf(SMARTCARD_CCID_STATE_ATR_OK, true,
              SC_TR("factory_reset_ready",
                    "Factory reset mode is ready. Auto-identification is "
                    "paused; follow the page instructions."));
}

static void ccid_handle_set_parameters_completion(ccid_ctx_t *ctx) {
  usb_transfer_t *transfer = ctx->bulk_in_xfer;
  const uint8_t *buf = transfer->data_buffer;
  const size_t actual = transfer->actual_num_bytes;

  if (!transfer_status_is_ok(transfer->status)) {
    report_setf(SMARTCARD_CCID_STATE_FAIL, true,
                SC_TR("t1_parameter_response_failed_format",
                      "T=1 parameter response failed: USB status=%d."),
                transfer->status);
    ctx->bulk_stage = BULK_STAGE_IDLE;
    return;
  }
  if (actual < CCID_MSG_MIN_LEN) {
    report_setf(SMARTCARD_CCID_STATE_FAIL, true,
                SC_TR("t1_parameter_response_short_format",
                      "T=1 parameter response is too short: %u bytes."),
                (unsigned)actual);
    ctx->bulk_stage = BULK_STAGE_IDLE;
    return;
  }

  const uint8_t msg_type = buf[0];
  const uint32_t dw_length = read_le32(&buf[1]);
  const uint8_t cmd_status = ccid_command_status(buf[7]);
  const uint8_t error = buf[8];

  if (cmd_status == CCID_CMD_STATUS_TIME_EXTENSION) {
    if (ccid_submit_bulk_in(ctx, BULK_STAGE_WAIT_SET_PARAMETERS_IN) != ESP_OK)
      ctx->bulk_stage = BULK_STAGE_IDLE;
    return;
  }

  if (cmd_status != CCID_CMD_STATUS_PROCESSED) {
    report_setf(SMARTCARD_CCID_STATE_FAIL, true,
                SC_TR("t1_parameter_setup_failed_format",
                      "T=1 parameter setup failed: cmd=%u err=%02x."),
                cmd_status, error);
    ctx->bulk_stage = BULK_STAGE_IDLE;
    return;
  }

  ESP_LOGI(TAG, "SetParameters response: type=0x%02X len=%" PRIu32
                " err=0x%02X",
           msg_type, dw_length, error);
  if (msg_type != CCID_MSG_RDR_TO_PC_PARAMETERS) {
    ESP_LOGW(TAG, "Unexpected SetParameters response type 0x%02X", msg_type);
  }

  report_setf(SMARTCARD_CCID_STATE_ATR_OK, false,
              SC_TR("t1_parameters_set",
                    "ATR read. T=1 parameters set. Continuing Satochip "
                    "detection."));
  if (ctx->factory_reset_mode) {
    ccid_finish_factory_reset_ready(ctx);
    return;
  }
  ctx->bulk_stage = BULK_STAGE_WAIT_APDU_OUT;
  if (ccid_submit_apdu_step(ctx) != ESP_OK) {
    report_setf(SMARTCARD_CCID_STATE_ATR_OK, true,
                SC_TR("t1_set_apdu_start_failed",
                      "T=1 parameters set, but APDU test could not start."));
    ctx->bulk_stage = BULK_STAGE_IDLE;
  }
}

static void ccid_handle_apdu_completion(ccid_ctx_t *ctx) {
  usb_transfer_t *transfer = ctx->bulk_in_xfer;
  const uint8_t *buf = transfer->data_buffer;
  const size_t actual = transfer->actual_num_bytes;

  if (!transfer_status_is_ok(transfer->status)) {
    if (ccid_custom_apdu_fail_if_active(ctx, ESP_ERR_INVALID_RESPONSE))
      return;
    report_setf(SMARTCARD_CCID_STATE_FAIL, true,
                SC_TR("apdu_response_failed_format",
                      "APDU response failed: USB status=%d."),
                transfer->status);
    ctx->bulk_stage = BULK_STAGE_IDLE;
    return;
  }
  if (actual < CCID_MSG_MIN_LEN) {
    if (ccid_custom_apdu_fail_if_active(ctx, ESP_ERR_INVALID_RESPONSE))
      return;
    report_setf(SMARTCARD_CCID_STATE_FAIL, true,
                SC_TR("apdu_response_short_format",
                      "APDU response is too short: %u bytes."),
                (unsigned)actual);
    ctx->bulk_stage = BULK_STAGE_IDLE;
    return;
  }

  const uint32_t dw_length = read_le32(&buf[1]);
  const uint8_t cmd_status = ccid_command_status(buf[7]);
  const uint8_t error = buf[8];
  const size_t available = actual > CCID_MSG_MIN_LEN ? actual - CCID_MSG_MIN_LEN : 0;
  size_t resp_len = dw_length;
  if (resp_len > available)
    resp_len = available;

  if (cmd_status == CCID_CMD_STATUS_TIME_EXTENSION) {
    esp_err_t err = ccid_submit_bulk_in(ctx, BULK_STAGE_WAIT_APDU_IN);
    if (err != ESP_OK) {
      if (ccid_custom_apdu_fail_if_active(ctx, err))
        return;
      ctx->bulk_stage = BULK_STAGE_IDLE;
    }
    return;
  }

  if (cmd_status != CCID_CMD_STATUS_PROCESSED || resp_len == 0) {
    if (ccid_custom_apdu_fail_if_active(ctx, ESP_FAIL))
      return;
    report_setf(SMARTCARD_CCID_STATE_FAIL, true,
                SC_TR("apdu_execution_failed_format",
                      "APDU execution failed: cmd=%u err=%02x."),
                cmd_status, error);
    ctx->bulk_stage = BULK_STAGE_IDLE;
    return;
  }

  const uint8_t *resp = &buf[CCID_MSG_MIN_LEN];
  if (ctx->tpdu_exchange) {
    if (!ccid_handle_t1_payload(ctx, resp, resp_len, &resp, &resp_len))
      return;
  }

  uint16_t sw = 0;
  if (resp_len >= 2)
    sw = ((uint16_t)resp[resp_len - 2] << 8) | resp[resp_len - 1];
  else {
    if (ccid_custom_apdu_fail_if_active(ctx, ESP_ERR_INVALID_RESPONSE))
      return;
    report_setf(SMARTCARD_CCID_STATE_FAIL, true,
                SC_TR("apdu_response_short_format",
                      "APDU response is too short: %u bytes."),
                (unsigned)resp_len);
    ctx->bulk_stage = BULK_STAGE_IDLE;
    return;
  }

  if (ctx->custom_apdu_active) {
    ccid_custom_apdu_finish(ctx, ESP_OK, resp, resp_len, sw);
    return;
  }

  size_t payload_len = 0;
  const char *name = NULL;
  (void)apdu_for_step(ctx->apdu_step, &payload_len, &name);

  if (ctx->apdu_step == 0 && sw == CARD_SW_OK) {
    report_store_response(resp, resp_len, sw, "Satochip",
                          SMARTCARD_CCID_STATE_APPLET_OK, false,
                          SC_TR("satochip_detected_read_status",
                                "Satochip detected. Reading read-only "
                                "status."));
    ctx->apdu_step = 3;
  } else if (ctx->apdu_step == 1 && sw == CARD_SW_OK) {
    report_store_response(resp, resp_len, sw, "SeedKeeper",
                          SMARTCARD_CCID_STATE_APPLET_OK, false,
                          SC_TR("seedkeeper_detected_read_status",
                                "SeedKeeper detected. Reading read-only "
                                "status."));
    ctx->apdu_step = 3;
  } else if (ctx->apdu_step == 3) {
    smartcard_ccid_state_t state =
        (sw == CARD_SW_OK || sw == 0x9C04) ? SMARTCARD_CCID_STATE_PASS
                                           : SMARTCARD_CCID_STATE_APPLET_OK;
    const char *detail =
        (sw == CARD_SW_OK)
            ? SC_TR("status_read_passed",
                    "Smartcard app detected. Read-only status check passed.")
            : ((sw == 0x9C04)
                   ? SC_TR("card_not_initialized",
                           "Smartcard app detected. The card is not "
                           "initialized.")
                   : SC_TR("status_apdu_non_success",
                           "Smartcard app detected. Status APDU returned a "
                           "non-success code."));
    report_store_response(resp, resp_len, sw,
                          s_ctx.report.applet[0] ? s_ctx.report.applet : NULL,
                          state, true, detail);
    ctx->bulk_stage = BULK_STAGE_IDLE;
    return;
  } else if (ctx->apdu_step == 2) {
    ccid_finish_with_generic_apdu(ctx, resp, resp_len, sw);
    return;
  }

  if (ctx->apdu_step == 0 && sw != CARD_SW_OK) {
    ctx->apdu_step = 1;
  } else if (ctx->apdu_step == 1 && sw != CARD_SW_OK) {
    ctx->apdu_step = 2;
  }

  ctx->bulk_stage = BULK_STAGE_WAIT_APDU_OUT;
  if (ccid_submit_apdu_step(ctx) != ESP_OK) {
    report_store_response(resp, resp_len, sw, NULL, SMARTCARD_CCID_STATE_ATR_OK,
                          true,
                          SC_TR("next_apdu_continue_failed",
                                "ATR read, but the next APDU could not "
                                "continue."));
    ctx->bulk_stage = BULK_STAGE_IDLE;
  }
}

static void ccid_handle_bulk_out_completion(ccid_ctx_t *ctx) {
  if (ctx->bulk_stage == BULK_STAGE_WAIT_POWER_ON_OUT) {
    if (!transfer_status_is_ok(ctx->bulk_out_xfer->status)) {
      report_setf(SMARTCARD_CCID_STATE_FAIL, true,
                  SC_TR("power_on_send_failed_format",
                        "Card power-on send failed: USB status=%d."),
                  ctx->bulk_out_xfer->status);
      ctx->bulk_stage = BULK_STAGE_IDLE;
      return;
    }
    if (ccid_submit_bulk_in(ctx, BULK_STAGE_WAIT_POWER_ON_IN) != ESP_OK)
      ctx->bulk_stage = BULK_STAGE_IDLE;
  } else if (ctx->bulk_stage == BULK_STAGE_WAIT_SET_PARAMETERS_OUT) {
    if (!transfer_status_is_ok(ctx->bulk_out_xfer->status)) {
      report_setf(SMARTCARD_CCID_STATE_FAIL, true,
                  SC_TR("t1_parameter_send_failed_format",
                        "T=1 parameter send failed: USB status=%d."),
                  ctx->bulk_out_xfer->status);
      ctx->bulk_stage = BULK_STAGE_IDLE;
      return;
    }
    if (ccid_submit_bulk_in(ctx, BULK_STAGE_WAIT_SET_PARAMETERS_IN) != ESP_OK)
      ctx->bulk_stage = BULK_STAGE_IDLE;
  } else if (ctx->bulk_stage == BULK_STAGE_WAIT_APDU_OUT) {
    if (!transfer_status_is_ok(ctx->bulk_out_xfer->status)) {
      if (ccid_custom_apdu_fail_if_active(ctx, ESP_FAIL))
        return;
      report_setf(SMARTCARD_CCID_STATE_FAIL, true,
                  SC_TR("apdu_send_failed_format",
                        "APDU send failed: USB status=%d."),
                  ctx->bulk_out_xfer->status);
      ctx->bulk_stage = BULK_STAGE_IDLE;
      return;
    }
    esp_err_t err = ccid_submit_bulk_in(ctx, BULK_STAGE_WAIT_APDU_IN);
    if (err != ESP_OK) {
      if (ccid_custom_apdu_fail_if_active(ctx, err))
        return;
      ctx->bulk_stage = BULK_STAGE_IDLE;
    }
  }
}

static void ccid_handle_bulk_in_completion(ccid_ctx_t *ctx) {
  if (ctx->bulk_stage == BULK_STAGE_WAIT_POWER_ON_IN) {
    ccid_handle_power_on_completion(ctx);
  } else if (ctx->bulk_stage == BULK_STAGE_WAIT_SET_PARAMETERS_IN) {
    ccid_handle_set_parameters_completion(ctx);
  } else if (ctx->bulk_stage == BULK_STAGE_WAIT_APDU_IN) {
    ccid_handle_apdu_completion(ctx);
  }
}

static void host_lib_task(void *arg) {
  (void)arg;
  while (true) {
    uint32_t event_flags = 0;
    esp_err_t err = usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "usb_host_lib_handle_events returned %s",
               esp_err_to_name(err));
      vTaskDelay(pdMS_TO_TICKS(50));
    }
  }
}

static void service_transfer_events(ccid_ctx_t *ctx, uint32_t events) {
  if (!ctx->device_open)
    return;

  if (ctx->dev_gone_pending) {
    ccid_teardown_device(ctx);
    return;
  }
  if ((events & (1U << 0)) != 0U)
    ccid_handle_interrupt_completion(ctx);
  if ((events & (1U << 1)) != 0U)
    ccid_handle_bulk_out_completion(ctx);
  if ((events & (1U << 2)) != 0U)
    ccid_handle_bulk_in_completion(ctx);
}

static void client_task(void *arg) {
  ccid_ctx_t *ctx = (ccid_ctx_t *)arg;
  ctx->client_task = xTaskGetCurrentTaskHandle();

  usb_host_client_config_t client_config = {
      .is_synchronous = false,
      .max_num_event_msg = 8,
      .async =
          {
              .client_event_callback = client_event_cb,
              .callback_arg = ctx,
          },
  };

  esp_err_t err = usb_host_client_register(&client_config, &ctx->client_hdl);
  if (err != ESP_OK) {
    report_setf(SMARTCARD_CCID_STATE_FAIL, true,
                SC_TR("host_client_register_failed_format",
                      "USB Host client registration failed: %s."),
                esp_err_to_name(err));
    vTaskDelete(NULL);
    return;
  }
  ctx->client_registered = true;

  while (true) {
    if (ctx->dev_gone_pending) {
      ccid_teardown_device(ctx);
      continue;
    }
    if (ctx->new_dev_pending && !ctx->device_open) {
      if (ccid_open_pending_device(ctx) != ESP_OK)
        ccid_teardown_device(ctx);
      continue;
    }

    uint32_t notified = 0;
    if (xTaskNotifyWait(0, UINT32_MAX, &notified, 0) == pdTRUE) {
      service_transfer_events(ctx, notified);
      continue;
    }

    err = usb_host_client_handle_events(ctx->client_hdl, pdMS_TO_TICKS(250));
    if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
      ESP_LOGW(TAG, "usb_host_client_handle_events returned %s",
               esp_err_to_name(err));
      vTaskDelay(pdMS_TO_TICKS(50));
    }
  }
}

esp_err_t smartcard_ccid_start(void) {
  if (!s_ctx.lock) {
    s_ctx.lock = xSemaphoreCreateMutex();
    if (!s_ctx.lock)
      return ESP_ERR_NO_MEM;
  }
  if (!s_ctx.done) {
    s_ctx.done = xSemaphoreCreateBinary();
    if (!s_ctx.done)
      return ESP_ERR_NO_MEM;
  }
  if (!s_ctx.apdu_done) {
    s_ctx.apdu_done = xSemaphoreCreateBinary();
    if (!s_ctx.apdu_done)
      return ESP_ERR_NO_MEM;
  }

  if (s_ctx.host_installed)
    return ESP_OK;

  const usb_host_config_t host_config = {
      .skip_phy_setup = false,
      .intr_flags = ESP_INTR_FLAG_LEVEL1,
  };

  esp_err_t err = usb_host_install(&host_config);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    return err;
  s_ctx.host_installed = true;

  esp_err_t power_err = usb_host_lib_set_root_port_power(true);
  if (power_err != ESP_OK && power_err != ESP_ERR_INVALID_STATE) {
    ESP_LOGW(TAG, "USB root port power-on returned %s",
             esp_err_to_name(power_err));
  }

  BaseType_t ok =
      xTaskCreatePinnedToCore(host_lib_task, "ksig_usb_host", 6144, NULL, 5,
                              &s_ctx.host_task, 0);
  if (ok != pdPASS)
    return ESP_ERR_NO_MEM;

  ok = xTaskCreatePinnedToCore(client_task, "ksig_ccid", 8192, &s_ctx, 4,
                               &s_ctx.client_task, 0);
  if (ok != pdPASS)
    return ESP_ERR_NO_MEM;

  return ESP_OK;
}

esp_err_t smartcard_ccid_probe(uint32_t timeout_ms) {
  esp_err_t err = smartcard_ccid_start();
  if (err != ESP_OK) {
    report_setf(SMARTCARD_CCID_STATE_FAIL, true,
                SC_TR("host_start_failed_format",
                      "USB Host startup failed: %s."),
                esp_err_to_name(err));
    return err;
  }

  report_reset_for_probe();
  ccid_drain_binary_semaphore(s_ctx.done);

  if (s_ctx.lock)
    xSemaphoreTake(s_ctx.lock, portMAX_DELAY);
  if (s_ctx.custom_apdu_active) {
    if (s_ctx.lock)
      xSemaphoreGive(s_ctx.lock);
    report_setf(SMARTCARD_CCID_STATE_FAIL, true,
                SC_TR("busy_previous_command",
                      "Smartcard is still processing the previous command. "
                      "Try again shortly."));
    return ESP_ERR_INVALID_STATE;
  }
  s_ctx.seq = 1;
  s_ctx.apdu_step = 0;
  s_ctx.probe_active = true;
  bool start_power_on = s_ctx.device_open && s_ctx.interface_claimed &&
                        s_ctx.bulk_stage == BULK_STAGE_IDLE &&
                        !s_ctx.bulk_out_in_flight && !s_ctx.bulk_in_in_flight;
  if (start_power_on)
    err = ccid_submit_power_on(&s_ctx);
  if (s_ctx.lock)
    xSemaphoreGive(s_ctx.lock);
  if (start_power_on && err != ESP_OK)
    report_setf(SMARTCARD_CCID_STATE_FAIL, true,
                SC_TR("power_on_start_failed_format",
                      "Card power-on start failed: %s."),
                esp_err_to_name(err));

  TickType_t ticks = pdMS_TO_TICKS(timeout_ms ? timeout_ms : 12000);
  if (s_ctx.done && xSemaphoreTake(s_ctx.done, ticks) == pdTRUE) {
    if (s_ctx.lock)
      xSemaphoreTake(s_ctx.lock, portMAX_DELAY);
    s_ctx.probe_active = false;
    if (s_ctx.lock)
      xSemaphoreGive(s_ctx.lock);
    return smartcard_ccid_report_is_success(&s_ctx.report) ? ESP_OK : ESP_FAIL;
  }

  if (s_ctx.lock)
    xSemaphoreTake(s_ctx.lock, portMAX_DELAY);
  s_ctx.probe_active = false;
  if (s_ctx.lock)
    xSemaphoreGive(s_ctx.lock);
  report_setf(SMARTCARD_CCID_STATE_TIMEOUT, true,
              SC_TR("probe_timeout",
                    "Probe timed out: no complete result. Check external "
                    "power, reader, and card."));
  return ESP_ERR_TIMEOUT;
}

void smartcard_ccid_set_factory_reset_mode(bool enabled) {
  if (s_ctx.lock)
    xSemaphoreTake(s_ctx.lock, portMAX_DELAY);
  s_ctx.factory_reset_mode = enabled;
  if (s_ctx.lock)
    xSemaphoreGive(s_ctx.lock);
  ESP_LOGI(TAG, "factory reset mode: %u", enabled ? 1U : 0U);
}

esp_err_t smartcard_ccid_transmit_apdu(const uint8_t *apdu, size_t apdu_len,
                                       uint8_t *response,
                                       size_t response_cap,
                                       size_t *response_len, uint16_t *sw,
                                       uint32_t timeout_ms) {
  if (!apdu || apdu_len == 0 || apdu_len > SMARTCARD_CCID_APDU_MAX_LEN ||
      !response_len || (response_cap > 0 && !response)) {
    return ESP_ERR_INVALID_ARG;
  }

  *response_len = 0;
  if (sw)
    *sw = 0;

  esp_err_t err = smartcard_ccid_start();
  if (err != ESP_OK)
    return err;

  if (s_ctx.lock)
    xSemaphoreTake(s_ctx.lock, portMAX_DELAY);

  if (s_ctx.custom_apdu_active || s_ctx.probe_active ||
      s_ctx.bulk_stage != BULK_STAGE_IDLE || !s_ctx.device_open ||
      !s_ctx.interface_claimed || !s_ctx.card_present) {
    if (s_ctx.lock)
      xSemaphoreGive(s_ctx.lock);
    return ESP_ERR_INVALID_STATE;
  }

  ccid_drain_binary_semaphore(s_ctx.apdu_done);
  ccid_custom_apdu_clear(&s_ctx);
  memcpy(s_ctx.custom_apdu_buf, apdu, apdu_len);
  s_ctx.custom_apdu_len = apdu_len;
  s_ctx.custom_resp = response;
  s_ctx.custom_resp_cap = response_cap;
  s_ctx.custom_resp_len = 0;
  s_ctx.custom_sw = 0;
  s_ctx.custom_result = ESP_ERR_INVALID_STATE;
  s_ctx.custom_apdu_abandoned = false;
  s_ctx.custom_apdu_completed = false;
  s_ctx.custom_apdu_active = true;

  err = ccid_submit_custom_apdu(&s_ctx);
  if (s_ctx.lock)
    xSemaphoreGive(s_ctx.lock);

  if (err != ESP_OK) {
    if (s_ctx.lock)
      xSemaphoreTake(s_ctx.lock, portMAX_DELAY);
    if (s_ctx.custom_apdu_active)
      ccid_custom_apdu_clear(&s_ctx);
    if (s_ctx.lock)
      xSemaphoreGive(s_ctx.lock);
    return err;
  }

  TickType_t ticks = pdMS_TO_TICKS(timeout_ms ? timeout_ms : 12000);
  if (!s_ctx.apdu_done || xSemaphoreTake(s_ctx.apdu_done, ticks) != pdTRUE) {
    if (s_ctx.lock)
      xSemaphoreTake(s_ctx.lock, portMAX_DELAY);
    if (s_ctx.custom_apdu_active) {
      s_ctx.custom_apdu_abandoned = true;
      s_ctx.custom_resp = NULL;
      s_ctx.custom_resp_cap = 0;
      s_ctx.custom_resp_len = 0;
      s_ctx.custom_sw = 0;
      s_ctx.custom_result = ESP_ERR_TIMEOUT;
    }
    if (s_ctx.lock)
      xSemaphoreGive(s_ctx.lock);
    return ESP_ERR_TIMEOUT;
  }

  if (s_ctx.lock)
    xSemaphoreTake(s_ctx.lock, portMAX_DELAY);
  err = s_ctx.custom_result;
  *response_len = s_ctx.custom_resp_len;
  if (sw)
    *sw = s_ctx.custom_sw;
  ccid_custom_apdu_clear(&s_ctx);
  if (s_ctx.lock)
    xSemaphoreGive(s_ctx.lock);

  return err;
}

void smartcard_ccid_snapshot(smartcard_ccid_report_t *out) {
  if (!out)
    return;
  if (s_ctx.lock)
    xSemaphoreTake(s_ctx.lock, portMAX_DELAY);
  *out = s_ctx.report;
  if (s_ctx.lock)
    xSemaphoreGive(s_ctx.lock);
}

#endif

static void hex_append(char *out, size_t out_len, const uint8_t *data,
                       size_t data_len, size_t max_bytes) {
  size_t used = strlen(out);
  if (used >= out_len)
    return;
  size_t show = data_len < max_bytes ? data_len : max_bytes;
  for (size_t i = 0; i < show && used + 3 < out_len; i++) {
    int n = snprintf(out + used, out_len - used, "%02X", data[i]);
    if (n <= 0)
      return;
    used += (size_t)n;
    if (i + 1 < show && used + 2 < out_len) {
      out[used++] = ' ';
      out[used] = '\0';
    }
  }
  if (data_len > show && used + 8 < out_len)
    snprintf(out + used, out_len - used, " ...");
}

static const char *bool_text(uint8_t value) {
  return value ? i18n_tr_or("common.yes", "Yes")
               : i18n_tr_or("common.no", "No");
}

static void append_status_parse(char *out, size_t out_len,
                                const smartcard_ccid_report_t *report) {
  if (!out || !report || !report->applet[0])
    return;

  size_t data_len = report->response_len;
  if (data_len >= 2)
    data_len -= 2;
  if (data_len < 4) {
    if (report->sw == 0x9C04) {
      size_t used = strlen(out);
      if (used < out_len)
        snprintf(out + used, out_len - used, "\n%s",
                 SC_TR("card_status_not_initialized",
                       "Card status: not initialized"));
    }
    return;
  }

  const uint8_t *r = report->response;
  size_t used = strlen(out);
  if (used >= out_len)
    return;

  int n = snprintf(out + used, out_len - used,
                   SC_TR("card_status_protocol_format",
                         "\nCard status: protocol %u.%u / app %u.%u"),
                   (unsigned)r[0],
                   (unsigned)r[1], (unsigned)r[2], (unsigned)r[3]);
  if (n <= 0)
    return;
  used += (size_t)n;

  if (data_len >= 8 && used < out_len) {
    n = snprintf(out + used, out_len - used,
                 SC_TR("pin_remaining_format",
                       "\nPIN remaining: PIN0=%u PUK0=%u PIN1=%u PUK1=%u"),
                 (unsigned)r[4], (unsigned)r[5], (unsigned)r[6],
                 (unsigned)r[7]);
    if (n > 0)
      used += (size_t)n;
  }

  if (data_len >= 12 && used < out_len) {
    snprintf(out + used, out_len - used,
             SC_TR("status_flags_format",
                   "\n2FA: %s / Seed: %s / Initialized: %s / Secure "
                   "channel: %s"),
             bool_text(r[8]), bool_text(r[9]), bool_text(r[10]),
             bool_text(r[11]));
  } else if (report->sw == 0x9C04 && used < out_len) {
    snprintf(out + used, out_len - used, "\n%s",
             SC_TR("card_status_not_initialized",
                   "Card status: not initialized"));
  }
}

const char *smartcard_ccid_state_name(smartcard_ccid_state_t state) {
  switch (state) {
  case SMARTCARD_CCID_STATE_IDLE:
    return SC_TR("state.idle", "Not checked");
  case SMARTCARD_CCID_STATE_STARTING:
    return SC_TR("state.starting", "Starting");
  case SMARTCARD_CCID_STATE_WAITING:
    return SC_TR("state.waiting", "Waiting for device");
  case SMARTCARD_CCID_STATE_READER_READY:
    return SC_TR("state.reader_ready", "Reader detected");
  case SMARTCARD_CCID_STATE_ATR_OK:
    return SC_TR("state.atr_ok", "ATR read");
  case SMARTCARD_CCID_STATE_APPLET_OK:
    return SC_TR("state.applet_ok", "App detected");
  case SMARTCARD_CCID_STATE_PASS:
    return SC_TR("state.pass", "Passed");
  case SMARTCARD_CCID_STATE_TIMEOUT:
    return SC_TR("state.timeout", "Timed out");
  case SMARTCARD_CCID_STATE_FAIL:
    return SC_TR("state.fail", "Failed");
  case SMARTCARD_CCID_STATE_UNSUPPORTED:
    return SC_TR("state.unsupported", "Unsupported");
  default:
    return i18n_tr_or("common.unknown", "Unknown");
  }
}

bool smartcard_ccid_report_is_success(const smartcard_ccid_report_t *report) {
  if (!report)
    return false;
  return report->state == SMARTCARD_CCID_STATE_PASS ||
         report->state == SMARTCARD_CCID_STATE_APPLET_OK ||
         report->state == SMARTCARD_CCID_STATE_ATR_OK;
}

void smartcard_ccid_format_report(char *out, size_t out_len) {
  if (!out || out_len == 0)
    return;

  smartcard_ccid_report_t report;
  smartcard_ccid_snapshot(&report);

  snprintf(out, out_len,
           SC_TR("report_format",
                 "State: %s\n"
                 "Reader: %s\n"
                 "Card: %s\n"
                 "VID/PID: %04X:%04X\n"
                 "Interface: %u\n"
                 "Application: %s\n"
                 "SW: %04X\n"
                 "%s"),
           smartcard_ccid_state_name(report.state),
           report.reader_present ? SC_TR("reader.detected", "Detected")
                                 : SC_TR("reader.not_detected",
                                         "Not detected"),
           report.card_present ? SC_TR("card.powered", "Powered")
                               : SC_TR("card.not_powered", "Not powered"),
           report.vid, report.pid,
           (unsigned)report.interface_num,
           report.applet[0] ? report.applet
                            : SC_TR("applet.not_detected", "Not detected"),
           report.sw,
           report.detail);

  if (report.atr_len > 0) {
    size_t used = strlen(out);
    if (used + 12 < out_len) {
      snprintf(out + used, out_len - used, "\n%s", SC_TR("atr", "ATR:"));
      hex_append(out, out_len, report.atr, report.atr_len, 32);
    }
  }

  if (report.response_len > 0) {
    size_t used = strlen(out);
    if (used + 14 < out_len) {
      snprintf(out + used, out_len - used, "\n%s",
               SC_TR("response", "Response:"));
      hex_append(out, out_len, report.response, report.response_len, 48);
    }
  }

  append_status_parse(out, out_len, &report);
}
