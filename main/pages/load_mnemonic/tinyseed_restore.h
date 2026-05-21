#ifndef TINYSEED_RESTORE_H
#define TINYSEED_RESTORE_H

#include <lvgl.h>

void tinyseed_restore_page_create(lv_obj_t *parent, void (*return_cb)(void),
                                  void (*success_cb)(void));
void tinyseed_restore_page_show(void);
void tinyseed_restore_page_hide(void);
void tinyseed_restore_page_destroy(void);

#endif // TINYSEED_RESTORE_H
