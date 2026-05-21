#ifndef STACKBIT_RESTORE_H
#define STACKBIT_RESTORE_H

#include <lvgl.h>

void stackbit_restore_page_create(lv_obj_t *parent, void (*return_cb)(void),
                                  void (*success_cb)(void));
void stackbit_restore_page_show(void);
void stackbit_restore_page_hide(void);
void stackbit_restore_page_destroy(void);

#endif // STACKBIT_RESTORE_H
