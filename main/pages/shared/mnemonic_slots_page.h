#ifndef MNEMONIC_SLOTS_PAGE_H
#define MNEMONIC_SLOTS_PAGE_H

#include <lvgl.h>

void mnemonic_slots_page_create(lv_obj_t *parent, void (*return_cb)(void),
                                void (*success_cb)(void));
void mnemonic_slots_page_show(void);
void mnemonic_slots_page_hide(void);
void mnemonic_slots_page_destroy(void);

#endif // MNEMONIC_SLOTS_PAGE_H
