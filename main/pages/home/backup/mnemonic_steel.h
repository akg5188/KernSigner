#ifndef MNEMONIC_STEEL_H
#define MNEMONIC_STEEL_H

#include <lvgl.h>

void mnemonic_steel_page_create(lv_obj_t *parent, void (*return_cb)(void));
void mnemonic_steel_page_show(void);
void mnemonic_steel_page_hide(void);
void mnemonic_steel_page_destroy(void);

#endif // MNEMONIC_STEEL_H
