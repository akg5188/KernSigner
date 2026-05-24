/*
 * Scan Page
 * Universal QR content detection: PSBT, message, descriptor, address, mnemonic
 */

#include "scan.h"
#include "../../../components/cUR/src/types/bytes_type.h"
#include "../../../components/cUR/src/types/cbor_data.h"
#include "../../../components/cUR/src/types/cbor_decoder.h"
#include "../../../components/cUR/src/types/cbor_encoder.h"
#include "../../../components/cUR/src/types/psbt.h"
#include "../../../components/cUR/src/ur_encoder.h"
#include "../../core/base43.h"
#include "../../core/btc_derivation.h"
#include "../../core/eip712.h"
#include "../../core/evm.h"
#include "../../core/key.h"
#include "../../core/message_sign.h"
#include "../../core/mnemonic_slots.h"
#include "../../core/psbt.h"
#include "../../core/storage.h"
#include "../../core/custom_derivation.h"
#include "../../core/wallet.h"
#include "../../i18n/i18n.h"
#include "../../qr/encoder.h"
#include "../../qr/parser.h"
#include "../../qr/scanner.h"
#include "../../qr/viewer.h"
#include "../../smartcard/smartcard_satochip.h"
#include "../../ui/assets/icons_24.h"
#include "../../ui/dialog.h"
#include "../../ui/input_helpers.h"
#include "../../ui/menu.h"
#include "../../ui/sankey.h"
#include "../../ui/theme.h"
#include "../../utils/secure_mem.h"
#include "../load_descriptor_storage.h"
#include "../shared/address_checker.h"
#include "../shared/descriptor_loader.h"
#include "../shared/mnemonic_slots_page.h"
#include "../shared/sensitive_pin.h"
#include <ctype.h>
#include <cJSON.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/idf_additions.h>
#include <freertos/task.h>
#include <inttypes.h>
#include <lvgl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wally_address.h>
#include <wally_bip32.h>
#include <wally_bip39.h>
#include <wally_core.h>
#include <wally_crypto.h>
#include <wally_psbt.h>
#include <wally_psbt_members.h>
#include <wally_script.h>
#include <wally_transaction.h>

static const char *TAG = "KSIG_SCAN";

typedef enum {
  OUTPUT_TYPE_SELF_TRANSFER,
  OUTPUT_TYPE_CHANGE,
  OUTPUT_TYPE_SPEND,
} output_type_t;

typedef struct {
  size_t index;
  output_type_t type;
  uint64_t value;
  char *address;
  uint32_t address_index;
} classified_output_t;

// UI components
static lv_obj_t *scan_screen = NULL;
static lv_obj_t *psbt_info_container = NULL;
static sankey_diagram_t *tx_diagram = NULL;
static ui_menu_t *multisig_menu = NULL;
static void (*return_callback)(void) = NULL;
static void (*saved_return_callback)(void) = NULL;

// PSBT data
static struct wally_psbt *current_psbt = NULL;
static char *psbt_base64 = NULL;
static char *signed_psbt_base64 = NULL;
static bool is_testnet = false;
static int scanned_qr_format = FORMAT_NONE;
static int parsed_psbt_preferred_export_format = -1;
static bool skip_verification = false;

// Message signing data
static parsed_sign_message_t current_message = {0};
static bool is_message_sign = false;

// Mnemonic data
static char *scanned_mnemonic = NULL;
static bool web3_info_active = false;
static bool smartcard_web3_mode = false;
static bool unified_scan_mode = false;
static bool pending_web3_from_ur = false;

#define WEB3_DEFAULT_DERIVATION_PATH "m/44'/60'/0'/0/0"
#define WEB3_REQUEST_TYPE_LEGACY_TX 1
#define WEB3_REQUEST_TYPE_TYPED_DATA 2
#define WEB3_REQUEST_TYPE_PERSONAL 3
#define WEB3_REQUEST_TYPE_TYPED_TX 4
#define WEB3_REQUEST_ID_BYTES_MAX 128

typedef enum {
  WEB3_REQUEST_ID_NONE = 0,
  WEB3_REQUEST_ID_STRING,
  WEB3_REQUEST_ID_UINT,
  WEB3_REQUEST_ID_BYTES,
  WEB3_REQUEST_ID_UUID,
} web3_request_id_kind_t;

typedef struct {
  bool valid;
  char wallet[48];
  char action[64];
  char chain[64];
  char origin[128];
  char request_id[96];
  uint8_t request_id_uuid[16];
  uint8_t request_id_bytes[WEB3_REQUEST_ID_BYTES_MAX];
  size_t request_id_bytes_len;
  uint64_t request_id_uint;
  uint64_t request_id_tag;
  web3_request_id_kind_t request_id_kind;
  char path[96];
  char address[72];
  int request_type_id;
  int chain_id;
  uint8_t *sign_data;
  size_t sign_data_len;
  char detail[256];
} web3_sign_request_t;

typedef enum {
  WEB3_SIGN_SOURCE_NONE = 0,
  WEB3_SIGN_SOURCE_SMARTCARD = 1,
  WEB3_SIGN_SOURCE_MNEMONIC = 2,
} web3_sign_source_t;

typedef enum {
  BTC_SIGN_SOURCE_NONE = 0,
  BTC_SIGN_SOURCE_MNEMONIC,
  BTC_SIGN_SOURCE_SMARTCARD,
} btc_sign_source_t;

typedef struct {
  const uint8_t *payload;
  size_t payload_len;
  const uint8_t *next;
  bool is_list;
} web3_rlp_item_t;

typedef struct {
  int tx_type;
  char chain_id[48];
  char nonce[48];
  char gas_limit[48];
  char gas_price[80];
  char max_priority_fee[80];
  char max_fee[80];
  char value[96];
  char to[48];
  char data_len[32];
  char data_note[96];
  char method[80];
  char token_target[48];
  char token_from[48];
  char token_amount[96];
  char access_list_count[32];
  char error[96];
  size_t payload_len;
  bool has_to;
  bool value_is_zero;
  bool valid;
} web3_tx_summary_t;

static web3_sign_request_t pending_web3_request;
static web3_sign_source_t pending_web3_source = WEB3_SIGN_SOURCE_NONE;
static bool web3_summary_back_to_choice = false;
static btc_sign_source_t pending_btc_source = BTC_SIGN_SOURCE_NONE;
static ui_text_input_t btc_satochip_pin_input;
static bool btc_satochip_pin_input_active = false;
static ui_text_input_t web3_pin_input;
static bool web3_pin_input_active = false;

#define WEB3_SATOCHIP_SIGN_TASK_STACK_SIZE 16384
#define WEB3_SATOCHIP_SIGN_TASK_PSRAM_STACK_SIZE 32768

static lv_obj_t *web3_sign_progress_dialog;
static lv_timer_t *web3_sign_poll_timer;
static TaskHandle_t web3_sign_task_handle;
static volatile bool web3_sign_task_done;
static volatile bool web3_sign_task_with_caps;
static esp_err_t web3_sign_task_err = ESP_OK;
static char web3_sign_task_pin[80];
static char web3_sign_task_path[96];
static uint8_t web3_sign_task_digest[32];
static smartcard_satochip_signature_t web3_sign_task_sig;

#define BTC_SATOCHIP_SIGN_TASK_STACK_SIZE 32768
static lv_obj_t *btc_satochip_sign_progress_dialog;
static lv_timer_t *btc_satochip_sign_poll_timer;
static TaskHandle_t btc_satochip_sign_task_handle;
static volatile bool btc_satochip_sign_task_done;
static volatile bool btc_satochip_sign_task_with_caps;
static esp_err_t btc_satochip_sign_task_err = ESP_OK;
static char btc_satochip_sign_task_pin[80];
static char btc_satochip_sign_task_detail[256];
static size_t btc_satochip_signatures_added;

// Forward declarations
static void back_button_cb(lv_event_t *e);
static void return_from_qr_scanner_cb(void);
static bool parse_and_display_psbt(const char *base64_data);
static void cleanup_psbt_data(void);
static bool create_psbt_info_display(void);
static output_type_t classify_output(size_t output_index,
                                     const struct wally_tx_output *tx_output,
                                     const struct wally_tx *global_tx,
                                     uint32_t *address_index_out);
static void sign_button_cb(lv_event_t *e);
static void return_from_qr_viewer_cb(void);
static bool check_psbt_mismatch(void);
static void mismatch_dialog_cb(void *user_data);
static void show_multisig_options_menu(void);
static void return_from_descriptor_scanner_cb(void);
static void create_message_sign_display(void);
static void message_sign_button_cb(lv_event_t *e);
static void handle_descriptor_content(const char *descriptor_str);
static void handle_address_content(const char *content);
static void handle_mnemonic_content(const char *data, size_t len);
static void show_scanned_sign_payload(void);
static void return_from_sign_slots_cb(void);
static void show_btc_source_choice(void);
static void btc_source_mnemonic_cb(lv_event_t *e);
static void btc_source_satochip_cb(lv_event_t *e);
static void btc_source_back_cb(lv_event_t *e);
static void btc_satochip_pin_ready_cb(lv_event_t *e);
static void btc_satochip_pin_back_cb(lv_event_t *e);
static bool handle_web3_relay_content(const char *content);
static void web3_relay_info_done_cb(void *user_data);
static void web3_request_clear(web3_sign_request_t *req);
static void web3_show_source_choice(void);
static void web3_show_pin_input(const web3_sign_request_t *req);
static void web3_show_request_summary(void);
static void web3_show_request_summary_for_source(web3_sign_source_t source,
                                                 bool back_to_choice);
static void web3_source_mnemonic_cb(lv_event_t *e);
static void web3_source_satochip_cb(lv_event_t *e);
static void web3_summary_back_cb(lv_event_t *e);
static void web3_summary_sign_cb(lv_event_t *e);
static void web3_pin_sign_after_summary_cb(lv_event_t *e);
static void web3_mnemonic_sign_cb(lv_timer_t *timer);
static bool web3_sign_with_mnemonic(void);
static lv_obj_t *web3_prepare_page(void);
static void web3_add_detail_field(lv_obj_t *parent, const char *title,
                                  const char *value, bool wrap);
static int web3_estimate_visual_lines(const char *text, int columns);
static int web3_detail_field_height(const char *title, const char *value,
                                    bool wrap);
static void web3_add_transaction_details(lv_obj_t *parent,
                                         const web3_sign_request_t *req);
static void web3_add_typed_data_details(lv_obj_t *parent,
                                        const web3_sign_request_t *req);
static void web3_resume_current_step(void);
static bool web3_compose_signature_bytes(const web3_sign_request_t *req,
                                         const uint8_t compact[64],
                                         uint8_t recovery_id,
                                         uint8_t signature[72],
                                         size_t *signature_len);
static bool contains_ci(const char *haystack, const char *needle);
static bool starts_ci(const char *text, const char *prefix);

static int max_i(int a, int b) { return a > b ? a : b; }

static const char *scan_tr(const char *key, const char *fallback) {
  return i18n_tr_or(key, fallback);
}

static void short_middle(char *dst, size_t dst_len, const char *src,
                         size_t prefix, size_t suffix) {
  if (!dst || dst_len == 0)
    return;
  dst[0] = '\0';
  if (!src)
    return;
  size_t len = strlen(src);
  if (len <= prefix + suffix + 3 || dst_len <= prefix + suffix + 4) {
    snprintf(dst, dst_len, "%s", src);
    return;
  }
  snprintf(dst, dst_len, "%.*s...%s", (int)prefix, src, src + len - suffix);
}

static lv_obj_t *web3_prepare_page(void) {
  qr_scanner_page_hide();
  qr_scanner_page_destroy();
  if (!scan_screen)
    scan_screen = theme_create_page_container(lv_screen_active());
  lv_obj_move_foreground(scan_screen);
  lv_obj_clean(scan_screen);
  theme_apply_screen(scan_screen);
  lv_obj_clear_flag(scan_screen, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_flex_flow(scan_screen, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(scan_screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(scan_screen, theme_get_default_padding(), 0);
  lv_obj_set_style_pad_gap(scan_screen, theme_get_small_padding(), 0);
  return scan_screen;
}

static lv_obj_t *web3_create_fixed_title(lv_obj_t *root, const char *text) {
  lv_obj_t *header = lv_obj_create(root);
  lv_obj_set_width(header, LV_PCT(100));
  lv_obj_set_height(header, max_i(theme_get_corner_button_height() +
                                      theme_get_small_padding() * 3,
                                  76));
  theme_apply_transparent_container(header);
  lv_obj_set_style_pad_top(header, theme_get_small_padding(), 0);
  lv_obj_set_style_pad_bottom(header, theme_get_small_padding(), 0);
  lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = theme_create_label(header, text ? text : "", false);
  lv_obj_set_width(title, LV_PCT(50));
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(title, theme_font_medium(), 0);
  lv_obj_set_style_text_color(title, main_color(), 0);
  lv_obj_center(title);
  return title;
}

static bool web3_bytes_are_readable_text(const uint8_t *data, size_t len) {
  if (!data || len == 0)
    return false;
  size_t readable = 0;
  for (size_t i = 0; i < len; i++) {
    uint8_t c = data[i];
    if (c == '\n' || c == '\r' || c == '\t' ||
        (c >= 0x20 && c < 0x7f) || c >= 0x80) {
      readable++;
    }
  }
  return readable * 100U / len >= 92U;
}

static char *web3_pretty_json_like_alloc(const uint8_t *data, size_t len) {
  if (!data || len == 0)
    return NULL;
  size_t first = 0;
  while (first < len && isspace((unsigned char)data[first]))
    first++;
  if (first >= len || (data[first] != '{' && data[first] != '['))
    return NULL;

  size_t cap = len * 3U + 1U;
  char *out = malloc(cap);
  if (!out)
    return NULL;
  size_t pos = 0;
  int depth = 0;
  bool in_string = false;
  bool escaped = false;

  for (size_t i = 0; i < len && pos + 4 < cap; i++) {
    char c = (char)data[i];
    if (in_string) {
      out[pos++] = c;
      if (escaped)
        escaped = false;
      else if (c == '\\')
        escaped = true;
      else if (c == '"')
        in_string = false;
      continue;
    }

    if (c == '"') {
      in_string = true;
      out[pos++] = c;
    } else if (c == '{' || c == '[') {
      out[pos++] = c;
      out[pos++] = '\n';
      depth++;
      for (int j = 0; j < depth && pos + 2 < cap; j++)
        out[pos++] = ' ';
    } else if (c == '}' || c == ']') {
      out[pos++] = '\n';
      if (depth > 0)
        depth--;
      for (int j = 0; j < depth && pos + 2 < cap; j++)
        out[pos++] = ' ';
      out[pos++] = c;
    } else if (c == ',') {
      out[pos++] = c;
      out[pos++] = '\n';
      for (int j = 0; j < depth && pos + 2 < cap; j++)
        out[pos++] = ' ';
    } else if (c == ':') {
      out[pos++] = c;
      out[pos++] = ' ';
    } else if (!isspace((unsigned char)c)) {
      out[pos++] = c;
    }
  }
  out[pos] = '\0';
  return out;
}

static char *web3_format_full_data_alloc(const uint8_t *data, size_t len,
                                         bool prefer_text) {
  if (!data || len == 0) {
    char *empty = malloc(2);
    if (empty)
      snprintf(empty, 2, "-");
    return empty;
  }

  if (prefer_text || web3_bytes_are_readable_text(data, len)) {
    char *pretty = web3_pretty_json_like_alloc(data, len);
    if (pretty)
      return pretty;
    char *text = malloc(len + 1);
    if (!text)
      return NULL;
    memcpy(text, data, len);
    text[len] = '\0';
    return text;
  }

  size_t line_breaks = len / 24U;
  size_t cap = len * 2U + len / 4U + line_breaks + 8U;
  char *hex = malloc(cap);
  if (!hex)
    return NULL;
  size_t pos = 0;
  for (size_t i = 0; i < len && pos + 4 < cap; i++) {
    pos += (size_t)snprintf(hex + pos, cap - pos, "%02x", data[i]);
    if (i + 1U < len) {
      if ((i + 1U) % 24U == 0)
        hex[pos++] = '\n';
      else if ((i + 1U) % 4U == 0)
        hex[pos++] = ' ';
    }
  }
  hex[pos] = '\0';
  return hex;
}

static void web3_add_detail_field(lv_obj_t *parent, const char *title,
                                  const char *value, bool wrap) {
  if (!parent || !title)
    return;

  lv_obj_t *box = lv_obj_create(parent);
  lv_obj_set_width(box, LV_PCT(100));
  lv_obj_set_height(box, web3_detail_field_height(title, value, wrap));
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);
  lv_obj_set_style_bg_color(box, bg_color(), 0);
  lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(box, highlight_color(), 0);
  lv_obj_set_style_border_width(box, 2, 0);
  lv_obj_set_style_radius(box, 8, 0);
  lv_obj_set_style_pad_all(box, theme_get_small_padding(), 0);
  lv_obj_set_style_pad_gap(box, max_i(8, theme_get_small_padding()), 0);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *label = theme_create_label(box, title, true);
  lv_obj_set_width(label, LV_PCT(100));
  lv_obj_set_height(label, LV_SIZE_CONTENT);
  lv_obj_set_flex_grow(label, 0);
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_color(label, highlight_color(), 0);
  lv_obj_set_style_text_font(label, theme_font_small(), 0);
  lv_obj_set_style_text_line_space(label, 2, 0);

  lv_obj_t *text = theme_create_label(box, value ? value : "-", false);
  lv_obj_set_width(text, LV_PCT(100));
  lv_obj_set_height(text, LV_SIZE_CONTENT);
  lv_obj_set_flex_grow(text, 0);
  lv_label_set_long_mode(text, wrap ? LV_LABEL_LONG_WRAP : LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_font(text, theme_font_small(), 0);
  lv_obj_set_style_text_line_space(text, 5, 0);
}

static int web3_estimate_visual_lines(const char *text, int columns) {
  if (!text || !text[0])
    return 1;
  if (columns < 12)
    columns = 12;

  int lines = 1;
  int col = 0;
  const unsigned char *p = (const unsigned char *)text;
  while (*p) {
    if (*p == '\n') {
      lines++;
      col = 0;
      p++;
      continue;
    }

    int width = 1;
    if (*p >= 0x80) {
      width = 2;
      do {
        p++;
      } while ((*p & 0xC0) == 0x80);
    } else {
      p++;
    }

    col += width;
    if (col >= columns) {
      lines++;
      col = 0;
    }
  }
  return lines;
}

static int web3_detail_field_height(const char *title, const char *value,
                                    bool wrap) {
  const int pad = theme_get_small_padding();
  const int screen_w = theme_get_screen_width();
  const int content_w =
      max_i(160, screen_w - theme_get_default_padding() * 2 - pad * 6 - 24);
  const int columns = max_i(18, content_w / 10);
  const int line_h = theme_font_small() ? theme_font_small()->line_height + 5
                                       : 25;
  const int gap = max_i(8, pad);
  const int title_lines = web3_estimate_visual_lines(title, columns);
  const int value_lines =
      wrap ? web3_estimate_visual_lines(value ? value : "-", columns) : 1;
  const int min_h = wrap ? max_i(92, theme_get_min_touch_size() + pad * 3)
                         : max_i(68, theme_get_min_touch_size() + pad);
  const int wanted =
      pad * 2 + title_lines * line_h + gap + value_lines * line_h + 10;
  return max_i(min_h, wanted);
}

static bool web3_rlp_read_item(const uint8_t *cursor, const uint8_t *end,
                               web3_rlp_item_t *item) {
  if (!cursor || !end || cursor >= end || !item)
    return false;

  uint8_t prefix = *cursor++;
  size_t len = 0;
  item->payload = NULL;
  item->payload_len = 0;
  item->next = NULL;
  item->is_list = false;

  if (prefix <= 0x7f) {
    item->payload = cursor - 1;
    item->payload_len = 1;
    item->next = cursor;
    return true;
  }
  if (prefix <= 0xb7) {
    len = (size_t)(prefix - 0x80);
    if ((size_t)(end - cursor) < len)
      return false;
    item->payload = cursor;
    item->payload_len = len;
    item->next = cursor + len;
    return true;
  }
  if (prefix <= 0xbf) {
    size_t len_of_len = (size_t)(prefix - 0xb7);
    if (len_of_len == 0 || len_of_len > sizeof(size_t) ||
        (size_t)(end - cursor) < len_of_len)
      return false;
    for (size_t i = 0; i < len_of_len; i++)
      len = (len << 8) | cursor[i];
    cursor += len_of_len;
    if ((size_t)(end - cursor) < len)
      return false;
    item->payload = cursor;
    item->payload_len = len;
    item->next = cursor + len;
    return true;
  }
  if (prefix <= 0xf7) {
    len = (size_t)(prefix - 0xc0);
    if ((size_t)(end - cursor) < len)
      return false;
    item->payload = cursor;
    item->payload_len = len;
    item->next = cursor + len;
    item->is_list = true;
    return true;
  }

  size_t len_of_len = (size_t)(prefix - 0xf7);
  if (len_of_len == 0 || len_of_len > sizeof(size_t) ||
      (size_t)(end - cursor) < len_of_len)
    return false;
  for (size_t i = 0; i < len_of_len; i++)
    len = (len << 8) | cursor[i];
  cursor += len_of_len;
  if ((size_t)(end - cursor) < len)
    return false;
  item->payload = cursor;
  item->payload_len = len;
  item->next = cursor + len;
  item->is_list = true;
  return true;
}

static size_t web3_rlp_collect_list(const web3_rlp_item_t *list,
                                    web3_rlp_item_t *items,
                                    size_t max_items) {
  if (!list || !list->is_list || !items || max_items == 0)
    return 0;
  const uint8_t *cursor = list->payload;
  const uint8_t *end = list->payload + list->payload_len;
  size_t count = 0;
  while (cursor < end && count < max_items) {
    if (!web3_rlp_read_item(cursor, end, &items[count]))
      return 0;
    cursor = items[count].next;
    count++;
  }
  return cursor == end ? count : 0;
}

static void web3_quantity_to_decimal(const uint8_t *bytes, size_t len,
                                     char *out, size_t out_len) {
  if (!out || out_len == 0)
    return;
  out[0] = '\0';
  if (!bytes || len == 0) {
    snprintf(out, out_len, "0");
    return;
  }

  char digits[96] = "0";
  size_t digit_len = 1;
  for (size_t i = 0; i < len; i++) {
    unsigned carry = bytes[i];
    for (size_t pos = digit_len; pos > 0; pos--) {
      unsigned v = (unsigned)(digits[pos - 1] - '0') * 256U + carry;
      digits[pos - 1] = (char)('0' + (v % 10U));
      carry = v / 10U;
    }
    while (carry > 0 && digit_len + 1 < sizeof(digits)) {
      memmove(digits + 1, digits, digit_len);
      digits[0] = (char)('0' + (carry % 10U));
      digit_len++;
      carry /= 10U;
    }
    if (carry > 0) {
      snprintf(out, out_len, "%s",
               scan_tr("scan.value_too_large", "Value too large"));
      return;
    }
  }
  digits[digit_len] = '\0';
  snprintf(out, out_len, "%s", digits);
}

static void web3_rlp_quantity_text(const web3_rlp_item_t *item, char *out,
                                   size_t out_len) {
  if (!item || item->is_list) {
    snprintf(out, out_len, "-");
    return;
  }
  web3_quantity_to_decimal(item->payload, item->payload_len, out, out_len);
}

static bool web3_rlp_quantity_is_zero(const web3_rlp_item_t *item) {
  if (!item || item->is_list || item->payload_len == 0)
    return true;
  for (size_t i = 0; i < item->payload_len; i++) {
    if (item->payload[i] != 0)
      return false;
  }
  return true;
}

static void web3_rlp_address_text(const web3_rlp_item_t *item, char *out,
                                  size_t out_len, bool *has_to) {
  if (has_to)
    *has_to = false;
  if (!out || out_len == 0)
    return;
  out[0] = '\0';
  if (!item || item->is_list) {
    snprintf(out, out_len, "-");
    return;
  }
  if (item->payload_len == 0) {
    snprintf(out, out_len, "%s",
             scan_tr("scan.contract_creation", "Contract creation"));
    return;
  }
  if (item->payload_len != 20) {
    snprintf(out, out_len,
             scan_tr("scan.address_length_invalid",
                     "Invalid address length (%u)"),
             (unsigned)item->payload_len);
    return;
  }
  size_t pos = 0;
  pos += snprintf(out + pos, out_len - pos, "0x");
  for (size_t i = 0; i < item->payload_len && pos + 2 < out_len; i++)
    pos += snprintf(out + pos, out_len - pos, "%02x", item->payload[i]);
  if (has_to)
    *has_to = true;
}

static void web3_bytes_address_text(const uint8_t *bytes, char *out,
                                    size_t out_len) {
  if (!out || out_len == 0)
    return;
  out[0] = '\0';
  if (!bytes) {
    snprintf(out, out_len, "-");
    return;
  }
  size_t pos = 0;
  pos += snprintf(out + pos, out_len - pos, "0x");
  for (size_t i = 0; i < 20 && pos + 2 < out_len; i++)
    pos += snprintf(out + pos, out_len - pos, "%02x", bytes[i]);
}

static bool web3_decimal_is_digits(const char *text) {
  if (!text || !text[0])
    return false;
  for (const char *p = text; *p; p++) {
    if (!isdigit((unsigned char)*p))
      return false;
  }
  return true;
}

static bool web3_decimal_is_zero_text(const char *text) {
  if (!web3_decimal_is_digits(text))
    return false;
  for (const char *p = text; *p; p++) {
    if (*p != '0')
      return false;
  }
  return true;
}

static bool web3_decimal_to_u64(const char *text, uint64_t *out) {
  if (!text || !out)
    return false;
  uint64_t value = 0;
  bool seen = false;
  while (*text && isspace((unsigned char)*text))
    text++;
  for (const char *p = text; *p; p++) {
    if (!isdigit((unsigned char)*p))
      break;
    uint64_t digit = (uint64_t)(*p - '0');
    if (value > (UINT64_MAX - digit) / 10ULL)
      return false;
    value = value * 10ULL + digit;
    seen = true;
  }
  if (!seen)
    return false;
  *out = value;
  return true;
}

static const char *web3_chain_name_from_id(uint64_t chain_id) {
  switch (chain_id) {
  case 1:
    return "Ethereum";
  case 10:
    return "Optimism";
  case 25:
    return "Cronos";
  case 56:
    return "BSC";
  case 100:
    return "Gnosis";
  case 137:
    return "Polygon";
  case 250:
    return "Fantom";
  case 324:
    return "zkSync Era";
  case 1101:
    return "Polygon zkEVM";
  case 5000:
    return "Mantle";
  case 8453:
    return "Base";
  case 42161:
    return "Arbitrum";
  case 43114:
    return "Avalanche";
  case 59144:
    return "Linea";
  case 534352:
    return "Scroll";
  default:
    return NULL;
  }
}

static void web3_chain_display_from_id(uint64_t chain_id, char *out,
                                       size_t out_len) {
  if (!out || out_len == 0)
    return;
  const char *name = web3_chain_name_from_id(chain_id);
  if (name)
    snprintf(out, out_len, "%s", name);
  else
    snprintf(out, out_len, "EVM %llu", (unsigned long long)chain_id);
}

static void web3_request_chain_display(const web3_sign_request_t *req,
                                       char *out, size_t out_len) {
  if (!out || out_len == 0)
    return;
  if (req && req->chain[0] && !starts_ci(req->chain, "EVM")) {
    snprintf(out, out_len, "%s", req->chain);
    return;
  }
  if (req && req->chain_id > 0) {
    web3_chain_display_from_id((uint64_t)req->chain_id, out, out_len);
    return;
  }
  snprintf(out, out_len, "%s", (req && req->chain[0]) ? req->chain : "EVM");
}

static void web3_format_decimal_unit(const char *digits, unsigned decimals,
                                     const char *unit, char *out,
                                     size_t out_len) {
  if (!out || out_len == 0)
    return;
  out[0] = '\0';
  if (!web3_decimal_is_digits(digits)) {
    snprintf(out, out_len, "%s", digits ? digits : "-");
    return;
  }

  while (digits[0] == '0' && digits[1])
    digits++;
  if (web3_decimal_is_zero_text(digits)) {
    snprintf(out, out_len, "0 %s", unit ? unit : "");
    return;
  }

  char body[144];
  size_t pos = 0;
  size_t len = strlen(digits);
  if (decimals == 0 || len > decimals) {
    size_t whole_len = decimals == 0 ? len : len - decimals;
    for (size_t i = 0; i < whole_len && pos + 1 < sizeof(body); i++)
      body[pos++] = digits[i];
    if (decimals > 0) {
      size_t frac_start = whole_len;
      size_t frac_len = len - whole_len;
      while (frac_len > 0 && digits[frac_start + frac_len - 1] == '0')
        frac_len--;
      if (frac_len > 0 && pos + 1 < sizeof(body)) {
        body[pos++] = '.';
        for (size_t i = 0; i < frac_len && pos + 1 < sizeof(body); i++)
          body[pos++] = digits[frac_start + i];
      }
    }
  } else {
    body[pos++] = '0';
    if (pos + 1 < sizeof(body))
      body[pos++] = '.';
    for (size_t i = 0; i < decimals - len && pos + 1 < sizeof(body); i++)
      body[pos++] = '0';
    for (size_t i = 0; i < len && pos + 1 < sizeof(body); i++)
      body[pos++] = digits[i];
    while (pos > 0 && body[pos - 1] == '0')
      pos--;
    if (pos > 0 && body[pos - 1] == '.')
      pos--;
  }
  body[pos] = '\0';
  snprintf(out, out_len, "%s %s", body, unit ? unit : "");
}

static void web3_format_wei_amount(const char *wei, char *out,
                                   size_t out_len) {
  if (!out || out_len == 0)
    return;
  char eth[160];
  web3_format_decimal_unit(wei, 18, "ETH", eth, sizeof(eth));
  snprintf(out, out_len, "%s", eth);
}

static void web3_decode_known_calldata(const web3_rlp_item_t *data_item,
                                       web3_tx_summary_t *summary) {
  if (!data_item || data_item->is_list || !summary)
    return;
  const uint8_t *p = data_item->payload;
  size_t len = data_item->payload_len;
  summary->payload_len = len;
  if (len == 0) {
    snprintf(summary->data_note, sizeof(summary->data_note), "%s",
             scan_tr("common.none", "None"));
    return;
  }

  if (len >= 4) {
    snprintf(summary->data_note, sizeof(summary->data_note),
             scan_tr("scan.bytes_method_format",
                     "%u bytes, method 0x%02x%02x%02x%02x"),
             (unsigned)len, p[0], p[1], p[2], p[3]);
  } else {
    snprintf(summary->data_note, sizeof(summary->data_note),
             scan_tr("scan.bytes_format", "%u bytes"), (unsigned)len);
    return;
  }

  if (len >= 68 && p[0] == 0xa9 && p[1] == 0x05 && p[2] == 0x9c &&
      p[3] == 0xbb) {
    snprintf(summary->method, sizeof(summary->method), "ERC20 transfer");
    web3_bytes_address_text(p + 4 + 12, summary->token_target,
                            sizeof(summary->token_target));
    web3_quantity_to_decimal(p + 4 + 32, 32, summary->token_amount,
                             sizeof(summary->token_amount));
    return;
  }

  if (len >= 68 && p[0] == 0x09 && p[1] == 0x5e && p[2] == 0xa7 &&
      p[3] == 0xb3) {
    snprintf(summary->method, sizeof(summary->method), "ERC20 approve");
    web3_bytes_address_text(p + 4 + 12, summary->token_target,
                            sizeof(summary->token_target));
    web3_quantity_to_decimal(p + 4 + 32, 32, summary->token_amount,
                             sizeof(summary->token_amount));
    return;
  }

  if (len >= 100 && p[0] == 0x23 && p[1] == 0xb8 && p[2] == 0x72 &&
      p[3] == 0xdd) {
    snprintf(summary->method, sizeof(summary->method), "ERC20 transferFrom");
    web3_bytes_address_text(p + 4 + 12, summary->token_from,
                            sizeof(summary->token_from));
    web3_bytes_address_text(p + 4 + 32 + 12, summary->token_target,
                            sizeof(summary->token_target));
    web3_quantity_to_decimal(p + 4 + 64, 32, summary->token_amount,
                             sizeof(summary->token_amount));
    return;
  }

  snprintf(summary->method, sizeof(summary->method), "%s",
           scan_tr("scan.contract_call", "Contract call"));
}

static void web3_parse_tx_summary(const web3_sign_request_t *req,
                                  web3_tx_summary_t *summary) {
  if (!summary)
    return;
  memset(summary, 0, sizeof(*summary));
  if (!req || !req->sign_data || req->sign_data_len == 0) {
    snprintf(summary->error, sizeof(summary->error), "%s",
             scan_tr("scan.tx_data_empty", "Transaction data is empty"));
    return;
  }

  const uint8_t *data = req->sign_data;
  size_t len = req->sign_data_len;
  summary->tx_type =
      req->request_type_id == WEB3_REQUEST_TYPE_TYPED_TX ? data[0] : 0;
  if (req->request_type_id == WEB3_REQUEST_TYPE_TYPED_TX) {
    if (len < 2 || (summary->tx_type != 1 && summary->tx_type != 2)) {
      snprintf(summary->error, sizeof(summary->error), "%s",
               scan_tr("scan.unsupported_tx_type",
                       "Unsupported transaction type"));
      return;
    }
    data++;
    len--;
  }

  web3_rlp_item_t root;
  if (!web3_rlp_read_item(data, data + len, &root) || root.next != data + len ||
      !root.is_list) {
    snprintf(summary->error, sizeof(summary->error), "%s",
             scan_tr("scan.tx_rlp_parse_failed",
                     "Transaction RLP parse failed"));
    return;
  }

  web3_rlp_item_t fields[12];
  size_t count = web3_rlp_collect_list(&root, fields, 12);
  if (summary->tx_type == 0) {
    if (count < 6) {
      snprintf(summary->error, sizeof(summary->error), "%s",
               scan_tr("scan.legacy_fields_missing",
                       "Not enough legacy fields"));
      return;
    }
    web3_rlp_quantity_text(&fields[0], summary->nonce,
                           sizeof(summary->nonce));
    web3_rlp_quantity_text(&fields[1], summary->gas_price,
                           sizeof(summary->gas_price));
    web3_rlp_quantity_text(&fields[2], summary->gas_limit,
                           sizeof(summary->gas_limit));
    web3_rlp_address_text(&fields[3], summary->to, sizeof(summary->to),
                          &summary->has_to);
    web3_rlp_quantity_text(&fields[4], summary->value,
                           sizeof(summary->value));
    summary->value_is_zero = web3_rlp_quantity_is_zero(&fields[4]);
    snprintf(summary->data_len, sizeof(summary->data_len),
             scan_tr("scan.bytes_format", "%u bytes"),
             (unsigned)fields[5].payload_len);
    web3_decode_known_calldata(&fields[5], summary);
    if (count > 6 && !fields[6].is_list && fields[6].payload_len > 0) {
      web3_rlp_quantity_text(&fields[6], summary->chain_id,
                             sizeof(summary->chain_id));
    } else {
      snprintf(summary->chain_id, sizeof(summary->chain_id), "%d",
               req->chain_id);
    }
    summary->valid = true;
    return;
  }

  if (summary->tx_type == 1) {
    if (count < 8) {
      snprintf(summary->error, sizeof(summary->error), "%s",
               scan_tr("scan.eip2930_fields_missing",
                       "Not enough EIP-2930 fields"));
      return;
    }
    web3_rlp_quantity_text(&fields[0], summary->chain_id,
                           sizeof(summary->chain_id));
    web3_rlp_quantity_text(&fields[1], summary->nonce,
                           sizeof(summary->nonce));
    web3_rlp_quantity_text(&fields[2], summary->gas_price,
                           sizeof(summary->gas_price));
    web3_rlp_quantity_text(&fields[3], summary->gas_limit,
                           sizeof(summary->gas_limit));
    web3_rlp_address_text(&fields[4], summary->to, sizeof(summary->to),
                          &summary->has_to);
    web3_rlp_quantity_text(&fields[5], summary->value,
                           sizeof(summary->value));
    summary->value_is_zero = web3_rlp_quantity_is_zero(&fields[5]);
    snprintf(summary->data_len, sizeof(summary->data_len),
             scan_tr("scan.bytes_format", "%u bytes"),
             (unsigned)fields[6].payload_len);
    web3_decode_known_calldata(&fields[6], summary);
    snprintf(summary->access_list_count, sizeof(summary->access_list_count),
             scan_tr("scan.items_format", "%u items"),
             fields[7].is_list
                 ? (unsigned)web3_rlp_collect_list(&fields[7], fields, 12)
                 : 0U);
    summary->valid = true;
    return;
  }

  if (summary->tx_type == 2) {
    if (count < 9) {
      snprintf(summary->error, sizeof(summary->error), "%s",
               scan_tr("scan.eip1559_fields_missing",
                       "Not enough EIP-1559 fields"));
      return;
    }
    web3_rlp_quantity_text(&fields[0], summary->chain_id,
                           sizeof(summary->chain_id));
    web3_rlp_quantity_text(&fields[1], summary->nonce,
                           sizeof(summary->nonce));
    web3_rlp_quantity_text(&fields[2], summary->max_priority_fee,
                           sizeof(summary->max_priority_fee));
    web3_rlp_quantity_text(&fields[3], summary->max_fee,
                           sizeof(summary->max_fee));
    web3_rlp_quantity_text(&fields[4], summary->gas_limit,
                           sizeof(summary->gas_limit));
    web3_rlp_address_text(&fields[5], summary->to, sizeof(summary->to),
                          &summary->has_to);
    web3_rlp_quantity_text(&fields[6], summary->value,
                           sizeof(summary->value));
    summary->value_is_zero = web3_rlp_quantity_is_zero(&fields[6]);
    snprintf(summary->data_len, sizeof(summary->data_len),
             scan_tr("scan.bytes_format", "%u bytes"),
             (unsigned)fields[7].payload_len);
    web3_decode_known_calldata(&fields[7], summary);
    snprintf(summary->access_list_count, sizeof(summary->access_list_count),
             scan_tr("scan.items_format", "%u items"),
             fields[8].is_list
                 ? (unsigned)web3_rlp_collect_list(&fields[8], fields, 12)
                 : 0U);
    summary->valid = true;
    return;
  }

  snprintf(summary->error, sizeof(summary->error), "%s",
           scan_tr("scan.unsupported_tx_type", "Unsupported transaction type"));
}

static void web3_add_transaction_details(lv_obj_t *parent,
                                         const web3_sign_request_t *req) {
  if (!parent || !req ||
      (req->request_type_id != WEB3_REQUEST_TYPE_LEGACY_TX &&
       req->request_type_id != WEB3_REQUEST_TYPE_TYPED_TX)) {
    return;
  }

  web3_tx_summary_t tx;
  web3_parse_tx_summary(req, &tx);
  if (!tx.valid) {
    web3_add_detail_field(parent,
                          scan_tr("scan.transaction_parse",
                                  "Transaction parse"),
                          tx.error[0] ? tx.error
                                      : scan_tr("dialog.failed", "Failed"),
                          true);
    return;
  }

  char action[96];
  if (!tx.has_to) {
    snprintf(action, sizeof(action), "%s",
             scan_tr("scan.contract_creation", "Contract creation"));
  } else if (tx.payload_len > 0 && tx.method[0]) {
    snprintf(action, sizeof(action), "%s", tx.method);
  } else if (tx.payload_len > 0) {
    snprintf(action, sizeof(action), "%s",
             scan_tr("scan.contract_call", "Contract call"));
  } else {
    snprintf(action, sizeof(action), "%s",
             scan_tr("scan.transfer", "Transfer"));
  }

  char chain_display[96];
  uint64_t chain_id = 0;
  if (web3_decimal_to_u64(tx.chain_id, &chain_id) && chain_id > 0)
    web3_chain_display_from_id(chain_id, chain_display, sizeof(chain_display));
  else
    snprintf(chain_display, sizeof(chain_display), "%s", tx.chain_id);

  char amount[192];
  web3_format_wei_amount(tx.value, amount, sizeof(amount));

  web3_add_detail_field(parent, scan_tr("scan.action", "Action"), action,
                        true);
  web3_add_detail_field(parent, scan_tr("scan.chain", "Chain"),
                        chain_display, false);
  web3_add_detail_field(parent,
                        tx.has_to
                            ? scan_tr("scan.recipient_contract",
                                      "Recipient / contract")
                            : scan_tr("scan.target", "Target"),
                        tx.to, true);
  if (tx.token_from[0])
    web3_add_detail_field(parent, scan_tr("scan.from", "From"),
                          tx.token_from, true);
  if (tx.token_target[0])
    web3_add_detail_field(parent,
                          contains_ci(tx.method, "approve")
                              ? scan_tr("scan.approval_spender", "Spender")
                              : scan_tr("scan.token", "Token"),
                          tx.token_target, true);
  if (tx.token_amount[0])
    web3_add_detail_field(parent, scan_tr("sign.amount", "Amount"),
                          tx.token_amount, true);
  if (!tx.value_is_zero)
    web3_add_detail_field(parent, scan_tr("scan.value", "Value"), amount,
                          true);
  if (tx.payload_len > 0) {
    const char *notice =
        contains_ci(tx.method, "approve")
            ? scan_tr("scan.token_approval_notice",
                      "This is a token approval. Check the spender and amount.")
            : scan_tr("scan.contract_interaction_notice",
                      "This is a contract interaction. Check the DApp, "
                      "contract address, and amount.");
    web3_add_detail_field(parent, scan_tr("common.warning", "Warning"),
                          notice, true);
  }
}

static bool web3_cjson_value_text(const cJSON *item, char *out,
                                  size_t out_len) {
  if (!item || !out || out_len == 0)
    return false;
  out[0] = '\0';

  if (cJSON_IsString(item) && item->valuestring) {
    snprintf(out, out_len, "%s", item->valuestring);
    return true;
  }
  if (cJSON_IsBool(item)) {
    snprintf(out, out_len, "%s", cJSON_IsTrue(item) ? "true" : "false");
    return true;
  }

  char *printed = cJSON_PrintUnformatted((cJSON *)item);
  if (!printed)
    return false;
  if (strlen(printed) >= out_len) {
    short_middle(out, out_len, printed, 96, 48);
  } else {
    snprintf(out, out_len, "%s", printed);
  }
  free(printed);
  return true;
}

static void web3_add_cjson_field(lv_obj_t *parent, const char *title,
                                 const cJSON *item) {
  char value[256];
  if (web3_cjson_value_text(item, value, sizeof(value)) && value[0])
    web3_add_detail_field(parent, title, value, true);
}

static void web3_add_typed_data_details(lv_obj_t *parent,
                                        const web3_sign_request_t *req) {
  if (!parent || !req ||
      req->request_type_id != WEB3_REQUEST_TYPE_TYPED_DATA ||
      !req->sign_data || req->sign_data_len == 0) {
    return;
  }

  char *json = malloc(req->sign_data_len + 1U);
  if (!json)
    return;
  memcpy(json, req->sign_data, req->sign_data_len);
  json[req->sign_data_len] = '\0';

  cJSON *root = cJSON_Parse(json);
  secure_memzero(json, req->sign_data_len);
  free(json);
  if (!cJSON_IsObject(root)) {
    cJSON_Delete(root);
    web3_add_detail_field(parent, scan_tr("address.type", "Type"),
                          "TypedData", false);
    return;
  }

  web3_add_detail_field(parent, scan_tr("address.type", "Type"),
                        scan_tr("scan.dapp_typed_signature",
                                "DApp typed signature"),
                        false);
  const cJSON *primary =
      cJSON_GetObjectItemCaseSensitive(root, "primaryType");
  web3_add_cjson_field(parent, scan_tr("scan.primary_type", "Primary type"),
                       primary);

  const cJSON *domain = cJSON_GetObjectItemCaseSensitive(root, "domain");
  bool chain_shown = false;
  if (cJSON_IsObject(domain)) {
    char dapp_text[128];
    if (web3_cjson_value_text(
            cJSON_GetObjectItemCaseSensitive((cJSON *)domain, "name"),
            dapp_text, sizeof(dapp_text)) &&
        dapp_text[0]) {
      web3_add_detail_field(parent, "DApp", dapp_text, true);
    } else if (req->origin[0]) {
      web3_add_detail_field(parent, "DApp", req->origin, true);
    }
    web3_add_cjson_field(parent, scan_tr("scan.version", "Version"),
                         cJSON_GetObjectItemCaseSensitive((cJSON *)domain,
                                                          "version"));
    char chain_text[64];
    if (web3_cjson_value_text(
            cJSON_GetObjectItemCaseSensitive((cJSON *)domain, "chainId"),
            chain_text, sizeof(chain_text)) &&
        chain_text[0]) {
      char chain_display[96];
      uint64_t chain_id = 0;
      if (web3_decimal_to_u64(chain_text, &chain_id) && chain_id > 0) {
        web3_chain_display_from_id(chain_id, chain_display,
                                   sizeof(chain_display));
      } else {
        snprintf(chain_display, sizeof(chain_display), "%s", chain_text);
      }
      web3_add_detail_field(parent, scan_tr("scan.chain", "Chain"),
                            chain_display, false);
      chain_shown = true;
    }
    web3_add_cjson_field(parent, scan_tr("scan.contract", "Contract"),
                         cJSON_GetObjectItemCaseSensitive((cJSON *)domain,
                                                          "verifyingContract"));
  } else if (req->origin[0]) {
    web3_add_detail_field(parent, "DApp", req->origin, true);
  }
  if (!chain_shown) {
    char chain_display[96];
    web3_request_chain_display(req, chain_display, sizeof(chain_display));
    web3_add_detail_field(parent, scan_tr("scan.chain", "Chain"),
                          chain_display, false);
  }

  const cJSON *message = cJSON_GetObjectItemCaseSensitive(root, "message");
  if (cJSON_IsObject(message)) {
    static const struct {
      const char *key;
      const char *title_key;
      const char *fallback;
    } fields[] = {
        {"from", "scan.from", "From"},
        {"to", "scan.to", "To"},
        {"owner", "scan.owner", "Owner"},
        {"spender", "scan.approval_spender", "Spender"},
        {"value", "scan.value", "Value"},
        {"amount", "sign.amount", "Amount"},
        {"tokenId", "scan.token_id", "Token ID"},
        {"nonce", "sign.nonce", "Nonce"},
        {"deadline", "scan.deadline", "Deadline"},
        {"contents", "scan.contents", "Contents"},
        {"statement", "scan.statement", "Statement"},
        {"uri", "scan.uri", "URI"},
    };
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
      web3_add_cjson_field(
          parent, scan_tr(fields[i].title_key, fields[i].fallback),
          cJSON_GetObjectItemCaseSensitive((cJSON *)message, fields[i].key));
    }
  }

  cJSON_Delete(root);
}

static void web3_add_full_sign_data_details(lv_obj_t *parent,
                                            const web3_sign_request_t *req) {
  if (!parent || !req || !req->sign_data || req->sign_data_len == 0)
    return;

  if (req->request_type_id == WEB3_REQUEST_TYPE_LEGACY_TX ||
      req->request_type_id == WEB3_REQUEST_TYPE_TYPED_TX) {
    web3_tx_summary_t tx;
    web3_parse_tx_summary(req, &tx);
    if (tx.valid && tx.payload_len == 0)
      return;
    if (!tx.valid) {
      web3_add_detail_field(parent,
                            scan_tr("scan.raw_transaction",
                                    "Raw transaction"),
                            scan_tr("scan.tx_fields_parse_failed",
                                    "Transaction fields could not be parsed. "
                                    "Check the raw hex content."),
                            true);
      char *full = web3_format_full_data_alloc(req->sign_data,
                                               req->sign_data_len, false);
      if (full) {
        web3_add_detail_field(parent,
                              scan_tr("scan.full_raw_transaction",
                                      "Full raw transaction"),
                              full, true);
        free(full);
      }
      return;
    }

    char *full = web3_format_full_data_alloc(req->sign_data,
                                             req->sign_data_len, false);
    if (!full)
      return;
    web3_add_detail_field(parent,
                          scan_tr("scan.full_raw_transaction_contract",
                                  "Full raw transaction (contract review)"),
                          full, true);
    free(full);
    return;
  }
  if (req->request_type_id == WEB3_REQUEST_TYPE_TYPED_DATA) {
    char *full =
        web3_format_full_data_alloc(req->sign_data, req->sign_data_len, true);
    if (!full)
      return;
    web3_add_detail_field(parent,
                          scan_tr("scan.full_dapp_signing_content",
                                  "Full DApp signing content"),
                          full, true);
    free(full);
    return;
  }

  const bool is_text = req->request_type_id == WEB3_REQUEST_TYPE_PERSONAL ||
                       req->request_type_id == WEB3_REQUEST_TYPE_TYPED_DATA;
  const char *title = scan_tr("scan.full_data", "Full data");
  if (req->request_type_id == WEB3_REQUEST_TYPE_PERSONAL)
    title = scan_tr("scan.message_text", "Message text");
  else if (req->request_type_id == WEB3_REQUEST_TYPE_TYPED_DATA)
    title = scan_tr("scan.full_typed_data", "Full TypedData");
  else if (req->request_type_id == WEB3_REQUEST_TYPE_LEGACY_TX ||
           req->request_type_id == WEB3_REQUEST_TYPE_TYPED_TX)
    title = scan_tr("scan.raw_transaction_advanced",
                    "Raw transaction (advanced)");

  char *full = web3_format_full_data_alloc(req->sign_data, req->sign_data_len,
                                           is_text);
  if (!full)
    return;
  web3_add_detail_field(parent, title, full, true);
  free(full);
}

static void web3_resume_current_step(void) {
  if (pending_web3_source == WEB3_SIGN_SOURCE_SMARTCARD) {
    web3_pin_sign_after_summary_cb(NULL);
  } else if (pending_web3_source == WEB3_SIGN_SOURCE_MNEMONIC) {
    web3_show_request_summary_for_source(WEB3_SIGN_SOURCE_MNEMONIC, true);
  } else {
    web3_show_source_choice();
  }
}

static bool web3_compose_signature_bytes(const web3_sign_request_t *req,
                                         const uint8_t compact[64],
                                         uint8_t recovery_id,
                                         uint8_t signature[72],
                                         size_t *signature_len) {
  if (!req || !compact || !signature || !signature_len)
    return false;

  memcpy(signature, compact, 64);
  if (req->request_type_id == WEB3_REQUEST_TYPE_LEGACY_TX &&
      (contains_ci(req->wallet, "OKX") ||
       contains_ci(req->wallet, "Bitget") ||
       contains_ci(req->origin, "okx") ||
       contains_ci(req->origin, "bitget") ||
       contains_ci(req->origin, "bitkeep")) &&
      req->chain_id > 0) {
    uint64_t v = (uint64_t)req->chain_id * 2ULL + 35ULL +
                 (uint64_t)(recovery_id & 1U);
    uint8_t v_bytes[8];
    size_t v_len = 0;
    bool seen = false;
    for (int i = 7; i >= 0; i--) {
      uint8_t b = (uint8_t)(v >> (unsigned)(i * 8));
      if (b || seen || i == 0) {
        seen = true;
        v_bytes[v_len++] = b;
      }
    }
    memcpy(signature + 64, v_bytes, v_len);
    *signature_len = 64 + v_len;
  } else {
    signature[64] = recovery_id & 1U;
    *signature_len = 65;
  }

  return true;
}

// Format satoshis as Bitcoin with visual grouping: "1.00 000 000"
static void format_btc(char *buf, size_t buf_size, uint64_t sats) {
  uint64_t whole = sats / 100000000ULL;
  uint64_t frac = sats % 100000000ULL;
  // Split fraction: first 2 digits, then two groups of 3
  uint32_t frac_first = (uint32_t)(frac / 1000000ULL);
  uint32_t frac_second = (uint32_t)((frac / 1000ULL) % 1000ULL);
  uint32_t frac_third = (uint32_t)(frac % 1000ULL);
  snprintf(buf, buf_size, "%llu.%02u %03u %03u", whole, frac_first, frac_second,
           frac_third);
}

// Create address label with first and last 6 chars highlighted in given color
static lv_obj_t *create_address_label(lv_obj_t *parent, const char *address,
                                      lv_color_t highlight) {
  size_t len = strlen(address);
  char *formatted = malloc(len + 32);
  if (!formatted) {
    return theme_create_label(parent, address, false);
  }

  lv_color32_t c32 = lv_color_to_32(highlight, LV_OPA_COVER);
  uint32_t color_hex = (c32.red << 16) | (c32.green << 8) | c32.blue;

  if (len > 12) {
    char first[7], last[7];
    strncpy(first, address, 6);
    first[6] = '\0';
    strncpy(last, address + len - 6, 6);
    last[6] = '\0';

    snprintf(formatted, len + 32, "#%06X %s#%.*s#%06X %s#", (unsigned)color_hex,
             first, (int)(len - 12), address + 6, (unsigned)color_hex, last);
  } else {
    snprintf(formatted, len + 32, "#%06X %s#", (unsigned)color_hex, address);
  }

  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_recolor(label, true);
  lv_label_set_text(label, formatted);
  lv_obj_set_style_text_font(label, theme_font_small(), 0);
  lv_obj_set_style_text_color(label, lv_color_hex(0xAAAAAA), 0);
  free(formatted);

  return label;
}

// Create a row with: [prefix text] [BTC] [formatted value]
static lv_obj_t *create_btc_value_row(lv_obj_t *parent, const char *prefix,
                                      uint64_t sats, lv_color_t color) {
  lv_obj_t *row = theme_create_flex_row(parent);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_column(row, 4, 0);

  lv_obj_t *prefix_label = lv_label_create(row);
  lv_label_set_text(prefix_label, prefix);
  lv_obj_set_style_text_font(prefix_label, theme_font_small(), 0);
  lv_obj_set_style_text_color(prefix_label, color, 0);

  lv_obj_t *icon_label = lv_label_create(row);
  // Avoid the Bitcoin glyph here: the bundled Chinese font does not contain it
  // on all builds, and LVGL renders missing glyphs as square boxes.
  lv_label_set_text(icon_label, "BTC");
  lv_obj_set_style_text_font(icon_label, theme_font_small(), 0);
  lv_obj_set_style_text_color(icon_label, color, 0);

  char btc_str[32];
  format_btc(btc_str, sizeof(btc_str), sats);
  lv_obj_t *value_label = lv_label_create(row);
  lv_label_set_text(value_label, btc_str);
  lv_obj_set_style_text_font(value_label, theme_font_small(), 0);
  lv_obj_set_style_text_color(value_label, color, 0);

  return row;
}

static uint32_t btc_input_sighash_byte(const struct wally_psbt *psbt,
                                       size_t index) {
  size_t sighash = 0;
  if (wally_psbt_get_input_sighash(psbt, index, &sighash) != WALLY_OK ||
      sighash == 0) {
    return WALLY_SIGHASH_ALL;
  }
  return (uint32_t)(sighash & 0xffU);
}

static btc_derivation_script_t
btc_satochip_input_script_type(const struct wally_psbt *psbt, size_t index) {
  unsigned char script[256];
  size_t script_len = 0;
  size_t script_type = 0;

  if (!psbt_input_utxo_script(psbt, index, script, sizeof(script),
                              &script_len) ||
      wally_scriptpubkey_get_type(script, script_len, &script_type) !=
          WALLY_OK) {
    return BTC_DERIVATION_SCRIPT_UNKNOWN;
  }

  if (script_type == WALLY_SCRIPT_TYPE_P2PKH)
    return BTC_DERIVATION_SCRIPT_P2PKH;
  if (script_type == WALLY_SCRIPT_TYPE_P2WPKH)
    return BTC_DERIVATION_SCRIPT_P2WPKH;
  if (script_type != WALLY_SCRIPT_TYPE_P2SH)
    return BTC_DERIVATION_SCRIPT_UNKNOWN;

  size_t redeem_len = 0;
  if (wally_psbt_get_input_redeem_script_len(psbt, index, &redeem_len) !=
          WALLY_OK ||
      redeem_len == 0 || redeem_len > sizeof(script)) {
    return BTC_DERIVATION_SCRIPT_UNKNOWN;
  }

  size_t written = 0;
  if (wally_psbt_get_input_redeem_script(psbt, index, script, redeem_len,
                                         &written) != WALLY_OK ||
      written != redeem_len ||
      wally_scriptpubkey_get_type(script, redeem_len, &script_type) !=
          WALLY_OK) {
    return BTC_DERIVATION_SCRIPT_UNKNOWN;
  }

  return script_type == WALLY_SCRIPT_TYPE_P2WPKH
             ? BTC_DERIVATION_SCRIPT_P2SH_P2WPKH
             : BTC_DERIVATION_SCRIPT_UNKNOWN;
}

static bool btc_input_signature_hash(struct wally_psbt *psbt,
                                     const struct wally_tx *tx, size_t index,
                                     uint8_t out[WALLY_TXHASH_LEN],
                                     char *detail, size_t detail_len) {
  size_t script_len = 0;
  if (wally_psbt_get_input_signing_script_len(psbt, index, &script_len) !=
          WALLY_OK ||
      script_len == 0) {
    snprintf(detail, detail_len,
             scan_tr("scan.input_missing_signing_script",
                     "Input %zu is missing the signing script."),
             index);
    return false;
  }

  unsigned char *script = malloc(script_len);
  if (!script) {
    snprintf(detail, detail_len,
             scan_tr("scan.input_out_of_memory",
                     "Input %zu ran out of memory."),
             index);
    return false;
  }

  size_t written = 0;
  bool ok = false;
  if (wally_psbt_get_input_signing_script(psbt, index, script, script_len,
                                          &written) != WALLY_OK ||
      written != script_len) {
    snprintf(detail, detail_len,
             scan_tr("scan.input_read_signing_script_failed",
                     "Input %zu signing script read failed."),
             index);
    goto done;
  }

  size_t scriptcode_len = 0;
  if (wally_psbt_get_input_scriptcode_len(psbt, index, script, script_len,
                                          &scriptcode_len) != WALLY_OK ||
      scriptcode_len == 0) {
    snprintf(detail, detail_len,
             scan_tr("scan.input_scriptcode_create_failed",
                     "Input %zu scriptCode creation failed."),
             index);
    goto done;
  }

  unsigned char *scriptcode = malloc(scriptcode_len);
  if (!scriptcode) {
    snprintf(detail, detail_len,
             scan_tr("scan.input_scriptcode_out_of_memory",
                     "Input %zu scriptCode ran out of memory."),
             index);
    goto done;
  }

  written = 0;
  if (wally_psbt_get_input_scriptcode(psbt, index, script, script_len,
                                      scriptcode, scriptcode_len,
                                      &written) != WALLY_OK ||
      written != scriptcode_len) {
    snprintf(detail, detail_len,
             scan_tr("scan.input_read_scriptcode_failed",
                     "Input %zu scriptCode read failed."),
             index);
    free(scriptcode);
    goto done;
  }

  if (wally_psbt_get_input_signature_hash(psbt, index, tx, scriptcode,
                                          scriptcode_len, 0, out,
                                          WALLY_TXHASH_LEN) != WALLY_OK) {
    snprintf(detail, detail_len,
             scan_tr("scan.input_signature_hash_failed",
                     "Input %zu signature hash calculation failed."),
             index);
    free(scriptcode);
    goto done;
  }

  ok = true;
  free(scriptcode);

done:
  free(script);
  return ok;
}

static size_t psbt_sign_with_satochip(struct wally_psbt *psbt, const char *pin,
                                      bool sign_testnet, char *detail,
                                      size_t detail_len) {
  if (detail && detail_len)
    detail[0] = '\0';
  if (!psbt || !pin || !pin[0]) {
    snprintf(detail, detail_len, "%s",
             scan_tr("scan.missing_tx_or_card_pin",
                     "Missing transaction or smartcard PIN."));
    return 0;
  }

  struct wally_tx *tx = NULL;
  if (wally_psbt_get_global_tx_alloc(psbt, &tx) != WALLY_OK || !tx) {
    snprintf(detail, detail_len, "%s",
             scan_tr("scan.bluewallet_psbt_v0_only",
                     "Only common BlueWallet PSBT v0 is currently supported."));
    return 0;
  }

  size_t signatures_added = 0;
  for (size_t i = 0; i < psbt->num_inputs; i++) {
    btc_derivation_script_t script_type =
        btc_satochip_input_script_type(psbt, i);
    const struct wally_map *keypaths = &psbt->inputs[i].keypaths;
    for (size_t j = 0; j < keypaths->num_items; j++) {
      const struct wally_map_item *item = &keypaths->items[j];
      if (!item->key || item->key_len != EC_PUBLIC_KEY_LEN || !item->value)
        continue;

      char path[96];
      if (!btc_derivation_satochip_sign_path(item->value, item->value_len,
                                             script_type, sign_testnet, 0, path,
                                             sizeof(path))) {
        snprintf(detail, detail_len,
                 scan_tr("scan.input_card_path_parse_failed",
                         "Input %zu smartcard signing path could not be parsed."),
                 i);
        continue;
      }

      uint8_t digest[WALLY_TXHASH_LEN];
      if (!btc_input_signature_hash(psbt, tx, i, digest, detail, detail_len))
        continue;

      smartcard_satochip_signature_t sig;
      memset(&sig, 0, sizeof(sig));
      esp_err_t err = smartcard_satochip_sign_evm_digest(pin, path, digest,
                                                         &sig, 25000);
      if (err != ESP_OK || !sig.has_signature) {
        snprintf(detail, detail_len,
                 scan_tr("scan.input_card_sign_failed",
                         "Input %zu smartcard signing failed: %s\n%s"),
                 i, esp_err_to_name(err), sig.detail);
        secure_memzero(&sig, sizeof(sig));
        secure_memzero(digest, sizeof(digest));
        continue;
      }

      if (!sig.has_compressed_pubkey ||
          memcmp(sig.compressed_pubkey, item->key, EC_PUBLIC_KEY_LEN) != 0) {
        snprintf(detail, detail_len,
                 scan_tr("scan.input_path_not_on_card",
                         "Input %zu path is not on the current smartcard: %s"),
                 i, path);
        secure_memzero(&sig, sizeof(sig));
        secure_memzero(digest, sizeof(digest));
        continue;
      }

      unsigned char der[EC_SIGNATURE_DER_MAX_LEN + 1];
      size_t der_len = 0;
      if (wally_ec_sig_to_der(sig.compact, sizeof(sig.compact), der,
                              sizeof(der), &der_len) != WALLY_OK ||
          der_len + 1 > sizeof(der)) {
        snprintf(detail, detail_len,
                 scan_tr("scan.input_signature_der_failed",
                         "Input %zu signature DER encoding failed."),
                 i);
        secure_memzero(&sig, sizeof(sig));
        secure_memzero(digest, sizeof(digest));
        continue;
      }

      der[der_len++] = (unsigned char)btc_input_sighash_byte(psbt, i);
      if (wally_psbt_add_input_signature(psbt, i, item->key, item->key_len,
                                         der, der_len) == WALLY_OK) {
        signatures_added++;
      } else {
        snprintf(detail, detail_len,
                 scan_tr("scan.input_write_psbt_signature_failed",
                         "Input %zu PSBT signature write failed."),
                 i);
      }

      secure_memzero(der, sizeof(der));
      secure_memzero(&sig, sizeof(sig));
      secure_memzero(digest, sizeof(digest));
      break;
    }
  }

  wally_tx_free(tx);
  if (signatures_added > 0) {
    snprintf(detail, detail_len,
             scan_tr("scan.card_sign_complete_format",
                     "Smartcard signing complete: %zu inputs."),
             signatures_added);
  } else if (detail && detail_len && detail[0] == '\0') {
    snprintf(detail, detail_len, "%s",
             scan_tr("scan.no_card_signable_inputs",
                     "No inputs signable by the current smartcard were found."));
  }
  return signatures_added;
}

// Classify output as self-transfer, change, or spend
static output_type_t classify_output(size_t output_index,
                                     const struct wally_tx_output *tx_output,
                                     const struct wally_tx *global_tx,
                                     uint32_t *address_index_out) {
  bool is_change = false;
  uint32_t address_index = 0;

  if (skip_verification) {
    return OUTPUT_TYPE_SPEND;
  }

  if (psbt_is_multisig(current_psbt) && wallet_has_descriptor()) {
    if (psbt_verify_output_with_descriptor(current_psbt, output_index,
                                           global_tx, &is_change,
                                           &address_index)) {
      *address_index_out = address_index;
      return is_change ? OUTPUT_TYPE_CHANGE : OUTPUT_TYPE_SELF_TRANSFER;
    }
    return OUTPUT_TYPE_SPEND;
  }

  if (!psbt_get_output_derivation(current_psbt, output_index, is_testnet,
                                  &is_change, &address_index)) {
    return OUTPUT_TYPE_SPEND;
  }

  unsigned char expected_script[WALLY_WITNESSSCRIPT_MAX_LEN];
  size_t expected_script_len;

  if (!wallet_get_scriptpubkey(is_change, address_index, expected_script,
                               &expected_script_len) ||
      tx_output->script_len != expected_script_len ||
      memcmp(tx_output->script, expected_script, expected_script_len) != 0) {
    return OUTPUT_TYPE_SPEND;
  }

  *address_index_out = address_index;
  return is_change ? OUTPUT_TYPE_CHANGE : OUTPUT_TYPE_SELF_TRANSFER;
}

static void back_button_cb(lv_event_t *e) {
  (void)e;
  if (web3_sign_task_handle &&
      !__atomic_load_n(&web3_sign_task_done, __ATOMIC_ACQUIRE)) {
    dialog_show_error(scan_tr("scan.signing_wait",
                              "Signing is in progress. Please wait."),
                      NULL, 1600);
    return;
  }

  void (*cb)(void) = return_callback;
  scan_page_destroy();
  if (cb)
    cb();
}

// --- Content detection ---

static char *scan_strndup(const char *text, size_t len) {
  char *out = malloc(len + 1);
  if (!out)
    return NULL;
  if (len > 0)
    memcpy(out, text, len);
  out[len] = '\0';
  return out;
}

static bool is_descriptor_prefix(const char *data) {
  return strncmp(data, "wsh(", 4) == 0 || strncmp(data, "sh(", 3) == 0 ||
         strncmp(data, "wpkh(", 5) == 0 || strncmp(data, "pkh(", 4) == 0 ||
         strncmp(data, "tr(", 3) == 0;
}

static bool is_bluewallet_descriptor(const char *data) {
  return strstr(data, "Policy:") != NULL;
}

static bool is_valid_address(const char *data) {
  const char *addr = data;
  char *stripped = NULL;

  // Strip BIP21 prefix
  if (strncasecmp(data, "bitcoin:", 8) == 0) {
    const char *start = data + 8;
    const char *query = strchr(start, '?');
    size_t addr_len = query ? (size_t)(query - start) : strlen(start);
    stripped = scan_strndup(start, addr_len);
    if (!stripped)
      return false;
    addr = stripped;
  }

  const char *hrp =
      (wallet_get_network() == WALLET_NETWORK_MAINNET) ? "bc" : "tb";
  uint32_t wally_net = (wallet_get_network() == WALLET_NETWORK_MAINNET)
                           ? WALLY_NETWORK_BITCOIN_MAINNET
                           : WALLY_NETWORK_BITCOIN_TESTNET;
  unsigned char script[128];
  size_t written = 0;
  bool valid =
      (wally_addr_segwit_to_bytes(addr, hrp, 0, script, sizeof(script),
                                  &written) == WALLY_OK) ||
      (wally_address_to_scriptpubkey(addr, wally_net, script, sizeof(script),
                                     &written) == WALLY_OK);
  free(stripped);
  return valid;
}

static bool contains_ci(const char *haystack, const char *needle) {
  if (!haystack || !needle || !*needle)
    return false;

  size_t needle_len = strlen(needle);
  for (const char *p = haystack; *p; p++) {
    if (strncasecmp(p, needle, needle_len) == 0)
      return true;
  }
  return false;
}

static bool starts_ci(const char *text, const char *prefix) {
  return text && prefix && strncasecmp(text, prefix, strlen(prefix)) == 0;
}

static bool looks_like_web3_relay_content(const char *content) {
  if (!content || !*content)
    return false;

  return starts_ci(content, "ur:eth-sign-request") ||
         starts_ci(content, "ethereum:") ||
         starts_ci(content, "tp:web3RelayNative-") ||
         starts_ci(content, "tp:signTransaction-") ||
         starts_ci(content, "tp:personalSign-") ||
         starts_ci(content, "tp:signPersonalMessage-") ||
         starts_ci(content, "tp:signTypedData-") ||
         starts_ci(content, "tp:signTypedDataV4-") ||
         starts_ci(content, "tp:signTypeData-") ||
         starts_ci(content, "tp:signTypeDataV4-") ||
         (content[0] == '{' &&
          (contains_ci(content, "\"protocol\":\"keystone-eth\"") ||
           contains_ci(content, "\"qr_type\":\"eth-sign-request\"") ||
           contains_ci(content, "\"response_protocol\":\"eth-signature\""))) ||
         (content[0] == '{' &&
          (contains_ci(content, "\"payload\":\"tp:sign") ||
           contains_ci(content, "\"payload\":\"ethereum:") ||
           contains_ci(content, "\"action\":\"EVM")));
}

static const char *json_value_start(const char *json, const char *key) {
  if (!json || !key)
    return NULL;

  char pattern[48];
  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  const char *p = strstr(json, pattern);
  if (!p)
    return NULL;
  p += strlen(pattern);
  while (*p && isspace((unsigned char)*p))
    p++;
  if (*p != ':')
    return NULL;
  p++;
  while (*p && isspace((unsigned char)*p))
    p++;
  return p;
}

static bool json_string_value(const char *json, const char *key, char *out,
                              size_t out_len) {
  if (!out || out_len == 0)
    return false;
  out[0] = '\0';

  const char *p = json_value_start(json, key);
  if (!p || *p != '"')
    return false;
  p++;

  size_t pos = 0;
  while (*p && *p != '"' && pos + 1 < out_len) {
    if (*p == '\\' && p[1]) {
      p++;
      switch (*p) {
      case 'n':
        out[pos++] = '\n';
        break;
      case 'r':
        out[pos++] = '\r';
        break;
      case 't':
        out[pos++] = '\t';
        break;
      default:
        out[pos++] = *p;
        break;
      }
    } else {
      out[pos++] = *p;
    }
    p++;
  }
  out[pos] = '\0';
  return *p == '"';
}

static bool json_int_value(const char *json, const char *key, long *out) {
  if (!out)
    return false;

  const char *p = json_value_start(json, key);
  if (!p)
    return false;
  if (*p == '"')
    p++;

  char *end = NULL;
  long value = strtol(p, &end, 10);
  if (end == p)
    return false;

  *out = value;
  return true;
}

static void web3_relay_info_done_cb(void *user_data) {
  (void)user_data;
  web3_info_active = false;
  smartcard_web3_mode = false;
  pending_web3_from_ur = false;
  if (return_callback)
    return_callback();
}

static void web3_request_clear(web3_sign_request_t *req) {
  if (!req)
    return;
  if (req->sign_data) {
    secure_memzero(req->sign_data, req->sign_data_len);
    free(req->sign_data);
  }
  memset(req, 0, sizeof(*req));
}

static void web3_set_request_id_string(web3_sign_request_t *req,
                                       const char *value) {
  if (!req || !value || !value[0])
    return;
  snprintf(req->request_id, sizeof(req->request_id), "%s", value);
  req->request_id_kind = WEB3_REQUEST_ID_STRING;
}

static int web3_hex_value(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

static bool web3_uuid_text_to_bytes(const char *text, uint8_t out[16]) {
  if (!text || !out || strlen(text) != 36)
    return false;
  if (text[8] != '-' || text[13] != '-' || text[18] != '-' ||
      text[23] != '-') {
    return false;
  }

  size_t out_pos = 0;
  int high = -1;
  for (size_t i = 0; text[i]; i++) {
    if (text[i] == '-')
      continue;
    int v = web3_hex_value(text[i]);
    if (v < 0)
      return false;
    if (high < 0) {
      high = v;
    } else {
      if (out_pos >= 16)
        return false;
      out[out_pos++] = (uint8_t)((high << 4) | v);
      high = -1;
    }
  }
  return high < 0 && out_pos == 16;
}

static void web3_set_request_id_text_fallback(web3_sign_request_t *req,
                                              const char *value) {
  if (!req || !value)
    return;
  while (*value && isspace((unsigned char)*value))
    value++;
  char trimmed[96];
  snprintf(trimmed, sizeof(trimmed), "%s", value);
  size_t len = strlen(trimmed);
  while (len > 0 && isspace((unsigned char)trimmed[len - 1]))
    trimmed[--len] = '\0';
  if (!trimmed[0])
    return;

  uint8_t uuid_bytes[16];
  if (web3_uuid_text_to_bytes(trimmed, uuid_bytes)) {
    snprintf(req->request_id, sizeof(req->request_id), "%s", trimmed);
    memcpy(req->request_id_uuid, uuid_bytes, sizeof(req->request_id_uuid));
    req->request_id_kind = WEB3_REQUEST_ID_UUID;
    return;
  }

  web3_set_request_id_string(req, trimmed);
}

static bool web3_set_request_id_bytes(web3_sign_request_t *req,
                                      const uint8_t *bytes, size_t len,
                                      uint64_t tag,
                                      web3_request_id_kind_t kind) {
  if (!req || !bytes || len == 0 || len > sizeof(req->request_id_bytes))
    return false;
  memcpy(req->request_id_bytes, bytes, len);
  req->request_id_bytes_len = len;
  req->request_id_tag = tag;
  req->request_id_kind = kind;
  size_t pos = 0;
  for (size_t i = 0; i < len && pos + 2 < sizeof(req->request_id); i++) {
    int n = snprintf(req->request_id + pos, sizeof(req->request_id) - pos,
                     "%02x", bytes[i]);
    if (n < 0)
      break;
    pos += (size_t)n;
  }
  return true;
}

static char *web3_strndup(const char *text, size_t len) {
  char *out = malloc(len + 1);
  if (!out)
    return NULL;
  if (len > 0)
    memcpy(out, text, len);
  out[len] = '\0';
  return out;
}

static void web3_uppercase_in_place(char *text) {
  if (!text)
    return;
  for (char *p = text; *p; p++)
    *p = (char)toupper((unsigned char)*p);
}

static char *web3_url_decode_alloc(const char *input, size_t input_len) {
  if (!input)
    return NULL;
  char *out = malloc(input_len + 1);
  if (!out)
    return NULL;

  size_t pos = 0;
  for (size_t i = 0; i < input_len; i++) {
    char c = input[i];
    if (c == '%' && i + 2 < input_len) {
      int hi = web3_hex_value(input[i + 1]);
      int lo = web3_hex_value(input[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out[pos++] = (char)((hi << 4) | lo);
        i += 2;
        continue;
      }
    }
    out[pos++] = (c == '+') ? ' ' : c;
  }
  out[pos] = '\0';
  return out;
}

static bool web3_query_param_decode(const char *query, const char *key,
                                    char *out, size_t out_len) {
  if (!query || !key || !out || out_len == 0)
    return false;
  out[0] = '\0';

  size_t key_len = strlen(key);
  const char *p = query;
  while (*p) {
    const char *part_end = strchr(p, '&');
    if (!part_end)
      part_end = p + strlen(p);

    const char *eq = memchr(p, '=', (size_t)(part_end - p));
    if (eq && (size_t)(eq - p) == key_len && strncmp(p, key, key_len) == 0) {
      char *decoded =
          web3_url_decode_alloc(eq + 1, (size_t)(part_end - eq - 1));
      if (!decoded)
        return false;
      snprintf(out, out_len, "%s", decoded);
      free(decoded);
      return true;
    }

    p = (*part_end == '&') ? part_end + 1 : part_end;
  }
  return false;
}

static bool web3_query_param_decode_alloc(const char *query, const char *key,
                                          char **out) {
  if (!query || !key || !out)
    return false;
  *out = NULL;

  size_t key_len = strlen(key);
  const char *p = query;
  while (*p) {
    const char *part_end = strchr(p, '&');
    if (!part_end)
      part_end = p + strlen(p);

    const char *eq = memchr(p, '=', (size_t)(part_end - p));
    if (eq && (size_t)(eq - p) == key_len && strncmp(p, key, key_len) == 0) {
      *out = web3_url_decode_alloc(eq + 1, (size_t)(part_end - eq - 1));
      return *out != NULL;
    }

    p = (*part_end == '&') ? part_end + 1 : part_end;
  }
  return false;
}

static bool web3_query_data_decode(const char *query, char **out) {
  if (!query || !out)
    return false;
  *out = NULL;

  const char *p = query;
  while (*p) {
    const char *part_end = strchr(p, '&');
    if (!part_end)
      part_end = p + strlen(p);

    const char *eq = memchr(p, '=', (size_t)(part_end - p));
    if (eq && (size_t)(eq - p) == 4 && strncmp(p, "data", 4) == 0) {
      *out = web3_url_decode_alloc(eq + 1, (size_t)(part_end - eq - 1));
      return *out != NULL;
    }

    p = (*part_end == '&') ? part_end + 1 : part_end;
  }

  return false;
}

static bool web3_json_string_value_alloc(const char *json, const char *key,
                                         char **out) {
  if (!json || !key || !out)
    return false;
  *out = NULL;

  char pattern[64];
  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  const char *p = strstr(json, pattern);
  if (!p)
    return false;
  p += strlen(pattern);
  while (*p && isspace((unsigned char)*p))
    p++;
  if (*p != ':')
    return false;
  p++;
  while (*p && isspace((unsigned char)*p))
    p++;
  if (*p != '"')
    return false;
  p++;

  size_t cap = strlen(p) + 1;
  char *buf = malloc(cap);
  if (!buf)
    return false;
  size_t pos = 0;
  while (*p && *p != '"') {
    if (*p == '\\' && p[1]) {
      p++;
      switch (*p) {
      case 'n':
        buf[pos++] = '\n';
        break;
      case 'r':
        buf[pos++] = '\r';
        break;
      case 't':
        buf[pos++] = '\t';
        break;
      case 'b':
        buf[pos++] = '\b';
        break;
      case 'f':
        buf[pos++] = '\f';
        break;
      default:
        buf[pos++] = *p;
        break;
      }
    } else {
      buf[pos++] = *p;
    }
    p++;
  }
  if (*p != '"') {
    free(buf);
    return false;
  }
  buf[pos] = '\0';
  *out = buf;
  return true;
}

static bool web3_json_string_value_copy(const char *json, const char *key,
                                        char *out, size_t out_len) {
  if (!out || out_len == 0)
    return false;
  out[0] = '\0';
  char *tmp = NULL;
  if (!web3_json_string_value_alloc(json, key, &tmp))
    return false;
  snprintf(out, out_len, "%s", tmp);
  free(tmp);
  return true;
}

static bool web3_json_raw_value_alloc(const char *json, const char *key,
                                      char **out) {
  if (!json || !key || !out)
    return false;
  *out = NULL;

  const char *p = json_value_start(json, key);
  if (!p)
    return false;

  const char *start = p;
  const char *end = NULL;
  bool in_string = false;
  bool escaped = false;
  int depth = 0;

  if (*p == '{' || *p == '[') {
    for (; *p; p++) {
      char c = *p;
      if (in_string) {
        if (escaped) {
          escaped = false;
        } else if (c == '\\') {
          escaped = true;
        } else if (c == '"') {
          in_string = false;
        }
        continue;
      }
      if (c == '"') {
        in_string = true;
      } else if (c == '{' || c == '[') {
        depth++;
      } else if (c == '}' || c == ']') {
        depth--;
        if (depth == 0) {
          end = p + 1;
          break;
        }
      }
    }
  } else {
    for (; *p; p++) {
      char c = *p;
      if (in_string) {
        if (escaped) {
          escaped = false;
        } else if (c == '\\') {
          escaped = true;
        } else if (c == '"') {
          in_string = false;
          if (p > start)
            end = p + 1;
        }
        continue;
      }
      if (c == '"') {
        in_string = true;
      } else if (c == ',' || c == '}' || c == ']') {
        end = p;
        break;
      }
    }
    if (!end)
      end = p;
  }

  if (!end || end <= start)
    return false;
  while (end > start && isspace((unsigned char)end[-1]))
    end--;
  *out = web3_strndup(start, (size_t)(end - start));
  return *out != NULL;
}

static bool web3_json_int_value(const char *json, const char *key, int *out) {
  if (!json || !key || !out)
    return false;
  char text[48];
  if (web3_json_string_value_copy(json, key, text, sizeof(text))) {
    char *end = NULL;
    long value = strtol(text, &end, 10);
    if (end != text) {
      *out = (int)value;
      return true;
    }
  }

  char pattern[64];
  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  const char *p = strstr(json, pattern);
  if (!p)
    return false;
  p += strlen(pattern);
  while (*p && isspace((unsigned char)*p))
    p++;
  if (*p != ':')
    return false;
  p++;
  while (*p && isspace((unsigned char)*p))
    p++;
  char *end = NULL;
  long value = strtol(p, &end, 10);
  if (end == p)
    return false;
  *out = (int)value;
  return true;
}

static bool web3_hex_to_bytes_alloc(const char *hex, uint8_t **out,
                                    size_t *out_len) {
  if (!hex || !out || !out_len)
    return false;
  *out = NULL;
  *out_len = 0;

  while (isspace((unsigned char)*hex))
    hex++;
  if (hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X'))
    hex += 2;

  size_t hex_len = 0;
  for (const char *p = hex; *p; p++) {
    if (isspace((unsigned char)*p))
      continue;
    if (web3_hex_value(*p) < 0)
      return false;
    hex_len++;
  }
  if (hex_len == 0 || (hex_len % 2U) != 0)
    return false;

  uint8_t *buf = malloc(hex_len / 2U);
  if (!buf)
    return false;

  int high = -1;
  size_t pos = 0;
  for (const char *p = hex; *p; p++) {
    if (isspace((unsigned char)*p))
      continue;
    int value = web3_hex_value(*p);
    if (high < 0) {
      high = value;
    } else {
      buf[pos++] = (uint8_t)((high << 4) | value);
      high = -1;
    }
  }

  *out = buf;
  *out_len = pos;
  return true;
}

static bool web3_copy_sign_data(web3_sign_request_t *req, const uint8_t *data,
                                size_t len) {
  if (!req || (!data && len > 0) || len == 0)
    return false;
  uint8_t *copy = malloc(len);
  if (!copy)
    return false;
  memcpy(copy, data, len);
  if (req->sign_data) {
    secure_memzero(req->sign_data, req->sign_data_len);
    free(req->sign_data);
  }
  req->sign_data = copy;
  req->sign_data_len = len;
  return true;
}

static bool web3_copy_sign_data_from_hex(web3_sign_request_t *req,
                                         const char *hex) {
  uint8_t *bytes = NULL;
  size_t len = 0;
  if (!web3_hex_to_bytes_alloc(hex, &bytes, &len))
    return false;
  bool ok = web3_copy_sign_data(req, bytes, len);
  secure_memzero(bytes, len);
  free(bytes);
  return ok;
}

static bool web3_decode_personal_message_alloc(const char *message,
                                               uint8_t **out,
                                               size_t *out_len) {
  if (!message || !out || !out_len)
    return false;
  *out = NULL;
  *out_len = 0;
  if (starts_ci(message, "0x")) {
    if (web3_hex_to_bytes_alloc(message, out, out_len))
      return true;
  }

  size_t len = strlen(message);
  uint8_t *buf = malloc(len ? len : 1);
  if (!buf)
    return false;
  if (len > 0)
    memcpy(buf, message, len);
  *out = buf;
  *out_len = len;
  return true;
}

static bool web3_parse_tp_payload(const char *payload,
                                  web3_sign_request_t *req) {
  if (!payload || !req || !starts_ci(payload, "tp:"))
    return false;

  const char *dash = strchr(payload, '-');
  if (!dash || dash == payload)
    return false;

  size_t action_len = (size_t)(dash - payload);
  if (action_len >= sizeof(req->action))
    action_len = sizeof(req->action) - 1;
  memcpy(req->action, payload, action_len);
  req->action[action_len] = '\0';

  const char *query = dash + 1;
  if (*query == '?')
    query++;

  char *data_json = NULL;
  if (!web3_query_data_decode(query, &data_json))
    return false;

  char text[128];
  if (web3_query_param_decode(query, "chain_id", text, sizeof(text)) ||
      web3_query_param_decode(query, "blockchainId", text, sizeof(text))) {
    char *end = NULL;
    long value = strtol(strrchr(text, ':') ? strrchr(text, ':') + 1 : text,
                        &end, 10);
    if (end != text)
      req->chain_id = (int)value;
  }
  if (web3_query_param_decode(query, "requestId", text, sizeof(text)))
    web3_set_request_id_text_fallback(req, text);
  if (web3_query_param_decode(query, "path", text, sizeof(text)) && text[0])
    snprintf(req->path, sizeof(req->path), "%s", text);

  bool ok = false;
  if (contains_ci(req->action, "personalSign") ||
      contains_ci(req->action, "signPersonalMessage") ||
      contains_ci(req->action, "ethSign") ||
      contains_ci(req->action, "signMessage")) {
    char *message = NULL;
    if (web3_json_string_value_alloc(data_json, "message", &message)) {
      uint8_t *bytes = NULL;
      size_t bytes_len = 0;
      if (web3_decode_personal_message_alloc(message, &bytes, &bytes_len)) {
        req->request_type_id = WEB3_REQUEST_TYPE_PERSONAL;
        ok = web3_copy_sign_data(req, bytes, bytes_len);
        secure_memzero(bytes, bytes_len);
        free(bytes);
      }
      free(message);
    }
  } else if (contains_ci(req->action, "signTypedData") ||
             contains_ci(req->action, "signTypeData")) {
    char *typed_json = NULL;
    if (!web3_json_string_value_alloc(data_json, "message", &typed_json))
      (void)web3_json_raw_value_alloc(data_json, "message", &typed_json);
    if (typed_json && typed_json[0]) {
      req->request_type_id = WEB3_REQUEST_TYPE_TYPED_DATA;
      ok = web3_copy_sign_data(req, (const uint8_t *)typed_json,
                               strlen(typed_json));
      snprintf(req->detail, sizeof(req->detail), "%s",
               scan_tr("scan.typed_data_review_notice",
                       "TypedData recognized. Review the full content before "
                       "signing."));
    } else {
      snprintf(req->detail, sizeof(req->detail), "%s",
               scan_tr("scan.sign_typed_data_missing_message",
                       "signTypedData is missing the message field."));
      ok = false;
    }
    free(typed_json);
  } else if (contains_ci(req->action, "signTransaction")) {
    snprintf(req->detail, sizeof(req->detail), "%s",
             scan_tr("scan.tp_tx_missing_sign_data",
                     "TP transaction QR requires request_sign_data_hex. This QR "
                     "does not include directly signable data."));
    ok = false;
  }

  (void)web3_json_string_value_copy(data_json, "address", req->address,
                                    sizeof(req->address));
  free(data_json);
  return ok;
}

static bool web3_parse_native_relay_query(const char *content,
                                          web3_sign_request_t *req) {
  if (!starts_ci(content, "tp:web3RelayNative-") || !req)
    return false;

  const char *query = strchr(content, '-');
  if (!query)
    return false;
  query++;
  if (*query == '?')
    query++;

  char text[1024];
  (void)web3_query_param_decode(query, "wallet_name", req->wallet,
                                sizeof(req->wallet));
  if (!req->wallet[0])
    (void)web3_query_param_decode(query, "wallet", req->wallet,
                                  sizeof(req->wallet));
  (void)web3_query_param_decode(query, "action", req->action,
                                sizeof(req->action));
  (void)web3_query_param_decode(query, "chain", req->chain,
                                sizeof(req->chain));
  (void)web3_query_param_decode(query, "origin", req->origin,
                                sizeof(req->origin));
  if (web3_query_param_decode(query, "request_id", text, sizeof(text)))
    web3_set_request_id_text_fallback(req, text);
  (void)web3_query_param_decode(query, "path", req->path, sizeof(req->path));
  (void)web3_query_param_decode(query, "address", req->address,
                                sizeof(req->address));
  if (!req->address[0])
    (void)web3_query_param_decode(query, "expected_address", req->address,
                                  sizeof(req->address));

  if (web3_query_param_decode(query, "request_data_type_id", text,
                              sizeof(text))) {
    req->request_type_id = atoi(text);
  }
  if (web3_query_param_decode(query, "chain_id", text, sizeof(text)))
    req->chain_id = atoi(text);
  char *sign_hex = NULL;
  if (web3_query_param_decode_alloc(query, "request_sign_data_hex",
                                    &sign_hex) &&
      sign_hex && sign_hex[0]) {
    bool ok = web3_copy_sign_data_from_hex(req, sign_hex);
    free(sign_hex);
    return ok;
  }
  free(sign_hex);

  char *payload = NULL;
  if (web3_query_param_decode_alloc(query, "payload", &payload)) {
    bool ok = web3_parse_tp_payload(payload, req);
    free(payload);
    return ok;
  }
  return false;
}

static bool web3_parse_json_envelope(const char *content,
                                     web3_sign_request_t *req) {
  if (!content || content[0] != '{' || !req)
    return false;

  (void)web3_json_string_value_copy(content, "wallet_name", req->wallet,
                                    sizeof(req->wallet));
  if (!req->wallet[0])
    (void)web3_json_string_value_copy(content, "wallet", req->wallet,
                                      sizeof(req->wallet));
  (void)web3_json_string_value_copy(content, "action", req->action,
                                    sizeof(req->action));
  (void)web3_json_string_value_copy(content, "chain", req->chain,
                                    sizeof(req->chain));
  (void)web3_json_string_value_copy(content, "origin", req->origin,
                                    sizeof(req->origin));
  char request_id_text[128];
  if (web3_json_string_value_copy(content, "request_id", request_id_text,
                                  sizeof(request_id_text)) ||
      web3_json_string_value_copy(content, "requestId", request_id_text,
                                  sizeof(request_id_text))) {
    web3_set_request_id_text_fallback(req, request_id_text);
  }
  (void)web3_json_string_value_copy(content, "address_path", req->path,
                                    sizeof(req->path));
  if (!req->path[0])
    (void)web3_json_string_value_copy(content, "path", req->path,
                                      sizeof(req->path));
  (void)web3_json_string_value_copy(content, "address", req->address,
                                    sizeof(req->address));
  if (!req->address[0])
    (void)web3_json_string_value_copy(content, "expected_address",
                                      req->address, sizeof(req->address));
  (void)web3_json_int_value(content, "request_data_type_id",
                            &req->request_type_id);
  (void)web3_json_int_value(content, "chain_id", &req->chain_id);

  char *sign_hex = NULL;
  if (web3_json_string_value_alloc(content, "request_sign_data_hex",
                                   &sign_hex) &&
      sign_hex[0]) {
    bool ok = web3_copy_sign_data_from_hex(req, sign_hex);
    free(sign_hex);
    return ok;
  }
  free(sign_hex);

  char *payload = NULL;
  bool ok = false;
  if (web3_json_string_value_alloc(content, "payload", &payload)) {
    ok = web3_parse_tp_payload(payload, req);
    free(payload);
  }
  return ok;
}

static bool web3_parse_sign_request(const char *content,
                                    web3_sign_request_t *req) {
  if (!content || !req)
    return false;
  web3_request_clear(req);
  req->chain_id = 1;
  snprintf(req->path, sizeof(req->path), "%s", WEB3_DEFAULT_DERIVATION_PATH);

  bool ok = false;
  if (starts_ci(content, "tp:web3RelayNative-")) {
    ok = web3_parse_native_relay_query(content, req);
  } else if (content[0] == '{') {
    ok = web3_parse_json_envelope(content, req);
  } else if (starts_ci(content, "tp:")) {
    ok = web3_parse_tp_payload(content, req);
  }

  if (!req->wallet[0])
    snprintf(req->wallet, sizeof(req->wallet), "Web3");
  if (!req->action[0]) {
    const char *label = scan_tr("scan.evm_signing", "EVM signing");
    if (req->request_type_id == WEB3_REQUEST_TYPE_PERSONAL)
      label = "personalSign";
    else if (req->request_type_id == WEB3_REQUEST_TYPE_LEGACY_TX ||
             req->request_type_id == WEB3_REQUEST_TYPE_TYPED_TX)
      label = scan_tr("scan.transaction_signing", "Transaction signing");
    else if (req->request_type_id == WEB3_REQUEST_TYPE_TYPED_DATA)
      label = "TypedData";
    snprintf(req->action, sizeof(req->action), "%s", label);
  }
  if (!req->chain[0])
    snprintf(req->chain, sizeof(req->chain), "EVM %d", req->chain_id);
  if (req->request_type_id == WEB3_REQUEST_TYPE_TYPED_DATA &&
      !req->detail[0]) {
    snprintf(req->detail, sizeof(req->detail), "%s",
             scan_tr("scan.typed_data_review_notice",
                     "TypedData recognized. Review the full content before "
                     "signing."));
  }

  req->valid = ok && req->sign_data && req->sign_data_len > 0 &&
               req->request_type_id > 0;
  if (!req->valid && !req->detail[0]) {
    snprintf(req->detail, sizeof(req->detail), "%s",
             scan_tr("scan.web3_qr_missing_sign_data",
                     "This Web3 QR is missing signable data. Use the OKX/"
                     "Bitget Keystone hardware-wallet signing QR."));
  }
  return req->valid;
}

static bool web3_keypath_to_text(cbor_value_t *value, char *out,
                                 size_t out_len) {
  if (!out || out_len == 0)
    return false;
  out[0] = '\0';
  if (!value)
    return false;
  if (cbor_value_get_type(value) == CBOR_TYPE_TAG) {
    if (cbor_value_get_tag(value) != 304)
      return false;
    value = cbor_value_get_tag_content(value);
  }
  if (!value || cbor_value_get_type(value) != CBOR_TYPE_MAP)
    return false;

  cbor_value_t *components = cbor_map_get_int(value, 1);
  if (!components || cbor_value_get_type(components) != CBOR_TYPE_ARRAY)
    return false;

  size_t count = cbor_value_get_array_size(components);
  if ((count % 2U) != 0)
    return false;

  size_t pos = 0;
  int written = snprintf(out, out_len, "m");
  if (written < 0 || (size_t)written >= out_len)
    return false;
  pos = (size_t)written;

  for (size_t i = 0; i < count; i += 2) {
    cbor_value_t *index_val = cbor_value_get_array_item(components, i);
    cbor_value_t *hardened_val = cbor_value_get_array_item(components, i + 1);
    if (!index_val || !hardened_val ||
        cbor_value_get_type(hardened_val) != CBOR_TYPE_BOOL)
      return false;

    char component[32];
    if (cbor_value_get_type(index_val) == CBOR_TYPE_UNSIGNED_INT) {
      snprintf(component, sizeof(component), "%llu%s",
               (unsigned long long)cbor_value_get_uint(index_val),
               cbor_value_get_bool(hardened_val) ? "'" : "");
    } else if (cbor_value_get_type(index_val) == CBOR_TYPE_ARRAY &&
               cbor_value_get_array_size(index_val) == 0) {
      snprintf(component, sizeof(component), "*%s",
               cbor_value_get_bool(hardened_val) ? "'" : "");
    } else {
      return false;
    }

    written = snprintf(out + pos, out_len - pos, "/%s", component);
    if (written < 0 || (size_t)written >= out_len - pos)
      return false;
    pos += (size_t)written;
  }

  return true;
}

static bool web3_store_request_id_from_cbor(cbor_value_t *value,
                                            web3_sign_request_t *req) {
  if (!value || !req)
    return false;

  uint64_t tag = 0;
  if (cbor_value_get_type(value) == CBOR_TYPE_TAG) {
    tag = cbor_value_get_tag(value);
    value = cbor_value_get_tag_content(value);
  }
  if (!value)
    return false;

  switch (cbor_value_get_type(value)) {
  case CBOR_TYPE_STRING: {
    const char *text = cbor_value_get_string(value);
    if (!text || !text[0])
      return false;
    if (tag != 0 && tag != 37) {
      web3_set_request_id_string(req, text);
      req->request_id_tag = tag;
    } else {
      web3_set_request_id_text_fallback(req, text);
    }
    return true;
  }
  case CBOR_TYPE_UNSIGNED_INT:
    req->request_id_uint = cbor_value_get_uint(value);
    req->request_id_tag = (tag != 0 && tag != 37) ? tag : 0;
    snprintf(req->request_id, sizeof(req->request_id), "%llu",
             (unsigned long long)req->request_id_uint);
    req->request_id_kind = WEB3_REQUEST_ID_UINT;
    return true;
  case CBOR_TYPE_BYTES: {
    size_t len = 0;
    const uint8_t *bytes = cbor_value_get_bytes(value, &len);
    if (!bytes || len == 0)
      return false;
    web3_request_id_kind_t kind =
        (tag == 37 && len == sizeof(req->request_id_uuid))
            ? WEB3_REQUEST_ID_UUID
            : WEB3_REQUEST_ID_BYTES;
    uint64_t preserved_tag =
        (kind != WEB3_REQUEST_ID_UUID && tag != 0) ? tag : 0;
    if (!web3_set_request_id_bytes(req, bytes, len, preserved_tag, kind))
      return false;
    if (kind == WEB3_REQUEST_ID_UUID)
      memcpy(req->request_id_uuid, bytes, sizeof(req->request_id_uuid));
    return true;
  }
  default:
    return false;
  }
}

static bool web3_parse_eth_sign_request_cbor(const uint8_t *cbor_data,
                                             size_t cbor_len,
                                             web3_sign_request_t *req) {
  if (!cbor_data || cbor_len == 0 || !req)
    return false;

  web3_request_clear(req);
  req->chain_id = 1;
  snprintf(req->wallet, sizeof(req->wallet), "Keystone");
  snprintf(req->path, sizeof(req->path), "%s", WEB3_DEFAULT_DERIVATION_PATH);

  cbor_value_t *decoded = cbor_decode(cbor_data, cbor_len);
  cbor_value_t *root = decoded;
  if (!decoded)
    return false;
  if (cbor_value_get_type(root) == CBOR_TYPE_TAG)
    root = cbor_value_get_tag_content(root);
  if (!root || cbor_value_get_type(root) != CBOR_TYPE_MAP) {
    cbor_value_free(decoded);
    return false;
  }

  bool ok = false;
  cbor_value_t *sign_data = cbor_map_get_int(root, 2);
  if (!sign_data || cbor_value_get_type(sign_data) != CBOR_TYPE_BYTES)
    goto done;
  size_t sign_data_len = 0;
  const uint8_t *sign_data_bytes =
      cbor_value_get_bytes(sign_data, &sign_data_len);
  if (!web3_copy_sign_data(req, sign_data_bytes, sign_data_len))
    goto done;

  cbor_value_t *data_type = cbor_map_get_int(root, 3);
  req->request_type_id =
      (data_type && cbor_value_get_type(data_type) == CBOR_TYPE_UNSIGNED_INT)
          ? (int)cbor_value_get_uint(data_type)
          : WEB3_REQUEST_TYPE_LEGACY_TX;
  cbor_value_t *chain_id = cbor_map_get_int(root, 4);
  if (chain_id && cbor_value_get_type(chain_id) == CBOR_TYPE_UNSIGNED_INT)
    req->chain_id = (int)cbor_value_get_uint(chain_id);

  (void)web3_keypath_to_text(cbor_map_get_int(root, 5), req->path,
                             sizeof(req->path));
  cbor_value_t *address = cbor_map_get_int(root, 6);
  if (address && cbor_value_get_type(address) == CBOR_TYPE_BYTES) {
    size_t address_len = 0;
    const uint8_t *address_bytes = cbor_value_get_bytes(address, &address_len);
    if (address_bytes && address_len == 20) {
      size_t pos = 0;
      pos += snprintf(req->address + pos, sizeof(req->address) - pos, "0x");
      for (size_t i = 0; i < address_len && pos + 2 < sizeof(req->address);
           i++) {
        pos += snprintf(req->address + pos, sizeof(req->address) - pos,
                        "%02x", address_bytes[i]);
      }
    }
  }
  cbor_value_t *origin = cbor_map_get_int(root, 7);
  if (origin && cbor_value_get_type(origin) == CBOR_TYPE_STRING) {
    const char *origin_text = cbor_value_get_string(origin);
    snprintf(req->origin, sizeof(req->origin), "%s",
             origin_text ? origin_text : "");
  }
  (void)web3_store_request_id_from_cbor(cbor_map_get_int(root, 1), req);

  if (contains_ci(req->origin, "bitget") || contains_ci(req->origin, "bitkeep"))
    snprintf(req->wallet, sizeof(req->wallet), "Bitget Wallet");
  else if (contains_ci(req->origin, "okx") || contains_ci(req->origin, "okex"))
    snprintf(req->wallet, sizeof(req->wallet), "OKX Wallet");
  snprintf(req->chain, sizeof(req->chain), "EVM %d", req->chain_id);
  snprintf(req->action, sizeof(req->action), "%s",
           req->request_type_id == WEB3_REQUEST_TYPE_PERSONAL
               ? "personalSign"
               : (req->request_type_id == WEB3_REQUEST_TYPE_TYPED_DATA
                      ? "TypedData"
                      : scan_tr("scan.transaction_signing",
                                "Transaction signing")));
  req->valid = true;
  ok = true;

done:
  cbor_value_free(decoded);
  if (!ok)
    web3_request_clear(req);
  return ok;
}

static bool web3_make_digest(const web3_sign_request_t *req, uint8_t digest[32],
                             char *err, size_t err_len) {
  if (err && err_len > 0)
    err[0] = '\0';
  if (!req || !digest)
    return false;
  if (req->request_type_id == WEB3_REQUEST_TYPE_PERSONAL) {
    char prefix[64];
    int n = snprintf(prefix, sizeof(prefix), "\x19"
                     "Ethereum Signed Message:\n%u",
                     (unsigned)req->sign_data_len);
    if (n < 0)
      n = 0;
    size_t prefix_len = (size_t)n;
    uint8_t *buf = malloc(prefix_len + req->sign_data_len);
    if (!buf) {
      memset(digest, 0, 32);
      if (err && err_len > 0)
        snprintf(err, err_len, "%s",
                 scan_tr("scan.out_of_memory", "Out of memory"));
      return false;
    }
    memcpy(buf, prefix, prefix_len);
    memcpy(buf + prefix_len, req->sign_data, req->sign_data_len);
    evm_keccak256(buf, prefix_len + req->sign_data_len, digest);
    secure_memzero(buf, prefix_len + req->sign_data_len);
    free(buf);
    return true;
  }

  if (req->request_type_id == WEB3_REQUEST_TYPE_TYPED_DATA) {
    char *typed_json = malloc(req->sign_data_len + 1U);
    if (!typed_json) {
      memset(digest, 0, 32);
      if (err && err_len > 0)
        snprintf(err, err_len, "%s",
                 scan_tr("scan.out_of_memory", "Out of memory"));
      return false;
    }
    memcpy(typed_json, req->sign_data, req->sign_data_len);
    typed_json[req->sign_data_len] = '\0';
    bool ok = eip712_hash_typed_data_json(typed_json, digest, err, err_len);
    secure_memzero(typed_json, req->sign_data_len);
    free(typed_json);
    if (!ok)
      memset(digest, 0, 32);
    return ok;
  }

  evm_keccak256(req->sign_data, req->sign_data_len, digest);
  return true;
}

static bool web3_build_eth_signature_ur(const web3_sign_request_t *req,
                                        const uint8_t *signature,
                                        size_t signature_len, char **out_ur) {
  if (!req || !signature || !out_ur)
    return false;
  *out_ur = NULL;

  cbor_value_t *map = cbor_value_new_map();
  if (!map)
    return false;

  bool ok = true;
  if (req->request_id_kind != WEB3_REQUEST_ID_NONE || req->request_id[0]) {
    cbor_value_t *request_id_value = NULL;
    if (req->request_id_kind == WEB3_REQUEST_ID_UUID) {
      cbor_value_t *uuid_bytes =
          cbor_value_new_bytes(req->request_id_uuid, sizeof(req->request_id_uuid));
      request_id_value = uuid_bytes ? cbor_value_new_tag(37, uuid_bytes) : NULL;
      if (!request_id_value)
        cbor_value_free(uuid_bytes);
    } else if (req->request_id_kind == WEB3_REQUEST_ID_BYTES) {
      request_id_value = cbor_value_new_bytes(req->request_id_bytes,
                                              req->request_id_bytes_len);
    } else if (req->request_id_kind == WEB3_REQUEST_ID_UINT) {
      request_id_value = cbor_value_new_unsigned_int(req->request_id_uint);
    } else {
      request_id_value = cbor_value_new_string(req->request_id);
    }
    if (request_id_value &&
        (contains_ci(req->origin, "imtoken") ||
         contains_ci(req->wallet, "imtoken")) &&
        req->request_id_kind == WEB3_REQUEST_ID_STRING && req->request_id[0]) {
      cbor_value_free(request_id_value);
      request_id_value = cbor_value_new_bytes((const uint8_t *)req->request_id,
                                              strlen(req->request_id));
      cbor_value_t *tagged =
          request_id_value ? cbor_value_new_tag(37, request_id_value) : NULL;
      if (tagged) {
        request_id_value = tagged;
      } else {
        cbor_value_free(request_id_value);
        request_id_value = NULL;
      }
    } else if (request_id_value &&
               req->request_id_kind != WEB3_REQUEST_ID_UUID &&
               req->request_id_tag != 0) {
      cbor_value_t *tagged =
          cbor_value_new_tag(req->request_id_tag, request_id_value);
      if (tagged) {
        request_id_value = tagged;
      } else {
        cbor_value_free(request_id_value);
        request_id_value = NULL;
      }
    }
    ok = request_id_value &&
         cbor_map_set(map, cbor_value_new_unsigned_int(1), request_id_value);
    if (!ok)
      cbor_value_free(request_id_value);
  }
  if (ok) {
    ok = cbor_map_set(map, cbor_value_new_unsigned_int(2),
                      cbor_value_new_bytes(signature, signature_len));
  }
  const char *origin_text = req->origin;
  if (contains_ci(req->origin, "imtoken") ||
      contains_ci(req->wallet, "imtoken")) {
    origin_text = "imToken";
  }
  if (ok && origin_text[0]) {
    ok = cbor_map_set(map, cbor_value_new_unsigned_int(3),
                      cbor_value_new_string(origin_text));
  }

  size_t cbor_len = 0;
  uint8_t *cbor = ok ? cbor_encode(map, &cbor_len) : NULL;
  cbor_value_free(map);
  if (!cbor)
    return false;

  ok = ur_encoder_encode_single("eth-signature", cbor, cbor_len, out_ur);
  if (ok && out_ur && *out_ur)
    web3_uppercase_in_place(*out_ur);
  ESP_LOGI(TAG,
           "ETH_SIGNATURE_QR ok=%d cbor=%u sig=%u sig_last=%02X "
           "req_id_kind=%d req_id_tag=%llu req_id_bytes=%u origin='%s' len=%u",
           ok ? 1 : 0, (unsigned)cbor_len, (unsigned)signature_len,
           signature_len > 0 ? signature[signature_len - 1] : 0,
           (int)req->request_id_kind,
           (unsigned long long)req->request_id_tag,
           (unsigned)req->request_id_bytes_len, req->origin,
           (unsigned)(ok && *out_ur ? strlen(*out_ur) : 0));
  free(cbor);
  return ok && *out_ur;
}

static void web3_pin_input_cleanup(void) {
  if (web3_pin_input_active) {
    ui_text_input_destroy(&web3_pin_input);
    memset(&web3_pin_input, 0, sizeof(web3_pin_input));
    web3_pin_input_active = false;
  }
}

static void web3_pin_back_cb(lv_event_t *event) {
  (void)event;
  web3_pin_input_cleanup();
  web3_show_request_summary();
}

static void web3_pin_sign_after_summary_cb(lv_event_t *event) {
  (void)event;
  web3_show_pin_input(&pending_web3_request);
}

static void web3_show_sign_result_qr(char *response_ur) {
  saved_return_callback = return_callback;
  if (!qr_viewer_page_create(lv_screen_active(), response_ur,
                             scan_tr("scan.web3_signature_result",
                                     "Web3 signature result"),
                             return_from_qr_viewer_cb)) {
    dialog_show_error(scan_tr("scan.signature_qr_create_failed",
                              "Signature result QR creation failed"),
                      return_callback, 0);
    free(response_ur);
    return;
  }

  free(response_ur);
  web3_request_clear(&pending_web3_request);
  pending_web3_source = WEB3_SIGN_SOURCE_NONE;
  scan_page_hide();
  scan_page_destroy();
  qr_viewer_page_show();
}

static void web3_sign_task(void *arg) {
  (void)arg;
  memset(&web3_sign_task_sig, 0, sizeof(web3_sign_task_sig));
  const bool delete_with_caps = web3_sign_task_with_caps;
  ESP_LOGD(TAG, "WEB3_SIGN begin");
  web3_sign_task_err = smartcard_satochip_sign_evm_digest(
      web3_sign_task_pin, web3_sign_task_path, web3_sign_task_digest,
      &web3_sign_task_sig, 25000);
  ESP_LOGD(TAG, "WEB3_SIGN done err=%s SW=%04X",
           esp_err_to_name(web3_sign_task_err), web3_sign_task_sig.sw);
  secure_memzero(web3_sign_task_pin, sizeof(web3_sign_task_pin));
  secure_memzero(web3_sign_task_digest, sizeof(web3_sign_task_digest));
  __atomic_store_n(&web3_sign_task_done, true, __ATOMIC_RELEASE);
  if (delete_with_caps) {
    vTaskDeleteWithCaps(NULL);
    return;
  }
  vTaskDelete(NULL);
}

static void web3_sign_finish_ui(void) {
  if (web3_sign_progress_dialog) {
    lv_obj_del(web3_sign_progress_dialog);
    web3_sign_progress_dialog = NULL;
  }

  if (web3_sign_task_err != ESP_OK) {
    char msg[384];
    snprintf(msg, sizeof(msg),
             scan_tr("scan.card_sign_failed_detail",
                     "Smartcard signing failed: %s\n%s"),
             esp_err_to_name(web3_sign_task_err), web3_sign_task_sig.detail);
    dialog_show_error(msg, web3_resume_current_step, 0);
    secure_memzero(&web3_sign_task_sig, sizeof(web3_sign_task_sig));
    return;
  }

  if (pending_web3_request.address[0] && web3_sign_task_sig.has_address &&
      strcasecmp(pending_web3_request.address, web3_sign_task_sig.address) != 0) {
    char msg[384];
    snprintf(msg, sizeof(msg),
             scan_tr("scan.card_address_mismatch",
                     "Signing address mismatch. Refused.\nRequest: %s\n"
                     "Smartcard: %s"),
             pending_web3_request.address, web3_sign_task_sig.address);
    dialog_show_error(msg, web3_resume_current_step, 0);
    secure_memzero(&web3_sign_task_sig, sizeof(web3_sign_task_sig));
    return;
  }

  size_t signature_len = 0;
  uint8_t signature[72];
  if (!web3_compose_signature_bytes(&pending_web3_request,
                                    web3_sign_task_sig.compact,
                                    web3_sign_task_sig.recovery_id, signature,
                                    &signature_len)) {
    secure_memzero(signature, sizeof(signature));
    secure_memzero(&web3_sign_task_sig, sizeof(web3_sign_task_sig));
    dialog_show_error(scan_tr("scan.signature_result_encode_failed",
                              "Signature result encoding failed"),
                      web3_resume_current_step, 0);
    return;
  }

  char *response_ur = NULL;
  bool ok = web3_build_eth_signature_ur(&pending_web3_request, signature,
                                        signature_len, &response_ur);
  secure_memzero(signature, sizeof(signature));
  secure_memzero(&web3_sign_task_sig, sizeof(web3_sign_task_sig));

  if (!ok || !response_ur) {
    dialog_show_error(scan_tr("scan.signature_result_encode_failed",
                              "Signature result encoding failed"),
                      web3_resume_current_step, 0);
    free(response_ur);
    return;
  }

  web3_show_sign_result_qr(response_ur);
}

static void web3_sign_poll_cb(lv_timer_t *timer) {
  (void)timer;
  if (!__atomic_load_n(&web3_sign_task_done, __ATOMIC_ACQUIRE))
    return;

  if (web3_sign_poll_timer) {
    lv_timer_del(web3_sign_poll_timer);
    web3_sign_poll_timer = NULL;
  }
  web3_sign_task_handle = NULL;
  web3_sign_task_with_caps = false;
  web3_sign_finish_ui();
}

static void web3_pin_ready_cb(lv_event_t *event) {
  (void)event;
  if (!web3_pin_input.textarea)
    return;
  const char *pin_text = lv_textarea_get_text(web3_pin_input.textarea);
  if (!pin_text || pin_text[0] == '\0') {
    dialog_show_error(scan_tr("sign.enter_card_pin", "Enter smartcard PIN"),
                      NULL, 1600);
    return;
  }
  char pin_copy[80];
  snprintf(pin_copy, sizeof(pin_copy), "%s", pin_text);
  web3_pin_input_cleanup();

  if (web3_sign_task_handle) {
    secure_memzero(pin_copy, sizeof(pin_copy));
    dialog_show_error(scan_tr("sign.card_busy", "Smartcard busy"), NULL,
                      1600);
    return;
  }

  snprintf(web3_sign_task_pin, sizeof(web3_sign_task_pin), "%s", pin_copy);
  snprintf(web3_sign_task_path, sizeof(web3_sign_task_path), "%s",
           pending_web3_request.path[0] ? pending_web3_request.path
                                        : WEB3_DEFAULT_DERIVATION_PATH);
  secure_memzero(pin_copy, sizeof(pin_copy));
  char digest_err[192];
  if (!web3_make_digest(&pending_web3_request, web3_sign_task_digest,
                        digest_err, sizeof(digest_err))) {
    secure_memzero(web3_sign_task_pin, sizeof(web3_sign_task_pin));
    char msg[256];
    snprintf(msg, sizeof(msg),
             scan_tr("scan.sign_data_parse_failed_detail",
                     "Signing data parse failed\n%s"),
             digest_err[0] ? digest_err
                           : scan_tr("scan.check_qr_content",
                                     "Check the QR content"));
    dialog_show_error(msg, web3_show_request_summary, 0);
    return;
  }
  memset(&web3_sign_task_sig, 0, sizeof(web3_sign_task_sig));
  __atomic_store_n(&web3_sign_task_done, false, __ATOMIC_RELEASE);
  web3_sign_task_with_caps = false;
  web3_sign_task_err = ESP_ERR_INVALID_STATE;

  ESP_LOGD(TAG,
           "Creating Satochip sign task; internal=%u min_internal=%u spiram=%u",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  web3_sign_task_with_caps = true;
  BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(
      web3_sign_task, "satochip_sign",
      WEB3_SATOCHIP_SIGN_TASK_PSRAM_STACK_SIZE, NULL, 4,
      &web3_sign_task_handle, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (ok != pdPASS) {
    ESP_LOGW(TAG,
             "Satochip sign task PSRAM stack failed; internal=%u min_internal=%u spiram=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    web3_sign_task_with_caps = false;
    ok = xTaskCreatePinnedToCore(
        web3_sign_task, "satochip_sign", WEB3_SATOCHIP_SIGN_TASK_STACK_SIZE,
        NULL, 4, &web3_sign_task_handle, 1);
  }
  if (ok != pdPASS) {
    ESP_LOGE(TAG,
             "Satochip sign task create failed; internal=%u min_internal=%u spiram=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    secure_memzero(web3_sign_task_pin, sizeof(web3_sign_task_pin));
    secure_memzero(web3_sign_task_digest, sizeof(web3_sign_task_digest));
    web3_sign_task_handle = NULL;
    web3_sign_task_with_caps = false;
    dialog_show_error(scan_tr("scan.card_sign_task_start_failed",
                              "Smartcard signing task failed to start"),
                      return_callback, 0);
    web3_request_clear(&pending_web3_request);
    return;
  }

  web3_sign_progress_dialog = dialog_show_progress(
      scan_tr("scan.smartcard_signing", "Smartcard signing"),
      scan_tr("sign.signing", "Signing"), DIALOG_STYLE_OVERLAY);
  lv_refr_now(NULL);
  web3_sign_poll_timer = lv_timer_create(web3_sign_poll_cb, 100, NULL);
}

static void web3_show_pin_input(const web3_sign_request_t *req) {
  lv_obj_t *root = web3_prepare_page();

  (void)web3_create_fixed_title(root,
                                scan_tr("sign.card_pin", "Smartcard PIN"));

  web3_pin_input_cleanup();
  ui_text_input_create(&web3_pin_input, root,
                       scan_tr("sign.card_pin", "Smartcard PIN"), true,
                       web3_pin_ready_cb);
  web3_pin_input_active = true;
  if (web3_pin_input.keyboard)
    lv_obj_add_event_cb(web3_pin_input.keyboard, web3_pin_back_cb,
                        LV_EVENT_CANCEL, NULL);
  if (web3_pin_input.textarea)
    lv_obj_add_event_cb(web3_pin_input.textarea, web3_pin_back_cb,
                        LV_EVENT_CANCEL, NULL);

  lv_obj_t *card = lv_obj_create(root);
  lv_obj_set_width(card, LV_PCT(100));
  lv_obj_set_height(card, LV_VER_RES * 27 / 100);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(card, LV_DIR_VER);
  lv_obj_add_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  theme_apply_frame(card);
  lv_obj_set_style_pad_all(card, theme_get_small_padding() + 4, 0);
  lv_obj_set_style_pad_gap(card, theme_get_small_padding(), 0);
  lv_obj_set_style_margin_top(card, theme_get_small_padding(), 0);

  char wallet_line[96];
  snprintf(wallet_line, sizeof(wallet_line), "%s / %s",
           req->wallet[0] ? req->wallet : "Web3",
           req->chain[0] ? req->chain : "EVM");
  web3_add_detail_field(card, scan_tr("menu.wallet", "Wallet"), wallet_line,
                        true);
  web3_add_detail_field(card, scan_tr("sign.path", "Path"),
                        req->path[0] ? req->path : WEB3_DEFAULT_DERIVATION_PATH,
                        true);
  web3_add_detail_field(card, scan_tr("sign.address", "Address"),
                        req->address[0] ? req->address : "-", true);

  (void)ui_create_back_button(root, web3_pin_back_cb);
}

static void web3_source_back_cb(lv_event_t *e) {
  (void)e;
  web3_request_clear(&pending_web3_request);
  pending_web3_source = WEB3_SIGN_SOURCE_NONE;
  web3_summary_back_to_choice = false;
  pending_web3_from_ur = false;
  if (return_callback)
    return_callback();
}

static void web3_source_satochip_cb(lv_event_t *e) {
  (void)e;
  pending_web3_source = WEB3_SIGN_SOURCE_SMARTCARD;
  pending_web3_from_ur = false;
  web3_show_request_summary_for_source(WEB3_SIGN_SOURCE_SMARTCARD, true);
}

static void web3_source_mnemonic_cb(lv_event_t *e) {
  (void)e;
  pending_web3_source = WEB3_SIGN_SOURCE_MNEMONIC;
  pending_web3_from_ur = false;
  web3_show_request_summary_for_source(WEB3_SIGN_SOURCE_MNEMONIC, true);
}

static void add_web3_source_button(lv_obj_t *parent, const char *label,
                                   lv_event_cb_t cb) {
  lv_obj_t *btn = lv_btn_create(parent);
  bool wide = theme_get_screen_width() >= 420;
  lv_obj_set_size(btn, wide ? LV_PCT(46) : LV_PCT(100),
                  wide ? 96 : max_i(72, theme_get_min_touch_size()));
  theme_apply_touch_button(btn, false);
  lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *text = lv_label_create(btn);
  lv_label_set_text(text, label);
  lv_obj_center(text);
  theme_apply_button_label(text, false);
}

static void web3_show_source_choice(void) {
  pending_web3_source = WEB3_SIGN_SOURCE_NONE;
  web3_summary_back_to_choice = false;
  lv_obj_t *root = web3_prepare_page();

  (void)web3_create_fixed_title(root,
                                scan_tr("scan.signing_method",
                                        "Signing method"));

  lv_obj_t *row = lv_obj_create(root);
  lv_obj_set_width(row, LV_PCT(100));
  lv_obj_set_height(row, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_style_pad_gap(row, theme_get_small_padding(), 0);
  lv_obj_set_style_margin_top(row, theme_get_small_padding(), 0);

  add_web3_source_button(row, scan_tr("menu.mnemonic", "Mnemonic"),
                         web3_source_mnemonic_cb);
  add_web3_source_button(row, scan_tr("sign.smartcard", "Smartcard"),
                         web3_source_satochip_cb);

  (void)ui_create_back_button(root, web3_source_back_cb);
}

static void web3_show_request_summary(void) {
  if (pending_web3_source == WEB3_SIGN_SOURCE_NONE) {
    web3_show_source_choice();
    return;
  }
  web3_show_request_summary_for_source(pending_web3_source, true);
}

static void web3_summary_back_cb(lv_event_t *e) {
  (void)e;
  if (web3_summary_back_to_choice) {
    pending_web3_source = WEB3_SIGN_SOURCE_NONE;
    web3_show_source_choice();
    return;
  }

  web3_request_clear(&pending_web3_request);
  pending_web3_source = WEB3_SIGN_SOURCE_NONE;
  pending_web3_from_ur = false;
  if (return_callback)
    return_callback();
}

static void web3_summary_sign_cb(lv_event_t *e) {
  (void)e;
  if (pending_web3_source == WEB3_SIGN_SOURCE_SMARTCARD) {
    web3_pin_sign_after_summary_cb(NULL);
    return;
  }
  if (pending_web3_source == WEB3_SIGN_SOURCE_MNEMONIC) {
    if (web3_sign_progress_dialog) {
      lv_obj_del(web3_sign_progress_dialog);
      web3_sign_progress_dialog = NULL;
    }
    web3_sign_progress_dialog =
        dialog_show_progress(scan_tr("scan.mnemonic_signing",
                                     "Mnemonic signing"),
                             scan_tr("sign.signing", "Signing"),
                             DIALOG_STYLE_OVERLAY);
    lv_timer_t *t = lv_timer_create(web3_mnemonic_sign_cb, 50, NULL);
    lv_timer_set_repeat_count(t, 1);
    return;
  }

  web3_show_source_choice();
}

static void web3_show_request_summary_for_source(web3_sign_source_t source,
                                                 bool back_to_choice) {
  if (!pending_web3_request.valid) {
    web3_show_source_choice();
    return;
  }

  pending_web3_source = source;
  web3_summary_back_to_choice = back_to_choice;

  lv_obj_t *root = web3_prepare_page();
  (void)web3_create_fixed_title(root,
                                scan_tr("scan.confirm_signature",
                                        "Confirm signature"));

  lv_obj_t *card = lv_obj_create(root);
  lv_obj_set_width(card, LV_PCT(100));
  lv_obj_set_flex_grow(card, 1);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);
  lv_obj_set_scroll_dir(card, LV_DIR_VER);
  lv_obj_add_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  theme_apply_frame(card);
  lv_obj_set_style_pad_all(card, theme_get_small_padding() + 6, 0);
  lv_obj_set_style_pad_gap(card, theme_get_small_padding() + 2, 0);
  lv_obj_set_style_pad_top(card, theme_get_small_padding() + 6, 0);
  lv_obj_set_style_pad_bottom(card,
                              theme_get_default_padding() +
                                  theme_get_small_padding(),
                              0);
  lv_obj_set_style_margin_top(card, theme_get_small_padding(), 0);

  web3_add_detail_field(card, scan_tr("menu.wallet", "Wallet"),
                        pending_web3_request.wallet[0]
                            ? pending_web3_request.wallet
                            : "Web3",
                        true);
  web3_add_transaction_details(card, &pending_web3_request);
  web3_add_typed_data_details(card, &pending_web3_request);

  if (pending_web3_request.request_type_id == WEB3_REQUEST_TYPE_TYPED_DATA) {
    web3_add_full_sign_data_details(card, &pending_web3_request);
  }

  web3_add_detail_field(card, scan_tr("sign.path", "Path"),
                        pending_web3_request.path[0] ? pending_web3_request.path
                                                      : WEB3_DEFAULT_DERIVATION_PATH,
                        true);
  web3_add_detail_field(card, scan_tr("sign.address", "Address"),
                        pending_web3_request.address[0]
                            ? pending_web3_request.address
                            : "-",
                        true);
  if (pending_web3_request.request_id[0])
    web3_add_detail_field(card, scan_tr("scan.request_id", "Request ID"),
                          pending_web3_request.request_id, true);

  if (pending_web3_request.request_type_id == WEB3_REQUEST_TYPE_PERSONAL) {
    char len_text[32];
    snprintf(len_text, sizeof(len_text),
             scan_tr("scan.bytes_format", "%u bytes"),
             (unsigned)pending_web3_request.sign_data_len);
    web3_add_detail_field(card, scan_tr("scan.data_length", "Data length"),
                          len_text, false);
  }

  if (pending_web3_request.request_type_id != WEB3_REQUEST_TYPE_TYPED_DATA)
    web3_add_full_sign_data_details(card, &pending_web3_request);

  if (pending_web3_request.detail[0])
    web3_add_detail_field(card, scan_tr("scan.description", "Description"),
                          pending_web3_request.detail, true);

  lv_obj_t *button_row = lv_obj_create(root);
  lv_obj_set_width(button_row, LV_PCT(100));
  lv_obj_set_height(button_row,
                    theme_get_min_touch_size() + theme_get_small_padding());
  lv_obj_set_flex_flow(button_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(button_row, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_bg_opa(button_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(button_row, 0, 0);
  lv_obj_set_style_pad_all(button_row, 0, 0);
  lv_obj_set_style_pad_gap(button_row, theme_get_small_padding(), 0);
  lv_obj_set_style_margin_top(button_row, theme_get_small_padding(), 0);

  lv_obj_t *sign_btn = lv_btn_create(button_row);
  lv_obj_set_size(sign_btn, LV_PCT(100), theme_get_min_touch_size());
  theme_apply_touch_button(sign_btn, false);
  lv_obj_add_event_cb(sign_btn, web3_summary_sign_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_clear_flag(sign_btn, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_t *sign_label = lv_label_create(sign_btn);
  const char *sign_text =
      source == WEB3_SIGN_SOURCE_SMARTCARD
          ? scan_tr("sign.enter_card_pin", "Enter PIN")
          : scan_tr("sign.sign", "Sign");
  lv_label_set_text(sign_label, sign_text);
  lv_obj_center(sign_label);
  theme_apply_button_label(sign_label, false);

  (void)ui_create_back_button(root, web3_summary_back_cb);
}

static void web3_mnemonic_sign_cb(lv_timer_t *timer) {
  (void)timer;
  if (web3_sign_progress_dialog) {
    lv_obj_del(web3_sign_progress_dialog);
    web3_sign_progress_dialog = NULL;
  }
  if (!web3_sign_with_mnemonic()) {
    web3_show_request_summary();
  }
}

static bool web3_sign_with_mnemonic(void) {
  if (!key_has_signing_key()) {
    dialog_show_error(scan_tr("wallet.no_mnemonic_loaded",
                              "Load a mnemonic first"),
                      web3_show_request_summary, 0);
    return false;
  }

  const char *path = pending_web3_request.path[0]
                         ? pending_web3_request.path
                         : WEB3_DEFAULT_DERIVATION_PATH;

  char derived_address[EVM_ADDRESS_HEX_LEN + 1] = {0};
  if (!custom_derivation_get_address(path, CUSTOM_ADDR_EVM, false,
                                     derived_address,
                                     sizeof(derived_address))) {
    dialog_show_error(scan_tr("scan.mnemonic_address_derive_failed",
                              "Mnemonic address derivation failed"),
                      web3_show_request_summary, 0);
    return false;
  }
  if (pending_web3_request.address[0] &&
      strcasecmp(pending_web3_request.address, derived_address) != 0) {
    char msg[384];
    snprintf(msg, sizeof(msg),
             scan_tr("scan.mnemonic_address_mismatch",
                     "Signing address mismatch. Refused.\nRequest: %s\n"
                     "Mnemonic: %s"),
             pending_web3_request.address, derived_address);
    dialog_show_error(msg, web3_show_request_summary, 0);
    return false;
  }

  uint8_t digest[32];
  char digest_err[192];
  if (!web3_make_digest(&pending_web3_request, digest, digest_err,
                        sizeof(digest_err))) {
    char msg[256];
    snprintf(msg, sizeof(msg),
             scan_tr("scan.sign_data_parse_failed_detail",
                     "Signing data parse failed\n%s"),
             digest_err[0] ? digest_err
                           : scan_tr("scan.check_qr_content",
                                     "Check the QR content"));
    dialog_show_error(msg, web3_show_request_summary, 0);
    return false;
  }

  struct ext_key *derived_key = NULL;
  if (!key_get_derived_key(path, &derived_key) || !derived_key) {
    secure_memzero(digest, sizeof(digest));
    dialog_show_error(scan_tr("scan.mnemonic_path_derive_failed",
                              "Mnemonic path derivation failed"),
                      web3_show_request_summary, 0);
    return false;
  }

  uint8_t recoverable_sig[EC_SIGNATURE_RECOVERABLE_LEN];
  int ret = wally_ec_sig_from_bytes(
      derived_key->priv_key + 1, EC_PRIVATE_KEY_LEN, digest,
      EC_MESSAGE_HASH_LEN, EC_FLAG_ECDSA | EC_FLAG_RECOVERABLE,
      recoverable_sig, sizeof(recoverable_sig));
  secure_memzero(digest, sizeof(digest));
  bip32_key_free(derived_key);

  if (ret != WALLY_OK) {
    secure_memzero(recoverable_sig, sizeof(recoverable_sig));
    dialog_show_error(scan_tr("scan.mnemonic_sign_failed",
                              "Mnemonic signing failed"),
                      web3_show_request_summary, 0);
    return false;
  }

  uint8_t signature[72];
  size_t signature_len = 0;
  if (!web3_compose_signature_bytes(&pending_web3_request, recoverable_sig,
                                    recoverable_sig[64], signature,
                                    &signature_len)) {
    secure_memzero(recoverable_sig, sizeof(recoverable_sig));
    dialog_show_error(scan_tr("scan.signature_result_encode_failed",
                              "Signature result encoding failed"),
                      web3_show_request_summary, 0);
    return false;
  }

  char *response_ur = NULL;
  bool ok = web3_build_eth_signature_ur(&pending_web3_request, signature,
                                        signature_len, &response_ur);
  secure_memzero(signature, sizeof(signature));
  secure_memzero(recoverable_sig, sizeof(recoverable_sig));
  if (!ok || !response_ur) {
    dialog_show_error(scan_tr("scan.signature_result_encode_failed",
                              "Signature result encoding failed"),
                      web3_show_request_summary, 0);
    free(response_ur);
    return false;
  }

  web3_show_sign_result_qr(response_ur);
  return true;
}

static bool handle_web3_relay_content(const char *content) {
  if (!looks_like_web3_relay_content(content))
    return false;

  char wallet[48] = "";
  char action[64] = "";
  char chain[64] = "";
  char path[96] = "";
  char address[72] = "";
  char payload[96] = "";
  long data_type = 0;
  (void)json_string_value(content, "wallet_name", wallet, sizeof(wallet));
  (void)json_string_value(content, "action", action, sizeof(action));
  (void)json_string_value(content, "chain", chain, sizeof(chain));
  (void)json_string_value(content, "address_path", path, sizeof(path));
  if (!path[0])
    (void)json_string_value(content, "path", path, sizeof(path));
  (void)json_string_value(content, "address", address, sizeof(address));
  (void)json_string_value(content, "payload", payload, sizeof(payload));
  (void)json_int_value(content, "request_data_type_id", &data_type);

  const char *kind = scan_tr("scan.evm_sign_request", "EVM sign request");
  if (starts_ci(content, "tp:web3RelayNative-"))
    kind = scan_tr("scan.web3_native_relay", "Web3 native QR");
  else if (starts_ci(content, "ethereum:"))
    kind = scan_tr("scan.ethereum_request", "Ethereum request");
  else if (starts_ci(content, "tp:personalSign-") ||
           contains_ci(content, "personalSign"))
    kind = scan_tr("sign.message_signature", "Message signature");
  else if (starts_ci(content, "tp:signTransaction-") ||
           contains_ci(content, "signTransaction"))
    kind = scan_tr("scan.transaction_signing", "Transaction signing");
  else if (contains_ci(content, "signTypedData") ||
           contains_ci(content, "signTypeData"))
    kind = scan_tr("scan.typed_data_signature", "TypedData signature");

  char target[96];
  short_middle(target, sizeof(target), address[0] ? address : path,
               address[0] ? 8 : 16, address[0] ? 6 : 12);
  char message[256];
  snprintf(message, sizeof(message),
           scan_tr("scan.web3_relay_info_format",
                   "Wallet: %s\nType: %s\nChain: %s\n%s%s"),
           wallet[0] ? wallet : "Web3",
           action[0] ? action : kind,
           chain[0] ? chain : "-",
           target[0] ? target : "-",
           (smartcard_web3_mode || unified_scan_mode)
               ? ""
               : scan_tr("scan.use_smartcard_to_sign",
                         "\nUse the smartcard to sign"));

  qr_scanner_page_hide();
  qr_scanner_page_destroy();
  if (smartcard_web3_mode || unified_scan_mode) {
    if (!web3_parse_sign_request(content, &pending_web3_request)) {
      char msg[256];
      snprintf(msg, sizeof(msg),
               scan_tr("scan.cannot_sign_detail", "Cannot sign\n%s"),
               pending_web3_request.detail[0] ? pending_web3_request.detail
                                               : scan_tr("scan.qr_missing_sign_data",
                                                         "QR is missing signable "
                                                         "data"));
      web3_request_clear(&pending_web3_request);
      dialog_show_error(msg, return_callback, 0);
      return true;
    }
    web3_show_source_choice();
    return true;
  }

  web3_info_active = true;
  dialog_show_info(smartcard_web3_mode
                       ? scan_tr("scan.smartcard_web3", "Smartcard Web3")
                       : scan_tr("scan.signing_not_supported",
                                 "Signing not supported"),
                   message, web3_relay_info_done_cb, NULL,
                   DIALOG_STYLE_FULLSCREEN);
  return true;
}

// --- Main scanner callback with two-layer detection ---

static void return_from_qr_scanner_cb(void) {
  int detected_format = qr_scanner_get_format();

  char *qr_content = NULL;
  size_t qr_content_len = 0;
  bool parse_success = false;

  if (detected_format == FORMAT_UR) {
    const char *ur_type = NULL;
    const uint8_t *cbor_data = NULL;
    size_t cbor_len = 0;

    if (qr_scanner_get_ur_result(&ur_type, &cbor_data, &cbor_len)) {
      // Layer 1: UR type hints
      if (ur_type && strcmp(ur_type, "crypto-psbt") == 0) {
        // PSBT via UR
        psbt_data_t *psbt_data = psbt_from_cbor(cbor_data, cbor_len);
        if (psbt_data) {
          size_t psbt_len;
          const uint8_t *psbt_bytes = psbt_get_data(psbt_data, &psbt_len);
          if (psbt_bytes) {
            cleanup_psbt_data();
            parse_success = (wally_psbt_from_bytes(psbt_bytes, psbt_len, 0,
                                                   &current_psbt) == WALLY_OK);
          }
          psbt_free(psbt_data);
        }
      } else if (ur_type && (strcmp(ur_type, "crypto-output") == 0 ||
                             strcmp(ur_type, "crypto-account") == 0)) {
        // Descriptor via UR — extract before destroying scanner
        char *desc = descriptor_extract_from_scanner();
        qr_scanner_page_hide();
        qr_scanner_page_destroy();
        if (desc) {
          handle_descriptor_content(desc);
          free(desc);
        } else {
          dialog_show_error(scan_tr("scan.descriptor_parse_failed",
                                    "Descriptor parse failed"),
                            return_callback, 0);
        }
        return;
      } else if (ur_type && strcmp(ur_type, "bytes") == 0) {
        // UR bytes: decode to string, fall through to Layer 2
        bytes_data_t *bytes = bytes_from_cbor(cbor_data, cbor_len);
        if (bytes) {
          size_t len = 0;
          const uint8_t *data = bytes_get_data(bytes, &len);
          if (data && len > 0) {
            qr_content = web3_strndup((const char *)data, len);
            qr_content_len = len;
          }
          bytes_free(bytes);
        }
      } else if (ur_type && strcmp(ur_type, "eth-sign-request") == 0) {
        if (smartcard_web3_mode || unified_scan_mode) {
          if (!web3_parse_eth_sign_request_cbor(cbor_data, cbor_len,
                                                &pending_web3_request)) {
            qr_scanner_page_hide();
            qr_scanner_page_destroy();
            dialog_show_error(scan_tr("scan.web3_ur_parse_failed",
                                      "Web3 UR request parse failed"),
                              return_callback, 0);
            return;
          }
          qr_scanner_page_hide();
          qr_scanner_page_destroy();
          web3_show_source_choice();
          return;
        }
        qr_scanner_page_hide();
        qr_scanner_page_destroy();
        dialog_show_error(scan_tr("scan.use_unified_scan_for_web3",
                                  "Use the unified scan entry for Web3 "
                                  "signing"),
                          return_callback, 0);
        return;
      }
    }
  } else if (detected_format == FORMAT_BBQR) {
    // BBQr returns raw binary PSBT data
    qr_content = qr_scanner_get_completed_content_with_len(&qr_content_len);
    if (qr_content && qr_content_len > 0) {
      cleanup_psbt_data();
      parse_success =
          (wally_psbt_from_bytes((const uint8_t *)qr_content, qr_content_len, 0,
                                 &current_psbt) == WALLY_OK);
      free(qr_content);
      qr_content = NULL;
    }
  } else {
    // Other formats (PMOFN, NONE) — get content with length for binary formats
    qr_content = qr_scanner_get_completed_content_with_len(&qr_content_len);
  }

  // Layer 2: plaintext/binary heuristics — try each parser in priority order
  if (!parse_success && qr_content) {
    // 1. Message
    if (message_sign_parse(qr_content, &current_message)) {
      is_message_sign = true;
      parse_success = true;
    }

    // 1b. Web3 relay envelopes from satochip-signer / mobile relay.
    if (!parse_success && handle_web3_relay_content(qr_content)) {
      free(qr_content);
      return;
    }

    // 2. PSBT (base64)
    if (!parse_success) {
      parse_success = parse_and_display_psbt(qr_content);
    }

    // 3. Descriptor
    if (!parse_success && (is_descriptor_prefix(qr_content) ||
                           is_bluewallet_descriptor(qr_content))) {
      qr_scanner_page_hide();
      qr_scanner_page_destroy();
      handle_descriptor_content(qr_content);
      free(qr_content);
      return;
    }

    // 4. Address
    if (!parse_success && is_valid_address(qr_content)) {
      qr_scanner_page_hide();
      qr_scanner_page_destroy();
      handle_address_content(qr_content);
      free(qr_content);
      return;
    }

    // 5. Mnemonic
    if (!parse_success) {
      char *mnemonic =
          mnemonic_qr_to_mnemonic_unchecked(qr_content, qr_content_len, NULL);
      if (mnemonic) {
        SECURE_FREE_STRING(mnemonic);
        qr_scanner_page_hide();
        qr_scanner_page_destroy();
        handle_mnemonic_content(qr_content, qr_content_len);
        free(qr_content);
        return;
      }
      SECURE_FREE_STRING(mnemonic);
    }

    free(qr_content);
    qr_content = NULL;
  }

  qr_scanner_page_hide();
  qr_scanner_page_destroy();

  if (parse_success) {
    scanned_qr_format = detected_format;
    if (parsed_psbt_preferred_export_format >= 0)
      scanned_qr_format = parsed_psbt_preferred_export_format;

    if (current_psbt && unified_scan_mode) {
      show_btc_source_choice();
      return;
    }

    if (!key_is_loaded()) {
      cleanup_psbt_data();
      message_sign_free_parsed(&current_message);
      is_message_sign = false;
      dialog_show_error(scan_tr("wallet.no_mnemonic_loaded",
                                "Load a mnemonic first"),
                        return_callback, 1800);
      return;
    }
    pending_btc_source = BTC_SIGN_SOURCE_MNEMONIC;
    if (mnemonic_slots_count() > 1) {
      mnemonic_slots_page_create(lv_screen_active(), return_from_sign_slots_cb,
                                 show_scanned_sign_payload);
      mnemonic_slots_page_show();
    } else {
      show_scanned_sign_payload();
    }
  } else {
    dialog_show_error(scan_tr("sign.unsupported_qr",
                              "Unsupported QR format"),
                      return_callback, 0);
  }
}

static void return_from_sign_slots_cb(void) {
  mnemonic_slots_page_destroy();
  if (return_callback)
    return_callback();
}

static lv_obj_t *btc_prepare_page(void) {
  qr_scanner_page_hide();
  qr_scanner_page_destroy();
  if (!scan_screen)
    scan_screen = theme_create_page_container(lv_screen_active());
  lv_obj_move_foreground(scan_screen);
  lv_obj_clean(scan_screen);
  theme_apply_screen(scan_screen);
  lv_obj_clear_flag(scan_screen, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_flex_flow(scan_screen, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(scan_screen, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(scan_screen, theme_get_default_padding(), 0);
  lv_obj_set_style_pad_gap(scan_screen, theme_get_default_padding(), 0);
  return scan_screen;
}

static lv_obj_t *create_scan_review_scroll_container(void) {
  if (!scan_screen)
    return NULL;

  lv_obj_t *container = lv_obj_create(scan_screen);
  lv_obj_set_width(container, LV_PCT(100));
  lv_obj_set_height(container, 0);
  lv_obj_set_flex_grow(container, 1);
  lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_top(container, 10, 0);
  lv_obj_set_style_pad_left(container, 10, 0);
  lv_obj_set_style_pad_right(container, 10, 0);
  lv_obj_set_style_pad_bottom(container, 10 + theme_get_min_touch_size(), 0);
  lv_obj_set_style_pad_gap(container, 10, 0);
  theme_apply_screen(container);
  lv_obj_add_flag(container, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(container, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_AUTO);
  return container;
}

static void btc_source_back_cb(lv_event_t *e) {
  (void)e;
  pending_btc_source = BTC_SIGN_SOURCE_NONE;
  cleanup_psbt_data();
  message_sign_free_parsed(&current_message);
  is_message_sign = false;
  if (return_callback)
    return_callback();
}

static void btc_source_mnemonic_cb(lv_event_t *e) {
  (void)e;
  pending_btc_source = BTC_SIGN_SOURCE_MNEMONIC;

  if (!key_is_loaded()) {
    dialog_show_error(scan_tr("wallet.no_mnemonic_loaded",
                              "Load a mnemonic first"),
                      return_callback, 1800);
    return;
  }

  if (mnemonic_slots_count() > 1) {
    mnemonic_slots_page_create(lv_screen_active(), return_from_sign_slots_cb,
                               show_scanned_sign_payload);
    mnemonic_slots_page_show();
  } else {
    show_scanned_sign_payload();
  }
}

static void btc_source_satochip_cb(lv_event_t *e) {
  (void)e;
  pending_btc_source = BTC_SIGN_SOURCE_SMARTCARD;
  show_scanned_sign_payload();
}

static void show_btc_source_choice(void) {
  pending_btc_source = BTC_SIGN_SOURCE_NONE;
  lv_obj_t *root = btc_prepare_page();

  (void)web3_create_fixed_title(root,
                                scan_tr("scan.signing_method",
                                        "Signing method"));

  lv_obj_t *row = lv_obj_create(root);
  lv_obj_set_width(row, LV_PCT(100));
  lv_obj_set_height(row, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_style_pad_gap(row, theme_get_default_padding(), 0);
  lv_obj_set_style_margin_top(row, theme_get_small_padding(), 0);

  lv_obj_t *mnemonic_btn = lv_btn_create(row);
  lv_obj_set_size(mnemonic_btn, LV_PCT(48), 96);
  theme_apply_touch_button(mnemonic_btn, true);
  lv_obj_add_event_cb(mnemonic_btn, btc_source_mnemonic_cb, LV_EVENT_CLICKED,
                      NULL);
  lv_obj_t *mnemonic_label = lv_label_create(mnemonic_btn);
  lv_label_set_text(mnemonic_label, scan_tr("menu.mnemonic", "Mnemonic"));
  theme_apply_button_label(mnemonic_label, true);
  lv_obj_center(mnemonic_label);

  lv_obj_t *card_btn = lv_btn_create(row);
  lv_obj_set_size(card_btn, LV_PCT(48), 96);
  theme_apply_touch_button(card_btn, true);
  lv_obj_add_event_cb(card_btn, btc_source_satochip_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *card_label = lv_label_create(card_btn);
  lv_label_set_text(card_label, scan_tr("sign.smartcard", "Smartcard"));
  theme_apply_button_label(card_label, true);
  lv_obj_center(card_label);

  (void)ui_create_back_button(root, btc_source_back_cb);
}

static void show_scanned_sign_payload(void) {
  mnemonic_slots_page_destroy();

  if (is_message_sign) {
    create_message_sign_display();
    return;
  }

  if (!current_psbt) {
    dialog_show_error(scan_tr("sign.psbt_invalid", "Invalid transaction data"),
                      return_callback, 0);
    return;
  }

  if (psbt_is_multisig(current_psbt) && !wallet_has_descriptor()) {
    show_multisig_options_menu();
    return;
  }

  lv_obj_t *root = btc_prepare_page();
  (void)web3_create_fixed_title(root,
                                scan_tr("scan.confirm_signature",
                                        "Confirm signature"));
  (void)ui_create_back_button(root, back_button_cb);

  if (!create_psbt_info_display())
    dialog_show_error(scan_tr("sign.psbt_invalid", "Invalid transaction data"),
                      return_callback, 0);
}

// --- Descriptor handler ---

static void descriptor_loaded_info_cb(void *user_data) {
  (void)user_data;
  if (return_callback)
    return_callback();
}

static void scan_descriptor_validation_cb(descriptor_validation_result_t result,
                                          void *user_data) {
  (void)user_data;

  if (result == VALIDATION_SUCCESS) {
    dialog_show_info(scan_tr("sign.descriptor_loaded", "Descriptor loaded"),
                     scan_tr("scan.wallet_descriptor_updated",
                             "Wallet descriptor updated"),
                     descriptor_loaded_info_cb, NULL, DIALOG_STYLE_FULLSCREEN);
    return;
  }

  descriptor_loader_show_error(result);
  if (return_callback)
    return_callback();
}

static void handle_descriptor_content(const char *descriptor_str) {
  descriptor_loader_process_string(descriptor_str,
                                   scan_descriptor_validation_cb, NULL);
}

// --- Address handler ---

static void address_found_cb(void) {
  address_checker_destroy();
  if (return_callback)
    return_callback();
}

static void address_not_found_cb(void) {
  address_checker_destroy();
  if (return_callback)
    return_callback();
}

static void handle_address_content(const char *content) {
  // For multisig without descriptor, we can't verify addresses
  if (wallet_get_policy() == WALLET_POLICY_MULTISIG &&
      !wallet_has_descriptor()) {
    dialog_show_error(scan_tr("address.multisig_descriptor_required",
                              "Load a descriptor before checking multisig "
                              "addresses"),
                      return_callback, 0);
    return;
  }

  address_checker_check(content, address_found_cb, address_not_found_cb);
}

// --- Mnemonic handler ---

static void mnemonic_confirm_cb(bool confirmed, void *user_data) {
  (void)user_data;

  if (!confirmed || !scanned_mnemonic) {
    SECURE_FREE_STRING(scanned_mnemonic);
    if (return_callback)
      return_callback();
    return;
  }

  wallet_network_t net = wallet_get_network();
  char *passphrase = NULL;
  (void)key_get_session_passphrase(&passphrase);

  if (!key_load_from_mnemonic(scanned_mnemonic, passphrase,
                              net == WALLET_NETWORK_TESTNET)) {
    SECURE_FREE_STRING(passphrase);
    SECURE_FREE_STRING(scanned_mnemonic);
    dialog_show_error(scan_tr("wallet.read_mnemonic_failed",
                              "Mnemonic load failed"),
                      return_callback, 0);
    return;
  }
  SECURE_FREE_STRING(passphrase);

  wallet_cleanup();
  if (!wallet_init(net)) {
    SECURE_FREE_STRING(scanned_mnemonic);
    dialog_show_error(scan_tr("settings.wallet_init_failed",
                              "Wallet initialization failed"),
                      return_callback, 0);
    return;
  }

  (void)mnemonic_slots_add_current(NULL);
  SECURE_FREE_STRING(scanned_mnemonic);

  // Return to home — it will recreate with new key info
  if (return_callback)
    return_callback();
}

static void mnemonic_intermediate_info_cb(void *user_data) {
  (void)user_data;
  if (return_callback)
    return_callback();
}

static void handle_mnemonic_content(const char *data, size_t len) {
  char *mnemonic = mnemonic_qr_to_mnemonic_unchecked(data, len, NULL);
  if (!mnemonic) {
    SECURE_FREE_STRING(mnemonic);
    dialog_show_error(scan_tr("scan.invalid_mnemonic",
                              "Invalid mnemonic"),
                      return_callback, 0);
    return;
  }

  if (bip39_mnemonic_validate(NULL, mnemonic) != WALLY_OK) {
    if (!key_load_from_mnemonic_unchecked(mnemonic)) {
      SECURE_FREE_STRING(mnemonic);
      dialog_show_error(scan_tr("scan.temporary_mnemonic_import_failed",
                                "Temporary mnemonic import failed"),
                        return_callback, 0);
      return;
    }
    wallet_cleanup();
    SECURE_FREE_STRING(mnemonic);
    dialog_show_info(scan_tr("scan.temporary_mnemonic_imported",
                             "Temporary mnemonic imported"),
                     scan_tr("scan.temporary_mnemonic_notice",
                             "It can only be used for mnemonic conversion and "
                             "recovery, not signing or backup."),
                     mnemonic_intermediate_info_cb, NULL,
                     DIALOG_STYLE_FULLSCREEN);
    return;
  }

  // Get current fingerprint
  char current_fp[9] = "--------";
  key_get_fingerprint_hex(current_fp);

  // Compute new mnemonic's fingerprint without touching the loaded key
  wallet_network_t net = wallet_get_network();
  bool is_test = (net == WALLET_NETWORK_TESTNET);

  char new_fp[9] = "--------";
  {
    unsigned char seed[BIP39_SEED_LEN_512];
    size_t seed_len = 0;
    if (bip39_mnemonic_to_seed(mnemonic, NULL, seed, sizeof(seed), &seed_len) ==
        WALLY_OK) {
      uint32_t ver = is_test ? BIP32_VER_TEST_PRIVATE : BIP32_VER_MAIN_PRIVATE;
      struct ext_key *tmp_key = NULL;
      if (bip32_key_from_seed_alloc(seed, seed_len, ver, 0, &tmp_key) ==
          WALLY_OK) {
        unsigned char fp[BIP32_KEY_FINGERPRINT_LEN];
        if (bip32_key_get_fingerprint(tmp_key, fp, BIP32_KEY_FINGERPRINT_LEN) ==
            WALLY_OK) {
          for (int i = 0; i < BIP32_KEY_FINGERPRINT_LEN; i++)
            snprintf(new_fp + (i * 2), 3, "%02x", fp[i]);
          new_fp[BIP32_KEY_FINGERPRINT_LEN * 2] = '\0';
        }
        bip32_key_free(tmp_key);
      }
      secure_memzero(seed, sizeof(seed));
    }
  }

  // Store mnemonic for confirmation callback
  scanned_mnemonic = mnemonic;

  char msg[256];
  snprintf(
      msg, sizeof(msg),
      scan_tr("scan.replace_current_key_confirm",
              "Replace the current key?\n\n"
      "  %s > #%06X %s#\n\n"
              "The passphrase and descriptor will be cleared."),
      current_fp,
      (unsigned)((lv_color_to_32(highlight_color(), LV_OPA_COVER).red << 16) |
                 (lv_color_to_32(highlight_color(), LV_OPA_COVER).green << 8) |
                 lv_color_to_32(highlight_color(), LV_OPA_COVER).blue),
      new_fp);

  dialog_show_confirm(msg, mnemonic_confirm_cb, NULL, DIALOG_STYLE_FULLSCREEN);
}

// --- PSBT handling (unchanged from sign.c) ---

static bool psbt_magic_matches(const uint8_t *data, size_t len) {
  return data && len >= 5 && data[0] == 'p' && data[1] == 's' &&
         data[2] == 'b' && data[3] == 't' && data[4] == 0xff;
}

static char *psbt_compact_copy(const char *src, size_t len, bool uppercase) {
  if (!src)
    return NULL;

  const char *start = src;
  const char *end = src + len;
  while (start < end && isspace((unsigned char)*start))
    start++;
  while (end > start && isspace((unsigned char)end[-1]))
    end--;
  if (end > start + 1 &&
      ((*start == '"' && end[-1] == '"') ||
       (*start == '\'' && end[-1] == '\''))) {
    start++;
    end--;
  }

  char *out = malloc((size_t)(end - start) + 1);
  if (!out)
    return NULL;

  size_t pos = 0;
  for (const char *p = start; p < end; p++) {
    if (isspace((unsigned char)*p))
      continue;
    out[pos++] = uppercase ? (char)toupper((unsigned char)*p) : *p;
  }
  out[pos] = '\0';
  return out;
}

static bool psbt_decode_base64_alloc(const char *candidate, uint8_t **out,
                                     size_t *out_len) {
  if (!candidate || !out || !out_len)
    return false;
  *out = NULL;
  *out_len = 0;

  char *compact = psbt_compact_copy(candidate, strlen(candidate), false);
  if (!compact || !compact[0]) {
    free(compact);
    return false;
  }

  size_t compact_len = strlen(compact);
  size_t pad = (4U - (compact_len % 4U)) % 4U;
  if ((compact_len % 4U) == 1U) {
    free(compact);
    return false;
  }

  char *padded = malloc(compact_len + pad + 1U);
  if (!padded) {
    free(compact);
    return false;
  }
  for (size_t i = 0; i < compact_len; i++) {
    char ch = compact[i];
    padded[i] = (ch == '-') ? '+' : (ch == '_' ? '/' : ch);
  }
  for (size_t i = 0; i < pad; i++)
    padded[compact_len + i] = '=';
  padded[compact_len + pad] = '\0';
  free(compact);

  size_t max_len = 0;
  if (wally_base64_get_maximum_length(padded, 0, &max_len) != WALLY_OK ||
      max_len == 0) {
    free(padded);
    return false;
  }
  uint8_t *buf = malloc(max_len);
  if (!buf) {
    free(padded);
    return false;
  }

  size_t written = 0;
  int ret = wally_base64_to_bytes(padded, 0, buf, max_len, &written);
  free(padded);
  if (ret != WALLY_OK || !psbt_magic_matches(buf, written)) {
    secure_memzero(buf, max_len);
    free(buf);
    return false;
  }

  *out = buf;
  *out_len = written;
  return true;
}

static bool psbt_decode_hex_alloc(const char *candidate, uint8_t **out,
                                  size_t *out_len) {
  if (!candidate || !out || !out_len)
    return false;
  *out = NULL;
  *out_len = 0;

  char *compact = psbt_compact_copy(candidate, strlen(candidate), false);
  if (!compact)
    return false;
  char *hex = compact;
  if (starts_ci(hex, "0x"))
    hex += 2;
  size_t hex_len = strlen(hex);
  if (hex_len < 10 || (hex_len % 2U) != 0 ||
      strncasecmp(hex, "70736274ff", 10) != 0) {
    free(compact);
    return false;
  }

  uint8_t *buf = malloc(hex_len / 2U);
  if (!buf) {
    free(compact);
    return false;
  }
  size_t written = 0;
  int ret = wally_hex_to_bytes(hex, buf, hex_len / 2U, &written);
  free(compact);
  if (ret != WALLY_OK || !psbt_magic_matches(buf, written)) {
    secure_memzero(buf, hex_len / 2U);
    free(buf);
    return false;
  }

  *out = buf;
  *out_len = written;
  return true;
}

static bool psbt_decode_base43_alloc(const char *candidate, uint8_t **out,
                                     size_t *out_len) {
  if (!candidate || !out || !out_len)
    return false;
  *out = NULL;
  *out_len = 0;

  char *compact = psbt_compact_copy(candidate, strlen(candidate), true);
  if (!compact || !compact[0]) {
    free(compact);
    return false;
  }

  uint8_t *buf = NULL;
  size_t written = 0;
  bool ok = base43_decode(compact, strlen(compact), &buf, &written) &&
            psbt_magic_matches(buf, written);
  free(compact);
  if (!ok) {
    free(buf);
    return false;
  }

  *out = buf;
  *out_len = written;
  return true;
}

static bool psbt_load_bytes(const uint8_t *bytes, size_t len,
                            int preferred_export_format) {
  if (!psbt_magic_matches(bytes, len))
    return false;

  struct wally_psbt *parsed = NULL;
  if (wally_psbt_from_bytes(bytes, len, 0, &parsed) != WALLY_OK || !parsed)
    return false;

  char *encoded = NULL;
  if (wally_base64_from_bytes(bytes, len, 0, &encoded) != WALLY_OK ||
      !encoded) {
    wally_psbt_free(parsed);
    return false;
  }

  cleanup_psbt_data();
  current_psbt = parsed;
  psbt_base64 = strdup(encoded);
  wally_free_string(encoded);
  if (!psbt_base64) {
    cleanup_psbt_data();
    return false;
  }
  parsed_psbt_preferred_export_format = preferred_export_format;
  return true;
}

static bool parse_psbt_candidate(const char *candidate,
                                 int preferred_export_format) {
  if (!candidate || !candidate[0])
    return false;

  uint8_t *bytes = NULL;
  size_t len = 0;
  if (psbt_decode_base64_alloc(candidate, &bytes, &len) ||
      psbt_decode_hex_alloc(candidate, &bytes, &len)) {
    bool ok = psbt_load_bytes(bytes, len, preferred_export_format);
    secure_memzero(bytes, len);
    free(bytes);
    return ok;
  }

  if (psbt_decode_base43_alloc(candidate, &bytes, &len)) {
    bool ok = psbt_load_bytes(bytes, len, FORMAT_ELECTRUM);
    secure_memzero(bytes, len);
    free(bytes);
    return ok;
  }

  return false;
}

static char *url_decode_preserve_plus_alloc(const char *input,
                                            size_t input_len) {
  if (!input)
    return NULL;
  char *out = malloc(input_len + 1);
  if (!out)
    return NULL;

  size_t pos = 0;
  for (size_t i = 0; i < input_len; i++) {
    char c = input[i];
    if (c == '%' && i + 2 < input_len) {
      int hi = web3_hex_value(input[i + 1]);
      int lo = web3_hex_value(input[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out[pos++] = (char)((hi << 4) | lo);
        i += 2;
        continue;
      }
    }
    out[pos++] = c;
  }
  out[pos] = '\0';
  return out;
}

static bool parse_psbt_query_candidate(const char *query, const char *key,
                                       int preferred_export_format) {
  if (!query || !key)
    return false;

  size_t key_len = strlen(key);
  const char *p = query;
  while (*p) {
    const char *part_end = strchr(p, '&');
    if (!part_end)
      part_end = p + strlen(p);
    const char *eq = memchr(p, '=', (size_t)(part_end - p));
    if (eq && (size_t)(eq - p) == key_len &&
        strncasecmp(p, key, key_len) == 0) {
      char *decoded =
          url_decode_preserve_plus_alloc(eq + 1, (size_t)(part_end - eq - 1));
      if (!decoded)
        return false;
      bool ok = parse_psbt_candidate(decoded, preferred_export_format);
      free(decoded);
      return ok;
    }
    p = (*part_end == '&') ? part_end + 1 : part_end;
  }
  return false;
}

static bool parse_psbt_uri_candidates(const char *content) {
  if (!content)
    return false;

  const char *query = strchr(content, '?');
  if (!query)
    return false;
  query++;

  return parse_psbt_query_candidate(query, "psbt", FORMAT_ELECTRUM) ||
         parse_psbt_query_candidate(query, "data", FORMAT_ELECTRUM) ||
         parse_psbt_query_candidate(query, "tx", FORMAT_ELECTRUM) ||
         parse_psbt_query_candidate(query, "transaction", FORMAT_ELECTRUM);
}

static bool parse_and_display_psbt(const char *base64_data) {
  if (!base64_data) {
    return false;
  }

  if (parse_psbt_candidate(base64_data, -1))
    return true;

  if (starts_ci(base64_data, "electrum:")) {
    if (parse_psbt_candidate(base64_data + strlen("electrum:"),
                             FORMAT_ELECTRUM))
      return true;
  } else if (starts_ci(base64_data, "psbt:")) {
    if (parse_psbt_candidate(base64_data + strlen("psbt:"),
                             FORMAT_ELECTRUM))
      return true;
  } else if (starts_ci(base64_data, "bitcoin:")) {
    if (parse_psbt_uri_candidates(base64_data))
      return true;
    const char *body = base64_data + strlen("bitcoin:");
    if (body[0] && body[0] != '?' &&
        parse_psbt_candidate(body, FORMAT_ELECTRUM))
      return true;
  }

  return parse_psbt_uri_candidates(base64_data);
}

static void mismatch_dialog_cb(void *user_data) {
  cleanup_psbt_data();
  if (return_callback) {
    return_callback();
  }
}

static bool check_psbt_mismatch(void) {
  if (!current_psbt) {
    return false;
  }

  is_testnet = psbt_detect_network(current_psbt);
  int32_t psbt_account = psbt_detect_account(current_psbt);

  wallet_network_t wallet_net = wallet_get_network();
  bool wallet_is_testnet = (wallet_net == WALLET_NETWORK_TESTNET);
  uint32_t wallet_account = wallet_get_account();

  bool network_mismatch = (is_testnet != wallet_is_testnet);
  bool account_mismatch =
      (psbt_account >= 0 && (uint32_t)psbt_account != wallet_account);

  if (!network_mismatch && !account_mismatch) {
    return false;
  }

  char message[256];
  int offset = 0;
  offset += snprintf(
      message + offset, sizeof(message) - offset,
      "%s",
      scan_tr("scan.settings_mismatch_notice",
              "This transaction needs different settings to identify change "
              "correctly:\n\n"));

  if (network_mismatch) {
    offset += snprintf(message + offset, sizeof(message) - offset,
                       scan_tr("scan.network_change_format",
                               "  Network: %s -> %s\n"),
                       wallet_is_testnet
                           ? scan_tr("settings.testnet", "Testnet")
                           : scan_tr("settings.mainnet", "Mainnet"),
                       is_testnet ? scan_tr("settings.testnet", "Testnet")
                                  : scan_tr("settings.mainnet", "Mainnet"));
  }

  if (account_mismatch) {
    offset += snprintf(message + offset, sizeof(message) - offset,
                       scan_tr("scan.account_change_format",
                               "  Account: %u -> %d\n"),
                       wallet_account, psbt_account);
  }

  snprintf(message + offset, sizeof(message) - offset,
           "%s",
           scan_tr("scan.update_settings_before_signing",
                   "\nUpdate settings first, then sign."));

  dialog_show_info(scan_tr("scan.settings_mismatch", "Settings mismatch"),
                   message, mismatch_dialog_cb, NULL,
                   DIALOG_STYLE_FULLSCREEN);

  return true;
}

static bool create_psbt_info_display(void) {
  if (!scan_screen || !current_psbt) {
    return false;
  }

  if (pending_btc_source != BTC_SIGN_SOURCE_SMARTCARD &&
      !wallet_is_initialized()) {
    return false;
  }

  if (pending_btc_source != BTC_SIGN_SOURCE_SMARTCARD && check_psbt_mismatch()) {
    return true;
  }

  size_t num_inputs = 0;
  size_t num_outputs = 0;

  if (wally_psbt_get_num_inputs(current_psbt, &num_inputs) != WALLY_OK ||
      wally_psbt_get_num_outputs(current_psbt, &num_outputs) != WALLY_OK) {
    return false;
  }

  if (num_inputs == 0 || num_outputs == 0) {
    return false;
  }

  uint64_t *input_amounts = malloc(num_inputs * sizeof(uint64_t));
  if (!input_amounts) {
    return false;
  }
  uint64_t total_input_value = 0;
  for (size_t i = 0; i < num_inputs; i++) {
    input_amounts[i] = psbt_get_input_value(current_psbt, i);
    total_input_value += input_amounts[i];
  }

  struct wally_tx *global_tx = NULL;
  int tx_ret = wally_psbt_get_global_tx_alloc(current_psbt, &global_tx);
  if (tx_ret != WALLY_OK || !global_tx) {
    free(input_amounts);
    return false;
  }

  classified_output_t *classified_outputs =
      calloc(num_outputs, sizeof(classified_output_t));
  if (!classified_outputs) {
    free(input_amounts);
    wally_tx_free(global_tx);
    return false;
  }

  uint64_t total_output_value = 0;
  for (size_t i = 0; i < num_outputs; i++) {
    total_output_value += global_tx->outputs[i].satoshi;
  }
  uint64_t fee = (total_input_value > total_output_value)
                     ? (total_input_value - total_output_value)
                     : 0;

  size_t diagram_output_count = num_outputs + (fee > 0 ? 1 : 0);
  uint64_t *output_amounts = malloc(diagram_output_count * sizeof(uint64_t));
  lv_color_t *output_colors = malloc(diagram_output_count * sizeof(lv_color_t));
  if (!output_amounts || !output_colors) {
    free(input_amounts);
    free(output_amounts);
    free(output_colors);
    free(classified_outputs);
    wally_tx_free(global_tx);
    return false;
  }

  for (size_t i = 0; i < num_outputs; i++) {
    classified_outputs[i].index = i;
    classified_outputs[i].value = global_tx->outputs[i].satoshi;
    classified_outputs[i].address = psbt_scriptpubkey_to_address(
        global_tx->outputs[i].script, global_tx->outputs[i].script_len,
        is_testnet);
    classified_outputs[i].type =
        classify_output(i, &global_tx->outputs[i], global_tx,
                        &classified_outputs[i].address_index);
  }

  size_t diagram_idx = 0;

  for (size_t i = 0; i < num_outputs; i++) {
    if (classified_outputs[i].type == OUTPUT_TYPE_SELF_TRANSFER) {
      output_amounts[diagram_idx] = classified_outputs[i].value;
      output_colors[diagram_idx] = cyan_color();
      diagram_idx++;
    }
  }

  for (size_t i = 0; i < num_outputs; i++) {
    if (classified_outputs[i].type == OUTPUT_TYPE_CHANGE) {
      output_amounts[diagram_idx] = classified_outputs[i].value;
      output_colors[diagram_idx] = yes_color();
      diagram_idx++;
    }
  }

  for (size_t i = 0; i < num_outputs; i++) {
    if (classified_outputs[i].type == OUTPUT_TYPE_SPEND) {
      output_amounts[diagram_idx] = classified_outputs[i].value;
      output_colors[diagram_idx] = highlight_color();
      diagram_idx++;
    }
  }

  if (fee > 0) {
    output_amounts[diagram_idx] = fee;
    output_colors[diagram_idx] = error_color();
  }

  psbt_info_container = create_scan_review_scroll_container();
  if (!psbt_info_container) {
    free(input_amounts);
    free(output_amounts);
    free(output_colors);
    free(classified_outputs);
    wally_tx_free(global_tx);
    return false;
  }

  lv_obj_update_layout(psbt_info_container);
  int32_t diagram_width = lv_obj_get_content_width(psbt_info_container);
  if (diagram_width <= 0)
    diagram_width = lv_obj_get_width(scan_screen) - 20;
  tx_diagram = sankey_diagram_create(psbt_info_container, diagram_width, 160);
  if (tx_diagram) {
    sankey_diagram_set_inputs(tx_diagram, input_amounts, num_inputs);
    sankey_diagram_set_outputs(tx_diagram, output_amounts, diagram_output_count,
                               output_colors);
    sankey_diagram_render(tx_diagram);
  }

  size_t input_overflow = sankey_diagram_get_input_overflow(tx_diagram);
  size_t output_overflow = sankey_diagram_get_output_overflow(tx_diagram);

  if (input_overflow > 0 || output_overflow > 0) {
    lv_obj_t *overflow_row = lv_obj_create(psbt_info_container);
    lv_obj_set_size(overflow_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(overflow_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(overflow_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(overflow_row, 0, 0);
    lv_obj_set_style_bg_opa(overflow_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(overflow_row, 0, 0);

    if (input_overflow > 0) {
      char overflow_text[32];
      snprintf(overflow_text, sizeof(overflow_text),
               scan_tr("scan.more_items_format", "%zu more"),
               input_overflow);
      lv_obj_t *label = theme_create_label(overflow_row, overflow_text, false);
      lv_obj_set_style_text_color(label, secondary_color(), 0);
    } else {
      lv_obj_t *spacer = lv_obj_create(overflow_row);
      lv_obj_set_size(spacer, 1, 1);
      lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
      lv_obj_set_style_border_width(spacer, 0, 0);
    }

    if (output_overflow > 0) {
      char overflow_text[32];
      snprintf(overflow_text, sizeof(overflow_text),
               scan_tr("scan.more_items_format", "%zu more"),
               output_overflow);
      lv_obj_t *label = theme_create_label(overflow_row, overflow_text, false);
      lv_obj_set_style_text_color(label, secondary_color(), 0);
    }
  }

  free(input_amounts);
  free(output_amounts);
  free(output_colors);

  char prefix_text[64];
  snprintf(prefix_text, sizeof(prefix_text),
           scan_tr("scan.inputs_count_format", "Inputs (%zu):"), num_inputs);
  lv_obj_t *inputs_row = create_btc_value_row(psbt_info_container, prefix_text,
                                              total_input_value, main_color());
  lv_obj_set_width(inputs_row, LV_PCT(100));

  lv_obj_t *separator1 = lv_obj_create(psbt_info_container);
  lv_obj_set_size(separator1, LV_PCT(100), 2);
  lv_obj_set_style_bg_color(separator1, main_color(), 0);
  lv_obj_set_style_bg_opa(separator1, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(separator1, 0, 0);

  bool has_self_transfers = false;
  for (size_t i = 0; i < num_outputs; i++) {
    if (classified_outputs[i].type == OUTPUT_TYPE_SELF_TRANSFER) {
      if (!has_self_transfers) {
        lv_obj_t *title =
            theme_create_label(psbt_info_container,
                               scan_tr("scan.self_transfer",
                                       "Self transfer:"),
                               false);
        theme_apply_label(title, true);
        lv_obj_set_style_text_color(title, cyan_color(), 0);
        lv_obj_set_width(title, LV_PCT(100));
        has_self_transfers = true;
      }

      char text[64];
      snprintf(text, sizeof(text),
               scan_tr("scan.receive_index_format", "Receive #%u:"),
               classified_outputs[i].address_index);
      lv_obj_t *row = create_btc_value_row(
          psbt_info_container, text, classified_outputs[i].value, main_color());
      lv_obj_set_width(row, LV_PCT(100));
      lv_obj_set_style_pad_left(row, 20, 0);

      if (classified_outputs[i].address) {
        lv_obj_t *addr = create_address_label(
            psbt_info_container, classified_outputs[i].address, cyan_color());
        lv_obj_set_width(addr, LV_PCT(100));
        lv_label_set_long_mode(addr, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_pad_left(addr, 20, 0);
      }
    }
  }

  bool has_change = false;
  for (size_t i = 0; i < num_outputs; i++) {
    if (classified_outputs[i].type == OUTPUT_TYPE_CHANGE) {
      if (!has_change) {
        lv_obj_t *title =
            theme_create_label(psbt_info_container,
                               scan_tr("sign.change", "Change:"), false);
        theme_apply_label(title, true);
        lv_obj_set_style_text_color(title, yes_color(), 0);
        lv_obj_set_style_margin_top(title, 15, 0);
        lv_obj_set_width(title, LV_PCT(100));
        has_change = true;
      }

      char text[64];
      snprintf(text, sizeof(text),
               scan_tr("scan.change_index_format", "Change #%u:"),
               classified_outputs[i].address_index);
      lv_obj_t *row = create_btc_value_row(
          psbt_info_container, text, classified_outputs[i].value, main_color());
      lv_obj_set_width(row, LV_PCT(100));
      lv_obj_set_style_pad_left(row, 20, 0);

      if (classified_outputs[i].address) {
        lv_obj_t *addr = create_address_label(
            psbt_info_container, classified_outputs[i].address, yes_color());
        lv_obj_set_width(addr, LV_PCT(100));
        lv_label_set_long_mode(addr, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_pad_left(addr, 20, 0);
      }
    }
  }

  bool has_spends = false;
  for (size_t i = 0; i < num_outputs; i++) {
    if (classified_outputs[i].type == OUTPUT_TYPE_SPEND) {
      if (!has_spends) {
        lv_obj_t *title =
            theme_create_label(psbt_info_container,
                               scan_tr("sign.external_output",
                                       "External output"),
                               false);
        theme_apply_label(title, true);
        lv_obj_set_style_text_color(title, highlight_color(), 0);
        lv_obj_set_style_margin_top(title, 15, 0);
        lv_obj_set_width(title, LV_PCT(100));
        has_spends = true;
      }

      char text[64];
      snprintf(text, sizeof(text),
               scan_tr("scan.output_index_format", "Output %zu:"),
               classified_outputs[i].index);
      lv_obj_t *row = create_btc_value_row(
          psbt_info_container, text, classified_outputs[i].value, main_color());
      lv_obj_set_width(row, LV_PCT(100));
      lv_obj_set_style_pad_left(row, 20, 0);

      if (classified_outputs[i].address) {
        lv_obj_t *addr = create_address_label(psbt_info_container,
                                              classified_outputs[i].address,
                                              highlight_color());
        lv_obj_set_width(addr, LV_PCT(100));
        lv_label_set_long_mode(addr, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_pad_left(addr, 20, 0);
      }
    }
  }

  for (size_t i = 0; i < num_outputs; i++) {
    if (classified_outputs[i].address) {
      if (strcmp(classified_outputs[i].address, "OP_RETURN") == 0) {
        free(classified_outputs[i].address);
      } else {
        wally_free_string(classified_outputs[i].address);
      }
    }
  }
  free(classified_outputs);

  if (global_tx) {
    wally_tx_free(global_tx);
    global_tx = NULL;
  }

  if (fee > 0) {
    lv_obj_t *separator2 = lv_obj_create(psbt_info_container);
    lv_obj_set_size(separator2, LV_PCT(100), 2);
    lv_obj_set_style_bg_color(separator2, main_color(), 0);
    lv_obj_set_style_bg_opa(separator2, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(separator2, 0, 0);

    lv_obj_t *fee_row =
        create_btc_value_row(psbt_info_container,
                             scan_tr("scan.fee_label", "Fee:"), fee,
                             error_color());
    lv_obj_set_width(fee_row, LV_PCT(100));
  }

  lv_obj_t *button_container = lv_obj_create(psbt_info_container);
  lv_obj_set_size(button_container, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(button_container, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(button_container, LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(button_container, 0, 0);
  lv_obj_set_style_pad_gap(button_container, 10, 0);
  lv_obj_set_style_bg_opa(button_container, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(button_container, 0, 0);

  lv_obj_t *back_button = lv_btn_create(button_container);
  lv_obj_set_size(back_button, LV_PCT(45), LV_SIZE_CONTENT);
  theme_apply_touch_button(back_button, false);
  lv_obj_add_event_cb(back_button, back_button_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_clear_flag(back_button, LV_OBJ_FLAG_EVENT_BUBBLE);

  lv_obj_t *back_label = lv_label_create(back_button);
  lv_label_set_text(back_label, scan_tr("common.back", "Back"));
  lv_obj_center(back_label);
  theme_apply_button_label(back_label, false);

  lv_obj_t *sign_button = lv_btn_create(button_container);
  lv_obj_set_size(sign_button, LV_PCT(45), LV_SIZE_CONTENT);
  theme_apply_touch_button(sign_button, false);
  lv_obj_add_event_cb(sign_button, sign_button_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_clear_flag(sign_button, LV_OBJ_FLAG_EVENT_BUBBLE);

  lv_obj_t *sign_label = lv_label_create(sign_button);
  lv_label_set_text(sign_label, scan_tr("sign.sign", "Sign"));
  lv_obj_center(sign_label);
  theme_apply_button_label(sign_label, false);

  return true;
}

static lv_obj_t *sign_progress_dialog = NULL;

static void dismiss_sign_progress(void) {
  if (sign_progress_dialog) {
    lv_obj_del(sign_progress_dialog);
    sign_progress_dialog = NULL;
  }
}

static bool show_signed_psbt_qr_after_sign(void) {
  if (!current_psbt) {
    dialog_show_error(scan_tr("scan.no_loaded_transaction",
                              "No transaction loaded"),
                      NULL, 2000);
    return false;
  }

  if (signed_psbt_base64) {
    wally_free_string(signed_psbt_base64);
    signed_psbt_base64 = NULL;
  }

  struct wally_psbt *trimmed_psbt = psbt_trim(current_psbt);
  struct wally_psbt *export_psbt = trimmed_psbt ? trimmed_psbt : current_psbt;

  int ret = wally_psbt_to_base64(export_psbt, 0, &signed_psbt_base64);

  if (trimmed_psbt) {
    wally_psbt_free(trimmed_psbt);
  }

  dismiss_sign_progress();

  if (ret != WALLY_OK) {
    dialog_show_error(scan_tr("scan.transaction_encode_failed",
                              "Transaction encoding failed"),
                      NULL, 2000);
    return false;
  }

  saved_return_callback = return_callback;

  int export_format =
      (scanned_qr_format == -1) ? FORMAT_NONE : scanned_qr_format;

  if (!qr_viewer_page_create_with_format(lv_screen_active(), export_format,
                                         signed_psbt_base64,
                                         scan_tr("sign.signed_transaction",
                                                 "Signed transaction"),
                                         return_from_qr_viewer_cb)) {
    dialog_show_error(scan_tr("scan.qr_viewer_create_failed",
                              "QR display page creation failed"),
                      return_callback, 2000);
    return false;
  }

  scan_page_hide();
  scan_page_destroy();

  qr_viewer_page_show();
  return true;
}

static void deferred_sign_cb(lv_timer_t *timer) {
  (void)timer;

  if (!current_psbt) {
    dismiss_sign_progress();
    dialog_show_error(scan_tr("scan.no_loaded_transaction",
                              "No transaction loaded"),
                      NULL, 2000);
    return;
  }

  size_t signatures_added = psbt_sign(current_psbt, is_testnet);

  if (signatures_added == 0) {
    dismiss_sign_progress();
    dialog_show_error(scan_tr("sign.signature_failed",
                              "Transaction signing failed"),
                      NULL, 2000);
    return;
  }

  show_signed_psbt_qr_after_sign();
}

static void start_psbt_sign_after_pin(void) {
  if (!current_psbt) {
    dialog_show_error(scan_tr("scan.no_loaded_transaction",
                              "No transaction loaded"),
                      NULL, 2000);
    return;
  }

  // defer the work to a one-shot timer so LVGL gets to render it first.
  sign_progress_dialog = dialog_show_progress(
      scan_tr("sign.sign", "Sign"),
      scan_tr("dialog.processing", "Processing..."), DIALOG_STYLE_OVERLAY);
  lv_timer_t *t = lv_timer_create(deferred_sign_cb, 50, NULL);
  lv_timer_set_repeat_count(t, 1);
}

static void btc_satochip_pin_input_cleanup(void) {
  if (btc_satochip_pin_input_active) {
    ui_text_input_destroy(&btc_satochip_pin_input);
    memset(&btc_satochip_pin_input, 0, sizeof(btc_satochip_pin_input));
    btc_satochip_pin_input_active = false;
  }
}

static void btc_satochip_sign_task(void *arg) {
  (void)arg;
  const bool delete_with_caps = btc_satochip_sign_task_with_caps;
  btc_satochip_sign_task_err = ESP_ERR_INVALID_STATE;
  btc_satochip_signatures_added = 0;
  btc_satochip_sign_task_detail[0] = '\0';

  btc_satochip_signatures_added = psbt_sign_with_satochip(
      current_psbt, btc_satochip_sign_task_pin, is_testnet,
      btc_satochip_sign_task_detail, sizeof(btc_satochip_sign_task_detail));
  btc_satochip_sign_task_err =
      btc_satochip_signatures_added > 0 ? ESP_OK : ESP_FAIL;

  secure_memzero(btc_satochip_sign_task_pin,
                 sizeof(btc_satochip_sign_task_pin));
  __atomic_store_n(&btc_satochip_sign_task_done, true, __ATOMIC_RELEASE);
  if (delete_with_caps) {
    vTaskDeleteWithCaps(NULL);
    return;
  }
  vTaskDelete(NULL);
}

static void btc_satochip_sign_finish_ui(void) {
  if (btc_satochip_sign_progress_dialog) {
    lv_obj_del(btc_satochip_sign_progress_dialog);
    btc_satochip_sign_progress_dialog = NULL;
  }

  if (btc_satochip_sign_task_err != ESP_OK ||
      btc_satochip_signatures_added == 0) {
    char msg[384];
    snprintf(msg, sizeof(msg),
             scan_tr("scan.card_sign_failed_detail_no_colon",
                     "Smartcard signing failed\n%s"),
             btc_satochip_sign_task_detail[0]
                 ? btc_satochip_sign_task_detail
                 : scan_tr("scan.no_signable_inputs",
                           "No signable inputs."));
    dialog_show_error(msg, show_scanned_sign_payload, 0);
    return;
  }

  show_signed_psbt_qr_after_sign();
}

static void btc_satochip_sign_poll_cb(lv_timer_t *timer) {
  (void)timer;
  if (!__atomic_load_n(&btc_satochip_sign_task_done, __ATOMIC_ACQUIRE))
    return;

  if (btc_satochip_sign_poll_timer) {
    lv_timer_del(btc_satochip_sign_poll_timer);
    btc_satochip_sign_poll_timer = NULL;
  }
  btc_satochip_sign_task_handle = NULL;
  btc_satochip_sign_task_with_caps = false;
  btc_satochip_sign_finish_ui();
}

static void start_psbt_satochip_sign(const char *pin_text) {
  if (!pin_text || !pin_text[0]) {
    dialog_show_error(scan_tr("sign.enter_card_pin", "Enter smartcard PIN"),
                      show_scanned_sign_payload, 1600);
    return;
  }
  if (!current_psbt) {
    dialog_show_error(scan_tr("scan.no_loaded_transaction",
                              "No transaction loaded"),
                      return_callback, 2000);
    return;
  }

  snprintf(btc_satochip_sign_task_pin, sizeof(btc_satochip_sign_task_pin),
           "%s", pin_text);
  btc_satochip_sign_task_detail[0] = '\0';
  btc_satochip_signatures_added = 0;
  btc_satochip_sign_task_err = ESP_ERR_INVALID_STATE;
  __atomic_store_n(&btc_satochip_sign_task_done, false, __ATOMIC_RELEASE);
  btc_satochip_sign_task_with_caps = false;

  btc_satochip_sign_progress_dialog = dialog_show_progress(
      scan_tr("scan.smartcard_signing", "Smartcard signing"),
      scan_tr("sign.signing", "Signing"), DIALOG_STYLE_OVERLAY);
  lv_refr_now(NULL);

  BaseType_t ok = xTaskCreatePinnedToCore(
      btc_satochip_sign_task, "btc_card_sign",
      BTC_SATOCHIP_SIGN_TASK_STACK_SIZE, NULL, 4,
      &btc_satochip_sign_task_handle, 1);
  if (ok != pdPASS) {
    btc_satochip_sign_task_with_caps = true;
    ok = xTaskCreatePinnedToCoreWithCaps(
        btc_satochip_sign_task, "btc_card_sign",
        BTC_SATOCHIP_SIGN_TASK_STACK_SIZE, NULL, 4,
        &btc_satochip_sign_task_handle, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
  if (ok != pdPASS) {
    secure_memzero(btc_satochip_sign_task_pin,
                   sizeof(btc_satochip_sign_task_pin));
    btc_satochip_sign_task_handle = NULL;
    btc_satochip_sign_task_with_caps = false;
    if (btc_satochip_sign_progress_dialog) {
      lv_obj_del(btc_satochip_sign_progress_dialog);
      btc_satochip_sign_progress_dialog = NULL;
    }
    dialog_show_error(scan_tr("scan.card_sign_task_start_failed",
                              "Smartcard signing task failed to start"),
                      show_scanned_sign_payload, 0);
    return;
  }

  btc_satochip_sign_poll_timer =
      lv_timer_create(btc_satochip_sign_poll_cb, 100, NULL);
}

static void btc_satochip_pin_ready_cb(lv_event_t *e) {
  (void)e;
  if (!btc_satochip_pin_input.textarea)
    return;
  const char *pin_text =
      lv_textarea_get_text(btc_satochip_pin_input.textarea);
  char pin_copy[80];
  snprintf(pin_copy, sizeof(pin_copy), "%s", pin_text ? pin_text : "");
  btc_satochip_pin_input_cleanup();
  start_psbt_satochip_sign(pin_copy);
  secure_memzero(pin_copy, sizeof(pin_copy));
}

static void btc_satochip_pin_back_cb(lv_event_t *e) {
  (void)e;
  btc_satochip_pin_input_cleanup();
  show_scanned_sign_payload();
}

static void show_btc_satochip_pin_input(void) {
  lv_obj_t *root = btc_prepare_page();
  (void)web3_create_fixed_title(root,
                                scan_tr("sign.card_pin", "Smartcard PIN"));

  btc_satochip_pin_input_cleanup();
  ui_text_input_create(&btc_satochip_pin_input, root,
                       scan_tr("sign.card_pin", "Smartcard PIN"), true,
                       btc_satochip_pin_ready_cb);
  btc_satochip_pin_input_active = true;
  if (btc_satochip_pin_input.keyboard)
    lv_obj_add_event_cb(btc_satochip_pin_input.keyboard,
                        btc_satochip_pin_back_cb, LV_EVENT_CANCEL, NULL);
  if (btc_satochip_pin_input.textarea)
    lv_obj_add_event_cb(btc_satochip_pin_input.textarea,
                        btc_satochip_pin_back_cb, LV_EVENT_CANCEL, NULL);
  (void)ui_create_back_button(root, btc_satochip_pin_back_cb);
}

static void psbt_final_confirm_cb(bool confirmed, void *user_data) {
  (void)user_data;
  if (!confirmed)
    return;
  start_psbt_sign_after_pin();
}

static void request_psbt_final_confirm_after_pin(void) {
  dialog_show_danger_confirm(
      scan_tr("scan.psbt_final_confirm",
              "Final check:\nReview the recipient address, amount, change, "
              "and fee again.\nConfirm signing?"),
      psbt_final_confirm_cb, NULL, DIALOG_STYLE_OVERLAY);
}

static void sign_button_cb(lv_event_t *e) {
  (void)e;
  if (pending_btc_source == BTC_SIGN_SOURCE_SMARTCARD) {
    show_btc_satochip_pin_input();
    return;
  }
  sensitive_pin_require(request_psbt_final_confirm_after_pin, NULL);
}

static void return_from_qr_viewer_cb(void) {
  qr_viewer_page_destroy();
  if (saved_return_callback) {
    void (*callback)(void) = saved_return_callback;
    saved_return_callback = NULL;
    callback();
  }
}

static void cleanup_psbt_data(void) {
  parsed_psbt_preferred_export_format = -1;
  if (current_psbt) {
    wally_psbt_free(current_psbt);
    current_psbt = NULL;
  }

  if (psbt_base64) {
    free(psbt_base64);
    psbt_base64 = NULL;
  }

  if (signed_psbt_base64) {
    wally_free_string(signed_psbt_base64);
    signed_psbt_base64 = NULL;
  }

  message_sign_free_parsed(&current_message);
  is_message_sign = false;

  is_testnet = false;
  scanned_qr_format = FORMAT_NONE;
  skip_verification = false;
}

// Multisig menu callbacks
static void multisig_menu_back_cb(void) {
  descriptor_loader_destroy_source_menu();
  if (multisig_menu) {
    ui_menu_destroy(multisig_menu);
    multisig_menu = NULL;
  }
  cleanup_psbt_data();
  if (return_callback) {
    return_callback();
  }
}

static void show_multisig_menu_on_error(void) {
  if (multisig_menu)
    ui_menu_show(multisig_menu);
}

static void descriptor_validation_cb(descriptor_validation_result_t result,
                                     void *user_data) {
  (void)user_data;

  if (result == VALIDATION_SUCCESS) {
    descriptor_loader_destroy_source_menu();
    if (multisig_menu) {
      ui_menu_destroy(multisig_menu);
      multisig_menu = NULL;
    }
    if (!create_psbt_info_display()) {
      dialog_show_error(scan_tr("sign.psbt_invalid",
                                "Invalid transaction data"),
                        return_callback, 0);
    }
    return;
  }

  descriptor_loader_show_error(result);
  show_multisig_menu_on_error();
}

static void return_from_descriptor_scanner_cb(void) {
  descriptor_loader_process_scanner(descriptor_validation_cb, NULL,
                                    show_multisig_menu_on_error);
}

static void return_from_descriptor_storage(void) {
  load_descriptor_storage_page_destroy();
  show_multisig_menu_on_error();
}

static void success_from_descriptor_storage(void) {
  load_descriptor_storage_page_destroy();
  descriptor_loader_destroy_source_menu();
  if (multisig_menu) {
    ui_menu_destroy(multisig_menu);
    multisig_menu = NULL;
  }
  if (!create_psbt_info_display()) {
    dialog_show_error(scan_tr("sign.psbt_invalid", "Invalid transaction data"),
                      return_callback, 0);
  }
}

static void load_desc_from_qr_cb(void) {
  descriptor_loader_destroy_source_menu();
  if (multisig_menu)
    ui_menu_hide(multisig_menu);
  qr_scanner_page_create(NULL, return_from_descriptor_scanner_cb);
  qr_scanner_page_show();
}

static void load_desc_from_flash_cb(void) {
  descriptor_loader_destroy_source_menu();
  if (multisig_menu)
    ui_menu_hide(multisig_menu);
  load_descriptor_storage_page_create(
      lv_screen_active(), return_from_descriptor_storage,
      success_from_descriptor_storage, STORAGE_FLASH);
  load_descriptor_storage_page_show();
}

static void load_desc_from_sd_cb(void) {
  descriptor_loader_destroy_source_menu();
  if (multisig_menu)
    ui_menu_hide(multisig_menu);
  load_descriptor_storage_page_create(
      lv_screen_active(), return_from_descriptor_storage,
      success_from_descriptor_storage, STORAGE_SD);
  load_descriptor_storage_page_show();
}

static void load_desc_source_back_cb(void) {
  descriptor_loader_destroy_source_menu();
}

static void load_descriptor_menu_cb(void) {
  descriptor_loader_show_source_menu(
      scan_screen, load_desc_from_qr_cb, load_desc_from_flash_cb,
      load_desc_from_sd_cb, load_desc_source_back_cb);
}

static void unsafe_multisig_signing_disabled_cb(void) {
  if (multisig_menu) {
    ui_menu_destroy(multisig_menu);
    multisig_menu = NULL;
  }
  skip_verification = false;
  dialog_show_error(scan_tr("scan.unsafe_multisig_disabled",
                            "Unchecked signing is disabled.\n\nLoad a "
                            "descriptor before signing multisig transactions "
                            "so change and outputs can be verified."),
                    return_callback, 0);
}

static void show_multisig_options_menu(void) {
  if (!scan_screen) {
    return;
  }

  multisig_menu = ui_menu_create(scan_screen,
                                 scan_tr("sign.multisig_detected",
                                         "Multisig transaction detected"),
                                 multisig_menu_back_cb);
  if (!multisig_menu) {
    dialog_show_error(scan_tr("scan.menu_create_failed",
                              "Menu creation failed"),
                      return_callback, 0);
    return;
  }

  ui_menu_add_entry(multisig_menu,
                    scan_tr("descriptor.load_descriptor", "Load descriptor"),
                    load_descriptor_menu_cb);
  ui_menu_add_entry(multisig_menu,
                    scan_tr("scan.descriptor_required_notice",
                            "Safety note: descriptor required"),
                    unsafe_multisig_signing_disabled_cb);
  ui_menu_show(multisig_menu);
}

static void create_message_sign_display(void) {
  if (!scan_screen) {
    return;
  }

  wallet_network_t net = wallet_get_network();
  bool testnet = (net == WALLET_NETWORK_TESTNET);

  char *address = NULL;
  if (!message_sign_get_address(current_message.derivation_path, testnet,
                                &address)) {
    dialog_show_error(scan_tr("scan.address_derive_failed",
                              "Address derivation failed"),
                      return_callback, 0);
    return;
  }

  psbt_info_container = create_scan_review_scroll_container();
  if (!psbt_info_container) {
    wally_free_string(address);
    return;
  }

  theme_create_page_title(psbt_info_container,
                          scan_tr("sign.message_signing",
                                  "Message signing"));

  lv_obj_t *path_title =
      theme_create_label(psbt_info_container, scan_tr("scan.path_label",
                                                      "Path:"),
                         false);
  theme_apply_label(path_title, true);
  lv_obj_set_style_text_color(path_title, secondary_color(), 0);

  lv_obj_t *path_label = theme_create_label(
      psbt_info_container, current_message.derivation_path, false);
  lv_obj_set_width(path_label, LV_PCT(100));

  lv_obj_t *addr_title =
      theme_create_label(psbt_info_container, scan_tr("scan.address_label",
                                                      "Address:"),
                         false);
  theme_apply_label(addr_title, true);
  lv_obj_set_style_text_color(addr_title, secondary_color(), 0);

  lv_obj_t *addr_label =
      create_address_label(psbt_info_container, address, highlight_color());
  lv_obj_set_width(addr_label, LV_PCT(100));
  lv_label_set_long_mode(addr_label, LV_LABEL_LONG_WRAP);

  wally_free_string(address);

  lv_obj_t *separator = lv_obj_create(psbt_info_container);
  lv_obj_set_size(separator, LV_PCT(100), 2);
  lv_obj_set_style_bg_color(separator, main_color(), 0);
  lv_obj_set_style_bg_opa(separator, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(separator, 0, 0);

  lv_obj_t *msg_title =
      theme_create_label(psbt_info_container, scan_tr("scan.message_label",
                                                      "Message:"),
                         false);
  theme_apply_label(msg_title, true);
  lv_obj_set_style_text_color(msg_title, secondary_color(), 0);

  lv_obj_t *msg_label =
      theme_create_label(psbt_info_container, current_message.message, false);
  lv_obj_set_width(msg_label, LV_PCT(100));
  lv_label_set_long_mode(msg_label, LV_LABEL_LONG_WRAP);

  lv_obj_t *button_container = lv_obj_create(psbt_info_container);
  lv_obj_set_size(button_container, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(button_container, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(button_container, LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(button_container, 0, 0);
  lv_obj_set_style_pad_gap(button_container, 10, 0);
  lv_obj_set_style_bg_opa(button_container, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(button_container, 0, 0);

  lv_obj_t *back_button = lv_btn_create(button_container);
  lv_obj_set_size(back_button, LV_PCT(45), LV_SIZE_CONTENT);
  theme_apply_touch_button(back_button, false);
  lv_obj_add_event_cb(back_button, back_button_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_clear_flag(back_button, LV_OBJ_FLAG_EVENT_BUBBLE);

  lv_obj_t *back_label = lv_label_create(back_button);
  lv_label_set_text(back_label, scan_tr("common.back", "Back"));
  lv_obj_center(back_label);
  theme_apply_button_label(back_label, false);

  lv_obj_t *sign_button = lv_btn_create(button_container);
  lv_obj_set_size(sign_button, LV_PCT(45), LV_SIZE_CONTENT);
  theme_apply_touch_button(sign_button, false);
  lv_obj_add_event_cb(sign_button, message_sign_button_cb, LV_EVENT_CLICKED,
                      NULL);
  lv_obj_clear_flag(sign_button, LV_OBJ_FLAG_EVENT_BUBBLE);

  lv_obj_t *sign_label = lv_label_create(sign_button);
  lv_label_set_text(sign_label, scan_tr("sign.sign", "Sign"));
  lv_obj_center(sign_label);
  theme_apply_button_label(sign_label, false);
}

static void start_message_sign_after_pin(void) {
  char *sig_b64 = NULL;
  if (!message_sign_sign(current_message.derivation_path,
                         current_message.message, &sig_b64)) {
    dialog_show_error(scan_tr("scan.message_sign_failed",
                              "Message signing failed"),
                      NULL, 2000);
    return;
  }

  saved_return_callback = return_callback;

  bool qr_ready = qr_viewer_page_create(lv_screen_active(), sig_b64,
                                        scan_tr("sign.message_signature",
                                                "Message signature"),
                                        return_from_qr_viewer_cb);
  wally_free_string(sig_b64);
  if (!qr_ready) {
    dialog_show_error(scan_tr("scan.signature_qr_create_failed",
                              "Signature QR creation failed"),
                      NULL, 2000);
    return;
  }

  scan_page_hide();
  scan_page_destroy();

  qr_viewer_page_show();
}

static void message_final_confirm_cb(bool confirmed, void *user_data) {
  (void)user_data;
  if (!confirmed)
    return;
  start_message_sign_after_pin();
}

static void request_message_final_confirm_after_pin(void) {
  dialog_show_danger_confirm(
      scan_tr("scan.message_final_confirm",
              "Final check:\nReview the message, address, and derivation path "
              "again.\nConfirm signing?"),
      message_final_confirm_cb, NULL, DIALOG_STYLE_OVERLAY);
}

static void message_sign_button_cb(lv_event_t *e) {
  (void)e;
  sensitive_pin_require(request_message_final_confirm_after_pin, NULL);
}

#ifdef SIMULATOR
static void scan_simulator_seed_web3_common(web3_sign_request_t *req,
                                            const char *wallet,
                                            const char *action) {
  web3_request_clear(req);
  req->valid = true;
  req->chain_id = 1;
  snprintf(req->wallet, sizeof(req->wallet), "%s", wallet);
  snprintf(req->chain, sizeof(req->chain), "Ethereum");
  snprintf(req->origin, sizeof(req->origin), "app.uniswap.org");
  snprintf(req->action, sizeof(req->action), "%s", action);
  snprintf(req->path, sizeof(req->path), "%s", WEB3_DEFAULT_DERIVATION_PATH);
  snprintf(req->address, sizeof(req->address),
           "0x1234567890abcdef1234567890abcdef12345678");
  web3_set_request_id_string(req, "sim-web3-review");
}

void scan_simulator_show_web3_tx_review(void) {
  scan_simulator_seed_web3_common(&pending_web3_request, "OKX",
                                  "signTransaction");
  pending_web3_request.request_type_id = WEB3_REQUEST_TYPE_LEGACY_TX;
  (void)web3_copy_sign_data_from_hex(
      &pending_web3_request,
      "ec098504a817c800825208943535353535353535353535353535353535353535"
      "880de0b6b3a764000080");
  web3_show_request_summary_for_source(WEB3_SIGN_SOURCE_SMARTCARD, false);
}

void scan_simulator_show_web3_typed_review(void) {
  static const char typed_json[] =
      "{\"types\":{\"EIP712Domain\":[{\"name\":\"name\",\"type\":\"string\"},"
      "{\"name\":\"version\",\"type\":\"string\"},{\"name\":\"chainId\","
      "\"type\":\"uint256\"},{\"name\":\"verifyingContract\","
      "\"type\":\"address\"}],\"Mail\":[{\"name\":\"from\","
      "\"type\":\"address\"},{\"name\":\"to\",\"type\":\"address\"},"
      "{\"name\":\"contents\",\"type\":\"string\"}]},"
      "\"primaryType\":\"Mail\",\"domain\":{\"name\":\"KernSigner Test\","
      "\"version\":\"1\",\"chainId\":1,\"verifyingContract\":"
      "\"0xCcCCccccCCCCcCCCCCCcCcCccCcCCCcCcccccccC\"},"
      "\"message\":{\"from\":\"0x1111111111111111111111111111111111111111\","
      "\"to\":\"0x2222222222222222222222222222222222222222\","
      "\"contents\":\"Confirm DApp approval test\"}}";

  scan_simulator_seed_web3_common(&pending_web3_request, "Bitget",
                                  "signTypedData");
  pending_web3_request.request_type_id = WEB3_REQUEST_TYPE_TYPED_DATA;
  (void)web3_copy_sign_data(&pending_web3_request, (const uint8_t *)typed_json,
                            strlen(typed_json));
  web3_show_request_summary_for_source(WEB3_SIGN_SOURCE_SMARTCARD, false);
}
#endif

void scan_page_create(lv_obj_t *parent, void (*return_cb)(void)) {
  if (!parent || !key_is_loaded()) {
    return;
  }

  smartcard_web3_mode = false;
  unified_scan_mode = false;
  pending_web3_from_ur = false;
  return_callback = return_cb;
  (void)mnemonic_slots_add_current(NULL);

  scan_screen = theme_create_page_container(parent);
  qr_scanner_page_create(NULL, return_from_qr_scanner_cb);
  qr_scanner_page_show();
}

void scan_page_create_unified(lv_obj_t *parent, void (*return_cb)(void)) {
  if (!parent) {
    return;
  }

  smartcard_web3_mode = false;
  unified_scan_mode = true;
  pending_web3_from_ur = false;
  return_callback = return_cb;
  if (key_is_loaded())
    (void)mnemonic_slots_add_current(NULL);

  scan_screen = theme_create_page_container(parent);
  qr_scanner_page_create(NULL, return_from_qr_scanner_cb);
  qr_scanner_page_show();
}

void scan_page_create_smartcard_web3(lv_obj_t *parent, void (*return_cb)(void)) {
  if (!parent) {
    return;
  }

  smartcard_web3_mode = true;
  unified_scan_mode = false;
  pending_web3_from_ur = false;
  return_callback = return_cb;
  scan_screen = theme_create_page_container(parent);
  qr_scanner_page_create(NULL, return_from_qr_scanner_cb);
  qr_scanner_page_show();
}

void scan_page_show(void) {
  if (scan_screen) {
    lv_obj_clear_flag(scan_screen, LV_OBJ_FLAG_HIDDEN);
  }
}

void scan_page_hide(void) {
  if (scan_screen) {
    lv_obj_add_flag(scan_screen, LV_OBJ_FLAG_HIDDEN);
  }
}

void scan_page_destroy(void) {
  if (web3_info_active) {
    return_callback = NULL;
    return;
  }

  if (web3_sign_task_handle &&
      !__atomic_load_n(&web3_sign_task_done, __ATOMIC_ACQUIRE)) {
    ESP_LOGW(TAG, "Scan page destroy deferred while Web3 sign task is running");
    return;
  }
  if (btc_satochip_sign_task_handle &&
      !__atomic_load_n(&btc_satochip_sign_task_done, __ATOMIC_ACQUIRE)) {
    ESP_LOGW(TAG,
             "Scan page destroy deferred while BTC smartcard sign task is running");
    return;
  }
  if (web3_sign_poll_timer) {
    lv_timer_del(web3_sign_poll_timer);
    web3_sign_poll_timer = NULL;
  }
  if (btc_satochip_sign_poll_timer) {
    lv_timer_del(btc_satochip_sign_poll_timer);
    btc_satochip_sign_poll_timer = NULL;
  }
  web3_sign_task_handle = NULL;
  web3_sign_task_with_caps = false;
  btc_satochip_sign_task_handle = NULL;
  btc_satochip_sign_task_with_caps = false;

  dismiss_sign_progress();
  if (btc_satochip_sign_progress_dialog) {
    lv_obj_del(btc_satochip_sign_progress_dialog);
    btc_satochip_sign_progress_dialog = NULL;
  }
  btc_satochip_pin_input_cleanup();
  web3_pin_input_cleanup();
  qr_scanner_page_destroy();
  load_descriptor_storage_page_destroy();
  descriptor_loader_destroy_source_menu();
  address_checker_destroy();

  cleanup_psbt_data();

  SECURE_FREE_STRING(scanned_mnemonic);

  if (multisig_menu) {
    ui_menu_destroy(multisig_menu);
    multisig_menu = NULL;
  }

  sankey_diagram_t *diagram_to_free = tx_diagram;
  tx_diagram = NULL;

  psbt_info_container = NULL;

  if (scan_screen) {
    lv_obj_del(scan_screen);
    scan_screen = NULL;
  }

  sankey_diagram_destroy_after_parent_deleted(diagram_to_free);

  return_callback = NULL;
  web3_info_active = false;
  smartcard_web3_mode = false;
  unified_scan_mode = false;
  pending_web3_from_ur = false;
  pending_btc_source = BTC_SIGN_SOURCE_NONE;
}
