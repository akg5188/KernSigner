#include "mnemonic_entropy.h"
#include "../../../core/key.h"
#include "../../../qr/encoder.h"
#include "../../../ui/input_helpers.h"
#include "../../../ui/theme.h"
#include "../../../utils/secure_mem.h"

#include <lvgl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wally_bip39.h>

static lv_obj_t *entropy_screen = NULL;
static void (*return_callback)(void) = NULL;

static void back_cb(lv_event_t *e) {
  (void)e;
  if (return_callback)
    return_callback();
}

static void append_hex_spaced(char *dst, size_t dst_len, const uint8_t *src,
                              size_t src_len) {
  size_t pos = 0;
  for (size_t i = 0; i < src_len && pos + 3 < dst_len; i++) {
    int written = snprintf(dst + pos, dst_len - pos, "%s%02x", i ? " " : "",
                           (unsigned)src[i]);
    if (written <= 0)
      break;
    pos += (size_t)written;
  }
}

static void add_text_qr(lv_obj_t *parent, const char *text) {
  if (!parent || !text || text[0] == '\0')
    return;

  int32_t qr_size = theme_get_screen_width() <= 520 ? 206 : 280;
  lv_obj_t *qr_box = lv_obj_create(parent);
  lv_obj_set_size(qr_box, qr_size, qr_size);
  lv_obj_set_style_bg_color(qr_box, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(qr_box, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(qr_box, 0, 0);
  lv_obj_set_style_pad_all(qr_box, 12, 0);
  lv_obj_set_style_radius(qr_box, 0, 0);
  lv_obj_clear_flag(qr_box, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *qr = lv_qrcode_create(qr_box);
  lv_qrcode_set_size(qr, qr_size - 24);
  (void)qr_update_optimal(qr, text, NULL);
  lv_obj_center(qr);
}

void mnemonic_entropy_page_create(lv_obj_t *parent, void (*return_cb)(void)) {
  if (!parent || !key_is_loaded() || !key_mnemonic_is_valid())
    return;

  return_callback = return_cb;

  char *mnemonic = NULL;
  if (!key_get_mnemonic(&mnemonic) || !mnemonic)
    return;

  uint8_t entropy[32] = {0};
  size_t entropy_len = 0;
  if (bip39_mnemonic_to_bytes(NULL, mnemonic, entropy, sizeof(entropy),
                              &entropy_len) != WALLY_OK ||
      entropy_len == 0) {
    SECURE_FREE_STRING(mnemonic);
    return;
  }

  entropy_screen = theme_create_page_container(parent);
  lv_obj_add_flag(entropy_screen, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(entropy_screen, back_cb, LV_EVENT_CLICKED, NULL);
  theme_create_page_title(entropy_screen, "原始熵");
  (void)ui_create_back_button(entropy_screen, back_cb);

  char fingerprint_hex[9] = "--------";
  key_get_fingerprint_hex(fingerprint_hex);
  char fingerprint_text[32];
  snprintf(fingerprint_text, sizeof(fingerprint_text), "指纹 %s",
           fingerprint_hex);

  lv_obj_t *card = lv_obj_create(entropy_screen);
  lv_obj_set_size(card, LV_PCT(92), LV_PCT(80));
  lv_obj_align(card, LV_ALIGN_BOTTOM_MID, 0, -theme_get_small_padding());
  lv_obj_set_style_bg_color(card, panel_color(), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_80, 0);
  lv_obj_set_style_border_color(card, highlight_color(), 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_radius(card, 14, 0);
  lv_obj_set_style_pad_all(card, theme_get_default_padding(), 0);
  lv_obj_set_style_pad_gap(card, theme_get_small_padding(), 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  lv_obj_t *fingerprint =
      theme_create_label(card, fingerprint_text, false);
  lv_obj_set_style_text_font(fingerprint, theme_font_small(), 0);
  lv_obj_set_style_text_color(fingerprint, highlight_color(), 0);
  lv_obj_set_style_text_align(fingerprint, LV_TEXT_ALIGN_CENTER, 0);

  char *source_label = NULL;
  char *source_text = NULL;
  bool has_source = key_get_source_material(&source_label, &source_text);

  char bits_text[32];
  snprintf(bits_text, sizeof(bits_text), "熵：%u位",
           (unsigned)(entropy_len * 8));
  lv_obj_t *bits_label = theme_create_label(card, bits_text, false);
  lv_obj_set_style_text_font(bits_label, theme_font_small(), 0);
  lv_obj_set_style_text_color(bits_label, secondary_color(), 0);
  lv_obj_set_style_text_align(bits_label, LV_TEXT_ALIGN_CENTER, 0);

  if (has_source) {
    char source_title[64];
    snprintf(source_title, sizeof(source_title), "原始输入：%s",
             source_label ? source_label : "");
    lv_obj_t *source_title_label =
        theme_create_label(card, source_title, false);
    lv_obj_set_style_text_font(source_title_label, theme_font_small(), 0);
    lv_obj_set_style_text_color(source_title_label, highlight_color(), 0);
    lv_obj_set_style_text_align(source_title_label, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *source_value = theme_create_label(card, source_text, false);
    lv_obj_set_width(source_value, LV_PCT(100));
    lv_label_set_long_mode(source_value, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(source_value, theme_font_small(), 0);
    lv_obj_set_style_text_color(source_value, main_color(), 0);
    lv_obj_set_style_text_align(source_value, LV_TEXT_ALIGN_CENTER, 0);

    add_text_qr(card, source_text);
  }

  char entropy_text[160] = {0};
  append_hex_spaced(entropy_text, sizeof(entropy_text), entropy, entropy_len);
  lv_obj_t *entropy_label = theme_create_label(card, entropy_text, false);
  lv_obj_set_width(entropy_label, LV_PCT(100));
  lv_label_set_long_mode(entropy_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(entropy_label, theme_font_medium(), 0);
  lv_obj_set_style_text_color(entropy_label, main_color(), 0);
  lv_obj_set_style_text_align(entropy_label, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_t *raw_label = theme_create_label(card, "十六进制", false);
  lv_obj_set_style_text_font(raw_label, theme_font_small(), 0);
  lv_obj_set_style_text_color(raw_label, highlight_color(), 0);

  char compact_hex[96] = {0};
  size_t hex_pos = 0;
  for (size_t i = 0; i < entropy_len && hex_pos + 2 < sizeof(compact_hex); i++) {
    int written = snprintf(compact_hex + hex_pos, sizeof(compact_hex) - hex_pos,
                           "%02x", (unsigned)entropy[i]);
    if (written <= 0)
      break;
    hex_pos += (size_t)written;
  }

  lv_obj_t *compact_label = theme_create_label(card, compact_hex, false);
  lv_obj_set_width(compact_label, LV_PCT(100));
  lv_label_set_long_mode(compact_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(compact_label, theme_font_small(), 0);
  lv_obj_set_style_text_color(compact_label, main_color(), 0);
  lv_obj_set_style_text_align(compact_label, LV_TEXT_ALIGN_CENTER, 0);

  if (!has_source)
    add_text_qr(card, compact_hex);

  SECURE_FREE_STRING(source_label);
  SECURE_FREE_STRING(source_text);
  secure_memzero(entropy, sizeof(entropy));
  SECURE_FREE_STRING(mnemonic);
}

void mnemonic_entropy_page_show(void) {
  if (entropy_screen)
    lv_obj_clear_flag(entropy_screen, LV_OBJ_FLAG_HIDDEN);
}

void mnemonic_entropy_page_hide(void) {
  if (entropy_screen)
    lv_obj_add_flag(entropy_screen, LV_OBJ_FLAG_HIDDEN);
}

void mnemonic_entropy_page_destroy(void) {
  if (entropy_screen) {
    lv_obj_del(entropy_screen);
    entropy_screen = NULL;
  }
  return_callback = NULL;
}
