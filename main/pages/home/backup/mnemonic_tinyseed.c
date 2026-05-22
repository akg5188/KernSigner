#include "mnemonic_tinyseed.h"
#include "../../../core/key.h"
#include "../../../ui/input_helpers.h"
#include "../../../ui/theme.h"
#include "../../../utils/bip39_filter.h"

#include <lvgl.h>
#include <stdio.h>
#include <stdlib.h>

static lv_obj_t *tinyseed_screen = NULL;
static void (*return_callback)(void) = NULL;
static const int COLUMN_WEIGHTS[11] = {1,   2,   4,   8,   16,  32,
                                       64,  128, 256, 512, 1024};
static const int WORD_LABEL_WIDTH = 30;

static void back_cb(lv_event_t *e) {
  (void)e;
  if (return_callback)
    return_callback();
}

static lv_obj_t *create_cell(lv_obj_t *parent, int w, int h) {
  lv_obj_t *cell = lv_obj_create(parent);
  lv_obj_set_size(cell, w, h);
  lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(cell, 0, 0);
  lv_obj_set_style_pad_all(cell, 0, 0);
  lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
  return cell;
}

static void add_square(lv_obj_t *row, bool filled, int size) {
  lv_obj_t *cell = create_cell(row, size, size);
  lv_obj_t *dot = lv_obj_create(cell);
  int dot_size = size > 24 ? size - 6 : size - 4;
  if (dot_size < 10)
    dot_size = 10;
  lv_obj_set_size(dot, dot_size, dot_size);
  lv_obj_set_style_radius(dot, 4, 0);
  lv_obj_set_style_border_color(dot, highlight_color(), 0);
  lv_obj_set_style_border_width(dot, 2, 0);
  lv_obj_set_style_bg_color(dot, filled ? highlight_color() : bg_color(), 0);
  lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
  lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_center(dot);
}

static lv_obj_t *add_text_cell(lv_obj_t *row, const char *text, int w, int h,
                               lv_color_t color) {
  lv_obj_t *cell = create_cell(row, w, h);
  lv_obj_t *label = lv_label_create(cell);
  lv_label_set_text(label, text ? text : "");
  lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
  lv_obj_set_width(label, w);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(label, color, 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(label);
  return cell;
}

static lv_obj_t *create_grid_row(lv_obj_t *list) {
  lv_obj_t *row = lv_obj_create(list);
  lv_obj_set_width(row, LV_SIZE_CONTENT);
  lv_obj_set_height(row, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_style_pad_gap(row, 4, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  return row;
}

static void add_header_row(lv_obj_t *list, int cell_size, bool weights) {
  lv_obj_t *row = create_grid_row(list);
  add_text_cell(row, weights ? "词" : "", WORD_LABEL_WIDTH, cell_size,
                secondary_color());
  for (int col = 0; col < 11; col++) {
    char text[8];
    snprintf(text, sizeof(text), "%d", weights ? COLUMN_WEIGHTS[col] : col + 1);
    add_text_cell(row, text, cell_size, cell_size,
                  weights ? highlight_color() : secondary_color());
  }
}

void mnemonic_tinyseed_page_create(lv_obj_t *parent, void (*return_cb)(void)) {
  if (!parent || !key_is_loaded())
    return;

  return_callback = return_cb;
  if (!bip39_filter_init())
    return;

  char **words = NULL;
  size_t word_count = 0;
  if (!key_get_mnemonic_words(&words, &word_count))
    return;

  tinyseed_screen = theme_create_page_container(parent);
  theme_create_page_title(tinyseed_screen, "点阵板");
  (void)ui_create_back_button(tinyseed_screen, back_cb);

  lv_obj_t *list = lv_obj_create(tinyseed_screen);
  lv_obj_set_width(list, LV_PCT(100));
  lv_obj_set_height(list, LV_PCT(84));
  lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, -theme_get_small_padding());
  lv_obj_set_style_bg_color(list, bg_color(), 0);
  lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(list, highlight_color(), 0);
  lv_obj_set_style_border_width(list, 2, 0);
  lv_obj_set_style_radius(list, 8, 0);
  lv_obj_set_style_pad_all(list, 8, 0);
  lv_obj_set_style_pad_gap(list, 4, 0);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(list, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  int screen_w = theme_get_screen_width();
  int cell_size = screen_w >= 700 ? 38 : (screen_w >= 460 ? 33 : 20);

  add_header_row(list, cell_size, true);
  add_header_row(list, cell_size, false);

  for (size_t i = 0; i < word_count; i++) {
    int index = bip39_filter_get_word_index(words[i]);
    if (index < 0)
      continue;

    lv_obj_t *row = create_grid_row(list);
    char prefix[8];
    snprintf(prefix, sizeof(prefix), "%02u", (unsigned)(i + 1));
    add_text_cell(row, prefix, WORD_LABEL_WIDTH, cell_size, main_color());

    for (int col = 0; col < 11; col++)
      add_square(row, (index & COLUMN_WEIGHTS[col]) != 0, cell_size);
  }

  key_free_mnemonic_words(words, word_count);
}

void mnemonic_tinyseed_page_show(void) {
  if (tinyseed_screen)
    lv_obj_clear_flag(tinyseed_screen, LV_OBJ_FLAG_HIDDEN);
}

void mnemonic_tinyseed_page_hide(void) {
  if (tinyseed_screen)
    lv_obj_add_flag(tinyseed_screen, LV_OBJ_FLAG_HIDDEN);
}

void mnemonic_tinyseed_page_destroy(void) {
  if (tinyseed_screen) {
    lv_obj_del(tinyseed_screen);
    tinyseed_screen = NULL;
  }
  return_callback = NULL;
}
