// Dice Rolls Page - Generate mnemonic entropy from dice rolls

#include "dice_rolls.h"
#include "../../ui/dialog.h"
#include "../../ui/input_helpers.h"
#include "../../ui/theme.h"
#include "../../ui/word_selector.h"
#include <lvgl.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <wally_bip39.h>
#include <wally_core.h>
#include <wally_crypto.h>

#include "../../utils/secure_mem.h"

#define MAX_ROLLS 256
#define DICE_BTN_DELETE 9
#define DICE_BTN_DONE 10

static lv_obj_t *dice_rolls_screen = NULL;
static lv_obj_t *back_btn = NULL;
static lv_obj_t *dice_btnmatrix = NULL;
static lv_obj_t *title_label = NULL;
static lv_obj_t *count_label = NULL;
static lv_obj_t *rolls_textarea = NULL;
static void (*return_callback)(void) = NULL;
static char *completed_mnemonic = NULL;

static int total_words = 0;
static int min_rolls = 0;
static char rolls_string[MAX_ROLLS + 1];
static int rolls_count = 0;

static void create_word_count_menu(void);
static void create_dice_input(void);
static void cleanup_ui(void);
static void on_word_count_selected(int word_count);
static void back_cb(void);
static void dice_btnmatrix_event_cb(lv_event_t *e);
static void rolls_textarea_event_cb(lv_event_t *e);
static void update_display(void);
static bool generate_mnemonic_from_rolls(void);
static void finish_dice_rolls(void);
static void confirm_finish_cb(bool confirmed, void *user_data);
static void back_btn_cb(lv_event_t *e);
static void back_confirm_cb(bool confirmed, void *user_data);

static const char *dice_map[] = {
    "1", "2", "3", "\n",
    "4", "5", "6", "\n",
    "<", "空", ">", "\n",
    "删", "完成", ""};

static const lv_buttonmatrix_ctrl_t dice_ctrl_map[] = {
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_2,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_2,
    LV_BUTTONMATRIX_CTRL_WIDTH_3,
};

static int entropy_len_for_word_count(int word_count) {
  switch (word_count) {
  case 12:
    return 16;
  case 15:
    return 20;
  case 18:
    return 24;
  case 21:
    return 28;
  case 24:
    return 32;
  default:
    return 0;
  }
}

static int min_rolls_for_word_count(int word_count) {
  switch (word_count) {
  case 12:
    return 50;
  case 15:
    return 62;
  case 18:
    return 75;
  case 21:
    return 87;
  case 24:
    return 99;
  default:
    return 0;
  }
}

static int count_roll_digits(const char *text) {
  int count = 0;
  for (const char *p = text ? text : ""; *p; p++) {
    if (*p >= '1' && *p <= '6')
      count++;
  }
  return count;
}

static void cleanup_ui(void) {
  if (back_btn) {
    lv_obj_del(back_btn);
    back_btn = NULL;
  }
  if (dice_btnmatrix) {
    lv_obj_del(dice_btnmatrix);
    dice_btnmatrix = NULL;
  }
  if (title_label) {
    lv_obj_del(title_label);
    title_label = NULL;
  }
  if (count_label) {
    lv_obj_del(count_label);
    count_label = NULL;
  }
  if (rolls_textarea) {
    lv_obj_del(rolls_textarea);
    rolls_textarea = NULL;
  }
}

static void create_word_count_menu(void) {
  cleanup_ui();
  ui_word_count_selector_create(dice_rolls_screen, back_cb,
                                on_word_count_selected);
}

static void back_confirm_cb(bool confirmed, void *user_data) {
  (void)user_data;
  if (confirmed) {
    rolls_count = 0;
    rolls_string[0] = '\0';
    if (return_callback)
      return_callback();
  }
}

static void back_btn_cb(lv_event_t *e) {
  (void)e;
  dialog_show_confirm("确定返回？", back_confirm_cb, NULL,
                      DIALOG_STYLE_OVERLAY);
}

static void create_dice_input(void) {
  cleanup_ui();

  title_label = theme_create_page_title(dice_rolls_screen, "");

  back_btn = ui_create_back_button(dice_rolls_screen, back_btn_cb);

  dice_btnmatrix = lv_buttonmatrix_create(dice_rolls_screen);
  lv_buttonmatrix_set_map(dice_btnmatrix, dice_map);
  lv_buttonmatrix_set_ctrl_map(dice_btnmatrix, dice_ctrl_map);
  lv_obj_align(dice_btnmatrix, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_size(dice_btnmatrix, LV_PCT(100),
                  theme_get_screen_height() >= 760 ? 310 : 245);
  theme_apply_btnmatrix(dice_btnmatrix);
  lv_obj_set_style_pad_all(dice_btnmatrix, 8, 0);
  lv_obj_set_style_pad_gap(dice_btnmatrix, 8, 0);
  lv_obj_set_style_text_font(dice_btnmatrix, theme_font_medium(),
                             LV_PART_ITEMS);

  rolls_textarea = lv_textarea_create(dice_rolls_screen);
  lv_obj_set_width(rolls_textarea, LV_PCT(92));
  lv_obj_set_height(rolls_textarea, LV_PCT(31));
  lv_obj_align(rolls_textarea, LV_ALIGN_TOP_MID, 0, 112);
  lv_textarea_set_one_line(rolls_textarea, false);
  lv_textarea_set_max_length(rolls_textarea, MAX_ROLLS + 40);
  lv_textarea_set_accepted_chars(rolls_textarea, "123456 ");
  lv_textarea_set_placeholder_text(rolls_textarea, "12345 61234 56123");
  lv_textarea_set_cursor_click_pos(rolls_textarea, true);
  lv_obj_set_style_text_color(rolls_textarea, main_color(), 0);
  lv_obj_set_style_text_font(rolls_textarea, theme_font_medium(), 0);
  lv_obj_set_style_bg_color(rolls_textarea, bg_color(), 0);
  lv_obj_set_style_bg_opa(rolls_textarea, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(rolls_textarea, highlight_color(), 0);
  lv_obj_set_style_border_width(rolls_textarea, 2, 0);
  lv_obj_set_style_radius(rolls_textarea, 8, 0);
  lv_obj_set_style_pad_all(rolls_textarea, 12, 0);
  lv_obj_set_style_bg_color(rolls_textarea, highlight_color(),
                            LV_PART_CURSOR);
  lv_obj_set_style_bg_opa(rolls_textarea, LV_OPA_COVER, LV_PART_CURSOR);
  lv_obj_add_event_cb(rolls_textarea, rolls_textarea_event_cb,
                      LV_EVENT_VALUE_CHANGED, NULL);

  count_label = theme_create_label(dice_rolls_screen, "", false);
  lv_obj_set_width(count_label, LV_PCT(92));
  lv_obj_set_style_text_align(count_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align_to(count_label, rolls_textarea, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);

  lv_obj_add_event_cb(dice_btnmatrix, dice_btnmatrix_event_cb,
                      LV_EVENT_VALUE_CHANGED, NULL);

  update_display();
}

static void update_display(void) {
  if (!title_label)
    return;

  rolls_count = count_roll_digits(rolls_textarea ? lv_textarea_get_text(rolls_textarea)
                                                 : rolls_string);
  char title[64];
  snprintf(title, sizeof(title), "%d 词 - %d/%d 个骰子点数", total_words,
           rolls_count, min_rolls);
  lv_label_set_text(title_label, title);

  if (count_label) {
    char status[96];
    snprintf(status, sizeof(status), "%d/%d，5 个一组", rolls_count,
             min_rolls);
    lv_label_set_text(count_label, status);
    lv_obj_set_style_text_color(count_label,
                                rolls_count >= min_rolls ? yes_color()
                                                         : secondary_color(),
                                0);
  }

  if (dice_btnmatrix) {
    if (rolls_count >= min_rolls)
      lv_buttonmatrix_clear_button_ctrl(dice_btnmatrix, DICE_BTN_DONE,
                                        LV_BUTTONMATRIX_CTRL_DISABLED);
    else
      lv_buttonmatrix_set_button_ctrl(dice_btnmatrix, DICE_BTN_DONE,
                                      LV_BUTTONMATRIX_CTRL_DISABLED);

    const char *text = rolls_textarea ? lv_textarea_get_text(rolls_textarea) : "";
    if (text && text[0])
      lv_buttonmatrix_clear_button_ctrl(dice_btnmatrix, DICE_BTN_DELETE,
                                        LV_BUTTONMATRIX_CTRL_DISABLED);
    else
      lv_buttonmatrix_set_button_ctrl(dice_btnmatrix, DICE_BTN_DELETE,
                                      LV_BUTTONMATRIX_CTRL_DISABLED);
  }
}

static void dice_btnmatrix_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED)
    return;

  lv_obj_t *obj = lv_event_get_current_target(e);
  uint32_t id = lv_buttonmatrix_get_selected_button(obj);
  if (id == LV_BUTTONMATRIX_BUTTON_NONE)
    return;

  const char *txt = lv_buttonmatrix_get_button_text(obj, id);

  if (!txt)
    return;

  if (strcmp(txt, "完成") == 0) {
    if (rolls_count >= min_rolls) {
      char msg[64];
      snprintf(msg, sizeof(msg), "用 %d 个点数生成 %d 词助记词？",
               rolls_count, total_words);
      dialog_show_confirm(msg, confirm_finish_cb, NULL, DIALOG_STYLE_OVERLAY);
    }
  } else if (strcmp(txt, "删") == 0) {
    if (rolls_textarea)
      lv_textarea_delete_char(rolls_textarea);
    update_display();
  } else if (strcmp(txt, "<") == 0) {
    if (rolls_textarea)
      lv_textarea_cursor_left(rolls_textarea);
  } else if (strcmp(txt, ">") == 0) {
    if (rolls_textarea)
      lv_textarea_cursor_right(rolls_textarea);
  } else if (strcmp(txt, "空") == 0) {
    if (rolls_textarea)
      lv_textarea_add_char(rolls_textarea, ' ');
    update_display();
  } else {
    char dice_value = txt[0];
    if (dice_value >= '1' && dice_value <= '6' && rolls_count < MAX_ROLLS &&
        rolls_textarea) {
      lv_textarea_add_text(rolls_textarea, txt);
      update_display();
    }
  }
}

static void rolls_textarea_event_cb(lv_event_t *e) {
  (void)e;
  update_display();
}

static void confirm_finish_cb(bool confirmed, void *user_data) {
  (void)user_data;
  if (confirmed)
    finish_dice_rolls();
}

static bool generate_mnemonic_from_rolls(void) {
  const char *text = rolls_textarea ? lv_textarea_get_text(rolls_textarea)
                                    : rolls_string;
  size_t pos = 0;
  rolls_count = 0;
  for (const char *p = text ? text : ""; *p; p++) {
    unsigned char c = (unsigned char)*p;
    if (isspace(c) || c == ',' || c == ';' || c == '-')
      continue;
    if (c < '1' || c > '6' || pos + 1 >= sizeof(rolls_string))
      return false;
    rolls_string[pos++] = c == '6' ? '0' : (char)c;
    rolls_count++;
  }
  rolls_string[pos] = '\0';

  if (rolls_count < min_rolls)
    return false;

  size_t entropy_len = entropy_len_for_word_count(total_words);
  if (entropy_len == 0)
    return false;

  unsigned char hash[SHA256_LEN];
  if (wally_sha256((const unsigned char *)rolls_string, strlen(rolls_string), hash,
                   sizeof(hash)) != WALLY_OK)
    return false;

  char *mnemonic = NULL;
  if (bip39_mnemonic_from_bytes(NULL, hash, entropy_len, &mnemonic) !=
          WALLY_OK ||
      !mnemonic)
    return false;

  if (bip39_mnemonic_validate(NULL, mnemonic) != WALLY_OK) {
    wally_free_string(mnemonic);
    return false;
  }

  if (completed_mnemonic)
    free(completed_mnemonic);
  completed_mnemonic = strdup(mnemonic);
  wally_free_string(mnemonic);

  secure_memzero(hash, sizeof(hash));
  secure_memzero(rolls_string, sizeof(rolls_string));
  rolls_count = 0;

  return true;
}

static void finish_dice_rolls(void) {
  if (!generate_mnemonic_from_rolls()) {
    dialog_show_error("助记词生成失败", NULL, 0);
    return;
  }

  dice_rolls_page_hide();
  if (return_callback)
    return_callback();
}

static void on_word_count_selected(int word_count) {
  total_words = word_count;
  min_rolls = min_rolls_for_word_count(word_count);
  rolls_count = 0;
  rolls_string[0] = '\0';
  create_dice_input();
}

static void back_cb(void) {
  void (*callback)(void) = return_callback;
  dice_rolls_page_hide();
  dice_rolls_page_destroy();
  if (callback)
    callback();
}

void dice_rolls_page_create(lv_obj_t *parent, void (*return_cb)(void)) {
  if (!parent)
    return;

  return_callback = return_cb;

  if (completed_mnemonic) {
    free(completed_mnemonic);
    completed_mnemonic = NULL;
  }

  total_words = 0;
  min_rolls = 0;
  rolls_count = 0;
  rolls_string[0] = '\0';

  dice_rolls_screen = theme_create_page_container(parent);

  create_word_count_menu();
}

void dice_rolls_page_show(void) {
  if (dice_rolls_screen)
    lv_obj_clear_flag(dice_rolls_screen, LV_OBJ_FLAG_HIDDEN);
}

void dice_rolls_page_hide(void) {
  if (dice_rolls_screen)
    lv_obj_add_flag(dice_rolls_screen, LV_OBJ_FLAG_HIDDEN);
}

void dice_rolls_page_destroy(void) {
  cleanup_ui();

  if (dice_rolls_screen) {
    lv_obj_del(dice_rolls_screen);
    dice_rolls_screen = NULL;
  }

  secure_memzero(rolls_string, sizeof(rolls_string));
  rolls_count = 0;
  total_words = 0;
  min_rolls = 0;
  return_callback = NULL;
}

char *dice_rolls_get_completed_mnemonic(void) {
  if (completed_mnemonic) {
    char *result = completed_mnemonic;
    completed_mnemonic = NULL;
    return result;
  }
  return NULL;
}
