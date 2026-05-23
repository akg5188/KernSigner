#include "tinyseed_restore.h"

#include "../../core/mnemonic_tools.h"
#include "../../i18n/i18n.h"
#include "../../ui/dialog.h"
#include "../../ui/input_helpers.h"
#include "../../ui/theme.h"
#include "../../ui/word_selector.h"
#include "../../utils/bip39_filter.h"
#include "../../utils/secure_mem.h"
#include "../shared/key_confirmation.h"

#include <lvgl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wally_bip39.h>

#define TINYSEED_COLUMN_COUNT 11
#define TINYSEED_MAX_WORDS 24
#define ACTION_BTN_PREVIOUS 0
#define ACTION_BTN_CLEAR 1
#define ACTION_BTN_CONFIRM 2

static lv_obj_t *page_screen = NULL;
static lv_obj_t *back_btn = NULL;
static lv_obj_t *title_label = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *word_card = NULL;
static lv_obj_t *index_label = NULL;
static lv_obj_t *word_label = NULL;
static lv_obj_t *grid = NULL;
static lv_obj_t *action_matrix = NULL;
static ui_menu_t *word_count_menu = NULL;

static void (*return_callback)(void) = NULL;
static void (*success_callback)(void) = NULL;

static int total_words = 0;
static int current_word_index = 0;
static uint16_t entered_indices[TINYSEED_MAX_WORDS] = {0};
static uint16_t current_index = 0;
static bool page_ready = false;

static const int COLUMN_WEIGHTS[TINYSEED_COLUMN_COUNT] = {1,   2,   4,   8,
                                                          16,  32,  64,  128,
                                                          256, 512, 1024};
static const char *ACTION_MAP[] = {NULL, NULL, "\n", NULL, ""};

static void render_word_editor(void);
static void update_word_preview(void);
static void cleanup_word_editor(void);
static void abandon_confirm_cb(bool confirmed, void *user_data);

static void update_action_map(void) {
  ACTION_MAP[0] = i18n_tr_or("dialog.previous", "Previous");
  ACTION_MAP[1] = i18n_tr_or("dialog.clear", "Clear");
  ACTION_MAP[3] = i18n_tr_or("common.confirm", "Confirm");
}

static void back_to_parent(void) {
  tinyseed_restore_page_hide();
  tinyseed_restore_page_destroy();
  if (return_callback)
    return_callback();
}

static void show_error(const char *message) {
  dialog_show_error(message, NULL, 0);
}

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
  if (grid) {
    lv_obj_del(grid);
    grid = NULL;
  }
  if (word_label) {
    lv_obj_del(word_label);
    word_label = NULL;
  }
  if (index_label) {
    lv_obj_del(index_label);
    index_label = NULL;
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

  if (current_word_index > 0 || current_index != 0) {
    dialog_show_confirm(i18n_tr_or("input.abandon_restore_confirm",
                                   "Abandon restore?"),
                        abandon_confirm_cb, NULL,
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

static void finish_restore(void) {
  char *mnemonic =
      mnemonic_tools_from_indices(entered_indices, (size_t)total_words);
  if (!mnemonic) {
    show_error(i18n_tr_or("input.invalid_punch_grid", "Invalid punch grid"));
    return;
  }

  key_confirmation_page_create(lv_screen_active(), back_to_parent,
                               success_callback, mnemonic, strlen(mnemonic));
  key_confirmation_page_show();
  SECURE_FREE_STRING(mnemonic);
  tinyseed_restore_page_hide();
}

static void update_word_preview(void) {
  if (!status_label || !index_label || !word_label)
    return;

  if (!bip39_filter_init()) {
    lv_label_set_text(status_label,
                      i18n_tr_or("wallet.wordlist_not_loaded",
                                 "Wordlist not loaded"));
    lv_label_set_text(index_label,
                      i18n_tr_or("input.index_placeholder", "Index: ----"));
    lv_label_set_text(word_label,
                      i18n_tr_or("wallet.word_placeholder", "Word: --"));
    return;
  }

  struct words *wordlist = NULL;
  const char *word = NULL;
  if (bip39_get_wordlist(NULL, &wordlist) == WALLY_OK && wordlist)
    word = bip39_get_word_by_index(wordlist, current_index);

  char status[48];
  snprintf(status, sizeof(status),
           i18n_tr_or("wallet.word_format", "Word %d/%d"),
           current_word_index + 1, total_words);
  lv_label_set_text(status_label, status);

  char index_text[32];
  snprintf(index_text, sizeof(index_text),
           i18n_tr_or("input.index_padded_format", "Index: %04u"),
           (unsigned)current_index);
  lv_label_set_text(index_label, index_text);

  char word_text[80];
  snprintf(word_text, sizeof(word_text),
           i18n_tr_or("wallet.word_label_format", "Word: %s"),
           word ? word : i18n_tr_or("common.unknown", "Unknown"));
  lv_label_set_text(word_label, word_text);

  if (!grid)
    return;

  uint32_t child_count = lv_obj_get_child_cnt(grid);
  for (uint32_t i = 0; i < child_count && i < TINYSEED_COLUMN_COUNT; i++) {
    lv_obj_t *btn = lv_obj_get_child(grid, i);
    bool enabled = (current_index & (uint16_t)COLUMN_WEIGHTS[i]) != 0;
    lv_obj_set_style_bg_color(btn,
                              enabled ? highlight_color() : bg_color(), 0);
    lv_obj_set_style_border_color(btn, highlight_color(), 0);
  }
}

static void toggle_weight_event(lv_event_t *e) {
  lv_obj_t *btn = lv_event_get_target(e);
  int col = (int)(intptr_t)lv_event_get_user_data(e);
  if (!btn || col < 0 || col >= TINYSEED_COLUMN_COUNT)
    return;

  current_index ^= (uint16_t)COLUMN_WEIGHTS[col];
  update_word_preview();
}

static void action_event(lv_event_t *e) {
  lv_obj_t *obj = lv_event_get_target(e);
  uint32_t id = lv_btnmatrix_get_selected_btn(obj);

  if (id == ACTION_BTN_CLEAR) {
    current_index = 0;
    update_word_preview();
    return;
  }

  if (id == ACTION_BTN_PREVIOUS) {
    if (current_word_index <= 0)
      return;
    current_word_index--;
    current_index = entered_indices[current_word_index];
    update_word_preview();
    return;
  }

  if (id != ACTION_BTN_CONFIRM)
    return;

  entered_indices[current_word_index] = current_index;
  current_word_index++;

  if (current_word_index >= total_words) {
    finish_restore();
    return;
  }

  current_index = 0;
  update_word_preview();
}

static lv_obj_t *create_weight_button(lv_obj_t *parent, int col, int size) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_size(btn, size, size);
  lv_obj_set_style_radius(btn, 8, 0);
  lv_obj_set_style_bg_color(btn, bg_color(), 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(btn, 2, 0);
  lv_obj_set_style_border_color(btn, highlight_color(), 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_add_event_cb(btn, toggle_weight_event, LV_EVENT_CLICKED,
                      (void *)(intptr_t)col);

  char weight_label[8];
  snprintf(weight_label, sizeof(weight_label), "%d", col + 1);
  lv_obj_t *label = lv_label_create(btn);
  lv_label_set_text(label, weight_label);
  lv_obj_set_style_text_font(label, theme_font_small(), 0);
  lv_obj_set_style_text_color(label, main_color(), 0);
  lv_obj_center(label);

  return btn;
}

static void render_word_editor(void) {
  cleanup_word_editor();
  destroy_word_count_menu();
  page_ready = true;

  title_label = theme_create_page_title(
      page_screen, i18n_tr_or("input.punch_grid", "Punch Grid"));

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

  index_label = theme_create_label(word_card, "", false);
  lv_obj_set_style_text_font(index_label, theme_font_medium(), 0);
  lv_obj_set_style_text_color(index_label, main_color(), 0);

  word_label = theme_create_label(word_card, "", false);
  lv_obj_set_width(word_label, LV_PCT(100));
  lv_label_set_long_mode(word_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(word_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(word_label, highlight_color(), 0);

  grid = lv_obj_create(page_screen);
  lv_obj_set_size(grid, LV_PCT(92), 222);
  lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 256);
  lv_obj_set_style_bg_color(grid, bg_color(), 0);
  lv_obj_set_style_bg_opa(grid, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(grid, highlight_color(), 0);
  lv_obj_set_style_border_width(grid, 2, 0);
  lv_obj_set_style_radius(grid, 8, 0);
  lv_obj_set_style_pad_all(grid, 12, 0);
  lv_obj_set_style_pad_gap(grid, 8, 0);
  lv_obj_set_layout(grid, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  int screen_w = theme_get_screen_width();
  int cell_size = screen_w >= 460 ? 66 : 52;
  for (int col = 0; col < TINYSEED_COLUMN_COUNT; col++)
    (void)create_weight_button(grid, col, cell_size);

  action_matrix = lv_btnmatrix_create(page_screen);
  update_action_map();
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
  current_index = 0;
  memset(entered_indices, 0, sizeof(entered_indices));
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

  title_label = theme_create_page_title(
      page_screen, i18n_tr_or("input.punch_grid", "Punch Grid"));

  word_count_menu =
      ui_menu_create(page_screen,
                     i18n_tr_or("wallet.word_count", "Word count"),
                     word_count_back_cb);
  if (!word_count_menu)
    return;
  ui_menu_add_entry(word_count_menu, i18n_tr_or("wallet.12_words", "12 words"),
                    select_12_cb);
  ui_menu_add_entry(word_count_menu, i18n_tr_or("wallet.15_words", "15 words"),
                    select_15_cb);
  ui_menu_add_entry(word_count_menu, i18n_tr_or("wallet.18_words", "18 words"),
                    select_18_cb);
  ui_menu_add_entry(word_count_menu, i18n_tr_or("wallet.21_words", "21 words"),
                    select_21_cb);
  ui_menu_add_entry(word_count_menu, i18n_tr_or("wallet.24_words", "24 words"),
                    select_24_cb);
  ui_menu_show(word_count_menu);
}

void tinyseed_restore_page_create(lv_obj_t *parent, void (*return_cb)(void),
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

void tinyseed_restore_page_show(void) {
  if (page_screen)
    lv_obj_clear_flag(page_screen, LV_OBJ_FLAG_HIDDEN);
}

void tinyseed_restore_page_hide(void) {
  if (page_screen)
    lv_obj_add_flag(page_screen, LV_OBJ_FLAG_HIDDEN);
}

void tinyseed_restore_page_destroy(void) {
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
  current_index = 0;
  page_ready = false;
  secure_memzero(entered_indices, sizeof(entered_indices));
}
