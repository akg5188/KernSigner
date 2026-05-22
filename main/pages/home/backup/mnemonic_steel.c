#include "mnemonic_steel.h"
#include "../../../core/key.h"
#include "../../../ui/dialog.h"
#include "../../../ui/input_helpers.h"
#include "../../../ui/theme.h"
#include "../../../utils/bip39_filter.h"

#include <lvgl.h>
#include <stdio.h>
#include <stdlib.h>

static lv_obj_t *steel_screen = NULL;
static void (*return_callback)(void) = NULL;
static const int COLUMN_WEIGHTS[11] = {1,   2,   4,   8,   16,  32,
                                       64,  128, 256, 512, 1024};

static void back_cb(lv_event_t *e) {
  (void)e;
  if (return_callback)
    return_callback();
}

static void format_weights(int index, char *out, size_t out_len) {
  if (!out || out_len == 0)
    return;
  out[0] = '\0';
  size_t pos = 0;
  for (int i = 0; i < 11; i++) {
    if ((index & COLUMN_WEIGHTS[i]) == 0)
      continue;
    int written = snprintf(out + pos, out_len - pos, "%s%d", pos ? " " : "",
                           COLUMN_WEIGHTS[i]);
    if (written <= 0 || (size_t)written >= out_len - pos)
      break;
    pos += (size_t)written;
  }
  if (out[0] == '\0')
    snprintf(out, out_len, "0");
}

void mnemonic_steel_page_create(lv_obj_t *parent, void (*return_cb)(void)) {
  if (!parent || !key_is_loaded())
    return;

  return_callback = return_cb;
  if (!key_mnemonic_is_valid()) {
    dialog_show_error("临时助记词不能显示钢板打孔", return_cb, 0);
    return;
  }
  if (!bip39_filter_init())
    return;

  char **words = NULL;
  size_t word_count = 0;
  if (!key_get_mnemonic_words(&words, &word_count))
    return;

  steel_screen = theme_create_page_container(parent);
  theme_create_page_title(steel_screen, "钢板打孔位置");
  (void)ui_create_back_button(steel_screen, back_cb);

  lv_obj_t *list = lv_obj_create(steel_screen);
  lv_obj_set_size(list, LV_PCT(94), LV_PCT(82));
  lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, -theme_get_small_padding());
  lv_obj_set_style_bg_color(list, bg_color(), 0);
  lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(list, highlight_color(), 0);
  lv_obj_set_style_border_width(list, 2, 0);
  lv_obj_set_style_radius(list, 8, 0);
  lv_obj_set_style_pad_all(list, theme_get_small_padding(), 0);
  lv_obj_set_style_pad_gap(list, theme_get_small_padding(), 0);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  for (size_t i = 0; i < word_count; i++) {
    int index = bip39_filter_get_word_index(words[i]);
    if (index < 0)
      continue;

    lv_obj_t *card = lv_obj_create(list);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, bg_color(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, highlight_color(), 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, theme_get_small_padding(), 0);
    lv_obj_set_style_pad_gap(card, 4, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    char title[64];
    snprintf(title, sizeof(title), "%02u. %04d  %s", (unsigned)(i + 1), index,
             words[i]);
    lv_obj_t *title_label = theme_create_label(card, title, false);
    lv_obj_set_style_text_font(title_label, theme_font_small(), 0);
    lv_obj_set_style_text_color(title_label, highlight_color(), 0);

    char weights[96];
    format_weights(index, weights, sizeof(weights));
    lv_obj_t *weights_label = theme_create_label(card, weights, false);
    lv_obj_set_width(weights_label, LV_PCT(100));
    lv_label_set_long_mode(weights_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(weights_label, theme_font_small(), 0);
    lv_obj_set_style_text_color(weights_label, main_color(), 0);
    lv_obj_set_style_text_align(weights_label, LV_TEXT_ALIGN_LEFT, 0);
  }

  key_free_mnemonic_words(words, word_count);
}

void mnemonic_steel_page_show(void) {
  if (steel_screen)
    lv_obj_clear_flag(steel_screen, LV_OBJ_FLAG_HIDDEN);
}

void mnemonic_steel_page_hide(void) {
  if (steel_screen)
    lv_obj_add_flag(steel_screen, LV_OBJ_FLAG_HIDDEN);
}

void mnemonic_steel_page_destroy(void) {
  if (steel_screen) {
    lv_obj_del(steel_screen);
    steel_screen = NULL;
  }
  return_callback = NULL;
}
