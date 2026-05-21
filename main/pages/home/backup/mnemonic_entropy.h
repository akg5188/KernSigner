#ifndef MNEMONIC_ENTROPY_H
#define MNEMONIC_ENTROPY_H

#include <lvgl.h>

void mnemonic_entropy_page_create(lv_obj_t *parent, void (*return_cb)(void));
void mnemonic_entropy_page_show(void);
void mnemonic_entropy_page_hide(void);
void mnemonic_entropy_page_destroy(void);

#endif // MNEMONIC_ENTROPY_H
