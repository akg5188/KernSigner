#include "mnemonic_tool_page.h"
#include "../../core/key.h"
#include "../../core/mnemonic_tools.h"
#include "../../ui/dialog.h"
#include "../../ui/input_helpers.h"
#include "../../ui/menu.h"
#include "../../ui/theme.h"
#include "../../utils/secure_mem.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TOOL_TEXT_MAX 768
#define MAX_INDEX_WORDS 24
#define TOOL_INPUT_TEXT_MAX 768
#define BINARY_BTN_0 0
#define BINARY_BTN_1 1
#define BINARY_BTN_DELETE 5
#define BINARY_BTN_DONE 6

static lv_obj_t *tool_screen = NULL;
static lv_obj_t *back_btn = NULL;
static lv_obj_t *run_btn = NULL;
static lv_obj_t *tool_textarea = NULL;
static lv_obj_t *tool_count_label = NULL;
static lv_obj_t *tool_keyboard = NULL;
static ui_menu_t *word_count_menu = NULL;
static ui_text_input_t input = {0};
static void (*return_callback)(void) = NULL;
static mnemonic_tool_mode_t current_mode;
static int selected_words = 12;
static char *completed_mnemonic = NULL;

static size_t entropy_len_for_word_count(int words);
static int target_entropy_bits_for_word_count(int words);
static int min_d20_rolls_for_word_count(int words);
static int required_card_bits_for_word_count(int words);
static bool normalize_card_token(const char *tok, char out[3]);
static void create_text_input(void);
static void input_ready_cb(lv_event_t *e);

static const char *const digit_keyboard_map[] = {
    "1", "2", "3", "\n",
    "4", "5", "6", "\n",
    "7", "8", "9", "\n",
    "<", "0", ">", "\n",
    "空", "删", "完成", ""};

static const char *const d6_keyboard_map[] = {
    "1", "2", "3", "\n",
    "4", "5", "6", "\n",
    "<", "空", ">", "\n",
    "删", "完成", ""};

static const char *const binary_keyboard_map[] = {
    "0", "1", "\n",
    "<", "空", ">", "\n",
    "删", "完成", ""};

static const char *const hex_keyboard_map[] = {
    "1", "2", "3", "A", "\n",
    "4", "5", "6", "B", "\n",
    "7", "8", "9", "C", "\n",
    "D", "0", "E", "F", "\n",
    "<", "空", ">", "删", "完成", ""};

static const char *const card_keyboard_map[] = {
    "A", "K", "Q", "J", "T", "\n",
    "9", "8", "7", "6", "5", "\n",
    "4", "3", "2", "C", "D", "\n",
    "H", "S", "<", ">", "\n",
    "空", "删", "完成", ""};

static const char *const shift_keyboard_map[] = {
    "1", "2", "3", "+", "\n",
    "4", "5", "6", "-", "\n",
    "7", "8", "9", "空", "\n",
    "<", "0", ">", "删", "\n",
    "空", "删", "完成", ""};

static const lv_buttonmatrix_ctrl_t digit_keyboard_ctrl_map[] = {
    LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_2, LV_BUTTONMATRIX_CTRL_WIDTH_2,
    LV_BUTTONMATRIX_CTRL_WIDTH_3,
};

static const lv_buttonmatrix_ctrl_t d6_keyboard_ctrl_map[] = {
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

static const lv_buttonmatrix_ctrl_t binary_keyboard_ctrl_map[] = {
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_2,
    LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_2,
    LV_BUTTONMATRIX_CTRL_WIDTH_3,
};

static const lv_buttonmatrix_ctrl_t hex_keyboard_ctrl_map[] = {
    LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_2, LV_BUTTONMATRIX_CTRL_WIDTH_2,
    LV_BUTTONMATRIX_CTRL_WIDTH_2, LV_BUTTONMATRIX_CTRL_WIDTH_2,
    LV_BUTTONMATRIX_CTRL_WIDTH_3,
};

static const lv_buttonmatrix_ctrl_t card_keyboard_ctrl_map[] = {
    LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_2,
    LV_BUTTONMATRIX_CTRL_WIDTH_2, LV_BUTTONMATRIX_CTRL_WIDTH_3,
};

static const lv_buttonmatrix_ctrl_t shift_keyboard_ctrl_map[] = {
    LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1,
    LV_BUTTONMATRIX_CTRL_WIDTH_2, LV_BUTTONMATRIX_CTRL_WIDTH_2,
    LV_BUTTONMATRIX_CTRL_WIDTH_3,
};

static const char *mode_title(mnemonic_tool_mode_t mode) {
  switch (mode) {
  case MNEMONIC_TOOL_INDEX_IMPORT:
    return "编号导入";
  case MNEMONIC_TOOL_HEX_ENTROPY:
    return "16进制创建";
  case MNEMONIC_TOOL_BINARY_ENTROPY:
    return "抛硬币";
  case MNEMONIC_TOOL_D6_ENTROPY:
    return "骰子创建";
  case MNEMONIC_TOOL_D20_ENTROPY:
    return "D20 骰子";
  case MNEMONIC_TOOL_CARD_ENTROPY:
    return "扑克牌创建";
  case MNEMONIC_TOOL_BIP85_MNEMONIC:
    return "BIP85 子助记词";
  case MNEMONIC_TOOL_XOR_CURRENT:
    return "助记词异或";
  case MNEMONIC_TOOL_SECONDARY_SHIFT:
    return "助记词加密";
  case MNEMONIC_TOOL_SECONDARY_ADD:
    return "助记词加密";
  case MNEMONIC_TOOL_SECONDARY_SUB:
    return "助记词加密";
  case MNEMONIC_TOOL_STEEL_RESTORE:
    return "钢板数字恢复";
  default:
    return "助记词工具";
  }
}

static void cleanup_input(void) {
  ui_text_input_destroy(&input);
  if (tool_keyboard) {
    lv_obj_del(tool_keyboard);
    tool_keyboard = NULL;
  }
  if (tool_count_label) {
    lv_obj_del(tool_count_label);
    tool_count_label = NULL;
  }
  if (tool_textarea) {
    lv_obj_del(tool_textarea);
    tool_textarea = NULL;
  }
  if (run_btn) {
    lv_obj_del(run_btn);
    run_btn = NULL;
  }
  if (back_btn) {
    lv_obj_del(back_btn);
    back_btn = NULL;
  }
  if (word_count_menu) {
    ui_menu_destroy(word_count_menu);
    word_count_menu = NULL;
  }
}

static size_t entropy_len_for_word_count(int words) {
  switch (words) {
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

static int target_entropy_bits_for_word_count(int words) {
  return (int)(entropy_len_for_word_count(words) * 8);
}

static int count_binary_bits(const char *text, bool *invalid_out) {
  int count = 0;
  bool invalid = false;

  for (const char *p = text ? text : ""; *p; p++) {
    if (*p == '0' || *p == '1') {
      count++;
    } else if (isspace((unsigned char)*p)) {
      continue;
    } else {
      invalid = true;
    }
  }

  if (invalid_out)
    *invalid_out = invalid;
  return count;
}

static size_t tool_max_length_for_mode(void) {
  if (current_mode == MNEMONIC_TOOL_BINARY_ENTROPY) {
    return TOOL_INPUT_TEXT_MAX - 1;
  }

  return TOOL_INPUT_TEXT_MAX - 1;
}

static int min_d20_rolls_for_word_count(int words) {
  switch (words) {
  case 12:
    return 30;
  case 15:
    return 38;
  case 18:
    return 45;
  case 21:
    return 52;
  case 24:
    return 60;
  default:
    return 0;
  }
}

static int required_card_bits_for_word_count(int words) {
  return target_entropy_bits_for_word_count(words);
}

static const char *card_entropy_bits_str(const char card[3]) {
  static const char *const bits_by_card[52] = {
      "00000", "00001", "00010", "00011", "00100", "00101", "00110",
      "00111", "01000", "01001", "01010", "01011", "01100", "01101",
      "01110", "01111", "10000", "10001", "10010", "10011", "10100",
      "10101", "10110", "10111", "11000", "11001", "11010", "11011",
      "11100", "11101", "11110", "11111", "0000",  "0001",  "0010",
      "0011",  "0100",  "0101",  "0110",  "0111",  "1000",  "1001",
      "1010",  "1011",  "1100",  "1101",  "1110",  "1111",  "00",
      "01",    "10",    "11",
  };
  const char *ranks = "A23456789TJQK";
  const char *suits = "CDHS";
  const char *rank = strchr(ranks, card[0]);
  const char *suit = strchr(suits, card[1]);
  if (!rank || !suit)
    return NULL;
  int index = (int)((suit - suits) * 13 + (rank - ranks));
  return index >= 0 ? bits_by_card[index] : NULL;
}

static int count_d6_digits(const char *text) {
  int count = 0;
  for (const char *p = text ? text : ""; *p; p++) {
    if (*p >= '1' && *p <= '6')
      count++;
  }
  return count;
}

static int d6_entropy_bits(const char *text) {
  int bits = 0;
  for (const char *p = text ? text : ""; *p; p++) {
    if (*p == '1' || *p == '2' || *p == '3' || *p == '6') {
      bits += 2;
    } else if (*p == '4' || *p == '5') {
      bits += 1;
    }
  }
  return bits;
}

static int count_hex_digits(const char *text) {
  int count = 0;
  for (const char *p = text ? text : ""; *p; p++) {
    if (isxdigit((unsigned char)*p))
      count++;
  }
  return count;
}

static int count_number_tokens(const char *text) {
  int count = 0;
  bool in_token = false;
  for (const char *p = text ? text : ""; *p; p++) {
    if (isdigit((unsigned char)*p)) {
      if (!in_token) {
        count++;
        in_token = true;
      }
    } else {
      in_token = false;
    }
  }
  return count;
}

static bool is_secondary_shift_mode(mnemonic_tool_mode_t mode) {
  return mode == MNEMONIC_TOOL_SECONDARY_SHIFT ||
         mode == MNEMONIC_TOOL_SECONDARY_ADD ||
         mode == MNEMONIC_TOOL_SECONDARY_SUB;
}

static int current_mnemonic_word_count(void) {
  char **words = NULL;
  size_t word_count = 0;
  if (!key_get_mnemonic_words(&words, &word_count))
    return 0;
  key_free_mnemonic_words(words, word_count);
  return (int)word_count;
}

static bool shift_token_is_complete(const char *token) {
  if (!token || !*token)
    return false;
  const char *p = token;
  if (*p != '+' && *p != '-')
    return false;
  p++;
  if (!isdigit((unsigned char)*p))
    return false;
  while (*p) {
    if (!isdigit((unsigned char)*p))
      return false;
    p++;
  }
  return true;
}

static int count_shift_tokens(const char *text, bool *has_invalid_out) {
  int count = 0;
  bool has_invalid = false;
  char token[24];
  size_t token_len = 0;

  for (const char *p = text ? text : ""; ; p++) {
    unsigned char c = (unsigned char)*p;
    bool delim = (c == '\0' || isspace(c) || c == ',' || c == ';');
    if (!delim) {
      if (token_len + 1 < sizeof(token)) {
        token[token_len++] = (char)c;
      } else {
        has_invalid = true;
      }
      continue;
    }

    if (token_len > 0) {
      token[token_len] = '\0';
      if (shift_token_is_complete(token)) {
        count++;
      } else {
        has_invalid = true;
      }
      token_len = 0;
    }

    if (c == '\0')
      break;
  }

  if (has_invalid_out)
    *has_invalid_out = has_invalid;
  return count;
}

static bool card_input_stats(const char *text, int *card_count_out,
                             int *bits_out, bool *invalid_out,
                             bool require_complete) {
  char compact[TOOL_TEXT_MAX];
  size_t compact_len = 0;
  int card_count = 0;
  int bits = 0;
  bool invalid = false;

  for (const char *p = text ? text : ""; *p; p++) {
    unsigned char c = (unsigned char)*p;
    if (isspace(c) || c == ',' || c == ';' || c == '-' || c == '|')
      continue;
    if (compact_len + 1 >= sizeof(compact)) {
      invalid = true;
      break;
    }
    compact[compact_len++] = (char)c;
  }
  compact[compact_len] = '\0';

  if ((compact_len % 2) != 0 && require_complete)
    invalid = true;

  size_t complete_len = compact_len - (compact_len % 2);
  for (size_t offset = 0; offset < complete_len && !invalid; offset += 2) {
    char tok[3] = {compact[offset], compact[offset + 1], '\0'};
    char card[3];
    if (!normalize_card_token(tok, card)) {
      invalid = true;
      break;
    }
    const char *part = card_entropy_bits_str(card);
    if (!part) {
      invalid = true;
      break;
    }
    bits += (int)strlen(part);
    card_count++;
  }

  if (card_count_out)
    *card_count_out = card_count;
  if (bits_out)
    *bits_out = bits;
  if (invalid_out)
    *invalid_out = invalid;
  secure_memzero(compact, sizeof(compact));
  return !invalid;
}

static bool mode_uses_custom_keyboard(mnemonic_tool_mode_t mode) {
  return mode == MNEMONIC_TOOL_INDEX_IMPORT ||
         mode == MNEMONIC_TOOL_HEX_ENTROPY ||
         mode == MNEMONIC_TOOL_BINARY_ENTROPY ||
         mode == MNEMONIC_TOOL_D6_ENTROPY ||
         mode == MNEMONIC_TOOL_D20_ENTROPY ||
         mode == MNEMONIC_TOOL_CARD_ENTROPY ||
         mode == MNEMONIC_TOOL_BIP85_MNEMONIC ||
         mode == MNEMONIC_TOOL_STEEL_RESTORE ||
         is_secondary_shift_mode(mode);
}

static const char *source_label_for_mode(mnemonic_tool_mode_t mode) {
  switch (mode) {
  case MNEMONIC_TOOL_INDEX_IMPORT:
  case MNEMONIC_TOOL_STEEL_RESTORE:
    return "序号";
  case MNEMONIC_TOOL_HEX_ENTROPY:
    return "16进制";
  case MNEMONIC_TOOL_BINARY_ENTROPY:
    return "抛硬币";
  case MNEMONIC_TOOL_D6_ENTROPY:
    return "D6骰子";
  case MNEMONIC_TOOL_D20_ENTROPY:
    return "D20骰子";
  case MNEMONIC_TOOL_CARD_ENTROPY:
    return "扑克牌";
  case MNEMONIC_TOOL_SECONDARY_SHIFT:
  case MNEMONIC_TOOL_SECONDARY_ADD:
  case MNEMONIC_TOOL_SECONDARY_SUB:
    return "加减数字";
  default:
    return "原始输入";
  }
}

static void complete_with_mnemonic(char *mnemonic) {
  if (!mnemonic) {
    dialog_show_error("助记词生成失败，请检查输入。", NULL, 0);
    return;
  }

  if (mode_uses_custom_keyboard(current_mode) && tool_textarea) {
    key_set_pending_source_material(source_label_for_mode(current_mode),
                                    lv_textarea_get_text(tool_textarea),
                                    mnemonic);
  } else {
    key_clear_pending_source_material();
  }

  SECURE_FREE_STRING(completed_mnemonic);
  completed_mnemonic = mnemonic;
  mnemonic_tool_page_hide();
  if (return_callback)
    return_callback();
}

static char *normalized_hex_from_input(const char *text) {
  if (!text)
    return NULL;
  size_t len = strlen(text);
  char *hex = malloc(len + 1);
  if (!hex)
    return NULL;
  size_t pos = 0;
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)text[i];
    if (isspace(c) || c == ':' || c == '-' || c == ',')
      continue;
    if (!isxdigit(c)) {
      secure_memzero(hex, len + 1);
      free(hex);
      return NULL;
    }
    hex[pos++] = (char)tolower(c);
  }
  hex[pos] = '\0';
  return hex;
}

static bool parse_index_token(const char *token, uint16_t *out) {
  if (!token || !out)
    return false;

  int base = 10;
  const char *p = token;
  if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
    base = 16;
    p += 2;
  } else if (p[0] == '0' && (p[1] == 'o' || p[1] == 'O')) {
    base = 8;
    p += 2;
  }

  char *end = NULL;
  const char *digits_start = p;
  unsigned long value = strtoul(p, &end, base);
  if (!end || end == digits_start || *end != '\0' || value > 2047)
    return false;
  *out = (uint16_t)value;
  return true;
}

static char *parse_index_mnemonic(const char *text) {
  char copy[TOOL_TEXT_MAX];
  snprintf(copy, sizeof(copy), "%s", text ? text : "");

  uint16_t indices[MAX_INDEX_WORDS];
  size_t count = 0;
  char *save = NULL;
  for (char *tok = strtok_r(copy, " ,;\n\r\t", &save); tok;
       tok = strtok_r(NULL, " ,;\n\r\t", &save)) {
    if (count >= MAX_INDEX_WORDS || !parse_index_token(tok, &indices[count])) {
      secure_memzero(copy, sizeof(copy));
      return NULL;
    }
    count++;
  }

  char *mnemonic = mnemonic_tools_from_indices_unchecked(indices, count);
  secure_memzero(copy, sizeof(copy));
  secure_memzero(indices, sizeof(indices));
  return mnemonic;
}

static char *parse_hex_mnemonic(const char *text) {
  char *hex = normalized_hex_from_input(text);
  if (!hex)
    return NULL;

  size_t entropy_len = entropy_len_for_word_count(selected_words);
  if (strlen(hex) != entropy_len * 2) {
    secure_memzero(hex, strlen(hex));
    free(hex);
    return NULL;
  }

  char *mnemonic = mnemonic_tools_from_hex_entropy(hex);
  secure_memzero(hex, strlen(hex));
  free(hex);
  return mnemonic;
}

static char *parse_binary_mnemonic(const char *text) {
  bool invalid = false;
  int bits = count_binary_bits(text, &invalid);
  int target_bits = target_entropy_bits_for_word_count(selected_words);
  if (invalid || bits != target_bits)
    return NULL;

  char compact[TOOL_INPUT_TEXT_MAX];
  size_t pos = 0;
  for (const char *p = text ? text : ""; *p; p++) {
    if (*p == '0' || *p == '1') {
      if (pos + 1 >= sizeof(compact))
        return NULL;
      compact[pos++] = *p;
    }
  }
  compact[pos] = '\0';

  char *mnemonic = mnemonic_tools_from_raw_bits(compact);
  secure_memzero(compact, sizeof(compact));
  return mnemonic;
}

static char *parse_d6_mnemonic(const char *text) {
  return mnemonic_tools_from_d6_entropy(
      text, entropy_len_for_word_count(selected_words), NULL, NULL);
}

static bool tool_input_ready_to_finish(void) {
  const char *text = tool_textarea ? lv_textarea_get_text(tool_textarea) : "";
  switch (current_mode) {
  case MNEMONIC_TOOL_HEX_ENTROPY:
    return count_hex_digits(text) ==
           (int)(entropy_len_for_word_count(selected_words) * 2);
  case MNEMONIC_TOOL_BINARY_ENTROPY: {
    bool invalid = false;
    int bits = count_binary_bits(text, &invalid);
    return !invalid && bits == target_entropy_bits_for_word_count(selected_words);
  }
  case MNEMONIC_TOOL_D6_ENTROPY:
    return d6_entropy_bits(text) >=
           target_entropy_bits_for_word_count(selected_words);
  case MNEMONIC_TOOL_D20_ENTROPY:
    return count_number_tokens(text) >=
           min_d20_rolls_for_word_count(selected_words);
  case MNEMONIC_TOOL_CARD_ENTROPY:
    {
      int cards = 0;
      int bits = 0;
      bool invalid = false;
      (void)cards;
      card_input_stats(text, &cards, &bits, &invalid, true);
      return !invalid && bits >= required_card_bits_for_word_count(selected_words);
    }
  case MNEMONIC_TOOL_INDEX_IMPORT:
  case MNEMONIC_TOOL_STEEL_RESTORE:
    return count_number_tokens(text) == selected_words;
  case MNEMONIC_TOOL_BIP85_MNEMONIC:
    return count_number_tokens(text) > 0;
  case MNEMONIC_TOOL_SECONDARY_SHIFT:
  case MNEMONIC_TOOL_SECONDARY_ADD:
  case MNEMONIC_TOOL_SECONDARY_SUB:
    {
      bool invalid = false;
      int target = current_mnemonic_word_count();
      return target > 0 && count_shift_tokens(text, &invalid) == target &&
             !invalid;
    }
  default:
    return text && text[0] != '\0';
  }
}

static void tool_update_count(void) {
  if (!tool_textarea)
    return;

  const char *text = lv_textarea_get_text(tool_textarea);
  bool ready = tool_input_ready_to_finish();
  if (tool_count_label) {
    char status[128];
    switch (current_mode) {
    case MNEMONIC_TOOL_HEX_ENTROPY:
      snprintf(status, sizeof(status), "十六进制 %d/%u",
               count_hex_digits(text),
               (unsigned)(entropy_len_for_word_count(selected_words) * 2));
      break;
    case MNEMONIC_TOOL_BINARY_ENTROPY: {
      bool invalid = false;
      int bits = count_binary_bits(text, &invalid);
      int target = target_entropy_bits_for_word_count(selected_words);
      snprintf(status, sizeof(status), "%s%d/%d 位",
               invalid ? "格式错 " : "抛硬币 ", bits, target);
      break;
    }
    case MNEMONIC_TOOL_D6_ENTROPY:
      snprintf(status, sizeof(status), "骰子 %d 次，熵 %d/%d",
               count_d6_digits(text), d6_entropy_bits(text),
               target_entropy_bits_for_word_count(selected_words));
      break;
    case MNEMONIC_TOOL_D20_ENTROPY:
      snprintf(status, sizeof(status), "点数 %d/%d",
               count_number_tokens(text),
               min_d20_rolls_for_word_count(selected_words));
      break;
    case MNEMONIC_TOOL_CARD_ENTROPY:
      {
        int cards = 0;
        int bits = 0;
        bool invalid = false;
        card_input_stats(text, &cards, &bits, &invalid, false);
        snprintf(status, sizeof(status), "%s%d 张，熵 %d/%d",
                 invalid ? "格式错 " : "扑克牌 ", cards, bits,
                 required_card_bits_for_word_count(selected_words));
        break;
      }
      break;
    case MNEMONIC_TOOL_INDEX_IMPORT:
    case MNEMONIC_TOOL_STEEL_RESTORE:
      snprintf(status, sizeof(status), "序号 %d/%d",
               count_number_tokens(text), selected_words);
      break;
    case MNEMONIC_TOOL_BIP85_MNEMONIC:
      snprintf(status, sizeof(status), "索引");
      break;
    case MNEMONIC_TOOL_SECONDARY_SHIFT:
    case MNEMONIC_TOOL_SECONDARY_ADD:
    case MNEMONIC_TOOL_SECONDARY_SUB: {
      bool invalid = false;
      int count = count_shift_tokens(text, &invalid);
      int target = current_mnemonic_word_count();
      if (target <= 0) {
        snprintf(status, sizeof(status), "请先加载助记词");
      } else if (invalid) {
        snprintf(status, sizeof(status), "格式错：%d/%d", count, target);
      } else {
        snprintf(status, sizeof(status), "加减 %d/%d", count, target);
      }
      break;
    }
    default:
      snprintf(status, sizeof(status), "完成后点完成");
      break;
    }
    lv_label_set_text(tool_count_label, status);
    lv_obj_set_style_text_color(tool_count_label,
                                ready ? yes_color() : secondary_color(),
                                0);
  }

  if (current_mode == MNEMONIC_TOOL_BINARY_ENTROPY && tool_keyboard) {
    bool invalid = false;
    int bits = count_binary_bits(text, &invalid);
    int target = target_entropy_bits_for_word_count(selected_words);
    bool finished = !invalid && bits == target;
    bool has_text = text && text[0] != '\0';

    if (finished) {
      lv_buttonmatrix_set_button_ctrl(tool_keyboard, BINARY_BTN_0,
                                      LV_BUTTONMATRIX_CTRL_DISABLED);
      lv_buttonmatrix_set_button_ctrl(tool_keyboard, BINARY_BTN_1,
                                      LV_BUTTONMATRIX_CTRL_DISABLED);
      lv_buttonmatrix_clear_button_ctrl(tool_keyboard, BINARY_BTN_DONE,
                                        LV_BUTTONMATRIX_CTRL_DISABLED);
    } else {
      lv_buttonmatrix_clear_button_ctrl(tool_keyboard, BINARY_BTN_0,
                                        LV_BUTTONMATRIX_CTRL_DISABLED);
      lv_buttonmatrix_clear_button_ctrl(tool_keyboard, BINARY_BTN_1,
                                        LV_BUTTONMATRIX_CTRL_DISABLED);
      lv_buttonmatrix_set_button_ctrl(tool_keyboard, BINARY_BTN_DONE,
                                      LV_BUTTONMATRIX_CTRL_DISABLED);
    }

    if (has_text)
      lv_buttonmatrix_clear_button_ctrl(tool_keyboard, BINARY_BTN_DELETE,
                                        LV_BUTTONMATRIX_CTRL_DISABLED);
    else
      lv_buttonmatrix_set_button_ctrl(tool_keyboard, BINARY_BTN_DELETE,
                                      LV_BUTTONMATRIX_CTRL_DISABLED);
  }

  (void)ready;
}

static void tool_textarea_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_VALUE_CHANGED || code == LV_EVENT_INSERT)
    tool_update_count();
}

static void tool_keyboard_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED || !tool_textarea)
    return;

  lv_obj_t *keyboard = lv_event_get_current_target(e);
  uint32_t btn_id = lv_buttonmatrix_get_selected_button(keyboard);
  if (btn_id == LV_BUTTONMATRIX_BUTTON_NONE)
    return;

  const char *txt = lv_buttonmatrix_get_button_text(keyboard, btn_id);
  if (!txt)
    return;

  if (strcmp(txt, "完成") == 0) {
    input_ready_cb(e);
  } else if (strcmp(txt, "删") == 0) {
    lv_textarea_delete_char(tool_textarea);
  } else if (strcmp(txt, "<") == 0) {
    lv_textarea_cursor_left(tool_textarea);
  } else if (strcmp(txt, ">") == 0) {
    lv_textarea_cursor_right(tool_textarea);
  } else if (strcmp(txt, "空") == 0) {
    lv_textarea_add_char(tool_textarea, ' ');
  } else {
    lv_textarea_add_text(tool_textarea, txt);
  }

  tool_update_count();
}

static void custom_keyboard_for_mode(const char *const **map_out,
                                     const lv_buttonmatrix_ctrl_t **ctrl_out) {
  switch (current_mode) {
  case MNEMONIC_TOOL_BINARY_ENTROPY:
    *map_out = binary_keyboard_map;
    *ctrl_out = binary_keyboard_ctrl_map;
    return;
  case MNEMONIC_TOOL_D6_ENTROPY:
    *map_out = d6_keyboard_map;
    *ctrl_out = d6_keyboard_ctrl_map;
    return;
  case MNEMONIC_TOOL_HEX_ENTROPY:
    *map_out = hex_keyboard_map;
    *ctrl_out = hex_keyboard_ctrl_map;
    return;
  case MNEMONIC_TOOL_CARD_ENTROPY:
    *map_out = card_keyboard_map;
    *ctrl_out = card_keyboard_ctrl_map;
    return;
  case MNEMONIC_TOOL_SECONDARY_SHIFT:
  case MNEMONIC_TOOL_SECONDARY_ADD:
  case MNEMONIC_TOOL_SECONDARY_SUB:
    *map_out = shift_keyboard_map;
    *ctrl_out = shift_keyboard_ctrl_map;
    return;
  default:
    *map_out = digit_keyboard_map;
    *ctrl_out = digit_keyboard_ctrl_map;
    return;
  }
}

static const char *accepted_chars_for_mode(void) {
  switch (current_mode) {
  case MNEMONIC_TOOL_BINARY_ENTROPY:
    return "01 ";
  case MNEMONIC_TOOL_D6_ENTROPY:
    return "123456 ";
  case MNEMONIC_TOOL_HEX_ENTROPY:
    return "0123456789abcdefABCDEF ";
  case MNEMONIC_TOOL_CARD_ENTROPY:
    return "AKQJTCDHSakqjtcdhs23456789 ";
  case MNEMONIC_TOOL_SECONDARY_SHIFT:
  case MNEMONIC_TOOL_SECONDARY_ADD:
  case MNEMONIC_TOOL_SECONDARY_SUB:
    return "0123456789+- ";
  default:
    return "0123456789 ";
  }
}

static const char *placeholder_for_mode(void) {
  switch (current_mode) {
  case MNEMONIC_TOOL_BINARY_ENTROPY:
    return "0/1，可空格分组";
  case MNEMONIC_TOOL_HEX_ENTROPY:
    return "32/40/48/56/64 位十六进制";
  case MNEMONIC_TOOL_D6_ENTROPY:
    return "六面骰 1-6";
  case MNEMONIC_TOOL_D20_ENTROPY:
    return "D20 点数 1-20";
  case MNEMONIC_TOOL_CARD_ENTROPY:
    return "AH QS 9D TC，10=T";
  case MNEMONIC_TOOL_SECONDARY_SHIFT:
  case MNEMONIC_TOOL_SECONDARY_ADD:
  case MNEMONIC_TOOL_SECONDARY_SUB:
    return "+N / -N，空格分隔";
  default:
    return "0-2047 序号";
  }
}

static char *parse_d20_mnemonic(const char *text) {
  char copy[TOOL_TEXT_MAX];
  snprintf(copy, sizeof(copy), "%s", text ? text : "");

  int count = 0;
  char normalized[TOOL_TEXT_MAX];
  normalized[0] = '\0';
  char *save = NULL;
  for (char *tok = strtok_r(copy, " ,;\n\r\t", &save); tok;
       tok = strtok_r(NULL, " ,;\n\r\t", &save)) {
    uint32_t value = 0;
    if (!mnemonic_tools_parse_uint32(tok, &value) || value < 1 || value > 20) {
      secure_memzero(copy, sizeof(copy));
      secure_memzero(normalized, sizeof(normalized));
      return NULL;
    }
    char part[8];
    snprintf(part, sizeof(part), "%s%u", count ? "-" : "", (unsigned)value);
    strncat(normalized, part, sizeof(normalized) - strlen(normalized) - 1);
    count++;
  }

  int min_count = min_d20_rolls_for_word_count(selected_words);
  if (count < min_count) {
    secure_memzero(copy, sizeof(copy));
    secure_memzero(normalized, sizeof(normalized));
    return NULL;
  }

  char *mnemonic = mnemonic_tools_from_sha256_text(
      normalized, entropy_len_for_word_count(selected_words));
  secure_memzero(copy, sizeof(copy));
  secure_memzero(normalized, sizeof(normalized));
  return mnemonic;
}

static bool normalize_card_token(const char *tok, char out[3]) {
  if (!tok || !out)
    return false;

  char compact[3] = {0};
  size_t pos = 0;
  for (const char *p = tok; *p; p++) {
    if (isspace((unsigned char)*p))
      continue;
    if (pos >= 2)
      return false;
    compact[pos++] = *p;
  }
  if (pos != 2)
    return false;

  char rank = (char)toupper((unsigned char)compact[0]);
  char suit = (char)toupper((unsigned char)compact[1]);
  const char *ranks = "A23456789TJQK";
  const char *suits = "CDHS";
  if (!strchr(ranks, rank) || !strchr(suits, suit))
    return false;
  out[0] = rank;
  out[1] = suit;
  out[2] = '\0';
  return true;
}

static char *parse_card_mnemonic(const char *text) {
  return mnemonic_tools_from_card_entropy(
      text, entropy_len_for_word_count(selected_words), NULL, NULL);
}

static char *parse_secondary_shift_mnemonic(const char *text,
                                            char default_operator) {
  (void)default_operator;
  bool invalid = false;
  int shift_count = count_shift_tokens(text, &invalid);
  int word_count = current_mnemonic_word_count();
  if (word_count <= 0 || invalid || shift_count != word_count)
    return NULL;

  char *source = NULL;
  if (!key_get_mnemonic(&source))
    return NULL;

  char *mnemonic = mnemonic_tools_secondary_shift(source, text, '+');
  SECURE_FREE_STRING(source);
  return mnemonic;
}

static void input_ready_cb(lv_event_t *e) {
  (void)e;
  const char *text =
      tool_textarea
          ? lv_textarea_get_text(tool_textarea)
          : (input.textarea ? lv_textarea_get_text(input.textarea) : "");
  char *mnemonic = NULL;

  switch (current_mode) {
  case MNEMONIC_TOOL_INDEX_IMPORT:
    mnemonic = parse_index_mnemonic(text);
    break;
  case MNEMONIC_TOOL_HEX_ENTROPY:
    mnemonic = parse_hex_mnemonic(text);
    break;
  case MNEMONIC_TOOL_BINARY_ENTROPY:
    mnemonic = parse_binary_mnemonic(text);
    break;
  case MNEMONIC_TOOL_D6_ENTROPY:
    mnemonic = parse_d6_mnemonic(text);
    break;
  case MNEMONIC_TOOL_D20_ENTROPY:
    mnemonic = parse_d20_mnemonic(text);
    break;
  case MNEMONIC_TOOL_CARD_ENTROPY:
    mnemonic = parse_card_mnemonic(text);
    break;
  case MNEMONIC_TOOL_BIP85_MNEMONIC: {
    uint32_t index = 0;
    if (mnemonic_tools_parse_uint32(text, &index))
      mnemonic = mnemonic_tools_bip85_child((uint32_t)selected_words, index);
    break;
  }
  case MNEMONIC_TOOL_XOR_CURRENT:
    mnemonic = mnemonic_tools_xor_with_current(text);
    break;
  case MNEMONIC_TOOL_SECONDARY_SHIFT:
    mnemonic = parse_secondary_shift_mnemonic(text, '+');
    break;
  case MNEMONIC_TOOL_SECONDARY_ADD:
    mnemonic = parse_secondary_shift_mnemonic(text, '+');
    break;
  case MNEMONIC_TOOL_SECONDARY_SUB:
    mnemonic = parse_secondary_shift_mnemonic(text, '+');
    break;
  case MNEMONIC_TOOL_STEEL_RESTORE:
    mnemonic = parse_index_mnemonic(text);
    break;
  }

  complete_with_mnemonic(mnemonic);
}

static void run_btn_cb(lv_event_t *e) { input_ready_cb(e); }

static void input_focus_cb(lv_event_t *e) {
  (void)e;
  if (input.keyboard)
    lv_obj_clear_flag(input.keyboard, LV_OBJ_FLAG_HIDDEN);
}

static const char *run_button_label(void) {
  switch (current_mode) {
  case MNEMONIC_TOOL_INDEX_IMPORT:
  case MNEMONIC_TOOL_STEEL_RESTORE:
    return "生成助记词";
  case MNEMONIC_TOOL_SECONDARY_SHIFT:
  case MNEMONIC_TOOL_SECONDARY_ADD:
  case MNEMONIC_TOOL_SECONDARY_SUB:
    return "执行转换";
  default:
    return "生成";
  }
}

static void style_run_button(lv_obj_t *btn) {
  if (!btn)
    return;
  lv_obj_set_style_bg_color(btn, bg_color(), 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(btn, highlight_color(), 0);
  lv_obj_set_style_border_width(btn, 2, 0);
  lv_obj_set_style_radius(btn, 8, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
}

static void back_btn_cb(lv_event_t *e) {
  (void)e;
  mnemonic_tool_page_hide();
  if (return_callback)
    return_callback();
}

static void back_menu_cb(void) {
  mnemonic_tool_page_hide();
  if (return_callback)
    return_callback();
}

static void create_text_input(void) {
  cleanup_input();

  theme_create_page_title(tool_screen, mode_title(current_mode));
  back_btn = ui_create_back_button(tool_screen, back_btn_cb);

  if (mode_uses_custom_keyboard(current_mode)) {
    tool_textarea = lv_textarea_create(tool_screen);
    lv_obj_set_width(tool_textarea, LV_PCT(92));
    lv_obj_set_height(tool_textarea,
                      theme_get_screen_height() >= 760 ? 250 : 155);
    lv_obj_align(tool_textarea, LV_ALIGN_TOP_MID, 0,
                 theme_get_screen_height() >= 760 ? 92 : 88);
    lv_textarea_set_one_line(tool_textarea, false);
    lv_textarea_set_max_length(tool_textarea, tool_max_length_for_mode());
    lv_textarea_set_accepted_chars(tool_textarea, accepted_chars_for_mode());
    lv_textarea_set_placeholder_text(tool_textarea, placeholder_for_mode());
    lv_textarea_set_cursor_click_pos(tool_textarea, true);
    lv_obj_set_style_text_font(tool_textarea, theme_font_medium(), 0);
    lv_obj_set_style_text_color(tool_textarea, main_color(), 0);
    lv_obj_set_style_bg_color(tool_textarea, bg_color(), 0);
    lv_obj_set_style_bg_opa(tool_textarea, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(tool_textarea, highlight_color(), 0);
    lv_obj_set_style_border_width(tool_textarea, 2, 0);
    lv_obj_set_style_radius(tool_textarea, 8, 0);
    lv_obj_set_style_pad_all(tool_textarea, 12, 0);
    lv_obj_set_style_bg_color(tool_textarea, highlight_color(), LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(tool_textarea, LV_OPA_COVER, LV_PART_CURSOR);
    lv_obj_add_event_cb(tool_textarea, tool_textarea_event_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    tool_count_label = theme_create_label(tool_screen, "", false);
    lv_obj_set_width(tool_count_label, LV_PCT(92));
    lv_label_set_long_mode(tool_count_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(tool_count_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(tool_count_label, theme_font_small(), 0);
    lv_obj_align_to(tool_count_label, tool_textarea, LV_ALIGN_OUT_BOTTOM_MID, 0,
                    8);

    const char *const *map = NULL;
    const lv_buttonmatrix_ctrl_t *ctrl = NULL;
    custom_keyboard_for_mode(&map, &ctrl);
    tool_keyboard = lv_buttonmatrix_create(tool_screen);
    lv_buttonmatrix_set_map(tool_keyboard, map);
    lv_buttonmatrix_set_ctrl_map(tool_keyboard, ctrl);
    lv_obj_set_width(tool_keyboard, LV_PCT(100));
    lv_obj_set_height(tool_keyboard,
                      theme_get_screen_height() >= 760 ? 310 : 245);
    lv_obj_align(tool_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    theme_apply_btnmatrix(tool_keyboard);
    lv_obj_set_style_bg_color(tool_keyboard, lv_color_black(), 0);
    lv_obj_set_style_border_width(tool_keyboard, 0, 0);
    lv_obj_set_style_pad_all(tool_keyboard, 8, 0);
    lv_obj_set_style_pad_gap(tool_keyboard, 8, 0);
    lv_obj_set_style_text_font(tool_keyboard, theme_font_medium(),
                               LV_PART_ITEMS);
    lv_obj_add_event_cb(tool_keyboard, tool_keyboard_event_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);
    tool_update_count();
    return;
  }

  ui_text_input_create(&input, tool_screen, "输入内容", false, input_ready_cb);
  if (input.textarea) {
    lv_textarea_set_one_line(input.textarea, false);
    lv_obj_set_width(input.textarea, LV_PCT(86));
    lv_obj_set_height(input.textarea, 92);
    lv_obj_align(input.textarea, LV_ALIGN_TOP_MID, 0, 112);
    lv_obj_set_style_bg_color(input.textarea, bg_color(), 0);
    lv_obj_set_style_bg_opa(input.textarea, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(input.textarea, main_color(), 0);
    lv_obj_set_style_border_color(input.textarea, highlight_color(), 0);
    lv_obj_set_style_border_width(input.textarea, 2, 0);
    lv_obj_set_style_radius(input.textarea, 8, 0);
    lv_obj_add_event_cb(input.textarea, input_focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(input.textarea, input_focus_cb, LV_EVENT_CLICKED, NULL);
  }

  run_btn = lv_btn_create(tool_screen);
  lv_obj_set_size(run_btn, LV_PCT(54), 48);
  lv_obj_align(run_btn, LV_ALIGN_TOP_MID, 0, 214);
  style_run_button(run_btn);
  lv_obj_add_event_cb(run_btn, run_btn_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *label = lv_label_create(run_btn);
  lv_label_set_text(label, run_button_label());
  lv_obj_set_style_text_font(label, theme_font_medium(), 0);
  lv_obj_set_style_text_color(label, highlight_color(), 0);
  lv_obj_center(label);
}

static void word_count_selected(int word_count) {
  selected_words = word_count;
  create_text_input();
}

static void word_count_12_cb(void) { word_count_selected(12); }

static void word_count_15_cb(void) { word_count_selected(15); }

static void word_count_18_cb(void) { word_count_selected(18); }

static void word_count_21_cb(void) { word_count_selected(21); }

static void word_count_24_cb(void) { word_count_selected(24); }

static bool mode_needs_word_count(mnemonic_tool_mode_t mode) {
  return mode == MNEMONIC_TOOL_HEX_ENTROPY ||
         mode == MNEMONIC_TOOL_BINARY_ENTROPY ||
         mode == MNEMONIC_TOOL_D6_ENTROPY ||
         mode == MNEMONIC_TOOL_D20_ENTROPY ||
         mode == MNEMONIC_TOOL_CARD_ENTROPY ||
         mode == MNEMONIC_TOOL_BIP85_MNEMONIC;
}

static void create_tool_word_count_menu(void) {
  cleanup_input();

  word_count_menu = ui_menu_create(tool_screen, "助记词长度", back_menu_cb);
  if (!word_count_menu)
    return;

  ui_menu_add_entry(word_count_menu, "12 个单词", word_count_12_cb);
  if (current_mode != MNEMONIC_TOOL_BIP85_MNEMONIC)
    ui_menu_add_entry(word_count_menu, "15 个单词", word_count_15_cb);
  ui_menu_add_entry(word_count_menu, "18 个单词", word_count_18_cb);
  if (current_mode != MNEMONIC_TOOL_BIP85_MNEMONIC)
    ui_menu_add_entry(word_count_menu, "21 个单词", word_count_21_cb);
  ui_menu_add_entry(word_count_menu, "24 个单词", word_count_24_cb);
  ui_menu_show(word_count_menu);
}

void mnemonic_tool_page_create(lv_obj_t *parent, void (*return_cb)(void),
                               mnemonic_tool_mode_t mode) {
  if (!parent)
    return;

  return_callback = return_cb;
  current_mode = mode;
  selected_words = 12;
  SECURE_FREE_STRING(completed_mnemonic);

  if ((mode == MNEMONIC_TOOL_BIP85_MNEMONIC ||
       mode == MNEMONIC_TOOL_XOR_CURRENT ||
       is_secondary_shift_mode(mode)) &&
      !key_is_loaded()) {
    dialog_show_error("请先加载助记词", return_cb, 0);
    return;
  }

  tool_screen = theme_create_page_container(parent);

  if (mode_needs_word_count(mode)) {
    create_tool_word_count_menu();
  } else {
    create_text_input();
  }
}

void mnemonic_tool_page_show(void) {
  if (tool_screen)
    lv_obj_clear_flag(tool_screen, LV_OBJ_FLAG_HIDDEN);
  ui_text_input_show(&input);
}

void mnemonic_tool_page_hide(void) {
  if (tool_screen)
    lv_obj_add_flag(tool_screen, LV_OBJ_FLAG_HIDDEN);
  ui_text_input_hide(&input);
}

void mnemonic_tool_page_destroy(void) {
  cleanup_input();
  if (tool_screen) {
    lv_obj_del(tool_screen);
    tool_screen = NULL;
  }
  return_callback = NULL;
  current_mode = MNEMONIC_TOOL_INDEX_IMPORT;
  selected_words = 12;
}

char *mnemonic_tool_page_get_completed_mnemonic(void) {
  char *result = completed_mnemonic;
  completed_mnemonic = NULL;
  return result;
}
