#ifndef LOAD_PUNCH_GRID_H
#define LOAD_PUNCH_GRID_H

#include <lvgl.h>

void load_punch_grid_page_create(lv_obj_t *parent, void (*return_cb)(void),
                                 void (*success_cb)(void));
void load_punch_grid_page_show(void);
void load_punch_grid_page_hide(void);
void load_punch_grid_page_destroy(void);
void load_punch_grid_page_handle_completed_mnemonic(char *mnemonic);

#endif // LOAD_PUNCH_GRID_H
