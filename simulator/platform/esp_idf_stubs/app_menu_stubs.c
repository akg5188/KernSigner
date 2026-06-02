#include "../../main/ui/dialog.h"
#include "../../main/core/custom_derivation.h"
#include "../../main/core/evm.h"
#include "../../main/core/key.h"
#include "../../main/core/mnemonic_slots.h"
#include "../../main/core/wallet.h"
#include "../../main/smartcard/smartcard_satochip.h"
#include "../../main/ui/input_helpers.h"
#include "../../main/ui/theme.h"
#include <esp_err.h>
#include <lvgl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <wally_bip39.h>
#include <wally_core.h>

static void fill_string(char *dst, size_t dst_len, const char *src) {
  if (!dst || dst_len == 0)
    return;
  if (!src)
    src = "";
  strncpy(dst, src, dst_len - 1);
  dst[dst_len - 1] = '\0';
}

static int sim_max_i(int a, int b) { return a > b ? a : b; }

static void sim_style_black_orange_box(lv_obj_t *obj, int radius) {
  if (!obj)
    return;
  lv_obj_set_style_bg_color(obj, bg_color(), LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(obj, main_color(), LV_STATE_DEFAULT);
  lv_obj_set_style_border_color(obj, highlight_color(), LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(obj, 2, LV_STATE_DEFAULT);
  lv_obj_set_style_radius(obj, radius, LV_STATE_DEFAULT);
  lv_obj_set_style_shadow_width(obj, 0, LV_STATE_DEFAULT);
  lv_obj_set_style_outline_width(obj, 0, LV_STATE_DEFAULT);
}

bool key_has_signing_key(void) { return true; }
bool key_get_fingerprint_hex(char *hex_out) {
  fill_string(hex_out, 9, "12345678");
  return true;
}
bool key_compute_mnemonic_fingerprint(unsigned char *fingerprint_out,
                                      const char *mnemonic) {
  if (!fingerprint_out || !mnemonic || !mnemonic[0])
    return false;

  uint32_t hash = 2166136261u;
  for (const unsigned char *p = (const unsigned char *)mnemonic; *p; p++) {
    hash ^= *p;
    hash *= 16777619u;
  }
  fingerprint_out[0] = (unsigned char)((hash >> 24) & 0xff);
  fingerprint_out[1] = (unsigned char)((hash >> 16) & 0xff);
  fingerprint_out[2] = (unsigned char)((hash >> 8) & 0xff);
  fingerprint_out[3] = (unsigned char)(hash & 0xff);
  return true;
}
bool key_compute_mnemonic_fingerprint_hex(char *hex_out,
                                          const char *mnemonic) {
  if (!hex_out)
    return false;

  unsigned char fp[BIP32_KEY_FINGERPRINT_LEN] = {0};
  if (!key_compute_mnemonic_fingerprint(fp, mnemonic)) {
    fill_string(hex_out, 9, "--------");
    return false;
  }
  snprintf(hex_out, 9, "%02x%02x%02x%02x", fp[0], fp[1], fp[2], fp[3]);
  return true;
}
bool key_get_mnemonic_fingerprint_hex(char *hex_out) {
  fill_string(hex_out, 9, "12345678");
  return true;
}
bool key_get_session_passphrase(char **passphrase_out) {
  if (passphrase_out)
    *passphrase_out = NULL;
  return false;
}

bool mnemonic_slots_get_info(size_t slot_index, mnemonic_slot_info_t *info_out) {
  if (!info_out || slot_index != 0)
    return false;
  memset(info_out, 0, sizeof(*info_out));
  info_out->used = true;
  fill_string(info_out->fingerprint, sizeof(info_out->fingerprint), "87654321");
  info_out->word_count = 12;
  return true;
}
bool mnemonic_slots_load(size_t slot_index, const char *passphrase) {
  (void)slot_index;
  (void)passphrase;
  return true;
}
void mnemonic_slots_clear_all(void) {}
size_t mnemonic_slots_count(void) { return 1; }

bool wallet_is_initialized(void) { return true; }
wallet_network_t wallet_get_network(void) { return WALLET_NETWORK_MAINNET; }

bool custom_derivation_get_address(const char *path, custom_address_type_t type,
                                   bool is_testnet, char *address_out,
                                   size_t address_out_len) {
  (void)path;
  (void)is_testnet;
  if (type == CUSTOM_ADDR_EVM) {
    fill_string(address_out, address_out_len,
                "0x0000000000000000000000000000000000000000");
  } else {
    fill_string(address_out, address_out_len,
                "bc1q000000000000000000000000000000000000000");
  }
  return true;
}

int wally_free_string(char *str) {
  free(str);
  return WALLY_OK;
}
int bip39_get_wordlist(const char *lang, struct words **output) {
  (void)lang;
  if (output)
    *output = NULL;
  return WALLY_ERROR;
}
int bip39_mnemonic_from_bytes(const struct words *w,
                              const unsigned char *bytes, size_t bytes_len,
                              char **output) {
  (void)w;
  (void)bytes;
  (void)bytes_len;
  if (output)
    *output = NULL;
  return WALLY_ERROR;
}

void dialog_show_error(const char *message, dialog_simple_callback_t callback,
                       int timeout_ms) {
  (void)timeout_ms;
  if (callback)
    callback();
  (void)message;
}

void dialog_show_info(const char *title, const char *message,
                      dialog_callback_t callback, void *user_data,
                      dialog_style_t style) {
  (void)title;
  (void)message;
  (void)style;
  if (callback)
    callback(user_data);
}

void dialog_show_confirm(const char *message,
                         dialog_confirm_callback_t callback, void *user_data,
                         dialog_style_t style) {
  (void)message;
  (void)style;
  if (callback)
    callback(true, user_data);
}

void dialog_show_danger_confirm(const char *message,
                                dialog_confirm_callback_t callback,
                                void *user_data, dialog_style_t style) {
  dialog_show_confirm(message, callback, user_data, style);
}

void dialog_show_message(const char *title, const char *message) {
  (void)title;
  (void)message;
}

lv_obj_t *dialog_show_progress(const char *title, const char *message,
                               dialog_style_t style) {
  (void)title;
  (void)message;
  (void)style;
  return lv_obj_create(lv_screen_active());
}

static void sim_eye_cb(lv_event_t *e) {
  ui_text_input_t *input = lv_event_get_user_data(e);
  if (!input || !input->textarea || !input->eye_label)
    return;
  bool hidden = lv_textarea_get_password_mode(input->textarea);
  lv_textarea_set_password_mode(input->textarea, !hidden);
  lv_label_set_text(input->eye_label, hidden ? "隐藏" : "显示");
}

void ui_text_input_create(ui_text_input_t *input, lv_obj_t *parent,
                          const char *placeholder, bool password_mode,
                          lv_event_cb_t ready_cb) {
  if (!input || !parent)
    return;
  memset(input, 0, sizeof(*input));

  input->textarea = lv_textarea_create(parent);
  lv_obj_set_size(input->textarea, password_mode ? LV_PCT(76) : LV_PCT(90),
                  58);
  sim_style_black_orange_box(input->textarea, 8);
  lv_obj_set_style_text_color(input->textarea, main_color(), 0);
  lv_obj_set_style_text_color(input->textarea, main_color(),
                              LV_PART_TEXTAREA_PLACEHOLDER);
  lv_obj_set_style_bg_color(input->textarea, highlight_color(), LV_PART_CURSOR);
  lv_obj_set_style_bg_opa(input->textarea, LV_OPA_COVER, LV_PART_CURSOR);
  lv_obj_set_style_width(input->textarea, 4, LV_PART_CURSOR);
  lv_textarea_set_one_line(input->textarea, true);
  lv_textarea_set_password_mode(input->textarea, password_mode);
  lv_textarea_set_placeholder_text(input->textarea, placeholder ? placeholder : "");
  if (ready_cb)
    lv_obj_add_event_cb(input->textarea, ready_cb, LV_EVENT_READY, NULL);

  if (password_mode) {
    input->eye_btn = lv_btn_create(parent);
    lv_obj_set_size(input->eye_btn, 58, 58);
    lv_obj_set_style_bg_opa(input->eye_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(input->eye_btn, 0, 0);
    lv_obj_set_style_shadow_width(input->eye_btn, 0, 0);
    lv_obj_add_event_cb(input->eye_btn, sim_eye_cb, LV_EVENT_CLICKED, input);
    input->eye_label = lv_label_create(input->eye_btn);
    lv_label_set_text(input->eye_label, "显示");
    lv_obj_set_style_text_color(input->eye_label, main_color(), 0);
    lv_obj_center(input->eye_label);
  }

  input->input_group = lv_group_create();
  lv_group_add_obj(input->input_group, input->textarea);

  input->keyboard = lv_keyboard_create(lv_screen_active());
  lv_obj_set_size(input->keyboard, LV_HOR_RES, LV_VER_RES * 36 / 100);
  lv_obj_align(input->keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_textarea(input->keyboard, input->textarea);
  lv_obj_set_style_bg_color(input->keyboard, bg_color(), 0);
  lv_obj_set_style_bg_opa(input->keyboard, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(input->keyboard, 0, 0);
  lv_obj_set_style_bg_color(input->keyboard, bg_color(), LV_PART_ITEMS);
  lv_obj_set_style_text_color(input->keyboard, main_color(), LV_PART_ITEMS);
  lv_obj_set_style_bg_color(input->keyboard, bg_color(),
                            LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_text_color(input->keyboard, main_color(),
                              LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(input->keyboard, bg_color(),
                            LV_PART_ITEMS | LV_STATE_FOCUSED);
  lv_obj_set_style_text_color(input->keyboard, main_color(),
                              LV_PART_ITEMS | LV_STATE_FOCUSED);
  lv_obj_set_style_border_color(input->keyboard, highlight_color(),
                                LV_PART_ITEMS);
  lv_obj_set_style_border_color(input->keyboard, highlight_color(),
                                LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_border_color(input->keyboard, highlight_color(),
                                LV_PART_ITEMS | LV_STATE_FOCUSED);
  lv_obj_set_style_border_width(input->keyboard, 1, LV_PART_ITEMS);
  lv_obj_set_style_border_width(input->keyboard, 1,
                                LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_border_width(input->keyboard, 1,
                                LV_PART_ITEMS | LV_STATE_FOCUSED);
  lv_obj_set_style_radius(input->keyboard, 6, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(input->keyboard, highlight_color(),
                            LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_add_flag(input->keyboard, LV_OBJ_FLAG_HIDDEN);
}

void ui_text_input_show(ui_text_input_t *input) {
  if (input && input->textarea)
    lv_obj_clear_flag(input->textarea, LV_OBJ_FLAG_HIDDEN);
  if (input && input->eye_btn)
    lv_obj_clear_flag(input->eye_btn, LV_OBJ_FLAG_HIDDEN);
  if (input && input->keyboard)
    lv_obj_clear_flag(input->keyboard, LV_OBJ_FLAG_HIDDEN);
}
void ui_text_input_hide(ui_text_input_t *input) {
  if (input && input->textarea)
    lv_obj_add_flag(input->textarea, LV_OBJ_FLAG_HIDDEN);
  if (input && input->eye_btn)
    lv_obj_add_flag(input->eye_btn, LV_OBJ_FLAG_HIDDEN);
  if (input && input->keyboard)
    lv_obj_add_flag(input->keyboard, LV_OBJ_FLAG_HIDDEN);
}
void ui_text_input_destroy(ui_text_input_t *input) {
  if (!input)
    return;
  if (input->input_group)
    lv_group_del(input->input_group);
  if (input->keyboard)
    lv_obj_del(input->keyboard);
  if (input->eye_btn)
    lv_obj_del(input->eye_btn);
  if (input->textarea)
    lv_obj_del(input->textarea);
  memset(input, 0, sizeof(*input));
}
void ui_keyboard_apply_safe_text_map(lv_obj_t *keyboard) { (void)keyboard; }
void ui_textarea_enable_safe_keyboard_shortcuts(lv_obj_t *textarea) {
  (void)textarea;
}
void ui_textarea_keep_cursor_visible(lv_obj_t *textarea) {
  if (textarea)
    lv_obj_invalidate(textarea);
}
lv_obj_t *ui_create_back_button(lv_obj_t *parent, lv_event_cb_t event_cb) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_size(btn, theme_get_corner_button_width(),
                  theme_get_corner_button_height());
  lv_obj_align(btn, LV_ALIGN_TOP_LEFT, theme_get_small_padding(),
               theme_get_small_padding());
  sim_style_black_orange_box(btn, 8);
  lv_obj_set_style_pad_all(btn, 4, 0);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_FLOATING);
  lv_obj_move_foreground(btn);
  if (event_cb)
    lv_obj_add_event_cb(btn, event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *label = lv_label_create(btn);
  lv_label_set_text(label, "返回");
  lv_obj_set_width(label, LV_PCT(100));
  lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(label);
  return btn;
}
lv_obj_t *ui_create_home_button(lv_obj_t *parent, lv_event_cb_t event_cb) {
  (void)event_cb;
  return lv_btn_create(parent);
}
lv_obj_t *ui_create_power_button(lv_obj_t *parent, lv_event_cb_t event_cb) {
  (void)event_cb;
  return lv_btn_create(parent);
}
lv_obj_t *ui_create_settings_button(lv_obj_t *parent,
                                    lv_event_cb_t event_cb) {
  (void)event_cb;
  return lv_btn_create(parent);
}
void ui_power_off_confirmed_cb(bool confirmed, void *user_data) {
  (void)confirmed;
  (void)user_data;
}

static int sim_web3_estimate_lines(const char *text, int columns) {
  if (!text || !text[0])
    return 1;
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

static int sim_web3_field_height(const char *title, const char *value,
                                 bool wrap) {
  const int pad = theme_get_small_padding();
  const int content_w = sim_max_i(160, theme_get_screen_width() -
                                           theme_get_default_padding() * 2 -
                                           pad * 6 - 24);
  const int columns = sim_max_i(18, content_w / 10);
  const int line_h = theme_font_small() ? theme_font_small()->line_height + 5
                                       : 25;
  const int value_lines =
      wrap ? sim_web3_estimate_lines(value ? value : "-", columns) : 1;
  const int title_lines = sim_web3_estimate_lines(title, columns);
  const int min_h = wrap ? sim_max_i(92, theme_get_min_touch_size() + pad * 3)
                         : sim_max_i(68, theme_get_min_touch_size() + pad);
  return sim_max_i(min_h, pad * 2 + title_lines * line_h +
                              sim_max_i(8, pad) + value_lines * line_h + 10);
}

static void sim_web3_add_field(lv_obj_t *parent, const char *title,
                               const char *value, bool wrap) {
  lv_obj_t *box = lv_obj_create(parent);
  lv_obj_set_width(box, LV_PCT(100));
  lv_obj_set_height(box, sim_web3_field_height(title, value, wrap));
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);
  lv_obj_set_style_bg_color(box, bg_color(), 0);
  lv_obj_set_style_bg_opa(box, LV_OPA_50, 0);
  lv_obj_set_style_border_color(box, lv_color_hex(0x343434), 0);
  lv_obj_set_style_border_width(box, 1, 0);
  lv_obj_set_style_radius(box, 10, 0);
  lv_obj_set_style_pad_all(box, theme_get_small_padding(), 0);
  lv_obj_set_style_pad_gap(box, sim_max_i(8, theme_get_small_padding()), 0);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *label = theme_create_label(box, title, true);
  lv_obj_set_width(label, LV_PCT(100));
  lv_obj_set_height(label, LV_SIZE_CONTENT);
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_color(label, highlight_color(), 0);
  lv_obj_set_style_text_font(label, theme_font_small(), 0);

  lv_obj_t *text = theme_create_label(box, value ? value : "-", false);
  lv_obj_set_width(text, LV_PCT(100));
  lv_obj_set_height(text, LV_SIZE_CONTENT);
  lv_label_set_long_mode(text, wrap ? LV_LABEL_LONG_WRAP : LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_font(text, theme_font_small(), 0);
  lv_obj_set_style_text_line_space(text, 5, 0);
}

static lv_obj_t *sim_web3_review_root(void) {
  lv_obj_t *root = lv_screen_active();
  lv_obj_clean(root);
  theme_apply_screen(root);
  lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(root, theme_get_default_padding(), 0);
  lv_obj_set_style_pad_gap(root, theme_get_small_padding(), 0);

  lv_obj_t *header = lv_obj_create(root);
  lv_obj_set_width(header, LV_PCT(100));
  lv_obj_set_height(header, sim_max_i(theme_get_corner_button_height() +
                                          theme_get_small_padding() * 3,
                                      76));
  theme_apply_transparent_container(header);
  lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = theme_create_label(header, "确认签名", false);
  lv_obj_set_width(title, LV_PCT(62));
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(title, theme_font_medium(), 0);
  lv_obj_center(title);
  (void)ui_create_back_button(root, NULL);

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
  lv_obj_set_style_margin_top(card, theme_get_small_padding(), 0);
  return card;
}

static void sim_web3_add_button_row(lv_obj_t *root) {
  lv_obj_t *row = lv_obj_create(root);
  lv_obj_set_width(row, LV_PCT(100));
  lv_obj_set_height(row, theme_get_min_touch_size() + theme_get_small_padding());
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_style_pad_gap(row, theme_get_small_padding(), 0);

  lv_obj_t *btn = theme_create_button(row, "签名", true);
  lv_obj_set_width(btn, LV_PCT(44));
  lv_obj_t *cancel = theme_create_button(row, "返回", false);
  lv_obj_set_width(cancel, LV_PCT(44));
}

static lv_obj_t *sim_btc_review_root(void) {
  lv_obj_t *root = lv_screen_active();
  lv_obj_clean(root);
  theme_apply_screen(root);
  lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(root, theme_get_default_padding(), 0);
  lv_obj_set_style_pad_gap(root, theme_get_default_padding(), 0);

  lv_obj_t *card = lv_obj_create(root);
  lv_obj_set_width(card, LV_PCT(100));
  lv_obj_set_height(card, 0);
  lv_obj_set_flex_grow(card, 1);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_scroll_dir(card, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_add_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  theme_apply_screen(card);
  lv_obj_set_style_pad_top(card, 10, 0);
  lv_obj_set_style_pad_left(card, 10, 0);
  lv_obj_set_style_pad_right(card, 10, 0);
  lv_obj_set_style_pad_bottom(card, 10 + theme_get_min_touch_size(), 0);
  lv_obj_set_style_pad_gap(card, 10, 0);
  return card;
}

static void sim_btc_add_label(lv_obj_t *parent, const char *text,
                              lv_color_t color, bool strong) {
  lv_obj_t *label = theme_create_label(parent, text, strong);
  lv_obj_set_width(label, LV_PCT(100));
  lv_obj_set_height(label, LV_SIZE_CONTENT);
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_color(label, color, 0);
}

static void sim_btc_add_separator(lv_obj_t *parent) {
  lv_obj_t *line = lv_obj_create(parent);
  lv_obj_set_size(line, LV_PCT(100), 2);
  lv_obj_set_style_bg_color(line, main_color(), 0);
  lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(line, 0, 0);
}

static void sim_btc_add_button_row(lv_obj_t *parent) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_set_width(row, LV_PCT(100));
  lv_obj_set_height(row, theme_get_min_touch_size() + theme_get_small_padding());
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_style_pad_gap(row, 10, 0);

  lv_obj_t *back = theme_create_button(row, "返回", false);
  lv_obj_set_width(back, LV_PCT(45));
  lv_obj_t *sign = theme_create_button(row, "签名", true);
  lv_obj_set_width(sign, LV_PCT(45));
}

void scan_simulator_show_btc_psbt_review(void) {
  lv_obj_t *card = sim_btc_review_root();
  sim_btc_add_label(card, "PSBT 审核", main_color(), true);
  sim_btc_add_label(card, "输入 (1): 2.00 000 000 BTC", main_color(), false);
  sim_btc_add_separator(card);

  sim_btc_add_label(card, "外部输出", highlight_color(), true);
  sim_btc_add_label(card, "输出 0: 0.50 000 000 BTC", main_color(), false);
  sim_btc_add_label(card,
                    "bc1qzqsrqszsvpcgpy9qkrqdpc83qygjzv6q8d7w5k",
                    highlight_color(), false);
  sim_btc_add_label(card, "输出 1: 0.40 000 000 BTC", main_color(), false);
  sim_btc_add_label(card,
                    "bc1qxq6rxve5xmnrd7gt3xys5vsk2xp8x9q0vm6m8t",
                    highlight_color(), false);
  sim_btc_add_label(card, "输出 2: 0.30 000 000 BTC", main_color(), false);
  sim_btc_add_label(card,
                    "bc1q5cyxnuxmeuwuvkwfem96llyxf5zx4dcnqfr4s6",
                    highlight_color(), false);
  sim_btc_add_label(card, "输出 3: 0.25 000 000 BTC", main_color(), false);
  sim_btc_add_label(card,
                    "bc1q9zpgru5z0t6w7h4fxx67x0yxj2d3gz57g9xq0m",
                    highlight_color(), false);
  sim_btc_add_label(card, "输出 4: 0.20 000 000 BTC", main_color(), false);
  sim_btc_add_label(card,
                    "bc1q0x6zyg4q4z9v0h6xq9cvk7vq9a0z5tc3hl4hpk",
                    highlight_color(), false);
  sim_btc_add_label(card, "输出 5: 0.15 000 000 BTC", main_color(), false);
  sim_btc_add_label(card,
                    "bc1q7m3l32egzr6za2j73gtm63h4crn5tczrqgd0s5",
                    highlight_color(), false);
  sim_btc_add_label(card, "输出 6: 0.10 000 000 BTC", main_color(), false);
  sim_btc_add_label(card,
                    "bc1q2n0w4gt64f8ph407u3u3v9d42v8fq4f9qdu4rx",
                    highlight_color(), false);

  sim_btc_add_separator(card);
  sim_btc_add_label(card, "手续费: 0.10 000 000 BTC", error_color(), false);
  sim_btc_add_button_row(card);
}

void scan_simulator_show_web3_tx_review(void) {
  lv_obj_t *card = sim_web3_review_root();
  sim_web3_add_field(card, "钱包", "OKX", false);
  sim_web3_add_field(card, "动作", "转账", false);
  sim_web3_add_field(card, "链", "Ethereum (1)", false);
  sim_web3_add_field(card, "收款/合约",
                     "0x3535353535353535353535353535353535353535", true);
  sim_web3_add_field(card, "金额", "1 ETH", false);
  sim_web3_add_field(card, "路径", "m/44'/60'/0'/0/0", true);
  sim_web3_add_field(card, "地址",
                     "0x1234567890abcdef1234567890abcdef12345678", true);
  sim_web3_add_button_row(lv_screen_active());
}

void scan_simulator_show_web3_typed_review(void) {
  lv_obj_t *card = sim_web3_review_root();
  sim_web3_add_field(card, "钱包", "Bitget", false);
  sim_web3_add_field(card, "类型", "DApp 结构化签名", false);
  sim_web3_add_field(card, "主要类型", "Mail", false);
  sim_web3_add_field(card, "DApp", "KernSigner Test", true);
  sim_web3_add_field(card, "链", "Ethereum (1)", false);
  sim_web3_add_field(card, "合约",
                     "0xCcCCccccCCCCcCCCCCCcCcCccCcCCCcCcccccccC", true);
  sim_web3_add_field(card, "发送方",
                     "0x1111111111111111111111111111111111111111", true);
  sim_web3_add_field(card, "接收方",
                     "0x2222222222222222222222222222222222222222", true);
  sim_web3_add_field(card, "内容", "确认 DApp 授权测试", true);
  sim_web3_add_button_row(lv_screen_active());
}

bool evm_get_address(char *address_out, size_t address_out_len) {
  fill_string(address_out, address_out_len,
              "0x0000000000000000000000000000000000000000");
  return true;
}
void evm_keccak256(const uint8_t *input, size_t input_len, uint8_t out[32]) {
  (void)input;
  (void)input_len;
  memset(out, 0, 32);
}
bool evm_address_from_uncompressed_pubkey(const uint8_t pubkey[65],
                                          char *address_out,
                                          size_t address_out_len) {
  (void)pubkey;
  fill_string(address_out, address_out_len,
              "0x0000000000000000000000000000000000000000");
  return true;
}
bool evm_web3_build_connect_qr(evm_web3_profile_t profile,
                               evm_web3_qr_bundle_t *bundle_out) {
  if (!bundle_out)
    return false;
  memset(bundle_out, 0, sizeof(*bundle_out));
  fill_string(bundle_out->address, sizeof(bundle_out->address),
              "0x0000000000000000000000000000000000000000");
  fill_string(bundle_out->summary, sizeof(bundle_out->summary),
              profile == EVM_WEB3_PROFILE_ADDRESS ? "地址" : "连接码");
  bundle_out->pages[0] = strdup("ethereum:0x0000000000000000000000000000000000000000");
  bundle_out->page_count = 1;
  return bundle_out->pages[0] != NULL;
}
bool evm_web3_build_external_connect_qr(
    evm_web3_profile_t profile, const evm_web3_external_account_t *account,
    evm_web3_qr_bundle_t *bundle_out) {
  (void)profile;
  (void)account;
  return evm_web3_build_connect_qr(EVM_WEB3_PROFILE_ADDRESS, bundle_out);
}
void evm_web3_qr_bundle_clear(evm_web3_qr_bundle_t *bundle) {
  if (!bundle)
    return;
  free(bundle->pages[0]);
  memset(bundle, 0, sizeof(*bundle));
}

esp_err_t smartcard_satochip_read_status(smartcard_satochip_status_t *out,
                                         uint32_t timeout_ms) {
  (void)timeout_ms;
  if (out) {
    memset(out, 0, sizeof(*out));
    out->status_valid = true;
    fill_string(out->detail, sizeof(out->detail), "模拟器占位");
  }
  return ESP_OK;
}
void smartcard_satochip_format_status(const smartcard_satochip_status_t *status,
                                      char *out, size_t out_len) {
  (void)status;
  fill_string(out, out_len, "模拟器占位");
}
esp_err_t smartcard_satochip_get_label(smartcard_satochip_label_t *out,
                                       uint32_t timeout_ms) {
  (void)timeout_ms;
  if (out) {
    memset(out, 0, sizeof(*out));
    fill_string(out->label, sizeof(out->label), "模拟器");
    fill_string(out->detail, sizeof(out->detail), "模拟器占位");
    out->has_label = true;
  }
  return ESP_OK;
}
void smartcard_satochip_format_label(const smartcard_satochip_label_t *label,
                                     char *out, size_t out_len) {
  (void)label;
  fill_string(out, out_len, "模拟器");
}
esp_err_t smartcard_seedkeeper_read_status(const char *pin,
                                           smartcard_seedkeeper_status_t *out,
                                           uint32_t timeout_ms) {
  (void)pin;
  (void)timeout_ms;
  if (out)
    memset(out, 0, sizeof(*out));
  return ESP_OK;
}
void smartcard_seedkeeper_format_status(
    const smartcard_seedkeeper_status_t *status, char *out, size_t out_len) {
  (void)status;
  fill_string(out, out_len, "模拟器");
}
esp_err_t smartcard_satochip_get_eth_account(const char *pin, const char *path,
                                             smartcard_satochip_account_t *out,
                                             uint32_t timeout_ms) {
  (void)pin; (void)path; (void)timeout_ms;
  if (out) {
    memset(out, 0, sizeof(*out));
    out->has_address = true;
    fill_string(out->address, sizeof(out->address),
                "0x0000000000000000000000000000000000000000");
    fill_string(out->detail, sizeof(out->detail), "模拟器智能卡地址");
  }
  return ESP_OK;
}
esp_err_t smartcard_satochip_get_web3_account(
    const char *pin, smartcard_satochip_web3_account_t *out,
    uint32_t timeout_ms) {
  (void)pin; (void)timeout_ms;
  if (out) memset(out, 0, sizeof(*out));
  return ESP_OK;
}
esp_err_t smartcard_satochip_sign_evm_digest(
    const char *pin, const char *path, const uint8_t digest[32],
    smartcard_satochip_signature_t *out, uint32_t timeout_ms) {
  (void)pin; (void)path; (void)digest; (void)timeout_ms;
  if (out) memset(out, 0, sizeof(*out));
  return ESP_OK;
}
esp_err_t smartcard_satochip_get_btc_xpub(
    const char *pin, const char *path, const char *xtype, bool is_testnet,
    smartcard_satochip_btc_xpub_t *out, uint32_t timeout_ms) {
  (void)pin; (void)path; (void)xtype; (void)is_testnet; (void)timeout_ms;
  if (out) memset(out, 0, sizeof(*out));
  return ESP_OK;
}
esp_err_t smartcard_satochip_get_btc_address(
    const char *pin, const char *path, smartcard_satochip_btc_script_t script,
    bool is_testnet, smartcard_satochip_btc_address_t *out,
    uint32_t timeout_ms) {
  (void)pin; (void)path; (void)script; (void)is_testnet; (void)timeout_ms;
  if (out) {
    memset(out, 0, sizeof(*out));
    out->has_address = true;
    fill_string(out->address, sizeof(out->address),
                "bc1q000000000000000000000000000000000000000");
    fill_string(out->detail, sizeof(out->detail), "模拟器智能卡地址");
  }
  return ESP_OK;
}
esp_err_t smartcard_satochip_card_set_label(
    const char *pin, const char *label, smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms) {
  (void)pin; (void)label; (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_satochip_card_set_nfc_policy(
    const char *pin, uint8_t policy,
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms) {
  (void)pin; (void)policy; (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_satochip_card_set_feature_policy(
    const char *pin, uint8_t feature_id, uint8_t policy,
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms) {
  (void)pin; (void)feature_id; (void)policy; (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_satochip_card_reset_factory_signal(
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms) {
  (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_satochip_card_setup_pin(
    const char *new_pin, smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms) {
  (void)new_pin;
  (void)timeout_ms;
  if (out) {
    memset(out, 0, sizeof(*out));
    out->sw = 0x9000;
    fill_string(out->detail, sizeof(out->detail), "模拟器设置 Satochip PIN");
  }
  return ESP_OK;
}
esp_err_t smartcard_satochip_card_import_mnemonic_seed(
    const char *pin, const char *mnemonic, const char *passphrase,
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms) {
  (void)pin;
  (void)mnemonic;
  (void)passphrase;
  (void)timeout_ms;
  if (out) {
    memset(out, 0, sizeof(*out));
    out->sw = 0x9000;
    fill_string(out->detail, sizeof(out->detail), "模拟器写入 Satochip");
  }
  return ESP_OK;
}
esp_err_t smartcard_seedkeeper_reset_factory_signal(
    const char *pin, smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms) {
  (void)pin;
  (void)timeout_ms;
  if (out) memset(out, 0, sizeof(*out));
  return ESP_OK;
}
esp_err_t smartcard_satochip_card_reset_seed(
    const char *pin, const uint8_t *hmac, size_t hmac_len,
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms) {
  (void)pin; (void)hmac; (void)hmac_len; (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_satochip_card_change_pin(
    uint8_t pin_nbr, const char *old_pin, const char *new_pin,
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms) {
  (void)pin_nbr; (void)old_pin; (void)new_pin; (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_seedkeeper_change_pin(
    uint8_t pin_nbr, const char *old_pin, const char *new_pin,
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms) {
  (void)pin_nbr; (void)old_pin; (void)new_pin; (void)timeout_ms;
  if (out) memset(out, 0, sizeof(*out));
  return ESP_OK;
}
esp_err_t smartcard_seedkeeper_reset_wrong_pin_step(
    const char *wrong_pin, smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms) {
  (void)wrong_pin;
  (void)timeout_ms;
  if (out) {
    memset(out, 0, sizeof(*out));
    out->sw = 0x63C0;
    fill_string(out->detail, sizeof(out->detail), "模拟器 SeedKeeper 错 PIN");
  }
  return ESP_OK;
}
esp_err_t smartcard_seedkeeper_reset_wrong_puk_step(
    const char *wrong_puk, smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms) {
  (void)wrong_puk;
  (void)timeout_ms;
  if (out) {
    memset(out, 0, sizeof(*out));
    out->sw = 0x63C0;
    fill_string(out->detail, sizeof(out->detail), "模拟器 SeedKeeper 错 PUK");
  }
  return ESP_OK;
}
esp_err_t smartcard_satochip_card_unblock_pin(
    uint8_t pin_nbr, const uint8_t *puk, size_t puk_len,
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms) {
  (void)pin_nbr; (void)puk; (void)puk_len; (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_satochip_card_set_2fa_key(
    const uint8_t *hmacsha160_key, size_t key_len, uint64_t amount_limit,
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms) {
  (void)hmacsha160_key; (void)key_len; (void)amount_limit; (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_satochip_card_reset_2fa_key(
    const uint8_t *chalresponse, size_t chal_len,
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms) {
  (void)chalresponse; (void)chal_len; (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_satochip_card_export_perso_certificate(
    smartcard_satochip_certificate_t *out, uint32_t timeout_ms) {
  (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_satochip_card_import_perso_certificate(
    const char *cert_text, smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms) {
  (void)cert_text; (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_satochip_card_import_ndef_authentikey(
    const uint8_t privkey[32], smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms) {
  (void)privkey; (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_satochip_card_export_authentikey(
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms) {
  (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_satochip_card_import_trusted_pubkey(
    const uint8_t pubkey[65], smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms) {
  (void)pubkey; (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_satochip_card_export_trusted_pubkey(
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms) {
  (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_satochip_card_challenge_response_pki(
    const uint8_t challenge_from_host[32], smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms) {
  (void)challenge_from_host; (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_satochip_card_logout_all(
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms) {
  (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_satochip_card_verify_authenticity(
    smartcard_satochip_authenticity_t *out, uint32_t timeout_ms) {
  (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_seedkeeper_list_secret_headers(
    const char *pin, smartcard_seedkeeper_header_list_t *out,
    uint32_t timeout_ms) {
  (void)pin; (void)timeout_ms;
  if (out) {
    memset(out, 0, sizeof(*out));
    out->count = 2;
    out->headers[0].id = 1;
    out->headers[0].type = 0x30;
    out->headers[0].export_rights = 0x01;
    memcpy(out->headers[0].fingerprint, "\x12\x34\x56\x78", 4);
    out->headers[1].id = 2;
    out->headers[1].type = 0xC0;
    out->headers[1].subtype = 0x02;
    out->headers[1].export_rights = 0x01;
    memcpy(out->headers[1].fingerprint, "\xAB\xCD\xEF\x01", 4);
    snprintf(out->headers[1].label, sizeof(out->headers[1].label),
             "模拟密码组");
    out->sw = 0x9000;
    fill_string(out->detail, sizeof(out->detail), "模拟器 SeedKeeper 列表");
  }
  return ESP_OK;
}
esp_err_t smartcard_seedkeeper_generate_masterseed(
    const char *pin, uint8_t seed_size, uint8_t export_rights,
    const char *label, smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms) {
  (void)pin; (void)seed_size; (void)export_rights; (void)label; (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_seedkeeper_generate_2fa_secret(
    const char *pin, uint8_t export_rights, const char *label,
    smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms) {
  (void)pin; (void)export_rights; (void)label; (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_seedkeeper_generate_random_secret(
    const char *pin, uint8_t stype, uint8_t subtype, uint8_t size,
    uint8_t export_rights, const char *label, bool save_entropy,
    const uint8_t *entropy,
    size_t entropy_len, smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms) {
  (void)pin; (void)stype; (void)subtype; (void)size; (void)export_rights; (void)label; (void)save_entropy; (void)entropy; (void)entropy_len; (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_seedkeeper_derive_master_password(
    const char *pin, const char *salt, uint16_t sid, uint16_t sid_pubkey,
    bool has_sid_pubkey, smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms) {
  (void)pin; (void)salt; (void)sid; (void)sid_pubkey; (void)has_sid_pubkey; (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_seedkeeper_import_secret(
    const char *pin, const uint8_t *header, size_t header_len,
    const uint8_t *secret, size_t secret_len, uint16_t sid_pubkey,
    const uint8_t *iv, size_t iv_len, const uint8_t *hmac, size_t hmac_len,
    bool secure_import,
    smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms) {
  (void)pin; (void)header; (void)header_len; (void)secret; (void)secret_len; (void)sid_pubkey; (void)iv; (void)iv_len; (void)hmac; (void)hmac_len; (void)secure_import; (void)timeout_ms;
  if (out) {
    memset(out, 0, sizeof(*out));
    out->sw = 0x9000;
    out->response_len = 6;
    uint16_t sid = (header_len >= 3 && header[2] == 0xC0) ? 2 : 1;
    out->response[0] = (uint8_t)(sid >> 8);
    out->response[1] = (uint8_t)sid;
    if (header_len >= 12) {
      memcpy(out->response + 2, header + 8, 4);
    } else {
      memcpy(out->response + 2, "\x12\x34\x56\x78", 4);
    }
    fill_string(out->detail, sizeof(out->detail),
                "导入成功：模拟器 SeedKeeper 条目。");
  }
  return ESP_OK;
}
esp_err_t smartcard_seedkeeper_export_secret(
    const char *pin, uint16_t sid, uint16_t sid_pubkey, bool secure_export,
    smartcard_satochip_apdu_result_t *out, uint32_t timeout_ms) {
  (void)pin; (void)sid_pubkey; (void)secure_export; (void)timeout_ms;
  if (out) {
    if (sid == 2) {
      static const char secret[] =
          "user:alice\npass:correct horse battery staple";
      const char label[] = "模拟密码组";
      size_t label_len = strlen(label);
      size_t text_len = strlen(secret);
      memset(out, 0, sizeof(*out));
      out->sw = 0x9000;
      size_t pos = 0;
      out->response[pos++] = 0x00;
      out->response[pos++] = 0x02;
      out->response[pos++] = 0xC0;
      out->response[pos++] = 0x01;
      out->response[pos++] = 0x01;
      out->response[pos++] = 0x00;
      out->response[pos++] = 0x00;
      out->response[pos++] = 0x00;
      out->response[pos++] = 0xAB;
      out->response[pos++] = 0xCD;
      out->response[pos++] = 0xEF;
      out->response[pos++] = 0x01;
      out->response[pos++] = 0x02;
      out->response[pos++] = 0x00;
      out->response[pos++] = (uint8_t)label_len;
      memcpy(out->response + pos, label, label_len);
      pos += label_len;
      size_t secret_len = text_len + 2;
      out->response[pos++] = (uint8_t)(secret_len >> 8);
      out->response[pos++] = (uint8_t)secret_len;
      out->response[pos++] = (uint8_t)(text_len >> 8);
      out->response[pos++] = (uint8_t)text_len;
      memcpy(out->response + pos, secret, text_len);
      pos += text_len;
      out->response_len = pos;
      fill_string(out->detail, sizeof(out->detail),
                  "导出完成：模拟器普通 Secret。");
      return ESP_OK;
    }
    static const char mnemonic[] =
        "abandon abandon abandon abandon abandon abandon abandon abandon "
        "abandon abandon abandon about";
    const char label[] = "";
    size_t label_len = 0;
    size_t mnemonic_len = strlen(mnemonic);
    memset(out, 0, sizeof(*out));
    out->sw = 0x9000;
    size_t pos = 0;
    out->response[pos++] = 0x00;
    out->response[pos++] = 0x01;
    out->response[pos++] = 0x30;
    out->response[pos++] = 0x01;
    out->response[pos++] = 0x01;
    out->response[pos++] = 0x00;
    out->response[pos++] = 0x00;
    out->response[pos++] = 0x00;
    out->response[pos++] = 0x12;
    out->response[pos++] = 0x34;
    out->response[pos++] = 0x56;
    out->response[pos++] = 0x78;
    out->response[pos++] = 0x00;
    out->response[pos++] = 0x00;
    out->response[pos++] = (uint8_t)label_len;
    memcpy(out->response + pos, label, label_len);
    pos += label_len;
    size_t secret_len = mnemonic_len + 2;
    out->response[pos++] = (uint8_t)(secret_len >> 8);
    out->response[pos++] = (uint8_t)secret_len;
    out->response[pos++] = (uint8_t)mnemonic_len;
    memcpy(out->response + pos, mnemonic, mnemonic_len);
    pos += mnemonic_len;
    out->response[pos++] = 0x00;
    out->response_len = pos;
    fill_string(out->detail, sizeof(out->detail),
                "导出完成：KernSigner 模拟助记词。");
  }
  return ESP_OK;
}
esp_err_t smartcard_seedkeeper_export_secret_to_satochip(
    const char *pin, uint16_t sid, uint16_t sid_pubkey,
    smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms) {
  (void)pin; (void)sid; (void)sid_pubkey; (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_seedkeeper_reset_secret(
    const char *pin, uint16_t sid, smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms) {
  (void)pin; (void)sid; (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
esp_err_t smartcard_seedkeeper_setup_pin(
    const char *new_pin, smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms) {
  (void)new_pin;
  (void)timeout_ms;
  if (out) {
    memset(out, 0, sizeof(*out));
    out->sw = 0x9000;
    fill_string(out->detail, sizeof(out->detail), "模拟器设置 SeedKeeper PIN");
  }
  return ESP_OK;
}
esp_err_t smartcard_seedkeeper_print_logs(
    const char *pin, smartcard_satochip_apdu_result_t *out,
    uint32_t timeout_ms) {
  (void)pin; (void)timeout_ms; if (out) memset(out, 0, sizeof(*out)); return ESP_OK;
}
