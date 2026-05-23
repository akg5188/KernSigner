// Mnemonic Words Backup Page

#include "mnemonic_words.h"
#include "../../../core/key.h"
#include "../../../i18n/i18n.h"
#include "../../../ui/dialog.h"
#include "../../../ui/input_helpers.h"
#include "../../../ui/theme.h"
#include "../../../utils/bip39_filter.h"
#include <lvgl.h>
#include <stdio.h>
#include <stdlib.h>

static lv_obj_t *mnemonic_screen = NULL;
static void (*return_callback)(void) = NULL;

static void back_cb(lv_event_t *e) {
  (void)e;
  if (return_callback)
    return_callback();
}

static void append_word_line(char *buf, size_t buf_len, int *offset,
                             size_t position, const char *word) {
  if (!buf || !offset || *offset >= (int)buf_len)
    return;

  int word_index = bip39_filter_get_word_index(word);
  char index_text[16];
  if (word_index >= 0)
    snprintf(index_text, sizeof(index_text), "%04d", word_index);
  else
    snprintf(index_text, sizeof(index_text), "%s",
             i18n_tr_or("common.unknown", "Unknown"));

  int remaining = (int)buf_len - *offset;
  int written = snprintf(buf + *offset, remaining, "%s%2zu. %s  %s",
                         *offset > 0 ? "\n" : "", position + 1, index_text,
                         word ? word : "");
  if (written > 0) {
    if (written >= remaining)
      *offset = (int)buf_len - 1;
    else
      *offset += written;
  }
}

void mnemonic_words_page_create(lv_obj_t *parent, void (*return_cb)(void)) {
  if (!parent || !key_is_loaded())
    return;

  return_callback = return_cb;

  if (!key_mnemonic_is_valid()) {
    dialog_show_error(
        i18n_tr_or("backup.no_temporary_words",
                   "Temporary mnemonic cannot show plaintext words or indexes"),
        return_cb, 0);
    return;
  }

  char **words = NULL;
  size_t word_count = 0;
  if (!key_get_mnemonic_words(&words, &word_count))
    return;
  bool wordlist_ready = bip39_filter_init();

  mnemonic_screen = theme_create_page_container(parent);
  theme_create_page_title(mnemonic_screen,
                          i18n_tr_or("backup.word_indexes", "Word indexes"));
  (void)ui_create_back_button(mnemonic_screen, back_cb);

  char fingerprint_text[32];
  char fingerprint_hex[9] = "--------";
  key_get_fingerprint_hex(fingerprint_hex);
  snprintf(fingerprint_text, sizeof(fingerprint_text), "%s %s",
           i18n_tr_or("wallet.wallet_fingerprint", "Wallet fingerprint"),
           fingerprint_hex);

  lv_obj_t *content = lv_obj_create(mnemonic_screen);
  lv_obj_set_size(content, LV_PCT(94), LV_PCT(80));
  lv_obj_set_style_pad_all(content, theme_get_small_padding(), 0);
  lv_obj_set_style_pad_gap(content, theme_get_small_padding(), 0);
  lv_obj_set_style_border_width(content, 0, 0);
  lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
  lv_obj_set_flex_grow(content, 1);
  lv_obj_align(content, LV_ALIGN_BOTTOM_MID, 0, -theme_get_small_padding());
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *fingerprint = theme_create_label(content, fingerprint_text, false);
  lv_obj_set_style_text_font(fingerprint, theme_font_small(), 0);
  lv_obj_set_style_text_color(fingerprint, highlight_color(), 0);
  lv_obj_set_style_text_align(fingerprint, LV_TEXT_ALIGN_CENTER, 0);

  if (!wordlist_ready) {
    lv_obj_t *warn = theme_create_label(
        content, i18n_tr_or("wallet.wordlist_not_loaded",
                            "Wordlist not loaded"),
        false);
    lv_obj_set_style_text_font(warn, theme_font_small(), 0);
    lv_obj_set_style_text_color(warn, error_color(), 0);
    lv_obj_set_style_text_align(warn, LV_TEXT_ALIGN_CENTER, 0);
  }

  lv_obj_t *words_row = lv_obj_create(content);
  lv_obj_set_width(words_row, LV_PCT(100));
  lv_obj_set_flex_grow(words_row, 1);
  lv_obj_set_style_pad_all(words_row, 0, 0);
  lv_obj_set_style_border_width(words_row, 0, 0);
  lv_obj_set_style_bg_opa(words_row, LV_OPA_TRANSP, 0);
  lv_obj_set_flex_flow(words_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(words_row, LV_FLEX_ALIGN_SPACE_EVENLY,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  char word_list[1024];
  int offset = 0;

  const lv_font_t *word_font =
      (word_count > 12 || theme_get_screen_width() <= 520)
          ? theme_font_small()
          : theme_font_medium();

  if (word_count <= 12) {
    for (size_t i = 0; i < word_count; i++)
      append_word_line(word_list, sizeof(word_list), &offset, i, words[i]);
    lv_obj_t *label = theme_create_label(words_row, word_list, false);
    lv_obj_set_style_text_font(label, word_font, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
  } else {
    size_t split_index = (word_count + 1) / 2;
    for (size_t i = 0; i < split_index; i++)
      append_word_line(word_list, sizeof(word_list), &offset, i, words[i]);
    lv_obj_t *left = theme_create_label(words_row, word_list, false);
    lv_obj_set_style_text_font(left, word_font, 0);
    lv_obj_set_style_text_align(left, LV_TEXT_ALIGN_LEFT, 0);

    offset = 0;
    for (size_t i = split_index; i < word_count; i++)
      append_word_line(word_list, sizeof(word_list), &offset, i, words[i]);
    lv_obj_t *right = theme_create_label(words_row, word_list, false);
    lv_obj_set_style_text_font(right, word_font, 0);
    lv_obj_set_style_text_align(right, LV_TEXT_ALIGN_LEFT, 0);
  }

  key_free_mnemonic_words(words, word_count);
}

void mnemonic_words_page_show(void) {
  if (mnemonic_screen)
    lv_obj_clear_flag(mnemonic_screen, LV_OBJ_FLAG_HIDDEN);
}

void mnemonic_words_page_hide(void) {
  if (mnemonic_screen)
    lv_obj_add_flag(mnemonic_screen, LV_OBJ_FLAG_HIDDEN);
}

void mnemonic_words_page_destroy(void) {
  if (mnemonic_screen) {
    lv_obj_del(mnemonic_screen);
    mnemonic_screen = NULL;
  }
  return_callback = NULL;
}
