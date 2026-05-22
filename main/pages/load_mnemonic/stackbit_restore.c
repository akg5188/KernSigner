#include "stackbit_restore.h"

#include "../../core/mnemonic_tools.h"
#include "../../ui/dialog.h"
#include "../../ui/input_helpers.h"
#include "../../ui/menu.h"
#include "../../ui/theme.h"
#include "../../utils/bip39_filter.h"
#include "../../utils/secure_mem.h"
#include "../shared/key_confirmation.h"

#include <lvgl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wally_bip39.h>

#define STACKBIT_MAX_WORDS 24

static lv_obj_t *page_screen = NULL;
static lv_obj_t *back_btn = NULL;
static lv_obj_t *title_label = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *word_card = NULL;
static lv_obj_t *digits_label = NULL;
static lv_obj_t *word_label = NULL;
static lv_obj_t *digit_grid = NULL;
static lv_obj_t *action_matrix = NULL;
static ui_menu_t *word_count_menu = NULL;

static void (*return_callback)(void) = NULL;
static void (*success_callback)(void) = NULL;

static int total_words = 0;
static int current_word_index = 0;
static uint16_t entered_indices[STACKBIT_MAX_WORDS] = {0};
static int current_digits[4] = {0, 0, 0, 0};
static bool page_ready = false;

static const char *ACTION_MAP[] = {"上个", "清零", "\n", "确认", ""};
static const int STACKBIT_TOP_LABELS[7] = {1, 1, 2, 1, 2, 1, 2};
static const int STACKBIT_BOTTOM_LABELS[7] = {2, 4, 8, 4, 8, 4, 8};
static const int STACKBIT_CELL_MAP[14][2] = {
    {0, 0}, {1, 0}, {1, 1}, {2, 0}, {2, 1}, {3, 0}, {3, 1},
    {0, 1}, {1, 2}, {1, 3}, {2, 2}, {2, 3}, {3, 2}, {3, 3}};
static lv_obj_t *digit_buttons[14] = {0};
static lv_obj_t *digit_labels[14] = {0};

static void render_word_editor(void);
static void update_word_preview(void);
static void cleanup_word_editor(void);
static void abandon_confirm_cb(bool confirmed, void *user_data);

static void back_to_parent(void) {
  stackbit_restore_page_hide();
  stackbit_restore_page_destroy();
  if (return_callback)
    return_callback();
}

static void show_error(const char *message) { dialog_show_error(message, NULL, 0); }

static void word_count_back_cb(void) { back_to_parent(); }

static void destroy_word_count_menu(void) {
  if (word_count_menu) {
    ui_menu_destroy(word_count_menu);
    word_count_menu = NULL;
  }
}

static void cleanup_word_editor(void) {
  if (action_matrix) {
    lv_obj_del(action_matrix);
    action_matrix = NULL;
  }
  if (digit_grid) {
    lv_obj_del(digit_grid);
    digit_grid = NULL;
  }
  memset(digit_buttons, 0, sizeof(digit_buttons));
  memset(digit_labels, 0, sizeof(digit_labels));
  if (word_label) {
    lv_obj_del(word_label);
    word_label = NULL;
  }
  if (digits_label) {
    lv_obj_del(digits_label);
    digits_label = NULL;
  }
  if (word_card) {
    lv_obj_del(word_card);
    word_card = NULL;
  }
  if (status_label) {
    lv_obj_del(status_label);
    status_label = NULL;
  }
  if (title_label) {
    lv_obj_del(title_label);
    title_label = NULL;
  }
}

static void go_back_event(lv_event_t *e) {
  (void)e;
  if (!page_ready) {
    back_to_parent();
    return;
  }
  if (current_word_index > 0 || current_digits[0] || current_digits[1] ||
      current_digits[2] || current_digits[3]) {
    dialog_show_confirm("放弃恢复？", abandon_confirm_cb, NULL,
                        DIALOG_STYLE_OVERLAY);
  } else {
    back_to_parent();
  }
}

static void abandon_confirm_cb(bool confirmed, void *user_data) {
  (void)user_data;
  if (confirmed)
    back_to_parent();
}

static int current_index_value(void) {
  return current_digits[0] * 1000 + current_digits[1] * 100 + current_digits[2] * 10 +
         current_digits[3];
}

static void finish_restore(void) {
  char *mnemonic =
      mnemonic_tools_from_indices(entered_indices, (size_t)total_words);
  if (!mnemonic) {
    show_error("序号无效");
    return;
  }

  key_confirmation_page_create(lv_screen_active(), back_to_parent,
                               success_callback, mnemonic, strlen(mnemonic));
  key_confirmation_page_show();
  SECURE_FREE_STRING(mnemonic);
  stackbit_restore_page_hide();
}

static void update_word_preview(void) {
  if (!status_label || !digits_label || !word_label)
    return;

  int idx = current_index_value();
  struct words *wordlist = NULL;
  const char *word = NULL;
  if (bip39_filter_init() && bip39_get_wordlist(NULL, &wordlist) == WALLY_OK &&
      wordlist && idx >= 0 && idx <= 2047)
    word = bip39_get_word_by_index(wordlist, (size_t)idx);

  char status[48];
  snprintf(status, sizeof(status), "词 %d/%d", current_word_index + 1,
           total_words);
  lv_label_set_text(status_label, status);

  char digits_text[32];
  snprintf(digits_text, sizeof(digits_text), "序号：%d%d%d%d", current_digits[0],
           current_digits[1], current_digits[2], current_digits[3]);
  lv_label_set_text(digits_label, digits_text);

  char word_text[80];
  snprintf(word_text, sizeof(word_text), "词：%s", word ? word : "未知");
  lv_label_set_text(word_label, word_text);

  for (int i = 0; i < 14; i++) {
    if (!digit_buttons[i])
      continue;
    int digit = STACKBIT_CELL_MAP[i][0];
    int bit = STACKBIT_CELL_MAP[i][1];
    bool active = (current_digits[digit] & (1 << bit)) != 0;
    lv_obj_set_style_bg_color(digit_buttons[i],
                              active ? highlight_color() : bg_color(), 0);
    lv_obj_set_style_border_color(digit_buttons[i], highlight_color(), 0);
    if (digit_labels[i]) {
      lv_obj_set_style_text_color(
          digit_labels[i], active ? bg_color() : main_color(), 0);
    }
  }
}

static void digit_toggle_event(lv_event_t *e) {
  int cell = (int)(intptr_t)lv_event_get_user_data(e);
  if (cell < 0 || cell >= 14)
    return;
  int digit = STACKBIT_CELL_MAP[cell][0];
  int bit = STACKBIT_CELL_MAP[cell][1];
  current_digits[digit] ^= (1 << bit);
  if (digit == 0 && current_digits[digit] > 2)
    current_digits[digit] = 1 << bit;
  if (digit != 0 && current_digits[digit] > 9)
    current_digits[digit] = 1 << bit;
  update_word_preview();
}

static void action_event(lv_event_t *e) {
  lv_obj_t *obj = lv_event_get_target(e);
  uint32_t id = lv_btnmatrix_get_selected_btn(obj);
  const char *txt = lv_btnmatrix_get_btn_text(obj, id);
  if (!txt)
    return;

  if (strcmp(txt, "清零") == 0) {
    memset(current_digits, 0, sizeof(current_digits));
    update_word_preview();
    return;
  }
  if (strcmp(txt, "上个") == 0) {
    if (current_word_index <= 0)
      return;
    current_word_index--;
    int index = (int)entered_indices[current_word_index];
    current_digits[0] = (index / 1000) % 10;
    current_digits[1] = (index / 100) % 10;
    current_digits[2] = (index / 10) % 10;
    current_digits[3] = index % 10;
    update_word_preview();
    return;
  }
  if (strcmp(txt, "确认") != 0)
    return;

  int index = current_index_value();
  if (index < 0 || index > 2047) {
    show_error("序号超出 0-2047");
    return;
  }

  entered_indices[current_word_index] = (uint16_t)index;
  current_word_index++;
  if (current_word_index >= total_words) {
    finish_restore();
    return;
  }
  memset(current_digits, 0, sizeof(current_digits));
  update_word_preview();
}

static lv_obj_t *create_clean_row(lv_obj_t *parent) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_set_width(row, LV_SIZE_CONTENT);
  lv_obj_set_height(row, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_style_pad_gap(row, 5, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  return row;
}

static void create_label_cell(lv_obj_t *parent, const char *text, int width) {
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, text);
  lv_obj_set_width(label, width);
  lv_obj_set_style_text_font(label, theme_font_small(), 0);
  lv_obj_set_style_text_color(label, secondary_color(), 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
}

static void create_digit_button(lv_obj_t *parent, int value, int idx) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_size(btn, 52, 38);
  lv_obj_set_style_radius(btn, 8, 0);
  lv_obj_set_style_bg_color(btn, bg_color(), 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(btn, 2, 0);
  lv_obj_set_style_border_color(btn, highlight_color(), 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_add_event_cb(btn, digit_toggle_event, LV_EVENT_CLICKED,
                      (void *)(intptr_t)idx);
  digit_buttons[idx] = btn;

  char text[2];
  snprintf(text, sizeof(text), "%d", value);
  lv_obj_t *label = lv_label_create(btn);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, theme_font_small(), 0);
  lv_obj_set_style_text_color(label, main_color(), 0);
  lv_obj_center(label);
  digit_labels[idx] = label;
}

static void render_word_editor(void) {
  cleanup_word_editor();
  destroy_word_count_menu();
  page_ready = true;

  title_label = theme_create_page_title(page_screen, "1248打孔板");

  status_label = theme_create_label(page_screen, "", false);
  lv_obj_set_style_text_font(status_label, theme_font_medium(), 0);
  lv_obj_set_style_text_color(status_label, highlight_color(), 0);
  lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, 88);

  word_card = lv_obj_create(page_screen);
  lv_obj_set_size(word_card, LV_PCT(90), 108);
  lv_obj_align(word_card, LV_ALIGN_TOP_MID, 0, 124);
  lv_obj_set_style_bg_color(word_card, bg_color(), 0);
  lv_obj_set_style_bg_opa(word_card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(word_card, highlight_color(), 0);
  lv_obj_set_style_border_width(word_card, 2, 0);
  lv_obj_set_style_radius(word_card, 8, 0);
  lv_obj_set_style_pad_all(word_card, theme_get_default_padding(), 0);
  lv_obj_set_flex_flow(word_card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(word_card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  digits_label = theme_create_label(word_card, "", false);
  lv_obj_set_style_text_font(digits_label, theme_font_medium(), 0);
  lv_obj_set_style_text_color(digits_label, main_color(), 0);

  word_label = theme_create_label(word_card, "", false);
  lv_obj_set_width(word_label, LV_PCT(100));
  lv_label_set_long_mode(word_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(word_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(word_label, highlight_color(), 0);

  digit_grid = lv_obj_create(page_screen);
  lv_obj_set_size(digit_grid, LV_PCT(92), 222);
  lv_obj_align(digit_grid, LV_ALIGN_TOP_MID, 0, 256);
  lv_obj_set_style_bg_color(digit_grid, bg_color(), 0);
  lv_obj_set_style_bg_opa(digit_grid, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(digit_grid, highlight_color(), 0);
  lv_obj_set_style_border_width(digit_grid, 2, 0);
  lv_obj_set_style_radius(digit_grid, 8, 0);
  lv_obj_set_style_pad_all(digit_grid, 12, 0);
  lv_obj_set_style_pad_gap(digit_grid, 6, 0);
  lv_obj_set_flex_flow(digit_grid, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(digit_grid, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *group_row = create_clean_row(digit_grid);
  create_label_cell(group_row, "千", 52);
  create_label_cell(group_row, "百", 109);
  create_label_cell(group_row, "十", 109);
  create_label_cell(group_row, "个", 109);

  lv_obj_t *top_row = create_clean_row(digit_grid);
  for (int col = 0; col < 7; col++)
    create_digit_button(top_row, STACKBIT_TOP_LABELS[col], col);

  lv_obj_t *bottom_row = create_clean_row(digit_grid);
  for (int col = 0; col < 7; col++)
    create_digit_button(bottom_row, STACKBIT_BOTTOM_LABELS[col], 7 + col);

  action_matrix = lv_btnmatrix_create(page_screen);
  lv_btnmatrix_set_map(action_matrix, ACTION_MAP);
  lv_obj_set_size(action_matrix, LV_PCT(92), 112);
  lv_obj_align(action_matrix, LV_ALIGN_BOTTOM_MID, 0, -12);
  theme_apply_btnmatrix(action_matrix);
  lv_obj_add_event_cb(action_matrix, action_event, LV_EVENT_VALUE_CHANGED, NULL);

  update_word_preview();
}

static void start_with_word_count(int word_count) {
  total_words = word_count;
  current_word_index = 0;
  memset(entered_indices, 0, sizeof(entered_indices));
  memset(current_digits, 0, sizeof(current_digits));
  render_word_editor();
}

static void select_12_cb(void) { start_with_word_count(12); }
static void select_15_cb(void) { start_with_word_count(15); }
static void select_18_cb(void) { start_with_word_count(18); }
static void select_21_cb(void) { start_with_word_count(21); }
static void select_24_cb(void) { start_with_word_count(24); }

static void create_word_count_menu(void) {
  cleanup_word_editor();
  destroy_word_count_menu();
  page_ready = false;

  title_label = theme_create_page_title(page_screen, "1248打孔板");

  word_count_menu = ui_menu_create(page_screen, "词数", word_count_back_cb);
  if (!word_count_menu)
    return;
  ui_menu_add_entry(word_count_menu, "12词", select_12_cb);
  ui_menu_add_entry(word_count_menu, "15词", select_15_cb);
  ui_menu_add_entry(word_count_menu, "18词", select_18_cb);
  ui_menu_add_entry(word_count_menu, "21词", select_21_cb);
  ui_menu_add_entry(word_count_menu, "24词", select_24_cb);
  ui_menu_show(word_count_menu);
}

void stackbit_restore_page_create(lv_obj_t *parent, void (*return_cb)(void),
                                  void (*success_cb)(void)) {
  if (!parent)
    return;

  return_callback = return_cb;
  success_callback = success_cb;
  bip39_filter_init();

  page_screen = theme_create_page_container(parent);
  back_btn = ui_create_back_button(page_screen, go_back_event);
  create_word_count_menu();
}

void stackbit_restore_page_show(void) {
  if (page_screen)
    lv_obj_clear_flag(page_screen, LV_OBJ_FLAG_HIDDEN);
}

void stackbit_restore_page_hide(void) {
  if (page_screen)
    lv_obj_add_flag(page_screen, LV_OBJ_FLAG_HIDDEN);
}

void stackbit_restore_page_destroy(void) {
  destroy_word_count_menu();
  cleanup_word_editor();
  if (back_btn) {
    lv_obj_del(back_btn);
    back_btn = NULL;
  }
  if (page_screen) {
    lv_obj_del(page_screen);
    page_screen = NULL;
  }
  return_callback = NULL;
  success_callback = NULL;
  total_words = 0;
  current_word_index = 0;
  page_ready = false;
  secure_memzero(entered_indices, sizeof(entered_indices));
  secure_memzero(current_digits, sizeof(current_digits));
}
