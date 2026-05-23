#include "bip39_check_page.h"
#include "../../i18n/i18n.h"
#include "../../ui/input_helpers.h"
#include "../../ui/theme.h"
#include "../../utils/bip39_filter.h"

#include <ctype.h>
#include <lvgl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wally_bip39.h>

static lv_obj_t *check_screen = NULL;
static lv_obj_t *result_card = NULL;
static lv_obj_t *result_title = NULL;
static lv_obj_t *result_body = NULL;
static lv_obj_t *back_btn = NULL;
static lv_obj_t *query_btn = NULL;
static ui_text_input_t input = {0};
static void (*return_callback)(void) = NULL;
static const int COLUMN_WEIGHTS[11] = {1,   2,   4,   8,   16,  32,
                                       64,  128, 256, 512, 1024};

static bool is_wave_43_portrait(void) {
  return theme_get_screen_width() <= 520 && theme_get_screen_height() >= 760;
}

static void back_cb(lv_event_t *e) {
  (void)e;
  bip39_check_page_hide();
  if (return_callback)
    return_callback();
}

static void destroy_result(void) {
  if (result_card) {
    lv_obj_del(result_card);
    result_card = NULL;
  }
  result_title = NULL;
  result_body = NULL;
}

static void ensure_result_card(void) {
  if (result_card || !check_screen)
    return;

  result_card = lv_obj_create(check_screen);
  lv_obj_set_width(result_card, LV_PCT(92));
  lv_obj_set_height(result_card, is_wave_43_portrait() ? 252 : 224);
  lv_obj_align(result_card, LV_ALIGN_BOTTOM_MID, 0, -theme_get_default_padding());
  lv_obj_set_style_bg_color(result_card, bg_color(), 0);
  lv_obj_set_style_bg_opa(result_card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(result_card, highlight_color(), 0);
  lv_obj_set_style_border_width(result_card, 2, 0);
  lv_obj_set_style_radius(result_card, 8, 0);
  lv_obj_set_style_pad_all(result_card, theme_get_default_padding(), 0);
  lv_obj_set_style_pad_gap(result_card, theme_get_small_padding(), 0);
  lv_obj_set_flex_flow(result_card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(result_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_add_flag(result_card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(result_card, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(result_card, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_scrollbar_mode(check_screen, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scroll_dir(check_screen, LV_DIR_VER);
  lv_obj_set_style_pad_right(result_card, theme_get_small_padding() + 4, 0);

  result_title = theme_create_label(result_card, "", false);
  lv_obj_set_style_text_font(result_title, theme_font_medium(), 0);
  lv_obj_set_style_text_color(result_title, highlight_color(), 0);
  lv_obj_set_style_text_align(result_title, LV_TEXT_ALIGN_CENTER, 0);

  result_body = theme_create_label(result_card, "", false);
  lv_obj_set_width(result_body, LV_PCT(100));
  lv_label_set_long_mode(result_body, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(result_body, theme_font_small(), 0);
  lv_obj_set_style_text_color(result_body, main_color(), 0);
  lv_obj_set_style_text_align(result_body, LV_TEXT_ALIGN_CENTER, 0);
}

static bool parse_uint_token(const char *text, uint32_t *out) {
  if (!text || !*text || !out)
    return false;
  char *end = NULL;
  unsigned long value = strtoul(text, &end, 10);
  if (!end || *end != '\0' || value > 2047)
    return false;
  *out = (uint32_t)value;
  return true;
}

static void format_weight_line(uint32_t index, char *out, size_t out_len) {
  if (!out || out_len == 0)
    return;
  out[0] = '\0';
  size_t pos = 0;
  for (int i = 0; i < 11; i++) {
    if ((index & (uint32_t)COLUMN_WEIGHTS[i]) == 0)
      continue;
    int written = snprintf(out + pos, out_len - pos, "%s%d",
                           pos ? " " : "", COLUMN_WEIGHTS[i]);
    if (written <= 0 || (size_t)written >= out_len - pos)
      break;
    pos += (size_t)written;
  }
  if (out[0] == '\0')
    snprintf(out, out_len, "0");
}

static void show_word_result(const char *word) {
  ensure_result_card();
  if (!result_title || !result_body)
    return;

  int index = bip39_filter_get_word_index(word);
  if (index < 0) {
    lv_label_set_text(result_title,
                      i18n_tr_or("bip39.word_not_found", "Word not found"));
    lv_label_set_text(result_body,
                      i18n_tr_or("bip39.enter_standard_word",
                                 "Enter a standard English BIP39 word."));
    return;
  }

  struct words *wordlist = NULL;
  const char *none_text = i18n_tr_or("common.none", "None");
  const char *previous_word = none_text;
  const char *next_word = none_text;
  if (bip39_get_wordlist(NULL, &wordlist) == WALLY_OK && wordlist) {
    if (index > 0) {
      const char *tmp = bip39_get_word_by_index(wordlist, (size_t)(index - 1));
      if (tmp)
        previous_word = tmp;
    }
    if (index < 2047) {
      const char *tmp = bip39_get_word_by_index(wordlist, (size_t)(index + 1));
      if (tmp)
        next_word = tmp;
    }
  }

  char weights[64];
  format_weight_line((uint32_t)index, weights, sizeof(weights));
  char body[256];
  snprintf(body, sizeof(body),
           i18n_tr_or("bip39.word_result_format",
                      "Word: %s\nDecimal: %04d\nHex: 0x%03x\nOctal: "
                      "0o%04o\nPrevious: %s\nNext: %s\nSteel punch: %s"),
           word, index, index, index, previous_word, next_word, weights);
  lv_label_set_text(result_title,
                    i18n_tr_or("bip39.word_result_title", "BIP39 word"));
  lv_label_set_text(result_body, body);
}

static void show_index_result(uint32_t index) {
  ensure_result_card();
  if (!result_title || !result_body)
    return;

  if (!bip39_filter_init()) {
    lv_label_set_text(result_title,
                      i18n_tr_or("wallet.wordlist_not_loaded",
                                 "Wordlist not loaded"));
    lv_label_set_text(result_body,
                      i18n_tr_or("bip39.wordlist_query_failed",
                                 "Unable to query the BIP39 wordlist."));
    return;
  }

  struct words *wordlist = NULL;
  if (bip39_get_wordlist(NULL, &wordlist) != WALLY_OK || !wordlist) {
    lv_label_set_text(result_title,
                      i18n_tr_or("wallet.wordlist_not_loaded",
                                 "Wordlist not loaded"));
    lv_label_set_text(result_body,
                      i18n_tr_or("bip39.wordlist_query_failed",
                                 "Unable to query the BIP39 wordlist."));
    return;
  }

  const char *word = bip39_get_word_by_index(wordlist, index);
  if (!word) {
    lv_label_set_text(result_title,
                      i18n_tr_or("input.invalid_index", "Invalid index"));
    lv_label_set_text(result_body,
                      i18n_tr_or("input.index_range_hint",
                                 "Enter an index between 0 and 2047."));
    return;
  }

  const char *none_text = i18n_tr_or("common.none", "None");
  const char *previous_word = index > 0
                                  ? bip39_get_word_by_index(wordlist, index - 1)
                                  : none_text;
  const char *next_word = index < 2047
                              ? bip39_get_word_by_index(wordlist, index + 1)
                              : none_text;
  char weights[64];
  format_weight_line(index, weights, sizeof(weights));
  char body[256];
  snprintf(body, sizeof(body),
           i18n_tr_or("bip39.index_result_format",
                      "Index: %04u\nWord: %s\nHex: 0x%03x\nOctal: "
                      "0o%04o\nPrevious: %s\nNext: %s\nSteel punch: %s"),
           (unsigned)index, word, (unsigned)index, (unsigned)index,
           previous_word ? previous_word : none_text,
           next_word ? next_word : none_text, weights);
  lv_label_set_text(result_title,
                    i18n_tr_or("bip39.index_result_title", "BIP39 index"));
  lv_label_set_text(result_body, body);
}

static void ready_cb(lv_event_t *e) {
  (void)e;
  const char *text = input.textarea ? lv_textarea_get_text(input.textarea) : "";
  if (!text || !*text)
    return;

  char normalized[32];
  size_t pos = 0;
  for (const char *p = text; *p && pos + 1 < sizeof(normalized); p++) {
    if (!isspace((unsigned char)*p))
      normalized[pos++] = (char)tolower((unsigned char)*p);
  }
  normalized[pos] = '\0';

  uint32_t index = 0;
  if (parse_uint_token(normalized, &index))
    show_index_result(index);
  else
    show_word_result(normalized);

  if (input.keyboard)
    lv_obj_add_flag(input.keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void query_btn_cb(lv_event_t *e) { ready_cb(e); }

static void input_focus_cb(lv_event_t *e) {
  (void)e;
  if (input.keyboard)
    lv_obj_clear_flag(input.keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void style_query_button(lv_obj_t *btn) {
  if (!btn)
    return;
  lv_obj_set_style_bg_color(btn, bg_color(), 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(btn, highlight_color(), 0);
  lv_obj_set_style_border_width(btn, 2, 0);
  lv_obj_set_style_radius(btn, 8, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
}

void bip39_check_page_create(lv_obj_t *parent, void (*return_cb)(void)) {
  if (!parent)
    return;

  return_callback = return_cb;
  bip39_filter_init();

  check_screen = theme_create_page_container(parent);
  lv_obj_set_style_bg_color(check_screen, bg_color(), 0);
  lv_obj_add_flag(check_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(check_screen, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(check_screen, LV_SCROLLBAR_MODE_OFF);
  theme_create_page_title(check_screen,
                          i18n_tr_or("bip39.self_check", "BIP39 Check"));
  back_btn = ui_create_back_button(check_screen, back_cb);

  ui_text_input_create(&input, check_screen,
                       i18n_tr_or("bip39.word_or_index_placeholder",
                                  "Word or index"),
                       false, ready_cb);
  if (input.textarea) {
    lv_textarea_set_one_line(input.textarea, false);
    lv_obj_set_width(input.textarea, LV_PCT(86));
    lv_obj_set_height(input.textarea, 58);
    lv_obj_align(input.textarea, LV_ALIGN_TOP_MID, 0, 92);
    lv_obj_set_style_bg_color(input.textarea, bg_color(), 0);
    lv_obj_set_style_bg_opa(input.textarea, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(input.textarea, main_color(), 0);
    lv_obj_set_style_border_color(input.textarea, highlight_color(), 0);
    lv_obj_set_style_border_width(input.textarea, 2, 0);
    lv_obj_set_style_radius(input.textarea, 8, 0);
    lv_obj_add_event_cb(input.textarea, input_focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(input.textarea, input_focus_cb, LV_EVENT_CLICKED, NULL);
  }

  query_btn = lv_btn_create(check_screen);
  lv_obj_set_size(query_btn, LV_PCT(50), 48);
  lv_obj_align(query_btn, LV_ALIGN_TOP_MID, 0, 158);
  style_query_button(query_btn);
  lv_obj_add_event_cb(query_btn, query_btn_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *label = lv_label_create(query_btn);
  lv_label_set_text(label, i18n_tr_or("common.check", "Check"));
  lv_obj_set_style_text_font(label, theme_font_medium(), 0);
  lv_obj_set_style_text_color(label, highlight_color(), 0);
  lv_obj_center(label);
}

void bip39_check_page_show(void) {
  if (check_screen)
    lv_obj_clear_flag(check_screen, LV_OBJ_FLAG_HIDDEN);
  ui_text_input_show(&input);
}

void bip39_check_page_hide(void) {
  if (check_screen)
    lv_obj_add_flag(check_screen, LV_OBJ_FLAG_HIDDEN);
  ui_text_input_hide(&input);
}

void bip39_check_page_destroy(void) {
  destroy_result();
  ui_text_input_destroy(&input);
  if (query_btn) {
    lv_obj_del(query_btn);
    query_btn = NULL;
  }
  if (back_btn) {
    lv_obj_del(back_btn);
    back_btn = NULL;
  }
  if (check_screen) {
    lv_obj_del(check_screen);
    check_screen = NULL;
  }
  return_callback = NULL;
}
