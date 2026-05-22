#include "mnemonic_grid.h"
#include "../../../core/key.h"
#include "../../../ui/dialog.h"
#include "../../../ui/input_helpers.h"
#include "../../../ui/theme.h"
#include "../../../utils/bip39_filter.h"

#include <lvgl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static lv_obj_t *grid_screen = NULL;
static void (*return_callback)(void) = NULL;

static const int COLUMN_WEIGHTS[11] = {1,   2,   4,   8,   16,  32,
                                       64,  128, 256, 512, 1024};

static const int WORD_LABEL_WIDTH = 28;

static void back_btn_cb(lv_event_t *e) {
  (void)e;
  if (return_callback)
    return_callback();
}

static lv_obj_t *create_cell(lv_obj_t *parent, int w, int h) {
  lv_obj_t *cell = lv_obj_create(parent);
  lv_obj_set_size(cell, w, h);
  lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(cell, 2, 0);
  lv_obj_set_style_border_color(cell, highlight_color(), 0);
  lv_obj_set_style_radius(cell, 0, 0);
  lv_obj_set_style_pad_all(cell, 0, 0);
  lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
  return cell;
}

static void add_dot_cell(lv_obj_t *row, bool filled, int size) {
  lv_obj_t *cell = create_cell(row, size, size);
  lv_obj_set_style_bg_color(cell, filled ? highlight_color() : bg_color(), 0);
  lv_obj_set_style_border_color(cell, highlight_color(), 0);
  lv_obj_set_style_border_width(cell, 2, 0);
}

static lv_obj_t *add_text_cell(lv_obj_t *row, const char *text, int w, int h,
                               lv_color_t color) {
  lv_obj_t *cell = create_cell(row, w, h);
  lv_obj_set_style_bg_color(cell, bg_color(), 0);
  lv_obj_set_style_border_color(cell, highlight_color(), 0);
  lv_obj_t *label = lv_label_create(cell);
  lv_label_set_text(label, text ? text : "");
  lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
  lv_obj_set_width(label, w);
  lv_obj_set_style_text_font(label, theme_font_small(), 0);
  lv_obj_set_style_text_color(label, color, 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(label);
  return cell;
}

static lv_obj_t *create_grid_row(lv_obj_t *parent) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_set_width(row, LV_SIZE_CONTENT);
  lv_obj_set_height(row, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_style_pad_gap(row, 0, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  return row;
}

static void add_header_row(lv_obj_t *list, int dot_size, bool weights) {
  lv_obj_t *row = create_grid_row(list);
  add_text_cell(row, weights ? "#" : "", WORD_LABEL_WIDTH, dot_size,
                secondary_color());
  for (int col = 0; col < 11; col++) {
    char text[8];
    snprintf(text, sizeof(text), weights ? "%d" : "%d",
             weights ? COLUMN_WEIGHTS[col] : col + 1);
    add_text_cell(row, text, dot_size, dot_size,
                  weights ? highlight_color() : secondary_color());
  }
}

static void add_word_row(lv_obj_t *list, size_t position, const char *word,
                         int dot_size) {
  int index = bip39_filter_get_word_index(word);
  if (index < 0)
    return;

  lv_obj_t *row = create_grid_row(list);
  char prefix[8];
  snprintf(prefix, sizeof(prefix), "%02u", (unsigned)(position + 1));
  add_text_cell(row, prefix, WORD_LABEL_WIDTH, dot_size, main_color());

  for (int col = 0; col < 11; col++)
    add_dot_cell(row, (index & COLUMN_WEIGHTS[col]) != 0, dot_size);
}

void mnemonic_grid_page_create(lv_obj_t *parent, void (*return_cb)(void)) {
  if (!parent || !key_is_loaded())
    return;

  return_callback = return_cb;
  if (!key_mnemonic_is_valid()) {
    dialog_show_error("临时助记词不能显示点阵板", return_cb, 0);
    return;
  }
  if (!bip39_filter_init())
    return;

  char **words = NULL;
  size_t word_count = 0;
  if (!key_get_mnemonic_words(&words, &word_count))
    return;

  grid_screen = theme_create_page_container(parent);
  lv_obj_set_style_bg_color(grid_screen, bg_color(), 0);
  lv_obj_set_style_bg_opa(grid_screen, LV_OPA_COVER, 0);
  theme_create_page_title(grid_screen, "点阵板");
  (void)ui_create_back_button(grid_screen, back_btn_cb);

  lv_obj_t *list = lv_obj_create(grid_screen);
  lv_obj_set_width(list, LV_PCT(100));
  lv_obj_set_height(list, LV_PCT(88));
  lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(list, bg_color(), 0);
  lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_radius(list, 0, 0);
  lv_obj_set_style_pad_all(list, 0, 0);
  lv_obj_set_style_pad_gap(list, 0, 0);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(list, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);

  int screen_w = theme_get_screen_width();
  int dot_size = (screen_w - WORD_LABEL_WIDTH) / 11;
  if (dot_size > 42)
    dot_size = 42;
  if (dot_size < 20)
    dot_size = 20;
  add_header_row(list, dot_size, true);
  add_header_row(list, dot_size, false);
  for (size_t i = 0; i < word_count; i++)
    add_word_row(list, i, words[i], dot_size);

  key_free_mnemonic_words(words, word_count);
}

void mnemonic_grid_page_show(void) {
  if (grid_screen)
    lv_obj_clear_flag(grid_screen, LV_OBJ_FLAG_HIDDEN);
}

void mnemonic_grid_page_hide(void) {
  if (grid_screen)
    lv_obj_add_flag(grid_screen, LV_OBJ_FLAG_HIDDEN);
}

void mnemonic_grid_page_destroy(void) {
  if (grid_screen) {
    lv_obj_del(grid_screen);
    grid_screen = NULL;
  }
  return_callback = NULL;
}
