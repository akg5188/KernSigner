#include "mnemonic_1248.h"
#include "../../../core/key.h"
#include "../../../i18n/i18n.h"
#include "../../../ui/dialog.h"
#include "../../../ui/input_helpers.h"
#include "../../../ui/theme.h"
#include "../../../utils/bip39_filter.h"

#include <lvgl.h>
#include <stdio.h>
#include <stdlib.h>

static lv_obj_t *stack_screen = NULL;
static void (*return_callback)(void) = NULL;

#define STACKBIT_CELL_W 42
#define STACKBIT_CELL_H 28
#define STACKBIT_CELL_GAP 5

static const int STACKBIT_TOP_LABELS[7] = {1, 1, 2, 1, 2, 1, 2};
static const int STACKBIT_BOTTOM_LABELS[7] = {2, 4, 8, 4, 8, 4, 8};

static void back_cb(lv_event_t *e) {
  (void)e;
  if (return_callback)
    return_callback();
}

static void split_digits(int index, int digits[4]) {
  digits[0] = (index / 1000) % 10;
  digits[1] = (index / 100) % 10;
  digits[2] = (index / 10) % 10;
  digits[3] = index % 10;
}

static lv_obj_t *create_clean_row(lv_obj_t *parent) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_set_width(row, LV_SIZE_CONTENT);
  lv_obj_set_height(row, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_style_pad_gap(row, STACKBIT_CELL_GAP, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  return row;
}

static bool is_stackbit_cell_active(const int digits[4], int col,
                                    bool bottom_row) {
  if (col == 0) {
    int value = bottom_row ? 2 : 1;
    return digits[0] == value;
  }

  int digit_index = 1 + (col - 1) / 2;
  int pair_col = (col - 1) % 2;
  int value = 0;
  if (!bottom_row)
    value = pair_col == 0 ? 1 : 2;
  else
    value = pair_col == 0 ? 4 : 8;
  return (digits[digit_index] & value) != 0;
}

static void create_label_cell(lv_obj_t *parent, const char *text, int width,
                              lv_color_t color) {
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, text);
  lv_obj_set_width(label, width);
  lv_obj_set_style_text_font(label, theme_font_small(), 0);
  lv_obj_set_style_text_color(label, color, 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
}

static void create_punch_cell(lv_obj_t *parent, int value, bool active) {
  lv_obj_t *cell = lv_obj_create(parent);
  lv_obj_set_size(cell, STACKBIT_CELL_W, STACKBIT_CELL_H);
  lv_obj_set_style_bg_color(cell, active ? highlight_color() : bg_color(), 0);
  lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(cell, highlight_color(), 0);
  lv_obj_set_style_border_width(cell, 2, 0);
  lv_obj_set_style_radius(cell, 8, 0);
  lv_obj_set_style_pad_all(cell, 0, 0);
  lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);

  char text[2];
  snprintf(text, sizeof(text), "%d", value);
  lv_obj_t *label = lv_label_create(cell);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, theme_font_small(), 0);
  lv_obj_set_style_text_color(label,
                              active ? bg_color() : main_color(), 0);
  lv_obj_center(label);
}

static void create_stackbit_grid(lv_obj_t *parent, const int digits[4]) {
  lv_obj_t *grid = lv_obj_create(parent);
  lv_obj_set_width(grid, LV_SIZE_CONTENT);
  lv_obj_set_height(grid, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(grid, 0, 0);
  lv_obj_set_style_pad_all(grid, 0, 0);
  lv_obj_set_style_pad_gap(grid, 2, 0);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *group_row = create_clean_row(grid);
  create_label_cell(group_row,
                    i18n_tr_or("backup.digit_thousands", "Thou"),
                    STACKBIT_CELL_W, secondary_color());
  create_label_cell(group_row,
                    i18n_tr_or("backup.digit_hundreds", "Hund"),
                    STACKBIT_CELL_W * 2 + STACKBIT_CELL_GAP, secondary_color());
  create_label_cell(group_row, i18n_tr_or("backup.digit_tens", "Tens"),
                    STACKBIT_CELL_W * 2 + STACKBIT_CELL_GAP, secondary_color());
  create_label_cell(group_row, i18n_tr_or("backup.digit_ones", "Ones"),
                    STACKBIT_CELL_W * 2 + STACKBIT_CELL_GAP, secondary_color());

  lv_obj_t *top_row = create_clean_row(grid);
  for (int col = 0; col < 7; col++) {
    create_punch_cell(top_row, STACKBIT_TOP_LABELS[col],
                      is_stackbit_cell_active(digits, col, false));
  }

  lv_obj_t *bottom_row = create_clean_row(grid);
  for (int col = 0; col < 7; col++) {
    create_punch_cell(bottom_row, STACKBIT_BOTTOM_LABELS[col],
                      is_stackbit_cell_active(digits, col, true));
  }
}

void mnemonic_1248_page_create(lv_obj_t *parent, void (*return_cb)(void)) {
  if (!parent || !key_is_loaded())
    return;

  return_callback = return_cb;
  if (!key_mnemonic_is_valid()) {
    dialog_show_error(
        i18n_tr_or("backup.no_temporary_1248_punch",
                   "Temporary mnemonic cannot show 1248 punch backup"),
        return_cb, 0);
    return;
  }
  if (!bip39_filter_init())
    return;

  char **words = NULL;
  size_t word_count = 0;
  if (!key_get_mnemonic_words(&words, &word_count))
    return;

  stack_screen = theme_create_page_container(parent);
  theme_create_page_title(stack_screen,
                          i18n_tr_or("backup.1248_punch_board",
                                     "1248 punch board"));
  (void)ui_create_back_button(stack_screen, back_cb);

  lv_obj_t *list = lv_obj_create(stack_screen);
  lv_obj_set_size(list, LV_PCT(94), LV_PCT(84));
  lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, -theme_get_small_padding());
  lv_obj_set_style_bg_color(list, bg_color(), 0);
  lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(list, highlight_color(), 0);
  lv_obj_set_style_border_width(list, 2, 0);
  lv_obj_set_style_radius(list, 8, 0);
  lv_obj_set_style_pad_all(list, 8, 0);
  lv_obj_set_style_pad_gap(list, 6, 0);
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

    int digits[4];
    split_digits(index, digits);

    create_stackbit_grid(card, digits);
  }

  key_free_mnemonic_words(words, word_count);
}

void mnemonic_1248_page_show(void) {
  if (stack_screen)
    lv_obj_clear_flag(stack_screen, LV_OBJ_FLAG_HIDDEN);
}

void mnemonic_1248_page_hide(void) {
  if (stack_screen)
    lv_obj_add_flag(stack_screen, LV_OBJ_FLAG_HIDDEN);
}

void mnemonic_1248_page_destroy(void) {
  if (stack_screen) {
    lv_obj_del(stack_screen);
    stack_screen = NULL;
  }
  return_callback = NULL;
}
