#ifndef MNEMONIC_GRID_H
#define MNEMONIC_GRID_H

#include <lvgl.h>

void mnemonic_grid_page_create(lv_obj_t *parent, void (*return_cb)(void));
void mnemonic_grid_page_show(void);
void mnemonic_grid_page_hide(void);
void mnemonic_grid_page_destroy(void);

void mnemonic_grid_numbers_page_create(lv_obj_t *parent,
                                       void (*return_cb)(void));
void mnemonic_grid_numbers_page_show(void);
void mnemonic_grid_numbers_page_hide(void);
void mnemonic_grid_numbers_page_destroy(void);

#endif // MNEMONIC_GRID_H
