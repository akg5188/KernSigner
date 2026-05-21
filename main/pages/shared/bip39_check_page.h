#ifndef BIP39_CHECK_PAGE_H
#define BIP39_CHECK_PAGE_H

#include <lvgl.h>

void bip39_check_page_create(lv_obj_t *parent, void (*return_cb)(void));
void bip39_check_page_show(void);
void bip39_check_page_hide(void);
void bip39_check_page_destroy(void);

#endif // BIP39_CHECK_PAGE_H
