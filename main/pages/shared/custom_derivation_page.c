#include "custom_derivation_page.h"
#include "../../core/custom_derivation.h"
#include "../../core/key.h"
#include "../../core/wallet.h"
#include "../../ui/input_helpers.h"
#include "../../ui/theme.h"

#include <ctype.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

static lv_obj_t *page_screen = NULL;
static lv_obj_t *type_dropdown = NULL;
static lv_obj_t *result_card = NULL;
static lv_obj_t *result_title = NULL;
static lv_obj_t *result_body = NULL;
static lv_obj_t *result_qr = NULL;
static lv_obj_t *back_btn = NULL;
static lv_obj_t *generate_btn = NULL;
static ui_text_input_t input = {0};
static void (*return_callback)(void) = NULL;

static bool is_wave_43_portrait(void) {
  return theme_get_screen_width() <= 520 && theme_get_screen_height() >= 760;
}

static void back_cb(lv_event_t *e) {
  (void)e;
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
    return "BTC传统";
  case CUSTOM_ADDR_BTC_P2SH_P2WPKH:
    return "BTC隔离";
  case CUSTOM_ADDR_BTC_P2WPKH:
    return "BTC原生";
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

static void ensure_result_card(void) {
  if (result_card || !page_screen)
    return;

  result_card = lv_obj_create(page_screen);
  lv_obj_set_width(result_card, LV_PCT(92));
  lv_obj_set_height(result_card, is_wave_43_portrait() ? 258 : 232);
  lv_obj_align(result_card, LV_ALIGN_BOTTOM_MID, 0, -theme_get_default_padding());
  lv_obj_set_style_bg_color(result_card, panel_color(), 0);
  lv_obj_set_style_bg_opa(result_card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(result_card, highlight_color(), 0);
  lv_obj_set_style_border_width(result_card, 2, 0);
  lv_obj_set_style_radius(result_card, 14, 0);
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
  lv_obj_set_style_text_color(result_title, highlight_color(), 0);
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

static void ready_cb(lv_event_t *e) {
  (void)e;
  const char *text = input.textarea ? lv_textarea_get_text(input.textarea) : "";
  custom_address_type_t type = selected_type();
  bool is_testnet = wallet_get_network() == WALLET_NETWORK_TESTNET;

  ensure_result_card();
  if (!result_title || !result_body)
    return;

  char path[128];
  normalize_path(path, sizeof(path),
                 (text && *text) ? text : default_path_for_type(type, is_testnet));
  if (input.textarea)
    lv_textarea_set_text(input.textarea, path);

  char address[128];

  if (!custom_derivation_get_address(path, type, is_testnet, address,
                                     sizeof(address))) {
    lv_label_set_text(result_title, "失败");
    lv_label_set_text(result_body, "路径或类型无效");
    if (result_qr)
      lv_obj_add_flag(result_qr, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  char body[320];
  snprintf(body, sizeof(body), "路径：%s\n类型：%s\n地址：%s", path,
           selected_type_name(type), address);
  lv_label_set_text(result_title, "结果");
  lv_label_set_text(result_body, body);
  if (result_qr) {
    lv_qrcode_update(result_qr, address, (uint32_t)strlen(address));
    lv_obj_clear_flag(result_qr, LV_OBJ_FLAG_HIDDEN);
  }

  if (input.keyboard)
    lv_obj_add_flag(input.keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void generate_btn_cb(lv_event_t *e) { ready_cb(e); }

static void input_focus_cb(lv_event_t *e) {
  (void)e;
  if (input.keyboard)
    lv_obj_clear_flag(input.keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void style_generate_button(lv_obj_t *btn) {
  if (!btn)
    return;
  lv_obj_set_style_bg_color(btn, panel_color(), 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_90, 0);
  lv_obj_set_style_border_color(btn, highlight_color(), 0);
  lv_obj_set_style_border_width(btn, 2, 0);
  lv_obj_set_style_radius(btn, 14, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
}

static void type_changed_cb(lv_event_t *e) {
  (void)e;
  if (!input.textarea)
    return;
  bool is_testnet = wallet_get_network() == WALLET_NETWORK_TESTNET;
  lv_textarea_set_text(input.textarea,
                       default_path_for_type(selected_type(), is_testnet));
}

void custom_derivation_page_create(lv_obj_t *parent, void (*return_cb)(void)) {
  if (!parent || !key_is_loaded())
    return;

  return_callback = return_cb;
  page_screen = theme_create_page_container(parent);
  lv_obj_set_style_bg_color(page_screen, bg_color(), 0);
  lv_obj_add_flag(page_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(page_screen, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(page_screen, LV_SCROLLBAR_MODE_OFF);
  theme_create_page_title(page_screen, "派生地址");
  back_btn = ui_create_back_button(page_screen, back_cb);

  type_dropdown =
      theme_create_dropdown(page_screen, "BTC传统\nBTC隔离\nBTC原生\nTaproot\nEVM");
  lv_obj_set_width(type_dropdown, LV_PCT(62));
  lv_obj_align(type_dropdown, LV_ALIGN_TOP_MID, 0, 88);
  lv_obj_add_event_cb(type_dropdown, type_changed_cb, LV_EVENT_VALUE_CHANGED,
                      NULL);

  ui_text_input_create(&input, page_screen, "m/...", false, ready_cb);
  if (input.textarea) {
    lv_textarea_set_one_line(input.textarea, false);
    lv_obj_set_width(input.textarea, LV_PCT(86));
    lv_obj_set_height(input.textarea, 58);
    lv_obj_align(input.textarea, LV_ALIGN_TOP_MID, 0, 142);
    lv_obj_add_event_cb(input.textarea, input_focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(input.textarea, input_focus_cb, LV_EVENT_CLICKED, NULL);
    lv_textarea_set_text(
        input.textarea,
        default_path_for_type(CUSTOM_ADDR_BTC_P2PKH,
                              wallet_get_network() == WALLET_NETWORK_TESTNET));
  }

  generate_btn = lv_btn_create(page_screen);
  lv_obj_set_size(generate_btn, LV_PCT(52), 48);
  lv_obj_align(generate_btn, LV_ALIGN_TOP_MID, 0, 206);
  style_generate_button(generate_btn);
  lv_obj_add_event_cb(generate_btn, generate_btn_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *label = lv_label_create(generate_btn);
  lv_label_set_text(label, "生成地址");
  lv_obj_set_style_text_font(label, theme_font_medium(), 0);
  lv_obj_set_style_text_color(label, highlight_color(), 0);
  lv_obj_center(label);
}

void custom_derivation_page_show(void) {
  if (page_screen)
    lv_obj_clear_flag(page_screen, LV_OBJ_FLAG_HIDDEN);
  ui_text_input_show(&input);
}

void custom_derivation_page_hide(void) {
  if (page_screen)
    lv_obj_add_flag(page_screen, LV_OBJ_FLAG_HIDDEN);
  ui_text_input_hide(&input);
}

void custom_derivation_page_destroy(void) {
  if (result_card) {
    lv_obj_del(result_card);
    result_card = NULL;
  }
  result_title = NULL;
  result_body = NULL;
  result_qr = NULL;
  if (type_dropdown) {
    lv_obj_del(type_dropdown);
    type_dropdown = NULL;
  }
  ui_text_input_destroy(&input);
  if (generate_btn) {
    lv_obj_del(generate_btn);
    generate_btn = NULL;
  }
  if (back_btn) {
    lv_obj_del(back_btn);
    back_btn = NULL;
  }
  if (page_screen) {
    lv_obj_del(page_screen);
    page_screen = NULL;
  }
  return_callback = NULL;
}
