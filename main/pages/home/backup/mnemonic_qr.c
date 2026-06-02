// Mnemonic QR Code Backup Page

#include "mnemonic_qr.h"
#include "../../../core/base43.h"
#include "../../../core/key.h"
#include "../../../i18n/i18n.h"
#include "../../../qr/encoder.h"
#include "../../../qr/viewer.h"
#include "../../../ui/dialog.h"
#include "../../../ui/input_helpers.h"
#include "../../../ui/theme.h"
#include "../../../utils/bip39_filter.h"
#include "../../shared/kef_encrypt_page.h"
#include <lvgl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wally_core.h>

#include "../../../utils/secure_mem.h"

#define GRID_INTERVAL_DEFAULT 5
#define GRID_INTERVAL_21 7
#define LEGEND_SIZE 28
#define LABEL_PAD 6
#define SHADE_OPACITY LV_OPA_70

static int get_grid_interval(int modules) {
  return (modules == 21) ? GRID_INTERVAL_21 : GRID_INTERVAL_DEFAULT;
}

typedef enum {
  QR_TYPE_PLAINTEXT = 0,
  QR_TYPE_SEEDQR = 1,
  QR_TYPE_COMPACT_SEEDQR = 2,
  QR_TYPE_ENCRYPTED = 3
} qr_type_t;

static lv_obj_t *mnemonic_qr_screen = NULL;
static lv_obj_t *back_button = NULL;
static lv_obj_t *qr_type_dropdown = NULL;
static lv_obj_t *grid_btn = NULL;
static lv_obj_t *fullscreen_btn = NULL;
static lv_obj_t *qr_code = NULL;
static lv_obj_t *qr_container = NULL;
static lv_obj_t *qr_status_label = NULL;
static lv_obj_t *grid_overlay = NULL;
static lv_obj_t *content_area = NULL;
static lv_obj_t *fingerprint_label = NULL;
static lv_obj_t *index_label = NULL;
static lv_obj_t *shade_overlay = NULL;
static lv_obj_t **col_labels = NULL;
static lv_obj_t **row_labels = NULL;
static void (*return_callback)(void) = NULL;
static char *mnemonic_data = NULL;
static char *seedqr_data = NULL;
static unsigned char *compact_seedqr_data = NULL;
static size_t compact_seedqr_len = 0;
static qr_type_t current_qr_type = QR_TYPE_PLAINTEXT;
static bool grid_visible = false;
static bool shade_mode_active = false;
static int32_t qr_widget_size = 0;
static int shade_region_index = 0;
static int grid_divisions = 0;
static qr_encode_result_t last_qr_result = {0, 0};

/* Encrypted QR state */
static char *encrypted_qr_data = NULL;
static qr_type_t previous_qr_type = QR_TYPE_PLAINTEXT;
static bool qr_viewer_active = false;

/* Forward declaration */
static void update_qr_code(void);

static void build_mnemonic_index_summary(const char *mnemonic, char *out,
                                         size_t out_len) {
  if (!out || out_len == 0)
    return;
  out[0] = '\0';
  if (!mnemonic)
    return;

  char copy[256];
  snprintf(copy, sizeof(copy), "%s", mnemonic);

  bool wordlist_ready = bip39_filter_init();
  size_t pos = 0;
  int word_count = 0;
  char *token = strtok(copy, " ");
  while (token && pos < out_len - 1) {
    int word_index = wordlist_ready ? bip39_filter_get_word_index(token) : -1;
    int written = 0;
    if (word_index >= 0) {
      written = snprintf(out + pos, out_len - pos, "%s%04d",
                         word_count > 0 ? " " : "", word_index);
    } else {
      written = snprintf(out + pos, out_len - pos, "%s%s",
                         word_count > 0 ? " " : "",
                         i18n_tr_or("common.unknown", "Unknown"));
    }
    if (written < 0)
      break;
    if ((size_t)written >= out_len - pos) {
      pos = out_len - 1;
      break;
    }
    pos += (size_t)written;
    word_count++;
    token = strtok(NULL, " ");
  }

  secure_memzero(copy, sizeof(copy));
}

static void back_cb(lv_event_t *e) {
  (void)e;
  if (return_callback)
    return_callback();
}

static void destroy_grid_overlay(void) {
  if (grid_overlay) {
    lv_obj_del(grid_overlay);
    grid_overlay = NULL;
  }
  free(col_labels);
  col_labels = NULL;
  free(row_labels);
  row_labels = NULL;
  grid_divisions = 0;
}

static void update_grid_label_highlight(int highlight_row, int highlight_col) {
  if (!col_labels || !row_labels || grid_divisions == 0)
    return;

  lv_color_t normal_color = highlight_color();
  lv_color_t active_color = lv_color_hex(0xFFFFFF);

  for (int i = 0; i < grid_divisions; i++) {
    if (col_labels[i])
      lv_obj_set_style_text_color(
          col_labels[i], (i == highlight_col) ? active_color : normal_color, 0);
    if (row_labels[i])
      lv_obj_set_style_text_color(
          row_labels[i], (i == highlight_row) ? active_color : normal_color, 0);
  }
}

static void destroy_shade_overlay(void) {
  if (shade_overlay) {
    lv_obj_del(shade_overlay);
    shade_overlay = NULL;
  }
}

static void reset_shade_mode(void) {
  update_grid_label_highlight(-1, -1);
  destroy_shade_overlay();
  shade_mode_active = false;
  shade_region_index = 0;
}

static void add_shade_rect(int32_t x, int32_t y, int32_t w, int32_t h) {
  lv_obj_t *rect = lv_obj_create(shade_overlay);
  lv_obj_remove_style_all(rect);
  lv_obj_clear_flag(rect, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(rect, x, y);
  lv_obj_set_size(rect, w, h);
  lv_obj_set_style_bg_color(rect, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(rect, SHADE_OPACITY, 0);
}

static void create_shade_overlay(void) {
  destroy_shade_overlay();

  int modules = last_qr_result.modules;
  int32_t scale = last_qr_result.scale;
  if (modules == 0 || scale == 0)
    return;

  int grid_interval = get_grid_interval(modules);
  int divisions = (modules + grid_interval - 1) / grid_interval;
  int row = shade_region_index / divisions;
  int col = shade_region_index % divisions;

  int32_t content_size = modules * scale;
  int32_t margin = (qr_widget_size - content_size) / 2;
  int32_t cell_px = scale * grid_interval;

  lv_obj_update_layout(qr_code);
  lv_area_t qr_coords, container_coords, content_coords;
  lv_obj_get_coords(qr_code, &qr_coords);
  lv_obj_get_coords(qr_container, &container_coords);
  lv_obj_get_coords(content_area, &content_coords);

  int32_t qr_x = qr_coords.x1 - content_coords.x1 + margin;
  int32_t qr_y = qr_coords.y1 - content_coords.y1 + margin;
  int32_t cont_x = container_coords.x1 - content_coords.x1;
  int32_t cont_y = container_coords.y1 - content_coords.y1;
  int32_t cont_size = lv_obj_get_width(qr_container);

  int32_t win_x = qr_x + col * cell_px;
  int32_t win_y = qr_y + row * cell_px;
  int32_t win_w = cell_px;
  int32_t win_h = cell_px;

  if (win_x + win_w > qr_x + content_size)
    win_w = qr_x + content_size - win_x;
  if (win_y + win_h > qr_y + content_size)
    win_h = qr_y + content_size - win_y;

  shade_overlay = lv_obj_create(content_area);
  lv_obj_remove_style_all(shade_overlay);
  lv_obj_set_size(shade_overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_clear_flag(shade_overlay,
                    LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(shade_overlay, LV_OBJ_FLAG_IGNORE_LAYOUT);

  if (grid_overlay)
    lv_obj_move_foreground(grid_overlay);

  if (win_y > cont_y)
    add_shade_rect(cont_x, cont_y, cont_size, win_y - cont_y);

  int32_t bottom_y = win_y + win_h;
  if (bottom_y < cont_y + cont_size)
    add_shade_rect(cont_x, bottom_y, cont_size, cont_y + cont_size - bottom_y);

  if (win_x > cont_x)
    add_shade_rect(cont_x, win_y, win_x - cont_x, win_h);

  int32_t right_x = win_x + win_w;
  if (right_x < cont_x + cont_size)
    add_shade_rect(right_x, win_y, cont_x + cont_size - right_x, win_h);

  shade_mode_active = true;
  update_grid_label_highlight(row, col);
}

static void qr_area_tap_cb(lv_event_t *e) {
  (void)e;
  if (!grid_visible)
    return;

  int modules = last_qr_result.modules;
  int grid_interval = get_grid_interval(modules);
  int divisions = (modules + grid_interval - 1) / grid_interval;
  int total_regions = divisions * divisions;

  if (!shade_mode_active) {
    shade_region_index = 0;
    create_shade_overlay();
  } else {
    shade_region_index++;
    if (shade_region_index >= total_regions)
      reset_shade_mode();
    else
      create_shade_overlay();
  }
}

static void create_grid_overlay(void) {
  destroy_grid_overlay();

  int modules = last_qr_result.modules;
  int32_t scale = last_qr_result.scale;
  if (modules == 0 || scale == 0)
    return;

  int32_t content_size = modules * scale;
  int32_t margin = (qr_widget_size - content_size) / 2;

  lv_obj_update_layout(qr_code);
  lv_area_t qr_coords, content_coords;
  lv_obj_get_coords(qr_code, &qr_coords);
  lv_obj_get_coords(content_area, &content_coords);

  int32_t qr_x = qr_coords.x1 - content_coords.x1 + margin;
  int32_t qr_y = qr_coords.y1 - content_coords.y1 + margin;

  grid_overlay = lv_obj_create(content_area);
  lv_obj_remove_style_all(grid_overlay);
  lv_obj_set_size(grid_overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_clear_flag(grid_overlay,
                    LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(grid_overlay, LV_OBJ_FLAG_IGNORE_LAYOUT);

  lv_color_t color = highlight_color();
  int grid_interval = get_grid_interval(modules);
  int divisions = (modules + grid_interval - 1) / grid_interval;
  int32_t cell_px = scale * grid_interval;

  grid_divisions = divisions;
  col_labels = calloc(divisions, sizeof(lv_obj_t *));
  row_labels = calloc(divisions, sizeof(lv_obj_t *));

  for (int c = 0; c <= divisions; c++) {
    int32_t mod = (c * grid_interval > modules) ? modules : c * grid_interval;
    int32_t x = qr_x + mod * scale;

    lv_obj_t *line = lv_obj_create(grid_overlay);
    lv_obj_remove_style_all(line);
    lv_obj_set_size(line, 2, content_size);
    lv_obj_set_pos(line, x - 1, qr_y);
    lv_obj_set_style_bg_color(line, color, 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);

    if (c < divisions) {
      char txt[4];
      snprintf(txt, sizeof(txt), "%d", c);
      lv_obj_t *lbl = lv_label_create(grid_overlay);
      lv_label_set_text(lbl, txt);
      lv_obj_set_style_text_color(lbl, color, 0);
      lv_obj_set_style_text_font(lbl, theme_font_small(), 0);
      lv_obj_update_layout(lbl);
      lv_obj_set_pos(lbl, x + (cell_px - lv_obj_get_width(lbl)) / 2,
                     qr_y - LABEL_PAD - lv_obj_get_height(lbl));
      if (col_labels)
        col_labels[c] = lbl;
    }
  }

  for (int r = 0; r <= divisions; r++) {
    int32_t mod = (r * grid_interval > modules) ? modules : r * grid_interval;
    int32_t y = qr_y + mod * scale;

    lv_obj_t *line = lv_obj_create(grid_overlay);
    lv_obj_remove_style_all(line);
    lv_obj_set_size(line, content_size, 2);
    lv_obj_set_pos(line, qr_x, y - 1);
    lv_obj_set_style_bg_color(line, color, 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);

    if (r < divisions) {
      char txt[2] = {(char)('A' + r), '\0'};
      lv_obj_t *lbl = lv_label_create(grid_overlay);
      lv_label_set_text(lbl, txt);
      lv_obj_set_style_text_color(lbl, color, 0);
      lv_obj_set_style_text_font(lbl, theme_font_small(), 0);
      lv_obj_update_layout(lbl);
      lv_obj_set_pos(lbl, qr_x - LABEL_PAD - lv_obj_get_width(lbl),
                     y + (cell_px - lv_obj_get_height(lbl)) / 2);
      if (row_labels)
        row_labels[r] = lbl;
    }
  }
}

static void grid_btn_cb(lv_event_t *e) {
  (void)e;
  grid_visible = !grid_visible;
  if (grid_visible) {
    create_grid_overlay();
  } else {
    reset_shade_mode();
    destroy_grid_overlay();
  }
}

static const char *current_qr_title(void) {
  switch (current_qr_type) {
  case QR_TYPE_PLAINTEXT:
    return i18n_tr_or("backup.plaintext_backup", "Plaintext backup");
  case QR_TYPE_SEEDQR:
    return i18n_tr_or("backup.seedqr", "SeedQR");
  case QR_TYPE_COMPACT_SEEDQR:
    return i18n_tr_or("backup.compact_seedqr", "Compact QR");
  case QR_TYPE_ENCRYPTED:
    return i18n_tr_or("backup.encrypted_backup", "Encrypted backup");
  default:
    return i18n_tr_or("backup.qr", "Backup QR");
  }
}

static const char *current_text_qr_data(void) {
  switch (current_qr_type) {
  case QR_TYPE_PLAINTEXT:
    return mnemonic_data;
  case QR_TYPE_SEEDQR:
    return seedqr_data;
  case QR_TYPE_ENCRYPTED:
    return encrypted_qr_data;
  default:
    return NULL;
  }
}

static void return_from_print_qr_cb(void) {
  qr_viewer_page_destroy();
  qr_viewer_active = false;
  if (mnemonic_qr_screen)
    lv_obj_clear_flag(mnemonic_qr_screen, LV_OBJ_FLAG_HIDDEN);
  if (back_button)
    lv_obj_clear_flag(back_button, LV_OBJ_FLAG_HIDDEN);
}

static void open_fullscreen_qr(void) {
  if (!mnemonic_qr_screen)
    return;

  const char *data = current_text_qr_data();
  if (!data || data[0] == '\0') {
    if (current_qr_type == QR_TYPE_COMPACT_SEEDQR) {
      dialog_show_error(
          i18n_tr_or("backup.compact_seedqr_no_print",
                     "Compact QR is not suitable for print; choose SeedQR"),
          NULL, 2000);
    } else {
      dialog_show_error(i18n_tr_or("backup.no_display_data",
                                   "No data to display"),
                        NULL, 2000);
    }
    return;
  }

  if (!qr_viewer_page_create_print(lv_screen_active(), data, current_qr_title(),
                                   return_from_print_qr_cb, 180)) {
    dialog_show_error(i18n_tr_or("backup.fullscreen_qr_failed",
                                 "Failed to create fullscreen QR"),
                      NULL, 2000);
    return;
  }

  qr_viewer_active = true;
  lv_obj_add_flag(mnemonic_qr_screen, LV_OBJ_FLAG_HIDDEN);
  if (back_button)
    lv_obj_add_flag(back_button, LV_OBJ_FLAG_HIDDEN);
  qr_viewer_page_show();
}

static void fullscreen_btn_cb(lv_event_t *e) {
  (void)e;
  open_fullscreen_qr();
}

/* ---------- Encrypted QR flow (via kef_encrypt_page) ---------- */

static void encrypt_return_cb(void) {
  kef_encrypt_page_destroy();
  current_qr_type = previous_qr_type;
  lv_dropdown_set_selected(qr_type_dropdown, (uint32_t)current_qr_type);
}

static void encrypt_success_cb(const char *id, const uint8_t *envelope,
                               size_t len) {
  (void)id;
  char *b43 = NULL;
  size_t b43_len = 0;
  if (!base43_encode(envelope, len, &b43, &b43_len)) {
    kef_encrypt_page_destroy();
    dialog_show_error(i18n_tr_or("backup.encoding_failed", "Encoding failed"),
                      NULL, 0);
    current_qr_type = previous_qr_type;
    lv_dropdown_set_selected(qr_type_dropdown, (uint32_t)current_qr_type);
    return;
  }

  kef_encrypt_page_destroy();
  SECURE_FREE_STRING(encrypted_qr_data);
  encrypted_qr_data = b43;

  current_qr_type = QR_TYPE_ENCRYPTED;
  lv_dropdown_set_selected(qr_type_dropdown, 3);
  update_qr_code();
  open_fullscreen_qr();
}

static void start_encrypted_flow(void) {
  previous_qr_type = current_qr_type;

  if (!compact_seedqr_data || compact_seedqr_len == 0) {
    dialog_show_error(i18n_tr_or("backup.no_encryptable_data",
                                 "No encryptable data"),
                      NULL, 0);
    return;
  }

  kef_encrypt_page_create(lv_screen_active(), encrypt_return_cb,
                          encrypt_success_cb, compact_seedqr_data,
                          compact_seedqr_len, NULL);
}

static void update_qr_code(void) {
  if (!qr_code)
    return;

  lv_result_t result = LV_RESULT_INVALID;
  last_qr_result = (qr_encode_result_t){0, 0};

  if (current_qr_type == QR_TYPE_COMPACT_SEEDQR) {
    if (compact_seedqr_data && compact_seedqr_len > 0)
      result = qr_update_binary(qr_code, compact_seedqr_data,
                                compact_seedqr_len, &last_qr_result);
  } else if (current_qr_type == QR_TYPE_ENCRYPTED) {
    if (encrypted_qr_data)
      result = qr_update_optimal(qr_code, encrypted_qr_data, &last_qr_result);
  } else {
    const char *data = (current_qr_type == QR_TYPE_PLAINTEXT) ? mnemonic_data
                       : (current_qr_type == QR_TYPE_SEEDQR)  ? seedqr_data
                                                              : NULL;
    if (data)
      result = qr_update_optimal(qr_code, data, &last_qr_result);
  }

  if (qr_status_label) {
    if (result == LV_RESULT_OK) {
      lv_label_set_text(qr_status_label, "");
      lv_obj_set_style_text_color(qr_status_label, secondary_color(), 0);
    } else if (current_qr_type == QR_TYPE_ENCRYPTED) {
      lv_label_set_text(qr_status_label,
                        i18n_tr_or("backup.content_long", "Content is long"));
      lv_obj_set_style_text_color(qr_status_label, highlight_color(), 0);
    } else {
      lv_label_set_text(qr_status_label,
                        i18n_tr_or("backup.qr_too_large", "QR is too large"));
      lv_obj_set_style_text_color(qr_status_label, highlight_color(), 0);
    }
  }

  reset_shade_mode();
  if (grid_visible)
    create_grid_overlay();
}

static void dropdown_cb(lv_event_t *e) {
  uint32_t sel = lv_dropdown_get_selected(lv_event_get_target(e));
  if (sel == 3) {
    /* Encrypted: always trigger fresh flow (new GCM IV each time) */
    start_encrypted_flow();
    return;
  }
  qr_type_t new_type = (sel == 0)   ? QR_TYPE_PLAINTEXT
                       : (sel == 1) ? QR_TYPE_SEEDQR
                                    : QR_TYPE_COMPACT_SEEDQR;
  if (new_type != current_qr_type) {
    current_qr_type = new_type;
    update_qr_code();
  }
}

void mnemonic_qr_page_create(lv_obj_t *parent, void (*return_cb)(void)) {
  if (!parent || !key_is_loaded())
    return;

  return_callback = return_cb;

  if (!key_mnemonic_is_valid()) {
    dialog_show_error(
        i18n_tr_or("backup.no_temporary_seedqr",
                   "Temporary mnemonic cannot show mnemonic QR"),
        return_cb, 0);
    return;
  }

  if (!key_get_mnemonic(&mnemonic_data) || !mnemonic_data)
    return;

  seedqr_data = mnemonic_to_seedqr(mnemonic_data);
  compact_seedqr_data =
      mnemonic_to_compact_seedqr(mnemonic_data, &compact_seedqr_len);
  if (!seedqr_data || !compact_seedqr_data) {
    secure_memzero(mnemonic_data, strlen(mnemonic_data));
    wally_free_string(mnemonic_data);
    mnemonic_data = NULL;
    SECURE_FREE_STRING(seedqr_data);
    SECURE_FREE_BUFFER(compact_seedqr_data, compact_seedqr_len);
    compact_seedqr_len = 0;
    return;
  }

  current_qr_type = QR_TYPE_PLAINTEXT;
  grid_visible = false;

  mnemonic_qr_screen = lv_obj_create(parent);
  lv_obj_set_size(mnemonic_qr_screen, LV_PCT(100), LV_PCT(100));
  theme_apply_screen(mnemonic_qr_screen);
  lv_obj_clear_flag(mnemonic_qr_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(mnemonic_qr_screen, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(mnemonic_qr_screen, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(mnemonic_qr_screen, theme_get_default_padding(), 0);
  lv_obj_set_style_pad_gap(mnemonic_qr_screen, theme_get_small_padding(), 0);

  lv_obj_t *top_bar = lv_obj_create(mnemonic_qr_screen);
  lv_obj_set_size(top_bar, LV_PCT(100), theme_get_min_touch_size());
  lv_obj_set_style_bg_opa(top_bar, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(top_bar, 0, 0);
  lv_obj_set_style_pad_all(top_bar, 0, 0);
  lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);

  back_button = ui_create_back_button(parent, back_cb);

  char qr_type_options[128];
  snprintf(qr_type_options, sizeof(qr_type_options), "%s\n%s\n%s\n%s",
           i18n_tr_or("descriptor.plaintext_qr", "Plaintext"),
           i18n_tr_or("backup.seedqr", "SeedQR"),
           i18n_tr_or("backup.compact_seedqr", "Compact QR"),
           i18n_tr_or("backup.encrypted_qr", "Encrypted QR"));
  qr_type_dropdown = theme_create_dropdown(top_bar, qr_type_options);
  lv_obj_set_width(qr_type_dropdown, LV_PCT(58));
  lv_obj_align(qr_type_dropdown, LV_ALIGN_CENTER, -theme_get_min_touch_size() / 2,
               0);
  lv_obj_add_event_cb(qr_type_dropdown, dropdown_cb, LV_EVENT_VALUE_CHANGED,
                      NULL);

  grid_btn = lv_btn_create(top_bar);
  lv_obj_set_size(grid_btn, theme_get_min_touch_size(),
                  theme_get_min_touch_size());
  lv_obj_align_to(grid_btn, qr_type_dropdown, LV_ALIGN_OUT_RIGHT_MID,
                  theme_get_small_padding(), 0);
  theme_apply_touch_button(grid_btn, false);
  lv_obj_add_event_cb(grid_btn, grid_btn_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *grid_label = lv_label_create(grid_btn);
  lv_label_set_text(grid_label, "#");
  lv_obj_set_style_text_font(grid_label, theme_font_medium(), 0);
  lv_obj_set_style_text_color(grid_label, main_color(), 0);
  lv_obj_center(grid_label);

  fullscreen_btn = lv_btn_create(top_bar);
  lv_obj_set_size(fullscreen_btn, theme_get_min_touch_size() + 18,
                  theme_get_min_touch_size());
  lv_obj_align_to(fullscreen_btn, grid_btn, LV_ALIGN_OUT_RIGHT_MID,
                  theme_get_small_padding(), 0);
  theme_apply_touch_button(fullscreen_btn, true);
  lv_obj_add_event_cb(fullscreen_btn, fullscreen_btn_cb, LV_EVENT_CLICKED,
                      NULL);

  lv_obj_t *fullscreen_label = lv_label_create(fullscreen_btn);
  lv_label_set_text(fullscreen_label,
                    i18n_tr_or("common.fullscreen", "Full"));
  lv_obj_set_style_text_font(fullscreen_label, theme_font_small(), 0);
  lv_obj_set_style_text_color(fullscreen_label, bg_color(), 0);
  lv_obj_center(fullscreen_label);

  content_area = lv_obj_create(mnemonic_qr_screen);
  lv_obj_set_size(content_area, LV_PCT(100), 0);
  lv_obj_set_style_bg_opa(content_area, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(content_area, 0, 0);
  lv_obj_set_style_pad_all(content_area, 0, 0);
  lv_obj_set_flex_grow(content_area, 1);
  lv_obj_clear_flag(content_area, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(content_area, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(content_area, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_update_layout(mnemonic_qr_screen);
  lv_obj_update_layout(content_area);
  int32_t avail_w = lv_obj_get_content_width(content_area);
  int32_t avail_h = lv_obj_get_content_height(content_area);
  int32_t meta_reserve = theme_get_screen_height() >= 760 ? 70 : 54;
  int32_t max_qr_h = avail_h > meta_reserve ? avail_h - meta_reserve : avail_h;
  int32_t container_size = (avail_w < max_qr_h) ? avail_w : max_qr_h;

  qr_container = lv_obj_create(content_area);
  lv_obj_set_size(qr_container, container_size, container_size);
  lv_obj_set_style_bg_color(qr_container, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(qr_container, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(qr_container, 0, 0);
  lv_obj_set_style_pad_all(qr_container, LEGEND_SIZE, 0);
  lv_obj_set_style_radius(qr_container, 0, 0);
  lv_obj_clear_flag(qr_container, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_update_layout(qr_container);
  qr_widget_size = lv_obj_get_content_width(qr_container);

  qr_code = lv_qrcode_create(qr_container);
  lv_qrcode_set_size(qr_code, qr_widget_size);
  lv_obj_center(qr_code);

  lv_obj_add_flag(qr_container, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(qr_container, qr_area_tap_cb, LV_EVENT_CLICKED, NULL);

  char fingerprint_hex[9] = "--------";
  key_get_mnemonic_fingerprint_hex(fingerprint_hex);
  char fp_text[32];
  snprintf(fp_text, sizeof(fp_text), "%s %s",
           i18n_tr_or("wallet.mnemonic_fingerprint", "Mnemonic fingerprint"),
           fingerprint_hex);
  fingerprint_label = theme_create_label(content_area, fp_text, false);
  lv_obj_set_style_text_font(fingerprint_label, theme_font_small(), 0);
  lv_obj_set_style_text_color(fingerprint_label, highlight_color(), 0);
  lv_obj_set_style_text_align(fingerprint_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(fingerprint_label, LV_PCT(100));

  char index_text[160];
  char index_summary[128];
  build_mnemonic_index_summary(mnemonic_data, index_summary,
                               sizeof(index_summary));
  snprintf(index_text, sizeof(index_text), "%s %s",
           i18n_tr_or("backup.word_indexes", "Word indexes"), index_summary);
  index_label = theme_create_label(content_area, index_text, false);
  lv_obj_set_style_text_font(index_label, theme_font_small(), 0);
  lv_obj_set_style_text_color(index_label, secondary_color(), 0);
  lv_obj_set_style_text_align(index_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(index_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(index_label, LV_PCT(100));

  qr_status_label = theme_create_label(content_area, "", false);
  lv_obj_set_style_text_font(qr_status_label, theme_font_small(), 0);
  lv_obj_set_style_text_color(qr_status_label, secondary_color(), 0);
  lv_obj_set_style_text_align(qr_status_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(qr_status_label, LV_PCT(100));

  update_qr_code();
}

void mnemonic_qr_page_show(void) {
  if (mnemonic_qr_screen)
    lv_obj_clear_flag(mnemonic_qr_screen, LV_OBJ_FLAG_HIDDEN);
}

void mnemonic_qr_page_hide(void) {
  if (mnemonic_qr_screen)
    lv_obj_add_flag(mnemonic_qr_screen, LV_OBJ_FLAG_HIDDEN);
}

void mnemonic_qr_page_destroy(void) {
  kef_encrypt_page_destroy();
  if (qr_viewer_active) {
    qr_viewer_page_destroy();
    qr_viewer_active = false;
  }

  reset_shade_mode();
  destroy_grid_overlay();

  if (mnemonic_data) {
    secure_memzero(mnemonic_data, strlen(mnemonic_data));
    wally_free_string(mnemonic_data);
    mnemonic_data = NULL;
  }

  SECURE_FREE_STRING(seedqr_data);
  SECURE_FREE_BUFFER(compact_seedqr_data, compact_seedqr_len);
  compact_seedqr_len = 0;
  SECURE_FREE_STRING(encrypted_qr_data);

  if (back_button) {
    lv_obj_del(back_button);
    back_button = NULL;
  }

  if (mnemonic_qr_screen) {
    lv_obj_del(mnemonic_qr_screen);
    mnemonic_qr_screen = NULL;
  }

  qr_type_dropdown = NULL;
  grid_btn = NULL;
  fullscreen_btn = NULL;
  qr_code = NULL;
  qr_container = NULL;
  qr_status_label = NULL;
  content_area = NULL;
  fingerprint_label = NULL;
  index_label = NULL;
  return_callback = NULL;
  current_qr_type = QR_TYPE_PLAINTEXT;
  grid_visible = false;
  qr_widget_size = 0;
  last_qr_result = (qr_encode_result_t){0, 0};
}
