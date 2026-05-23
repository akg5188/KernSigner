#include "custom_derivation_page.h"
#include "../../core/custom_derivation.h"
#include "../../core/key.h"
#include "../../core/mnemonic_slots.h"
#include "../../core/wallet.h"
#include "../../i18n/i18n.h"
#include "../../smartcard/smartcard_satochip.h"
#include "../../ui/dialog.h"
#include "../../ui/input_helpers.h"
#include "../../ui/theme.h"
#include "../../utils/secure_mem.h"

#include <ctype.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

typedef enum {
  CUSTOM_DERIV_SOURCE_CURRENT = 0,
  CUSTOM_DERIV_SOURCE_SLOT = 1,
} custom_derivation_source_kind_t;

typedef struct {
  custom_derivation_source_kind_t kind;
  size_t slot_index;
  char label[64];
} custom_derivation_source_option_t;

typedef enum {
  CUSTOM_DERIV_FLOW_CHOOSE_SOURCE = 0,
  CUSTOM_DERIV_FLOW_CONFIGURE = 1,
} custom_derivation_flow_step_t;

typedef enum {
  CUSTOM_DERIV_ENTRY_NONE = 0,
  CUSTOM_DERIV_ENTRY_MNEMONIC = 1,
  CUSTOM_DERIV_ENTRY_SMARTCARD = 2,
} custom_derivation_entry_source_t;

static lv_obj_t *page_screen = NULL;
static lv_obj_t *source_choice_label = NULL;
static lv_obj_t *source_choice_row = NULL;
static lv_obj_t *source_label = NULL;
static lv_obj_t *source_dropdown = NULL;
static lv_obj_t *type_label = NULL;
static lv_obj_t *type_dropdown = NULL;
static lv_obj_t *path_label = NULL;
static lv_obj_t *smartcard_pin_label = NULL;
static lv_obj_t *result_card = NULL;
static lv_obj_t *result_title = NULL;
static lv_obj_t *result_body = NULL;
static lv_obj_t *result_qr = NULL;
static lv_obj_t *back_btn = NULL;
static lv_obj_t *generate_btn = NULL;
static lv_obj_t *generate_btn_label = NULL;
static ui_text_input_t path_input = {0};
static ui_text_input_t smartcard_pin_input = {0};
static void (*return_callback)(void) = NULL;
static void (*mnemonic_import_callback)(void) = NULL;
static custom_derivation_source_option_t source_options[MNEMONIC_SLOT_CAPACITY + 1];
static size_t source_option_count = 0;
static size_t source_selected_index = 0;
static custom_derivation_flow_step_t flow_step = CUSTOM_DERIV_FLOW_CHOOSE_SOURCE;
static custom_derivation_entry_source_t selected_entry =
    CUSTOM_DERIV_ENTRY_NONE;
static bool source_dropdown_syncing = false;

static void show_source_choice_stage(void);
static void show_detail_stage(custom_derivation_entry_source_t entry);
static void update_detail_source_controls(void);
static void layout_detail_controls(void);
static void create_source_choice_button(lv_obj_t *parent, const char *label,
                                       lv_event_cb_t cb);

static bool is_wave_43_portrait(void) {
  return theme_get_screen_width() <= 520 && theme_get_screen_height() >= 760;
}

static void back_cb(lv_event_t *e) {
  (void)e;
  if (flow_step == CUSTOM_DERIV_FLOW_CONFIGURE) {
    show_source_choice_stage();
    return;
  }

  custom_derivation_page_hide();
  if (return_callback)
    return_callback();
}

static custom_address_type_t selected_type(void) {
  uint16_t sel = type_dropdown ? lv_dropdown_get_selected(type_dropdown) : 0;
  switch (sel) {
  case 0:
    return CUSTOM_ADDR_BTC_P2PKH;
  case 1:
    return CUSTOM_ADDR_BTC_P2SH_P2WPKH;
  case 2:
    return CUSTOM_ADDR_BTC_P2WPKH;
  case 3:
    return CUSTOM_ADDR_BTC_P2TR;
  case 4:
  default:
    return CUSTOM_ADDR_EVM;
  }
}

static const char *selected_type_name(custom_address_type_t type) {
  switch (type) {
  case CUSTOM_ADDR_BTC_P2PKH:
    return i18n_tr_or("address.btc_legacy", "BTC Legacy");
  case CUSTOM_ADDR_BTC_P2SH_P2WPKH:
    return i18n_tr_or("address.btc_nested_segwit", "BTC Nested SegWit");
  case CUSTOM_ADDR_BTC_P2WPKH:
    return i18n_tr_or("address.btc_native_segwit", "BTC Native SegWit");
  case CUSTOM_ADDR_BTC_P2TR:
    return "BTC Taproot";
  case CUSTOM_ADDR_EVM:
  default:
    return "EVM";
  }
}

static const char *default_path_for_type(custom_address_type_t type,
                                         bool is_testnet) {
  switch (type) {
  case CUSTOM_ADDR_BTC_P2PKH:
    return is_testnet ? "m/44'/1'/0'/0/0" : "m/44'/0'/0'/0/0";
  case CUSTOM_ADDR_BTC_P2SH_P2WPKH:
    return is_testnet ? "m/49'/1'/0'/0/0" : "m/49'/0'/0'/0/0";
  case CUSTOM_ADDR_BTC_P2WPKH:
    return is_testnet ? "m/84'/1'/0'/0/0" : "m/84'/0'/0'/0/0";
  case CUSTOM_ADDR_BTC_P2TR:
    return is_testnet ? "m/86'/1'/0'/0/0" : "m/86'/0'/0'/0/0";
  case CUSTOM_ADDR_EVM:
  default:
    return "m/44'/60'/0'/0/0";
  }
}

static smartcard_satochip_btc_script_t
smartcard_script_for_type(custom_address_type_t type) {
  switch (type) {
  case CUSTOM_ADDR_BTC_P2PKH:
    return SMARTCARD_SATOCHIP_BTC_P2PKH;
  case CUSTOM_ADDR_BTC_P2SH_P2WPKH:
    return SMARTCARD_SATOCHIP_BTC_P2SH_P2WPKH;
  case CUSTOM_ADDR_BTC_P2WPKH:
    return SMARTCARD_SATOCHIP_BTC_P2WPKH;
  case CUSTOM_ADDR_BTC_P2TR:
  default:
    return SMARTCARD_SATOCHIP_BTC_P2TR;
  }
}

static const custom_derivation_source_option_t *selected_source(void) {
  if (source_selected_index >= source_option_count)
    return NULL;
  return &source_options[source_selected_index];
}

static const char *selected_source_name(void) {
  if (selected_entry == CUSTOM_DERIV_ENTRY_SMARTCARD)
    return i18n_tr_or("menu.smartcard", "Smartcard");
  const custom_derivation_source_option_t *source = selected_source();
  return source ? source->label : i18n_tr_or("menu.mnemonic", "Mnemonic");
}

static void set_generate_button_text(const char *text) {
  if (generate_btn_label)
    lv_label_set_text(generate_btn_label,
                      text ? text
                           : i18n_tr_or("address.generate_address",
                                        "Generate Address"));
}

static void hide_result_card(void) {
  if (result_card)
    lv_obj_add_flag(result_card, LV_OBJ_FLAG_HIDDEN);
}

static void show_result_card(void) {
  if (result_card) {
    lv_obj_clear_flag(result_card, LV_OBJ_FLAG_HIDDEN);
    lv_obj_scroll_to_view_recursive(result_card, LV_ANIM_OFF);
  }
}

static void set_obj_visible(lv_obj_t *obj, bool visible) {
  if (!obj)
    return;
  if (visible)
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static void style_black_orange_box(lv_obj_t *obj, int radius) {
  if (!obj)
    return;
  lv_obj_set_style_bg_color(obj, bg_color(), LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(obj, main_color(), LV_STATE_DEFAULT);
  lv_obj_set_style_border_color(obj, highlight_color(), LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(obj, 2, LV_STATE_DEFAULT);
  lv_obj_set_style_radius(obj, radius, LV_STATE_DEFAULT);
  lv_obj_set_style_shadow_width(obj, 0, LV_STATE_DEFAULT);
  lv_obj_set_style_outline_width(obj, 0, LV_STATE_DEFAULT);
}

static void style_form_textarea(lv_obj_t *textarea) {
  style_black_orange_box(textarea, 8);
  lv_obj_set_style_text_color(textarea, main_color(), 0);
  lv_obj_set_style_text_color(textarea, main_color(),
                              LV_PART_TEXTAREA_PLACEHOLDER);
  lv_obj_set_style_bg_color(textarea, highlight_color(), LV_PART_CURSOR);
  lv_obj_set_style_bg_opa(textarea, LV_OPA_COVER, LV_PART_CURSOR);
  lv_obj_set_style_width(textarea, 4, LV_PART_CURSOR);
}

static void dropdown_style_cb(lv_event_t *e) {
  lv_obj_t *dropdown = lv_event_get_target(e);
  lv_obj_t *list = dropdown ? lv_dropdown_get_list(dropdown) : NULL;
  if (!list)
    return;

  style_black_orange_box(list, 8);
  lv_obj_set_style_text_color(list, main_color(), 0);
  lv_obj_set_style_bg_color(list, bg_color(), LV_PART_SELECTED);
  lv_obj_set_style_text_color(list, main_color(), LV_PART_SELECTED);
  lv_obj_set_style_bg_color(list, highlight_color(),
                            LV_PART_SELECTED | LV_STATE_CHECKED);
  lv_obj_set_style_text_color(list, main_color(),
                              LV_PART_SELECTED | LV_STATE_CHECKED);
}

static void style_dropdown_control(lv_obj_t *dropdown) {
  style_black_orange_box(dropdown, 8);
  lv_obj_set_style_text_color(dropdown, main_color(), 0);
  lv_dropdown_set_symbol(dropdown, "v");
  lv_obj_add_event_cb(dropdown, dropdown_style_cb, LV_EVENT_READY, NULL);
  lv_obj_add_event_cb(dropdown, dropdown_style_cb, LV_EVENT_CLICKED, NULL);
}

static void set_text_input_visible(ui_text_input_t *input, bool visible) {
  if (!input)
    return;
  if (visible) {
    if (input->textarea)
      lv_obj_clear_flag(input->textarea, LV_OBJ_FLAG_HIDDEN);
    if (input->eye_btn)
      lv_obj_clear_flag(input->eye_btn, LV_OBJ_FLAG_HIDDEN);
  } else {
    if (input->textarea)
      lv_obj_add_flag(input->textarea, LV_OBJ_FLAG_HIDDEN);
    if (input->eye_btn)
      lv_obj_add_flag(input->eye_btn, LV_OBJ_FLAG_HIDDEN);
    if (input->keyboard)
      lv_obj_add_flag(input->keyboard, LV_OBJ_FLAG_HIDDEN);
  }
}

static void hide_input_keyboard(ui_text_input_t *input) {
  if (input && input->keyboard)
    lv_obj_add_flag(input->keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void hide_all_input_keyboards(void) {
  hide_input_keyboard(&path_input);
  hide_input_keyboard(&smartcard_pin_input);
}

static void set_path_cursor_visible(bool visible) {
  if (path_input.textarea)
    lv_obj_set_style_bg_opa(path_input.textarea,
                            visible ? LV_OPA_COVER : LV_OPA_TRANSP,
                            LV_PART_CURSOR);
}

static void keep_back_button_visible(void) {
  if (!back_btn)
    return;
  lv_obj_clear_flag(back_btn, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(back_btn, LV_OBJ_FLAG_FLOATING);
  lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, theme_get_small_padding(),
               theme_get_small_padding());
  lv_obj_move_foreground(back_btn);
}

static void focus_text_input(ui_text_input_t *target) {
  if (!target || !target->textarea)
    return;

  ui_text_input_t *other =
      (target == &path_input) ? &smartcard_pin_input : &path_input;
  hide_input_keyboard(other);
  if (other && other->textarea)
    lv_obj_remove_state(other->textarea, LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
  set_path_cursor_visible(target == &path_input);
  if (target->keyboard) {
    lv_obj_clear_flag(target->keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(target->keyboard);
    lv_keyboard_set_textarea(target->keyboard, target->textarea);
  }
  if (target->input_group)
    lv_group_focus_obj(target->textarea);
  lv_obj_update_layout(lv_screen_active());
  lv_obj_scroll_to_view_recursive(target->textarea, LV_ANIM_OFF);
  ui_textarea_keep_cursor_visible(target->textarea);
  keep_back_button_visible();
}

static void create_source_choice_button(lv_obj_t *parent, const char *label,
                                       lv_event_cb_t cb) {
  if (!parent)
    return;

  lv_obj_t *btn = lv_btn_create(parent);
  bool wide = theme_get_screen_width() >= 420;
  lv_obj_set_size(btn, wide ? LV_PCT(46) : LV_PCT(100),
                  wide ? 96 : theme_get_min_touch_size());
  theme_apply_touch_button(btn, false);
  style_black_orange_box(btn, 8);
  lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *text = lv_label_create(btn);
  lv_label_set_text(text, label);
  lv_obj_center(text);
  theme_apply_button_label(text, false);
  lv_obj_set_style_text_color(text, main_color(), 0);
}

static void ensure_result_card(void) {
  if (result_card || !page_screen)
    return;

  result_card = lv_obj_create(page_screen);
  lv_obj_set_width(result_card, LV_PCT(92));
  lv_obj_set_height(result_card, is_wave_43_portrait() ? 258 : 232);
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
  lv_obj_set_scrollbar_mode(page_screen, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scroll_dir(page_screen, LV_DIR_VER);
  lv_obj_set_style_pad_right(result_card, theme_get_small_padding() + 4, 0);

  result_title = theme_create_label(result_card, "", false);
  lv_obj_set_style_text_font(result_title, theme_font_medium(), 0);
  lv_obj_set_style_text_color(result_title, main_color(), 0);
  lv_obj_set_style_text_align(result_title, LV_TEXT_ALIGN_CENTER, 0);

  result_body = theme_create_label(result_card, "", false);
  lv_obj_set_width(result_body, LV_PCT(100));
  lv_label_set_long_mode(result_body, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(result_body, theme_font_small(), 0);
  lv_obj_set_style_text_color(result_body, main_color(), 0);
  lv_obj_set_style_text_align(result_body, LV_TEXT_ALIGN_CENTER, 0);

  result_qr = lv_qrcode_create(result_card);
  lv_qrcode_set_size(result_qr, 132);
  lv_qrcode_set_dark_color(result_qr, lv_color_hex(0x000000));
  lv_qrcode_set_light_color(result_qr, lv_color_hex(0xFFFFFF));
  lv_obj_set_style_border_color(result_qr, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_width(result_qr, 6, 0);
  lv_obj_set_style_radius(result_qr, 0, 0);
  lv_obj_add_flag(result_card, LV_OBJ_FLAG_HIDDEN);
}

static void show_failure(const char *title, const char *body) {
  ensure_result_card();
  if (!result_title || !result_body)
    return;

  hide_all_input_keyboards();
  lv_label_set_text(result_title,
                    title ? title : i18n_tr_or("dialog.failed", "Failed"));
  lv_label_set_text(result_body,
                    body ? body
                         : i18n_tr_or("dialog.operation_failed",
                                      "Operation failed"));
  if (result_qr)
    lv_obj_add_flag(result_qr, LV_OBJ_FLAG_HIDDEN);
  show_result_card();
}

static void normalize_path(char *dst, size_t dst_len, const char *src) {
  size_t pos = 0;
  for (const char *p = src; p && *p && pos + 1 < dst_len; p++) {
    if (isspace((unsigned char)*p))
      continue;
    dst[pos++] = (*p == 'h' || *p == 'H') ? '\'' : *p;
  }
  dst[pos] = '\0';
}

static void build_source_options(char *options_buf, size_t options_len,
                                 size_t *default_index_out) {
  if (options_buf && options_len > 0)
    options_buf[0] = '\0';
  source_option_count = 0;
  if (default_index_out)
    *default_index_out = 0;

  char current_fp[9] = {0};
  bool has_current = key_get_fingerprint_hex(current_fp);

  if (has_current && source_option_count < (sizeof(source_options) /
                                            sizeof(source_options[0]))) {
    snprintf(source_options[source_option_count].label,
             sizeof(source_options[source_option_count].label),
             i18n_tr_or("address.current_source_format", "Current %s"),
             current_fp);
    source_options[source_option_count].kind = CUSTOM_DERIV_SOURCE_CURRENT;
    source_options[source_option_count].slot_index = 0;
    source_option_count++;
  }

  for (size_t i = 0; i < MNEMONIC_SLOT_CAPACITY; i++) {
    mnemonic_slot_info_t info;
    if (!mnemonic_slots_get_info(i, &info) || !info.used)
      continue;
    if (has_current && strcmp(current_fp, info.fingerprint) == 0)
      continue;
    if (source_option_count >= (sizeof(source_options) /
                                sizeof(source_options[0])))
      break;

    snprintf(source_options[source_option_count].label,
             sizeof(source_options[source_option_count].label),
             i18n_tr_or("wallet.mnemonic_slot_format", "Mnemonic %u %s"),
             (unsigned)(i + 1), info.fingerprint);
    source_options[source_option_count].kind = CUSTOM_DERIV_SOURCE_SLOT;
    source_options[source_option_count].slot_index = i;
    source_option_count++;
  }

  if (source_option_count == 0) {
    if (options_buf && options_len > 0)
      options_buf[0] = '\0';
    return;
  }

  if (options_buf && options_len > 0) {
    for (size_t i = 0; i < source_option_count; i++) {
      if (i > 0)
        strncat(options_buf, "\n", options_len - strlen(options_buf) - 1);
      strncat(options_buf, source_options[i].label,
              options_len - strlen(options_buf) - 1);
    }
  }

  if (default_index_out) {
    size_t default_index = 0;
    if (!has_current) {
      for (size_t i = 0; i < source_option_count; i++) {
        if (source_options[i].kind == CUSTOM_DERIV_SOURCE_SLOT) {
          default_index = i;
          break;
        }
      }
    }
    *default_index_out = default_index;
  }
}

static void update_generate_button_label(void) {
  set_generate_button_text(i18n_tr_or("address.read_address", "Read Address"));
}

static void refresh_source_dropdown(void) {
  if (!source_dropdown)
    return;

  char options_buf[512];
  size_t default_index = 0;
  size_t previous_index = source_selected_index;
  source_dropdown_syncing = true;
  build_source_options(options_buf, sizeof(options_buf), &default_index);
  if (source_option_count > 0)
    lv_dropdown_set_options(source_dropdown, options_buf);
  if (source_option_count == 0) {
    source_selected_index = 0;
  } else {
    if (previous_index >= source_option_count)
      previous_index = default_index;
    source_selected_index = previous_index;
    lv_dropdown_set_selected(source_dropdown, (uint16_t)source_selected_index);
  }
  source_dropdown_syncing = false;
  update_generate_button_label();
}

static void update_detail_source_controls(void) {
  if (!source_label)
    return;

  if (selected_entry == CUSTOM_DERIV_ENTRY_SMARTCARD) {
    lv_label_set_text(source_label,
                      i18n_tr_or("address.source_smartcard",
                                 "Source: Smartcard"));
    set_obj_visible(source_dropdown, false);
    return;
  }

  const custom_derivation_source_option_t *source = selected_source();
  if (source_option_count > 1) {
    lv_label_set_text(source_label, i18n_tr_or("address.source", "Source"));
    set_obj_visible(source_dropdown, true);
    refresh_source_dropdown();
    return;
  }

  if (source)
    lv_label_set_text(source_label, source->label);
  else
    lv_label_set_text(source_label, i18n_tr_or("address.source", "Source"));
  set_obj_visible(source_dropdown, false);
}

static void show_source_choice_stage(void) {
  flow_step = CUSTOM_DERIV_FLOW_CHOOSE_SOURCE;
  selected_entry = CUSTOM_DERIV_ENTRY_NONE;
  hide_result_card();
  set_obj_visible(source_choice_label, true);
  set_obj_visible(source_choice_row, true);
  set_obj_visible(source_label, false);
  set_obj_visible(source_dropdown, false);
  set_obj_visible(type_label, false);
  set_obj_visible(type_dropdown, false);
  set_obj_visible(path_label, false);
  set_obj_visible(smartcard_pin_label, false);
  set_obj_visible(generate_btn, false);
  set_text_input_visible(&path_input, false);
  set_text_input_visible(&smartcard_pin_input, false);
  hide_all_input_keyboards();
  set_path_cursor_visible(true);
  update_generate_button_label();
}

static void show_detail_stage(custom_derivation_entry_source_t entry) {
  if (entry == CUSTOM_DERIV_ENTRY_MNEMONIC && source_option_count == 0) {
    if (mnemonic_import_callback) {
      mnemonic_import_callback();
      return;
    }
    dialog_show_error(i18n_tr_or("wallet.no_mnemonic_loaded",
                                 "Load a mnemonic first"),
                      NULL, 1600);
    return;
  }

  flow_step = CUSTOM_DERIV_FLOW_CONFIGURE;
  selected_entry = entry;
  hide_result_card();
  set_obj_visible(source_choice_label, false);
  set_obj_visible(source_choice_row, false);
  set_obj_visible(source_label, true);
  set_obj_visible(type_label, true);
  set_obj_visible(type_dropdown, true);
  set_obj_visible(path_label, true);
  set_obj_visible(generate_btn, true);
  set_obj_visible(smartcard_pin_label, entry == CUSTOM_DERIV_ENTRY_SMARTCARD);
  set_text_input_visible(&smartcard_pin_input,
                         entry == CUSTOM_DERIV_ENTRY_SMARTCARD);
  set_text_input_visible(&path_input, true);
  hide_all_input_keyboards();
  set_path_cursor_visible(entry != CUSTOM_DERIV_ENTRY_SMARTCARD);

  update_detail_source_controls();
  layout_detail_controls();
  update_generate_button_label();
  if (entry == CUSTOM_DERIV_ENTRY_SMARTCARD)
    focus_text_input(&smartcard_pin_input);
}

static bool render_mnemonic_address(const char *path,
                                    custom_address_type_t type,
                                    bool is_testnet, char *address,
                                    size_t address_len) {
  if (selected_entry != CUSTOM_DERIV_ENTRY_MNEMONIC)
    return false;

  const custom_derivation_source_option_t *source = selected_source();
  if (!source)
    return false;

  if (source->kind == CUSTOM_DERIV_SOURCE_CURRENT) {
    if (!key_has_signing_key())
      return false;
    return custom_derivation_get_address(path, type, is_testnet, address,
                                         address_len);
  }

  if (source->kind == CUSTOM_DERIV_SOURCE_SLOT) {
    char *passphrase = NULL;
    (void)key_get_session_passphrase(&passphrase);
    bool loaded = mnemonic_slots_load(source->slot_index, passphrase);
    SECURE_FREE_STRING(passphrase);
    if (!key_has_signing_key())
      return false;
    if (!loaded && !wallet_is_initialized()) {
      /*
       * Some slots can still derive correctly even if the wallet init path
       * is not available.  The key is what custom derivation needs.
       */
    }
    return custom_derivation_get_address(path, type, is_testnet, address,
                                         address_len);
  }

  return false;
}

static void render_address_result(const char *path, custom_address_type_t type,
                                  const char *address) {
  ensure_result_card();
  if (!result_title || !result_body)
    return;

  hide_all_input_keyboards();
  char body[384];
  snprintf(body, sizeof(body),
           i18n_tr_or("address.result_body_format",
                      "Source: %s\nPath: %s\nType: %s\nAddress: %s"),
           selected_source_name(), path, selected_type_name(type), address);
  lv_label_set_text(result_title, i18n_tr_or("address.result", "Result"));
  lv_label_set_text(result_body, body);
  if (result_qr) {
    lv_qrcode_update(result_qr, address, (uint32_t)strlen(address));
    lv_obj_clear_flag(result_qr, LV_OBJ_FLAG_HIDDEN);
  }
  show_result_card();
}

static bool render_smartcard_address(const char *pin, const char *path,
                                     custom_address_type_t type,
                                     bool is_testnet, char *address,
                                     size_t address_len, char *err_body,
                                     size_t err_body_len) {
  if (!pin || !pin[0])
    return false;

  if (type == CUSTOM_ADDR_EVM) {
    smartcard_satochip_account_t account;
    memset(&account, 0, sizeof(account));
    esp_err_t err = smartcard_satochip_get_eth_account(pin, path, &account,
                                                      20000);
    if (err != ESP_OK || !account.has_address) {
      if (err_body && err_body_len > 0)
        snprintf(err_body, err_body_len, "%s", account.detail);
      secure_memzero(&account, sizeof(account));
      return false;
    }
    snprintf(address, address_len, "%s", account.address);
    secure_memzero(&account, sizeof(account));
    return true;
  }

  smartcard_satochip_btc_address_t btc;
  memset(&btc, 0, sizeof(btc));
  esp_err_t err = smartcard_satochip_get_btc_address(
      pin, path, smartcard_script_for_type(type), is_testnet, &btc, 20000);
  if (err != ESP_OK || !btc.has_address) {
    if (err_body && err_body_len > 0)
      snprintf(err_body, err_body_len, "%s", btc.detail);
    secure_memzero(&btc, sizeof(btc));
    return false;
  }
  snprintf(address, address_len, "%s", btc.address);
  secure_memzero(&btc, sizeof(btc));
  return true;
}

static void render_success(const char *path, custom_address_type_t type,
                           bool is_testnet, const char *address) {
  render_address_result(path, type, address);
  (void)is_testnet;
}

static void generate_now(const char *path, custom_address_type_t type,
                         bool is_testnet) {
  ensure_result_card();
  if (!result_title || !result_body)
    return;

  const custom_derivation_source_option_t *source = selected_source();
  custom_derivation_source_kind_t source_kind =
      source ? source->kind : CUSTOM_DERIV_SOURCE_CURRENT;
  char address[128];
  if (!render_mnemonic_address(path, type, is_testnet, address,
                               sizeof(address))) {
    show_failure(i18n_tr_or("dialog.failed", "Failed"),
                 i18n_tr_or("address.mnemonic_or_path_invalid",
                            "Mnemonic or path is invalid"));
    return;
  }

  render_success(path, type, is_testnet, address);
  if (source_kind == CUSTOM_DERIV_SOURCE_SLOT)
    refresh_source_dropdown();
}

static void smartcard_pin_ready_cb(lv_event_t *e) {
  (void)e;
  if (selected_entry != CUSTOM_DERIV_ENTRY_SMARTCARD ||
      !smartcard_pin_input.textarea)
    return;

  const char *path_text =
      path_input.textarea ? lv_textarea_get_text(path_input.textarea) : "";
  custom_address_type_t type = selected_type();
  bool is_testnet = wallet_get_network() == WALLET_NETWORK_TESTNET;
  char path[128];
  normalize_path(path, sizeof(path),
                 (path_text && path_text[0])
                     ? path_text
                     : default_path_for_type(type, is_testnet));
  if (path_input.textarea)
    lv_textarea_set_text(path_input.textarea, path);
  if (path[0] == '\0') {
    dialog_show_error(i18n_tr_or("address.enter_path", "Enter path"), NULL,
                      1600);
    focus_text_input(&path_input);
    return;
  }

  const char *pin_text = lv_textarea_get_text(smartcard_pin_input.textarea);
  if (!pin_text || pin_text[0] == '\0') {
    dialog_show_error(i18n_tr_or("smartcard.enter_pin", "Smartcard PIN"), NULL,
                      1600);
    focus_text_input(&smartcard_pin_input);
    return;
  }

  char pin_copy[80];
  snprintf(pin_copy, sizeof(pin_copy), "%s", pin_text);
  lv_textarea_set_text(smartcard_pin_input.textarea, "");
  hide_all_input_keyboards();

  char address[128];
  char err_body[256] = {0};
  bool ok = render_smartcard_address(pin_copy, path, type, is_testnet,
                                     address, sizeof(address), err_body,
                                     sizeof(err_body));
  secure_memzero(pin_copy, sizeof(pin_copy));

  if (!ok) {
    show_failure(i18n_tr_or("dialog.failed", "Failed"),
                 err_body[0] ? err_body
                             : i18n_tr_or("smartcard.read_failed",
                                          "Smartcard read failed"));
    return;
  }

  render_address_result(path, type, address);
}

static void input_focus_cb(lv_event_t *e) {
  ui_text_input_t *target = lv_event_get_user_data(e);
  if (!target || !target->textarea)
    return;

  lv_event_code_t code = lv_event_get_code(e);
  ui_text_input_t *other =
      (target == &path_input) ? &smartcard_pin_input : &path_input;
  hide_input_keyboard(other);
  if (other && other->textarea)
    lv_obj_remove_state(other->textarea, LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
  set_path_cursor_visible(target == &path_input);
  if (target->keyboard) {
    lv_obj_clear_flag(target->keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(target->keyboard);
    lv_keyboard_set_textarea(target->keyboard, target->textarea);
  }
  if (code != LV_EVENT_FOCUSED && target->input_group && target->textarea)
    lv_group_focus_obj(target->textarea);
  lv_obj_update_layout(lv_screen_active());
  lv_obj_scroll_to_view_recursive(target->textarea, LV_ANIM_OFF);
  keep_back_button_visible();
}

static void input_cursor_cb(lv_event_t *e) {
  ui_text_input_t *target = lv_event_get_user_data(e);
  if (target && target->textarea)
    ui_textarea_keep_cursor_visible(target->textarea);
}

static void invalidate_result_cb(lv_event_t *e) {
  (void)e;
  hide_result_card();
}

static void style_generate_button(lv_obj_t *btn) {
  if (!btn)
    return;
  style_black_orange_box(btn, 8);
}

static void source_changed_cb(lv_event_t *e) {
  (void)e;
  if (!source_dropdown || source_dropdown_syncing)
    return;

  source_selected_index = lv_dropdown_get_selected(source_dropdown);
  if (source_selected_index >= source_option_count)
    source_selected_index = 0;

  hide_result_card();
  update_generate_button_label();
}

static void type_changed_cb(lv_event_t *e) {
  (void)e;
  if (!path_input.textarea)
    return;

  bool is_testnet = wallet_get_network() == WALLET_NETWORK_TESTNET;
  lv_textarea_set_text(path_input.textarea,
                       default_path_for_type(selected_type(), is_testnet));
  hide_result_card();
}

static void path_ready_cb(lv_event_t *e) {
  (void)e;
  const char *text = path_input.textarea ? lv_textarea_get_text(path_input.textarea)
                                         : "";
  custom_address_type_t type = selected_type();
  bool is_testnet = wallet_get_network() == WALLET_NETWORK_TESTNET;
  char path[128];
  normalize_path(path, sizeof(path),
                 (text && text[0]) ? text : default_path_for_type(type, is_testnet));
  if (path_input.textarea)
    lv_textarea_set_text(path_input.textarea, path);

  if (selected_entry == CUSTOM_DERIV_ENTRY_SMARTCARD) {
    smartcard_pin_ready_cb(e);
    return;
  }

  if (selected_entry != CUSTOM_DERIV_ENTRY_MNEMONIC) {
    show_failure(i18n_tr_or("dialog.failed", "Failed"),
                 i18n_tr_or("address.choose_source", "Choose source"));
    return;
  }

  generate_now(path, type, is_testnet);
}

static void generate_btn_cb(lv_event_t *e) {
  (void)e;
  path_ready_cb(e);
}

static lv_obj_t *create_page_label(lv_obj_t *parent, const char *text, int y) {
  lv_obj_t *label = theme_create_label(parent, text, false);
  lv_obj_set_width(label, LV_PCT(90));
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_set_style_text_color(label, main_color(), 0);
  lv_obj_set_style_text_font(label, theme_font_small(), 0);
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, y);
  return label;
}

static void layout_detail_controls(void) {
  bool tall = is_wave_43_portrait();
  bool smartcard = selected_entry == CUSTOM_DERIV_ENTRY_SMARTCARD;
  bool source_picker =
      selected_entry == CUSTOM_DERIV_ENTRY_MNEMONIC && source_option_count > 1;

  int source_y = tall ? 62 : 50;
  int type_label_y = tall ? 118 : 98;
  int type_dropdown_y = tall ? 142 : 122;
  int path_label_y = tall ? 198 : 172;
  int path_y = tall ? 222 : 196;
  int pin_label_y = tall ? 294 : 258;
  int pin_y = tall ? 318 : 282;
  int button_y = tall ? 410 : 360;

  if (source_picker) {
    type_label_y = tall ? 146 : 130;
    type_dropdown_y = tall ? 170 : 154;
    path_label_y = tall ? 226 : 210;
    path_y = tall ? 250 : 234;
    button_y = tall ? 336 : 320;
  } else if (!smartcard) {
    button_y = tall ? 310 : 286;
  }

  if (source_label)
    lv_obj_align(source_label, LV_ALIGN_TOP_MID, 0, source_y);
  if (source_dropdown)
    lv_obj_align(source_dropdown, LV_ALIGN_TOP_MID, 0, tall ? 86 : 74);
  if (type_label)
    lv_obj_align(type_label, LV_ALIGN_TOP_MID, 0, type_label_y);
  if (type_dropdown)
    lv_obj_align(type_dropdown, LV_ALIGN_TOP_MID, 0, type_dropdown_y);
  if (path_label)
    lv_obj_align(path_label, LV_ALIGN_TOP_MID, 0, path_label_y);
  if (path_input.textarea)
    lv_obj_align(path_input.textarea, LV_ALIGN_TOP_MID, 0, path_y);
  if (smartcard_pin_label)
    lv_obj_align(smartcard_pin_label, LV_ALIGN_TOP_MID, 0, pin_label_y);
  if (smartcard_pin_input.textarea)
    lv_obj_align(smartcard_pin_input.textarea, LV_ALIGN_TOP_LEFT,
                 LV_HOR_RES * 5 / 100, pin_y);
  if (smartcard_pin_input.eye_btn)
    lv_obj_align_to(smartcard_pin_input.eye_btn, smartcard_pin_input.textarea,
                    LV_ALIGN_OUT_RIGHT_MID, 5, 0);
  if (generate_btn)
    lv_obj_align(generate_btn, LV_ALIGN_TOP_MID, 0, button_y);
}

static void mnemonic_choice_cb(lv_event_t *e) {
  (void)e;
  show_detail_stage(CUSTOM_DERIV_ENTRY_MNEMONIC);
}

static void smartcard_choice_cb(lv_event_t *e) {
  (void)e;
  show_detail_stage(CUSTOM_DERIV_ENTRY_SMARTCARD);
}

void custom_derivation_page_create_with_import(lv_obj_t *parent,
                                               void (*return_cb)(void),
                                               void (*mnemonic_import_cb)(void)) {
  if (!parent)
    return;

  return_callback = return_cb;
  mnemonic_import_callback = mnemonic_import_cb;
  page_screen = theme_create_page_container(parent);
  lv_obj_set_style_bg_color(page_screen, bg_color(), 0);
  lv_obj_add_flag(page_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(page_screen, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(page_screen, LV_SCROLLBAR_MODE_OFF);
  theme_create_page_title(page_screen,
                          i18n_tr_or("address.derive_address",
                                     "Derive address"));
  back_btn = ui_create_back_button(page_screen, back_cb);

  char source_options_text[512];
  size_t source_default_index = 0;
  build_source_options(source_options_text, sizeof(source_options_text),
                       &source_default_index);

  source_choice_label =
      create_page_label(page_screen,
                        i18n_tr_or("address.choose_source", "Choose source"),
                        80);
  source_choice_row = lv_obj_create(page_screen);
  lv_obj_set_width(source_choice_row, LV_PCT(92));
  lv_obj_set_height(source_choice_row, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(source_choice_row, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(source_choice_row, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(source_choice_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(source_choice_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(source_choice_row, 0, 0);
  lv_obj_set_style_pad_all(source_choice_row, 0, 0);
  lv_obj_set_style_pad_gap(source_choice_row, theme_get_small_padding(), 0);
  lv_obj_set_style_margin_top(source_choice_row, theme_get_small_padding(), 0);
  lv_obj_align(source_choice_row, LV_ALIGN_TOP_MID, 0, 112);

  create_source_choice_button(source_choice_row,
                              i18n_tr_or("menu.mnemonic", "Mnemonic"),
                              mnemonic_choice_cb);
  create_source_choice_button(source_choice_row,
                              i18n_tr_or("menu.smartcard", "Smartcard"),
                              smartcard_choice_cb);

  if (source_option_count > 1) {
    source_label =
        create_page_label(page_screen, i18n_tr_or("address.source", "Source"),
                          54);
    source_dropdown = theme_create_dropdown(page_screen, source_options_text);
    lv_obj_set_width(source_dropdown, LV_PCT(90));
    lv_obj_align(source_dropdown, LV_ALIGN_TOP_MID, 0, 76);
    style_dropdown_control(source_dropdown);
    lv_obj_add_event_cb(source_dropdown, source_changed_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);
    source_selected_index = source_default_index < source_option_count
                                ? source_default_index
                                : 0;
    lv_dropdown_set_selected(source_dropdown, (uint16_t)source_selected_index);
    set_obj_visible(source_label, false);
    set_obj_visible(source_dropdown, false);
  } else if (source_option_count == 1) {
    source_label = create_page_label(page_screen, source_options[0].label, 54);
    set_obj_visible(source_label, false);
  } else {
    source_label =
        create_page_label(page_screen, i18n_tr_or("address.source", "Source"),
                          54);
    set_obj_visible(source_label, false);
  }

  type_label =
      create_page_label(page_screen, i18n_tr_or("address.type", "Type"), 118);
  char type_options[160];
  snprintf(type_options, sizeof(type_options), "%s\n%s\n%s\n%s\n%s",
           i18n_tr_or("address.btc_legacy", "BTC Legacy"),
           i18n_tr_or("address.btc_nested_segwit", "BTC Nested SegWit"),
           i18n_tr_or("address.btc_native_segwit", "BTC Native SegWit"),
           "Taproot", "EVM");
  type_dropdown = theme_create_dropdown(page_screen, type_options);
  lv_obj_set_width(type_dropdown, LV_PCT(90));
  lv_obj_align(type_dropdown, LV_ALIGN_TOP_MID, 0, 140);
  style_dropdown_control(type_dropdown);
  lv_obj_add_event_cb(type_dropdown, type_changed_cb, LV_EVENT_VALUE_CHANGED,
                      NULL);
  set_obj_visible(type_label, false);
  set_obj_visible(type_dropdown, false);

  path_label =
      create_page_label(page_screen, i18n_tr_or("address.path", "Path"), 184);
  set_obj_visible(path_label, false);
  smartcard_pin_label =
      create_page_label(page_screen,
                        i18n_tr_or("smartcard.enter_pin", "Smartcard PIN"),
                        284);
  set_obj_visible(smartcard_pin_label, false);

  ui_text_input_create(&path_input, page_screen, "m/...", false, path_ready_cb);
  if (path_input.textarea) {
    lv_textarea_set_one_line(path_input.textarea, true);
    lv_textarea_set_cursor_click_pos(path_input.textarea, true);
    lv_obj_set_width(path_input.textarea, LV_PCT(90));
    lv_obj_set_height(path_input.textarea, 58);
    lv_obj_align(path_input.textarea, LV_ALIGN_TOP_MID, 0, 184);
    style_form_textarea(path_input.textarea);
    lv_obj_add_event_cb(path_input.textarea, input_focus_cb, LV_EVENT_FOCUSED,
                        &path_input);
    lv_obj_add_event_cb(path_input.textarea, input_focus_cb, LV_EVENT_CLICKED,
                        &path_input);
    lv_obj_add_event_cb(path_input.textarea, input_cursor_cb,
                        LV_EVENT_VALUE_CHANGED, &path_input);
    lv_obj_add_event_cb(path_input.textarea, input_cursor_cb, LV_EVENT_PRESSED,
                        &path_input);
    lv_obj_add_event_cb(path_input.textarea, invalidate_result_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);
    lv_textarea_set_text(
        path_input.textarea,
        default_path_for_type(CUSTOM_ADDR_BTC_P2PKH,
                              wallet_get_network() == WALLET_NETWORK_TESTNET));
  }
  ui_text_input_hide(&path_input);

  ui_text_input_create(&smartcard_pin_input, page_screen,
                       i18n_tr_or("smartcard.enter_pin", "Smartcard PIN"),
                       true, smartcard_pin_ready_cb);
  if (smartcard_pin_input.textarea) {
    lv_obj_set_width(smartcard_pin_input.textarea, LV_PCT(76));
    lv_obj_set_height(smartcard_pin_input.textarea, 58);
    lv_obj_align(smartcard_pin_input.textarea, LV_ALIGN_TOP_MID, 0, 184);
    style_form_textarea(smartcard_pin_input.textarea);
    lv_obj_add_event_cb(smartcard_pin_input.textarea, input_focus_cb,
                        LV_EVENT_FOCUSED, &smartcard_pin_input);
    lv_obj_add_event_cb(smartcard_pin_input.textarea, input_focus_cb,
                        LV_EVENT_CLICKED, &smartcard_pin_input);
    lv_obj_add_event_cb(smartcard_pin_input.textarea, input_cursor_cb,
                        LV_EVENT_VALUE_CHANGED, &smartcard_pin_input);
    lv_obj_add_event_cb(smartcard_pin_input.textarea, input_cursor_cb,
                        LV_EVENT_PRESSED, &smartcard_pin_input);
  }
  if (smartcard_pin_input.eye_btn)
    lv_obj_align_to(smartcard_pin_input.eye_btn, smartcard_pin_input.textarea,
                    LV_ALIGN_OUT_RIGHT_MID, 5, 0);
  if (smartcard_pin_input.eye_label)
    lv_obj_set_style_text_color(smartcard_pin_input.eye_label, main_color(), 0);
  ui_text_input_hide(&smartcard_pin_input);

  generate_btn = lv_btn_create(page_screen);
  lv_obj_set_size(generate_btn, LV_PCT(52), 48);
  lv_obj_align(generate_btn, LV_ALIGN_TOP_MID, 0, 258);
  style_generate_button(generate_btn);
  lv_obj_add_event_cb(generate_btn, generate_btn_cb, LV_EVENT_CLICKED, NULL);

  generate_btn_label = lv_label_create(generate_btn);
  lv_label_set_text(generate_btn_label,
                    i18n_tr_or("address.read_address", "Read Address"));
  lv_obj_set_style_text_font(generate_btn_label, theme_font_medium(), 0);
  lv_obj_set_style_text_color(generate_btn_label, main_color(), 0);
  lv_obj_center(generate_btn_label);
  set_obj_visible(generate_btn, false);

  hide_result_card();
  show_source_choice_stage();
}

void custom_derivation_page_create(lv_obj_t *parent, void (*return_cb)(void)) {
  custom_derivation_page_create_with_import(parent, return_cb, NULL);
}

void custom_derivation_page_show(void) {
  if (page_screen)
    lv_obj_clear_flag(page_screen, LV_OBJ_FLAG_HIDDEN);
  if (flow_step == CUSTOM_DERIV_FLOW_CONFIGURE) {
    set_obj_visible(source_choice_label, false);
    set_obj_visible(source_choice_row, false);
    set_obj_visible(source_label, true);
    set_obj_visible(type_label, true);
    set_obj_visible(type_dropdown, true);
    set_obj_visible(path_label, true);
    set_obj_visible(generate_btn, true);
    set_obj_visible(smartcard_pin_label,
                    selected_entry == CUSTOM_DERIV_ENTRY_SMARTCARD);
    set_text_input_visible(&path_input, true);
    set_text_input_visible(&smartcard_pin_input,
                           selected_entry == CUSTOM_DERIV_ENTRY_SMARTCARD);
    update_detail_source_controls();
    layout_detail_controls();
    update_generate_button_label();
    if (selected_entry == CUSTOM_DERIV_ENTRY_SMARTCARD)
      focus_text_input(&smartcard_pin_input);
    else
      hide_all_input_keyboards();
  } else {
    set_obj_visible(source_choice_label, true);
    set_obj_visible(source_choice_row, true);
    set_obj_visible(source_label, false);
    set_obj_visible(source_dropdown, false);
    set_obj_visible(type_label, false);
    set_obj_visible(type_dropdown, false);
    set_obj_visible(path_label, false);
    set_obj_visible(smartcard_pin_label, false);
    set_obj_visible(generate_btn, false);
    set_text_input_visible(&path_input, false);
    set_text_input_visible(&smartcard_pin_input, false);
    hide_all_input_keyboards();
  }
}

void custom_derivation_page_hide(void) {
  if (page_screen)
    lv_obj_add_flag(page_screen, LV_OBJ_FLAG_HIDDEN);
  ui_text_input_hide(&path_input);
  ui_text_input_hide(&smartcard_pin_input);
}

void custom_derivation_page_destroy(void) {
  if (result_card) {
    lv_obj_del(result_card);
    result_card = NULL;
  }
  result_title = NULL;
  result_body = NULL;
  result_qr = NULL;
  if (source_dropdown) {
    lv_obj_del(source_dropdown);
    source_dropdown = NULL;
  }
  if (source_choice_row) {
    lv_obj_del(source_choice_row);
    source_choice_row = NULL;
  }
  source_choice_label = NULL;
  source_label = NULL;
  if (type_dropdown) {
    lv_obj_del(type_dropdown);
    type_dropdown = NULL;
  }
  type_label = NULL;
  path_label = NULL;
  smartcard_pin_label = NULL;
  ui_text_input_destroy(&path_input);
  ui_text_input_destroy(&smartcard_pin_input);
  if (generate_btn) {
    lv_obj_del(generate_btn);
    generate_btn = NULL;
  }
  generate_btn_label = NULL;
  if (back_btn) {
    lv_obj_del(back_btn);
    back_btn = NULL;
  }
  if (page_screen) {
    lv_obj_del(page_screen);
    page_screen = NULL;
  }
  return_callback = NULL;
  mnemonic_import_callback = NULL;
  source_option_count = 0;
  source_selected_index = 0;
  flow_step = CUSTOM_DERIV_FLOW_CHOOSE_SOURCE;
  selected_entry = CUSTOM_DERIV_ENTRY_NONE;
  source_dropdown_syncing = false;
}
