#ifndef CUSTOM_DERIVATION_PAGE_H
#define CUSTOM_DERIVATION_PAGE_H

#include <lvgl.h>

void custom_derivation_page_create(lv_obj_t *parent, void (*return_cb)(void));
void custom_derivation_page_create_with_import(lv_obj_t *parent,
                                               void (*return_cb)(void),
                                               void (*mnemonic_import_cb)(void));
void custom_derivation_page_show(void);
void custom_derivation_page_hide(void);
void custom_derivation_page_destroy(void);

#endif // CUSTOM_DERIVATION_PAGE_H
