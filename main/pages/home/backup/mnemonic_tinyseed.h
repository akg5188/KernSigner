#ifndef MNEMONIC_TINYSEED_H
#define MNEMONIC_TINYSEED_H

#include <lvgl.h>

void mnemonic_tinyseed_page_create(lv_obj_t *parent, void (*return_cb)(void));
void mnemonic_tinyseed_page_show(void);
void mnemonic_tinyseed_page_hide(void);
void mnemonic_tinyseed_page_destroy(void);

#endif // MNEMONIC_TINYSEED_H
