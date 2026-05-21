// PIN settings page — manage PIN, timeout, and shutdown protection threshold.

#ifndef PIN_SETTINGS_H
#define PIN_SETTINGS_H

#include <lvgl.h>

void pin_settings_page_create(lv_obj_t *parent, void (*return_cb)(void));
void pin_settings_page_show(void);
void pin_settings_page_hide(void);
void pin_settings_page_destroy(void);

#endif // PIN_SETTINGS_H
