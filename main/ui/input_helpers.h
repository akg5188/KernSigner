// UI Input Helpers - Shared components for input pages

#ifndef INPUT_HELPERS_H
#define INPUT_HELPERS_H

#include <lvgl.h>
#include <stdbool.h>

// Shared text input: textarea + optional eye toggle + keyboard
typedef struct {
  lv_obj_t *textarea;
  lv_obj_t *eye_btn;
  lv_obj_t *eye_label;
  lv_obj_t *keyboard;
  lv_group_t *input_group;
} ui_text_input_t;

// Creates textarea + eye toggle (if password_mode) + keyboard with dark theme.
// Struct is caller-owned; reusable after destroy.
void ui_text_input_create(ui_text_input_t *input, lv_obj_t *parent,
                          const char *placeholder, bool password_mode,
                          lv_event_cb_t ready_cb);
void ui_text_input_show(ui_text_input_t *input);
void ui_text_input_hide(ui_text_input_t *input);
void ui_text_input_destroy(ui_text_input_t *input);

// Applies the firmware-safe text keyboard maps; no icon glyphs are required.
void ui_keyboard_apply_safe_text_map(lv_obj_t *keyboard);

// Makes Chinese safe-keyboard labels act as functions if a default LVGL
// keyboard inserts them before our keyboard callback gets control.
void ui_textarea_enable_safe_keyboard_shortcuts(lv_obj_t *textarea);

// Creates back button at top-left.
lv_obj_t *ui_create_back_button(lv_obj_t *parent, lv_event_cb_t event_cb);

// Creates home button at top-left.
lv_obj_t *ui_create_home_button(lv_obj_t *parent, lv_event_cb_t event_cb);

// Creates power button at top-left.
lv_obj_t *ui_create_power_button(lv_obj_t *parent, lv_event_cb_t event_cb);

// Creates settings button at top-right.
lv_obj_t *ui_create_settings_button(lv_obj_t *parent, lv_event_cb_t event_cb);

// Dialog confirm callback: attempts PMIC power-off with fallback.
// If unload_key is true (passed as user_data), unloads wallet first and
// falls back to esp_restart(). Otherwise shows an error on failure.
void ui_power_off_confirmed_cb(bool confirmed, void *user_data);

#endif
