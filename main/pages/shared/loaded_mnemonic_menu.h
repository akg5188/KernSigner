#ifndef LOADED_MNEMONIC_MENU_H
#define LOADED_MNEMONIC_MENU_H

#include <lvgl.h>

void loaded_mnemonic_menu_page_create(lv_obj_t *parent, void (*return_cb)(void));
void loaded_mnemonic_menu_page_show(void);
void loaded_mnemonic_menu_page_hide(void);
void loaded_mnemonic_menu_page_destroy(void);

#endif // LOADED_MNEMONIC_MENU_H
